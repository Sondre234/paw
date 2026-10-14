// SPDX-License-Identifier: GPL-3.0-or-later
/* Rices (src/rice.cpp): rice.lua read as data alone and written back, what a rice may set and
 * name, the installed rices and the record of one on trial. */
#include "check.h"
#include "paw/config.hpp"
#include "paw/rice.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
// The error `run` throws, or "" when it throws none.
template <typename Run> std::string error_of(Run run) {
    try {
        run();
    } catch (const std::exception &error) {
        return error.what();
    }
    return "";
}
bool mentions(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}
void write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
std::string read(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
const std::string washi = R"(-- A rice for paw.
return {
    format = 1,
    name = "washi",
    author = "shao",
    description = [[Sakura paper,
navy ink.]],
    profile = {
        appearance = { background = "#f6eef0", color_scheme = "light" },
        windows = {
            border_width = 2,
            border_color = { "#8cc8ff", "#ffb7c5", angle = 45 },
            shadow = { enabled = true, offset = { 7, 7 }, blur = 0 },
            opacity = 0.95,
        },
        shell = { accent = "#e0607e", wallpaper = "wallpaper/washi.jpg", start_icon = "neko.svg" },
    },
    extras = { "kitty/washi.conf" },
}
)";

void data_reads_and_writes() {
    auto data = paw::parse_rice_data(R"(return {
        -- comments, [[long ones]] too
        --[==[ a long
        comment ]==]
        a = "x\"\n\65\x41\u{e9}\z
              y", b = 'single', c = [==[long]]
string]==],
        ["key with spaces"] = -12, d = 0x10, e = 1.5e2, f = -.5, g = true, h = false;
        { 1, 2, 3 },
        nested = { deeper = { "list", 2 } },
    })");
    CHECK(data.field("a") && data.field("a")->string == "x\"\nAA\xc3\xa9y", "%s",
          data.field("a") ? data.field("a")->string.c_str() : "missing");
    CHECK(data.field("b")->string == "single");
    CHECK(data.field("c")->string == "long]]\nstring");
    CHECK(data.field("key with spaces")->integer == -12);
    CHECK(data.field("d")->type == paw::RiceData::Type::Integer && data.field("d")->integer == 16);
    CHECK(data.field("e")->type == paw::RiceData::Type::Number && data.field("e")->number == 150);
    CHECK(data.field("f")->number == -0.5);
    CHECK(data.field("g")->boolean && !data.field("h")->boolean);
    CHECK(data.items.size() == 1 && data.items[0].items.size() == 3);
    CHECK(data.field("nested")->field("deeper")->items[1].integer == 2);

    // What it writes reads back the same, and is written the same again.
    auto written = paw::write_rice_data(data);
    auto again = paw::parse_rice_data(written);
    CHECK(paw::write_rice_data(again) == written, "%s", written.c_str());
    CHECK(again.field("a")->string == data.field("a")->string);
    CHECK(again.field("e")->type == paw::RiceData::Type::Number, "1.5e2 stays a float");
    auto two = paw::parse_rice_data("{ x = 2.0 }");
    CHECK(paw::write_rice_data(two) == "{\n    x = 2.0,\n}", "%s",
          paw::write_rice_data(two).c_str());
    CHECK(mentions(written, "[\"key with spaces\"] = -12"), "%s", written.c_str());
    CHECK(mentions(written, "{ 1, 2, 3 }"), "a short list on one line: %s", written.c_str());
    // Text that would steer a terminal is escaped.
    auto escaped = paw::write_rice_data(paw::RiceData::string_of("a\x1b[31m\xc2\x9b\xff\xe9" "b"));
    CHECK(escaped == "\"a\\027[31m\\194\\155\\255\\233b\"", "%s", escaped.c_str());
    CHECK(paw::write_rice_data(paw::RiceData::string_of("日本")) == "\"日本\"");
}

void code_is_refused() {
    // Nothing that computes or calls is read: names, calls, operators, functions, nil.
    for (const char *source : {
             "return { os.execute(\"rm -rf ~\") }",
             "return { a = os }",
             "return { a = io.open(\"/etc/passwd\"):read(\"a\") }",
             "return { a = require(\"x\") }",
             "return { a = (function() return 1 end)() }",
             "return { a = 1 + 2 }",
             "return { a = \"x\" .. \"y\" }",
             "return { a = nil }",
             "local t = {} return t",
             "return {} os.exit()",
             "return { a = load(\"x\") }",
             "return { a = dofile }",
             "while true do end",
             "return { a = 1 }; return {}",
         })
        CHECK(!error_of([&] { paw::parse_rice_data(source); }).empty(), "%s was read", source);
    CHECK(mentions(error_of([] { paw::parse_rice_data("return {\n a = os.time() }"); }),
                   "rice.lua:2: 'os' is a name"),
          "the line and why: %s",
          error_of([] { paw::parse_rice_data("return {\n a = os.time() }"); }).c_str());
    CHECK(mentions(error_of([] { paw::parse_rice_data("{ a = 1, a = 2 }"); }), "given twice"));
    CHECK(mentions(error_of([] { paw::parse_rice_data("{ a = \"open"); }), "not closed"));
    CHECK(mentions(error_of([] { paw::parse_rice_data("{ [1] = 2 }"); }), "must be a string"));
    CHECK(mentions(error_of([] { paw::parse_rice_data("{ a = 99999999999999999999 }"); }),
                   "out of range"));
    std::string deep = "{ a = " + std::string(40, '{') + std::string(40, '}') + " }";
    CHECK(mentions(error_of([&] { paw::parse_rice_data(deep); }), "nested too deeply"));
    CHECK(mentions(error_of([] { paw::parse_rice_data("{ a = \"\\q\" }"); }), "no escape"));
}

void manifests_are_checked() {
    auto rice = paw::parse_rice(washi);
    CHECK(rice.name == "washi" && rice.author == "shao");
    CHECK(rice.description == "Sakura paper,\nnavy ink.");
    CHECK(rice.pictures == (std::vector<std::string>{"wallpaper/washi.jpg", "neko.svg"}));
    CHECK(rice.extras == (std::vector<std::string>{"kitty/washi.conf"}));
    CHECK(rice.profile.field("windows")->field("shadow")->field("offset")->items.size() == 2);

    auto with = [](const std::string &fields) {
        return error_of([&] { paw::parse_rice("return { format = 1, name = \"r\", " + fields +
                                              " }"); });
    };
    CHECK(with("profile = {}").empty(), "%s", with("profile = {}").c_str());
    CHECK(mentions(error_of([] { paw::parse_rice("return { name = \"r\", profile = {} }"); }),
                   "format = 1"));
    CHECK(mentions(error_of([] { paw::parse_rice("return { format = 2, name = \"r\", "
                                                 "profile = {} }"); }),
                   "format = 1"));
    CHECK(mentions(with("profile = {}, script = \"x\""), "no setting script"));
    CHECK(mentions(error_of([] { paw::parse_rice("return { format = 1, name = \"../x\", "
                                                 "profile = {} }"); }),
                   "name is 1 to 32"));
    CHECK(mentions(with("profile = {}, author = \"a\\27[2Jb\""), "author"),
          "an author that would steer the terminal");
    CHECK(mentions(with("profile = {}, author = \"a\\nb\""), "author"));
    CHECK(mentions(with("profile = {}, description = \"\\194\\155x\""), "description"),
          "C1 control characters");
    CHECK(with("profile = {}, description = \"two\\nlines, 日本\"").empty());
    CHECK(mentions(with("profile = { shell = { launchers = { { name = \"x\", command = "
                        "{ \"sh\", \"-c\", \"evil\" } } } } }"),
                   "cannot set shell.launchers"),
          "%s", with("profile = { shell = { launchers = {} } }").c_str());
    CHECK(mentions(with("profile = { windows = { rules = {} } }"), "cannot set windows.rules"));
    CHECK(mentions(with("profile = { shell = { search = { web = \"https://x/?q=%s\" } } }"),
                   "cannot set shell.search"));
    CHECK(mentions(with("profile = { shell = { wallpapers = \"/home\" } }"),
                   "cannot set shell.wallpapers"));
    CHECK(mentions(with("profile = { windows = { magnet = { enabled = false } } }"),
                   "cannot set windows.magnet.enabled"));
    CHECK(with("profile = { windows = { magnet = { guide_color = \"#ffffff\" } } }").empty());
    CHECK(mentions(with("profile = { keyboard = { layout = \"de\" } }"), "cannot set keyboard"));
    CHECK(mentions(with("profile = { { 1 } }"), "keyed by name"));
    // Pictures and extras stay inside the rice.
    for (const char *path : {"/etc/passwd.png", "../outside.png", "a/../../b.png", ".hidden.png",
                             "a//b.png", "~/x.png", "a\\b.png", "wallpaper.exe", ""})
        if (*path)
            CHECK(!with("profile = { shell = { wallpaper = \"" + std::string(path) + "\" } }")
                       .empty(),
                  "the wallpaper %s", path);
    CHECK(with("profile = { shell = { wallpaper = \"\" } }").empty(), "no wallpaper at all");
    CHECK(mentions(with("profile = { shell = { start_icon = 3 } }"), "must be a string"));
    for (const char *path : {"/etc/shadow", "../../.bashrc", "kitty/../../x"})
        CHECK(mentions(with("profile = {}, extras = { \"" + std::string(path) + "\" }"),
                       "inside the rice"),
              "the extra %s", path);
    CHECK(paw::rice_path_valid("kitty/washi.conf") && paw::rice_path_valid("a-b_c.1.png"));
    CHECK(!paw::rice_path_valid("a/b/c/d/e"), "at most four parts");
    CHECK(paw::rice_setting_allowed({"windows", "shadow", "offset"}));
    CHECK(!paw::rice_setting_allowed({"windows", "rules"}));
}

void looks_and_changes() {
    auto profile = paw::parse_rice_data(R"({
        appearance = { background = "#000000" },
        windows = { border_width = 3, rules = { { app_id = "x", opacity = 0.5 } },
                    snap = { enabled = true, color = "#ffffff" } },
        shell = { accent = "#123456", launchers = {}, widgets = { clock = false } },
    })");
    std::vector<std::string> left_out;
    auto look = paw::rice_look(profile, &left_out);
    CHECK(left_out == (std::vector<std::string>{"windows.rules", "windows.snap.enabled",
                                                "shell.launchers"}),
          "%zu left out", left_out.size());
    CHECK(look.field("windows")->field("snap")->field("color"));
    CHECK(!look.field("windows")->field("rules"));
    std::vector<std::string> setting;
    CHECK(error_of([&] {
              paw::RiceData rice;
              rice.fields.emplace_back("format", paw::RiceData::integer_of(1));
              rice.fields.emplace_back("name", paw::RiceData::string_of("r"));
              rice.fields.emplace_back("profile", look);
              paw::parse_rice(paw::write_rice_data(rice));
          }).empty(),
          "what rice_look keeps, a rice may hold");

    auto before = paw::parse_rice_data(R"({ shell = { accent = "#123456", font = "Inter" },
                                           windows = { shadow = { offset = { 1, 2 } } } })");
    auto after = paw::parse_rice_data(R"({ shell = { accent = "#654321", font = "Inter" },
                                          windows = { shadow = { offset = { 7, 7 } } },
                                          appearance = { background = "#ffffff" } })");
    auto changes = paw::rice_changes(before, after);
    CHECK(changes.size() == 3, "%zu changes", changes.size());
    CHECK(changes[0].setting == "shell.accent" && changes[0].before == "\"#123456\"" &&
          changes[0].after == "\"#654321\"");
    CHECK(changes[1].setting == "windows.shadow.offset" && changes[1].after == "{ 7, 7 }",
          "%s", changes[1].after.c_str());
    CHECK(changes[2].setting == "appearance.background" && changes[2].before.empty());

    CHECK(paw::rice_install_name("washi", {"koi", "sumi"}) == "washi");
    CHECK(paw::rice_install_name("washi", {"washi", "washi-2"}) == "washi-3");
    CHECK(paw::rice_install_name(std::string(32, 'a'), {std::string(32, 'a')}) ==
          std::string(30, 'a') + "-2");
}

void directories_are_checked(const fs::path &root) {
    auto rice = root / "washi";
    write(rice / "rice.lua", washi);
    write(rice / "wallpaper/washi.jpg", "jpeg");
    write(rice / "neko.svg", "<svg/>");
    write(rice / "kitty/washi.conf", "background #f6eef0");
    write(rice / "notes.txt", "not named, not taken");
    auto found = paw::read_rice(rice);
    CHECK(found.directory == rice);
    auto files = paw::rice_files(rice);
    CHECK(files.size() == 4 && files["neko.svg"] == "<svg/>" && !files.contains("notes.txt"));
    auto profile = paw::rice_profile(found);
    CHECK(profile.field("shell")->field("wallpaper")->string ==
          (rice / "wallpaper/washi.jpg").string());

    // A link, to a file outside or to a directory outside, is refused, as is a missing file.
    fs::remove(rice / "neko.svg");
    CHECK(mentions(error_of([&] { paw::read_rice(rice); }), "no neko.svg"));
    fs::create_symlink("/etc/passwd", rice / "neko.svg");
    CHECK(mentions(error_of([&] { paw::read_rice(rice); }), "is a link"));
    fs::remove(rice / "neko.svg");
    write(rice / "neko.svg", "<svg/>");
    fs::remove_all(rice / "kitty");
    fs::create_directory_symlink("/etc", rice / "kitty");
    CHECK(mentions(error_of([&] { paw::read_rice(rice); }), "is a link"),
          "a directory through a link");
    fs::remove(rice / "kitty");
    write(rice / "kitty/washi.conf", "x");
    auto manifest = read(rice / "rice.lua");
    fs::remove(rice / "rice.lua");
    fs::create_symlink(root / "elsewhere.lua", rice / "rice.lua");
    write(root / "elsewhere.lua", washi);
    CHECK(mentions(error_of([&] { paw::read_rice(rice); }), "is a link"), "rice.lua a link");
    fs::remove(rice / "rice.lua");
    write(rice / "rice.lua", manifest);

    // Staging writes only what rice.lua names, in a directory the configuration passes over.
    files["../escape"] = "x";
    files["extra.txt"] = "x";
    auto staged = paw::stage_rice(files, root / "rices");
    CHECK(staged.filename().string().starts_with(".washi-"), "%s", staged.c_str());
    CHECK(fs::exists(staged / "wallpaper/washi.jpg") && !fs::exists(staged / "extra.txt"));
    CHECK(!fs::exists(root / "escape"));
    fs::remove_all(staged);
    files.erase("neko.svg");
    CHECK(mentions(error_of([&] { paw::stage_rice(files, root / "rices"); }), "no neko.svg"));
    CHECK(std::distance(fs::directory_iterator(root / "rices"), fs::directory_iterator()) == 0,
          "a failed staging leaves nothing");
}

void trials(const fs::path &root) {
    setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
    auto rices = paw::rices_directory();
    CHECK(rices == root / "state/paw/rices");
    auto files = paw::rice_files(root / "washi");
    CHECK(paw::installed_rices().empty());

    // A first trial, with no profile saved before: going back removes the saved choice.
    auto staged = paw::stage_rice(files, rices);
    paw::begin_rice_trial(staged, "washi");
    CHECK(paw::saved_profile() == "washi");
    CHECK(fs::exists(rices / "washi/rice.lua") && !fs::exists(staged));
    auto trial = paw::load_rice_trial();
    CHECK(trial && trial->rice == "washi" && !trial->previous && !trial->replaced);
    CHECK(paw::installed_rices().size() == 1);
    CHECK(mentions(error_of([&] { paw::begin_rice_trial(paw::stage_rice(files, rices), "x"); }),
                   "on trial already"));
    for (const auto &entry : fs::directory_iterator(rices))
        if (entry.path().filename().string().starts_with(".washi-"))
            fs::remove_all(entry.path());
    CHECK(paw::revert_rice_trial());
    CHECK(!fs::exists(paw::profile_state_path()) && !fs::exists(rices / "washi"));
    CHECK(!paw::load_rice_trial() && !paw::revert_rice_trial(), "nothing left to revert");

    // Kept, it stays; tried again, the installed one waits aside and comes back on revert.
    paw::save_profile("koi");
    paw::begin_rice_trial(paw::stage_rice(files, rices), "washi");
    CHECK(paw::load_rice_trial()->previous == std::optional<std::string>("koi"));
    CHECK(paw::keep_rice_trial());
    CHECK(!paw::load_rice_trial() && paw::saved_profile() == "washi");
    write(rices / "washi/kitty/washi.conf", "the kept one");
    files["kitty/washi.conf"] = "the new one";
    paw::begin_rice_trial(paw::stage_rice(files, rices), "washi");
    CHECK(paw::load_rice_trial()->replaced);
    CHECK(read(rices / "washi/kitty/washi.conf") == "the new one");
    CHECK(fs::exists(paw::rice_set_aside_path("washi")));
    CHECK(paw::installed_rices().size() == 1, "the one set aside is no profile");
    paw::revert_rice_trial();
    CHECK(read(rices / "washi/kitty/washi.conf") == "the kept one");
    CHECK(!fs::exists(paw::rice_set_aside_path("washi")) && paw::saved_profile() == "washi");

    // A trial whose compositor is gone goes back at the next start; one whose compositor runs
    // stays.
    paw::save_profile("koi");
    paw::begin_rice_trial(paw::stage_rice(files, rices), "washi");
    auto record = *paw::load_rice_trial();
    record.pid = 999999999;
    paw::save_rice_trial(record);
    CHECK(paw::recover_rice_trial() && paw::saved_profile() == "koi");
    CHECK(read(rices / "washi/kitty/washi.conf") == "the kept one");
    paw::begin_rice_trial(paw::stage_rice(files, rices), "washi");
    record = *paw::load_rice_trial();
    record.pid = getppid(); // running, but not a compositor
    paw::save_rice_trial(record);
    CHECK(paw::recover_rice_trial(), "a process that is not paw holds no trial");
    CHECK(paw::saved_profile() == "koi");

    // A rice that cannot be read is passed over, and said.
    write(rices / "broken/rice.lua", "return { format = 1, name = \"broken\", profile = "
                                     "{ shell = { launchers = {} } } }");
    std::vector<std::string> problems;
    auto installed = paw::installed_rices(&problems);
    CHECK(installed.size() == 1 && installed[0].directory.filename() == "washi");
    CHECK(problems.size() == 1 && mentions(problems[0], "broken: a rice cannot set"),
          "%s", problems.empty() ? "" : problems[0].c_str());
}
void profiles(const fs::path &root) {
    // An installed rice is a profile of its directory's name, its pictures in its directory.
    setenv("XDG_STATE_HOME", (root / "profiles-state").c_str(), 1);
    auto rices = paw::rices_directory();
    fs::create_directories(rices);
    fs::copy(root / "washi", rices / "washi-2", fs::copy_options::recursive);
    write(rices / "sepia/rice.lua", R"(return { format = 1, name = "sepia",
        profile = { shell = { accent = "#704214" } } })");
    write(rices / "dark/rice.lua", R"(return { format = 1, name = "dark", profile = {} })");
    write(rices / "bad/rice.lua", R"(return { format = 1, name = "bad",
        profile = { shell = { accent = "blue" } } })");
    const std::string source = R"(return {
        profile = "dark",
        profiles = { dark = { shell = { accent = "#111111" } }, light = {} },
    })";
    auto installed = paw::installed_rices();
    CHECK(installed.size() == 4);
    auto config = paw::parse_config(source, "config", root, "washi-2", installed);
    CHECK(config.profile == "washi-2", "%s", config.profile.c_str());
    CHECK(config.profiles ==
          (std::vector<std::string>{"dark", "light", "sepia", "washi-2"}));
    CHECK(config.rices == (std::vector<std::string>{"sepia", "washi-2"}));
    CHECK(config.shell.accent == "#e0607e", "%s", config.shell.accent.c_str());
    CHECK(config.shell.wallpaper == (rices / "washi-2/wallpaper/washi.jpg").string(), "%s",
          config.shell.wallpaper.c_str());
    CHECK(config.shell.start_icon == (rices / "washi-2/neko.svg").string());
    // The configuration's own profile wins over a rice of its name, and one with a setting the
    // configuration refuses is passed over, said, rather than an error.
    CHECK(config.rice_problems.size() == 2, "%zu", config.rice_problems.size());
    CHECK(mentions(config.rice_problems[0], "bad: accent must be") ||
              mentions(config.rice_problems[1], "bad: accent must be"),
          "%s", config.rice_problems.empty() ? "" : config.rice_problems[0].c_str());
    CHECK(mentions(config.rice_problems[0] + config.rice_problems[1],
                   "dark: the configuration has a profile of that name"));
    auto fallback = paw::parse_config(source, "config", root, "bad", installed);
    CHECK(fallback.profile == "dark" && fallback.shell.accent == "#111111");
    auto own = paw::parse_config(source, "config", root, "dark", installed);
    CHECK(own.shell.accent == "#111111");
    // load_config finds them.
    write(root / "config/init.lua", source);
    paw::save_profile("sepia");
    auto loaded = paw::load_config(root / "config/init.lua");
    CHECK(loaded.profile == "sepia" && loaded.shell.accent == "#704214");

    // What a profile looks like: the configuration's, its theme's and the profile's settings.
    write(root / "config/theme.lua", R"(return { shell = { panel_color = "#222222",
        font = "Inter" }, windows = { border_width = 4 } })");
    write(root / "config/init.lua", R"(return {
        theme = "theme.lua",
        shell = { font = "Maple Mono", wallpaper = "walls/day.png",
                  launchers = { { name = "x", command = { "true" } } } },
        windows = { rules = { { app_id = "x", opacity = 0.5 } } },
        profiles = { night = { shell = { accent = "#000080", wallpaper = "walls/night.png" } } },
    })");
    auto look = paw::configured_look(root / "config/init.lua", "night");
    auto shell = look.field("shell");
    CHECK(shell && shell->field("font")->string == "Maple Mono");
    CHECK(shell->field("panel_color")->string == "#222222", "from the theme");
    CHECK(shell->field("accent")->string == "#000080", "from the profile");
    CHECK(shell->field("wallpaper")->string == "walls/night.png", "as the profile names it");
    CHECK(look.field("windows")->field("border_width")->integer == 4);
    CHECK(look.field("windows")->field("rules"), "everything, before rice_look keeps the look");
    CHECK(paw::configured_look(root / "config/init.lua", "")
              .field("shell")->field("wallpaper")->string == "walls/day.png");
    auto rice_look = paw::configured_look(root / "config/init.lua", "washi-2");
    CHECK(rice_look.field("shell")->field("accent")->string == "#e0607e");
    CHECK(mentions(error_of([&] { paw::configured_look(root / "config/init.lua", "nope"); }),
                   "no profile nope"));
}
} // namespace

int main() {
    auto root = fs::temp_directory_path() / ("paw-rice-tests-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    data_reads_and_writes();
    code_is_refused();
    manifests_are_checked();
    looks_and_changes();
    directories_are_checked(root);
    trials(root);
    profiles(root);
    fs::remove_all(root);
    return failures != 0;
}
