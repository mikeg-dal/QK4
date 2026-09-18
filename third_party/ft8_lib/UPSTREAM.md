# Vendored codec

Source: https://github.com/kgoba/ft8_lib
Revision: `9fec6ca39886edbf96f4f5e71edc76da5074e871`
Retrieved: 2026-09-08. MIT license; see LICENSE and the FFT copyright notices.

Only the C codec, monitor and FFT sources are compiled. Upstream examples are
not executed. QK4 does not use the example decoder's fabricated SNR estimate.
Sync score remains separate. QK4's own `src/ft8/ft8snr.cpp` now supplies
automatic reports using the WSJT-X reference estimators; see
`docs/FT8_SNR_REFERENCE.md`. The vendored MIT codec is unchanged by that port.
No claim of WSJT-X sensitivity or advanced-mode parity is made.

Local portability change: the single `stpcpy` call in `ft8/message.c` uses
the library's equivalent `append_string` routine. This avoids an undeclared
POSIX function and its pointer-return ABI risk on MinGW.

