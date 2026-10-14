// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw shake`, `paw gravity` and `paw party`: the request each sends, and what they refuse. */
#include "check.h"
#include "paw/cli.hpp"

#include <stdexcept>
#include <string>

static std::string request(std::initializer_list<const char *> words) {
    std::vector<char *> argv;
    for (const char *word : words)
        argv.push_back(const_cast<char *>(word));
    return paw::cli::toy_request(static_cast<int>(argv.size()), argv.data());
}

static bool refused(std::initializer_list<const char *> words) {
    try {
        request(words);
    } catch (const std::runtime_error &) {
        return true;
    }
    return false;
}

int main() {
    CHECK(request({"paw", "shake"}) == "shake");
    CHECK(request({"paw", "shake", "7"}) == "shake 7", "a window's number goes along");
    CHECK(request({"paw", "gravity"}) == "gravity");
    CHECK(request({"paw", "party"}) == "party");
    CHECK(refused({"paw", "shake", "7", "8"}), "one window at most");
    CHECK(refused({"paw", "gravity", "now"}), "gravity takes nothing");
    CHECK(refused({"paw", "party", "hard"}), "nor does a party");
    CHECK(refused({"paw", "dance"}), "only the toys");
    if (failures)
        return 1;
    std::puts("toy_command passed");
    return 0;
}
