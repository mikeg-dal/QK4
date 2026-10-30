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

# Optional RealtimeSanitizer build: pass -DQK4_TESTS_RTSAN=ON, with Clang 20 or later, to check every
# function marked QK4_NONBLOCKING (src/utils/rtsan.h) for allocation, locks, sleeps and I/O while the
# tests run (CONVENTIONS Rule 16). Run by the RealtimeSanitizer job in ci-checks.yml.
# WHY Qt need not be instrumented, unlike for TSan: RTSan intercepts the C library calls themselves,
# so a malloc or a mutex inside Qt is seen however Qt was built. Outside a marked function it checks
# nothing, so every suite runs at full speed and all of them run.
option(QK4_TESTS_RTSAN "Build tests with RealtimeSanitizer (Clang 20+)" OFF)

if(QK4_TESTS_RTSAN)
    if(QK4_TESTS_SANITIZE)
        message(FATAL_ERROR "QK4_TESTS_RTSAN cannot be combined with QK4_TESTS_SANITIZE")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
        message(FATAL_ERROR "QK4_TESTS_RTSAN needs Clang 20 or later, found "
                            "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    message(STATUS "QK4 tests: RealtimeSanitizer enabled")
    add_compile_options(-fsanitize=realtime -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=realtime)
endif()

# Every logging category is defined in src/logging/logcategories.cpp. Linked into every test below
# (see the aggregate loop at the end), so a test that compiles a logging source needs nothing extra.
add_library(qk4_logging STATIC
    ${CMAKE_SOURCE_DIR}/src/logging/logcategories.cpp
    ${CMAKE_SOURCE_DIR}/src/logging/logpaths.cpp
    ${CMAKE_SOURCE_DIR}/src/logging/logsink.cpp
    ${CMAKE_SOURCE_DIR}/src/logging/logrules.cpp
    ${CMAKE_SOURCE_DIR}/src/logging/logsession.cpp
)
target_include_directories(qk4_logging PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(qk4_logging PUBLIC Qt6::Core)

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

# Panadapter amplitude-axis arithmetic: dBm <-> S-units, label placement and decimation.
add_executable(test_spectrumscale test_spectrumscale.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/spectrumscale.cpp
)
target_include_directories(test_spectrumscale PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_spectrumscale Qt6::Core Qt6::Test)
add_test(NAME SpectrumScaleTests COMMAND test_spectrumscale)

# Panadapter frequency axis: mode parsing, CW display centre, passband edges, PAN frame bin bounds.
add_executable(test_spectrumgeometry test_spectrumgeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/spectrumgeometry.cpp
)
target_include_directories(test_spectrumgeometry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_spectrumgeometry Qt6::Core Qt6::Test)
add_test(NAME SpectrumGeometryTests COMMAND test_spectrumgeometry)

# RFC 6455 framing for the TCI server: length forms, masking, fragment reassembly, size limits.
add_executable(test_websocketframe test_websocketframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
)
target_include_directories(test_websocketframe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_websocketframe Qt6::Core Qt6::Test)
add_test(NAME WebSocketFrameTests COMMAND test_websocketframe)

# K4 receive audio -> TCI RX_AUDIO samples: rate, channel mapping, and packet-boundary continuity.
add_executable(test_tciaudiobridge test_tciaudiobridge.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudiobridge.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/cwmacro.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserverstate.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudioframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/audioupsampler.cpp
)
target_include_directories(test_tciaudiobridge PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tciaudiobridge Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME TciAudioBridgeTests COMMAND test_tciaudiobridge)

# TCI server, protocol surface: init burst contents and ordering, audio subscription, per-client
# isolation, CAT sets and the read-only queries. The transmit path is a separate target below.
add_executable(test_tciserver test_tciserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/cwmacro.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserverstate.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudioframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
)
target_include_directories(test_tciserver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tciserver Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME TciServerTests COMMAND test_tciserver)

# TCI server, transmit path: PTT ownership, TX_CHRONO pacing and TX audio admission. Split from
# test_tciserver so neither file passes the 800-line limit in CONVENTIONS.md rule 7; both share
# the socket client in tcitestclient.h.
add_executable(test_tciservertransmit test_tciservertransmit.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/cwmacro.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserverstate.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudioframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
)
target_include_directories(test_tciservertransmit PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tciservertransmit Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME TciServerTransmitTests COMMAND test_tciservertransmit)

# TCI server, answers: the read-only query group and the spec value constraints (AGC vocabulary,
# squelch range, the one shared RIT/XIT offset). Split from test_tciserver to stay under the
# 800-line limit in CONVENTIONS.md rule 7.
add_executable(test_tciqueries test_tciqueries.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/cwmacro.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserverstate.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudioframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
)
target_include_directories(test_tciqueries PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tciqueries Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME TciQueriesTests COMMAND test_tciqueries)

# TCI client roster: the address/last-seen/last-message table on the options page. Driven over a
# real socket, because the peer address only exists on a real connection. Split from test_tciserver
# to stay under the 800-line limit in CONVENTIONS.md rule 7.
add_executable(test_tciclients test_tciclients.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/cwmacro.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserverstate.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudioframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
)
target_include_directories(test_tciclients PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tciclients Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME TciClientsTests COMMAND test_tciclients)

# TCI sensor reporting: subscription per direction, interval clamping, and the server-to-client
# telemetry an --audit sweep cannot see because none of it is ever a reply to a query.
add_executable(test_tcisensors test_tcisensors.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/cwmacro.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciserverstate.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudioframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
)
target_include_directories(test_tcisensors PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tcisensors Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME TciSensorsTests COMMAND test_tcisensors)

# RadioSettings: the connect-at-startup rule, which is enforced in the model rather than only in
# the dialog because a settings file can also be hand-edited or restored from a backup.
add_executable(test_radiosettings test_radiosettings.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/radiosettings.cpp
)
target_include_directories(test_radiosettings PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_radiosettings Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME RadioSettingsTests COMMAND test_radiosettings)

# TCI binary audio framing: the 64-byte header, and the opposite payload rules per direction.
add_executable(test_tciaudioframe test_tciaudioframe.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciaudioframe.cpp
)
target_include_directories(test_tciaudioframe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tciaudioframe Qt6::Core Qt6::Test)
add_test(NAME TciAudioFrameTests COMMAND test_tciaudioframe)

# TCI grammar: tokenising, the global one-argument form, and delimiter-safe formatting.
add_executable(test_tciprotocol test_tciprotocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/tciprotocol.cpp
)
target_include_directories(test_tciprotocol PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_tciprotocol Qt6::Core Qt6::Test)
add_test(NAME TciProtocolTests COMMAND test_tciprotocol)

# WebSocket transport over a real loopback socket: upgrade, session lifecycle, ping/pong, limits.
add_executable(test_websocketserver test_websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/websocketframe.cpp
)
target_include_directories(test_websocketserver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_websocketserver Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME WebSocketServerTests COMMAND test_websocketserver)

# 12kHz -> 48kHz interpolation for the TCI receive path: passband flatness and image rejection.
add_executable(test_audioupsampler test_audioupsampler.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/audioupsampler.cpp
)
target_include_directories(test_audioupsampler PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_audioupsampler Qt6::Core Qt6::Test)
add_test(NAME AudioUpsamplerTests COMMAND test_audioupsampler)

# Waterfall row/pixel mapping: which stored row each screen pixel samples, mirroring the shaders.
add_executable(test_waterfallgeometry test_waterfallgeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/waterfallgeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/spectrumscale.cpp
)
target_include_directories(test_waterfallgeometry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_waterfallgeometry Qt6::Core Qt6::Test)
add_test(NAME WaterfallGeometryTests COMMAND test_waterfallgeometry)

# MIDI HaliKey port-name matching: which port we open, and whether it has disappeared.
# Header-only, no RtMidi or MIDI subsystem needed.
add_executable(test_halikeymidiports test_halikeymidiports.cpp)
target_include_directories(test_halikeymidiports PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_halikeymidiports Qt6::Core Qt6::Test)
add_test(NAME HalikeyMidiPortsTests COMMAND test_halikeymidiports)

# Dashed-overlay vertex arithmetic: the buffer-sizing rule for the notch and RTTY tone lines.
# Header-only (PanadapterConstants + DashGeometry), so no source files. Those headers deliberately
# depend on nothing but Qt Core — rhi_utils.h next to them includes the PRIVATE rhi/qshader.h, which
# is not installed on every platform this builds on.
add_executable(test_panadapterdashes test_panadapterdashes.cpp)
target_include_directories(test_panadapterdashes PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_panadapterdashes Qt6::Core Qt6::Test)
add_test(NAME PanadapterDashTests COMMAND test_panadapterdashes)

# BandPlan data/lookup tests (QColor needs Qt6::Gui)
add_executable(test_bandplan test_bandplan.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/bandplan.cpp
)
target_include_directories(test_bandplan PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_bandplan Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME BandPlanTests COMMAND test_bandplan)

# KPA1500 sub-antenna enable map + antenna labels
add_executable(test_kpa1500antennas test_kpa1500antennas.cpp
    ${CMAKE_SOURCE_DIR}/src/network/kpa1500antennas.cpp
)
target_include_directories(test_kpa1500antennas PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_kpa1500antennas Qt6::Core Qt6::Test)
add_test(NAME Kpa1500AntennasTests COMMAND test_kpa1500antennas)

# KPA1500 response parsing (fault code, device info) against a loopback fake amplifier
add_executable(test_kpa1500client test_kpa1500client.cpp
    ${CMAKE_SOURCE_DIR}/src/network/kpa1500client.cpp
    ${CMAKE_SOURCE_DIR}/src/network/kpa1500antennas.cpp
)
target_include_directories(test_kpa1500client PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_kpa1500client Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME Kpa1500ClientTests COMMAND test_kpa1500client)

# Band-plan strip layout: blocks, tag, sticky labels, marker collisions (QColor via bandplan.cpp)
add_executable(test_bandplanstrip test_bandplanstrip.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/bandplanstrip.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/bandplan.cpp
)
target_include_directories(test_bandplanstrip PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_bandplanstrip Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME BandPlanStripTests COMMAND test_bandplanstrip)

# MenuModel parsing + label resolution tests
add_executable(test_menumodel test_menumodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/menumodel.cpp
)
target_include_directories(test_menumodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_menumodel Qt6::Core Qt6::Test)
add_test(NAME MenuModelTests COMMAND test_menumodel)

# Menu overlay across disconnect/reconnect (#124) — the overlay against a real MenuModel, offscreen.
add_executable(test_menuoverlay test_menuoverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/models/menumodel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/menuoverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/wheelaccumulator.cpp
)
target_include_directories(test_menuoverlay PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_menuoverlay Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME MenuOverlayTests COMMAND test_menuoverlay)
set_tests_properties(MenuOverlayTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# RX playout: sink size per SL tier and the jitter policy, with a simulated network and device.
add_executable(test_rxplayout test_rxplayout.cpp ${CMAKE_SOURCE_DIR}/src/utils/radioutils.cpp)
target_include_directories(test_rxplayout PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_rxplayout Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME RxPlayoutTests COMMAND test_rxplayout)

# Lock-free SPSC ring carrying RX audio from the I/O thread to the audio thread. Run under TSan too.
add_executable(test_spscring test_spscring.cpp)
target_include_directories(test_spscring PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_spscring Qt6::Core Qt6::Test)
add_test(NAME SpscRingTests COMMAND test_spscring)

# RingDeque and FunctionRef: the keyer's and sidetone's non-allocating containers (src/utils/)
add_executable(test_rtcontainers test_rtcontainers.cpp)
target_include_directories(test_rtcontainers PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_rtcontainers Qt6::Core Qt6::Test)
add_test(NAME RtContainersTests COMMAND test_rtcontainers)

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

# KZ egress: the lock-free queue from the CW keying threads to the I/O thread, its wake-up across a
# moveToThread, and the latency histogram the I/O thread keeps.
add_executable(test_kzegress test_kzegress.cpp ${CMAKE_SOURCE_DIR}/src/network/kzegress.cpp)
target_include_directories(test_kzegress PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_kzegress Qt6::Core Qt6::Test)
add_test(NAME KzEgressTests COMMAND test_kzegress)

# Real-time scheduling: every OS's fallback chain through fake OS calls, the report table, and
# the real promotion on the runner (which usually has no real-time rights and must fall back).
add_executable(test_rtscheduling test_rtscheduling.cpp ${CMAKE_SOURCE_DIR}/src/utils/rtscheduling.cpp)
target_include_directories(test_rtscheduling PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_rtscheduling Qt6::Core Qt6::Test)
target_compile_definitions(test_rtscheduling PRIVATE QK4_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")
if(WIN32)
    target_link_libraries(test_rtscheduling avrt)
endif()
add_test(NAME RtSchedulingTests COMMAND test_rtscheduling)

# ThreadShutdown: a thread that stops in time is joined and deleted with what lives on it; one that
# misses its deadline is disowned (unparented, reported, left running) instead of crashing or hanging.
add_executable(test_threadshutdown test_threadshutdown.cpp ${CMAKE_SOURCE_DIR}/src/utils/threadshutdown.cpp)
target_include_directories(test_threadshutdown PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_threadshutdown Qt6::Core Qt6::Test)
add_test(NAME ThreadShutdownTests COMMAND test_threadshutdown)

# CwGate: when the keyer may key and when the sidetone stream is open (#92 idle CPU).
add_executable(test_cwgate test_cwgate.cpp)
target_include_directories(test_cwgate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_cwgate Qt6::Core Qt6::Test)
add_test(NAME CwGateTests COMMAND test_cwgate)

# CW by CAT: the TCI macro grammar (speed markers, prosign brackets, escaped separators) and the
# KY/KS commands it becomes. One target because the interesting cases are at the seam, where TCI
# and the K4 use the same characters for different things.
add_executable(test_cwmacro test_cwmacro.cpp
    ${CMAKE_SOURCE_DIR}/src/network/cwmacro.cpp
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
target_include_directories(test_cwmacro PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_cwmacro Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME CwMacroTests COMMAND test_cwmacro)

# CatServer command handler tests
add_executable(test_catserver test_catserver.cpp
    ${CMAKE_SOURCE_DIR}/src/network/catserver.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/radioutils.cpp
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

# DxClusterController: live connections stay matched to the saved list when an entry is removed.
add_executable(test_dxclustercontroller test_dxclustercontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/dxclustercontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/network/dxclusterclient.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/threadshutdown.cpp
)
target_include_directories(test_dxclustercontroller PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dxclustercontroller Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME DxClusterControllerTests COMMAND test_dxclustercontroller)

# PaddleKeyer — port of K4-Companion's keyer (Dale Farnsworth, GPL-3.0). Pure logic on a virtual
# clock; see tests/paddlekeyersim.h and docs/cw-keyer-port-plan.md.
set(QK4_PADDLEKEYER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/hardware/paddlekeyer.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/sidetonescheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/rtsemaphore.cpp
)
add_executable(test_paddlekeyersweeps test_paddlekeyersweeps.cpp ${QK4_PADDLEKEYER_SOURCES})
target_include_directories(test_paddlekeyersweeps PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_paddlekeyersweeps PRIVATE
    QK4_PADDLEKEYER_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data/paddlekeyer")
target_link_libraries(test_paddlekeyersweeps Qt6::Core Qt6::Test)
add_test(NAME PaddleKeyerSweepsTests COMMAND test_paddlekeyersweeps)

add_executable(test_paddlekeyer test_paddlekeyer.cpp ${QK4_PADDLEKEYER_SOURCES})
target_include_directories(test_paddlekeyer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_paddlekeyer Qt6::Core Qt6::Test)
add_test(NAME PaddleKeyerTests COMMAND test_paddlekeyer)

add_executable(test_paddlekeyerthreaded test_paddlekeyerthreaded.cpp ${QK4_PADDLEKEYER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/hardware/keyerruntime.cpp)
target_include_directories(test_paddlekeyerthreaded PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_paddlekeyerthreaded Qt6::Core Qt6::Test)
add_test(NAME PaddleKeyerThreadedTests COMMAND test_paddlekeyerthreaded)

add_executable(test_seriallinepoller test_seriallinepoller.cpp ${CMAKE_SOURCE_DIR}/src/hardware/seriallinepoller.cpp)
target_include_directories(test_seriallinepoller PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_seriallinepoller Qt6::Core Qt6::Test)
add_test(NAME SerialLinePollerTests COMMAND test_seriallinepoller)

add_executable(test_sidetonescheduler test_sidetonescheduler.cpp ${CMAKE_SOURCE_DIR}/src/audio/sidetonescheduler.cpp)
target_include_directories(test_sidetonescheduler PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_sidetonescheduler Qt6::Core Qt6::Test)
add_test(NAME SidetoneSchedulerTests COMMAND test_sidetonescheduler)

# KPOD+ USB worker — pure-logic tests (command builders, EP02 trim, response decoder)
add_executable(test_kpodplususbworker test_kpodplususbworker.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/kpodplususbworker.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/rtscheduling.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/kpodplusdevice.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/threadshutdown.cpp
)
target_include_directories(test_kpodplususbworker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUSB_INCLUDE_DIRS}
)
target_link_libraries(test_kpodplususbworker Qt6::Core Qt6::Test ${LIBUSB_LIBRARIES})
if(WIN32)
    target_link_libraries(test_kpodplususbworker avrt) # utils/rtscheduling.cpp
endif()
add_test(NAME KpodPlusUsbWorkerTests COMMAND test_kpodplususbworker)

# Connect-failure classification — pure logic, no socket or Qt::Network linkage needed.
add_executable(test_connectfailure test_connectfailure.cpp)
target_include_directories(test_connectfailure PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_connectfailure Qt6::Core Qt6::Test)
add_test(NAME ConnectFailureTests COMMAND test_connectfailure)

# Transmit arbiter — pure logic, no audio, network or widget linkage needed.
add_executable(test_transmitowner test_transmitowner.cpp)
target_include_directories(test_transmitowner PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_transmitowner Qt6::Core Qt6::Test)
add_test(NAME TransmitOwnerTests COMMAND test_transmitowner)

# USB device lifecycle policy — pure logic, no libusb/hidapi/Qt-device linkage needed.
add_executable(test_usbdevicelifecycle test_usbdevicelifecycle.cpp)
target_include_directories(test_usbdevicelifecycle PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_usbdevicelifecycle Qt6::Core Qt6::Test)
add_test(NAME UsbDeviceLifecycleTests COMMAND test_usbdevicelifecycle)

# RAW TX audio wire format (EM0/EM1) — pure logic, no audio device or opus linkage needed.
add_executable(test_rawaudioformat test_rawaudioformat.cpp)
target_include_directories(test_rawaudioformat PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_rawaudioformat Qt6::Core Qt6::Test)
add_test(NAME RawAudioFormatTests COMMAND test_rawaudioformat)

# RX audio normalisation for EM0..EM3 — the gate on the 2026-09-19 calibration.
# Needs real libopus because EM2/EM3 decode through it.
add_executable(test_opusdecoder
    test_opusdecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/opusdecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/opusencoder.cpp
)
target_include_directories(test_opusdecoder PRIVATE ${CMAKE_SOURCE_DIR}/src ${OPUS_INCLUDE_DIRS})
target_link_libraries(test_opusdecoder Qt6::Core Qt6::Test ${OPUS_LIBRARIES})
add_test(NAME OpusDecoderTests COMMAND test_opusdecoder)

# Meter scale marks — needs the widget TU, so Qt6::Widgets and the K4Styles header it uses.
add_executable(test_meterscale
    test_meterscale.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/txmeterwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
)
target_include_directories(test_meterscale PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_meterscale Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME MeterScaleTests COMMAND test_meterscale)

# Idle wake-ups (#92) — widget timers that must not run without work, offscreen.
add_executable(test_idlewakeups
    test_idlewakeups.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/txmeterwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
)
target_include_directories(test_idlewakeups PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_idlewakeups Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME IdleWakeupTests COMMAND test_idlewakeups)
set_tests_properties(IdleWakeupTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# RX playback boost + soft limiter - header-only, no audio device.
add_executable(test_rxoutputstage test_rxoutputstage.cpp)
target_include_directories(test_rxoutputstage PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_rxoutputstage Qt6::Core Qt6::Test)
add_test(NAME RxOutputStageTests COMMAND test_rxoutputstage)

# Form-control theming — renders bare controls under the app-wide fallback stylesheet, offscreen.
add_executable(test_formcontroltheme
    test_formcontroltheme.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
)
target_include_directories(test_formcontroltheme PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_formcontroltheme PRIVATE QK4_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")
target_link_libraries(test_formcontroltheme Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME FormControlThemeTests COMMAND test_formcontroltheme)
set_tests_properties(FormControlThemeTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Typography and colour standard: token contrast, no repeated hex values, and a source scan that keeps
# literal sizes and colours out of src/.
add_executable(test_tokenstandard
    test_tokenstandard.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
)
target_include_directories(test_tokenstandard PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_tokenstandard PRIVATE QK4_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")
target_link_libraries(test_tokenstandard Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME TokenStandardTests COMMAND test_tokenstandard)

# Main-window sizing - pure logic on sizes, no screen needed.
add_executable(test_windowfit test_windowfit.cpp)
target_include_directories(test_windowfit PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_windowfit Qt6::Core Qt6::Test)
add_test(NAME WindowFitTests COMMAND test_windowfit)

# Left side panel at its minimum size - nothing may overlap, offscreen.
add_executable(test_sidecontrolpanel
    test_sidecontrolpanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/sidecontrolpanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/sidepanelbutton.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/memorykeyrow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/bottommenubar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/vforowwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/rightsidepanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/kpa1500minipanel.cpp
    ${CMAKE_SOURCE_DIR}/src/network/kpa1500antennas.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/dualcontrolbutton.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/sidecontroloverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/monoverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/baloverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/wheelaccumulator.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/radiosettings.cpp
)
target_include_directories(test_sidecontrolpanel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_sidecontrolpanel PRIVATE QK4_VERSION="test")
target_link_libraries(test_sidecontrolpanel Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME SideControlPanelLayoutTests COMMAND test_sidecontrolpanel)
set_tests_properties(SideControlPanelLayoutTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Widgets whose screen must match their own state (review findings D-019..D-043), offscreen.
add_executable(test_widgetstate
    test_widgetstate.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4glyphs.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/frequencydisplaywidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/sidecontrolpanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/sidepanelbutton.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/dualcontrolbutton.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/featuremenubar.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/sidecontroloverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/monoverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/baloverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/popups/k4popupbase.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/popups/confirmpopup.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/popups/lineoutpopup.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/popups/fnpopupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/popups/displaypopupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/controlgroupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/displaymenubutton.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/togglegroupwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/macrodialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/overlays/dxspotoverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/radioutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/wheelaccumulator.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/radiosettings.cpp
)
target_include_directories(test_widgetstate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_widgetstate PRIVATE QK4_VERSION="test")
target_link_libraries(test_widgetstate Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME WidgetStateTests COMMAND test_widgetstate)
set_tests_properties(WidgetStateTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Capture-rate decimation — pure logic, no audio device needed.
add_executable(test_audiodecimator test_audiodecimator.cpp)
target_include_directories(test_audiodecimator PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_audiodecimator Qt6::Core Qt6::Test)
add_test(NAME AudioDecimatorTests COMMAND test_audiodecimator)

# Audio device selection policy — pure logic, no audio device needed.
add_executable(test_audiodeviceselection test_audiodeviceselection.cpp)
target_include_directories(test_audiodeviceselection PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_audiodeviceselection Qt6::Core Qt6::Test)
add_test(NAME AudioDeviceSelectionTests COMMAND test_audiodeviceselection)

# Rational resampling for capture rates AudioDecimator cannot take — pure DSP, no audio device.
add_executable(test_audioresampler test_audioresampler.cpp)
target_include_directories(test_audioresampler PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_audioresampler Qt6::Core Qt6::Test)
add_test(NAME AudioResamplerTests COMMAND test_audioresampler)

# What the operator is told when a microphone's rate cannot be captured from — pure wording.
add_executable(test_micrate test_micrate.cpp)
target_include_directories(test_micrate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_micrate Qt6::Core Qt6::Test)
add_test(NAME MicRateTests COMMAND test_micrate)

# Options dialog pages: none wider than the dialog, the DX cluster console takes the spare height,
# and the page list repaints all of itself on hover and selection (the stale-amber-edge fix). Offscreen.
add_executable(test_optionspagelayout
    test_optionspagelayout.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/pages/optionspagelayout.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/pages/stationpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/pages/displaypage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/pages/dxclusterpage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/radiosettings.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/interfacescale.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/dxclustercontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/network/dxclusterclient.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/threadshutdown.cpp
)
target_include_directories(test_optionspagelayout PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_optionspagelayout Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME OptionsPageLayoutTests COMMAND test_optionspagelayout)
set_tests_properties(OptionsPageLayoutTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# HaliKey port enumeration off the GUI thread - selection rule, queued delivery, superseded scans.
add_executable(test_halikeyportscan
    test_halikeyportscan.cpp
    ${CMAKE_SOURCE_DIR}/src/hardware/halikeyportscan.cpp
)
target_include_directories(test_halikeyportscan PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_halikeyportscan Qt6::Core Qt6::Test)
add_test(NAME HaliKeyPortScanTests COMMAND test_halikeyportscan)

# Logging categories: the qk4.* scheme, debug off by default, defined only in src/logging/.
add_executable(test_logcategories test_logcategories.cpp)
target_include_directories(test_logcategories PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_logcategories PRIVATE QK4_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")
target_link_libraries(test_logcategories Qt6::Core Qt6::Test)
add_test(NAME LogCategoriesTests COMMAND test_logcategories)

# Log directory per platform: a pure function of OS, environment and home.
add_executable(test_logpaths test_logpaths.cpp)
target_include_directories(test_logpaths PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_logpaths Qt6::Core Qt6::Test)
add_test(NAME LogPathsTests COMMAND test_logpaths)

# Log queue: bounded, drop-and-count, real-time producers never wait. Run under TSan weekly.
add_executable(test_logqueue test_logqueue.cpp)
target_include_directories(test_logqueue PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_logqueue Qt6::Core Qt6::Test)
add_test(NAME LogQueueTests COMMAND test_logqueue)

# Log file writer: rotation per launch and by size, header and footer, second instance, drops.
add_executable(test_logsink test_logsink.cpp)
target_include_directories(test_logsink PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_logsink Qt6::Core Qt6::Test)
add_test(NAME LogSinkTests COMMAND test_logsink)

# Diagnostics rules: level, groups, bench items and --log-rules, checked on the real categories.
add_executable(test_logrules test_logrules.cpp)
target_include_directories(test_logrules PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_logrules Qt6::Core Qt6::Test)
add_test(NAME LogRulesTests COMMAND test_logrules)

# Options -> Diagnostics: switches reach RadioSettings and change the real categories live. Offscreen.
add_executable(test_diagnosticspage
    test_diagnosticspage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/pages/diagnosticspage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/pages/optionspagelayout.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/styling/k4styles.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/radiosettings.cpp
)
target_include_directories(test_diagnosticspage PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_diagnosticspage Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME DiagnosticsPageTests COMMAND test_diagnosticspage)
set_tests_properties(DiagnosticsPageTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# RealtimeSanitizer self-test, RTSan builds only: proves the build checks anything, so a lost flag
# or a QK4_NONBLOCKING that expands to nothing fails instead of passing every suite unchecked. The
# two violation cases pass only on RTSan's report; abort_on_error=0 makes it exit rather than abort.
if(QK4_TESTS_RTSAN)
    add_executable(test_rtsanselftest test_rtsanselftest.cpp)
    target_include_directories(test_rtsanselftest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME RtSanSelfTestMalloc COMMAND test_rtsanselftest malloc)
    add_test(NAME RtSanSelfTestMutex COMMAND test_rtsanselftest mutex)
    add_test(NAME RtSanSelfTestAllowed COMMAND test_rtsanselftest allowed)
    set_tests_properties(RtSanSelfTestMalloc PROPERTIES
        ENVIRONMENT "RTSAN_OPTIONS=halt_on_error=1:abort_on_error=0"
        PASS_REGULAR_EXPRESSION "RealtimeSanitizer: unsafe-library-call.*malloc")
    set_tests_properties(RtSanSelfTestMutex PROPERTIES
        ENVIRONMENT "RTSAN_OPTIONS=halt_on_error=1:abort_on_error=0"
        PASS_REGULAR_EXPRESSION "RealtimeSanitizer: unsafe-library-call.*pthread_mutex_lock")
    set_tests_properties(RtSanSelfTestAllowed PROPERTIES PASS_REGULAR_EXPRESSION "allowed: no report")
endif()

# Aggregate target: `cmake --build <dir> --target qk4_tests` builds every
# test_* executable defined above. CI uses this so adding `add_executable(test_foo)`
# automatically gets the new test built — no parallel maintenance of an explicit
# list in .github/workflows/ci-checks.yml (which is what caused the late-May 2026
# string of red builds when new test suites were added but the workflow's
# --target list wasn't).
get_property(_qk4_dir_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(_qk4_test_targets "")
foreach(_t IN LISTS _qk4_dir_targets)
    if(_t MATCHES "^test_")
        list(APPEND _qk4_test_targets ${_t})
        target_link_libraries(${_t} qk4_logging)
    endif()
endforeach()
add_custom_target(qk4_tests DEPENDS ${_qk4_test_targets})
