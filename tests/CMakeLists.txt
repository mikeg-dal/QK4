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
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
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
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
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
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
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
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
        ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
    )
    target_include_directories(bench_radiostate PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(bench_radiostate Qt6::Core Qt6::Test)
endif()

# RadioUtils shared utility tests
add_executable(test_radioutils test_radioutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/radioutils.cpp
)
target_include_directories(test_radioutils PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radioutils Qt6::Core Qt6::Test)
add_test(NAME RadioUtilsTests COMMAND test_radioutils)

# Display popup widget behavior tests
add_executable(test_displaypopupwidget test_displaypopupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/popups/displaypopupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/popups/k4popupbase.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/controlgroupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/displaymenubutton.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/togglegroupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/wheelaccumulator.cpp
)
target_include_directories(test_displaypopupwidget PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_displaypopupwidget Qt6::Core Qt6::Widgets Qt6::Test)
add_test(NAME DisplayPopupWidgetTests COMMAND test_displaypopupwidget)

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
    ${CMAKE_SOURCE_DIR}/src/network/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/antennastate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/audioeffectsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/datacontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/frequencyvfostate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/levelsstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/modefilterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/processingstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/qskcontrolstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/rxtxmeterstate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/spectrumdisplaystate.cpp
    ${CMAKE_SOURCE_DIR}/src/models/radiostate/textdecodestate.cpp
)
target_include_directories(test_catserver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_catserver Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME CatServerTests COMMAND test_catserver)

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
