// SPDX-License-Identifier: GPL-3.0-or-later
// Hostile and damaged configuration: the parser must accept or reject it with an exception,
// never crash, hang, or read out of bounds (run this under AddressSanitizer for the last two).
#include "paw/config.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "config_check.hpp"

// The mutation fuzz's time limit, which catches parsing that turns slow (quadratic or worse). The
// sanitizers' build unwinds the stack on every allocation for its leak reports and runs dozens of
// times slower, so it gets room for that; the normal build checks the speed.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PAW_ASAN 1
#endif
#endif
// parse_limit: the seconds a configuration has to load in, src/config.cpp's time_limit.
#if defined(__SANITIZE_ADDRESS__) || defined(PAW_ASAN)
constexpr auto parse_budget = std::chrono::seconds(600);
constexpr double parse_limit = 30;
#else
constexpr auto parse_budget = std::chrono::seconds(60);
constexpr double parse_limit = 3;
#endif

namespace {
// Returns true when accepted; anything but a std::exception failing escapes and fails the test.
bool parses(const std::string &source) {
    try {
        (void)paw::parse_config(source);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
// Fails the test unless `source` is refused within `limit` seconds with `fragment` in the error.
void refused_in_time(const std::string &source, const std::string &fragment, double limit,
                     const std::string &what) {
    const auto start = std::chrono::steady_clock::now();
    std::string error;
    try {
        (void)paw::parse_config(source, "@init.lua");
    } catch (const std::exception &failure) {
        error = failure.what();
    }
    const double took = seconds_since(start);
    require(!error.empty(), what + " was accepted");
    require(error.find(fragment) != std::string::npos,
            what + ": expected '" + fragment + "' in: " + error.substr(0, 300));
    require(took < limit, what + " took " + std::to_string(took) + " s to refuse");
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "example config path required");
        std::ifstream file(argv[1], std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string example = buffer.str();
        require(!example.empty() && parses(example), "example config must parse");

        // Escape attempts and runaway scripts are rejected, not run.
        require(!parses("return os.execute('true')"), "os library reachable");
        require(!parses("return io.open('/etc/passwd')"), "io library reachable");
        require(!parses("dofile('/etc/passwd')"), "dofile reachable");
        require(!parses("return load('return 1')()"), "load reachable");
        require(!parses("while true do end"), "endless loop was not stopped");
        require(!parses("while true do pcall(function() while true do end end) end"),
                "an endless loop caught its own budget's error and ran on");
        require(!parses("local t = {} t[1] = t return setmetatable(t, {__index = function(t) "
                        "return t[1] end}).x"), "runaway recursion was not stopped");
        require(!parses("return string.rep('x', 2^40)"), "huge allocation was not refused");

        // One call into a C function is one instruction however long it runs or whatever it
        // allocates, so the instruction budget stops neither. What runs for more than 3 seconds
        // (30 with the sanitizers) is stopped, the error saying so.
        refused_in_time("local s = ('a'):rep(40) s:find(('a*'):rep(30) .. 'b') return {}",
                        "did not finish loading within", 3 * parse_limit,
                        "a pattern that backtracks");
        refused_in_time("local s = (''):rep(2^60) return {}", "did not finish loading within",
                        3 * parse_limit, "an empty string repeated 2^60 times");
        refused_in_time("table.move({}, 1, 2^50, 2) return {}", "did not finish loading within",
                        3 * parse_limit, "a table move over 2^50 places");
        // What asks for more than 64 MiB of memory is refused at once.
        refused_in_time("local big = ('x'):rep(2^34) return {}", "string too large", parse_limit,
                        "a 16 GiB string"); // over Lua's own limit for a string, 2 GiB
        refused_in_time("local big = ('x'):rep(2^31 - 1) return {}",
                        "init.lua: needs more than 64 MiB of memory", parse_limit,
                        "a 2 GiB string");
        refused_in_time("local t = {} for i = 1, 1e5 do t[i] = ('x'):rep(1e4) .. i end return {}",
                        "init.lua: needs more than 64 MiB of memory", parse_limit,
                        "a table of a hundred thousand strings of 10 KB");
        // Caught by the configuration, the refusal leaves it running within the limit.
        require(parses("local ok = pcall(string.rep, 'x', 2^31 - 1) assert(not ok) return {}"),
                "a configuration that caught a refused allocation was refused");

        // An unknown key a megabyte long is refused at once: no name is that long, so none is
        // suggested for it, and the error quotes only its start.
        {
            const auto start = std::chrono::steady_clock::now();
            auto error = error_of("return {['" + std::string(1 << 20, 'k') + "'] = 1}");
            require(seconds_since(start) < parse_limit / 3,
                    "a megabyte-long unknown key took " + std::to_string(seconds_since(start)) +
                        " s to refuse");
            require(error.find("unknown setting 'kkkk") != std::string::npos && error.size() < 2000,
                    "a megabyte-long unknown key: " + error.substr(0, 300));
        }

        // Wrong shapes and extreme values.
        for (const char *source : {
                 "", "return", "return 1", "return {}", "return {version=1e308}",
                 "return {version=0/0}", "return {version=-0}", "return {version='1'}",
                 "return {layout={gap=1e308}}", "return {layout={gap=0/0}}",
                 "return {layout={gap=math.huge}}", "return {layout={gap=-math.huge}}",
                 "return {layout={gap=2^63}}", "return {layout={gap=-2^63}}",
                 "return {bindings=42}", "return {bindings={{}}}", "return {bindings={{1,2,3}}}",
                 "return {shell={launchers={{}}}}", "return {shell={launchers={{command={}}}}}",
                 "return {shell={panel_height=2^31}}", "return {shell={panel_margin={1,2,3,4,5}}}",
                 "return {shell={wallpaper=('x'):rep(100000)}}",
                 "return {shell={background='#'}}", "return {shell={background='#GGGGGG'}}",
                 "return {shell={background='#12345'}}", "return {\255\254=1}", "return {\"\\0\"=1}",
                 "return setmetatable({}, {__index=function() error('boom') end})",
                 "return setmetatable({}, {__pairs=function() error('boom') end})",
                 "error(setmetatable({}, {__tostring=function() "
                 "error('again') end}))"}) {
            (void)parses(source);
        }
        std::string nested = "return ";
        for (int i = 0; i < 100000; ++i)
            nested += "{";
        (void)parses(nested);
        (void)parses(std::string(1 << 20, '('));
        (void)parses(std::string("return {") + std::string(1 << 20, ' ') + "}");

        // A theme laid over records nested as deep as the configuration's: to the overlay's
        // depth limit, and tables holding themselves. Lua grows its stack for C code only when
        // asked, so each level must ask, or the overlay writes past the stack into the heap.
        {
            auto directory = std::filesystem::temp_directory_path() /
                             ("paw-config-robust-" + std::to_string(getpid()));
            std::filesystem::create_directories(directory);
            auto theme = [&](const std::string &text) {
                std::ofstream(directory / "theme.lua") << text;
            };
            auto themed = [&](const std::string &source) {
                try {
                    (void)paw::parse_config(source, "config", directory);
                    return true;
                } catch (const std::exception &) {
                    return false;
                }
            };
            theme("local b = {} b.a = b return { appearance = b }");
            require(!themed("local a = {} a.a = a return { theme = 'theme.lua', appearance = a }"),
                    "a theme over tables holding themselves was accepted");
            std::string deep = "{}";
            for (int i = 0; i < 16; ++i)
                deep = "{ a = " + deep + " }";
            theme("return { appearance = " + deep + " }");
            (void)themed("return { theme = 'theme.lua', appearance = " + deep + " }");
            std::filesystem::remove_all(directory);
        }

        // Deterministic mutation fuzz of the real example: flip, drop, duplicate, and truncate.
        uint64_t state = 0x9e3779b97f4a7c15ull;
        auto next = [&state] {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        };
        const auto start = std::chrono::steady_clock::now();
        int accepted = 0, rejected = 0;
        for (int round = 0; round < 400; ++round) {
            std::string mutant = example;
            for (int edit = 0, edits = 1 + int(next() % 4); edit < edits; ++edit) {
                size_t at = next() % mutant.size();
                switch (next() % 4) {
                case 0: mutant[at] = char(next()); break;
                case 1: mutant.erase(at, 1 + next() % 8); break;
                case 2: mutant.insert(at, mutant.substr(next() % mutant.size(), next() % 24)); break;
                default: mutant.resize(at); break;
                }
                if (mutant.empty())
                    mutant = "x";
            }
            (parses(mutant) ? accepted : rejected)++;
        }
        require(rejected > 0, "mutations should break some configurations");
        require(std::chrono::steady_clock::now() - start < parse_budget,
                "mutated configurations parsed too slowly");
        std::cout << "Configuration robustness passed (" << accepted << " mutants accepted, "
                  << rejected << " rejected)\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
