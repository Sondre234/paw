// SPDX-License-Identifier: GPL-3.0-or-later
/* Rices (include/paw/rice.hpp): rice.lua read as data and written back, what a rice may hold,
 * the installed rices, and the record of a rice on trial. */
#include "paw/rice.hpp"
#include "paw/config.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace paw {

RiceData RiceData::string_of(std::string text) {
    RiceData data;
    data.type = Type::String;
    data.string = std::move(text);
    return data;
}
RiceData RiceData::integer_of(long long value) {
    RiceData data;
    data.type = Type::Integer;
    data.integer = value;
    return data;
}
RiceData RiceData::boolean_of(bool value) {
    RiceData data;
    data.type = Type::Boolean;
    data.boolean = value;
    return data;
}
const RiceData *RiceData::field(std::string_view name) const {
    for (const auto &[key, value] : fields)
        if (key == name)
            return &value;
    return nullptr;
}
RiceData *RiceData::field(std::string_view name) {
    for (auto &[key, value] : fields)
        if (key == name)
            return &value;
    return nullptr;
}

namespace {
constexpr int max_depth = 16;
constexpr size_t max_values = 20000;
const std::set<std::string_view> keywords = {
    "and",   "break", "do",  "else", "elseif", "end",    "false", "for",  "function", "goto",
    "if",    "in",    "local", "nil", "not",   "or",     "repeat", "return", "then",   "true",
    "until", "while"};

bool name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool name_part(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// rice.lua's reader: Lua's lexical rules for comments, strings and numbers, and its table
// constructor, with nothing that computes.
struct Reader {
    const std::string &text;
    const std::string &file;
    size_t at = 0;
    int line = 1;
    size_t values = 0;

    [[noreturn]] void fail(const std::string &what) const {
        throw std::runtime_error(file + ":" + std::to_string(line) + ": " + what);
    }
    bool more() const { return at < text.size(); }
    char peek(size_t ahead = 0) const {
        return at + ahead < text.size() ? text[at + ahead] : '\0';
    }
    void advance() {
        if (text[at] == '\n')
            ++line;
        ++at;
    }
    // The level of a long bracket opening at `at` ("[[" is 0, "[==[" 2), or -1 for none.
    int long_bracket() const {
        if (peek() != '[')
            return -1;
        size_t level = 0;
        while (peek(1 + level) == '=')
            ++level;
        return peek(1 + level) == '[' ? static_cast<int>(level) : -1;
    }
    // The text of a long string or comment from its opening bracket, which is at `at`.
    std::string long_text(int level) {
        int opened = line;
        at += static_cast<size_t>(level) + 2;
        // A newline right after the opening bracket is not part of the text.
        if (peek() == '\r' || peek() == '\n') {
            char first = peek();
            advance();
            if ((peek() == '\r' || peek() == '\n') && peek() != first)
                advance();
        }
        std::string close = "]" + std::string(static_cast<size_t>(level), '=') + "]";
        size_t end = text.find(close, at);
        if (end == std::string::npos) {
            line = opened;
            fail("a long string or comment is not closed");
        }
        std::string result = text.substr(at, end - at);
        while (at < end)
            advance();
        at = end + close.size();
        return result;
    }
    // Skips spaces and comments.
    void skip() {
        while (more()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
                advance();
            } else if (c == '-' && peek(1) == '-') {
                at += 2;
                if (int level = long_bracket(); level >= 0) {
                    long_text(level);
                } else {
                    while (more() && peek() != '\n')
                        advance();
                }
            } else {
                break;
            }
        }
    }
    bool take(char c) {
        skip();
        if (peek() != c || !more())
            return false;
        advance();
        return true;
    }
    std::string name() {
        size_t start = at;
        while (more() && name_part(peek()))
            advance();
        return text.substr(start, at - start);
    }
    static void utf8(std::string &out, unsigned long code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    static int hex(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    }
    // A string in quotes, from its opening quote at `at`.
    std::string quoted() {
        char quote = peek();
        advance();
        std::string out;
        while (true) {
            if (!more() || peek() == '\n' || peek() == '\r')
                fail("a string is not closed on its line");
            char c = peek();
            advance();
            if (c == quote)
                return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (!more())
                fail("a string is not closed");
            char e = peek();
            advance();
            switch (e) {
            case 'a': out += '\a'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'v': out += '\v'; break;
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            case '\'': out += '\''; break;
            case '\n':
            case '\r':
                // A line break, written as one.
                out += '\n';
                if ((peek() == '\n' || peek() == '\r') && peek() != e)
                    advance();
                break;
            case 'z':
                while (more() && std::isspace(static_cast<unsigned char>(peek())))
                    advance();
                break;
            case 'x': {
                int high = hex(peek()), low = hex(peek(1));
                if (high < 0 || low < 0)
                    fail("\\x takes two hexadecimal digits");
                at += 2;
                out += static_cast<char>(high * 16 + low);
                break;
            }
            case 'u': {
                if (peek() != '{')
                    fail("\\u takes a code point in braces");
                advance();
                unsigned long code = 0;
                int digits = 0;
                for (; hex(peek()) >= 0; ++digits, advance())
                    code = code * 16 + static_cast<unsigned long>(hex(peek()));
                if (!digits || digits > 6 || peek() != '}' || code > 0x10ffff)
                    fail("\\u takes a code point up to 10FFFF in braces");
                advance();
                utf8(out, code);
                break;
            }
            default:
                if (std::isdigit(static_cast<unsigned char>(e))) {
                    int code = e - '0';
                    for (int i = 0; i < 2 && std::isdigit(static_cast<unsigned char>(peek()));
                         ++i, advance())
                        code = code * 10 + (peek() - '0');
                    if (code > 255)
                        fail("a decimal escape is at most \\255");
                    out += static_cast<char>(code);
                } else {
                    fail(std::string("no escape \\") + e + " in a string");
                }
            }
        }
    }
    RiceData number(bool negative) {
        size_t start = at;
        RiceData result;
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            at += 2;
            size_t digits = at;
            while (hex(peek()) >= 0)
                ++at;
            unsigned long long value = 0;
            auto [end, error] = std::from_chars(text.data() + digits, text.data() + at, value, 16);
            if (digits == at || error != std::errc() || name_part(peek()) || peek() == '.')
                fail("a hexadecimal number is an integer of up to 16 digits");
            result.type = RiceData::Type::Integer;
            // Wrapping around as Lua's do: negated in unsigned arithmetic, -0x8000000000000000
            // is the least integer rather than an overflow.
            result.integer = static_cast<long long>(negative ? 0 - value : value);
            return result;
        }
        bool fraction = false;
        while (std::isdigit(static_cast<unsigned char>(peek())))
            ++at;
        if (peek() == '.') {
            fraction = true;
            ++at;
            while (std::isdigit(static_cast<unsigned char>(peek())))
                ++at;
        }
        if (peek() == 'e' || peek() == 'E') {
            fraction = true;
            ++at;
            if (peek() == '+' || peek() == '-')
                ++at;
            if (!std::isdigit(static_cast<unsigned char>(peek())))
                fail("a number's exponent needs digits");
            while (std::isdigit(static_cast<unsigned char>(peek())))
                ++at;
        }
        auto digits = text.substr(start, at - start);
        if (digits == "." || name_part(peek()) || peek() == '.')
            fail("malformed number " + digits);
        if (negative)
            digits.insert(digits.begin(), '-');
        if (!fraction) {
            auto [end, error] =
                std::from_chars(digits.data(), digits.data() + digits.size(), result.integer);
            if (error != std::errc())
                fail("the integer " + digits + " is out of range");
            result.type = RiceData::Type::Integer;
        } else {
            char *end = nullptr;
            result.number = std::strtod(digits.c_str(), &end);
            if (!std::isfinite(result.number))
                fail("the number " + digits + " is out of range");
            result.type = RiceData::Type::Number;
        }
        return result;
    }
    RiceData value(int depth) {
        skip();
        if (++values > max_values)
            fail("rice.lua holds too many values");
        RiceData result;
        char c = peek();
        if (!more())
            fail("a value is missing at the end");
        if (c == '{')
            return table(depth + 1);
        if (c == '"' || c == '\'') {
            result.type = RiceData::Type::String;
            result.string = quoted();
            return result;
        }
        if (int level = long_bracket(); level >= 0) {
            result.type = RiceData::Type::String;
            result.string = long_text(level);
            return result;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            bool negative = c == '-';
            if (negative) {
                advance();
                skip();
                if (!std::isdigit(static_cast<unsigned char>(peek())) && peek() != '.')
                    fail("only a number can follow '-'");
            }
            return number(negative);
        }
        if (name_start(c)) {
            auto word = name();
            if (word == "true" || word == "false") {
                result.type = RiceData::Type::Boolean;
                result.boolean = word == "true";
                return result;
            }
            fail(word == "nil" ? "nil has no place in a rice; leave the setting out"
                               : "'" + word + "' is a name, and a rice holds only data; quote "
                                              "it to make it a string");
        }
        fail(std::string("unexpected '") + c + "'; a rice holds strings, numbers, booleans and "
                                               "tables");
    }
    RiceData table(int depth) {
        if (depth > max_depth)
            fail("tables are nested too deeply");
        advance(); // {
        RiceData result;
        std::set<std::string> names;
        auto add = [&](std::string key, RiceData item) {
            if (!names.insert(key).second)
                fail("'" + key + "' is given twice");
            result.fields.emplace_back(std::move(key), std::move(item));
        };
        while (true) {
            skip();
            if (take('}'))
                return result;
            if (peek() == '[' && long_bracket() < 0) {
                advance();
                auto key = value(depth);
                if (!key.is_string())
                    fail("a key in brackets must be a string");
                if (!take(']') || !take('='))
                    fail("expected '] =' after a key in brackets");
                add(key.string, value(depth));
            } else if (name_start(peek())) {
                size_t start = at;
                int start_line = line;
                auto word = name();
                skip();
                if (peek() == '=' && peek(1) != '=' && !keywords.contains(word)) {
                    advance();
                    add(word, value(depth));
                } else {
                    at = start;
                    line = start_line;
                    result.items.push_back(value(depth));
                }
            } else {
                result.items.push_back(value(depth));
            }
            if (!take(',') && !take(';')) {
                if (take('}'))
                    return result;
                fail("expected ',' or '}' in a table");
            }
        }
    }
};

void write_string(std::string &out, const std::string &text) {
    out += '"';
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        // A whole UTF-8 sequence stays as it is, but for C1 control characters.
        size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool whole = length > 1 && i + length <= text.size();
        for (size_t j = 1; whole && j < length; ++j)
            whole = (static_cast<unsigned char>(text[i + j]) & 0xc0) == 0x80;
        if (whole && !(c == 0xc2 && static_cast<unsigned char>(text[i + 1]) < 0xa0)) {
            out.append(text, i, length);
            i += length - 1;
            continue;
        }
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20 || c >= 0x7f) {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\%03u", c);
            out += escape;
        } else {
            out += static_cast<char>(c);
        }
    }
    out += '"';
}
bool scalar(const RiceData &data) { return !data.is_table(); }
void write_value(std::string &out, const RiceData &data, int indent) {
    switch (data.type) {
    case RiceData::Type::Boolean: out += data.boolean ? "true" : "false"; return;
    case RiceData::Type::Integer: out += std::to_string(data.integer); return;
    case RiceData::Type::Number: {
        if (!std::isfinite(data.number))
            throw std::runtime_error("a rice cannot hold an infinite number");
        char buffer[64];
        auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), data.number);
        std::string text(buffer, end);
        // Written without a point, it would read back as an integer.
        if (text.find_first_of(".e") == std::string::npos)
            text += ".0";
        out += text;
        return;
    }
    case RiceData::Type::String: write_string(out, data.string); return;
    case RiceData::Type::Table: break;
    }
    if (data.items.empty() && data.fields.empty()) {
        out += "{}";
        return;
    }
    // A short list of plain values goes on one line: { 7, 7 }.
    if (data.fields.empty() && std::all_of(data.items.begin(), data.items.end(), scalar)) {
        std::string line = "{ ";
        for (size_t i = 0; i < data.items.size(); ++i) {
            write_value(line, data.items[i], 0);
            line += i + 1 < data.items.size() ? ", " : " }";
        }
        if (line.size() <= 72) {
            out += line;
            return;
        }
    }
    std::string inner(static_cast<size_t>(indent + 1) * 4, ' ');
    out += "{\n";
    for (const auto &item : data.items) {
        out += inner;
        write_value(out, item, indent + 1);
        out += ",\n";
    }
    for (const auto &[key, value] : data.fields) {
        out += inner;
        bool bare = !key.empty() && name_start(key[0]) &&
                    std::all_of(key.begin(), key.end(), name_part) && !keywords.contains(key);
        if (bare) {
            out += key;
        } else {
            out += '[';
            write_string(out, key);
            out += ']';
        }
        out += " = ";
        write_value(out, value, indent + 1);
        out += ",\n";
    }
    out += std::string(static_cast<size_t>(indent) * 4, ' ') + "}";
}
} // namespace

RiceData parse_rice_data(const std::string &source, const std::string &name) {
    Reader reader{source, name};
    reader.skip();
    if (reader.name() != "return")
        reader.at = 0, reader.line = 1;
    reader.skip();
    if (reader.peek() != '{')
        reader.fail("rice.lua is one table, `return { ... }`");
    auto result = reader.value(0);
    reader.take(';');
    reader.skip();
    if (reader.more())
        reader.fail("nothing may follow the table");
    return result;
}

std::string write_rice_data(const RiceData &data, int indent) {
    std::string out;
    write_value(out, data, indent);
    return out;
}

namespace {
// The settings a rice may set whole, tables and all.
const std::set<std::string> look_settings = {
    "appearance.background",
    "appearance.color_scheme",
    "appearance.cursor_theme",
    "appearance.cursor_size",
    "appearance.icon_theme",
    "appearance.ambient",
    "appearance.cursor_trail",
    "windows.border_width",
    "windows.corner_radius",
    "windows.round",
    "windows.border_color",
    "windows.border_inactive_color",
    "windows.opacity",
    "windows.inactive_opacity",
    "windows.dim_inactive",
    "windows.dim_duration",
    "windows.urgent_color",
    "windows.controls",
    "windows.shadow",
    "windows.blur",
    "windows.lift",
    "windows.magnet.guide_color",
    "windows.snap.color",
    "shell.style",
    "shell.panel_height",
    "shell.panel_position",
    "shell.panel_dock",
    "shell.panel_margin",
    "shell.panel_radius",
    "shell.frame",
    "shell.blur",
    "shell.font",
    "shell.font_size",
    "shell.icons_only",
    "shell.group_windows",
    "shell.workspaces_shown",
    "shell.mirror_menus",
    "shell.accent",
    "shell.panel_color",
    "shell.text_color",
    "shell.wallpaper",
    "shell.start_icon",
    "shell.widgets",
    "shell.tiling_bar",
};
const std::vector<std::string> picture_settings = {"wallpaper", "start_icon"};
const std::vector<std::string> picture_types = {".png", ".jpg", ".jpeg", ".webp", ".svg"};
std::string joined(const std::vector<std::string> &setting) {
    std::string name;
    for (const auto &part : setting)
        name += (name.empty() ? "" : ".") + part;
    return name;
}
// Whether some allowed setting lies inside `prefix`, a table.
bool holds_allowed(const std::string &prefix) {
    auto at = look_settings.lower_bound(prefix + ".");
    return at != look_settings.end() && at->starts_with(prefix + ".");
}
// Text a terminal and the shell show as it is: UTF-8 without control characters, but for
// newlines where `lines` allows them.
bool plain_text(const std::string &text, bool lines) {
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if ((c < 0x20 && !(lines && c == '\n')) || c == 0x7f)
                return false;
            continue;
        }
        size_t length = (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!length || i + length > text.size())
            return false;
        for (size_t j = 1; j < length; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80)
                return false;
        if (c == 0xc2 && static_cast<unsigned char>(text[i + 1]) < 0xa0)
            return false; // C1 control characters
        i += length - 1;
    }
    return true;
}
const RiceData &string_field(const RiceData &table, const char *name, bool required) {
    static const RiceData none = RiceData::string_of("");
    const auto *found = table.field(name);
    if (!found && required)
        throw std::runtime_error(std::string("rice.lua needs ") + name);
    if (found && !found->is_string())
        throw std::runtime_error(std::string("rice.lua's ") + name + " must be a string");
    return found ? *found : none;
}
// Checks that every key in `data` is a name, as settings are.
void check_keys(const RiceData &data, const std::string &setting) {
    for (const auto &item : data.items)
        check_keys(item, setting);
    for (const auto &[key, value] : data.fields) {
        if (key.empty() || !name_start(key[0]) || !std::all_of(key.begin(), key.end(), name_part))
            throw std::runtime_error("rice.lua's " + setting + " holds a key that is no "
                                     "setting's name");
        check_keys(value, setting + "." + key);
    }
}
// Checks that `data`, at `setting`, holds only allowed settings.
void check_look(const RiceData &data, std::vector<std::string> &setting) {
    if (!data.items.empty())
        throw std::runtime_error("rice.lua's " + (setting.empty() ? "profile" : joined(setting)) +
                                 " must be keyed by name");
    for (const auto &[key, value] : data.fields) {
        setting.push_back(key);
        auto name = joined(setting);
        if (look_settings.contains(name)) {
            check_keys(value, name);
        } else {
            if (!value.is_table() || !holds_allowed(name))
                throw std::runtime_error("a rice cannot set " + printable_text(name) +
                                         ": it holds only how the desktop looks");
            check_look(value, setting);
        }
        setting.pop_back();
    }
}
// lstat of `path`, which must be a directory, or a regular file of at most max_rice_file
// bytes, and not a link.
void check_entry(const std::filesystem::path &path, bool directory) {
    struct stat info;
    if (lstat(path.c_str(), &info) != 0)
        throw std::runtime_error("the rice has no " + path.filename().string());
    if (S_ISLNK(info.st_mode))
        throw std::runtime_error(path.filename().string() + " in the rice is a link, which a "
                                                            "rice cannot hold");
    if (directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode))
        throw std::runtime_error(path.filename().string() + " in the rice is not a " +
                                 (directory ? "directory" : "file"));
    if (!directory && static_cast<uintmax_t>(info.st_size) > max_rice_file)
        throw std::runtime_error(path.filename().string() + " in the rice is larger than 50 MB");
}
// Checks each part of `relative` inside `directory`: directories, then a file.
std::filesystem::path checked_file(const std::filesystem::path &directory,
                                   const std::string &relative) {
    if (!rice_path_valid(relative))
        throw std::runtime_error("the rice names a file it cannot hold: " + relative);
    auto path = directory;
    std::filesystem::path parts(relative);
    for (auto part = parts.begin(); part != parts.end(); ++part) {
        path /= *part;
        check_entry(path, std::next(part) != parts.end());
    }
    return path;
}
std::string read_whole(const std::filesystem::path &path, size_t most) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("cannot read " + path.string() + ": " + std::strerror(errno));
    std::string contents;
    char buffer[65536];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
        contents.append(buffer, static_cast<size_t>(count));
        if (contents.size() > most) {
            close(fd);
            throw std::runtime_error(path.filename().string() + " is too large");
        }
    }
    int error = errno;
    close(fd);
    if (count < 0)
        throw std::runtime_error("cannot read " + path.string() + ": " + std::strerror(error));
    return contents;
}
std::filesystem::path state_paw() {
    if (const auto *xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg == '/')
        return std::filesystem::path(xdg) / "paw";
    if (const auto *home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local/state/paw";
    return {};
}
bool valid_profile_name(const std::string &name) {
    return !name.empty() && name.size() <= 32 && name != "next" && name != "prev" &&
           std::all_of(name.begin(), name.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
           });
}
} // namespace

std::string printable_text(const std::string &text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool whole = length > 0 && i + length <= text.size();
        for (size_t j = 1; whole && j < length; ++j)
            whole = (static_cast<unsigned char>(text[i + j]) & 0xc0) == 0x80;
        if (!whole || c < 0x20 || c == 0x7f ||
            (c == 0xc2 && static_cast<unsigned char>(text[i + 1]) < 0xa0)) {
            out += '?';
            continue;
        }
        out.append(text, i, length);
        i += length - 1;
    }
    return out;
}
bool rice_setting_allowed(const std::vector<std::string> &setting) {
    for (size_t length = 1; length <= setting.size(); ++length)
        if (look_settings.contains(joined({setting.begin(), setting.begin() + long(length)})))
            return true;
    return false;
}

bool rice_path_valid(const std::string &path) {
    if (path.empty() || path.size() > 255)
        return false;
    int parts = 0;
    for (size_t start = 0; start <= path.size(); ++parts) {
        auto end = std::min(path.find('/', start), path.size());
        auto part = std::string_view(path).substr(start, end - start);
        if (part.empty() || part.front() == '.' ||
            !std::all_of(part.begin(), part.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' ||
                       c == '_';
            }))
            return false;
        start = end + 1;
    }
    return parts <= 4;
}

Rice parse_rice(const std::string &source) {
    if (source.size() > max_rice_source)
        throw std::runtime_error("rice.lua is larger than 256 KiB");
    auto data = parse_rice_data(source);
    for (const auto &[key, value] : data.fields)
        if (key != "format" && key != "name" && key != "author" && key != "description" &&
            key != "profile" && key != "extras")
            throw std::runtime_error("rice.lua has no setting " + key +
                                     "; it holds format, name, author, description, profile "
                                     "and extras");
    if (!data.items.empty())
        throw std::runtime_error("rice.lua's table must be keyed by name");
    const auto *format = data.field("format");
    if (!format || format->type != RiceData::Type::Integer || format->integer != 1)
        throw std::runtime_error("rice.lua needs format = 1; a newer paw may read this rice");
    Rice rice;
    rice.name = string_field(data, "name", true).string;
    if (!valid_profile_name(rice.name))
        throw std::runtime_error("a rice's name is 1 to 32 letters, digits, '-' and '_', and "
                                 "not next or prev");
    rice.author = string_field(data, "author", false).string;
    if (rice.author.size() > 64 || !plain_text(rice.author, false))
        throw std::runtime_error("a rice's author is one line of at most 64 bytes of UTF-8 "
                                 "text");
    rice.description = string_field(data, "description", false).string;
    if (rice.description.size() > 512 || !plain_text(rice.description, true))
        throw std::runtime_error("a rice's description is at most 512 bytes of UTF-8 text");
    const auto *profile = data.field("profile");
    if (!profile || !profile->is_table())
        throw std::runtime_error("rice.lua needs profile, a table of appearance, windows and "
                                 "shell settings");
    std::vector<std::string> setting;
    check_look(*profile, setting);
    rice.profile = *profile;
    if (const auto *shell = profile->field("shell"))
        for (const auto &key : picture_settings) {
            const auto *picture = shell->field(key);
            if (!picture || (picture->is_string() && picture->string.empty()))
                continue;
            if (!picture->is_string())
                throw std::runtime_error("shell." + key + " must be a string");
            auto lower = picture->string;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](char c) { return std::tolower(static_cast<unsigned char>(c)); });
            if (!rice_path_valid(picture->string) ||
                std::none_of(picture_types.begin(), picture_types.end(),
                             [&](const std::string &type) { return lower.ends_with(type); }))
                throw std::runtime_error(
                    "shell." + key +
                    " must name a PNG, JPEG, WebP or SVG picture inside the rice, as "
                    "\"wallpaper.jpg\": " + write_rice_data(*picture));
            rice.pictures.push_back(picture->string);
        }
    if (const auto *extras = data.field("extras")) {
        if (!extras->is_table() || !extras->fields.empty() ||
            extras->items.size() > max_rice_extras)
            throw std::runtime_error("rice.lua's extras is a list of at most 16 files");
        for (const auto &extra : extras->items) {
            if (!extra.is_string() || !rice_path_valid(extra.string))
                throw std::runtime_error("an extra must name a file inside the rice, as "
                                         "\"kitty/theme.conf\": " +
                                         write_rice_data(extra));
            rice.extras.push_back(extra.string);
        }
    }
    return rice;
}

Rice read_rice(const std::filesystem::path &directory) {
    auto source = checked_file(directory, "rice.lua");
    auto rice = parse_rice(read_whole(source, max_rice_source));
    for (const auto *list : {&rice.pictures, &rice.extras})
        for (const auto &file : *list)
            checked_file(directory, file);
    rice.directory = directory;
    return rice;
}

std::map<std::string, std::string> rice_files(const std::filesystem::path &directory) {
    auto rice = read_rice(directory);
    std::map<std::string, std::string> files;
    files["rice.lua"] = read_whole(directory / "rice.lua", max_rice_source);
    for (const auto *list : {&rice.pictures, &rice.extras})
        for (const auto &file : *list)
            files[file] = read_whole(checked_file(directory, file), max_rice_file);
    return files;
}

std::filesystem::path stage_rice(const std::map<std::string, std::string> &files,
                                 const std::filesystem::path &parent) {
    auto manifest = files.find("rice.lua");
    if (manifest == files.end())
        throw std::runtime_error("the rice has no rice.lua");
    auto rice = parse_rice(manifest->second);
    std::error_code failure;
    std::filesystem::create_directories(parent, failure);
    auto pattern = (parent / ("." + rice.name + "-XXXXXX")).string();
    if (!mkdtemp(pattern.data()))
        throw std::runtime_error("cannot create a directory in " + parent.string() + ": " +
                                 std::strerror(errno));
    std::filesystem::path staged(pattern);
    try {
        std::vector<std::string> names{"rice.lua"};
        names.insert(names.end(), rice.pictures.begin(), rice.pictures.end());
        names.insert(names.end(), rice.extras.begin(), rice.extras.end());
        for (const auto &name : names) {
            auto found = files.find(name);
            if (found == files.end())
                throw std::runtime_error("the rice has no " + name + ", which rice.lua names");
            if (found->second.size() > max_rice_file)
                throw std::runtime_error(name + " in the rice is larger than 50 MB");
            auto path = staged / name;
            std::filesystem::create_directories(path.parent_path());
            int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
            if (fd < 0) {
                if (errno == EEXIST)
                    continue; // named twice
                throw std::runtime_error("cannot write " + path.string() + ": " +
                                         std::strerror(errno));
            }
            const auto &contents = found->second;
            size_t written = 0;
            while (written < contents.size()) {
                ssize_t count = write(fd, contents.data() + written, contents.size() - written);
                if (count <= 0)
                    break;
                written += static_cast<size_t>(count);
            }
            if (close(fd) != 0 || written != contents.size())
                throw std::runtime_error("cannot write " + path.string());
        }
        read_rice(staged);
    } catch (...) {
        std::filesystem::remove_all(staged, failure);
        throw;
    }
    return staged;
}

RiceData rice_profile(const Rice &rice) {
    auto profile = rice.profile;
    if (auto *shell = profile.field("shell"))
        for (const auto &key : picture_settings)
            if (auto *picture = shell->field(key); picture && !picture->string.empty())
                picture->string = (rice.directory / picture->string).string();
    return profile;
}

namespace {
// `data` written on one line: { "#33ccff", "#00ff99", angle = 45 }.
std::string one_line(const RiceData &data) {
    if (!data.is_table())
        return write_rice_data(data);
    if (data.items.empty() && data.fields.empty())
        return "{}";
    std::string line = "{ ";
    for (const auto &item : data.items)
        line += one_line(item) + ", ";
    for (const auto &[key, value] : data.fields) {
        RiceData single;
        single.fields.emplace_back(key, RiceData::boolean_of(true));
        // The key as write_rice_data writes it: bare, or in brackets.
        auto written = write_rice_data(single);
        auto start = written.find_first_not_of(" {\n");
        line += written.substr(start, written.find(" = ") - start) + " = " + one_line(value) + ", ";
    }
    line.resize(line.size() - 2);
    return line + " }";
}
// Each setting of `data` under `prefix`, as "shell.accent", with its value on one line; a
// table whose keys are not all names (a list, a gradient) is one value.
void flatten(const RiceData &data, const std::string &prefix,
             std::vector<std::pair<std::string, std::string>> &out) {
    if (data.is_table() && data.items.empty() && !data.fields.empty()) {
        for (const auto &[key, value] : data.fields)
            flatten(value, prefix.empty() ? key : prefix + "." + key, out);
        return;
    }
    out.emplace_back(prefix, one_line(data));
}
} // namespace

std::vector<RiceChange> rice_changes(const RiceData &a, const RiceData &b) {
    std::vector<std::pair<std::string, std::string>> before, after;
    flatten(a, "", before);
    flatten(b, "", after);
    std::vector<RiceChange> changes;
    auto find = [](const auto &list, const std::string &name) -> const std::string * {
        for (const auto &[key, value] : list)
            if (key == name)
                return &value;
        return nullptr;
    };
    for (const auto &[name, value] : after) {
        const auto *old = find(before, name);
        if (!old || *old != value)
            changes.push_back({name, old ? *old : "", value});
    }
    for (const auto &[name, value] : before)
        if (!find(after, name))
            changes.push_back({name, value, ""});
    return changes;
}

namespace {
RiceData look_of(const RiceData &data, std::vector<std::string> &setting,
                 std::vector<std::string> *left_out) {
    RiceData result;
    for (const auto &[key, value] : data.fields) {
        setting.push_back(key);
        auto name = joined(setting);
        if (look_settings.contains(name)) {
            result.fields.emplace_back(key, value);
        } else if (value.is_table() && value.items.empty() && holds_allowed(name)) {
            auto inner = look_of(value, setting, left_out);
            if (!inner.fields.empty())
                result.fields.emplace_back(key, std::move(inner));
        } else if (left_out) {
            left_out->push_back(name);
        }
        setting.pop_back();
    }
    return result;
}
} // namespace

RiceData rice_look(const RiceData &profile, std::vector<std::string> *left_out) {
    std::vector<std::string> setting;
    return look_of(profile, setting, left_out);
}

std::filesystem::path rices_directory() {
    auto state = state_paw();
    return state.empty() ? state : state / "rices";
}

std::vector<Rice> installed_rices(std::vector<std::string> *problems) {
    std::vector<Rice> rices;
    auto directory = rices_directory();
    std::error_code failure;
    if (directory.empty() || !std::filesystem::is_directory(directory, failure))
        return rices;
    for (const auto &entry : std::filesystem::directory_iterator(directory, failure)) {
        auto name = entry.path().filename().string();
        if (!valid_profile_name(name))
            continue; // set aside, being staged, or not a rice
        try {
            check_entry(entry.path(), true);
            rices.push_back(read_rice(entry.path()));
        } catch (const std::exception &error) {
            if (problems)
                problems->push_back(name + ": " + error.what());
        }
    }
    std::sort(rices.begin(), rices.end(), [](const Rice &a, const Rice &b) {
        return a.directory.filename() < b.directory.filename();
    });
    return rices;
}

std::string rice_install_name(const std::string &name, const std::vector<std::string> &taken) {
    auto free = [&](const std::string &candidate) {
        return std::find(taken.begin(), taken.end(), candidate) == taken.end();
    };
    if (free(name))
        return name;
    for (int number = 2;; ++number) {
        auto suffix = "-" + std::to_string(number);
        auto candidate = name.substr(0, 32 - suffix.size()) + suffix;
        if (free(candidate))
            return candidate;
    }
}

std::filesystem::path rice_trial_path() {
    auto state = state_paw();
    return state.empty() ? state : state / "rice-trial";
}

std::optional<RiceTrial> load_rice_trial() {
    auto path = rice_trial_path();
    std::ifstream file(path);
    if (path.empty() || !file)
        return std::nullopt;
    RiceTrial trial;
    std::string line;
    while (std::getline(file, line)) {
        auto tab = line.find('\t');
        auto key = line.substr(0, tab);
        auto value = tab == std::string::npos ? "" : line.substr(tab + 1);
        if (key == "rice")
            trial.rice = value;
        else if (key == "previous")
            trial.previous = value;
        else if (key == "replaced")
            trial.replaced = value == "1";
        else if (key == "pid")
            trial.pid = std::atol(value.c_str());
    }
    if (!valid_profile_name(trial.rice))
        return std::nullopt;
    return trial;
}

void save_rice_trial(const RiceTrial &trial) {
    auto path = rice_trial_path();
    if (path.empty())
        throw std::runtime_error("neither XDG_STATE_HOME nor HOME is set");
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".new";
    {
        std::ofstream file(temporary, std::ios::trunc);
        file << "rice\t" << trial.rice << '\n';
        if (trial.previous)
            file << "previous\t" << *trial.previous << '\n';
        file << "replaced\t" << (trial.replaced ? 1 : 0) << '\n';
        if (trial.pid)
            file << "pid\t" << trial.pid << '\n';
        if (!file.flush())
            throw std::runtime_error("cannot write " + temporary.string());
    }
    std::filesystem::rename(temporary, path);
}

std::filesystem::path rice_set_aside_path(const std::string &name) {
    return rices_directory() / ("." + name + ".previous");
}

void begin_rice_trial(const std::filesystem::path &staged, const std::string &name) {
    if (load_rice_trial())
        throw std::runtime_error("a rice is on trial already; keep it or revert it first");
    auto target = rices_directory() / name;
    RiceTrial trial;
    trial.rice = name;
    std::error_code failure;
    if (std::filesystem::exists(profile_state_path(), failure))
        trial.previous = saved_profile();
    trial.replaced = std::filesystem::exists(target, failure);
    // Recorded first: whatever happens after, the next start finds the way back.
    save_rice_trial(trial);
    try {
        if (trial.replaced)
            std::filesystem::rename(target, rice_set_aside_path(name));
        std::filesystem::rename(staged, target);
        save_profile(name);
    } catch (...) {
        revert_rice_trial();
        throw;
    }
}

std::optional<RiceTrial> keep_rice_trial() {
    auto trial = load_rice_trial();
    if (!trial)
        return trial;
    std::error_code failure;
    std::filesystem::remove_all(rice_set_aside_path(trial->rice), failure);
    std::filesystem::remove(rice_trial_path(), failure);
    return trial;
}

std::optional<RiceTrial> revert_rice_trial() {
    auto trial = load_rice_trial();
    if (!trial)
        return trial;
    std::error_code failure;
    if (trial->previous)
        save_profile(*trial->previous);
    else
        std::filesystem::remove(profile_state_path(), failure);
    auto target = rices_directory() / trial->rice;
    auto aside = rice_set_aside_path(trial->rice);
    if (!trial->replaced || std::filesystem::exists(aside, failure)) {
        std::filesystem::remove_all(target, failure);
        if (trial->replaced)
            std::filesystem::rename(aside, target, failure);
    }
    std::filesystem::remove(rice_trial_path(), failure);
    return trial;
}

std::optional<RiceTrial> recover_rice_trial() {
    auto trial = load_rice_trial();
    if (!trial)
        return trial;
    // Another compositor, a nested one, leaves the trial of the session it runs in alone.
    if (trial->pid > 0 && trial->pid != getpid() && kill(static_cast<pid_t>(trial->pid), 0) == 0) {
        std::ifstream comm("/proc/" + std::to_string(trial->pid) + "/comm");
        std::string name;
        if (std::getline(comm, name) && name == "paw")
            return std::nullopt;
    }
    return revert_rice_trial();
}

} // namespace paw
