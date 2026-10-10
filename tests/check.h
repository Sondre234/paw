// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The unit tests' checks, for C and C++ alike. A check that fails says where, what did not hold
 * and, given a printf format and its arguments, what that means; it counts in `failures`, which
 * main turns into its exit status. */
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(condition, ...)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s", __FILE__, __LINE__, #condition);            \
            __VA_OPT__(fputs(": ", stderr); fprintf(stderr, __VA_ARGS__);)                         \
            fputc('\n', stderr);                                                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
/* Whether a is within tolerance of b. */
#define NEAR(a, b, tolerance) CHECK(fabs((a) - (b)) <= (tolerance))
