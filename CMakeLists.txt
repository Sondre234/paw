cmake_minimum_required(VERSION 3.25)
project(paw VERSION 0.1.1 LANGUAGES C CXX)

# An unoptimised compositor is noticeably slower; default to an optimised build with symbols
# unless the caller (or a multi-config generator) chose one.
get_property(PAW_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT CMAKE_BUILD_TYPE AND NOT PAW_MULTI_CONFIG)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_search_module(LUA REQUIRED IMPORTED_TARGET lua5.4 lua-5.4 lua54 lua)
if(LUA_VERSION VERSION_LESS 5.4 OR NOT LUA_VERSION VERSION_LESS 5.5)
    message(FATAL_ERROR "paw requires Lua 5.4; found ${LUA_VERSION}")
endif()
pkg_check_modules(XKB REQUIRED IMPORTED_TARGET xkbcommon)
include(GNUInstallDirs)
option(PAW_INSTALL_SESSION "Install the display-manager session entry" OFF)
option(PAW_BUILD_SHELL "Build the Qt Quick desktop shell" ON)
option(PAW_SHELL_PREVIEW_ONLY "Build only the shell UI preview without LayerShellQt" OFF)

add_library(paw_config STATIC src/curve.c src/session.c src/tabs.c src/config.cpp src/config_schema.cpp src/layout.cpp src/tiling.cpp
    src/overview_layout.cpp src/window_placement.cpp
    src/import/common.cpp src/import/hyprland.cpp src/import/waybar.cpp src/import/import.cpp
    src/autostart.cpp)
target_include_directories(paw_config PUBLIC include)
target_link_libraries(paw_config PRIVATE paw_effects PkgConfig::LUA PUBLIC PkgConfig::XKB)
target_compile_options(paw_config PRIVATE -Wall -Wextra -Wpedantic)
# The source tree's example stands in for a configuration with an error when paw runs
# uninstalled.
target_compile_definitions(paw_config PRIVATE
    PAW_DEFAULT_CONFIG="${CMAKE_INSTALL_FULL_DATADIR}/paw/init.lua"
    PAW_SOURCE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/init.lua")

# Arithmetic of the desktop effects (fades, night light, corners, zoom) and of touchpad swipes;
# no wlroots.
add_library(paw_effects STATIC src/effects.c src/swipe.c)
target_include_directories(paw_effects PUBLIC include)
target_link_libraries(paw_effects PRIVATE m)
target_compile_options(paw_effects PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    # Tests read a copy of the example so a theme.lua generated beside it by `paw import`
    # (git-ignored, personal) cannot change what they see.
    set(EXAMPLE_CONFIG ${CMAKE_CURRENT_BINARY_DIR}/example-config/init.lua)
    configure_file(config/init.lua ${EXAMPLE_CONFIG} COPYONLY)
    # A unit test: tests/NAME_tests.cpp linked to paw_config (or LIBRARY), run as the test NAME
    # with the arguments.
    function(paw_unit_test name)
        cmake_parse_arguments(PARSE_ARGV 1 UNIT "" "LIBRARY" "")
        if(NOT UNIT_LIBRARY)
            set(UNIT_LIBRARY paw_config)
        endif()
        add_executable(${name}_tests tests/${name}_tests.cpp)
        target_link_libraries(${name}_tests PRIVATE ${UNIT_LIBRARY})
        add_test(NAME ${name} COMMAND ${name}_tests ${UNIT_UNPARSED_ARGUMENTS})
    endfunction()
    # A unit test of src/NAME.c on its own: tests/NAME_tests.c, run as the test NAME.
    function(paw_c_unit_test name)
        add_executable(${name}_tests tests/${name}_tests.c src/${name}.c)
        target_include_directories(${name}_tests PRIVATE include)
        target_compile_options(${name}_tests PRIVATE -Wall -Wextra)
        target_link_libraries(${name}_tests PRIVATE m)
        add_test(NAME ${name} COMMAND ${name}_tests)
    endfunction()
    paw_unit_test(config ${EXAMPLE_CONFIG})
    # Not the developer's saved appearance profile: the example's own `profile` is under test.
    set(EMPTY_STATE ${CMAKE_CURRENT_BINARY_DIR}/empty-state)
    file(MAKE_DIRECTORY ${EMPTY_STATE})
    set_tests_properties(config PROPERTIES ENVIRONMENT "XDG_STATE_HOME=${EMPTY_STATE}")
    paw_unit_test(config_diagnostics ${EXAMPLE_CONFIG}
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/config-reference.md)
    paw_unit_test(config_robustness ${EXAMPLE_CONFIG})
    set_tests_properties(config_robustness PROPERTIES TIMEOUT 900)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # A Python test: paw_smoke(NAME [SCRIPT FILE] [TIMEOUT SECONDS] ARGUMENTS...) runs
    # tests/NAME.py, or tests/FILE, with the arguments, for at most a minute or the timeout.
    function(paw_smoke name)
        cmake_parse_arguments(PARSE_ARGV 1 SMOKE "" "SCRIPT;TIMEOUT" "")
        if(NOT SMOKE_SCRIPT)
            set(SMOKE_SCRIPT ${name}.py)
        endif()
        if(NOT SMOKE_TIMEOUT)
            set(SMOKE_TIMEOUT 60)
        endif()
        add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/${SMOKE_SCRIPT}
            ${SMOKE_UNPARSED_ARGUMENTS})
        set_tests_properties(${name} PROPERTIES TIMEOUT ${SMOKE_TIMEOUT})
    endfunction()
    paw_smoke(docs_consistency ${CMAKE_CURRENT_SOURCE_DIR})
    paw_smoke(session_wrapper SCRIPT session_wrapper_test.py
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/paw-session)
    paw_unit_test(layout)
    paw_unit_test(curve)
    paw_unit_test(window_placement)
    paw_unit_test(overview_layout)
    paw_unit_test(tiling)
    paw_unit_test(effects LIBRARY paw_effects)
    paw_unit_test(swipe LIBRARY paw_effects)
    paw_unit_test(input_config)
    paw_unit_test(effects_config)
    paw_unit_test(keyboard_config)
    paw_unit_test(notifications_config)
    paw_unit_test(profiles_config)
    paw_unit_test(window_features)
    paw_unit_test(import ${CMAKE_CURRENT_SOURCE_DIR}/tests/import)
    paw_unit_test(urgent_config)
    paw_unit_test(productivity)
    paw_unit_test(power_config)
    paw_unit_test(autostart)
    paw_unit_test(display_power_config)
    paw_unit_test(output_config)
    paw_c_unit_test(power_supply)
    paw_c_unit_test(lid)
    paw_c_unit_test(output_state)
    paw_c_unit_test(border)
    paw_c_unit_test(dynamic_rule)
endif()
option(PAW_BUILD_COMPOSITOR "Build the wlroots compositor" ON)
if(PAW_BUILD_COMPOSITOR)
    # The wlroots API is unstable. Target one release series deliberately.
    pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET "wlroots-0.20>=0.20.0" "wlroots-0.20<0.21")
    pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-server)
    # Regions of damage, which wlroots' headers name but its link line leaves out.
    pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
    pkg_check_modules(PROTOCOLS REQUIRED wayland-protocols)
    pkg_get_variable(PROTOCOL_DIR wayland-protocols pkgdatadir)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
    set(WLR_PROTOCOL_HEADERS)
    set(WLR_CLIENT_PROTOCOL_SOURCES)
    # wlroots implements the wlr protocols; paw-window-control-v1 is the compositor's own.
    foreach(PROTOCOL wlr-layer-shell-unstable-v1 wlr-foreign-toplevel-management-unstable-v1
            wlr-output-management-unstable-v1 paw-window-control-v1
            wlr-output-power-management-unstable-v1)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-protocol.h
            COMMAND ${WAYLAND_SCANNER} server-header
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL}.xml
            ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-protocol.h
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL}.xml)
        list(APPEND WLR_PROTOCOL_HEADERS ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-protocol.h)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL}.xml
            ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-client-protocol.h
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL}.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL}.xml
            ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-protocol.c
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL}.xml)
        list(APPEND WLR_CLIENT_PROTOCOL_SOURCES
            ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/${PROTOCOL}-protocol.c)
    endforeach()
    # A window's capture source, which paw-window-control-v1 hands out and the shell's window
    # pictures copy, is ext-image-capture-source-v1's, and that protocol names ext-foreign-toplevel
    # handles. wlroots implements them but keeps their interfaces to itself, so every program
    # speaking paw-window-control-v1 compiles these in. The shell's clipboard history speaks
    # ext-data-control-v1.
    foreach(PROTOCOL staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1
            staging/ext-image-capture-source/ext-image-capture-source-v1
            staging/ext-image-copy-capture/ext-image-copy-capture-v1
            staging/ext-data-control/ext-data-control-v1)
        get_filename_component(NAME ${PROTOCOL} NAME)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_DIR}/${PROTOCOL}.xml
            ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-client-protocol.h
            DEPENDS ${PROTOCOL_DIR}/${PROTOCOL}.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_DIR}/${PROTOCOL}.xml
            ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-protocol.c
            DEPENDS ${PROTOCOL_DIR}/${PROTOCOL}.xml)
        list(APPEND WLR_CLIENT_PROTOCOL_SOURCES
            ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-protocol.c)
    endforeach()
    set(CAPTURE_SOURCE_PROTOCOL_SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/ext-image-capture-source-v1-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/ext-foreign-toplevel-list-v1-protocol.c)
    # A custom command's rule is only generated for targets in this directory, but paw-shell
    # (in shell/) uses the client files too. Without the tests, no target here uses them, so
    # this target carries the rule and paw-shell depends on it.
    add_custom_target(paw_wlr_client_protocols DEPENDS ${WLR_CLIENT_PROTOCOL_SOURCES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h
        COMMAND ${WAYLAND_SCANNER} server-header
        ${PROTOCOL_DIR}/stable/xdg-shell/xdg-shell.xml
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h
        DEPENDS ${PROTOCOL_DIR}/stable/xdg-shell/xdg-shell.xml)
    # X11 windows' keyboard grabs (shortcuts_inhibit.c), a protocol wlroots does not implement.
    set(XWAYLAND_GRAB_XML
        ${PROTOCOL_DIR}/unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xwayland-keyboard-grab-unstable-v1-protocol.h
        COMMAND ${WAYLAND_SCANNER} server-header ${XWAYLAND_GRAB_XML}
        ${CMAKE_CURRENT_BINARY_DIR}/xwayland-keyboard-grab-unstable-v1-protocol.h
        DEPENDS ${XWAYLAND_GRAB_XML})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xwayland-keyboard-grab-unstable-v1-protocol.c
        COMMAND ${WAYLAND_SCANNER} private-code ${XWAYLAND_GRAB_XML}
        ${CMAKE_CURRENT_BINARY_DIR}/xwayland-keyboard-grab-unstable-v1-protocol.c
        DEPENDS ${XWAYLAND_GRAB_XML})
    set(PAW_COMPOSITOR_SOURCES
        src/compositor/server.c
        src/compositor/actions.c
        src/compositor/binding_mode.c
        src/compositor/control.c
        src/compositor/cursor.c
        src/compositor/display_mode.c
        src/compositor/display_settings.c
        src/compositor/dynamic_rules.c
        src/compositor/effects.c
        src/compositor/focus.c
        src/compositor/foreign_toplevel.c
        src/compositor/frame.c
        src/compositor/gestures.c
        src/compositor/gradient_border.c
        src/compositor/grab.c
        src/compositor/group.c
        src/compositor/hdr.c
        src/compositor/headless_input.c
        src/compositor/idle.c
        src/compositor/input.c
        src/compositor/input_method.c
        src/compositor/keymap.c
        src/compositor/layer_shell.c
        src/compositor/lock.c
        src/compositor/mirror.c
        src/compositor/output.c
        src/compositor/output_moves.c
        src/compositor/output_power.c
        src/compositor/overview.c
        src/compositor/placement.c
        src/compositor/power.c
        src/compositor/query.c
        src/compositor/scaled_capture.c
        src/compositor/scratchpad.c
        src/compositor/session.c
        src/compositor/shortcuts_inhibit.c
        src/compositor/snap.c
        src/compositor/stacking.c
        src/compositor/swallow.c
        src/compositor/switcher.c
        src/compositor/switches.c
        src/compositor/tablet.c
        src/compositor/tearing.c
        src/compositor/tiling.c
        src/compositor/touch.c
        src/compositor/toplevel.c
        src/compositor/type.c
        src/compositor/volume.c
        src/compositor/window_control.c
        src/compositor/window_icon.c
        src/compositor/workspace.c
        src/compositor/xwayland.c
    )
    add_executable(paw src/main.cpp ${PAW_COMPOSITOR_SOURCES} src/decoration.c src/shadow.c src/animation.c src/effects_scene.c src/sleep.c src/login1.c
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h ${WLR_PROTOCOL_HEADERS})
    # The compositor's own protocol names a taskbar handle, whose interface wlroots keeps to
    # itself: both interfaces are compiled in here.
    target_sources(paw PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/paw-window-control-v1-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
        ${CAPTURE_SOURCE_PROTOCOL_SOURCES})
    target_sources(paw PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/xwayland-keyboard-grab-unstable-v1-protocol.h
        ${CMAKE_CURRENT_BINARY_DIR}/xwayland-keyboard-grab-unstable-v1-protocol.c)
    target_sources(paw PRIVATE src/overview_scene.c)
    target_sources(paw PRIVATE src/power_supply.c)
    target_sources(paw PRIVATE src/lid.c)
    target_sources(paw PRIVATE src/output_state.c)
    target_sources(paw PRIVATE src/dynamic_rule.c)
    target_sources(paw PRIVATE src/border.c)
    target_include_directories(paw PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(paw PRIVATE WLR_USE_UNSTABLE)
    # version.h, for --version: the project version, and `git describe` in a git checkout.
    add_custom_target(paw_version
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/version.h -DVERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.h)
    add_dependencies(paw paw_version)
    target_compile_options(paw PRIVATE -Wall -Wextra -Wno-unused-parameter)
    # A compositor function shared between files is declared in src/compositor/server.h; any
    # other is static. This flags one that is neither.
    set_source_files_properties(${PAW_COMPOSITOR_SOURCES} PROPERTIES
        COMPILE_OPTIONS -Wmissing-prototypes)
    target_link_libraries(paw PRIVATE paw_config paw_effects PkgConfig::WLROOTS PkgConfig::WAYLAND
        PkgConfig::PIXMAN m)
    # A standalone session keeps the machine awake through logind while it is on screen.
    pkg_search_module(SDBUS IMPORTED_TARGET libsystemd libelogind basu)
    if(SDBUS_FOUND)
        string(TOUPPER ${SDBUS_MODULE_NAME} PAW_SDBUS)
        string(REPLACE "LIB" "" PAW_SDBUS ${PAW_SDBUS})
        target_compile_definitions(paw PRIVATE PAW_SDBUS_${PAW_SDBUS}=1)
        target_link_libraries(paw PRIVATE PkgConfig::SDBUS)
    else()
        message(STATUS "No sd-bus (libsystemd, libelogind, basu); other desktops may suspend "
            "the machine while paw is on screen")
    endif()
    # X11 support follows the wlroots build; the XWM waker needs xcb directly, and reading an
    # X11 window's icon xcb-ewmh.
    find_file(PAW_WLR_CONFIG_H wlr/config.h PATHS ${WLROOTS_INCLUDE_DIRS} NO_DEFAULT_PATH)
    file(READ ${PAW_WLR_CONFIG_H} PAW_WLR_CONFIG)
    if(PAW_WLR_CONFIG MATCHES "WLR_HAS_XWAYLAND 1")
        # Unpatched wlroots strands X events; see packaging/patches/wlroots-xwm-drain.patch.
        option(PAW_XWM_WAKER "Nudge wlroots' X11 window manager (off only for patched wlroots)" ON)
        pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
        pkg_check_modules(XCB_EWMH REQUIRED IMPORTED_TARGET xcb-ewmh)
        target_link_libraries(paw PRIVATE PkgConfig::XCB PkgConfig::XCB_EWMH)
        if(PAW_XWM_WAKER)
            pkg_check_modules(XCB_XFIXES REQUIRED IMPORTED_TARGET xcb-xfixes)
            target_link_libraries(paw PRIVATE PkgConfig::XCB_XFIXES)
            target_compile_definitions(paw PRIVATE PAW_XWM_WAKER=1)
        endif()
        set(PAW_HAS_XWAYLAND ON)
    else()
        message(STATUS "wlroots lacks XWayland support; X11 applications will not run")
    endif()
    # Rounded window corners need wlroots patched by
    # packaging/patches/wlroots-rounded-corners.patch; without it windows stay square.
    find_file(PAW_WLR_SCENE_H wlr/types/wlr_scene.h PATHS ${WLROOTS_INCLUDE_DIRS}
              NO_DEFAULT_PATH)
    file(READ ${PAW_WLR_SCENE_H} PAW_WLR_SCENE)
    if(PAW_WLR_SCENE MATCHES "wlr_scene_tree_set_rounded_clip")
        target_compile_definitions(paw PRIVATE PAW_ROUNDED_CORNERS=1)
    else()
        message(STATUS "wlroots lacks rounded clips; window corners stay square "
            "(see packaging/patches/wlroots-rounded-corners.patch)")
    endif()
    # Pointer settings reach libinput devices directly (standalone sessions only).
    if(PAW_WLR_CONFIG MATCHES "WLR_HAS_LIBINPUT_BACKEND 1")
        pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
        target_link_libraries(paw PRIVATE PkgConfig::LIBINPUT)
    endif()
    install(TARGETS paw RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # Starts a standalone session with a log and, where there is none, a D-Bus session bus.
    install(PROGRAMS packaging/paw-session DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES config/init.lua DESTINATION ${CMAKE_INSTALL_DATADIR}/paw)
    # docs/ keeps the README's links to them working in the installed copy.
    install(FILES README.md CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES docs/features.md docs/config-reference.md docs/dotfile-import.md docs/gentoo.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/paw)
    install(FILES vendor/tinywl/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/paw
        RENAME TinyWL-LICENSE)
    install(DIRECTORY protocols/ DESTINATION ${CMAKE_INSTALL_DATADIR}/paw/protocols)
    install(FILES packaging/paw-portals.conf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal)
    if(PAW_INSTALL_SESSION)
        configure_file(packaging/paw.desktop.in paw.desktop @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/paw.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
    endif()
    if(PAW_BUILD_SHELL)
        add_subdirectory(shell)
        if(NOT PAW_SHELL_PREVIEW_ONLY)
            target_compile_definitions(paw PRIVATE PAW_HAS_SHELL=1)
        endif()
    endif()
    if(BUILD_TESTING)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
        # A Wayland client for the tests: paw_probe(NAME SOURCES... [LIBRARIES LIBRARY...]), the
        # client protocols' code among the sources, generated in the build directory.
        function(paw_probe name)
            cmake_parse_arguments(PARSE_ARGV 1 PROBE "" "" "LIBRARIES")
            add_executable(${name} ${PROBE_UNPARSED_ARGUMENTS})
            target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
            target_link_libraries(${name} PRIVATE PkgConfig::WAYLAND_CLIENT ${PROBE_LIBRARIES})
            target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        endfunction()
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header
            ${PROTOCOL_DIR}/stable/xdg-shell/xdg-shell.xml
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            DEPENDS ${PROTOCOL_DIR}/stable/xdg-shell/xdg-shell.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code
            ${PROTOCOL_DIR}/stable/xdg-shell/xdg-shell.xml
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
            DEPENDS ${PROTOCOL_DIR}/stable/xdg-shell/xdg-shell.xml)
        set(ACTIVATION_XML ${PROTOCOL_DIR}/staging/xdg-activation/xdg-activation-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${ACTIVATION_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-v1-client-protocol.h
            DEPENDS ${ACTIVATION_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${ACTIVATION_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-v1-protocol.c
            DEPENDS ${ACTIVATION_XML})
        set(TOPLEVEL_ICON_XML ${PROTOCOL_DIR}/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${TOPLEVEL_ICON_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-client-protocol.h
            DEPENDS ${TOPLEVEL_ICON_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${TOPLEVEL_ICON_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-protocol.c
            DEPENDS ${TOPLEVEL_ICON_XML})
        set(TEARING_XML ${PROTOCOL_DIR}/staging/tearing-control/tearing-control-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tearing-control-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${TEARING_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/tearing-control-v1-client-protocol.h
            DEPENDS ${TEARING_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tearing-control-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${TEARING_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/tearing-control-v1-protocol.c
            DEPENDS ${TEARING_XML})
        set(DECORATION_XML ${PROTOCOL_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${DECORATION_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-client-protocol.h
            DEPENDS ${DECORATION_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${DECORATION_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-protocol.c
            DEPENDS ${DECORATION_XML})
        paw_probe(wayland_probe tests/wayland_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-toplevel-icon-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/tearing-control-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/tearing-control-v1-protocol.c
            ${WLR_CLIENT_PROTOCOL_SOURCES}
            LIBRARIES PkgConfig::XKB)
        # A window printing the input it gets, for the input tests, or with --text-input one
        # taking text through text-input-unstable-v3, for the input method tests.
        set(GESTURES_XML ${PROTOCOL_DIR}/unstable/pointer-gestures/pointer-gestures-unstable-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pointer-gestures-unstable-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${GESTURES_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/pointer-gestures-unstable-v1-client-protocol.h
            DEPENDS ${GESTURES_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pointer-gestures-unstable-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${GESTURES_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/pointer-gestures-unstable-v1-protocol.c
            DEPENDS ${GESTURES_XML})
        set(TABLET_XML ${PROTOCOL_DIR}/stable/tablet/tablet-v2.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tablet-v2-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${TABLET_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/tablet-v2-client-protocol.h
            DEPENDS ${TABLET_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tablet-v2-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${TABLET_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/tablet-v2-protocol.c
            DEPENDS ${TABLET_XML})
        set(SHORTCUTS_INHIBIT_XML
            ${PROTOCOL_DIR}/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${SHORTCUTS_INHIBIT_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h
            DEPENDS ${SHORTCUTS_INHIBIT_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/keyboard-shortcuts-inhibit-unstable-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${SHORTCUTS_INHIBIT_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/keyboard-shortcuts-inhibit-unstable-v1-protocol.c
            DEPENDS ${SHORTCUTS_INHIBIT_XML})
        set(TEXT_INPUT_XML ${PROTOCOL_DIR}/unstable/text-input/text-input-unstable-v3.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${TEXT_INPUT_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h
            DEPENDS ${TEXT_INPUT_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${TEXT_INPUT_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c
            DEPENDS ${TEXT_INPUT_XML})
        paw_probe(input_probe tests/input_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/keyboard-shortcuts-inhibit-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/tablet-v2-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/tablet-v2-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/pointer-gestures-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/pointer-gestures-unstable-v1-protocol.c)
        set(VPTR_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-virtual-pointer-unstable-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${VPTR_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-client-protocol.h
            DEPENDS ${VPTR_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${VPTR_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-protocol.c
            DEPENDS ${VPTR_XML})
        set(VKBD_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/virtual-keyboard-unstable-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${VKBD_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-client-protocol.h
            DEPENDS ${VKBD_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${VKBD_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-protocol.c
            DEPENDS ${VKBD_XML})
        paw_probe(pointer_probe tests/pointer_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-protocol.c)
        # The other side of input methods, for input_method_smoke: an input method
        # (input-method-unstable-v2); input_probe --text-input is an application's text input.
        set(INPUT_METHOD_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/input-method-unstable-v2.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${INPUT_METHOD_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-client-protocol.h
            DEPENDS ${INPUT_METHOD_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${INPUT_METHOD_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c
            DEPENDS ${INPUT_METHOD_XML})
        paw_probe(input_method_probe tests/input_method_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-protocol.c)
        paw_probe(output_management_probe tests/output_management_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-management-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-management-unstable-v1-protocol.c)
        # A wlr-output-power-management client, as wlopm is, for output_power_smoke.
        paw_probe(output_power_probe tests/output_power_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.c)
        # Clients of idle-inhibit-unstable-v1 and ext-idle-notify-v1, for idle_smoke.
        foreach(PROTOCOL unstable/idle-inhibit/idle-inhibit-unstable-v1
                staging/ext-idle-notify/ext-idle-notify-v1)
            get_filename_component(NAME ${PROTOCOL} NAME)
            add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-client-protocol.h
                COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_DIR}/${PROTOCOL}.xml
                ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-client-protocol.h
                DEPENDS ${PROTOCOL_DIR}/${PROTOCOL}.xml)
            add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-protocol.c
                COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_DIR}/${PROTOCOL}.xml
                ${CMAKE_CURRENT_BINARY_DIR}/${NAME}-protocol.c
                DEPENDS ${PROTOCOL_DIR}/${PROTOCOL}.xml)
        endforeach()
        paw_probe(idle_probe tests/idle_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-protocol.c)
        # A taskbar's window menu through paw-window-control-v1, for window_control_smoke,
        # its capture of a window, for window_capture_smoke, its peek at one, for
        # window_peek_smoke, and windows' own icons, for window_icon_smoke.
        paw_probe(window_probe tests/window_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/paw-window-control-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/paw-window-control-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-foreign-toplevel-list-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-image-capture-source-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-image-copy-capture-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-image-copy-capture-v1-protocol.c
            ${CAPTURE_SOURCE_PROTOCOL_SOURCES})
        # A program copying and pasting through ext-data-control-v1, for the shell's clipboard
        # history test.
        paw_probe(clipboard_probe tests/clipboard_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-protocol.c)
        paw_smoke(output_management SCRIPT output_management_smoke.py $<TARGET_FILE:paw>
            $<TARGET_FILE:output_management_probe>)
        paw_smoke(output_power_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:output_power_probe> $<TARGET_FILE:lock_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(mirror_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:lock_probe> $<TARGET_FILE:output_management_probe>)
        paw_smoke(bit_depth_smoke $<TARGET_FILE:paw> $<TARGET_FILE:output_management_probe>)
        paw_smoke(hdr_smoke $<TARGET_FILE:paw>)
        paw_smoke(tearing_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(display_mode_smoke $<TARGET_FILE:paw>)
        paw_smoke(display_settings_smoke TIMEOUT 90 $<TARGET_FILE:paw>
            $<TARGET_FILE:output_management_probe>)
        paw_smoke(lid_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(compositor_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        paw_smoke(auto_reload_smoke $<TARGET_FILE:paw> ${EXAMPLE_CONFIG})
        # Without a personal configuration, paw runs the shipped one.
        set(EMPTY_HOME ${CMAKE_CURRENT_BINARY_DIR}/empty-home)
        file(MAKE_DIRECTORY ${EMPTY_HOME})
        add_test(NAME version COMMAND paw --version)
        set_tests_properties(version PROPERTIES
            PASS_REGULAR_EXPRESSION "^paw ${PROJECT_VERSION}( \\(git [^)]+\\))?\nbuilt with wlroots 0\\.20\\.[0-9]+\n$")
        add_test(NAME default_config COMMAND paw --check-config)
        set_tests_properties(default_config PROPERTIES
            ENVIRONMENT "HOME=${EMPTY_HOME};XDG_CONFIG_HOME=${EMPTY_HOME};XDG_STATE_HOME=${EMPTY_STATE};PAW_DEFAULT_CONFIG=${EXAMPLE_CONFIG}"
            PASS_REGULAR_EXPRESSION "Configuration valid: .*example-config/init.lua")
        paw_smoke(workspaces SCRIPT workspace_smoke.py $<TARGET_FILE:paw>
            $<TARGET_FILE:wayland_probe> ${EXAMPLE_CONFIG})
        paw_smoke(control_fuzz_smoke TIMEOUT 120 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        paw_smoke(swallow_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        paw_smoke(pid_at_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> ${EXAMPLE_CONFIG})
        paw_smoke(placement_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(sticky_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> ${EXAMPLE_CONFIG})
        paw_smoke(dynamic_rules_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(stacking_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(window_control_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:window_probe>)
        paw_smoke(workspace_back_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(tiling_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> ${EXAMPLE_CONFIG})
        paw_smoke(tiling_open_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        paw_smoke(smart_gaps_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(maximized_tile_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        paw_smoke(screenshot_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        paw_smoke(launch_smoke $<TARGET_FILE:paw> ${EXAMPLE_CONFIG})
        paw_smoke(autostart_smoke $<TARGET_FILE:paw>)
        paw_smoke(startup_failure_smoke $<TARGET_FILE:paw> ${EXAMPLE_CONFIG})
        paw_smoke(output_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(output_tiling_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(per_output_tiling_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(per_workspace_tiling_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(output_layout_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(output_hotplug_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(output_workspace_move_smoke TIMEOUT 90 $<TARGET_FILE:paw>
            $<TARGET_FILE:wayland_probe>)
        paw_smoke(move_window_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(focus_output_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(workspace_names_smoke $<TARGET_FILE:paw>)
        paw_smoke(profile_smoke $<TARGET_FILE:paw>)
        paw_smoke(session_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(session_scroll_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(session_restore_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(focus_last_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(switcher_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:window_probe>)
        paw_smoke(overview_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(overview_pointer_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(overview_fuzz_smoke TIMEOUT 120 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(overview_output_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(panel_focus_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(drag_strip_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(drag_focus_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:window_probe> $<TARGET_FILE:pointer_probe>)
        paw_smoke(pointer_gestures_smoke $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        paw_smoke(touchpad_gestures_smoke $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        paw_smoke(touchscreen_smoke $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>
            $<TARGET_FILE:wayland_probe>)
        paw_smoke(tablet_smoke $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        paw_smoke(spawn_at_pointer_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(scratchpad_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(group_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(group_fuzz_smoke TIMEOUT 120 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(reload_fuzz_smoke TIMEOUT 120 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(tile_focus_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(layout_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(scroll_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(scroll_follow_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(keyboard_resize_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(volume_keys_smoke $<TARGET_FILE:paw>)
        paw_smoke(keymap_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe> ${EXAMPLE_CONFIG})
        paw_smoke(media_keys_smoke $<TARGET_FILE:paw> ${EXAMPLE_CONFIG})
        paw_smoke(type_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(keyboard_layout_smoke $<TARGET_FILE:paw> $<TARGET_FILE:pointer_probe>)
        paw_smoke(window_style_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(window_round_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(window_rules_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(output_workspace_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(output_follow_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        add_executable(animation_tests tests/animation_tests.c src/animation.c
            src/overview_scene.c)
        target_compile_definitions(animation_tests PRIVATE WLR_USE_UNSTABLE)
        target_compile_options(animation_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_link_libraries(animation_tests PRIVATE paw_config PkgConfig::WLROOTS
            PkgConfig::WAYLAND m)
        add_test(NAME animation COMMAND animation_tests)
        add_executable(decoration_tests tests/decoration_tests.c src/decoration.c src/shadow.c)
        target_compile_definitions(decoration_tests PRIVATE WLR_USE_UNSTABLE)
        target_compile_options(decoration_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_include_directories(decoration_tests PRIVATE include)
        target_link_libraries(decoration_tests PRIVATE PkgConfig::WLROOTS m)
        add_test(NAME decoration COMMAND decoration_tests)
        paw_smoke(animation_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(fullscreen_panel_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        find_program(GRIM grim)
        # A missing grim is an empty argument, which the tests take as leaving out their
        # pixel checks (GRIM itself would be GRIM-NOTFOUND).
        set(GRIM_ARGUMENT "$<$<BOOL:${GRIM}>:${GRIM}>")
        paw_smoke(gradient_border_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            "${GRIM_ARGUMENT}")
        paw_smoke(dim_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        paw_smoke(idle_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:idle_probe>
            $<TARGET_FILE:lock_probe> $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        paw_smoke(urgent_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(urgent_border_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            "${GRIM_ARGUMENT}")
        paw_smoke(window_controls_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        paw_smoke(window_shadow_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        paw_smoke(peek_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        paw_smoke(magnet_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        paw_smoke(snap_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        paw_smoke(snap_keys_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        paw_smoke(snap_assist_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(night_light_smoke TIMEOUT 90 $<TARGET_FILE:paw>)
        paw_smoke(hot_corners_smoke $<TARGET_FILE:paw> $<TARGET_FILE:pointer_probe>)
        paw_smoke(zoom_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        paw_smoke(zoom_output_smoke $<TARGET_FILE:paw> $<TARGET_FILE:pointer_probe>)
        paw_smoke(example_snippets TIMEOUT 120 $<TARGET_FILE:paw>
            ${CMAKE_CURRENT_SOURCE_DIR}/config/init.lua)
        set(LOCK_XML ${PROTOCOL_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${LOCK_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
            DEPENDS ${LOCK_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${LOCK_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c
            DEPENDS ${LOCK_XML})
        paw_probe(lock_probe tests/lock_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
        paw_smoke(session_lock SCRIPT lock_smoke.py $<TARGET_FILE:paw> $<TARGET_FILE:lock_probe>
            ${EXAMPLE_CONFIG})
        paw_smoke(binding_modes_smoke $<TARGET_FILE:paw> $<TARGET_FILE:lock_probe>)
        paw_smoke(locked_bindings_smoke $<TARGET_FILE:paw> $<TARGET_FILE:lock_probe>)
        paw_smoke(shortcuts_inhibit_smoke $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        paw_smoke(input_method_smoke $<TARGET_FILE:paw> $<TARGET_FILE:input_method_probe>
            $<TARGET_FILE:lock_probe> $<TARGET_FILE:input_probe> "${GRIM_ARGUMENT}")
        paw_smoke(window_capture_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:window_probe> $<TARGET_FILE:lock_probe>)
        paw_smoke(window_peek_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:window_probe> $<TARGET_FILE:lock_probe> "${GRIM_ARGUMENT}")
        if(SDBUS_FOUND)
            # A stand-in for logind on a private bus, so that no power test reaches the real one.
            add_executable(fake_login1 tests/fake_login1.c)
            target_compile_definitions(fake_login1 PRIVATE PAW_SDBUS_${PAW_SDBUS}=1)
            target_link_libraries(fake_login1 PRIVATE PkgConfig::SDBUS)
            target_compile_options(fake_login1 PRIVATE -Wall -Wextra -Wno-unused-parameter)
            paw_smoke(power_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:fake_login1>
                $<TARGET_FILE:wayland_probe> $<TARGET_FILE:lock_probe>)
            paw_smoke(idle_suspend_smoke $<TARGET_FILE:paw> $<TARGET_FILE:fake_login1>
                $<TARGET_FILE:lock_probe>)
            paw_smoke(lid_logind_smoke $<TARGET_FILE:paw> $<TARGET_FILE:fake_login1>)
        endif()
        # The compositor benchmark (tools/bench/bench.py); see docs/performance.md.
        paw_probe(paw-bench-client tools/bench/bench_client.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
        add_test(NAME bench_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/bench/bench.py --quick
            --compositor $<TARGET_FILE:paw> --client $<TARGET_FILE:paw-bench-client>
            --pointer-probe $<TARGET_FILE:pointer_probe>)
        set_tests_properties(bench_smoke PROPERTIES TIMEOUT 120)
        paw_smoke(opacity_rules_smoke $<TARGET_FILE:paw> $<TARGET_FILE:paw-bench-client>
            $<TARGET_FILE:wayland_probe>)
        # XWayland coverage needs wlroots built with X support and an Xwayland binary.
        find_program(XWAYLAND_BINARY Xwayland)
        if(PAW_HAS_XWAYLAND AND XWAYLAND_BINARY)
            add_executable(x11_probe tests/x11_probe.c)
            target_link_libraries(x11_probe PRIVATE PkgConfig::XCB)
            target_compile_options(x11_probe PRIVATE -Wall -Wextra)
            paw_smoke(xwayland_smoke $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe>
                $<TARGET_FILE:wayland_probe> ${EXAMPLE_CONFIG})
            paw_smoke(swallow_x11_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe>
                $<TARGET_FILE:wayland_probe>)
            paw_smoke(xwayland_urgent_smoke $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe>
                $<TARGET_FILE:wayland_probe>)
            paw_smoke(xwayland_grab_smoke $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe>
                $<TARGET_FILE:wayland_probe>)
        else()
            message(STATUS "XWayland test skipped: needs an Xwayland binary and wlroots X support")
        endif()
        # Windows' own icons; an X11 window's only where the X11 probe is built.
        paw_smoke(window_icon_smoke $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:window_probe> $<$<TARGET_EXISTS:x11_probe>:$<TARGET_FILE:x11_probe>>)
    endif()
endif()
