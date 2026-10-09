#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds and tests everything in one go: a normal build with the whole test suite, then a second
# build with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection running the suite
# again. Everything runs headless; nothing touches a real session.
#
#   tools/check-all.sh              both passes
#   tools/check-all.sh --quick      only the normal pass
#   tools/check-all.sh --sanitize   only the sanitizer pass
#   tools/check-all.sh --repeat 20  run each test up to 20 times, stopping at its first failure
#                                   (finds flaky tests)
#
# BUILD_DIR (default build), ASAN_DIR (default build-asan) and JOBS (default: CPUs, at most 8,
# since the tests start compositors) can be set in the environment.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${BUILD_DIR:-$root/build}
asan=${ASAN_DIR:-$root/build-asan}
jobs=${JOBS:-$(n=$(nproc); echo $((n > 8 ? 8 : n)))}
normal=1 sanitize=1 repeat=()

while [ $# -gt 0 ]; do
    case $1 in
        --quick) sanitize=0 ;;
        --sanitize) normal=0 ;;
        --repeat) shift; repeat=(--repeat "until-fail:${1:?--repeat needs a count}") ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

generator=()
command -v ninja > /dev/null && generator=(-G Ninja)

configure_and_build() { # directory, extra cmake arguments...
    local dir=$1; shift
    cmake -S "$root" -B "$dir" "${generator[@]}" -DPAW_BUILD_COMPOSITOR=ON "$@"
    cmake --build "$dir"
}

if [ $normal = 1 ]; then
    echo "== Build and test ($build)"
    configure_and_build "$build"
    ctest --test-dir "$build" -j "$jobs" --output-on-failure "${repeat[@]}"
fi

if [ $sanitize = 1 ]; then
    echo "== Sanitizers: address, undefined, leaks ($asan)"
    flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
    configure_and_build "$asan" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
    # The test programs themselves exit without freeing their Wayland and Qt state; the
    # suppressions leave leak reports to the compositor and the shell. They match a test's
    # source file, which the fast unwinder loses inside libraries built without frame
    # pointers (libwayland-client, Qt), so leaks are unwound the slow way.
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:fast_unwind_on_malloc=0 \
    LSAN_OPTIONS=suppressions=$root/tests/lsan.supp:print_suppressions=0:fast_unwind_on_malloc=0 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        ctest --test-dir "$asan" -j "$jobs" --output-on-failure "${repeat[@]}"
fi
echo "== All checks passed"
