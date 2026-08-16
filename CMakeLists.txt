cmake_minimum_required(VERSION 3.21)
project(QK4 VERSION 0.7.0 LANGUAGES CXX)

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

# Core Qt components (GuiPrivate handled separately for cross-platform compatibility)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Multimedia ShaderTools Gui SerialPort Svg Test)

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
if(APPLE)
    # macOS with Homebrew - resolve prefix dynamically to support both
    # Intel (/usr/local) and Apple Silicon (/opt/homebrew)
    execute_process(COMMAND brew --prefix opus OUTPUT_VARIABLE OPUS_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND brew --prefix hidapi OUTPUT_VARIABLE HIDAPI_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND brew --prefix libusb OUTPUT_VARIABLE LIBUSB_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(OPUS_INCLUDE_DIRS "${OPUS_PREFIX}/include")
    set(OPUS_LIBRARIES "${OPUS_PREFIX}/lib/libopus.dylib")
    set(HIDAPI_INCLUDE_DIRS "${HIDAPI_PREFIX}/include")
    set(HIDAPI_LIBRARIES "${HIDAPI_PREFIX}/lib/libhidapi.dylib")
    set(LIBUSB_INCLUDE_DIRS "${LIBUSB_PREFIX}/include")
    set(LIBUSB_LIBRARIES "${LIBUSB_PREFIX}/lib/libusb-1.0.dylib")
elseif(WIN32)
    # Windows with vcpkg
    find_package(Opus CONFIG REQUIRED)
    find_package(hidapi CONFIG REQUIRED)
    # libusb doesn't provide CMake config files; use find_path/find_library
    find_path(LIBUSB_INCLUDE_DIRS NAMES libusb-1.0/libusb.h REQUIRED)
    find_library(LIBUSB_LIBRARIES NAMES usb-1.0 libusb-1.0 REQUIRED)
    set(OPUS_INCLUDE_DIRS "")
    set(HIDAPI_INCLUDE_DIRS "")
    set(OPUS_LIBRARIES Opus::opus)
    set(HIDAPI_LIBRARIES hidapi::hidapi)
else()
    # Linux with pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED opus)
    pkg_check_modules(HIDAPI REQUIRED hidapi-libusb)
    pkg_check_modules(LIBUDEV REQUIRED libudev)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0>=1.0.21)
endif()
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/utils/radioutils.cpp
    src/utils/bandplan.cpp
    src/controllers/connectioncontroller.cpp
    src/controllers/hardwarecontroller.cpp
    src/controllers/audiocontroller.cpp
    src/controllers/spectrumcontroller.cpp
    src/controllers/dxclustercontroller.cpp
    src/controllers/textdecodecontroller.cpp
    src/controllers/statusbarcontroller.cpp
    src/controllers/menucontroller.cpp
    src/controllers/antennaconfigcontroller.cpp
    src/controllers/featuremenucontroller.cpp
    src/controllers/popupmanager.cpp
    src/controllers/kpa1500uicontroller.cpp
    src/controllers/modepopupcontroller.cpp
    src/controllers/bandnavigationcontroller.cpp
    src/controllers/buttonrowdispatcher.cpp
    src/controllers/macrocontroller.cpp
    src/controllers/processingdisplaycontroller.cpp
    src/controllers/antennadisplaycontroller.cpp
    src/controllers/vforowindicatorcontroller.cpp
    src/controllers/ritxitcontroller.cpp
    src/controllers/modelabelcontroller.cpp
    src/controllers/vfofrequencycontroller.cpp
    src/controllers/subdivindicatorcontroller.cpp
    src/controllers/txstatecontroller.cpp
    src/controllers/sidecontroldisplaycontroller.cpp
    src/controllers/sidecontrolscrollcontroller.cpp
    src/controllers/rightsidecontroller.cpp
    src/controllers/memorybuttonscontroller.cpp
    src/controllers/cwcontroller.cpp
    src/network/tcpclient.cpp
    src/network/protocol.cpp
    src/network/networkmetrics.cpp
    src/network/kpa1500client.cpp
    src/network/dxclusterclient.cpp
    src/network/catserver.cpp
    src/network/catframes.cpp
    src/network/catpushbroadcaster.cpp
    src/network/k4discovery.cpp
    src/audio/audioengine.cpp
    src/audio/opusdecoder.cpp
    src/audio/opusencoder.cpp
    src/audio/sidetonegenerator.cpp
    src/dsp/panadapter_rhi.cpp
    src/dsp/minipan_rhi.cpp
    src/settings/radiosettings.cpp
    src/models/radiostate.cpp
    src/models/radiostate/antennastate.cpp
    src/models/radiostate/audioeffectsstate.cpp
    src/models/radiostate/datacontrolstate.cpp
    src/models/radiostate/frequencyvfostate.cpp
    src/models/radiostate/levelsstate.cpp
    src/models/radiostate/modefilterstate.cpp
    src/models/radiostate/powerstate.cpp
    src/models/radiostate/processingstate.cpp
    src/models/radiostate/qskcontrolstate.cpp
    src/models/radiostate/rxtxmeterstate.cpp
    src/models/radiostate/spectrumdisplaystate.cpp
    src/models/radiostate/textdecodestate.cpp
    src/models/radiostate/xvtrbandstate.cpp
    src/models/menumodel.cpp
    src/ui/dialogs/radiomanagerdialog.cpp
    src/ui/widgets/dualcontrolbutton.cpp
    src/ui/widgets/sidecontrolpanel.cpp
    src/ui/widgets/bottommenubar.cpp
    src/ui/overlays/menuoverlay.cpp
    src/ui/overlays/mousevfoindicator.cpp
    src/ui/widgets/vfowidget.cpp
    src/ui/widgets/frequencydisplaywidget.cpp
    src/ui/widgets/vforowwidget.cpp
    src/ui/widgets/filterindicatorwidget.cpp
    src/ui/popups/bandpopupwidget.cpp
    src/ui/popups/buttonrowpopup.cpp
    src/ui/widgets/displaymenubutton.cpp
    src/ui/widgets/controlgroupwidget.cpp
    src/ui/widgets/togglegroupwidget.cpp
    src/ui/popups/displaypopupwidget.cpp
    src/ui/dialogs/optionsdialog.cpp
    src/ui/pages/aboutpage.cpp
    src/ui/pages/stationpage.cpp
    src/ui/pages/audioinputpage.cpp
    src/ui/pages/audiooutputpage.cpp
    src/ui/pages/rigcontrolpage.cpp
    src/ui/pages/cwkeyerpage.cpp
    src/ui/pages/kpodpage.cpp
    src/ui/pages/kpa1500page.cpp
    src/ui/pages/dxclusterpage.cpp
    src/ui/overlays/dxspotoverlay.cpp
    src/ui/widgets/rightsidepanel.cpp
    src/ui/widgets/featuremenubar.cpp
    src/ui/popups/modepopupwidget.cpp
    src/ui/widgets/txmeterwidget.cpp
    src/ui/widgets/nethealthwidget.cpp
    src/ui/widgets/nethealthpopup.cpp
    src/ui/widgets/icontextlabel.cpp
    src/ui/widgets/powerstatusbutton.cpp
    src/ui/popups/fnpopupwidget.cpp
    src/ui/popups/rxeqpopupwidget.cpp
    src/ui/popups/antennacfgpopup.cpp
    src/ui/popups/lineoutpopup.cpp
    src/ui/popups/lineinpopup.cpp
    src/ui/popups/micinputpopup.cpp
    src/ui/popups/micconfigpopup.cpp
    src/ui/popups/voxpopup.cpp
    src/ui/popups/ssbbwpopup.cpp
    src/ui/popups/keyingweightpopup.cpp
    src/ui/popups/confirmpopup.cpp
    src/ui/popups/softwarelistpopup.cpp
    src/ui/dialogs/textdecodewindow.cpp
    src/ui/dialogs/macrodialog.cpp
    src/ui/widgets/notificationwidget.cpp
    src/ui/styling/k4styles.cpp
    src/ui/styling/k4glyphs.cpp
    src/ui/popups/k4popupbase.cpp
    src/ui/widgets/kpa1500minipanel.cpp
    src/ui/overlays/sidecontroloverlay.cpp
    src/ui/overlays/monoverlay.cpp
    src/ui/overlays/baloverlay.cpp
    src/utils/wheelaccumulator.cpp
    src/hardware/kpoddevice.cpp
    src/hardware/kpodhidworker.cpp
    src/hardware/kpodplusdevice.cpp
    src/hardware/kpodplususbworker.cpp
    src/hardware/halikeydevice.cpp
    src/hardware/halikeyworkerbase.cpp
    src/hardware/halikeyv14worker.cpp
    src/hardware/halikeymidiworker.cpp
    src/hardware/iambickeyer.cpp
    third_party/rtmidi/RtMidi.cpp
)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/hardware/kpodudevworker.cpp)
endif()

set(HEADERS
    src/mainwindow.h
    src/utils/radioutils.h
    src/utils/bandplan.h
    src/controllers/connectioncontroller.h
    src/controllers/hardwarecontroller.h
    src/controllers/audiocontroller.h
    src/controllers/spectrumcontroller.h
    src/controllers/dxclustercontroller.h
    src/controllers/textdecodecontroller.h
    src/controllers/statusbarcontroller.h
    src/controllers/menucontroller.h
    src/controllers/antennaconfigcontroller.h
    src/controllers/featuremenucontroller.h
    src/controllers/popupmanager.h
    src/controllers/kpa1500uicontroller.h
    src/controllers/modepopupcontroller.h
    src/controllers/bandnavigationcontroller.h
    src/controllers/buttonrowdispatcher.h
    src/controllers/macrocontroller.h
    src/controllers/processingdisplaycontroller.h
    src/controllers/antennadisplaycontroller.h
    src/controllers/vforowindicatorcontroller.h
    src/controllers/ritxitcontroller.h
    src/controllers/modelabelcontroller.h
    src/controllers/vfofrequencycontroller.h
    src/controllers/subdivindicatorcontroller.h
    src/controllers/txstatecontroller.h
    src/controllers/sidecontroldisplaycontroller.h
    src/controllers/sidecontrolscrollcontroller.h
    src/controllers/rightsidecontroller.h
    src/controllers/memorybuttonscontroller.h
    src/controllers/cwcontroller.h
    src/network/tcpclient.h
    src/network/protocol.h
    src/network/networkmetrics.h
    src/network/kpa1500client.h
    src/network/dxclusterclient.h
    src/network/catserver.h
    src/network/catframes.h
    src/network/catpushbroadcaster.h
    src/network/k4discovery.h
    src/audio/audioengine.h
    src/audio/opusdecoder.h
    src/audio/opusencoder.h
    src/audio/sidetonegenerator.h
    src/dsp/panadapter_rhi.h
    src/dsp/minipan_rhi.h
    src/settings/radiosettings.h
    src/models/radiostate.h
    src/models/radiostate/antennastate.h
    src/models/radiostate/datacontrolstate.h
    src/models/radiostate/frequencyvfostate.h
    src/models/radiostate/levelsstate.h
    src/models/radiostate/modefilterstate.h
    src/models/radiostate/processingstate.h
    src/models/radiostate/qskcontrolstate.h
    src/models/radiostate/textdecodestate.h
    src/models/radiostate/xvtrbandstate.h
    src/models/menumodel.h
    src/utils/macroids.h
    src/ui/dialogs/radiomanagerdialog.h
    src/ui/widgets/dualcontrolbutton.h
    src/ui/widgets/sidecontrolpanel.h
    src/ui/widgets/bottommenubar.h
    src/ui/overlays/menuoverlay.h
    src/ui/overlays/mousevfoindicator.h
    src/ui/widgets/vfowidget.h
    src/ui/widgets/frequencydisplaywidget.h
    src/ui/widgets/vforowwidget.h
    src/ui/widgets/filterindicatorwidget.h
    src/ui/popups/bandpopupwidget.h
    src/ui/popups/buttonrowpopup.h
    src/ui/widgets/displaymenubutton.h
    src/ui/widgets/controlgroupwidget.h
    src/ui/widgets/togglegroupwidget.h
    src/ui/popups/displaypopupwidget.h
    src/ui/dialogs/optionsdialog.h
    src/ui/pages/aboutpage.h
    src/ui/pages/stationpage.h
    src/ui/pages/audioinputpage.h
    src/ui/pages/audiooutputpage.h
    src/ui/pages/rigcontrolpage.h
    src/ui/pages/cwkeyerpage.h
    src/ui/pages/kpodpage.h
    src/ui/pages/kpa1500page.h
    src/ui/pages/dxclusterpage.h
    src/ui/overlays/dxspotoverlay.h
    src/ui/widgets/rightsidepanel.h
    src/ui/widgets/featuremenubar.h
    src/ui/popups/modepopupwidget.h
    src/ui/widgets/txmeterwidget.h
    src/ui/widgets/nethealthwidget.h
    src/ui/widgets/nethealthpopup.h
    src/ui/popups/fnpopupwidget.h
    src/ui/popups/rxeqpopupwidget.h
    src/ui/popups/antennacfgpopup.h
    src/ui/popups/lineoutpopup.h
    src/ui/popups/lineinpopup.h
    src/ui/popups/micinputpopup.h
    src/ui/popups/micconfigpopup.h
    src/ui/popups/voxpopup.h
    src/ui/popups/ssbbwpopup.h
    src/ui/popups/keyingweightpopup.h
    src/ui/popups/softwarelistpopup.h
    src/ui/dialogs/textdecodewindow.h
    src/ui/dialogs/macrodialog.h
    src/ui/widgets/notificationwidget.h
    src/ui/styling/k4styles.h
    src/ui/styling/k4constants.h
    src/ui/popups/k4popupbase.h
    src/ui/widgets/kpa1500minipanel.h
    src/ui/overlays/sidecontroloverlay.h
    src/ui/overlays/monoverlay.h
    src/ui/overlays/baloverlay.h
    src/utils/wheelaccumulator.h
    src/hardware/kpoddevice.h
    src/hardware/kpodhidworker.h
    src/hardware/kpodplusdevice.h
    src/hardware/kpodplususbworker.h
    src/hardware/halikeydevice.h
    src/hardware/halikeyworkerbase.h
    src/hardware/halikeyv14worker.h
    src/hardware/halikeymidiworker.h
    src/hardware/iambickeyer.h
)

if(UNIX AND NOT APPLE)
    list(APPEND HEADERS src/hardware/kpodudevworker.h)
endif()

# On macOS, create an app bundle
if(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME})
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${QK4_VERSION_FULL})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${QK4_VERSION_FULL})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.kf5o.qk4")
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
    ${LIBUSB_INCLUDE_DIRS}
)

if(UNIX AND NOT APPLE)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LIBUDEV_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBUDEV_LIBRARIES})
endif()

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
    ${LIBUSB_LIBRARIES}
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
        src/dsp/shaders/spectrum_fill.vert
        src/dsp/shaders/spectrum_fill.frag
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

# =============================================================================
# Tests
# =============================================================================

enable_testing()
add_subdirectory(tests)

# =============================================================================
# Deployment / Bundling - Create self-contained distributable app
# =============================================================================

if(APPLE)
    # Find macdeployqt
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${Qt6_DIR}/../../../bin")

    # Resolve Qt lib directory for macdeployqt - use get_filename_component to
    # produce a clean absolute path (no unresolved ../.. components) that
    # macdeployqt can consume. Works on both Intel (/usr/local) and
    # Apple Silicon (/opt/homebrew).
    get_filename_component(QT_LIB_DIR "${Qt6_DIR}/../../../lib" ABSOLUTE)
    message(STATUS "Qt lib dir for macdeployqt: ${QT_LIB_DIR}")

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
            -libpath="${QT_LIB_DIR}"
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
        # Copy libusb library
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBUSB_LIBRARIES}"
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
        # Fix libusb library path
        COMMAND install_name_tool -id "@executable_path/../Frameworks/libusb-1.0.dylib"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app/Contents/Frameworks/libusb-1.0.dylib" || true
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
