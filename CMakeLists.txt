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
    add_executable(config_tests tests/config_tests.cpp)
    target_link_libraries(config_tests PRIVATE paw_config)
    add_test(NAME config COMMAND config_tests ${EXAMPLE_CONFIG})
    # Not the developer's saved appearance profile: the example's own `profile` is under test.
    set(EMPTY_STATE ${CMAKE_CURRENT_BINARY_DIR}/empty-state)
    file(MAKE_DIRECTORY ${EMPTY_STATE})
    set_tests_properties(config PROPERTIES ENVIRONMENT "XDG_STATE_HOME=${EMPTY_STATE}")
    add_executable(config_diagnostics_tests tests/config_diagnostics_tests.cpp)
    target_link_libraries(config_diagnostics_tests PRIVATE paw_config)
    add_test(NAME config_diagnostics COMMAND config_diagnostics_tests ${EXAMPLE_CONFIG}
             ${CMAKE_CURRENT_SOURCE_DIR}/docs/config-reference.md)
    add_executable(config_robustness_tests tests/config_robustness_tests.cpp)
    target_link_libraries(config_robustness_tests PRIVATE paw_config)
    add_test(NAME config_robustness COMMAND config_robustness_tests ${EXAMPLE_CONFIG})
    set_tests_properties(config_robustness PROPERTIES TIMEOUT 900)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME docs_consistency COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/docs_consistency.py ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME session_wrapper COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_wrapper_test.py
             ${CMAKE_CURRENT_SOURCE_DIR}/packaging/paw-session)
    add_executable(layout_tests tests/layout_tests.cpp)
    target_link_libraries(layout_tests PRIVATE paw_config)
    add_test(NAME layout COMMAND layout_tests)
    add_executable(curve_tests tests/curve_tests.cpp)
    target_link_libraries(curve_tests PRIVATE paw_config)
    add_test(NAME curve COMMAND curve_tests)
    add_executable(window_placement_tests tests/window_placement_tests.cpp)
    target_link_libraries(window_placement_tests PRIVATE paw_config)
    add_test(NAME window_placement COMMAND window_placement_tests)
    add_executable(overview_layout_tests tests/overview_layout_tests.cpp)
    target_link_libraries(overview_layout_tests PRIVATE paw_config)
    add_test(NAME overview_layout COMMAND overview_layout_tests)
    add_executable(tiling_tests tests/tiling_tests.cpp)
    target_link_libraries(tiling_tests PRIVATE paw_config)
    add_test(NAME tiling COMMAND tiling_tests)
    add_executable(effects_tests tests/effects_tests.cpp)
    target_link_libraries(effects_tests PRIVATE paw_effects)
    add_test(NAME effects COMMAND effects_tests)
    add_executable(swipe_tests tests/swipe_tests.cpp)
    target_link_libraries(swipe_tests PRIVATE paw_effects)
    add_test(NAME swipe COMMAND swipe_tests)
    add_executable(input_config_tests tests/input_config_tests.cpp)
    target_link_libraries(input_config_tests PRIVATE paw_config)
    add_test(NAME input_config COMMAND input_config_tests)
    add_executable(effects_config_tests tests/effects_config_tests.cpp)
    target_link_libraries(effects_config_tests PRIVATE paw_config)
    add_test(NAME effects_config COMMAND effects_config_tests)
    add_executable(keyboard_config_tests tests/keyboard_config_tests.cpp)
    target_link_libraries(keyboard_config_tests PRIVATE paw_config)
    add_test(NAME keyboard_config COMMAND keyboard_config_tests)
    add_executable(notifications_config_tests tests/notifications_config_tests.cpp)
    target_link_libraries(notifications_config_tests PRIVATE paw_config)
    add_test(NAME notifications_config COMMAND notifications_config_tests)
    add_executable(profiles_config_tests tests/profiles_config_tests.cpp)
    target_link_libraries(profiles_config_tests PRIVATE paw_config)
    add_test(NAME profiles_config COMMAND profiles_config_tests)
    add_executable(window_features_tests tests/window_features_tests.cpp)
    target_link_libraries(window_features_tests PRIVATE paw_config)
    add_test(NAME window_features COMMAND window_features_tests)
    add_executable(import_tests tests/import_tests.cpp)
    target_link_libraries(import_tests PRIVATE paw_config)
    add_test(NAME import COMMAND import_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/import)
    add_executable(urgent_config_tests tests/urgent_config_tests.cpp)
    target_link_libraries(urgent_config_tests PRIVATE paw_config)
    add_test(NAME urgent_config COMMAND urgent_config_tests)
    add_executable(productivity_tests tests/productivity_tests.cpp)
    target_link_libraries(productivity_tests PRIVATE paw_config)
    add_test(NAME productivity COMMAND productivity_tests)
    add_executable(power_config_tests tests/power_config_tests.cpp)
    target_link_libraries(power_config_tests PRIVATE paw_config)
    add_test(NAME power_config COMMAND power_config_tests)
    add_executable(autostart_tests tests/autostart_tests.cpp)
    target_link_libraries(autostart_tests PRIVATE paw_config)
    add_test(NAME autostart COMMAND autostart_tests)
    add_executable(display_power_config_tests tests/display_power_config_tests.cpp)
    target_link_libraries(display_power_config_tests PRIVATE paw_config)
    add_test(NAME display_power_config COMMAND display_power_config_tests)
    add_executable(output_config_tests tests/output_config_tests.cpp)
    target_link_libraries(output_config_tests PRIVATE paw_config)
    add_test(NAME output_config COMMAND output_config_tests)
    add_executable(power_supply_tests tests/power_supply_tests.c src/power_supply.c)
    target_include_directories(power_supply_tests PRIVATE include)
    target_compile_options(power_supply_tests PRIVATE -Wall -Wextra)
    add_test(NAME power_supply COMMAND power_supply_tests)
    add_executable(lid_tests tests/lid_tests.c src/lid.c)
    target_include_directories(lid_tests PRIVATE include)
    target_compile_options(lid_tests PRIVATE -Wall -Wextra)
    add_test(NAME lid COMMAND lid_tests)
    add_executable(output_state_tests tests/output_state_tests.c src/output_state.c)
    target_include_directories(output_state_tests PRIVATE include)
    target_compile_options(output_state_tests PRIVATE -Wall -Wextra)
    add_test(NAME output_state COMMAND output_state_tests)
    add_executable(border_tests tests/border_tests.c src/border.c)
    target_include_directories(border_tests PRIVATE include)
    target_compile_options(border_tests PRIVATE -Wall -Wextra)
    target_link_libraries(border_tests PRIVATE m)
    add_test(NAME border COMMAND border_tests)
    add_executable(dynamic_rule_tests tests/dynamic_rule_tests.c src/dynamic_rule.c)
    target_include_directories(dynamic_rule_tests PRIVATE include)
    target_compile_options(dynamic_rule_tests PRIVATE -Wall -Wextra)
    add_test(NAME dynamic_rule COMMAND dynamic_rule_tests)
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
        add_executable(wayland_probe tests/wayland_probe.c
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
            ${WLR_CLIENT_PROTOCOL_SOURCES})
        target_include_directories(wayland_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(wayland_probe PRIVATE PkgConfig::WAYLAND_CLIENT PkgConfig::XKB)
        target_compile_options(wayland_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        # A window printing the input it gets, for the tests of gestures.
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
        add_executable(input_probe tests/input_probe.c
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
        target_include_directories(input_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(input_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(input_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
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
        add_executable(pointer_probe tests/pointer_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-virtual-pointer-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-protocol.c)
        target_include_directories(pointer_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(pointer_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(pointer_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        # Both sides of input methods, for input_method_smoke: an application's text input
        # (text-input-unstable-v3) and an input method (input-method-unstable-v2).
        set(TEXT_INPUT_XML ${PROTOCOL_DIR}/unstable/text-input/text-input-unstable-v3.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${TEXT_INPUT_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h
            DEPENDS ${TEXT_INPUT_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${TEXT_INPUT_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c
            DEPENDS ${TEXT_INPUT_XML})
        add_executable(text_input_probe tests/text_input_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
        target_include_directories(text_input_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(text_input_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(text_input_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        set(INPUT_METHOD_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/input-method-unstable-v2.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${INPUT_METHOD_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-client-protocol.h
            DEPENDS ${INPUT_METHOD_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${INPUT_METHOD_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c
            DEPENDS ${INPUT_METHOD_XML})
        add_executable(input_method_probe tests/input_method_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/virtual-keyboard-protocol.c)
        target_include_directories(input_method_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(input_method_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(input_method_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        add_executable(output_management_probe tests/output_management_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-management-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-management-unstable-v1-protocol.c)
        target_include_directories(output_management_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(output_management_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(output_management_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        # A wlr-output-power-management client, as wlopm is, for output_power_smoke.
        add_executable(output_power_probe tests/output_power_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.c)
        target_include_directories(output_power_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(output_power_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(output_power_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
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
        add_executable(idle_probe tests/idle_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-protocol.c)
        target_include_directories(idle_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(idle_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(idle_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        # A taskbar's window menu through paw-window-control-v1, for window_control_smoke,
        # its capture of a window, for window_capture_smoke, its peek at one, for
        # window_peek_smoke, and windows' own icons, for window_icon_smoke.
        add_executable(window_probe tests/window_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/paw-window-control-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/paw-window-control-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-foreign-toplevel-list-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-image-capture-source-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-image-copy-capture-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-image-copy-capture-v1-protocol.c
            ${CAPTURE_SOURCE_PROTOCOL_SOURCES})
        target_include_directories(window_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(window_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(window_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        # A program copying and pasting through ext-data-control-v1, for the shell's clipboard
        # history test.
        add_executable(clipboard_probe tests/clipboard_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-protocol.c)
        target_include_directories(clipboard_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(clipboard_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(clipboard_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        add_test(NAME output_management COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_management_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:output_management_probe>)
        set_tests_properties(output_management PROPERTIES TIMEOUT 60)
        add_test(NAME output_power_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_power_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:output_power_probe>
            $<TARGET_FILE:lock_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(output_power_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME mirror_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/mirror_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:lock_probe>
            $<TARGET_FILE:output_management_probe>)
        set_tests_properties(mirror_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME bit_depth_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/bit_depth_smoke.py $<TARGET_FILE:paw>
            $<TARGET_FILE:output_management_probe>)
        set_tests_properties(bit_depth_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME hdr_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/hdr_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(hdr_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME tearing_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tearing_smoke.py $<TARGET_FILE:paw>
            $<TARGET_FILE:wayland_probe>)
        set_tests_properties(tearing_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME display_mode_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/display_mode_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(display_mode_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME display_settings_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/display_settings_smoke.py $<TARGET_FILE:paw>
            $<TARGET_FILE:output_management_probe>)
        set_tests_properties(display_settings_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME lid_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/lid_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(lid_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME compositor_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/compositor_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(compositor_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME auto_reload_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/auto_reload_smoke.py $<TARGET_FILE:paw>
            ${EXAMPLE_CONFIG})
        set_tests_properties(auto_reload_smoke PROPERTIES TIMEOUT 60)
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
        add_test(NAME workspaces COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(workspaces PROPERTIES TIMEOUT 60)
        add_test(NAME control_fuzz_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_fuzz_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(control_fuzz_smoke PROPERTIES TIMEOUT 120)
        add_test(NAME swallow_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/swallow_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(swallow_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME pid_at_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/pid_at_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(pid_at_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME placement_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(placement_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME sticky_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sticky_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(sticky_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME dynamic_rules_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/dynamic_rules_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(dynamic_rules_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME stacking_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stacking_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(stacking_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME window_control_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_control_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>)
        set_tests_properties(window_control_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME workspace_back_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_back_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(workspace_back_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME tiling_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tiling_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(tiling_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME tiling_open_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tiling_open_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(tiling_open_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME smart_gaps_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/smart_gaps_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(smart_gaps_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME maximized_tile_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/maximized_tile_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(maximized_tile_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME screenshot_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/screenshot_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(screenshot_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME launch_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/launch_smoke.py
            $<TARGET_FILE:paw> ${EXAMPLE_CONFIG})
        set_tests_properties(launch_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME autostart_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/autostart_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(autostart_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME startup_failure_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_failure_smoke.py
            $<TARGET_FILE:paw> ${EXAMPLE_CONFIG})
        set_tests_properties(startup_failure_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME output_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(output_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME output_tiling_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_tiling_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(output_tiling_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME per_output_tiling_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/per_output_tiling_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(per_output_tiling_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME per_workspace_tiling_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/per_workspace_tiling_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(per_workspace_tiling_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME output_layout_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_layout_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(output_layout_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME output_hotplug_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_hotplug_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(output_hotplug_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME output_workspace_move_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_workspace_move_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(output_workspace_move_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME move_window_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/move_window_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(move_window_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME focus_output_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/focus_output_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(focus_output_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME workspace_names_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_names_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(workspace_names_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME profile_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/profile_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(profile_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME session_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(session_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME session_scroll_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_scroll_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(session_scroll_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME session_restore_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_restore_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(session_restore_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME focus_last_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/focus_last_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(focus_last_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME switcher_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/switcher_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>)
        set_tests_properties(switcher_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME overview_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/overview_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(overview_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME overview_pointer_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/overview_pointer_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(overview_pointer_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME overview_fuzz_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/overview_fuzz_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(overview_fuzz_smoke PROPERTIES TIMEOUT 120)
        add_test(NAME overview_output_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/overview_output_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(overview_output_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME panel_focus_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/panel_focus_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(panel_focus_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME drag_strip_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/drag_strip_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(drag_strip_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME drag_focus_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/drag_focus_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>
            $<TARGET_FILE:pointer_probe>)
        set_tests_properties(drag_focus_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME pointer_gestures_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/pointer_gestures_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        set_tests_properties(pointer_gestures_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME touchpad_gestures_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/touchpad_gestures_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        set_tests_properties(touchpad_gestures_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME touchscreen_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/touchscreen_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:input_probe> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(touchscreen_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME tablet_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tablet_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        set_tests_properties(tablet_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME spawn_at_pointer_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/spawn_at_pointer_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(spawn_at_pointer_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME scratchpad_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/scratchpad_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(scratchpad_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME group_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/group_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(group_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME group_edge_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/group_edge_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(group_edge_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME group_fuzz_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/group_fuzz_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(group_fuzz_smoke PROPERTIES TIMEOUT 120)
        add_test(NAME reload_fuzz_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/reload_fuzz_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(reload_fuzz_smoke PROPERTIES TIMEOUT 120)
        add_test(NAME tile_focus_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tile_focus_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(tile_focus_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME layout_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/layout_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(layout_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME scroll_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/scroll_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(scroll_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME scroll_follow_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/scroll_follow_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(scroll_follow_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME keyboard_resize_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/keyboard_resize_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(keyboard_resize_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME volume_keys_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/volume_keys_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(volume_keys_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME keymap_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/keymap_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(keymap_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME media_keys_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/media_keys_smoke.py
            $<TARGET_FILE:paw> ${EXAMPLE_CONFIG})
        set_tests_properties(media_keys_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME type_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/type_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(type_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME keyboard_layout_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/keyboard_layout_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(keyboard_layout_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME window_style_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_style_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(window_style_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME window_round_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_round_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(window_round_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME window_rules_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_rules_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(window_rules_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME output_workspace_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_workspace_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(output_workspace_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME output_follow_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_follow_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(output_follow_smoke PROPERTIES TIMEOUT 60)
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
        add_test(NAME animation_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/animation_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(animation_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME fullscreen_panel_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fullscreen_panel_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(fullscreen_panel_smoke PROPERTIES TIMEOUT 60)
        find_program(GRIM grim)
        # A missing grim is an empty argument, which the tests take as leaving out their
        # pixel checks (GRIM itself would be GRIM-NOTFOUND).
        set(GRIM_ARGUMENT "$<$<BOOL:${GRIM}>:${GRIM}>")
        add_test(NAME gradient_border_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/gradient_border_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(gradient_border_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME dim_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/dim_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(dim_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME idle_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/idle_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:idle_probe> $<TARGET_FILE:lock_probe>
            $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(idle_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME urgent_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/urgent_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(urgent_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME urgent_border_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/urgent_border_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(urgent_border_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME window_controls_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_controls_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>
            "${GRIM_ARGUMENT}")
        set_tests_properties(window_controls_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME window_shadow_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_shadow_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>
            "${GRIM_ARGUMENT}")
        set_tests_properties(window_shadow_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME peek_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/peek_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(peek_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME magnet_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/magnet_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>
            "${GRIM_ARGUMENT}")
        set_tests_properties(magnet_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME snap_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/snap_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>
            "${GRIM_ARGUMENT}")
        set_tests_properties(snap_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME snap_keys_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/snap_keys_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(snap_keys_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME snap_assist_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/snap_assist_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(snap_assist_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME night_light_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/night_light_smoke.py
            $<TARGET_FILE:paw>)
        set_tests_properties(night_light_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME hot_corners_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/hot_corners_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(hot_corners_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME zoom_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/zoom_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(zoom_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME zoom_output_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/zoom_output_smoke.py $<TARGET_FILE:paw>)
        set_tests_properties(zoom_output_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME example_snippets COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/example_snippets.py $<TARGET_FILE:paw>
            ${CMAKE_CURRENT_SOURCE_DIR}/config/init.lua)
        set_tests_properties(example_snippets PROPERTIES TIMEOUT 120)
        set(LOCK_XML ${PROTOCOL_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} client-header ${LOCK_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
            DEPENDS ${LOCK_XML})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} private-code ${LOCK_XML}
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c
            DEPENDS ${LOCK_XML})
        add_executable(lock_probe tests/lock_probe.c
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
        target_include_directories(lock_probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(lock_probe PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(lock_probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
        add_test(NAME session_lock COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/lock_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:lock_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(session_lock PROPERTIES TIMEOUT 60)
        add_test(NAME binding_modes_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/binding_modes_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:lock_probe>)
        set_tests_properties(binding_modes_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME locked_bindings_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/locked_bindings_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:lock_probe>)
        set_tests_properties(locked_bindings_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME shortcuts_inhibit_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/shortcuts_inhibit_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:input_probe>)
        set_tests_properties(shortcuts_inhibit_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME input_method_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/input_method_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:text_input_probe>
            $<TARGET_FILE:input_method_probe> $<TARGET_FILE:lock_probe>
            $<TARGET_FILE:input_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(input_method_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME window_capture_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_capture_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>
            $<TARGET_FILE:lock_probe>)
        set_tests_properties(window_capture_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME window_peek_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_peek_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>
            $<TARGET_FILE:lock_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(window_peek_smoke PROPERTIES TIMEOUT 60)
        if(SDBUS_FOUND)
            # A stand-in for logind on a private bus, so that no power test reaches the real one.
            add_executable(fake_login1 tests/fake_login1.c)
            target_compile_definitions(fake_login1 PRIVATE PAW_SDBUS_${PAW_SDBUS}=1)
            target_link_libraries(fake_login1 PRIVATE PkgConfig::SDBUS)
            target_compile_options(fake_login1 PRIVATE -Wall -Wextra -Wno-unused-parameter)
            add_test(NAME power_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/power_smoke.py
                $<TARGET_FILE:paw> $<TARGET_FILE:fake_login1> $<TARGET_FILE:wayland_probe>
                $<TARGET_FILE:lock_probe>)
            set_tests_properties(power_smoke PROPERTIES TIMEOUT 90)
            add_test(NAME idle_suspend_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/idle_suspend_smoke.py
                $<TARGET_FILE:paw> $<TARGET_FILE:fake_login1> $<TARGET_FILE:lock_probe>)
            set_tests_properties(idle_suspend_smoke PROPERTIES TIMEOUT 60)
            add_test(NAME lid_logind_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/lid_logind_smoke.py
                $<TARGET_FILE:paw> $<TARGET_FILE:fake_login1>)
            set_tests_properties(lid_logind_smoke PROPERTIES TIMEOUT 60)
        endif()
        # The compositor benchmark (tools/bench/bench.py); see docs/performance.md.
        add_executable(paw-bench-client tools/bench/bench_client.c
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
        target_include_directories(paw-bench-client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(paw-bench-client PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(paw-bench-client PRIVATE -Wall -Wextra -Wno-unused-parameter)
        add_test(NAME bench_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/bench/bench.py --quick
            --compositor $<TARGET_FILE:paw> --client $<TARGET_FILE:paw-bench-client>
            --pointer-probe $<TARGET_FILE:pointer_probe>)
        set_tests_properties(bench_smoke PROPERTIES TIMEOUT 120)
        add_test(NAME opacity_rules_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/opacity_rules_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:paw-bench-client>
            $<TARGET_FILE:wayland_probe>)
        set_tests_properties(opacity_rules_smoke PROPERTIES TIMEOUT 60)
        # XWayland coverage needs wlroots built with X support and an Xwayland binary.
        find_program(XWAYLAND_BINARY Xwayland)
        if(PAW_HAS_XWAYLAND AND XWAYLAND_BINARY)
            add_executable(x11_probe tests/x11_probe.c)
            target_link_libraries(x11_probe PRIVATE PkgConfig::XCB)
            target_compile_options(x11_probe PRIVATE -Wall -Wextra)
            add_test(NAME xwayland_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/xwayland_smoke.py
                $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe> $<TARGET_FILE:wayland_probe>
                ${EXAMPLE_CONFIG})
            set_tests_properties(xwayland_smoke PROPERTIES TIMEOUT 60)
            add_test(NAME swallow_x11_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/swallow_x11_smoke.py
                $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe> $<TARGET_FILE:wayland_probe>)
            set_tests_properties(swallow_x11_smoke PROPERTIES TIMEOUT 90)
            add_test(NAME xwayland_urgent_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/xwayland_urgent_smoke.py
                $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe> $<TARGET_FILE:wayland_probe>)
            set_tests_properties(xwayland_urgent_smoke PROPERTIES TIMEOUT 60)
            add_test(NAME xwayland_grab_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/xwayland_grab_smoke.py
                $<TARGET_FILE:paw> $<TARGET_FILE:x11_probe> $<TARGET_FILE:wayland_probe>)
            set_tests_properties(xwayland_grab_smoke PROPERTIES TIMEOUT 60)
        else()
            message(STATUS "XWayland test skipped: needs an Xwayland binary and wlroots X support")
        endif()
        # Windows' own icons; an X11 window's only where the X11 probe is built.
        add_test(NAME window_icon_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/window_icon_smoke.py
            $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>
            $<$<TARGET_EXISTS:x11_probe>:$<TARGET_FILE:x11_probe>>)
        set_tests_properties(window_icon_smoke PROPERTIES TIMEOUT 60)
    endif()
endif()
