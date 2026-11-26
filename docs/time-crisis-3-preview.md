# Time Crisis 3 development preview (not release-ready)

US retail SLUS-20645, CRC/UUID 7290669C, revision 1.00. New independent BSD-3-Clause implementation based on factual game observations documented in the separate TC3 research project. No GPL implementation copied, no original assets, RAM captures or save files included. Preserve upstream and dependency licenses.

This first module uses the game's existing six constant-output camera channels (mode 1), not an instruction NOP patch. Uses Acolyte's existing controller, playback, GUI and persistent PINE socket. Activate while the game is running; no ISO, save slots, frame advances or hotkeys. Activation reads two short original code spans for SHA-256 verification, checks the active primary context and packed track layout, and captures original selectors/constants. Motion writes at most six constants (18 data bytes) per changed target. Disable restores saved constants and selectors only while track and last-written values still match. A scene replacement stops control; manually re-enable in the next scene. No automatic scene handover.

This is a gameplay-camera override, not an independent spectator camera. Native shooting, culling, AI tracking and effects may be influenced. No freeze, FOV or HUD feature in this initial module. Do not fire while exploring. The original game has additional mode families that refuse admission. Supported contexts in Rescue/Crisis and arcade are not yet established. Stock PINE batches are not atomic with respect to EE execution: a frame or track transition can occur between reads and writes, and save-state loading with an identical track may evade detection. Failed writes may leave an override; restart game to recover if ownership or communication is uncertain. Closing the application attempts normal restoration if still active and the game identity matches, but power loss/disconnect cannot guarantee restoration. Do not load saves while enabled.

Build the existing solution with Visual Studio 2022 x64 Release, Windows 10+ SDK (BCryptHash). Copy upstream SDL2.dll and gamecontrollerdb.txt per README. No binary supplied or Windows compile/live test claimed. Development branch only; do not submit upstream before qualification.

Local model test command (Linux or other C++20 compiler):
    g++ -std=c++20 -Wall -Wextra -Werror tests/time_crisis_3_camera_test.cpp -o /tmp/tc3-camera-test
    /tmp/tc3-camera-test

Acceptance still pending: real PCSX2 2.0.2 and newer stock PINE, activation latency, sustained input cadence, output pose and visual correctness, native mode restoration without state rewind, natural track changes, resets and disconnects, all mission types, Direct3D renderer. Target <1s activation and 60–100 Hz control, not measured. No CI/CD.
