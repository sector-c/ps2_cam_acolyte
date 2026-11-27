# TC3 Producer Hook P1 verification tool

Owner-tested standalone console instrument, not the full Acolyte GUI. This is the baseline test package for the current TC3 module. The exact distributed archive was reopened and all 11 SHA256SUMS entries and ZIP CRCs verified on 2026-11-26. No rebuild or modification of the executable was made.

| Item | Value |
| --- | --- |
| Package | TC3-Producer-Hook-P1.zip |
| Package SHA-256 | 1d219f3153203c698dbb1052952daa51f03969591e50ed05120c28f9f7e4f725 |
| Executable | TC3-Producer-Hook-P1.exe |
| Executable size | 2343936 bytes |
| Executable SHA-256 | 5b39dcb30ff5bab7b6b466d78c165bf95b2c094b833696074ac181fefa033232 |
| Platform | PE32+ Windows x64 console |
| Acceptance | Owner reports P1 seems to work; exact live scope unrecorded |

## Package placement and status

Place the original executable, six Go source files under source/, README and original notices under licenses/ in this directory. SHA256SUMS.txt records the exact package inventory. **Binary/source included:** exact original archive payloads verified against SHA256SUMS.txt before staging. The executable is the owner-tested P1 artifact, not a rebuild. Do not commit runtime logs, game data, original ELF, ISO, saves, BIOS or captures.

The source is BSD-3-Clause; the executable includes Go runtime code with its separate license and patent notice. Keep all notices distributed with the executable. No proprietary game data in the package. The private original ELF fixture is not distributed; optional tests skip without it.

Run with stock PCSX2 PINE port 28011 and US retail TC3. Close older camera tools; reset TC3 if a prior override remains. Type enable, movement commands, disable, quit. Native freeze is intended to work; emulator itself must run. Do not shoot, load/save state, switch modes or use multiplayer while enabled. The selected primary call is restored on normal disable; recover is for exact abandoned P1 patches only. See [design](../../docs/time-crisis-3-producer-hook.md). 
