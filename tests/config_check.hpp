// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The configuration tests' checks: one that fails throws, and main reports what it says.
#include <stdexcept>
#include <string>

#include "paw/config.hpp"

inline void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

// The error `source` gives, read as the file `name` (errors name its lines); fails the test when
// it is accepted.
inline std::string error_of(const std::string &source, const std::string &name = "@init.lua") {
    try {
        (void)paw::parse_config(source, name);
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}

// Fails the test unless `source` is refused, with `fragment` in the error when it is given.
inline void rejects(const std::string &source, const std::string &fragment = "") {
    auto message = error_of(source);
    require(message.find(fragment) != std::string::npos,
            "expected '" + fragment + "' in: " + message + "\n  for " + source);
}
