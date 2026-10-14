// SPDX-License-Identifier: GPL-3.0-or-later
/* paw doctor's report and how it reads what other programs say (src/cli/doctor_parse.cpp). */
#include "check.h"
#include "paw/doctor.hpp"

#include <set>

using namespace paw::doctor;

namespace {
const char *const wpctl_status = R"(PipeWire 'pipewire-0' [1.6.8, someone@host, cookie:390364363]
 └─ Clients:
        32. pipewire                            [1.6.8, someone@host, pid:2178]
        34. WirePlumber                         [1.6.8, someone@host, pid:2154]
        47. WirePlumber [export]                [1.6.8, someone@host, pid:2154]
       122. wpctl                               [1.6.8, someone@host, pid:484169]

Audio
 ├─ Devices:
 │      51. AD102 High Definition Audio Controller [alsa]
 │      52. Arctis Nova Pro Wireless            [alsa]
 │
 ├─ Sinks:
 │      43. USB Audio Front Headphones          [vol: 1.00]
 │  *   33. Arctis Nova Pro Wireless Analog Stereo [vol: 0.40 MUTED]
 │
 ├─ Sources:
 │  *   46. Arctis Nova Pro Wireless Mono       [vol: 1.00]
 │
 ├─ Filters:
 │
 └─ Streams:
       134. WEBRTC VoiceEngine
            105. monitor_MONO

Video
 ├─ Devices:
 │
 ├─ Sinks:
 │  *   70. Some Video Sink
 │
 └─ Streams:

Settings
 └─ Default Configured Devices:
         0. Audio/Sink    alsa_output.usb-Arctis.analog-stereo
)";
// PipeWire with every device gone, as WirePlumber leaves it without its bus.
const char *const wpctl_no_sinks = R"(PipeWire 'pipewire-0' [1.6.8, someone@host, cookie:1]
 └─ Clients:
        32. pipewire                            [1.6.8, someone@host, pid:2178]

Audio
 ├─ Devices:
 │
 ├─ Sinks:
 │
 ├─ Sources:
 │
 └─ Streams:
)";
} // namespace

int main() {
    auto sinks = parse_wpctl_status(wpctl_status);
    CHECK(sinks.listed);
    CHECK(sinks.names == (std::vector<std::string>{"USB Audio Front Headphones",
                                                   "Arctis Nova Pro Wireless Analog Stereo"}),
          "%zu sinks, the video sink is not one", sinks.names.size());
    CHECK(sinks.default_name == "Arctis Nova Pro Wireless Analog Stereo", "%s",
          sinks.default_name.c_str());
    CHECK(sinks.clients.size() == 4, "%zu clients", sinks.clients.size());
    CHECK(sinks.clients.size() > 1 && sinks.clients[1].name == "WirePlumber" &&
          sinks.clients[1].pid == 2154);
    CHECK(sinks.clients.size() > 2 && sinks.clients[2].name == "WirePlumber [export]",
          "only the last bracket is the details");
    CHECK(!no_sound_output(sinks));
    auto none = parse_wpctl_status(wpctl_no_sinks);
    CHECK(none.listed && none.names.empty() && none.default_name.empty());
    CHECK(none.clients.size() == 1, "WirePlumber is not among them");
    CHECK(no_sound_output(none), "no sink at all is Dummy Output in the panel");
    CHECK(no_sound_output(Sinks{true, {}, {"Dummy Output"}, "Dummy Output"}));
    CHECK(no_sound_output(Sinks{true, {}, {"x", "auto_null"}, "auto_null"}));
    CHECK(!no_sound_output(Sinks{true, {}, {"Speakers"}, ""}), "one is enough without a default");
    auto refused = parse_wpctl_status("Could not connect to PipeWire\n");
    CHECK(!refused.listed && refused.names.empty());

    auto env = parse_environ(std::string("A=1\0B=x=y\0broken\0=no\0EMPTY=\0", 28));
    CHECK(env.size() == 3 && env["A"] == "1" && env["B"] == "x=y" && env.count("EMPTY") &&
              env["EMPTY"].empty(),
          "%zu entries", env.size());
    CHECK(desktop_names("Budgie:GNOME::paw") ==
          (std::vector<std::string>{"budgie", "gnome", "paw"}));
    CHECK(desktop_names("").empty());

    using Paths = std::vector<std::filesystem::path>;
    CHECK(portal_config_directories({{"HOME", "/home/u"}}) ==
              (Paths{"/home/u/.config/xdg-desktop-portal", "/etc/xdg/xdg-desktop-portal",
                     "/etc/xdg-desktop-portal", "/home/u/.local/share/xdg-desktop-portal",
                     "/usr/local/share/xdg-desktop-portal", "/usr/share/xdg-desktop-portal"}),
          "the defaults, in portals.conf(5)'s order");
    CHECK(portal_config_directories({{"HOME", "/home/u"},
                                     {"XDG_CONFIG_HOME", "/c"},
                                     {"XDG_CONFIG_DIRS", "/d1:relative:/d2"},
                                     {"XDG_DATA_HOME", "relative"},
                                     {"XDG_DATA_DIRS", "/usr/share:/opt/share"}}) ==
              (Paths{"/c/xdg-desktop-portal", "/d1/xdg-desktop-portal", "/d2/xdg-desktop-portal",
                     "/etc/xdg-desktop-portal", "/home/u/.local/share/xdg-desktop-portal",
                     "/usr/share/xdg-desktop-portal", "/opt/share/xdg-desktop-portal"}),
          "relative directories are ignored, and /usr/share counted once");
    CHECK(portal_config_directories({}).front() == "/etc/xdg/xdg-desktop-portal",
          "without HOME there is no personal directory");

    Paths directories{"/c", "/etc", "/usr/share"};
    auto config = [&](std::set<std::string> files, std::vector<std::string> desktops = {"paw"}) {
        return portal_config(directories, desktops, [&](const std::filesystem::path &file) {
            return files.count(file.string()) > 0;
        });
    };
    CHECK(config({"/usr/share/paw-portals.conf"}) == "/usr/share/paw-portals.conf");
    CHECK(config({"/usr/share/paw-portals.conf", "/c/portals.conf"}) == "/c/portals.conf",
          "a generic file in a more important directory wins");
    CHECK(config({"/c/portals.conf", "/c/paw-portals.conf"}) == "/c/paw-portals.conf",
          "the desktop's own file wins within a directory");
    CHECK(config({"/etc/gnome-portals.conf", "/etc/paw-portals.conf"}, {"gnome", "paw"}) ==
          "/etc/gnome-portals.conf", "the desktops in XDG_CURRENT_DESKTOP's order");
    CHECK(config({"/usr/share/wlroots-portals.conf"}).empty());

    const char *conf = "# a comment\n[preferred]\ndefault=gtk\n"
                       "org.freedesktop.impl.portal.ScreenCast = wlr; gnome ;\n"
                       "org.freedesktop.impl.portal.Inhibit=none\n[other]\n"
                       "org.freedesktop.impl.portal.Screenshot=kde\n";
    CHECK(portal_backends(conf, "org.freedesktop.impl.portal.ScreenCast") ==
          (std::vector<std::string>{"wlr", "gnome"}));
    CHECK(portal_backends(conf, "org.freedesktop.impl.portal.Screenshot") ==
              (std::vector<std::string>{"gtk"}),
          "a key outside [preferred] does not count");
    CHECK(portal_backends(conf, "org.freedesktop.impl.portal.Inhibit") ==
          (std::vector<std::string>{"none"}));
    CHECK(portal_backends("[preferred]\n", "org.freedesktop.impl.portal.ScreenCast").empty());
    const char *wlr = "[portal]\nDBusName=org.freedesktop.impl.portal.desktop.wlr\n"
                      "Interfaces=org.freedesktop.impl.portal.Screenshot;"
                      "org.freedesktop.impl.portal.ScreenCast;\n";
    CHECK(portal_implements(wlr, "org.freedesktop.impl.portal.ScreenCast"));
    CHECK(!portal_implements(wlr, "org.freedesktop.impl.portal.FileChooser"));

    auto updated = parse_update_log("update-paw: 2026-10-14 15:03:52 building 922e2b00\n"
                                    "-- Installing: /p/bin/paw-shell\n"
                                    "-- Up-to-date: /p/bin/paw\n"
                                    "-- Up-to-date: /p/share/paw/init.lua\n"
                                    "update-paw: 2026-10-14 15:04:14 installed 922e2b00\n");
    CHECK(updated.building == "922e2b00" && updated.building_time == "2026-10-14 15:03:52");
    CHECK(updated.installed == "922e2b00" && updated.installed_time == "2026-10-14 15:04:14");
    CHECK(updated.binary == "/p/bin/paw", "%s", updated.binary.c_str());
    auto failed = parse_update_log("update-paw: 2026-10-14 15:03:52 building abc\n"
                                   "FAILED: src/main.cpp.o\nninja: build stopped\n");
    CHECK(failed.building == "abc" && failed.installed.empty() && failed.binary.empty());
    CHECK(parse_update_log("").building.empty());

    auto stat = parse_stat("2154 (a (b) c) S 2152 2141 2036 0 -1 4194560 1 0 0 0 0 0 0 0 20 0 "
                           "1 0 1326399 528318464 55326");
    CHECK(stat && stat->state == 'S' && stat->ppid == 2152 && stat->start == 1326399,
          "a name with spaces and parentheses");
    CHECK(!parse_stat("2154 (cut short) S 1 2"));
    CHECK(!parse_stat("garbage"));
    CHECK(program_path("/p/bin/paw (deleted)") == "/p/bin/paw");
    CHECK(program_replaced("/p/bin/paw (deleted)") && !program_replaced("/p/bin/paw"));
    CHECK(program_path("/p/bin/paw") == "/p/bin/paw");
    CHECK(split_nul(std::string("paw\0--session\0", 14)) ==
          (std::vector<std::string>{"paw", "--session"}));
    CHECK(split_nul(std::string("a\0\0b", 4)) == (std::vector<std::string>{"a", "", "b"}));
    CHECK(version_commit("0.1.1 (git 922e2b0)") == "922e2b0");
    CHECK(version_commit("paw 0.1.1 (git v0.1.1-3-g922e2b0)") == "922e2b0");
    CHECK(version_commit("0.1.1 (git v0.1.1-3-g922e2b0-dirty)") == "922e2b0");
    CHECK(version_commit("0.1.1").empty() && version_commit("0.1.1 (git v0.1.0)").empty());

    const char *log = "paw: session starting\n"
                      "00:00:01.4 [INFO] [server.c:528] Running Wayland compositor on "
                      "WAYLAND_DISPLAY=wayland-0\n"
                      "Cannot start desktop shell: old\n"
                      "paw: restarting\n"
                      "00:00:01.2 [INFO] [server.c:528] Running Wayland compositor on "
                      "WAYLAND_DISPLAY=wayland-0\n"
                      "paw polkit: the session's authentication agent\n"
                      "paw polkit: another authentication agent serves this session\n";
    auto since = since_last_start(log);
    CHECK(since && since->starts_with("00:00:01.2"), "from the last start's line");
    CHECK(since && last_line(*since, "Cannot start desktop shell: ").empty(),
          "the previous compositor's lines are left out");
    CHECK(since && last_line(*since, "paw polkit: ") ==
                       "another authentication agent serves this session");
    CHECK(!since_last_start("paw: session starting\n"));

    CHECK(shell_word("/home/u/.config/xdg-desktop-portal") == "/home/u/.config/xdg-desktop-portal");
    CHECK(shell_word("/a b/it's") == "'/a b/it'\\''s'");
    CHECK(shell_word("") == "''");

    std::vector<Result> results{
        {"Session", "shell", Status::error, "The shell is gone", "kill -HUP 12"},
        {"Sound", "output", Status::ok, "Sound plays on Speakers", "unused"},
        {"Session", "build", Status::warning, "Old\tbuild", "Log out\nand in"},
    };
    CHECK(report(results, false) == "Session\n"
                                    "  ✗ The shell is gone\n"
                                    "      kill -HUP 12\n"
                                    "  ! Old\tbuild\n"
                                    "      Log out\n"
                                    "      and in\n"
                                    "\n"
                                    "Sound\n"
                                    "  ✓ Sound plays on Speakers\n"
                                    "\n"
                                    "1 problem and 1 warning.\n",
          "%s", report(results, false).c_str());
    CHECK(report(results, true).find("\033[31m✗\033[0m The shell is gone") != std::string::npos);
    CHECK(plain_report(results) == "error\tshell\tThe shell is gone\tkill -HUP 12\n"
                                   "ok\toutput\tSound plays on Speakers\t\n"
                                   "warning\tbuild\tOld build\tLog out; and in\n",
          "%s", plain_report(results).c_str());
    CHECK(report({results[1]}, false).ends_with("Nothing wrong found.\n"));
    CHECK(report({results[2], results[2]}, false).ends_with("2 warnings, nothing broken.\n"));
    return failures != 0;
}
