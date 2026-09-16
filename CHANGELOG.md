# Changelog

## [2.0.2] - 2026-09-16

### Fixed
- **The built-in updater.** The certificate bundle shipped all 121 root authorities Mozilla trusts, and the Vita's OpenSSL never got through them. The diagnostic added in 2.0.1 reported `PEM unreadable 0x2006C041 malloc failure`, which decodes as `BIO_new` failing to allocate — every certificate and every BIO takes a lock object as it is parsed, and the console runs out long before certificate 121. Not one certificate reached the trust store, which is why every possible cause arrived as the same "error adding trust anchors" message in 1.0.0, 2.0.0 and 2.0.1. The bundle now carries the 8 roots the update path actually needs, copied unchanged from the same Mozilla set: ISRG Root X1 and X2, USERTrust ECC and RSA, Sectigo Public Server Authentication Root E46 and R46, and DigiCert Global Root G2 and G3. It went from 191,850 bytes to 11,998.

### Changed
- `assets/cacert.pem` is deliberately no longer the full Mozilla bundle. It verifies `github.com`, `api.github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com` and `codeload.github.com` — every host the updater contacts — and nothing else. If GitHub ever moves to an authority outside that set the updater will stop working and a VPK will have to be installed by hand, so each authority's sibling roots ship alongside the ones in use to leave room for a rotation.

## [2.0.1] - 2026-09-16

### Fixed
- The update error now says what OpenSSL made of the certificate bundle, and says it first. *Check for Updates* still failed on a real Vita after 2.0.0, but the message changed from "error adding trust anchors from locations" to "from certificate **blob**". That wording proves the 2.0.0 change does what it was meant to — the game reads the bundle itself and hands it to curl in memory — and that opening `app0:` was never the cause. The failure is further on, where OpenSSL loads the certificates into its trust store, and curl collapses every possible cause there into a single number. The error now begins with how many certificates parsed, how many were accepted, and the OpenSSL error behind the first rejection.
- Notes on the end of an error message were never visible. Errors are drawn as three wrapped lines with the remainder dropped and no ellipsis, so the CA note 2.0.0 appended *after* a long TLS message ran off the bottom of the box. Any such note now leads the message.

### Known issues
- **The built-in updater still fails on hardware.** This release does not fix it — it makes the cause visible so the next one can. Install this VPK by hand: scan the QR code in the README with VitaShell, or copy the VPK across and install it.

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
- *Check for Updates* failed on a real Vita with "Problem with the SSL CA cert (path? access rights?) ... error adding trust anchors from locations: CAfile: app0:assets/cacert.pem CApath: none". The CA bundle is now read into memory by the game and handed to curl as a certificate blob, so the TLS layer never opens `app0:` itself. If that read fails, the old file path is still used and the on-screen error says which of the two was in use. Present in 1.0.0; it only shows on hardware, which is why the emulator never caught it.
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
