// SPDX-License-Identifier: GPL-3.0-or-later
/* paw fetch's palette, numbers, fields and drawing (src/cli/fetch.cpp), without a session. */
#include "check.h"
#include "paw/cli.hpp"
#include "paw/config.hpp"
#include "paw/fetch.hpp"

#include <algorithm>
#include <sstream>

using namespace paw::fetch;

static size_t columns(const std::string &line) {
    return static_cast<size_t>(std::count_if(line.begin(), line.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

static std::vector<std::string> lines_of(const std::string &text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);)
        lines.push_back(line);
    return lines;
}

static std::string trimmed(std::string line) {
    while (!line.empty() && line.back() == ' ')
        line.pop_back();
    return line;
}

static std::string repeated(const std::string &text, size_t count) {
    std::string result;
    for (size_t i = 0; i < count; ++i)
        result += text;
    return result;
}

static Rgb rgb(const char *hex) { return *parse_color(hex); }

int main() {
    // Colours.
    CHECK(parse_color("#8fc1ec") == (Rgb{0x8f, 0xc1, 0xec}));
    CHECK(parse_color("#8FC1EC80") == (Rgb{0x8f, 0xc1, 0xec}), "the alpha is left out");
    CHECK(!parse_color("8fc1ec") && !parse_color("#8fc1e") && !parse_color("#8fc1eg"));
    NEAR(contrast({0, 0, 0}, {255, 255, 255}), 21, 0.01);
    NEAR(contrast(rgb("#8fc1ec"), rgb("#8fc1ec")), 1, 1e-9);
    auto paper = rgb("#f4f0f3"), ink = rgb("#15141d");
    auto deeper = readable(rgb("#8fc1ec"), paper, 3);
    CHECK(contrast(deeper, paper) >= 3 && deeper.r < 0x8f, "light blue darkens on paper");
    CHECK(readable(rgb("#8fc1ec"), ink, 3) == rgb("#8fc1ec"), "a colour that stands out stays");

    // A profile's palette, from its settings and from `get palette`.
    auto washi = paw::parse_config(R"(return {
        appearance = { background = "#f4f0f3", color_scheme = "light" },
        windows = { border_color = { "#8fc1ec", "#f2a0be", angle = 135 },
                    border_inactive_color = "#4a5170", urgent_color = "#d9608c" },
    })");
    auto palette = palette_of(washi.settings);
    CHECK(palette.scheme == "light");
    CHECK(palette.background == paper);
    CHECK(palette.border == (std::vector<Rgb>{rgb("#8fc1ec"), rgb("#f2a0be")}));
    NEAR(palette.angle, 135, 1e-6);
    CHECK(palette.inactive == rgb("#4a5170") && palette.urgent == rgb("#d9608c"));
    auto defaults = palette_of(paw::Config{}.settings);
    CHECK(defaults.scheme.empty() && defaults.border.size() == 1);
    auto replied = palette_of(paw::cli::rows("scheme\tdark\nbackground\t#15141dff\n"
                                             "border\t90\t#8fc1ecff\t#f2a7c3ff\n"
                                             "border_inactive\t0\t#34334aff\nurgent\t#e8789fff\n"
                                             "shadow\t#000000cc\n"),
                              defaults);
    CHECK(replied.scheme == "dark" && replied.background == ink);
    CHECK(replied.border == (std::vector<Rgb>{rgb("#8fc1ec"), rgb("#f2a7c3")}));
    NEAR(replied.angle, 90, 1e-9);
    CHECK(replied.inactive == rgb("#34334a") && replied.urgent == rgb("#e8789f"));
    auto partial = palette_of(paw::cli::rows("scheme\t-\nborder\t0\n"), defaults);
    CHECK(partial.scheme.empty() && partial.border == defaults.border,
          "what the reply leaves out stays as it was");

    // The gradient runs along its angle, as the border's does.
    replied.angle = 0;
    CHECK(gradient_at(replied, 0, 0.5) == rgb("#8fc1ec") &&
          gradient_at(replied, 1, 0.5) == rgb("#f2a7c3"));
    replied.angle = 90;
    CHECK(gradient_at(replied, 0.5, 0) == rgb("#8fc1ec") &&
          gradient_at(replied, 0.5, 1) == rgb("#f2a7c3"));
    replied.angle = 135;
    CHECK(gradient_at(replied, 1, 0) == rgb("#8fc1ec") &&
              gradient_at(replied, 0, 1) == rgb("#f2a7c3"),
          "135 degrees runs from the top right to the bottom left");
    auto middle = gradient_at(replied, 0.5, 0.5);
    CHECK(middle.r > 0x8f && middle.r < 0xf2, "the middle is between the stops");
    CHECK(gradient_at(defaults, 0.9, 0.1) == defaults.border[0],
          "one colour is the same throughout");

    // Numbers.
    CHECK(duration(45'000) == "45s" && duration(570'000) == "9m30s" && duration(600'000) == "10m");
    CHECK(duration(4 * 3600'000 + 12 * 60'000 + 59'000) == "4h12m" && duration(7200'000) == "2h");
    CHECK(duration(3 * 86400'000LL + 4 * 3600'000) == "3d4h" && duration(-5) == "0s");
    CHECK(amount(999) == "999" && amount(1500) == "1.5k" && amount(158'312) == "158k");
    CHECK(amount(9'960) == "10k" && amount(2'400'000) == "2.4M" && amount(1000) == "1k");
    CHECK(milliseconds(0.4412) == "0.44ms" && milliseconds(9.5) == "9.5ms" &&
          milliseconds(25.2) == "25ms");
    CHECK(version_text("0.1.1 (git 61f4e8d)") == "0.1.1 (61f4e8d)" &&
          version_text("0.2.0") == "0.2.0");

    // Fields from the session's replies.
    CHECK(session_text(4 * 3600'000 + 12 * 60'000, 158'000) == "4h12m · 158k frames");
    auto stats = paw::cli::rows("359679\t148500458321\t25196999\t1\t2\t3\t4\t5\t6\t7\t8\n");
    std::string times = "200\n";
    for (int i = 0; i < 200; ++i)
        times += std::to_string(i < 196 ? 400 : 1200 + i) + "\t6944\n";
    CHECK(frames_text(stats, paw::cli::rows(times)) == "avg 0.41ms · p99 1.4ms · worst 25ms", "%s",
          frames_text(stats, paw::cli::rows(times)).c_str());
    CHECK(frames_text(stats, {}) == "avg 0.41ms · worst 25ms", "too few frames for a p99");
    CHECK(frames_text(paw::cli::rows("0\t0\t0\n"), {}).empty(), "no frames yet");

    auto window = [](const char *workspace, const char *output, bool minimized, bool scratchpad) {
        return std::vector<std::string>{
            workspace, "0", minimized ? "1" : "0",  "1", "0", "0", "640", "480", "kitty", "term",
            output,    "1", scratchpad ? "1" : "0", "0", "0", "0"};
    };
    Rows windows = {window("1", "DP-1", false, false), window("1", "DP-1", true, false),
                    window("2", "DP-1", false, false), window("1", "HDMI-A-1", false, false),
                    window("3", "DP-1", true, true)};
    CHECK(windows_text(windows, true, "scroll") == "4 on 3 workspaces · scroll", "%s",
          windows_text(windows, true, "scroll").c_str());
    CHECK(windows_text({windows[0]}, false, "dwindle") == "1 on 1 workspace · floating");
    CHECK(windows_text({}, true, "dwindle") == "none · dwindle");

    auto monitors = monitors_text(paw::cli::rows(
        "HDMI-A-1\t1\t0\t0\t2048\t1152\t1.25\t0\t2560x1440@143.912\tPhilips\ton\t-\t8\tsdr\n"
        "DP-1\t1\t2048\t0\t1920\t1080\t1\t0\t1920x1080@59.940\tMSI\toff\t-\t10\tsdr\n"
        "DP-3\t1\t0\t0\t3840\t2160\t1.5\t0\t3840x2160@120.000\tLG\ton\t-\t10\thdr\n"
        "eDP-1\t0\t0\t0\t0\t0\t1\t0\t1920x1200@60.000\tpanel\ton\tDP-1\t8\tsdr\n"
        "HEADLESS-2\t0\t0\t0\t0\t0\t1\t0\t1280x720@0.000\theadless\toff\t-\t8\tsdr\n"));
    CHECK(monitors.size() == 4, "%zu monitors", monitors.size());
    CHECK(monitors[0] == "HDMI-A-1 2560x1440@144Hz ×1.25", "%s", monitors[0].c_str());
    CHECK(monitors[1] == "DP-1 1920x1080@60Hz 10-bit (off)", "%s", monitors[1].c_str());
    CHECK(monitors[2] == "DP-3 3840x2160@120Hz ×1.5 HDR", "%s", monitors[2].c_str());
    CHECK(monitors[3] == "eDP-1 mirrors DP-1", "a disabled monitor shows only as a mirror");
    CHECK(monitors_text(paw::cli::rows("HEADLESS-1\t1\t0\t0\t1280\t720\t1\t0\t1280x720@0.000\t"
                                       "headless\ton\t-\t8\tsdr\n"))[0] == "HEADLESS-1 1280x720");

    CHECK(idle_text(paw::cli::rows("state\t303\t1\t0\t0\ndim\t570000\t0\ndisplay_off\t0\t0\n"
                                   "lock\t900000\t0\nsuspend\t0\t0\n")) ==
          "inhibited · dim 9m30s · lock 15m");
    CHECK(idle_text(paw::cli::rows("state\t9\t0\t1\t600\ndim\t0\t0\ndisplay_off\t600000\t0\n"
                                   "lock\t0\t0\nsuspend\t3600000\t0\n")) ==
          "dimmed · on battery · screens off 10m · sleep 1h");
    CHECK(idle_text(paw::cli::rows("state\t9\t0\t0\t0\ndim\t0\t0\n")) == "never sleeps");
    // The inhibitors counted on the state line, and a line per inhibitor, change nothing.
    CHECK(idle_text(paw::cli::rows("state\t303\t1\t0\t0\t2\t1\ndim\t570000\t0\n"
                                   "inhibitor\t1\twindow\tVideo\ninhibitor\t0\tother\t-\n")) ==
          "inhibited · dim 9m30s");
    CHECK(night_text(paw::cli::rows("3400\t0\t1\n")) == "3400K");
    CHECK(night_text(paw::cli::rows("6500\t0\t1\n")) == "scheduled");
    CHECK(night_text(paw::cli::rows("6500\t0\t0\n")).empty());
    CHECK(sessions_text({"work", "last"}) == "last, work");
    CHECK(sessions_text({"e", "d", "c", "b", "a", "f"}) == "a, b, c, d +2");

    auto top = top_app("discord.desktop\t26\t1791983281\nfirefox-bin.desktop\t28\t1791983390\n"
                       "broken line\nkitty.desktop\t28\t1791999999\nzero.desktop\t0\t1\n");
    CHECK(top && top->id == "kitty.desktop" && top->count == 28, "a tie goes to the latest");
    CHECK(!top_app("") && !top_app("x\ty\tz\n"));
    CHECK(desktop_name("[Desktop Entry]\nType=Application\nName[de]=Feuerfuchs\nName=Firefox  \n"
                       "[Desktop Action new]\nName=New Window\n") == "Firefox");
    CHECK(desktop_name("[Desktop Action x]\nName=Other\n").empty());
    CHECK(app_label("org.kde.dolphin.desktop") == "dolphin" &&
          app_label("firefox-bin.desktop") == "firefox-bin");

    // The logo: as many columns on every line, plain or coloured.
    auto plain = logo(palette, false);
    CHECK(plain.size() == 8, "%zu lines", plain.size());
    for (const auto &line : plain)
        CHECK(columns(line) == 22 && line.find('\x1b') == std::string::npos, "%s", line.c_str());
    CHECK(plain[7] == "    ▀██▀ ▀██▀ ▀██▀    ", "the pad's three lobes: %s", plain[7].c_str());
    auto colored = logo(palette, true);
    CHECK(colored.size() == plain.size());
    CHECK(colored[0].find("\x1b[38;2;") != std::string::npos);
    CHECK(colored[0].ends_with(std::string("\x1b[0m") + "     "), "each line ends its colours");

    // Everything side by side.
    std::vector<Field> fields = {{"profile", "washi · light"},
                                 {"monitors", "DP-1 2560x1440@144Hz"},
                                 {"", "DP-2 1920x1080@60Hz"},
                                 {"top app", "Firefox (28 launches)"}};
    auto text = render(palette, "paw 0.1.1 · wlroots 0.20.2", fields, {false, true, 0});
    auto lines = lines_of(text);
    CHECK(lines.size() == 8, "the logo is taller than the fields: %zu lines", lines.size());
    CHECK(lines[0] == plain[0] + "   paw 0.1.1 · wlroots 0.20.2", "%s", lines[0].c_str());
    CHECK(lines[1] == plain[1] + "   " + repeated("─", 26), "%s", lines[1].c_str());
    CHECK(lines[2].ends_with("   profile   washi · light"), "%s", lines[2].c_str());
    CHECK(lines[4] == plain[4] + "   " + std::string(10, ' ') + "DP-2 1920x1080@60Hz", "%s",
          lines[4].c_str());
    CHECK(lines[6] == trimmed(plain[6]) && lines[7] == "    ▀██▀ ▀██▀ ▀██▀", "trailing spaces go");
    CHECK(text.find('\x1b') == std::string::npos);
    auto bare = lines_of(render(palette, "paw 0.1.1", fields, {false, false, 0}));
    CHECK(bare.size() == 6 && bare[0] == "paw 0.1.1" && bare[1] == repeated("─", 9) &&
          bare[2] == "profile   washi · light");
    auto narrow = lines_of(render(palette, "paw 0.1.1", fields, {false, true, 40}));
    CHECK(narrow[0] == "paw 0.1.1", "a terminal too narrow for the logo goes without it");
    CHECK(lines_of(render(palette, "paw 0.1.1", fields, {false, true, 80}))[0] ==
          plain[0] + "   paw 0.1.1");
    auto painted = render(palette, "paw 0.1.1", fields, {true, true, 0});
    CHECK(painted.find("\x1b[1m\x1b[38;2;") != std::string::npos, "bold, coloured labels");
    CHECK(painted.find("███") != std::string::npos, "a strip of the palette");
    auto strip = lines_of(painted).back();
    CHECK(strip.find("\x1b[38;2;143;193;236m███") != std::string::npos, "%s", strip.c_str());
    return failures != 0;
}
