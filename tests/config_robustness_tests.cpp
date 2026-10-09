// SPDX-License-Identifier: GPL-3.0-or-later
// Hostile and damaged configuration: the parser must accept or reject it with an exception,
// never crash, hang, or read out of bounds (run this under AddressSanitizer for the last two).
#include "paw/config.hpp"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// The mutation fuzz's time limit, which catches parsing that turns slow (quadratic or worse). The
// sanitizers' build unwinds the stack on every allocation for its leak reports and runs dozens of
// times slower, so it gets room for that; the normal build checks the speed.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PAW_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(PAW_ASAN)
constexpr auto parse_budget = std::chrono::seconds(600);
#else
constexpr auto parse_budget = std::chrono::seconds(60);
#endif

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
// Returns true when accepted; anything but a std::exception failing escapes and fails the test.
bool parses(const std::string &source) {
    try {
        (void)paw::parse_config(source);
        return true;
    } catch (const std::exception &) {
        return false;
    }
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
        require(!parses("local t = {} t[1] = t return setmetatable(t, {__index = function(t) "
                        "return t[1] end}).x"), "runaway recursion was not stopped");
        require(!parses("return string.rep('x', 2^40)"), "huge allocation was not refused");

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
