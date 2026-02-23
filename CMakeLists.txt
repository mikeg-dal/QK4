cmake_minimum_required(VERSION 3.16)
project(QK4 VERSION 0.2.0 LANGUAGES CXX)

# Optional override paths for Opus when cross-compiling (primarily Android)
set(QK4_OPUS_INCLUDE_DIR "" CACHE PATH "Directory containing opus/opus.h for the target platform")
set(QK4_OPUS_LIBRARY "" CACHE FILEPATH "Path to libopus library for the target platform")

# Full version string can include prerelease info (e.g., "0.1.0-alpha.123")
# CI sets this via -DQK4_VERSION_FULL="x.y.z-prerelease"
# If not set, defaults to PROJECT_VERSION
if(NOT DEFINED QK4_VERSION_FULL)
    set(QK4_VERSION_FULL "${PROJECT_VERSION}")
endif()
message(STATUS "QK4 version: ${QK4_VERSION_FULL}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(ANDROID)
    # Packaging mode can differ from CMAKE_BUILD_TYPE:
    # - DEBUG: debug-signed APK for direct adb install/testing.
    # - RELEASE: requires explicit keystore signing for production distribution.
    set(QK4_ANDROID_DEPLOYMENT_TYPE "DEBUG" CACHE STRING
        "Android packaging mode: DEBUG (local testing) or RELEASE (production signed)")
    set_property(CACHE QK4_ANDROID_DEPLOYMENT_TYPE PROPERTY STRINGS DEBUG RELEASE)
    string(TOUPPER "${QK4_ANDROID_DEPLOYMENT_TYPE}" _qk4_android_deployment_type)
    if(NOT _qk4_android_deployment_type STREQUAL "DEBUG"
       AND NOT _qk4_android_deployment_type STREQUAL "RELEASE")
        message(FATAL_ERROR
            "QK4_ANDROID_DEPLOYMENT_TYPE must be DEBUG or RELEASE. "
            "Got '${QK4_ANDROID_DEPLOYMENT_TYPE}'.")
    endif()
    set(QT_ANDROID_DEPLOYMENT_TYPE "${_qk4_android_deployment_type}" CACHE STRING
        "Qt Android deployment type" FORCE)

    if(_qk4_android_deployment_type STREQUAL "RELEASE")
        # Qt's Android CMake integration passes only --sign; signing details are read from env vars.
        # Enforce required env vars at configure time so packaging never produces an unsigned release APK.
        set(QT_ANDROID_SIGN_APK ON CACHE BOOL "Sign Android APK during deployment" FORCE)
        set(_qk4_missing_signing_env "")
        foreach(_qk4_var
                QT_ANDROID_KEYSTORE_PATH
                QT_ANDROID_KEYSTORE_ALIAS
                QT_ANDROID_KEYSTORE_STORE_PASS
                QT_ANDROID_KEYSTORE_KEY_PASS)
            if(NOT DEFINED ENV{${_qk4_var}} OR "$ENV{${_qk4_var}}" STREQUAL "")
                list(APPEND _qk4_missing_signing_env "${_qk4_var}")
            endif()
        endforeach()
        if(_qk4_missing_signing_env)
            string(JOIN ", " _qk4_missing_signing_env_list ${_qk4_missing_signing_env})
            message(FATAL_ERROR
                "Android RELEASE packaging requires signing credentials. Missing environment variables: "
                "${_qk4_missing_signing_env_list}. "
                "For local device testing, configure with -DQK4_ANDROID_DEPLOYMENT_TYPE=DEBUG.")
        endif()
        unset(_qk4_missing_signing_env_list)
        unset(_qk4_missing_signing_env)
    endif()

    message(STATUS "QK4 Android deployment type: ${_qk4_android_deployment_type}")
    unset(_qk4_android_deployment_type)
endif()

# Core Qt components (GuiPrivate handled separately for cross-platform compatibility)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Multimedia ShaderTools Gui SerialPort Svg)

# GuiPrivate is needed for QRhi access but may not have CMake config on all platforms
if(APPLE)
    # macOS Homebrew Qt includes GuiPrivate CMake config
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    set(QT_GUI_PRIVATE_FOUND TRUE)
elseif(WIN32)
    # Windows aqtinstall binary packages may not include GuiPrivate CMake config
    # Try to find it, but fall back to manual include paths if not found
    find_package(Qt6 COMPONENTS GuiPrivate QUIET)
    if(Qt6GuiPrivate_FOUND)
        set(QT_GUI_PRIVATE_FOUND TRUE)
    else()
        message(STATUS "Qt6GuiPrivate CMake config not found, using manual include paths")
        set(QT_GUI_PRIVATE_FOUND FALSE)
    endif()
else()
    # Linux: Debian cross-compilation may not ship GuiPrivate CMake config
    find_package(Qt6 COMPONENTS GuiPrivate QUIET)
    if(Qt6GuiPrivate_FOUND)
        set(QT_GUI_PRIVATE_FOUND TRUE)
    else()
        message(STATUS "Qt6GuiPrivate CMake config not found, using manual include paths")
        set(QT_GUI_PRIVATE_FOUND FALSE)
    endif()
endif()

# Platform-specific dependency handling
if(ANDROID)
    # Android: HID/MIDI hardware features are disabled, Opus is still required for RX/TX audio.
    set(HIDAPI_INCLUDE_DIRS "")
    set(HIDAPI_LIBRARIES "")
    set(OPUS_INCLUDE_DIRS "")
    set(OPUS_LIBRARIES "")

    if(QK4_OPUS_INCLUDE_DIR AND QK4_OPUS_LIBRARY)
        # Accept both "<prefix>/include" and "<prefix>/include/opus" from callers.
        get_filename_component(_qk4_opus_inc_leaf "${QK4_OPUS_INCLUDE_DIR}" NAME)
        if(_qk4_opus_inc_leaf STREQUAL "opus" AND EXISTS "${QK4_OPUS_INCLUDE_DIR}/opus.h")
            get_filename_component(OPUS_INCLUDE_DIRS "${QK4_OPUS_INCLUDE_DIR}" DIRECTORY)
        else()
            set(OPUS_INCLUDE_DIRS "${QK4_OPUS_INCLUDE_DIR}")
        endif()
        unset(_qk4_opus_inc_leaf)
        set(OPUS_LIBRARIES "${QK4_OPUS_LIBRARY}")
    else()
        find_package(Opus CONFIG QUIET)
        if(TARGET Opus::opus)
            set(OPUS_LIBRARIES Opus::opus)
        else()
            find_path(OPUS_INCLUDE_DIRS NAMES opus/opus.h)
            find_library(OPUS_LIBRARIES NAMES opus)
        endif()
    endif()

    if(NOT TARGET Opus::opus AND (NOT OPUS_INCLUDE_DIRS OR NOT OPUS_LIBRARIES))
        message(FATAL_ERROR
            "Android build requires Opus. Provide -DQK4_OPUS_INCLUDE_DIR and -DQK4_OPUS_LIBRARY, "
            "or install an Opus package that exports Opus::opus.")
    endif()
elseif(APPLE)
    # macOS with Homebrew
    set(OPUS_PREFIX "/opt/homebrew/opt/opus")
    set(OPUS_INCLUDE_DIRS "${OPUS_PREFIX}/include")
    set(OPUS_LIBRARIES "${OPUS_PREFIX}/lib/libopus.dylib")

    set(HIDAPI_PREFIX "/opt/homebrew/opt/hidapi")
    set(HIDAPI_INCLUDE_DIRS "${HIDAPI_PREFIX}/include")
    set(HIDAPI_LIBRARIES "${HIDAPI_PREFIX}/lib/libhidapi.dylib")
elseif(WIN32)
    # Windows with vcpkg
    find_package(Opus CONFIG REQUIRED)
    find_package(hidapi CONFIG REQUIRED)
    set(OPUS_INCLUDE_DIRS "")
    set(HIDAPI_INCLUDE_DIRS "")
    set(OPUS_LIBRARIES Opus::opus)
    set(HIDAPI_LIBRARIES hidapi::hidapi)
else()
    # Linux with pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED opus)
    pkg_check_modules(HIDAPI REQUIRED hidapi-libusb)
endif()
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/network/tcpclient.cpp
    src/network/protocol.cpp
    src/network/kpa1500client.cpp
    src/network/catserver.cpp
    src/audio/audioengine.cpp
    src/audio/opusdecoder.cpp
    src/audio/opusencoder.cpp
    src/audio/sidetonegenerator.cpp
    src/dsp/panadapter_rhi.cpp
    src/dsp/minipan_rhi.cpp
    src/settings/radiosettings.cpp
    src/models/radiostate.cpp
    src/models/menumodel.cpp
    src/ui/radiomanagerdialog.cpp
    src/ui/dualcontrolbutton.cpp
    src/ui/sidecontrolpanel.cpp
    src/ui/bottommenubar.cpp
    src/ui/menuoverlay.cpp
    src/ui/micmeterwidget.cpp
    src/ui/vfowidget.cpp
    src/ui/frequencydisplaywidget.cpp
    src/ui/vforowwidget.cpp
    src/ui/filterindicatorwidget.cpp
    src/ui/bandpopupwidget.cpp
    src/ui/buttonrowpopup.cpp
    src/ui/displaypopupwidget.cpp
    src/ui/optionsdialog.cpp
    src/ui/rightsidepanel.cpp
    src/ui/featuremenubar.cpp
    src/ui/modepopupwidget.cpp
    src/ui/txmeterwidget.cpp
    src/ui/fnpopupwidget.cpp
    src/ui/rxeqpopupwidget.cpp
    src/ui/antennacfgpopup.cpp
    src/ui/lineoutpopup.cpp
    src/ui/lineinpopup.cpp
    src/ui/micinputpopup.cpp
    src/ui/micconfigpopup.cpp
    src/ui/voxpopup.cpp
    src/ui/ssbbwpopup.cpp
    src/ui/textdecodewindow.cpp
    src/ui/macrodialog.cpp
    src/ui/notificationwidget.cpp
    src/ui/k4styles.cpp
    src/ui/k4popupbase.cpp
    src/ui/kpa1500panel.cpp
    src/ui/kpa1500window.cpp
    src/ui/sidecontroloverlay.cpp
    src/ui/monoverlay.cpp
    src/ui/baloverlay.cpp
    src/ui/wheelaccumulator.cpp
)

if(ANDROID)
    # Keep class availability on Android via no-op implementations.
    list(APPEND SOURCES
        src/hardware/kpoddevice.cpp
        src/hardware/halikeydevice.cpp
    )
else()
    list(APPEND SOURCES
        src/hardware/kpoddevice.cpp
        src/hardware/halikeydevice.cpp
        src/hardware/halikeyworkerbase.cpp
        src/hardware/halikeyv14worker.cpp
        src/hardware/halikeymidiworker.cpp
        third_party/rtmidi/RtMidi.cpp
    )
endif()

set(HEADERS
    src/mainwindow.h
    src/network/tcpclient.h
    src/network/protocol.h
    src/network/kpa1500client.h
    src/network/catserver.h
    src/audio/audioengine.h
    src/audio/opusdecoder.h
    src/audio/opusencoder.h
    src/audio/sidetonegenerator.h
    src/dsp/panadapter_rhi.h
    src/dsp/minipan_rhi.h
    src/settings/radiosettings.h
    src/models/radiostate.h
    src/models/menumodel.h
    src/ui/radiomanagerdialog.h
    src/ui/dualcontrolbutton.h
    src/ui/sidecontrolpanel.h
    src/ui/bottommenubar.h
    src/ui/menuoverlay.h
    src/ui/micmeterwidget.h
    src/ui/vfowidget.h
    src/ui/frequencydisplaywidget.h
    src/ui/vforowwidget.h
    src/ui/filterindicatorwidget.h
    src/ui/bandpopupwidget.h
    src/ui/buttonrowpopup.h
    src/ui/displaypopupwidget.h
    src/ui/optionsdialog.h
    src/ui/rightsidepanel.h
    src/ui/featuremenubar.h
    src/ui/modepopupwidget.h
    src/ui/txmeterwidget.h
    src/ui/fnpopupwidget.h
    src/ui/rxeqpopupwidget.h
    src/ui/antennacfgpopup.h
    src/ui/lineoutpopup.h
    src/ui/lineinpopup.h
    src/ui/micinputpopup.h
    src/ui/micconfigpopup.h
    src/ui/voxpopup.h
    src/ui/ssbbwpopup.h
    src/ui/textdecodewindow.h
    src/ui/macrodialog.h
    src/ui/notificationwidget.h
    src/ui/k4styles.h
    src/ui/k4popupbase.h
    src/ui/kpa1500panel.h
    src/ui/kpa1500window.h
    src/ui/sidecontroloverlay.h
    src/ui/monoverlay.h
    src/ui/baloverlay.h
    src/ui/wheelaccumulator.h
)

if(ANDROID)
    list(APPEND HEADERS
        src/hardware/kpoddevice.h
        src/hardware/halikeydevice.h
    )
else()
    list(APPEND HEADERS
        src/hardware/kpoddevice.h
        src/hardware/halikeydevice.h
        src/hardware/halikeyworkerbase.h
        src/hardware/halikeyv14worker.h
        src/hardware/halikeymidiworker.h
    )
endif()

# On Android, create Qt executable package target
if(ANDROID)
    qt_add_executable(${PROJECT_NAME} MANUAL_FINALIZATION ${SOURCES} ${HEADERS})
# On macOS, create an app bundle
elseif(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME})
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${QK4_VERSION_FULL})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${QK4_VERSION_FULL})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.ai5qk.qk4")
    set(MACOSX_BUNDLE_ICON_FILE QK4.icns)

    # Set the icon file as a resource
    set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/resources/QK4.icns")
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${APP_ICON_MACOS})

    # Set the app bundle output directory
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in"
    )
elseif(WIN32)
    # Configure Windows version resource file
    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/QK4.rc.in
        ${CMAKE_BINARY_DIR}/QK4.rc
        @ONLY
    )
    # WIN32 flag creates a GUI app without console window
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${CMAKE_BINARY_DIR}/QK4.rc)
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtmidi
    ${OPUS_INCLUDE_DIRS}
    ${HIDAPI_INCLUDE_DIRS}
)

# Pass version to source code as compile definition
target_compile_definitions(${PROJECT_NAME} PRIVATE
    QK4_VERSION="${QK4_VERSION_FULL}"
)

# Add Qt private headers for QRhi access
get_target_property(QT_GUI_INCLUDE_DIRS Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
foreach(dir ${QT_GUI_INCLUDE_DIRS})
    target_include_directories(${PROJECT_NAME} PRIVATE "${dir}/${Qt6_VERSION}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${dir}/${Qt6_VERSION}/QtGui")
endforeach()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::Gui
    Qt6::Svg
    Qt6::SerialPort
    ${OPUS_LIBRARIES}
    ${HIDAPI_LIBRARIES}
)

# Link GuiPrivate if found via CMake, otherwise private headers are included manually
if(QT_GUI_PRIVATE_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
endif()

# macOS frameworks for KPOD hotplug detection (IOKit)
if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# RtMidi platform-specific MIDI backend
if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE __MACOSX_CORE__)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework CoreMIDI"
        "-framework CoreAudio"
    )
elseif(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE __WINDOWS_MM__)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
elseif(ANDROID)
    # No RtMidi backend on Android (HaliKey/KPOD features disabled).
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE __LINUX_ALSA__)
    target_link_libraries(${PROJECT_NAME} PRIVATE asound)
endif()

# Compile shaders for QRhi (Metal/DirectX/Vulkan/OpenGL)
qt6_add_shaders(${PROJECT_NAME} "panadapter_shaders"
    BATCHABLE
    PRECOMPILE
    PREFIX "/shaders"
    FILES
        src/dsp/shaders/spectrum.vert
        src/dsp/shaders/spectrum.frag
        src/dsp/shaders/spectrum_blue.vert
        src/dsp/shaders/spectrum_blue_amp.frag
        src/dsp/shaders/waterfall.vert
        src/dsp/shaders/waterfall.frag
        src/dsp/shaders/overlay.vert
        src/dsp/shaders/overlay.frag
)

# Embed Inter font family for crisp HD rendering
qt_add_resources(${PROJECT_NAME} "fonts"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-Medium.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/Inter-Bold.ttf
)

# Embed UI icons (SVG)
qt_add_resources(${PROJECT_NAME} "icons"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/icons/globe.svg
        resources/icons/help.svg
)

if(ANDROID)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        QT_ANDROID_PACKAGE_NAME "com.ai5qk.qk4"
        QT_ANDROID_MIN_SDK_VERSION 26
        QT_ANDROID_TARGET_SDK_VERSION 34
    )
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY QT_ANDROID_PERMISSIONS
        "android.permission.INTERNET"
        "android.permission.RECORD_AUDIO"
    )
endif()

# =============================================================================
# Deployment / Bundling - Create self-contained distributable app
# =============================================================================

if(APPLE)
    # Find macdeployqt
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${Qt6_DIR}/../../../bin")

    # OpenSSL locations (check both Apple Silicon and Intel paths)
    set(OPENSSL_SEARCH_PATHS
        "/opt/homebrew/opt/openssl@3/lib"   # Apple Silicon
        "/usr/local/opt/openssl@3/lib"      # Intel Mac
        "/opt/homebrew/opt/openssl/lib"
        "/usr/local/opt/openssl/lib"
    )

    foreach(OPENSSL_PATH ${OPENSSL_SEARCH_PATHS})
        if(EXISTS "${OPENSSL_PATH}/libssl.3.dylib")
            set(OPENSSL_LIB_DIR ${OPENSSL_PATH})
            break()
        endif()
    endforeach()

    if(OPENSSL_LIB_DIR)
        message(STATUS "Found OpenSSL at: ${OPENSSL_LIB_DIR}")
    else()
        message(WARNING "OpenSSL not found - TLS/PSK will not be available in bundled app")
    endif()

    # Get the real (resolved) path for OpenSSL - Homebrew uses symlinks
    execute_process(
        COMMAND realpath "${OPENSSL_LIB_DIR}"
        OUTPUT_VARIABLE OPENSSL_REAL_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "OpenSSL real path: ${OPENSSL_REAL_DIR}")

    # Custom target to create distributable app bundle
    add_custom_target(deploy
        COMMAND ${MACDEPLOYQT_EXECUTABLE} "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" -always-overwrite
        # Copy OpenSSL libraries
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENSSL_LIB_DIR}/libssl.3.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENSSL_LIB_DIR}/libcrypto.3.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/"
        # Copy Opus library
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPUS_LIBRARIES}"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/"
        # Copy HIDAPI library
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HIDAPI_LIBRARIES}"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/"
        # Fix OpenSSL library paths - use real Cellar path for -change
        COMMAND install_name_tool -id "@executable_path/../Frameworks/libssl.3.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/libssl.3.dylib"
        COMMAND install_name_tool -id "@executable_path/../Frameworks/libcrypto.3.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/libcrypto.3.dylib"
        COMMAND install_name_tool -change "${OPENSSL_REAL_DIR}/libcrypto.3.dylib"
            "@executable_path/../Frameworks/libcrypto.3.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/libssl.3.dylib"
        # Fix Opus library path
        COMMAND install_name_tool -id "@executable_path/../Frameworks/libopus.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/libopus.dylib" || true
        COMMAND install_name_tool -id "@executable_path/../Frameworks/libopus.0.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/libopus.0.dylib" || true
        # Fix HIDAPI library path
        COMMAND install_name_tool -id "@executable_path/../Frameworks/libhidapi.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/libhidapi.dylib" || true
        # Re-sign the app bundle (required after modifying libraries)
        # Uses ad-hoc signing by default; set CODESIGN_IDENTITY for Developer ID
        COMMAND codesign --force --deep --sign "$ENV{CODESIGN_IDENTITY}"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app" ||
            codesign --force --deep --sign - "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app"
        COMMENT "Creating deployable app bundle with all dependencies..."
        DEPENDS ${PROJECT_NAME}
    )

elseif(WIN32)
    # Find windeployqt
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")

    # Custom target to create distributable package
    add_custom_target(deploy
        COMMAND ${WINDEPLOYQT_EXECUTABLE} --release --no-translations
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Deploying Qt dependencies for Windows..."
        DEPENDS ${PROJECT_NAME}
    )
endif()

if(ANDROID)
    qt_finalize_executable(${PROJECT_NAME})
endif()
