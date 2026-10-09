// SPDX-License-Identifier: GPL-3.0-or-later
/* A stand-in for polkitd's org.freedesktop.PolicyKit1 Authority on a private bus, so the shell's
 * authentication agent can be tested without polkit asking for anyone's password: it takes an
 * agent's registration and sends it requests as polkitd does when a program asks to be
 * authorized.
 *
 *     fake_polkitd ADDRESS [--taken]
 *
 * ADDRESS is the bus to serve on. With --taken, registering fails as when another agent serves the
 * session. It prints a line for each event: "ready", "registered KIND ID PATH" (the subject's kind
 * and its session or process, and the agent's object path), "unregistered PATH", "reply COOKIE ok"
 * or "reply COOKIE error NAME" (the agent's answer to a request), and "cancelled COOKIE". It reads
 * commands, one a line: "begin ACTION COOKIE IDENTITY..." sends the agent a request, its users or
 * groups written as "user:UID" or "group:GID", the message saying what is asked and pkexec's
 * details naming a command; "cancel COOKIE" withdraws one. */
#include <gio/gio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char introspection[] =
    "<node><interface name='org.freedesktop.PolicyKit1.Authority'>"
    "<method name='RegisterAuthenticationAgent'><arg type='(sa{sv})' direction='in'/>"
    "<arg type='s' direction='in'/><arg type='s' direction='in'/></method>"
    "<method name='RegisterAuthenticationAgentWithOptions'><arg type='(sa{sv})' direction='in'/>"
    "<arg type='s' direction='in'/><arg type='s' direction='in'/>"
    "<arg type='a{sv}' direction='in'/></method>"
    "<method name='UnregisterAuthenticationAgent'><arg type='(sa{sv})' direction='in'/>"
    "<arg type='s' direction='in'/></method>"
    "<property name='BackendName' type='s' access='read'/>"
    "<property name='BackendVersion' type='s' access='read'/>"
    "<property name='BackendFeatures' type='u' access='read'/>"
    "</interface></node>";

static GDBusConnection *bus;
static gboolean taken;
static char *agent_name, *agent_path; /* the registered agent's bus name and object */

static void say(const char *format, ...) G_GNUC_PRINTF(1, 2);
static void say(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
}

/* "unix-session ID" or "unix-process PID" for a subject. */
static char *describe_subject(GVariant *subject) {
    const char *kind;
    GVariant *details;
    g_variant_get(subject, "(&s@a{sv})", &kind, &details);
    char *id = NULL;
    GVariant *value;
    if ((value = g_variant_lookup_value(details, "session-id", G_VARIANT_TYPE_STRING))) {
        id = g_strdup(g_variant_get_string(value, NULL));
        g_variant_unref(value);
    } else if ((value = g_variant_lookup_value(details, "pid", G_VARIANT_TYPE_UINT32))) {
        id = g_strdup_printf("%u", g_variant_get_uint32(value));
        g_variant_unref(value);
    }
    char *text = g_strdup_printf("%s %s", kind, id ? id : "-");
    g_free(id);
    g_variant_unref(details);
    return text;
}

static void method_call(GDBusConnection *connection, const char *sender, const char *path,
                        const char *interface, const char *method, GVariant *parameters,
                        GDBusMethodInvocation *invocation, gpointer data) {
    if (!strcmp(method, "UnregisterAuthenticationAgent")) {
        const char *object;
        g_variant_get(parameters, "(@(sa{sv})&s)", NULL, &object);
        say("unregistered %s", object);
        g_clear_pointer(&agent_name, g_free);
        g_clear_pointer(&agent_path, g_free);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    /* Either way of registering. */
    GVariant *subject = g_variant_get_child_value(parameters, 0);
    const char *object;
    g_variant_get_child(parameters, 2, "&s", &object);
    if (taken || agent_name) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.freedesktop.PolicyKit1.Error.Failed",
            "An authentication agent already exists for the given subject");
        g_variant_unref(subject);
        return;
    }
    char *who = describe_subject(subject);
    say("registered %s %s", who, object);
    g_free(who);
    g_variant_unref(subject);
    agent_name = g_strdup(sender);
    agent_path = g_strdup(object);
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant *get_property(GDBusConnection *connection, const char *sender, const char *path,
                              const char *interface, const char *property, GError **error,
                              gpointer data) {
    if (!strcmp(property, "BackendFeatures"))
        return g_variant_new_uint32(0);
    return g_variant_new_string(!strcmp(property, "BackendName") ? "fake" : "1");
}

static void replied(GObject *source, GAsyncResult *result, gpointer data) {
    char *cookie = data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (reply) {
        say("reply %s ok", cookie);
        g_variant_unref(reply);
    } else {
        char *name = g_dbus_error_get_remote_error(error);
        say("reply %s error %s", cookie, name ? name : error->message);
        g_free(name);
        g_error_free(error);
    }
    g_free(cookie);
}

/* "begin ACTION COOKIE IDENTITY..." or "cancel COOKIE". */
static void command(char *line) {
    char **words = g_strsplit(g_strstrip(line), " ", -1);
    guint count = g_strv_length(words);
    if (!agent_name) {
        say("no agent");
    } else if (count >= 3 && !strcmp(words[0], "begin")) {
        GVariantBuilder details, identities;
        g_variant_builder_init(&details, G_VARIANT_TYPE("a{ss}"));
        g_variant_builder_add(&details, "{ss}", "program", "/usr/bin/true");
        g_variant_builder_add(&details, "{ss}", "command_line", "/usr/bin/true --test");
        g_variant_builder_init(&identities, G_VARIANT_TYPE("a(sa{sv})"));
        for (guint i = 3; i < count; ++i) {
            gboolean group = g_str_has_prefix(words[i], "group:");
            guint32 id = (guint32)strtoul(strchr(words[i], ':') + 1, NULL, 10);
            GVariantBuilder fields;
            g_variant_builder_init(&fields, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&fields, "{sv}", group ? "gid" : "uid", g_variant_new_uint32(id));
            g_variant_builder_add(&identities, "(sa{sv})", group ? "unix-group" : "unix-user",
                                  &fields);
        }
        g_dbus_connection_call(
            bus, agent_name, agent_path, "org.freedesktop.PolicyKit1.AuthenticationAgent",
            "BeginAuthentication",
            g_variant_new("(sssa{ss}sa(sa{sv}))", words[1],
                          "Authentication is needed to run `/usr/bin/true' as the super user",
                          "dialog-password", &details, words[2], &identities),
            NULL, G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, replied, g_strdup(words[2]));
    } else if (count == 2 && !strcmp(words[0], "cancel")) {
        GError *error = NULL;
        GVariant *reply = g_dbus_connection_call_sync(
            bus, agent_name, agent_path, "org.freedesktop.PolicyKit1.AuthenticationAgent",
            "CancelAuthentication", g_variant_new("(s)", words[1]), NULL, G_DBUS_CALL_FLAGS_NONE,
            5000, NULL, &error);
        if (reply)
            g_variant_unref(reply);
        say("cancelled %s%s%s", words[1], error ? " error " : "", error ? error->message : "");
        g_clear_error(&error);
    } else {
        say("unknown command");
    }
    g_strfreev(words);
}

static gboolean readable(GIOChannel *channel, GIOCondition condition, gpointer data) {
    char *line = NULL;
    GIOStatus status = g_io_channel_read_line(channel, &line, NULL, NULL, NULL);
    if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR) {
        g_main_loop_quit(data);
        return G_SOURCE_REMOVE;
    }
    if (line)
        command(line);
    g_free(line);
    return G_SOURCE_CONTINUE;
}

static void acquired(GDBusConnection *connection, const char *name, gpointer data) {
    say("ready");
}

static void lost(GDBusConnection *connection, const char *name, gpointer data) {
    say("lost the name");
    g_main_loop_quit(data);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: fake_polkitd ADDRESS [--taken]\n");
        return 2;
    }
    taken = argc > 2 && !strcmp(argv[2], "--taken");
    GError *error = NULL;
    bus = g_dbus_connection_new_for_address_sync(
        argv[1],
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, &error);
    if (!bus) {
        fprintf(stderr, "fake_polkitd: %s\n", error->message);
        return 1;
    }
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(introspection, NULL);
    static const GDBusInterfaceVTable vtable = {method_call, get_property, NULL, {0}};
    g_dbus_connection_register_object(bus, "/org/freedesktop/PolicyKit1/Authority",
                                      node->interfaces[0], &vtable, NULL, NULL, NULL);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_bus_own_name_on_connection(bus, "org.freedesktop.PolicyKit1", G_BUS_NAME_OWNER_FLAGS_NONE,
                                 acquired, lost, loop, NULL);
    GIOChannel *input = g_io_channel_unix_new(0);
    g_io_add_watch(input, G_IO_IN | G_IO_HUP | G_IO_ERR, readable, loop);
    g_main_loop_run(loop);
    g_io_channel_unref(input);
    g_dbus_node_info_unref(node);
    g_object_unref(bus);
    return 0;
}
