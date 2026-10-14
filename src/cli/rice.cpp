// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw rice`: a profile's look packed into one file to share (`pack`), and a rice tried on
 * (`try`): checked, installed beside the others, switched to at once, and taken back by the
 * compositor unless it is kept within 15 seconds (`keep`, `revert`, or the shell's dialog).
 * `list` and `remove` look after the installed ones. */
#include "paw/autostart.hpp"
#include "paw/cli.hpp"
#include "paw/config.hpp"
#include "paw/rice.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace paw::cli {
namespace {
const char *usage_text =
    "usage: paw rice pack [PROFILE] [-o FILE] [--name NAME] [--author TEXT] "
    "[--description TEXT] [--extra FILE]... [--config PATH]\n"
    "       paw rice try FILE|DIRECTORY|URL [--config PATH]\n"
    "       paw rice keep | revert | list | remove NAME";
const std::vector<std::string> picture_types = {".png", ".jpg", ".jpeg", ".webp", ".svg"};

[[noreturn]] void usage() { throw std::runtime_error(usage_text); }

// The configuration `paw` would load: `given`, else the personal one, else the installed one.
std::filesystem::path configuration(const std::filesystem::path &given) {
    if (!given.empty())
        return given;
    std::filesystem::path personal;
    if (const auto *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg == '/')
        personal = std::filesystem::path(xdg) / "paw/init.lua";
    else if (const auto *home = std::getenv("HOME"); home && *home)
        personal = std::filesystem::path(home) / ".config/paw/init.lua";
    std::error_code failure;
    if (!personal.empty() && std::filesystem::exists(personal, failure))
        return personal;
    if (auto shipped = default_config_path(); std::filesystem::exists(shipped, failure))
        return shipped;
    throw std::runtime_error("no configuration found; pass --config PATH");
}
std::string read_file(const std::filesystem::path &path, size_t most) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
        throw std::runtime_error("cannot read " + path.string() + ": " + std::strerror(errno));
    if (!S_ISREG(info.st_mode))
        throw std::runtime_error(path.string() + " is not a file");
    if (static_cast<uintmax_t>(info.st_size) > most)
        throw std::runtime_error(path.string() + " is larger than " + std::to_string(most >> 20) +
                                 " MiB");
    std::ifstream file(path, std::ios::binary);
    std::string contents{std::istreambuf_iterator<char>(file), {}};
    if (!file && !file.eof())
        throw std::runtime_error("cannot read " + path.string());
    return contents;
}
void write_file(const std::filesystem::path &path, const std::string &contents) {
    auto temporary = path;
    temporary += ".new";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << contents;
        if (!file.flush())
            throw std::runtime_error("cannot write " + temporary.string());
    }
    std::filesystem::rename(temporary, path);
}
std::string size_text(size_t bytes) {
    char text[32];
    if (bytes >= 1000 * 1000)
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / 1e6);
    else if (bytes >= 1000)
        std::snprintf(text, sizeof(text), "%zu kB", (bytes + 500) / 1000);
    else
        std::snprintf(text, sizeof(text), "%zu bytes", bytes);
    return text;
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](char c) { return std::tolower(static_cast<unsigned char>(c)); });
    return text;
}
// "~/..." for a path in the home directory, which reads shorter.
std::string shown(const std::filesystem::path &path) {
    std::string text = path.string();
    if (const auto *home = std::getenv("HOME"); home && *home) {
        std::string prefix = std::string(home) + "/";
        if (text.starts_with(prefix))
            return "~/" + text.substr(prefix.size());
    }
    return text;
}
std::string login_name() {
    if (const auto *entry = getpwuid(getuid()); entry && entry->pw_name)
        return entry->pw_name;
    const auto *user = std::getenv("USER");
    return user ? user : "";
}
// The wallpaper the shell's picker shows for `profile` in place of `configured`, if one was
// picked: its state file has a line "PROFILE<tab>CONFIGURED<tab>PICKED" for it.
std::optional<std::string> picked_wallpaper(const std::string &profile,
                                            const std::string &configured) {
    std::ifstream file(state_directory() / "wallpapers");
    std::string line;
    while (std::getline(file, line)) {
        auto first = line.find('\t'), second = line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            continue;
        if (line.substr(0, first) == profile &&
            line.substr(first + 1, second - first - 1) == configured && second + 1 < line.size())
            return line.substr(second + 1);
    }
    return std::nullopt;
}
// The words after the subcommand's verb, with `--config PATH` taken out.
std::vector<std::string> words_after(int argc, char **argv, int from,
                                     std::filesystem::path &config) {
    std::vector<std::string> words;
    for (int i = from; i < argc; ++i) {
        std::string word = argv[i];
        if (word == "--config") {
            if (i + 1 >= argc)
                usage();
            config = argv[++i];
        } else {
            words.push_back(word);
        }
    }
    return words;
}

int pack(const std::vector<std::string> &words, const std::filesystem::path &given) {
    std::filesystem::path output;
    std::optional<std::string> profile, name, author, description;
    std::vector<std::filesystem::path> extras;
    for (size_t i = 0; i < words.size(); ++i) {
        const auto &word = words[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= words.size())
                usage();
            return words[++i];
        };
        if (word == "-o" || word == "--output")
            output = value();
        else if (word == "--name")
            name = value();
        else if (word == "--author")
            author = value();
        else if (word == "--description")
            description = value();
        else if (word == "--extra")
            extras.emplace_back(value());
        else if (!profile && !word.starts_with("-"))
            profile = word;
        else
            usage();
    }
    auto config_path = configuration(given);
    auto config = load_config(config_path);
    if (!profile)
        profile = config.profile;
    else if (std::find(config.profiles.begin(), config.profiles.end(), *profile) ==
             config.profiles.end())
        throw std::runtime_error("no profile " + *profile +
                                 (config.profiles.empty()
                                      ? "; the configuration has none"
                                      : "; the profiles are " + join(config.profiles)));
    std::vector<std::string> left_out;
    auto look = rice_look(configured_look(config_path, *profile), &left_out);
    if (!name)
        name = profile->empty() ? "look" : *profile;
    if (!author)
        author = login_name();
    if (!description)
        description = profile->empty() ? "A look made with paw."
                                       : "The " + *profile + " look, made with paw.";

    // The pictures it names, from wherever they are, go into the rice.
    std::map<std::string, std::string> files;
    std::vector<std::pair<std::string, std::string>> went_in; // a file, and where from
    if (!look.field("shell"))
        look.fields.emplace_back("shell", RiceData{});
    auto &shell = *look.field("shell");
    for (const auto &[key, base] : {std::pair{"wallpaper", "wallpaper"}, {"start_icon", "start"}}) {
        auto *setting = shell.field(key);
        std::string configured = setting && setting->is_string() ? setting->string : "";
        std::string source = configured;
        if (std::string(key) == "wallpaper")
            source = picked_wallpaper(*profile, configured).value_or(configured);
        if (source.empty())
            continue; // none, or the one the macOS style draws
        std::filesystem::path path(source);
        if (path.is_relative())
            path = config_path.parent_path() / path;
        auto type = lower(path.extension().string());
        try {
            if (std::find(picture_types.begin(), picture_types.end(), type) ==
                picture_types.end())
                throw std::runtime_error("not a PNG, JPEG, WebP or SVG picture");
            auto file = base + type;
            files[file] = read_file(path, max_rice_file);
            if (setting)
                *setting = RiceData::string_of(file);
            else
                shell.fields.emplace_back(key, RiceData::string_of(file));
            went_in.emplace_back(file, shown(path));
        } catch (const std::exception &error) {
            std::erase_if(shell.fields, [&](const auto &field) { return field.first == key; });
            left_out.push_back(std::string("shell.") + key + " (" + shown(path) + ": " +
                               error.what() + ")");
        }
    }
    // A kitty theme named for the profile, and the extras asked for.
    RiceData extra_list;
    auto add_extra = [&](const std::filesystem::path &from, const std::string &file) {
        if (!rice_path_valid(file))
            throw std::runtime_error("a rice cannot name " + from.filename().string() +
                                     "; rename it to letters, digits, '.', '-' and '_'");
        if (files.contains(file))
            throw std::runtime_error("the rice holds " + file + " already");
        files[file] = read_file(from, max_rice_file);
        extra_list.items.push_back(RiceData::string_of(file));
        went_in.emplace_back(file, shown(from));
    };
    std::filesystem::path config_home;
    if (const auto *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg == '/')
        config_home = xdg;
    else if (const auto *home = std::getenv("HOME"); home && *home)
        config_home = std::filesystem::path(home) / ".config";
    std::error_code failure;
    if (auto kitty = config_home / "kitty/themes" / (*profile + ".conf");
        !profile->empty() && !config_home.empty() &&
        std::filesystem::is_regular_file(kitty, failure))
        add_extra(kitty, "kitty/" + kitty.filename().string());
    for (const auto &extra : extras)
        add_extra(extra, "extras/" + extra.filename().string());

    RiceData manifest;
    manifest.fields.emplace_back("format", RiceData::integer_of(1));
    manifest.fields.emplace_back("name", RiceData::string_of(*name));
    manifest.fields.emplace_back("author", RiceData::string_of(*author));
    manifest.fields.emplace_back("description", RiceData::string_of(*description));
    manifest.fields.emplace_back("profile", look);
    if (!extra_list.items.empty())
        manifest.fields.emplace_back("extras", extra_list);
    auto text = "-- A rice for paw: `paw rice try FILE` tries it on. This file is data, read and "
                "never run:\n-- an appearance profile's settings, and the files it names "
                "beside it.\nreturn " +
                write_rice_data(manifest) + "\n";
    parse_rice(text); // what was packed can be tried
    files["rice.lua"] = text;

    if (output.empty())
        output = *name + ".rice.tar";
    auto archive = rice_archive(*name, files);
    if (output.string().ends_with(".gz")) {
        auto plain = output;
        plain += ".tar";
        write_file(plain, archive);
        try {
            archive = run_filter({"gzip", "-c", "-n"}, plain, max_rice_archive);
        } catch (...) {
            std::filesystem::remove(plain, failure);
            throw;
        }
        std::filesystem::remove(plain, failure);
    }
    write_file(output, archive);

    size_t settings = 0;
    for (const auto &change : rice_changes(RiceData{}, look))
        settings += !change.after.empty();
    std::cout << "Packed " << (profile->empty() ? "the look" : "the profile " + *profile)
              << " as the rice " << *name << " into " << output.string() << " ("
              << size_text(archive.size()) << "):\n";
    std::cout << "  rice.lua: " << settings << " settings, by " << write_rice_data(
                     RiceData::string_of(*author))
              << '\n';
    for (const auto &[file, from] : went_in)
        std::cout << "  " << file << " (" << size_text(files[file].size()) << "), from " << from
                  << '\n';
    if (!left_out.empty()) {
        std::cout << "Left out, as a rice holds only how the desktop looks:\n";
        for (const auto &setting : left_out)
            std::cout << "  " << setting << '\n';
    }
    return 0;
}

// The files of an archive, gzip-compressed or not; `file` holds it, if anything does.
std::map<std::string, std::string> archive_files(std::string archive, std::filesystem::path file) {
    if (archive.starts_with("\x1f\x8b")) {
        std::error_code failure;
        std::filesystem::path temporary;
        if (file.empty()) {
            auto pattern = (std::filesystem::temp_directory_path() / "paw-rice-XXXXXX").string();
            int fd = mkstemp(pattern.data());
            if (fd < 0)
                throw std::runtime_error("cannot make a temporary file: " +
                                         std::string(std::strerror(errno)));
            close(fd);
            temporary = file = pattern;
            write_file(file, archive);
        }
        try {
            archive = run_filter({"gzip", "-dc"}, file, max_rice_archive);
        } catch (...) {
            if (!temporary.empty())
                std::filesystem::remove(temporary, failure);
            throw;
        }
        if (!temporary.empty())
            std::filesystem::remove(temporary, failure);
    }
    return rice_archive_files(archive);
}
// rice.lua and the files a rice names, from a directory, an archive or an https URL.
std::map<std::string, std::string> rice_source(const std::string &source) {
    if (source.find("://") != std::string::npos) {
        if (!source.starts_with("https://"))
            throw std::runtime_error("a rice is downloaded over https alone");
        if (!executable("curl"))
            throw std::runtime_error("downloading a rice needs curl");
        std::cout << "Downloading " << printable_text(source) << '\n' << std::flush;
        // Into memory, and cut short past the most a rice may be.
        auto limit = std::to_string(max_rice_file);
        auto archive = run_filter({"curl", "--proto", "=https", "--proto-redir", "=https", "--fail",
                                   "--silent", "--show-error", "--location", "--max-redirs", "5",
                                   "--max-filesize", limit, "--max-time", "300", "--output", "-",
                                   source},
                                  "/dev/null", max_rice_file);
        return archive_files(std::move(archive), {});
    }
    std::error_code failure;
    if (std::filesystem::is_directory(source, failure))
        return rice_files(source);
    return archive_files(read_file(source, max_rice_archive), source);
}

int try_on(const std::vector<std::string> &words, const std::filesystem::path &given) {
    if (words.size() != 1)
        usage();
    if (auto trial = load_rice_trial())
        throw std::runtime_error(trial->rice + " is on trial; keep it or revert it first "
                                               "(paw rice keep, paw rice revert)");
    // Asked first, so that nothing changes outside a session that can take it back.
    auto running = query("rice_trial");
    if (!running.empty() && !running[0].empty() && running[0][0] != "-")
        throw std::runtime_error(running[0][0] + " is on trial; keep it or revert it first");
    auto files = rice_source(words[0]);
    auto manifest = files.find("rice.lua");
    if (manifest == files.end())
        throw std::runtime_error("the rice has no rice.lua");
    auto rice = parse_rice(manifest->second);
    auto config_path = configuration(given);
    auto config = load_config(config_path);
    std::vector<std::string> own;
    for (const auto &profile : config.profiles)
        if (std::find(config.rices.begin(), config.rices.end(), profile) == config.rices.end())
            own.push_back(profile);
    auto name = rice_install_name(rice.name, own);
    auto staged = stage_rice(files, rices_directory());
    std::error_code failure;
    try {
        // The configuration with it in place, the one of its name it replaces gone.
        auto checked = read_rice(staged);
        checked.directory = rices_directory() / name;
        auto rices = installed_rices();
        std::erase_if(rices, [&](const Rice &other) { return other.directory == checked.directory; });
        rices.push_back(checked);
        auto with = parse_config(read_file(config_path, max_rice_source * 16),
                                 "@" + config_path.string(), config_path.parent_path(), name,
                                 rices);
        if (with.profile != name) {
            std::string why = "the configuration passes over it";
            for (const auto &problem : with.rice_problems)
                if (problem.starts_with(name + ": "))
                    why = problem.substr(name.size() + 2);
            throw std::runtime_error("cannot try " + rice.name + ": " + printable_text(why));
        }
        std::cout << rice.name;
        if (!rice.author.empty())
            std::cout << ", by " << rice.author;
        std::cout << '\n';
        if (!rice.description.empty()) {
            std::istringstream lines(rice.description);
            for (std::string line; std::getline(lines, line);)
                std::cout << "  " << line << '\n';
        }
        auto before = rice_look(configured_look(config_path, config.profile));
        std::vector<RiceChange> changes;
        for (const auto &change : rice_changes(before, rice.profile))
            if (!change.after.empty())
                changes.push_back(change);
        if (changes.empty()) {
            std::cout << "It looks as the profile in use does.\n";
        } else {
            std::cout << "It changes " << changes.size()
                      << (changes.size() == 1 ? " setting" : " settings") << ":\n";
            size_t width = 0;
            for (const auto &change : changes)
                width = std::max(width, change.setting.size());
            for (const auto &change : changes)
                std::cout << "  " << change.setting
                          << std::string(width - change.setting.size() + 2, ' ')
                          << (change.before.empty() ? "unset" : change.before) << " -> "
                          << change.after << '\n';
        }
        if (!rice.extras.empty()) {
            std::cout << "It brings files for other programs, which it leaves for you to put in "
                         "place:\n";
            for (const auto &extra : rice.extras)
                std::cout << "  " << shown(rices_directory() / name / extra) << '\n';
        }
        begin_rice_trial(staged, name);
    } catch (...) {
        std::filesystem::remove_all(staged, failure);
        throw;
    }
    Reply reply;
    try {
        reply = request("rice try " + name);
    } catch (...) {
        revert_rice_trial();
        throw;
    }
    if (!reply.ok) {
        revert_rice_trial();
        auto why = reply.body;
        while (!why.empty() && why.back() == '\n')
            why.pop_back();
        throw std::runtime_error(why.starts_with("error: ") ? why.substr(7) : why);
    }
    auto seconds = (std::atoi(reply.body.c_str()) + 500) / 1000;
    std::cout << "Trying " << name << ": the previous look comes back in " << seconds
              << " seconds unless it is kept, in the dialog or with paw rice keep.\n";
    return 0;
}

// `paw rice keep` and `revert`, asked of the session.
int end_trial(bool keep) {
    auto reply = request(keep ? "rice keep" : "rice revert");
    if (reply.ok) {
        auto rows_of = rows(reply.body);
        auto name = rows_of.empty() || rows_of[0].empty() ? std::string() : rows_of[0][0];
        if (keep)
            std::cout << "Kept " << name << ": it is a profile now, picked from the Appearance "
                                            "menu as the others are.\n";
        else
            std::cout << "Took " << name << " back: the previous look is in use again.\n";
        return 0;
    }
    // A trial the session does not know of was left by one that ended halfway.
    if (reply.body.find("no rice is on trial") == std::string::npos) {
        auto why = reply.body;
        while (!why.empty() && why.back() == '\n')
            why.pop_back();
        throw std::runtime_error(why.starts_with("error: ") ? why.substr(7) : why);
    }
    if (auto trial = keep ? keep_rice_trial() : revert_rice_trial()) {
        if (!keep)
            (void)request("reload");
        std::cout << (keep ? "Kept " : "Took back ") << trial->rice << ".\n";
        return 0;
    }
    throw std::runtime_error("no rice is on trial");
}

int list_rices() {
    std::vector<std::string> problems;
    auto rices = installed_rices(&problems);
    auto trial = load_rice_trial();
    auto in_use = saved_profile();
    if (rices.empty())
        std::cout << "No rices are installed; paw rice try FILE tries one on.\n";
    for (const auto &rice : rices) {
        auto name = rice.directory.filename().string();
        std::cout << name;
        if (rice.name != name)
            std::cout << " (" << rice.name << ")";
        if (!rice.author.empty())
            std::cout << ", by " << rice.author;
        if (trial && trial->rice == name)
            std::cout << " [on trial]";
        else if (in_use == name)
            std::cout << " [in use]";
        std::cout << '\n';
        if (!rice.description.empty())
            std::cout << "  " << rice.description.substr(0, rice.description.find('\n')) << '\n';
    }
    for (const auto &problem : problems)
        std::cout << "Cannot read " << printable_text(problem) << '\n';
    return 0;
}

int uninstall(const std::vector<std::string> &words) {
    if (words.size() != 1)
        usage();
    const auto &name = words[0];
    auto rices = installed_rices();
    auto found = std::find_if(rices.begin(), rices.end(), [&](const Rice &rice) {
        return rice.directory.filename() == name;
    });
    auto directory = rices_directory() / name;
    std::error_code failure;
    if (found == rices.end() && !(rice_path_valid(name) && name.find('/') == std::string::npos &&
                                  std::filesystem::is_directory(directory, failure)))
        throw std::runtime_error("no rice " + name + " is installed");
    if (auto trial = load_rice_trial(); trial && trial->rice == name)
        throw std::runtime_error(name + " is on trial; keep it or revert it first");
    if (saved_profile() == name)
        throw std::runtime_error(name + " is in use; pick another profile first");
    std::filesystem::remove_all(directory);
    std::cout << "Removed " << name << ".\n";
    // The session forgets it as it reads the configuration again.
    if (const auto *socket = std::getenv("PAW_SOCKET"); socket && *socket) {
        try {
            (void)request("reload");
        } catch (const std::exception &) {
        }
    }
    return 0;
}
} // namespace

int rice(int argc, char **argv) {
    if (argc < 3)
        usage();
    std::string verb = argv[2];
    std::filesystem::path config;
    auto words = words_after(argc, argv, 3, config);
    if (verb == "pack")
        return pack(words, config);
    if (verb == "try")
        return try_on(words, config);
    if ((verb == "keep" || verb == "revert") && words.empty())
        return end_trial(verb == "keep");
    if (verb == "list" && words.empty())
        return list_rices();
    if (verb == "remove")
        return uninstall(words);
    usage();
}

} // namespace paw::cli
