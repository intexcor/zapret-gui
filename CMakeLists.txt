cmake_minimum_required(VERSION 3.21)

project(zapret-gui VERSION 2.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Quick Network Concurrent)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 OLD)

# --- Source files ---
set(CORE_SOURCES
    src/core/ZapretEngine.h src/core/ZapretEngine.cpp
    src/core/StrategyManager.h src/core/StrategyManager.cpp
    src/core/HostlistManager.h src/core/HostlistManager.cpp
    src/core/ProcessManager.h src/core/ProcessManager.cpp
    src/core/ConfigManager.h src/core/ConfigManager.cpp
    src/core/UpdateChecker.h src/core/UpdateChecker.cpp
)

set(MODEL_SOURCES
    src/models/StrategyListModel.h src/models/StrategyListModel.cpp
    src/models/LogModel.h src/models/LogModel.cpp
)

set(PLATFORM_SOURCES
    src/platform/PlatformHelper.h
    src/platform/PlatformHelper.cpp
)

# Platform-specific sources
if(WIN32)
    list(APPEND PLATFORM_SOURCES src/platform/WindowsPlatform.h src/platform/WindowsPlatform.cpp)
elseif(APPLE AND NOT IOS)
    list(APPEND PLATFORM_SOURCES src/platform/MacOSPlatform.h src/platform/MacOSPlatform.cpp)
elseif(ANDROID)
    list(APPEND PLATFORM_SOURCES src/platform/AndroidPlatform.h src/platform/AndroidPlatform.cpp)
elseif(IOS)
    list(APPEND PLATFORM_SOURCES src/platform/IOSPlatform.h src/platform/IOSPlatform.cpp)
else()
    list(APPEND PLATFORM_SOURCES src/platform/LinuxPlatform.h src/platform/LinuxPlatform.cpp)
endif()

# --- Executable ---
qt_add_executable(zapret-gui
    src/main.cpp
    ${CORE_SOURCES}
    ${MODEL_SOURCES}
    ${PLATFORM_SOURCES}
)

# --- QML module ---

# Flatten all QML files into module root so they register as types
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/pages/HomePage.qml PROPERTIES QT_RESOURCE_ALIAS HomePage.qml)
set_source_files_properties(qml/pages/StrategiesPage.qml PROPERTIES QT_RESOURCE_ALIAS StrategiesPage.qml)
set_source_files_properties(qml/pages/ListsPage.qml PROPERTIES QT_RESOURCE_ALIAS ListsPage.qml)
set_source_files_properties(qml/pages/SettingsPage.qml PROPERTIES QT_RESOURCE_ALIAS SettingsPage.qml)
set_source_files_properties(qml/pages/LogPage.qml PROPERTIES QT_RESOURCE_ALIAS LogPage.qml)
set_source_files_properties(qml/pages/DiagnosticsPage.qml PROPERTIES QT_RESOURCE_ALIAS DiagnosticsPage.qml)
set_source_files_properties(qml/components/StatusIndicator.qml PROPERTIES QT_RESOURCE_ALIAS StatusIndicator.qml)
set_source_files_properties(qml/components/StrategyCard.qml PROPERTIES QT_RESOURCE_ALIAS StrategyCard.qml)
set_source_files_properties(qml/components/NavigationBar.qml PROPERTIES QT_RESOURCE_ALIAS NavigationBar.qml)

qt_add_qml_module(zapret-gui
    URI ZapretGui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/HomePage.qml
        qml/pages/StrategiesPage.qml
        qml/pages/ListsPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/LogPage.qml
        qml/pages/DiagnosticsPage.qml
        qml/components/StatusIndicator.qml
        qml/components/StrategyCard.qml
        qml/components/NavigationBar.qml
)

# --- Definitions ---
target_compile_definitions(zapret-gui PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    target_compile_definitions(zapret-gui PRIVATE PLATFORM_WINDOWS)
elseif(APPLE AND NOT IOS)
    target_compile_definitions(zapret-gui PRIVATE PLATFORM_MACOS)
elseif(ANDROID)
    target_compile_definitions(zapret-gui PRIVATE PLATFORM_ANDROID)
elseif(IOS)
    target_compile_definitions(zapret-gui PRIVATE PLATFORM_IOS)
else()
    target_compile_definitions(zapret-gui PRIVATE PLATFORM_LINUX)
endif()

# --- Link ---
target_link_libraries(zapret-gui PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Network
    Qt6::Concurrent
)

# --- Include dirs ---
target_include_directories(zapret-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# --- udp-bypass tool (macOS only) ---
if(APPLE AND NOT IOS)
    add_subdirectory(tools/udp-bypass)
endif()

# --- VPN packet processor JNI library (Android only) ---
if(ANDROID)
    add_library(vpn-processor SHARED
        src/dpi/dpi_bypass.h
        src/dpi/dpi_bypass.c
        platform/android/jni/vpn_processor.c
        platform/android/jni/tcp_relay.h
        platform/android/jni/tcp_relay.c
        platform/android/jni/udp_relay.h
        platform/android/jni/udp_relay.c
    )
    target_include_directories(vpn-processor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dpi
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/android/jni
    )
    target_link_libraries(vpn-processor PRIVATE log)
endif()

# --- Copy resources to build directory ---
add_custom_command(TARGET zapret-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/lists"
        "$<TARGET_FILE_DIR:zapret-gui>/../Resources/lists"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/fake"
        "$<TARGET_FILE_DIR:zapret-gui>/../Resources/fake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/bin"
        "$<TARGET_FILE_DIR:zapret-gui>/../Resources/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/lua"
        "$<TARGET_FILE_DIR:zapret-gui>/../Resources/lua"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/strategies.json"
        "$<TARGET_FILE_DIR:zapret-gui>/../Resources/strategies.json"
    COMMENT "Copying resources to app bundle"
)

# Copy udp-bypass binary to app bundle (macOS only)
if(APPLE AND NOT IOS)
    add_custom_command(TARGET zapret-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:udp-bypass>"
            "$<TARGET_FILE_DIR:zapret-gui>/../Resources/bin/macos/udp-bypass"
        COMMENT "Copying udp-bypass to app bundle"
    )
endif()

# --- Platform-specific settings ---
if(WIN32)
    set_target_properties(zapret-gui PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    # Embed UAC manifest
    if(MSVC)
        target_sources(zapret-gui PRIVATE platform/windows/zapret-gui.manifest)
    endif()
elseif(APPLE AND NOT IOS)
    set_target_properties(zapret-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "Zapret"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.zapretgui.app"
    )
elseif(ANDROID)
    set_property(TARGET zapret-gui PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/android"
    )
    set_property(TARGET zapret-gui PROPERTY QT_ANDROID_MIN_SDK_VERSION 26)
    set_property(TARGET zapret-gui PROPERTY QT_ANDROID_TARGET_SDK_VERSION 34)
elseif(IOS)
    set_target_properties(zapret-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.zapretgui.app"
    )
endif()

# --- Install ---
install(DIRECTORY lists/ DESTINATION share/zapret-gui/lists)
install(DIRECTORY fake/ DESTINATION share/zapret-gui/fake)
install(DIRECTORY resources/bin/ DESTINATION share/zapret-gui/bin)
install(FILES resources/strategies.json DESTINATION share/zapret-gui)

install(TARGETS zapret-gui
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
