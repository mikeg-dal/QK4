# Paddle keyer golden runs

These `summary_*.json` files come unchanged from `keying_simulations/` in Dale Farnsworth's
[K4-Companion](https://github.com/DaleFarnsworth/K4-Companion) (GPL-3.0), commit `040f711`
(v2.2, 2026-10-25). Each one records one sweep of his `paddle_sweep.py` harness against his
keyer: a paddle scenario, a speed, and for every run the exact commands the K4 was sent.

`tests/test_paddlekeyersweeps.cpp` drives QK4's port (`src/hardware/paddlekeyer.*`,
`src/audio/sidetonescheduler.*`) through the same scenarios on the same virtual clock (48 kHz,
384-frame blocks, 16 ms lead, 0.1 ms device-to-keyer hop) and requires the same commands, byte
for byte.

| File | Scenario | Runs |
|---|---|---|
| `summary_30wpm_squeeze.json`, `summary_40wpm_squeeze.json` | both paddles closed together, released together at the swept moment | 125, 94 |
| `summary_30wpm_swap.json`, `summary_40wpm_swap.json` | dit released and dah closed at the same instant | 21, 16 |
| `summary_30wpm_dit-tap.json`, `summary_40wpm_dit-tap.json` | dah held, dit tapped for 5 ms | 56, 42 |
| `summary_40wpm_tap.json` | dit held, dah tapped for 5 ms | 16 |
| `summary_30wpm_dah-first.json`, `summary_40wpm_dah-first.json` | dah closed first, dit after it, both held | 10, 8 |
| `summary_30wpm_stall.json`, `summary_40wpm_stall.json` | S then T over MoMIDI with the process stalled | 101, 75 |

564 runs in all. Recount with `jq -s 'map(length) | add' tests/data/paddlekeyer/summary_*.json`.

All were recorded with Iambic B, Dit Mem 0 %, and the K4's monitor mute on. That's why every
burst carries `ML0000;` after its first `KZD`.

Where a squeeze is released on the exact instant a tone starts, Dale's notes allow either answer
(see `squeeze_verdict` in his harness). The test does the same for those runs. It accepts the
recorded commands, or failing that, the Iambic B rule itself.
