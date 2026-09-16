# Changelog

## [2.0.0] - 2026-09-16

### Added
- Completion screen when a vault is cleared, showing your time, your best time and a **New best!** marker. Choose *Next Level*, *Level Select* or *Quit*. The first clear of a vault always counts as a new best.
- Best times are stored per vault in the save file and shown on the completion screen.
- Sound, generated in code rather than shipped as audio files: a rolling bed that gets louder the faster the ball moves, wall knocks scaled by impact, a pit-fall drop and a goal chime.
- Tilt indicator on the calibration screen, so you can see the pose being read before you commit to it.
- Screen flash when a vault is cleared.
- Run timer on the top bar, centred, formatted `mm:ss.cc`.

### Changed
- *Recalibrate* in the pause menu now resets the ball and asks you to place the Vita on a flat surface, then calibrates to that surface when you press Cross. Circle backs out without recalibrating. Once you have set a flat pose it is kept for the rest of the session, so vaults stop asking you to hold a pose — it is not written to the save file, so it resets when you quit the game.
- Calibration averages several motion samples instead of taking one, so a single noisy reading no longer sets a crooked neutral.
- A vault whose level data fails to load now reads **Unavailable** on Level Select, not *Locked*, with "This vault's data failed to load" underneath. It cannot be started.
- The timer resets to zero when the ball falls into a pit.
- The maze is drawn once per vault into a cached texture instead of every frame.
- The VPK version is derived from `GV_VERSION` in `src/version.h` instead of being hardcoded in `CMakeLists.txt`.

### Fixed
- The green cursor border was suppressed on greyed Level Select tiles, so moving the cursor onto a locked or unavailable vault made the selection invisible.

### Save format
- Save format version 3, 96 bytes: magic `GYRV`, version, unlocked count, reserved byte, completed mask, 20 best times in centiseconds, CRC32. Version 1 and 2 saves are migrated on load and keep their progress; they have no recorded times, so every vault's first clear after upgrading is a new best.

## [1.0.0] - 2026-09-15

First release.

### Added
- Tilt-controlled steel ball physics using the Vita's motion sensor, with a calibration hold at the start of each vault.
- 20 distinct vaults, ordered from easy to hardest.
- Main menu, 5 × 4 Level Select grid and pause menu (Resume / Recalibrate / Quit to menu).
- Saved progress with a CRC check. Cleared vaults stay playable.
- Built-in updater that checks GitHub releases, then downloads and installs the new VPK.
