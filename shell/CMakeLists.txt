find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Network Qml Quick)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)
pkg_check_modules(SHELL_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
# PulseAudio's client library (PipeWire serves it too) drives the panel's volume control.
option(PAW_PULSEAUDIO "Control sound from the panel through libpulse" ON)
if(PAW_PULSEAUDIO)
    pkg_check_modules(PULSE IMPORTED_TARGET libpulse)
    if(NOT PULSE_FOUND)
        message(STATUS "libpulse not found: the panel's volume control is left out")
    endif()
endif()
# The notification daemon and the system tray speak D-Bus; without QtDBus the shell builds
# without them and the panel leaves out the bell and the tray.
option(PAW_NOTIFICATIONS "Serve org.freedesktop.Notifications from the shell (needs Qt6 DBus)" ON)
option(PAW_TRAY "Show applications' status icons (StatusNotifierItem) in the panel (needs Qt6 DBus)" ON)
# Quick Settings' services on D-Bus; without QtDBus they are left out and their widgets never show.
option(PAW_MEDIA "Control media players (MPRIS) from Quick Settings and the media keys (needs Qt6 DBus)" ON)
option(PAW_POWER_PROFILES "Switch power-profiles-daemon's power mode from Quick Settings (needs Qt6 DBus)" ON)
option(PAW_NETWORKMANAGER "Manage Wi-Fi through NetworkManager from Quick Settings and the network widget (needs Qt6 DBus)" ON)
option(PAW_BLUETOOTH "Manage Bluetooth through BlueZ from Quick Settings (needs Qt6 DBus)" ON)
set(PAW_DBUS OFF)
set(PAW_TRAY_DBUS OFF)
set(PAW_MEDIA_DBUS OFF)
set(PAW_POWER_PROFILES_DBUS OFF)
set(PAW_NETWORKMANAGER_DBUS OFF)
set(PAW_BLUETOOTH_DBUS OFF)
if(PAW_NOTIFICATIONS OR PAW_TRAY OR PAW_MEDIA OR PAW_POWER_PROFILES OR PAW_NETWORKMANAGER
   OR PAW_BLUETOOTH)
    find_package(Qt6 6.9 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        set(PAW_DBUS ${PAW_NOTIFICATIONS})
        set(PAW_TRAY_DBUS ${PAW_TRAY})
        set(PAW_MEDIA_DBUS ${PAW_MEDIA})
        set(PAW_POWER_PROFILES_DBUS ${PAW_POWER_PROFILES})
        set(PAW_NETWORKMANAGER_DBUS ${PAW_NETWORKMANAGER})
        set(PAW_BLUETOOTH_DBUS ${PAW_BLUETOOTH})
    else()
        message(STATUS "Qt6 DBus not found: the notification daemon, the tray and Quick Settings' D-Bus services are left out")
    endif()
endif()
# The polkit authentication agent asks for the password pkexec, GParted or an updater needs; it
# needs libpolkit-agent-1, and without it the shell builds without the agent.
option(PAW_POLKIT "Be the session's polkit authentication agent (needs polkit-agent-1)" ON)
set(PAW_POLKIT_AGENT OFF)
if(PAW_POLKIT)
    pkg_check_modules(POLKIT_AGENT QUIET IMPORTED_TARGET polkit-agent-1)
    if(POLKIT_AGENT_FOUND)
        set(PAW_POLKIT_AGENT ON)
    else()
        message(STATUS "polkit-agent-1 not found: the polkit authentication agent is left out")
    endif()
endif()
if(NOT PAW_SHELL_PREVIEW_ONLY)
    find_package(LayerShellQt 6.6 QUIET)
    if(NOT LayerShellQt_FOUND)
        message(FATAL_ERROR "The desktop shell requires LayerShellQt 6.6+. Install layer-shell-qt, or use -DPAW_BUILD_SHELL=OFF for the compositor or -DPAW_SHELL_PREVIEW_ONLY=ON for UI development.")
    endif()
endif()

# The start menu's model (its pins, the launch history and its search), in everything that builds
# the controller, and the calculator, the files and the web search its search and the palette's
# share.
set(PAW_START_SOURCES start_menu.cpp start_menu.hpp launch_history.cpp launch_history.hpp
    calculator.cpp calculator.hpp file_index.cpp file_index.hpp gio_launch.hpp web_search.cpp
    web_search.hpp state_files.hpp)
# The authentication dialog's model, which the controller holds, in everything that builds it.
set(PAW_AUTH_SOURCES authentication.cpp authentication.hpp)
# The models of the services Quick Settings controls over D-Bus, in everything that builds the
# controller; their D-Bus backends only in the shell.
set(PAW_SERVICE_SOURCES media.cpp media.hpp power_mode.cpp power_mode.hpp wifi.cpp wifi.hpp
    row_model.hpp bluetooth.cpp bluetooth.hpp)
# The clipboard history, its pictures, the emoji picker, and the overlay their popups show in, in
# everything that builds the controller.
set(PAW_PICKER_SOURCES clipboard.cpp clipboard.hpp emoji.cpp emoji.hpp
    picker_view.cpp picker_view.hpp ${PROJECT_BINARY_DIR}/ext-data-control-v1-protocol.c
    ${PROJECT_BINARY_DIR}/ext-data-control-v1-client-protocol.h)
set(PAW_NOTIFY_SOURCES notifications.cpp notifications.hpp osd.cpp osd.hpp backlight.cpp
    backlight.hpp netlink.hpp dbus_util.hpp tray.cpp tray.hpp)
# What the compositor passes on from the volume, microphone and brightness keys, which feeds the
# on-screen display too.
list(APPEND PAW_NOTIFY_SOURCES volume_keys.cpp volume_keys.hpp)
# The display mode popup's model, which the controller holds too, and the display settings
# window's.
list(APPEND PAW_NOTIFY_SOURCES display_modes.cpp display_modes.hpp display_settings.cpp
    display_settings.hpp display_layout.cpp display_layout.hpp)
# The QML is compiled ahead of time into the binary (qmlcachegen), so starting the shell parses
# and compiles none of it. It lives at qrc:/shell/PawShell/*.qml.
qt_add_library(paw_shell_qml STATIC)
# Theme is the design tokens every QML file reads, one instance per engine.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The menus that use WindowMenu.js import it by its path, so the module leaves it out of its qmldir,
# which would evaluate it for every file.
set_source_files_properties(WindowMenu.js PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)
qt_add_qml_module(paw_shell_qml URI PawShell VERSION 1.0 RESOURCE_PREFIX /shell
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/PawShell
    NO_PLUGIN NO_GENERATE_QMLTYPES
    QML_FILES Theme.qml EmptyState.qml FlatButton.qml ButtonFill.qml PushButton.qml TextButton.qml CloseButton.qml SearchInput.qml PopupCard.qml Reveal.qml PopupMenu.qml MenuRow.qml Panel.qml Taskbar.qml Icon.qml FadingIcon.qml Desktop.qml Switcher.qml SwitcherCards.qml Palette.qml Overview.qml NotificationCards.qml Osd.qml ConfigError.qml NotificationBell.qml NotificationHistory.qml
    KeyboardLayout.qml BarTip.qml BarAppIcon.qml Badge.qml TaskButton.qml TaskList.qml TrayButton.qml
    WorkspaceIndicator.qml VolumeButton.qml ClockButton.qml BatteryWidget.qml NetworkWidget.qml SpeakerIcon.qml AudioSlider.qml MuteButton.qml StreamRow.qml
    AudioMixer.qml
    CalendarPopup.qml
    AudioOutputs.qml
    ProfileList.qml
    WallpaperPicker.qml
    Launcher.qml
    Launchpad.qml
    DrawnWallpaper.qml
    StartHome.qml
    StartAllApps.qml
    StartSearch.qml
    StartBestMatch.qml
    StartTile.qml
    StartRow.qml
    UserAvatar.qml
    PowerMenu.qml
    TaskbarMenu.qml
    WindowMenu.js
    TrayMenu.qml
    GroupList.qml
    WindowThumbnails.qml
    BarKeyboard.qml
    FocusRing.qml

    PowerDialog.qml
    AuthDialog.qml
    ClockFlyout.qml
    BatteryIcon.qml
    QuickSettingsButton.qml
    WallpapersButton.qml
    ProfilesButton.qml
    TilingButton.qml
    Dock.qml
    DockIcon.qml
    TopMenuBar.qml
    MenuBarMenu.qml
    QuickSettings.qml
    QuickTile.qml
    BindingMode.qml
    MediaCard.qml
    WifiList.qml
    WifiPopup.qml
    BluetoothList.qml
    DisplayMode.qml
    ClipboardPicker.qml
    EmojiPicker.qml
    DisplaySettings.qml)
# The emoji picker's table (vendor/emoji), compiled in as :/paw/emoji.tsv.
qt_add_resources(paw_shell_qml "emoji_table" PREFIX "/paw"
    BASE ${PROJECT_SOURCE_DIR}/vendor/emoji FILES ${PROJECT_SOURCE_DIR}/vendor/emoji/emoji.tsv)
# The protocols task_model.cpp and window_pictures.cpp speak, generated in the top-level build
# directory.
set(PAW_TASK_PROTOCOL_SOURCES
    ${PROJECT_BINARY_DIR}/paw-window-control-v1-protocol.c ${PROJECT_BINARY_DIR}/paw-window-control-v1-client-protocol.h
    ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
    ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
    ${PROJECT_BINARY_DIR}/ext-image-copy-capture-v1-protocol.c
    ${PROJECT_BINARY_DIR}/ext-image-copy-capture-v1-client-protocol.h
    ${CAPTURE_SOURCE_PROTOCOL_SOURCES})
# The controller, its models and the views: the shell's, and those of the tests that drive its QML,
# which build them once (paw_shell_test_core).
set(PAW_CONTROLLER_SOURCES ${PAW_NOTIFY_SOURCES} ${PAW_START_SOURCES} ${PAW_AUTH_SOURCES}
    ${PAW_SERVICE_SOURCES} ${PAW_PICKER_SOURCES} ${PAW_TASK_PROTOCOL_SOURCES}
    controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp
    task_model.cpp task_model.hpp window_pictures.cpp window_pictures.hpp task_filter.cpp
    task_filter.hpp taskbar_model.cpp taskbar_model.hpp icons.cpp icons.hpp app_match.cpp
    app_match.hpp wallpapers.cpp wallpapers.hpp image_provider.hpp audio.cpp audio.hpp
    system_status.cpp system_status.hpp power.cpp power.hpp)
add_executable(paw-shell main.cpp preview.cpp preview.hpp ${PAW_CONTROLLER_SOURCES})
add_dependencies(paw-shell paw_wlr_client_protocols paw_version)
set_target_properties(paw-shell PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_include_directories(paw-shell PRIVATE ${PROJECT_BINARY_DIR})
target_link_libraries(paw-shell PRIVATE paw_config Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick
    PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
target_compile_definitions(paw-shell PRIVATE QT_NO_KEYWORDS)
if(PAW_DBUS)
    target_sources(paw-shell PRIVATE notification_service.cpp notification_service.hpp)
    target_link_libraries(paw-shell PRIVATE Qt6::DBus)
    target_compile_definitions(paw-shell PRIVATE PAW_DBUS=1)
    # XDG autostart then leaves another notification daemon out.
    if(TARGET paw)
        target_compile_definitions(paw PRIVATE PAW_SHELL_NOTIFICATIONS=1)
    endif()
endif()
if(PAW_TRAY_DBUS)
    target_sources(paw-shell PRIVATE tray_host.cpp tray_host.hpp tray_watcher.cpp tray_watcher.hpp)
    target_link_libraries(paw-shell PRIVATE Qt6::DBus)
    target_compile_definitions(paw-shell PRIVATE PAW_TRAY=1)
    # XDG autostart then leaves another StatusNotifierWatcher out.
    if(TARGET paw)
        target_compile_definitions(paw PRIVATE PAW_SHELL_TRAY=1)
    endif()
endif()
if(PAW_POLKIT_AGENT)
    target_sources(paw-shell PRIVATE polkit_agent.cpp polkit_agent.hpp)
    target_link_libraries(paw-shell PRIVATE PkgConfig::POLKIT_AGENT)
    target_compile_definitions(paw-shell PRIVATE PAW_POLKIT=1)
    # XDG autostart then leaves another polkit agent out.
    if(TARGET paw)
        target_compile_definitions(paw PRIVATE PAW_SHELL_POLKIT=1)
    endif()
endif()
if(PAW_MEDIA_DBUS)
    target_sources(paw-shell PRIVATE mpris.cpp mpris.hpp)
    target_link_libraries(paw-shell PRIVATE Qt6::DBus)
    target_compile_definitions(paw-shell PRIVATE PAW_MEDIA=1)
endif()
if(PAW_POWER_PROFILES_DBUS)
    target_sources(paw-shell PRIVATE power_profiles_daemon.cpp power_profiles_daemon.hpp)
    target_link_libraries(paw-shell PRIVATE Qt6::DBus)
    target_compile_definitions(paw-shell PRIVATE PAW_POWER_PROFILES=1)
endif()
if(PAW_NETWORKMANAGER_DBUS)
    target_sources(paw-shell PRIVATE network_manager.cpp network_manager.hpp)
    target_link_libraries(paw-shell PRIVATE Qt6::DBus)
    target_compile_definitions(paw-shell PRIVATE PAW_NETWORKMANAGER=1)
endif()
if(PAW_BLUETOOTH_DBUS)
    target_sources(paw-shell PRIVATE bluez.cpp bluez.hpp)
    target_link_libraries(paw-shell PRIVATE Qt6::DBus)
    target_compile_definitions(paw-shell PRIVATE PAW_BLUETOOTH=1)
endif()
if(PAW_PULSEAUDIO AND PULSE_FOUND)
    target_sources(paw-shell PRIVATE pulse_audio.cpp)
    target_link_libraries(paw-shell PRIVATE PkgConfig::PULSE)
    target_compile_definitions(paw-shell PRIVATE PAW_PULSE=1)
endif()
target_compile_options(paw-shell PRIVATE -Wall -Wextra -Wpedantic)
if(PAW_SHELL_PREVIEW_ONLY)
    target_compile_definitions(paw-shell PRIVATE PAW_LAYER_SHELL=0)
else()
    target_link_libraries(paw-shell PRIVATE LayerShellQt::Interface)
    target_compile_definitions(paw-shell PRIVATE PAW_LAYER_SHELL=1)
    install(TARGETS paw-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
target_link_libraries(paw-shell PRIVATE paw_shell_qml)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
    # A test program of the shell's: tests/NAME.cpp and SOURCES, with Qt's keywords off, the
    # warnings on, shell/ and the build directory to include from, DEFINES, and linked to LIBS.
    function(paw_shell_test name)
        cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;DEFINES;LIBS")
        add_executable(${name} ${PROJECT_SOURCE_DIR}/tests/${name}.cpp ${ARG_SOURCES})
        set_target_properties(${name} PROPERTIES AUTOMOC ON)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
        target_compile_definitions(${name} PRIVATE QT_NO_KEYWORDS ${ARG_DEFINES})
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(${name} PRIVATE ${ARG_LIBS})
    endfunction()
    # The controller and the views, without layer shell and the D-Bus services, built once for the
    # tests that drive the shell's QML.
    add_library(paw_shell_test_core OBJECT ${PAW_CONTROLLER_SOURCES})
    add_dependencies(paw_shell_test_core paw_wlr_client_protocols)
    set_target_properties(paw_shell_test_core PROPERTIES AUTOMOC ON)
    target_include_directories(paw_shell_test_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(paw_shell_test_core PUBLIC QT_NO_KEYWORDS PAW_LAYER_SHELL=0)
    target_compile_options(paw_shell_test_core PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(paw_shell_test_core PUBLIC paw_config Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND paw_shell_qml)
    paw_shell_test(shell_ui_test LIBS paw_shell_test_core Qt6::Test)
    add_test(NAME shell_ui COMMAND shell_ui_test ${CMAKE_COMMAND})
    set_tests_properties(shell_ui PROPERTIES TIMEOUT 240
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    paw_shell_test(task_filter_test LIBS paw_shell_test_core Qt6::Test)
    add_test(NAME shell_task_filter COMMAND task_filter_test)
    set_tests_properties(shell_task_filter PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    paw_shell_test(palette_test SOURCES fuzzy.cpp fuzzy.hpp LIBS Qt6::Core Qt6::Test)
    add_test(NAME palette COMMAND palette_test)
    # The calculator of the palette's and the start menu's search.
    paw_shell_test(calculator_test SOURCES calculator.cpp calculator.hpp LIBS Qt6::Core Qt6::Test)
    add_test(NAME calculator COMMAND calculator_test)
    # The files the search finds, in a home folder of the test's own.
    paw_shell_test(file_index_test SOURCES file_index.cpp file_index.hpp fuzzy.cpp fuzzy.hpp
        LIBS Qt6::Core Qt6::Test PkgConfig::GIO)
    add_test(NAME file_index COMMAND file_index_test)
    set_tests_properties(file_index PROPERTIES TIMEOUT 60)
    # The search's last result, Search the web for “…”.
    paw_shell_test(web_search_test SOURCES web_search.cpp web_search.hpp LIBS Qt6::Core Qt6::Test)
    add_test(NAME web_search COMMAND web_search_test)
    # The emoji picker's table and model.
    paw_shell_test(emoji_test SOURCES emoji.cpp emoji.hpp
        DEFINES PAW_EMOJI_TABLE="${PROJECT_SOURCE_DIR}/vendor/emoji/emoji.tsv"
        LIBS Qt6::Core Qt6::Test)
    add_test(NAME emoji COMMAND emoji_test)
    # The display settings window's arrangement: snapping, settling, starting at 0, 0.
    paw_shell_test(display_layout_test SOURCES display_layout.cpp display_layout.hpp
        LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_display_layout COMMAND display_layout_test)
    # The display settings window's model, against a stand-in for the compositor.
    paw_shell_test(display_settings_test
        SOURCES display_settings.cpp display_settings.hpp display_layout.cpp display_layout.hpp
        LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_display_settings COMMAND display_settings_test)
    # The start menu's model: its pins, the launch history and its search.
    paw_shell_test(start_menu_test SOURCES ${PAW_START_SOURCES} fuzzy.cpp fuzzy.hpp
        LIBS Qt6::Core Qt6::Test PkgConfig::GIO)
    add_test(NAME start_menu COMMAND start_menu_test)
    # The authentication dialog's model, with stand-ins for polkit's helper.
    paw_shell_test(authentication_test SOURCES authentication.cpp authentication.hpp
        LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_authentication COMMAND authentication_test)
    if(PAW_POLKIT_AGENT)
        # The agent against a stand-in for polkitd on a private bus, the system bus the
        # environment names made unusable.
        add_executable(fake_polkitd ${PROJECT_SOURCE_DIR}/tests/fake_polkitd.c)
        target_compile_options(fake_polkitd PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_link_libraries(fake_polkitd PRIVATE PkgConfig::GIO)
        paw_shell_test(polkit_agent_test
            SOURCES ${PAW_AUTH_SOURCES} polkit_agent.cpp polkit_agent.hpp
            DEFINES FAKE_POLKITD="$<TARGET_FILE:fake_polkitd>"
            LIBS Qt6::Core Qt6::Test PkgConfig::POLKIT_AGENT)
        add_test(NAME shell_polkit_agent COMMAND polkit_agent_test)
        set_tests_properties(shell_polkit_agent PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    # Applications' icons: the desktop's icon theme, and which entry a window belongs to.
    paw_shell_test(app_icons_test SOURCES icons.cpp icons.hpp app_match.cpp app_match.hpp
        LIBS Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test PkgConfig::GIO)
    add_test(NAME app_icons COMMAND app_icons_test)
    paw_shell_test(task_model_test SOURCES task_model.cpp task_model.hpp window_pictures.cpp
        window_pictures.hpp ${PAW_TASK_PROTOCOL_SOURCES}
        LIBS Qt6::Core Qt6::Gui Qt6::Quick PkgConfig::SHELL_WAYLAND)
    paw_smoke(shell_tasks SCRIPT compositor_smoke.py $<TARGET_FILE:paw> $<TARGET_FILE:wayland_probe>
        ${EXAMPLE_CONFIG} $<TARGET_FILE:task_model_test>)
    # The clipboard history against a private compositor, with clipboard_probe copying.
    paw_shell_test(clipboard_test SOURCES clipboard.cpp clipboard.hpp
        ${PROJECT_BINARY_DIR}/ext-data-control-v1-protocol.c
        ${PROJECT_BINARY_DIR}/ext-data-control-v1-client-protocol.h
        LIBS Qt6::Gui Qt6::Quick Qt6::Test PkgConfig::SHELL_WAYLAND)
    paw_smoke(shell_clipboard SCRIPT clipboard_smoke.py TIMEOUT 120 $<TARGET_FILE:paw>
        $<TARGET_FILE:clipboard_probe> $<TARGET_FILE:clipboard_test>)
    # The window pictures' formats and sizes, without a compositor.
    paw_shell_test(window_pictures_test
        SOURCES window_pictures.cpp window_pictures.hpp ${PAW_TASK_PROTOCOL_SOURCES}
        LIBS Qt6::Gui Qt6::Test PkgConfig::SHELL_WAYLAND)
    add_test(NAME shell_window_pictures COMMAND window_pictures_test)
    paw_shell_test(notifications_test SOURCES notifications.cpp notifications.hpp
        LIBS paw_config Qt6::Gui Qt6::Test)
    add_test(NAME shell_notifications COMMAND notifications_test)
    set_tests_properties(shell_notifications PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(PAW_DBUS)
        paw_shell_test(notifications_dbus_test SOURCES notifications.cpp notifications.hpp
            notification_service.cpp notification_service.hpp
            LIBS paw_config Qt6::Gui Qt6::DBus Qt6::Test)
        # The test talks to a bus of its own; the environment's is made unusable so nothing can
        # reach the real one.
        add_executable(notify_probe ${PROJECT_SOURCE_DIR}/tests/notify_probe.cpp)
        set_target_properties(notify_probe PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        target_link_libraries(notify_probe PRIVATE Qt6::Core Qt6::DBus)
        add_test(NAME shell_notifications_dbus COMMAND notifications_dbus_test)
        set_tests_properties(shell_notifications_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=disabled:")
        # Setting the backlight through a stand-in for logind on a private bus too, with the
        # system bus made unusable as well.
        paw_shell_test(backlight_dbus_test SOURCES backlight.cpp backlight.hpp DEFINES PAW_DBUS=1
            LIBS Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_backlight_dbus COMMAND backlight_dbus_test)
        set_tests_properties(shell_backlight_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    if(PAW_TRAY_DBUS)
        # A tray item for the end-to-end tests; tests/tray_fake_item.hpp is its item and menu.
        add_executable(tray_probe ${PROJECT_SOURCE_DIR}/tests/tray_probe.cpp
            ${PROJECT_SOURCE_DIR}/tests/tray_fake_item.hpp)
        set_target_properties(tray_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        target_compile_definitions(tray_probe PRIVATE QT_NO_KEYWORDS)
        target_compile_options(tray_probe PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(tray_probe PRIVATE Qt6::Gui Qt6::DBus)
        # Like the notification test, on a bus of its own with the environment's made unusable.
        paw_shell_test(tray_dbus_test SOURCES ${PROJECT_SOURCE_DIR}/tests/tray_fake_item.hpp
            tray.cpp tray.hpp tray_host.cpp tray_host.hpp tray_watcher.cpp tray_watcher.hpp
            LIBS Qt6::Gui Qt6::DBus Qt6::Test)
        add_test(NAME shell_tray_dbus COMMAND tray_dbus_test)
        set_tests_properties(shell_tray_dbus PROPERTIES TIMEOUT 90
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=disabled:")
    endif()
    paw_shell_test(osd_test LIBS paw_shell_test_core Qt6::Test)
    add_test(NAME shell_osd COMMAND osd_test)
    set_tests_properties(shell_osd PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;DBUS_SESSION_BUS_ADDRESS=disabled:")
    # The authentication dialog in its view, with stand-ins for polkit's helper.
    paw_shell_test(auth_dialog_test LIBS paw_shell_test_core Qt6::Test)
    add_test(NAME shell_auth_dialog COMMAND auth_dialog_test)
    set_tests_properties(shell_auth_dialog PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;DBUS_SESSION_BUS_ADDRESS=disabled:")
    paw_shell_test(system_status_test SOURCES system_status.cpp system_status.hpp
        LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_system_status COMMAND system_status_test)
    set_tests_properties(shell_system_status PROPERTIES TIMEOUT 90)
    # The volume, microphone and brightness keys, on stand-ins for the sound server, a backlight
    # and wpctl.
    paw_shell_test(volume_keys_test SOURCES volume_keys.cpp volume_keys.hpp audio.cpp audio.hpp
        backlight.cpp backlight.hpp osd.cpp osd.hpp LIBS paw_config Qt6::Core Qt6::Test)
    add_test(NAME shell_volume_keys COMMAND volume_keys_test)
    set_tests_properties(shell_volume_keys PROPERTIES TIMEOUT 30)
    # The display mode popup's model.
    paw_shell_test(display_modes_test SOURCES display_modes.cpp display_modes.hpp
        LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_display_modes COMMAND display_modes_test)
    set_tests_properties(shell_display_modes PROPERTIES TIMEOUT 30)
    # Which window's process plays a stream of the sound server, on a made-up procfs and the real
    # one.
    paw_shell_test(window_sound_test SOURCES audio.cpp audio.hpp LIBS Qt6::Gui Qt6::Test)
    add_test(NAME shell_window_sound COMMAND window_sound_test)
    # The media players' model: their order, the current one, and what the controls ask of it.
    paw_shell_test(media_test SOURCES media.cpp media.hpp LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_media COMMAND media_test)
    # Wi-Fi's model: how networks are secured and listed, and what the controls ask for.
    paw_shell_test(wifi_test SOURCES wifi.cpp wifi.hpp LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_wifi COMMAND wifi_test)
    # Bluetooth's model: its lists of devices, what the controls ask for, and the answers to BlueZ.
    paw_shell_test(bluetooth_test SOURCES bluetooth.cpp bluetooth.hpp LIBS Qt6::Core Qt6::Test)
    add_test(NAME shell_bluetooth COMMAND bluetooth_test)
    if(PAW_MEDIA_DBUS)
        # The MPRIS backend against stand-in players on a bus of its own, the environment's made
        # unusable.
        paw_shell_test(media_dbus_test SOURCES ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp media.cpp
            media.hpp mpris.cpp mpris.hpp DEFINES PAW_MEDIA=1 LIBS Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_media_dbus COMMAND media_dbus_test)
        set_tests_properties(shell_media_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
        # A media player for the end-to-end test (media_smoke), as a program of its own.
        add_executable(mpris_probe ${PROJECT_SOURCE_DIR}/tests/mpris_probe.cpp
            ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp)
        set_target_properties(mpris_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        target_compile_definitions(mpris_probe PRIVATE QT_NO_KEYWORDS)
        target_compile_options(mpris_probe PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(mpris_probe PRIVATE Qt6::Core Qt6::DBus)
    endif()
    if(PAW_POWER_PROFILES_DBUS)
        # The power mode against a stand-in power-profiles-daemon on a bus of its own.
        paw_shell_test(power_mode_dbus_test SOURCES ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp
            power_mode.cpp power_mode.hpp power_profiles_daemon.cpp power_profiles_daemon.hpp
            DEFINES PAW_POWER_PROFILES=1 LIBS Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_power_mode_dbus COMMAND power_mode_dbus_test)
        set_tests_properties(shell_power_mode_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    if(PAW_NETWORKMANAGER_DBUS)
        # Wi-Fi against a stand-in NetworkManager on a bus of its own.
        paw_shell_test(network_manager_dbus_test SOURCES ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp
            wifi.cpp wifi.hpp network_manager.cpp network_manager.hpp DEFINES PAW_NETWORKMANAGER=1
            LIBS Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_network_manager_dbus COMMAND network_manager_dbus_test)
        set_tests_properties(shell_network_manager_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    if(PAW_BLUETOOTH_DBUS)
        # Bluetooth against a stand-in BlueZ on a bus of its own, which calls the shell's agent.
        paw_shell_test(bluez_dbus_test SOURCES ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp
            bluetooth.cpp bluetooth.hpp bluez.cpp bluez.hpp DEFINES PAW_BLUETOOTH=1
            LIBS Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_bluez_dbus COMMAND bluez_dbus_test)
        set_tests_properties(shell_bluez_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    paw_smoke(shell_preview TIMEOUT 90 $<TARGET_FILE:paw-shell> ${EXAMPLE_CONFIG})
    # Every popup of tools/shell_gallery.py renders without a QML warning.
    paw_smoke(shell_gallery TIMEOUT 120 ${PROJECT_SOURCE_DIR}/tools/shell_gallery.py
        ${PROJECT_BINARY_DIR} software)
    add_test(NAME shell_version COMMAND paw-shell --version)
    set_tests_properties(shell_version PROPERTIES PASS_REGULAR_EXPRESSION "^paw-shell ${PROJECT_VERSION}( \\(git [^)]+\\))?\n$")
    if(NOT PAW_SHELL_PREVIEW_ONLY)
        paw_smoke(shell_integration TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> ${EXAMPLE_CONFIG})
        paw_smoke(overview_shell_smoke $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe>)
        paw_smoke(snap_assist_shell_smoke $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        paw_smoke(overlay_exit_smoke $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe>)
        paw_smoke(clipboard_shell_smoke $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:clipboard_probe>)
        paw_smoke(emoji_shell_smoke $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe>)
        # A few of the gallery's popups and overlays through the GPU (Mesa's software OpenGL, in a
        # private compositor), where effects are drawn. Not under the sanitizers: now and then
        # Qt's Wayland platform leaks a surface of a preview that quits soon after showing.
        if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize")
            paw_smoke(shell_gallery_gpu SCRIPT shell_gallery.py TIMEOUT 120
                ${PROJECT_SOURCE_DIR}/tools/shell_gallery.py ${PROJECT_BINARY_DIR} gpu bar launcher
                tray-menu notifications osd-volume cards switcher palette power-dialog)
        endif()
        find_program(GRIM grim)
        # A missing grim is an empty argument, which the tests take as leaving out their
        # pixel checks (GRIM itself would be GRIM-NOTFOUND).
        set(GRIM_ARGUMENT "$<$<BOOL:${GRIM}>:${GRIM}>")
        paw_smoke(urgent_shell_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        paw_smoke(popover_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        paw_smoke(touch_shell_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>)
        paw_smoke(thumbnails_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe> $<TARGET_FILE:pointer_probe>
            "${GRIM_ARGUMENT}")
        paw_smoke(taskbar_drag_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe> $<TARGET_FILE:pointer_probe>)
        paw_smoke(taskbar_keyboard_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        paw_smoke(switcher_pictures_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe> "${GRIM_ARGUMENT}")
        paw_smoke(macos_shell_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        paw_smoke(volume_keys_shell_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>)
        paw_smoke(display_mode_shell_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
            $<TARGET_FILE:pointer_probe>)
        paw_smoke(display_settings_shell_smoke TIMEOUT 90 $<TARGET_FILE:paw>
            $<TARGET_FILE:paw-shell> $<TARGET_FILE:pointer_probe>)
        if(PAW_DBUS)
            paw_smoke(notifications_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
                $<TARGET_FILE:notify_probe> $<TARGET_FILE:pointer_probe>)
        endif()
        if(PAW_MEDIA_DBUS)
            paw_smoke(media_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
                $<TARGET_FILE:mpris_probe>)
        endif()
        if(PAW_TRAY_DBUS)
            paw_smoke(tray_smoke TIMEOUT 90 $<TARGET_FILE:paw> $<TARGET_FILE:paw-shell>
                $<TARGET_FILE:tray_probe> $<TARGET_FILE:pointer_probe>)
        endif()
    endif()
endif()
