# network/

TCP / TLS / PSK transport to the K4, binary protocol framing, CAT server for third-party apps, DX cluster client, KPA1500 amplifier client, K4 mDNS discovery.

## Files

- `tcpclient.{cpp,h}` — TLS/PSK socket to K4. State machine with ARP cold-start retry. `sendCAT` + `sendRaw` public entry points.
- `protocol.{cpp,h}` — K4 binary packet framing (`START_MARKER` + big-endian length + `END_MARKER`), payload type dispatch, audio + miniPAN + spectrum routing.
- `catserver.{cpp,h}` — TCP CAT server for WSJT-X / MacLoggerDX integration. Pinned public API via `docs/radiostate-catserver-api-contract.md`, regression-gated by `test_catserver`.
- `networkmetrics.{cpp,h}` — Latency / throughput aggregation. Feeds `NetHealthWidget`.
- `k4discovery.{cpp,h}` — UDP mDNS discovery for K4 servers on the LAN.
- `catframes.{h}` — `CatFrames::` namespace of small builders returning the exact `QByteArray` for a CAT command (`frequencyA`, `modeA`, …). Keeps command spelling in one place instead of scattered string literals.
- `catpushbroadcaster.{cpp,h}` — auto-information push for CAT clients. Tracks per-client AI level, subscribes to `RadioState` changes, and pushes updates to subscribers while polling clients receive nothing. Covered by `test_catpushbroadcaster`.
- `dxclusterclient.{cpp,h}` — TCP client to a single DX cluster node. Used by `DxClusterController` — one instance per configured cluster. Receive buffer capped at 64KB; overflow disconnects.
- `kpa1500client.{cpp,h}` — TCP client to the KPA1500 amplifier. Receive buffer capped at 64KB; overflow disconnects.

## Threading

- `TcpClient` lives on `ConnectionController::m_ioThread`.
- `DxClusterClient` lives on a per-instance `QThread` spawned by `DxClusterController::ensureInstance`.
- `KPA1500Client` lives on its owning `KPA1500UiController`'s thread (main).

## K4 framing

4-byte `START_MARKER` (0xFE 0xFD 0xFC 0xFB) + 4-byte big-endian length + payload + 4-byte `END_MARKER`. Parser keeps the last 3 bytes of any unparsed buffer tail so partial markers don't lose sync across `readyRead` calls.

## CatServer contract

`docs/radiostate-catserver-api-contract.md` lists the 22 `RadioState` getters that `catserver.cpp` depends on. No signature or semantic change without updating the contract. `test_catserver` (33 cases) catches violations.

## See also

- `docs/radiostate-catserver-api-contract.md` — pinned `RadioState` API.
- `docs/k4-protocol-quirks.md` — K4 CAT oddities, packet framing, and the ARP cold-start retry.

`TcpClient` runs on the I/O thread owned by `ConnectionController`, never on the main thread; CAT
writes reach it by queued connection. `CatServer` and `KPA1500Client` run on the main thread.
