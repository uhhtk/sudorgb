cmake_minimum_required(VERSION 3.21)
project(orkc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)  # qmldir per QML subdirectory (components/, pages/)

include(GNUInstallDirs)
set(ORKC_SERVICE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/orkc/service")

qt_add_executable(orkc
    src/main.cpp
    src/core/JsonStore.h src/core/JsonStore.cpp
    src/core/Settings.h src/core/Settings.cpp
    src/openrgb/OrgbProtocol.h src/openrgb/OrgbProtocol.cpp
    src/openrgb/OpenRgbClient.h src/openrgb/OpenRgbClient.cpp
    src/openrgb/RgbDeviceModel.h src/openrgb/RgbDeviceModel.cpp
    src/openrgb/RgbService.h src/openrgb/RgbService.cpp
    src/kraken/KrakenService.h src/kraken/KrakenService.cpp
    src/system/SystemMonitor.h src/system/SystemMonitor.cpp
    src/profiles/ProfileManager.h src/profiles/ProfileManager.cpp
    src/profiles/Presets.h src/profiles/Presets.cpp
    src/devices/NativeDevices.h src/devices/NativeDevices.cpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(orkc
    URI Orkc
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/Card.qml
        qml/components/NavTab.qml
        qml/components/Meter.qml
        qml/components/Readout.qml
        qml/components/CoolerView.qml
        qml/components/StatusPill.qml
        qml/components/ColorWheel.qml
        qml/components/AccentSlider.qml
        qml/components/PrimaryButton.qml
        qml/components/GhostButton.qml
        qml/components/Toggle.qml
        qml/components/Segmented.qml
        qml/components/LcdPreview.qml
        qml/components/CurveEditor.qml
        qml/components/Swatch.qml
        qml/components/SectionHeader.qml
        qml/components/Icon.qml
        qml/components/Toast.qml
        qml/components/ConflictBanner.qml
        qml/components/Sparkline.qml
        qml/components/FieldInput.qml
        qml/components/DropdownField.qml
        qml/components/ConfirmDialog.qml
        qml/pages/DashboardPage.qml
        qml/pages/LightingPage.qml
        qml/pages/LcdPage.qml
        qml/pages/CoolingPage.qml
        qml/pages/ProfilesPage.qml
        qml/pages/SettingsPage.qml
    RESOURCES
        resources/icons/sudorgb.png
)

target_include_directories(orkc PRIVATE src)
target_compile_definitions(orkc PRIVATE
    ORKC_VERSION="${PROJECT_VERSION}"
    ORKC_SOURCE_SERVICE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/service"
    ORKC_INSTALLED_SERVICE_DIR="${CMAKE_INSTALL_PREFIX}/${ORKC_SERVICE_INSTALL_DIR}"
    QT_NO_KEYWORDS
)
target_compile_options(orkc PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(orkc PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)

install(TARGETS orkc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES service/kraken_service.py service/backend.py service/conflicts.py service/media.py
        DESTINATION ${ORKC_SERVICE_INSTALL_DIR})
install(FILES service/kraken_driver/__init__.py service/kraken_driver/device.py
              service/kraken_driver/lcd_render.py service/kraken_driver/LICENSE.OpenKraken
        DESTINATION ${ORKC_SERVICE_INSTALL_DIR}/kraken_driver)
install(FILES packaging/orkc.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/70-orkc-glorious.rules DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)
install(FILES resources/icons/sudorgb.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)

# --- tests -------------------------------------------------------------------
option(ORKC_BUILD_TESTS "Build unit tests" ON)
if(ORKC_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(orkc_tests
        tests/tst_orkc.cpp
        src/openrgb/OrgbProtocol.h src/openrgb/OrgbProtocol.cpp
        src/core/JsonStore.h src/core/JsonStore.cpp
        src/profiles/Presets.h src/profiles/Presets.cpp
        src/devices/NativeDevices.h src/devices/NativeDevices.cpp
    )
    target_include_directories(orkc_tests PRIVATE src)
    target_link_libraries(orkc_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME orkc_tests COMMAND orkc_tests)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    qt_add_executable(orkc_live_tests
        tests/tst_openrgb_live.cpp
        src/openrgb/OrgbProtocol.h src/openrgb/OrgbProtocol.cpp
        src/openrgb/OpenRgbClient.h src/openrgb/OpenRgbClient.cpp
    )
    target_include_directories(orkc_live_tests PRIVATE src)
    target_link_libraries(orkc_live_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
    add_test(NAME openrgb_live_tests COMMAND orkc_live_tests)
    qt_add_executable(orkc_kraken_ipc_tests
        tests/tst_kraken_ipc.cpp
        src/core/JsonStore.h src/core/JsonStore.cpp
        src/core/Settings.h src/core/Settings.cpp
        src/kraken/KrakenService.h src/kraken/KrakenService.cpp
    )
    target_include_directories(orkc_kraken_ipc_tests PRIVATE src)
    target_compile_definitions(orkc_kraken_ipc_tests PRIVATE
        ORKC_SOURCE_SERVICE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/service" ORKC_INSTALLED_SERVICE_DIR="" QT_NO_KEYWORDS)
    target_link_libraries(orkc_kraken_ipc_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME kraken_ipc_tests COMMAND orkc_kraken_ipc_tests)
    qt_add_executable(orkc_softfx_tests
        tests/tst_softfx_live.cpp
        src/core/JsonStore.h src/core/JsonStore.cpp
        src/core/Settings.h src/core/Settings.cpp
        src/openrgb/OrgbProtocol.h src/openrgb/OrgbProtocol.cpp
        src/openrgb/OpenRgbClient.h src/openrgb/OpenRgbClient.cpp
        src/openrgb/RgbDeviceModel.h src/openrgb/RgbDeviceModel.cpp
        src/openrgb/RgbService.h src/openrgb/RgbService.cpp
        src/devices/NativeDevices.h src/devices/NativeDevices.cpp
    )
    target_include_directories(orkc_softfx_tests PRIVATE src)
    target_compile_definitions(orkc_softfx_tests PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(orkc_softfx_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
    add_test(NAME softfx_live_tests COMMAND orkc_softfx_tests)  # skips unless ORKC_TEST_OPENRGB_PORT is set
    add_test(NAME kraken_service_tests
             COMMAND /usr/bin/python3 -I -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/service/tests)
endif()
