// SPDX-License-Identifier: GPL-3.0-or-later
/* A rice as one file to share (`paw rice pack`, `paw rice try`): a tar archive, gzip-compressed
 * or not, read and written here so that what goes in and comes out is checked, entry by entry.
 * Only regular files and directories are taken from an archive, inside it, and gzip, when the
 * archive is compressed, unpacks it into memory, never onto the disk. */
#include "paw/cli.hpp"
#include "paw/rice.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <set>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace paw::cli {
namespace {
constexpr size_t block = 512;
constexpr size_t max_entries = 1000;

std::string octal(unsigned long long value, size_t width) {
    std::string digits(width - 1, '0');
    for (size_t i = width - 1; i-- > 0 && value;) {
        digits[i] = static_cast<char>('0' + (value & 7));
        value >>= 3;
    }
    return digits;
}
void put(std::string &header, size_t at, const std::string &text) {
    std::copy(text.begin(), text.end(), header.begin() + static_cast<long>(at));
}
// A ustar header for `name`, a file of `size` bytes or a directory.
std::string header(const std::string &name, size_t size, bool directory) {
    std::string header(block, '\0');
    // A name longer than 100 bytes is split at a '/' into the prefix and the name.
    std::string prefix, rest = name;
    if (rest.size() > 100) {
        auto slash = rest.find('/');
        while (slash != std::string::npos && rest.size() - slash - 1 > 100)
            slash = rest.find('/', slash + 1);
        if (slash == std::string::npos || slash > 155)
            throw std::runtime_error(name + " is too long a name for an archive");
        prefix = rest.substr(0, slash);
        rest = rest.substr(slash + 1);
    }
    put(header, 0, rest);
    put(header, 100, octal(directory ? 0755 : 0644, 8));
    put(header, 108, octal(0, 8));
    put(header, 116, octal(0, 8));
    put(header, 124, octal(size, 12));
    put(header, 136, octal(0, 12));
    header[156] = directory ? '5' : '0';
    put(header, 257, std::string("ustar\0" "00", 8));
    put(header, 345, prefix);
    put(header, 148, std::string(8, ' '));
    unsigned sum = 0;
    for (char c : header)
        sum += static_cast<unsigned char>(c);
    put(header, 148, octal(sum, 7));
    header[155] = ' ';
    return header;
}
// A NUL-terminated field of a header.
std::string field(const char *data, size_t length) {
    return std::string(data, strnlen(data, length));
}
unsigned long long read_octal(const char *data, size_t length) {
    if (static_cast<unsigned char>(data[0]) & 0x80)
        throw std::runtime_error("the archive holds a file too large for a rice");
    unsigned long long value = 0;
    size_t i = 0;
    while (i < length && data[i] == ' ')
        ++i;
    for (; i < length && data[i] >= '0' && data[i] <= '7'; ++i)
        value = value * 8 + static_cast<unsigned long long>(data[i] - '0');
    return value;
}
// An entry's path, without "./" before it or '/' after a directory's; throws for one that
// leaves the archive's directory.
std::string clean(const std::string &name) {
    if (name.starts_with('/'))
        throw std::runtime_error("the archive holds an absolute path, " + name +
                                 ", which a rice cannot");
    std::vector<std::string> parts;
    for (size_t start = 0; start <= name.size();) {
        auto end = std::min(name.find('/', start), name.size());
        auto part = name.substr(start, end - start);
        if (part == "..")
            throw std::runtime_error("the archive holds a path out of it, " + name +
                                     ", which a rice cannot");
        if (!part.empty() && part != ".")
            parts.push_back(part);
        start = end + 1;
    }
    std::string path;
    for (const auto &part : parts)
        path += (path.empty() ? "" : "/") + part;
    return path;
}
// The value of `key` in a pax extended header's records ("LENGTH key=value\n" each).
std::optional<std::string> pax_value(const std::string &records, const std::string &key) {
    for (size_t at = 0; at < records.size();) {
        auto space = records.find(' ', at);
        if (space == std::string::npos)
            break;
        size_t length = std::strtoul(records.c_str() + at, nullptr, 10);
        if (length == 0 || at + length > records.size())
            break;
        auto record = records.substr(space + 1, at + length - space - 2);
        if (record.starts_with(key + "="))
            return record.substr(key.size() + 1);
        at += length;
    }
    return std::nullopt;
}
} // namespace

std::string rice_archive(const std::string &name,
                         const std::map<std::string, std::string> &files) {
    std::string archive;
    std::set<std::string> directories;
    auto directory = [&](const std::string &path) {
        if (directories.insert(path).second)
            archive += header(path + "/", 0, true);
    };
    directory(name);
    for (const auto &[path, contents] : files) {
        if (!rice_path_valid(path) && path != "rice.lua")
            throw std::runtime_error("a rice cannot hold " + path);
        for (auto slash = path.find('/'); slash != std::string::npos;
             slash = path.find('/', slash + 1))
            directory(name + "/" + path.substr(0, slash));
        archive += header(name + "/" + path, contents.size(), false);
        archive += contents;
        archive.append((block - contents.size() % block) % block, '\0');
    }
    archive.append(2 * block, '\0');
    return archive;
}

std::map<std::string, std::string> rice_archive_files(const std::string &archive) {
    if (archive.size() > max_rice_archive)
        throw std::runtime_error("the archive is larger than 64 MiB");
    std::map<std::string, std::string> entries;
    std::optional<std::string> long_name;
    size_t count = 0;
    for (size_t at = 0;;) {
        if (at + block > archive.size())
            throw std::runtime_error("the archive ends before its last entry");
        const char *h = archive.data() + at;
        if (std::all_of(h, h + block, [](char c) { return c == '\0'; }))
            break;
        unsigned sum = 0;
        int signed_sum = 0;
        for (size_t i = 0; i < block; ++i) {
            char c = i >= 148 && i < 156 ? ' ' : h[i];
            sum += static_cast<unsigned char>(c);
            signed_sum += static_cast<signed char>(c);
        }
        auto recorded = read_octal(h + 148, 8);
        if (recorded != sum && static_cast<long long>(recorded) != signed_sum)
            throw std::runtime_error(at == 0 ? "the file is not a tar archive"
                                             : "the archive is damaged");
        if (++count > max_entries)
            throw std::runtime_error("the archive holds too many entries");
        auto size = read_octal(h + 124, 12);
        at += block;
        if (size > archive.size() - at)
            throw std::runtime_error("the archive ends inside an entry");
        std::string data = archive.substr(at, size);
        at += (size + block - 1) / block * block;
        std::string name = field(h, 100);
        if (field(h + 257, 6) == "ustar" && h[345])
            name = field(h + 345, 155) + "/" + name;
        char type = h[156];
        if (type == 'x') { // pax: the next entry's path, perhaps
            if (auto path = pax_value(data, "path"))
                long_name = *path;
            continue;
        }
        if (type == 'g')
            continue;
        if (type == 'L') { // GNU: the next entry's long name
            long_name = field(data.data(), data.size());
            continue;
        }
        if (long_name) {
            name = *long_name;
            long_name.reset();
        }
        auto path = clean(name);
        if (type == '5')
            continue;
        if (type != '0' && type != '\0' && type != '7')
            throw std::runtime_error(
                path + std::string(type == '1' || type == '2' || type == 'K'
                                       ? " is a link, which a rice cannot hold"
                                       : " is not a plain file, which a rice cannot hold"));
        if (path.empty())
            throw std::runtime_error("the archive holds a file without a name");
        if (!entries.emplace(path, std::move(data)).second)
            throw std::runtime_error("the archive holds " + path + " twice");
    }
    // rice.lua at the top, or in the one directory at the top.
    std::string top;
    if (!entries.contains("rice.lua")) {
        std::set<std::string> tops;
        for (const auto &[path, data] : entries)
            if (path.ends_with("/rice.lua") && path.find('/') == path.size() - 9)
                tops.insert(path.substr(0, path.size() - 9));
        if (tops.size() != 1)
            throw std::runtime_error(tops.empty() ? "the archive holds no rice.lua"
                                                  : "the archive holds more than one rice");
        top = *tops.begin() + "/";
    }
    std::map<std::string, std::string> files;
    for (auto &[path, data] : entries)
        if (path.starts_with(top))
            files.emplace(path.substr(top.size()), std::move(data));
    return files;
}

std::string run_filter(const std::vector<std::string> &command, const std::filesystem::path &input,
                       size_t most) {
    int in = open(input.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0)
        throw std::runtime_error("cannot read " + input.string() + ": " + std::strerror(errno));
    int pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
        close(in);
        throw std::runtime_error("cannot run " + command.front());
    }
    std::vector<char *> argv;
    for (const auto &word : command)
        argv.push_back(const_cast<char *>(word.c_str()));
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in, STDIN_FILENO);
        dup2(pipe_fds[1], STDOUT_FILENO);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(in);
    close(pipe_fds[1]);
    std::string output;
    bool too_much = false;
    char buffer[65536];
    for (ssize_t count; pid > 0 && (count = read(pipe_fds[0], buffer, sizeof(buffer))) != 0;) {
        if (count < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        output.append(buffer, static_cast<size_t>(count));
        if (output.size() > most) {
            too_much = true;
            kill(pid, SIGKILL);
            break;
        }
    }
    close(pipe_fds[0]);
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0)
        throw std::runtime_error("cannot run " + command.front());
    if (too_much)
        throw std::runtime_error(command.front() + " gave more than " +
                                 std::to_string(most >> 20) + " MiB");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(command.front() +
                                 (WIFEXITED(status) && WEXITSTATUS(status) == 127
                                      ? " is not installed"
                                      : " failed"));
    return output;
}

} // namespace paw::cli
