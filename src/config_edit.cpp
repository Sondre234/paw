// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/config_edit.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace paw {
namespace {
enum class Kind { Name, String, Number, Punct, Comment };
struct Token {
    Kind kind;
    size_t begin, end; // byte offsets in the source
    std::string value; // a string's contents (short strings without escapes, long strings)
};

bool is_name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool is_name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The level of a long bracket opening at `at` (`[[` is 0, `[==[` is 2); -1 when there is none.
int long_bracket(const std::string &source, size_t at) {
    if (at >= source.size() || source[at] != '[')
        return -1;
    size_t i = at + 1;
    while (i < source.size() && source[i] == '=')
        ++i;
    return i < source.size() && source[i] == '[' ? static_cast<int>(i - at - 1) : -1;
}
// Where the long bracket of `level` opening at `at` is closed by its `]==]`; npos when it never
// is.
size_t long_bracket_close(const std::string &source, size_t at, int level) {
    return source.find("]" + std::string(static_cast<size_t>(level), '=') + "]",
                       at + static_cast<size_t>(level) + 2);
}

// The tokens of a Lua chunk, comments among them, as Lua 5.4 reads them. What Lua would refuse
// (a string never closed) ends a token where it can, as the edit is checked by loading it anyway.
std::vector<Token> tokenize(const std::string &source) {
    std::vector<Token> tokens;
    size_t i = 0;
    const size_t n = source.size();
    // A first line starting with # is skipped, as lua does.
    if (source.starts_with("#"))
        while (i < n && source[i] != '\n')
            ++i;
    while (i < n) {
        char c = source[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        size_t begin = i;
        if (c == '-' && i + 1 < n && source[i + 1] == '-') {
            int level = long_bracket(source, i + 2);
            if (level >= 0) {
                auto close = long_bracket_close(source, i + 2, level);
                i = close == std::string::npos ? n : close + static_cast<size_t>(level) + 2;
            } else {
                while (i < n && source[i] != '\n')
                    ++i;
            }
            tokens.push_back({Kind::Comment, begin, i, {}});
        } else if (is_name_start(c)) {
            while (i < n && is_name_char(source[i]))
                ++i;
            tokens.push_back({Kind::Name, begin, i, source.substr(begin, i - begin)});
        } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                   (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
            const bool hex = c == '0' && i + 1 < n && (source[i + 1] == 'x' || source[i + 1] == 'X');
            if (hex)
                i += 2;
            while (i < n) {
                char d = source[i];
                if ((hex && (d == 'p' || d == 'P')) || (!hex && (d == 'e' || d == 'E'))) {
                    ++i;
                    if (i < n && (source[i] == '+' || source[i] == '-'))
                        ++i;
                } else if (std::isxdigit(static_cast<unsigned char>(d)) || d == '.' || is_name_char(d)) {
                    ++i;
                } else {
                    break;
                }
            }
            tokens.push_back({Kind::Number, begin, i, {}});
        } else if (c == '"' || c == '\'') {
            bool plain = true;
            for (++i; i < n && source[i] != c && source[i] != '\n'; ++i)
                if (source[i] == '\\') {
                    plain = false;
                    ++i;
                }
            if (i < n && source[i] == c)
                ++i;
            tokens.push_back({Kind::String, begin, i,
                              plain && i - begin >= 2 ? source.substr(begin + 1, i - begin - 2) : ""});
        } else if (int level = long_bracket(source, i); level >= 0) {
            size_t open = begin + static_cast<size_t>(level) + 2;
            size_t close = long_bracket_close(source, i, level);
            if (close == std::string::npos)
                close = n;
            i = std::min(n, close + static_cast<size_t>(level) + 2);
            // A newline straight after the opening bracket is not part of the string.
            if (open < close && source[open] == '\r')
                ++open;
            if (open < close && source[open] == '\n')
                ++open;
            tokens.push_back({Kind::String, begin, i, source.substr(open, close - open)});
        } else {
            static const char *const symbols[] = {"...", "..", "==", "~=", "<=", ">=",
                                                  "//",  "::", "<<", ">>"};
            size_t length = 1;
            for (const char *symbol : symbols)
                if (source.compare(i, std::strlen(symbol), symbol) == 0) {
                    length = std::strlen(symbol);
                    break;
                }
            i += length;
            tokens.push_back({Kind::Punct, begin, i, source.substr(begin, length)});
        }
    }
    return tokens;
}

bool is(const Token &token, Kind kind, std::string_view text) {
    return token.kind == kind && token.value == text;
}
bool punct(const Token &token, std::string_view text) { return is(token, Kind::Punct, text); }

// One way to set the setting: the source with `text` in place of [begin, end).
struct Edit {
    size_t depth; // how many tables deep it is
    size_t begin, end;
    std::string text;
};

// A table constructor: where its `{` is, and the names it is given to, outermost first (the
// tables around it and its own: "", "profiles", "washi", "shell"), "" for one given to none.
struct Table {
    size_t brace; // index of the `{` among the code's tokens
    std::vector<std::string> path;
};

class Editor {
  public:
    Editor(const std::string &source, const std::vector<std::string> &trail,
           const std::string &literal)
        : source_(source), trail_(trail), literal_(literal) {
        for (auto &token : tokenize(source))
            if (token.kind != Kind::Comment)
                code_.push_back(std::move(token));
            else
                comments_.push_back(token);
    }

    // Every edit, in the order they are tried.
    std::vector<Edit> edits() {
        std::vector<Edit> edits;
        const std::string &leaf = trail_.back();
        const std::vector<std::string> sections(trail_.begin(), trail_.end() - 1);
        std::vector<Table> open;
        std::vector<Table> tables;
        std::optional<size_t> last_return;
        for (size_t i = 0; i < code_.size(); ++i) {
            const Token &token = code_[i];
            if (punct(token, "{")) {
                auto path = open.empty() ? std::vector<std::string>{} : open.back().path;
                path.push_back(key_before(i));
                open.push_back({i, path});
                tables.push_back(open.back());
                if (open.size() == 1 && i > 0 && is(code_[i - 1], Kind::Name, "return"))
                    last_return = i;
                continue;
            }
            if (punct(token, "}")) {
                if (!open.empty())
                    open.pop_back();
                continue;
            }
            // `leaf =` as a field, `["leaf"] =`, or `a.b.leaf =` as a statement.
            std::vector<std::string> context;
            size_t equals = i + 1;
            if (token.kind == Kind::Name && token.value == leaf && equals < code_.size() &&
                punct(code_[equals], "=")) {
                if (i > 0 && punct(code_[i - 1], ".")) {
                    context = open.empty() ? std::vector<std::string>{} : open.back().path;
                    auto chain = chain_before(i);
                    context.insert(context.end(), chain.begin(), chain.end());
                } else if (field_start(i)) {
                    if (open.empty())
                        continue;
                    context = open.back().path;
                } else {
                    continue;
                }
            } else if (token.kind == Kind::String && token.value == leaf && i > 0 &&
                       punct(code_[i - 1], "[") && field_start(i - 1) && i + 2 < code_.size() &&
                       punct(code_[i + 1], "]") && punct(code_[i + 2], "=") && !open.empty()) {
                equals = i + 2;
                context = open.back().path;
            } else {
                continue;
            }
            if (!ends_with(context, sections))
                continue;
            if (auto value = single_value(equals + 1))
                edits.push_back({open.size(), code_[*value].begin, code_[*value].end, literal_});
        }
        shallowest_first(edits, 0);
        // New fields: in a table given to all the sections, else to fewer of them with the rest
        // as tables around the setting, and last in the table the file returns.
        for (size_t held = sections.size(); held > 0; --held) {
            std::vector<std::string> outer(sections.begin(), sections.begin() + static_cast<long>(held));
            size_t from = edits.size();
            for (const auto &table : tables)
                if (ends_with(table.path, outer))
                    edits.push_back(insertion(table.brace, table.path.size(), nested(held)));
            shallowest_first(edits, from);
        }
        if (last_return)
            edits.push_back(insertion(*last_return, 1, nested(0)));
        return edits;
    }

  private:
    const std::string &source_;
    const std::vector<std::string> &trail_;
    const std::string &literal_;
    std::vector<Token> code_;
    std::vector<Token> comments_;

    static void shallowest_first(std::vector<Edit> &edits, size_t from) {
        std::stable_sort(edits.begin() + static_cast<long>(from), edits.end(),
                         [](const Edit &a, const Edit &b) { return a.depth < b.depth; });
    }
    static bool ends_with(const std::vector<std::string> &path, const std::vector<std::string> &tail) {
        return path.size() >= tail.size() &&
               std::equal(tail.begin(), tail.end(), path.end() - static_cast<long>(tail.size()));
    }
    // Whether the code token at `i` begins a field of a table constructor.
    bool field_start(size_t i) const {
        return i > 0 && (punct(code_[i - 1], "{") || punct(code_[i - 1], ",") || punct(code_[i - 1], ";"));
    }
    // The name a table opening at code token `brace` is given to: `name = {`, `a.name = {` or
    // `["name"] = {`; "" for none.
    std::string key_before(size_t brace) const {
        if (brace < 2 || !punct(code_[brace - 1], "="))
            return "";
        const Token &key = code_[brace - 2];
        if (key.kind == Kind::Name)
            return key.value;
        if (punct(key, "]") && brace >= 4 && code_[brace - 3].kind == Kind::String &&
            punct(code_[brace - 4], "["))
            return code_[brace - 3].value;
        return "";
    }
    // The names before `.leaf` in `a.b.leaf`, outermost first.
    std::vector<std::string> chain_before(size_t leaf) const {
        std::vector<std::string> chain;
        for (size_t i = leaf; i >= 2 && punct(code_[i - 1], ".") && code_[i - 2].kind == Kind::Name;
             i -= 2)
            chain.insert(chain.begin(), code_[i - 2].value);
        return chain;
    }
    // The value starting at code token `at` when it is that one token alone (a string, number,
    // boolean, nil or name, nothing applied to it or joined with it); nullopt otherwise.
    std::optional<size_t> single_value(size_t at) const {
        if (at >= code_.size())
            return std::nullopt;
        const Token &value = code_[at];
        if (value.kind != Kind::String && value.kind != Kind::Number && value.kind != Kind::Name)
            return std::nullopt;
        if (value.kind == Kind::Name &&
            (value.value == "function" || value.value == "not" || value.value == "and" ||
             value.value == "or"))
            return std::nullopt;
        if (at + 1 == code_.size())
            return at;
        const Token &next = code_[at + 1];
        if (punct(next, ",") || punct(next, ";") || punct(next, "}"))
            return at;
        // A statement ends where the next begins, with a name or a keyword.
        if (next.kind == Kind::Name && next.value != "and" && next.value != "or")
            return at;
        return std::nullopt;
    }
    // The setting as a field, inside the sections from `held` on: `panel_position = "left"`, or
    // `shell = { panel_position = "left" }` for held 0.
    std::string nested(size_t held) const {
        std::string text = trail_.back() + " = " + literal_;
        for (size_t i = trail_.size() - 1; i-- > held;)
            text = trail_[i] + " = { " + text + " }";
        return text;
    }
    size_t line_start(size_t at) const {
        auto newline = source_.rfind('\n', at == 0 ? 0 : at - 1);
        return at == 0 || newline == std::string::npos ? 0 : newline + 1;
    }
    std::string indent_at(size_t at) const {
        size_t start = line_start(at), end = start;
        while (end < source_.size() && (source_[end] == ' ' || source_[end] == '\t'))
            ++end;
        return source_.substr(start, end - start);
    }
    bool in_comment(size_t at) const {
        return std::any_of(comments_.begin(), comments_.end(), [&](const Token &comment) {
            return comment.begin <= at && at < comment.end;
        });
    }
    // `field` as the first field of the table opening at code token `brace`: on a line of its
    // own after the brace's (and a comment ending it), indented as the field below it, when the
    // table spans lines; beside the brace when it is on one line.
    Edit insertion(size_t brace, size_t depth, const std::string &field) const {
        const size_t after = code_[brace].end;
        const size_t next = brace + 1 < code_.size() ? code_[brace + 1].begin : source_.size();
        const bool closes = brace + 1 < code_.size() && punct(code_[brace + 1], "}");
        for (size_t at = after; at < next; ++at) {
            if (source_[at] != '\n' || in_comment(at))
                continue;
            std::string indent;
            if (!closes && next < source_.size()) {
                indent = indent_at(next);
            } else {
                indent = indent_at(code_[brace].begin);
                indent += indent.find('\t') != std::string::npos ? "\t" : "    ";
            }
            const std::string eol = at > 0 && source_[at - 1] == '\r' ? "\r\n" : "\n";
            return {depth, at + 1, at + 1, indent + field + "," + eol};
        }
        if (closes)
            return {depth, next, next, (next == after ? " " : "") + field + " "};
        return {depth, next, next, field + ", "};
    }
};
} // namespace

std::string edit_setting(const std::string &source, const std::vector<std::string> &trail,
                         const std::string &literal,
                         const std::function<bool(const std::string &)> &takes) {
    if (trail.empty())
        throw std::runtime_error("no setting to set");
    // Already in force, the file stays as it is: any edit would take, a profile's not in use too.
    if (takes(source))
        return source;
    Editor editor(source, trail, literal);
    for (const auto &edit : editor.edits()) {
        auto edited = source.substr(0, edit.begin) + edit.text + source.substr(edit.end);
        if (takes(edited))
            return edited;
    }
    std::string name;
    for (const auto &part : trail)
        name += (name.empty() ? "" : ".") + part;
    throw std::runtime_error("cannot find where to set " + name + " in the configuration");
}

namespace {
std::string read_text(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot read " + path.string());
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad())
        throw std::runtime_error("cannot read " + path.string());
    return text;
}
// Replaces `target` with `text` at once: written to a file beside it with its permissions, and
// renamed over it.
void replace_file(const std::filesystem::path &target, const std::string &text) {
    struct stat original {};
    if (stat(target.c_str(), &original) != 0)
        throw std::runtime_error("cannot read " + target.string() + ": " + std::strerror(errno));
    auto pattern = (target.parent_path() / ("." + target.filename().string() + ".XXXXXX")).string();
    int fd = mkostemp(pattern.data(), O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("cannot write beside " + target.string() + ": " +
                                 std::strerror(errno));
    auto fail = [&](const std::string &what) {
        int error = errno;
        close(fd);
        unlink(pattern.c_str());
        throw std::runtime_error("cannot " + what + " " + target.string() + ": " +
                                 std::strerror(error));
    };
    for (size_t written = 0; written < text.size();) {
        ssize_t count = write(fd, text.data() + written, text.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            fail("write");
        written += static_cast<size_t>(count);
    }
    if (fchmod(fd, original.st_mode & 07777) != 0)
        fail("keep the permissions of");
    if (fsync(fd) != 0)
        fail("write");
    if (close(fd) != 0) {
        int error = errno;
        unlink(pattern.c_str());
        throw std::runtime_error("cannot write " + target.string() + ": " + std::strerror(error));
    }
    if (rename(pattern.c_str(), target.c_str()) != 0) {
        int error = errno;
        unlink(pattern.c_str());
        throw std::runtime_error("cannot write " + target.string() + ": " + std::strerror(error));
    }
}
} // namespace

void save_setting(const std::filesystem::path &path, const std::vector<std::string> &trail,
                  const std::string &literal, const std::function<bool(const Config &)> &takes) {
    std::error_code failure;
    auto target = std::filesystem::is_symlink(path, failure) ? std::filesystem::canonical(path, failure)
                                                             : path;
    if (failure)
        throw std::runtime_error("cannot follow " + path.string() + ": " + failure.message());
    const auto source = read_text(target);
    const auto name = "@" + path.string();
    const auto profile = saved_profile();
    const auto rices = installed_rices();
    try {
        (void)parse_config(source, name, path.parent_path(), profile, rices);
    } catch (const std::exception &error) {
        throw std::runtime_error("cannot change the configuration while it has an error: " +
                                 std::string(error.what()));
    }
    auto edited = edit_setting(source, trail, literal, [&](const std::string &text) {
        try {
            return takes(parse_config(text, name, path.parent_path(), profile, rices));
        } catch (const std::exception &) {
            return false;
        }
    });
    // Unchanged, it is not written, which would reload the configuration for nothing.
    if (edited != source)
        replace_file(target, edited);
}
} // namespace paw
