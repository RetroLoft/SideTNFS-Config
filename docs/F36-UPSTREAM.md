# F36 integration and F37 configuration candidate

These are coordinated source submissions to RetroLoft/SideTNFS-Firmware,
RetroLoft/SideTNFS-Gemdrive and RetroLoft/SideTNFS-Config. Firmware and
GEMDRIVE use the native protocol together; use the matching Config changes.
The legacy upstream build remains the default. Historical Falcon names in
build switches also cover the tested Mega STE setup. Native firmware is
restricted to Pico W / RP2040; Pico 2 W is not claimed.

F36 was hardware-tested by the user on Mega STE: HighWire launches from the
TNFS mapping after accepting its 0x17 PRG flags. Earlier checkpoints cover
Falcon/HDDRIVER/BlueSCSI coexistence and Mega STE standalone boot. These are
specific observations, not a claim that every configuration is validated.
F37 embeds Config rebased onto current upstream, retaining upstream's wait
notice and initial Wi-Fi setup alongside our cartridge transport and AES
window fixes. F37 settings/status/edit/save/reopen needs hardware validation.

Historical docs describe individual bring-up stages; this document states
current submission scope. Raw UART captures and old binary kits are excluded.

## Config scope

CPU-aware cartridge encoding (including the Falcon shift window and ST token
collision avoidance), classified diagnostics and Falcon AES window behavior.
The updated utility identifies itself as f37-a1. Upstream's wait dialog,
automatic Wi-Fi setup and documentation changes are preserved.

Build with the existing Makefile: make SIDETNFS.PRG, using
m68k-atari-mint-gcc and GEMlib. Do not use make all for build-only validation:
that target also installs to INSTALLDIR. The 68000 build succeeds with the
cached project toolchain. Companion firmware tests exercise encoded cartridge
requests against its parser. Hardware-test the rebased UI on Mega STE and
Falcon before marking the coordinated submission ready.
