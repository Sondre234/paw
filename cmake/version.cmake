# Writes OUTPUT, a header defining PAW_VERSION for --version: VERSION, the project's,
# followed by `git describe` when SOURCE_DIR is a git checkout of its own (a release tarball is
# not) and HEAD is not the release's tag. Run on every build; the file only changes, and
# rebuilds what includes it, when the description does.
#     cmake -DSOURCE_DIR=... -DOUTPUT=... -DVERSION=... -P version.cmake
set(TEXT "${VERSION}")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --show-toplevel
        OUTPUT_VARIABLE TOP OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE FAILED)
    if(NOT FAILED)
        file(REAL_PATH "${TOP}" TOP)
        file(REAL_PATH "${SOURCE_DIR}" SOURCE)
    endif()
    if(NOT FAILED AND TOP STREQUAL SOURCE)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" describe --tags --always
            --match "v[0-9]*" OUTPUT_VARIABLE DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE FAILED)
        if(NOT FAILED AND DESCRIBE AND NOT DESCRIBE STREQUAL "v${VERSION}")
            string(APPEND TEXT " (git ${DESCRIBE})")
        endif()
    endif()
endif()
file(CONFIGURE OUTPUT "${OUTPUT}"
    CONTENT "#define PAW_VERSION \"${TEXT}\"\n")
