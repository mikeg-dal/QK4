# network/

TCP / TLS / PSK transport to the K4, binary protocol framing, CAT server for third-party apps, DX cluster client, KPA1500 amplifier client, K4 mDNS discovery.

## Files

- `tcpclient.{cpp,h}` — TLS/PSK socket to K4. State machine with ARP cold-start retry. `sendCAT` + `sendRaw` public entry points.
- `connect_failure.h` — header-only classifier deciding what a failed connection attempt is called and what the operator is told. Pure logic, no socket dependency, so `test_connectfailure` links it alone. The K4 reports neither a refused nor an accepted password, so this is where the limits of what QK4 can honestly claim are written down.
- `protocol.{cpp,h}` — K4 binary packet framing (`START_MARKER` + big-endian length + `END_MARKER`), payload type dispatch, audio + miniPAN + spectrum routing.
- `catserver.{cpp,h}` — TCP CAT server for WSJT-X / MacLoggerDX integration. Pinned public API via `docs/radiostate-catserver-api-contract.md`, regression-gated by `test_catserver`.
- `networkmetrics.{cpp,h}` — Latency / throughput aggregation. Feeds `NetHealthWidget`.
- `k4discovery.{cpp,h}` — UDP mDNS discovery for K4 servers on the LAN.
- `catframes.{h}` — `CatFrames::` namespace of small builders returning the exact `QByteArray` for a CAT command (`frequencyA`, `modeA`, …). Keeps command spelling in one place instead of scattered string literals.
- `catpushbroadcaster.{cpp,h}` — auto-information push for CAT clients. Tracks per-client AI level, subscribes to `RadioState` changes, and pushes updates to subscribers while polling clients receive nothing. Covered by `test_catpushbroadcaster`.
- `dxclusterclient.{cpp,h}` — TCP client to a single DX cluster node. Used by `DxClusterController` — one instance per configured cluster. Receive buffer capped at 64KB; overflow disconnects.
- `kpa1500client.{cpp,h}` — TCP client to the KPA1500 amplifier. Receive buffer capped at 64KB; overflow disconnects. Reads the per-band antenna enable map (`^AEbbALL;`) and steps antennas with `^AN+;`; never enables or configures antennas.
- `kpa1500antennas.{cpp,h}` — `Kpa1500Antennas::` namespace. KPA1500 firmware 3.x antenna numbering (1-32, sub-antennas routed through ANT1/ANT2): enable-map parsing and the `ANT1` / `ANT1:5` label. Pure; covered by `test_kpa1500antennas`. Syntax source: KPA1500 Programming Reference V3.

### TCI server

TCI 2.0 over WebSocket: CAT control, RX/TX audio and CW for WSJT-X, QLog, RumLogNG and TR4W. Owned by `TciController`; design in `docs/tci-server-design.md`, coverage in `docs/tci-command-coverage.md`.

- `websocketframe.{cpp,h}` — RFC 6455 framing with no sockets: length forms, masking, fragment reassembly, control frames. Covered by `test_websocketframe`.
- `websocketserver.{cpp,h}` — RFC 6455 server transport: HTTP upgrade, PING/PONG, CLOSE, client cap. Knows nothing about TCI. Covered by `test_websocketserver`.
- `tciprotocol.{cpp,h}` — TCI grammar (`name:arg,...;`) tokeniser and formatter. Pure text. Covered by `test_tciprotocol`.
- `tciaudioframe.{cpp,h}` — TCI binary audio frames: 64-byte header, RX_AUDIO / TX_AUDIO / TX_CHRONO encode and decode. Covered by `test_tciaudioframe`.
- `tciaudiobridge.{cpp,h}` — K4 12 kHz stereo receive audio → 48 kHz RX_AUDIO frames via `AudioUpsampler`. Skips all work while no client is subscribed. Covered by `test_tciaudiobridge`.
- `tciradiostate.h` — the snapshot the server answers from: two receivers (Main = 0, Sub = 1), fed by queued signals because `RadioState` is main-thread-only.
- `tciclientinfo.h` — one connected client as the TCI options page lists it.
- `tciserver.{cpp,h}` — the TCI server, dispatch/transport half: init burst, command dispatch, PTT ownership, TX_CHRONO pacing, audio. Covered by `test_tciserver`, `test_tciservertransmit`, `test_tciclients`.
- `tciserverstate.cpp` — the state-reporting half of `TciServer`: snapshot diffs, broadcasts, read-only queries, sensors. Covered by `test_tciqueries`, `test_tcisensors`.
- `tciserver_internal.h` — constants and helpers shared by the two `TciServer` halves. Not public.
- `cwmacro.{cpp,h}` — plans a TCI `cw_macros` message (speed markers, chunking) into K4 keying steps. `CatFrames::cwText` does the K4 spelling. Covered by `test_cwmacro`.

## Threading

- `TcpClient` lives on `ConnectionController::m_ioThread`.
- `DxClusterClient` lives on a per-instance `QThread` spawned by `DxClusterController::ensureInstance`.
- `KPA1500Client` lives on its owning `KPA1500UiController`'s thread (main).
- `TciServer`, its `WebSocketServer` and `TciAudioBridge` live on the `TCI` thread owned by `TciController`. State reaches them as queued snapshots; client requests come back as queued signals to the main thread.

## K4 framing

4-byte `START_MARKER` (0xFE 0xFD 0xFC 0xFB) + 4-byte big-endian length + payload + 4-byte `END_MARKER`. Parser keeps the last 3 bytes of any unparsed buffer tail so partial markers don't lose sync across `readyRead` calls.

## CatServer contract

`docs/radiostate-catserver-api-contract.md` lists the 22 `RadioState` getters that `catserver.cpp` depends on. No signature or semantic change without updating the contract. `test_catserver` (33 cases) catches violations.

## See also

- `docs/radiostate-catserver-api-contract.md` — pinned `RadioState` API.
- `docs/k4-protocol-quirks.md` — K4 CAT oddities, packet framing, and the ARP cold-start retry.

`TcpClient` runs on the I/O thread owned by `ConnectionController`, never on the main thread; CAT
writes reach it by queued connection. `CatServer` and `KPA1500Client` run on the main thread.
