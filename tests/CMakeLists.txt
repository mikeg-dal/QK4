# QK4 Unit Tests
# Uses Qt Test framework for test infrastructure
#
# Optional sanitizer build: pass -DQK4_TESTS_SANITIZE=ON to compile test
# executables with AddressSanitizer + UndefinedBehaviorSanitizer. Enabled
# in CI for the duration of the RadioState / MainWindow refactor so
# subsystem pointer shuffling can't introduce use-after-free quietly.
option(QK4_TESTS_SANITIZE "Build tests with ASAN + UBSAN" OFF)

if(QK4_TESTS_SANITIZE AND NOT MSVC)
    message(STATUS "QK4 tests: ASAN+UBSAN enabled")
    add_compile_options(-fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# RadioState CAT command parser tests
add_executable(test_radiostate test_radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/antennastate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/audioeffectsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/datacontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/frequencyvfostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/levelsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/modefilterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/powerstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/xvtrbandstate.cpp
)
target_include_directories(test_radiostate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radiostate Qt6::Core Qt6::Test)
add_test(NAME RadioStateTests COMMAND test_radiostate)

# RadioState command-registry structural invariant tests
add_executable(test_radiostate_registry test_radiostate_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/antennastate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/audioeffectsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/datacontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/frequencyvfostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/levelsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/modefilterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/powerstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/xvtrbandstate.cpp
)
target_include_directories(test_radiostate_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radiostate_registry Qt6::Core Qt6::Test)
add_test(NAME RadioStateRegistryTests COMMAND test_radiostate_registry)

# RadioState golden CAT-trace replay regression test. The trace fixture
# at tests/golden/session.cat.txt is not committed (local developer data,
# per .gitignore); the test QSKIPs on a fresh checkout. Primarily a local
# safety net for Phase 1 refactor work.
add_executable(test_radiostate_golden test_radiostate_golden.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/antennastate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/audioeffectsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/datacontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/frequencyvfostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/levelsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/modefilterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/powerstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/xvtrbandstate.cpp
)
target_include_directories(test_radiostate_golden PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radiostate_golden Qt6::Core Qt6::Test)
add_test(NAME RadioStateGoldenTests COMMAND test_radiostate_golden)

option(QK4_BUILD_BENCHMARKS "Build the RadioState parse-dispatch microbenchmark" OFF)

# RadioState::parseCATCommand() dispatch microbenchmark — not wired into
# add_test() so it doesn't run during every `ctest` invocation; build and
# invoke explicitly when checking for perf regressions. Gated behind the
# QK4_BUILD_BENCHMARKS option so regular CI builds don't compile it.
if(QK4_BUILD_BENCHMARKS)
    add_executable(bench_radiostate bench_radiostate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/antennastate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/audioeffectsstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/datacontrolstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/frequencyvfostate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/levelsstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/modefilterstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/powerstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/xvtrbandstate.cpp
    )
    target_include_directories(bench_radiostate PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_radiostate Qt6::Core Qt6::Test)
endif()

# RadioUtils shared utility tests
add_executable(test_radioutils test_radioutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/radioutils.cpp
)
target_include_directories(test_radioutils PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radioutils Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME RadioUtilsTests COMMAND test_radioutils)

# BandPlan data/lookup tests (QColor needs Qt6::Gui)
add_executable(test_bandplan test_bandplan.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/bandplan.cpp
)
target_include_directories(test_bandplan PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_bandplan Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME BandPlanTests COMMAND test_bandplan)

# MenuModel parsing + label resolution tests
add_executable(test_menumodel test_menumodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/menumodel.cpp
)
target_include_directories(test_menumodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_menumodel Qt6::Core Qt6::Test)
add_test(NAME MenuModelTests COMMAND test_menumodel)

# NetworkMetrics sparkline history + jitter tests
add_executable(test_networkmetrics test_networkmetrics.cpp
    ${CMAKE_SOURCE_DIR}/src/network/networkmetrics.cpp
)
target_include_directories(test_networkmetrics PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_networkmetrics Qt6::Core Qt6::Test)
add_test(NAME NetworkMetricsTests COMMAND test_networkmetrics)

# Protocol packet parsing tests
add_executable(test_protocol test_protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
)
target_include_directories(test_protocol PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_protocol Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME ProtocolTests COMMAND test_protocol)

# CatServer command handler tests
add_executable(test_catserver test_catserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/catserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/catframes.cpp
    ${CMAKE_SOURCE_DIR}/src/network/catpushbroadcaster.cpp
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/antennastate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/audioeffectsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/datacontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/frequencyvfostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/levelsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/modefilterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/powerstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/xvtrbandstate.cpp
)
target_include_directories(test_catserver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_catserver Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME CatServerTests COMMAND test_catserver)

# CatPushBroadcaster per-client AI subscription / push tests
add_executable(test_catpushbroadcaster test_catpushbroadcaster.cpp
    ${CMAKE_SOURCE_DIR}/src/network/catpushbroadcaster.cpp
    ${CMAKE_SOURCE_DIR}/src/network/catframes.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/antennastate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/audioeffectsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/datacontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/frequencyvfostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/levelsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/modefilterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/powerstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/xvtrbandstate.cpp
)
target_include_directories(test_catpushbroadcaster PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_catpushbroadcaster Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME CatPushBroadcasterTests COMMAND test_catpushbroadcaster)

# DX Cluster spot parsing tests
add_executable(test_dxcluster test_dxcluster.cpp
    ${CMAKE_SOURCE_DIR}/src/network/dxclusterclient.cpp
)
target_include_directories(test_dxcluster PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dxcluster Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME DxClusterTests COMMAND test_dxcluster)

# HalikeyDevice edge-dedupe tests — pure logic, no Qt/RtMidi/serial linkage needed.
add_executable(test_halikeydevice test_halikeydevice.cpp)
target_include_directories(test_halikeydevice PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_halikeydevice Qt6::Core Qt6::Test)
add_test(NAME HalikeyDeviceTests COMMAND test_halikeydevice)

# IambicKeyer state-machine tests — Iambic A/B squeeze behavior, single taps, reversal.
add_executable(test_iambickeyer test_iambickeyer.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/iambickeyer.cpp
)
target_include_directories(test_iambickeyer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_iambickeyer Qt6::Core Qt6::Test)
add_test(NAME IambicKeyerTests COMMAND test_iambickeyer)

# KPOD+ USB worker — pure-logic tests (command builders, EP02 trim, response decoder)
add_executable(test_kpodplususbworker test_kpodplususbworker.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/kpodplususbworker.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/kpodplusdevice.cpp
)
target_include_directories(test_kpodplususbworker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUSB_INCLUDE_DIRS}
)
target_link_libraries(test_kpodplususbworker Qt6::Core Qt6::Test ${LIBUSB_LIBRARIES})
add_test(NAME KpodPlusUsbWorkerTests COMMAND test_kpodplususbworker)

# RC-28 HID worker — pure-logic report decoder tests. hidapi is linked because
# the worker TU includes <hidapi/hidapi.h>; on Linux the udev hotplug worker and
# libudev are pulled in the same way the main app links them.
add_executable(test_rc28hidworker test_rc28hidworker.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/rc28hidworker.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/rc28device.cpp
)
if(UNIX AND NOT APPLE)
    target_sources(test_rc28hidworker PRIVATE ${CMAKE_SOURCE_DIR}/src/hardware/kpodudevworker.cpp)
endif()
target_include_directories(test_rc28hidworker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${HIDAPI_INCLUDE_DIRS}
)
target_link_libraries(test_rc28hidworker Qt6::Core Qt6::Test ${HIDAPI_LIBRARIES})
if(UNIX AND NOT APPLE)
    target_include_directories(test_rc28hidworker PRIVATE ${LIBUDEV_INCLUDE_DIRS})
    target_link_libraries(test_rc28hidworker ${LIBUDEV_LIBRARIES})
endif()
add_test(NAME Rc28HidWorkerTests COMMAND test_rc28hidworker)

# FlexControl serial worker — pure-logic token decoder tests.
add_executable(test_flexcontrolserialworker test_flexcontrolserialworker.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/flexcontrolserialworker.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/flexcontroldevice.cpp
)
target_include_directories(test_flexcontrolserialworker PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_flexcontrolserialworker Qt6::Core Qt6::Test Qt6::SerialPort)
add_test(NAME FlexControlSerialWorkerTests COMMAND test_flexcontrolserialworker)

# Aggregate target: `cmake --build <dir> --target qk4_tests` builds every
# test_* executable defined above. CI uses this so adding `add_executable(test_foo)`
# automatically gets the new test built — no parallel maintenance of an explicit
# list in .github/workflows/lint.yml (which is what caused the late-May 2026
# string of red builds when test_menumodel / test_catpushbroadcaster /
# test_halikeydevice were added but the workflow's --target list wasn't).
get_property(_qk4_dir_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(_qk4_test_targets "")
foreach(_t IN LISTS _qk4_dir_targets)
    if(_t MATCHES "^test_")
        list(APPEND _qk4_test_targets ${_t})
    endif()
endforeach()
add_custom_target(qk4_tests DEPENDS ${_qk4_test_targets})
