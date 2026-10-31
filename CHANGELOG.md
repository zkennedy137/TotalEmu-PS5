# Changelog

All notable changes to PS Launcher (title ID PPSA97500) are listed here, newest first.

## [v1.6.6] - ccfebca4 (helper 2.9 unchanged, 65549bc9)

Not yet tested on the console. Only the Credits page and the license credits changed; the rest of the build is the same as v1.6.5.

### Added
- CREDITS.md in the project root: every component, data source and license in the build, with the attribution PSSwanStation's additional GPL term asks for.
- Settings > Credits: GameDB-PSX by niemasd (PS1 game titles, GPL-3.0), and lapy.elf by BlackBearReloaded and ArkSama (MIT).

### Changed
- Version 1.6.6 (kVersion).

## [v1.6.5] - eaa643e6 (helper 2.9 unchanged, 65549bc9)

Tested on the console: a PS5 game disc is read and listed as "Unidentified disc" on the PS5 tab (main.log, 23:02 run). The rest of this entry is not yet tested on the console, except where the user reports otherwise. This build holds everything from v1.6.4, with the build number moved to 1.6.5; the v1.6.4 entries are repeated below.

### Added
- PS4 and PS5 tabs. assets/launcher.ini has [system.ps4] and [system.ps5] sections. Copy the new launcher.ini to /data/PSLauncher to get the tabs.
- PS5 title_id in launcher.ini (for example title_id=PPSA01234): the PS5 tab lists that installed title as "PS5 game PPSA01234", and Cross starts it through the helper (LAUNCH). Needs the helper running. Leave it empty for no entry.
- Blu-ray discs (up to about 120 GB) are read. Before, a PS5 disc was rejected as "not a CD" and never listed.
- A disc with no ISO 9660 volume is listed on the PS4 and PS5 tabs, not offered the PS1 install.
- A disc with no SYSTEM.CNF in its root and no PS3_GAME is listed as "Unidentified disc" on the PS4 and PS5 tabs. Its root entries are logged as "disc check: root entry" in main.log. It cannot be started or installed yet.
- assets/gamedb/psx.tsv: PS1 titles and Redump-style names for 11,753 serials, from GameDB-PSX by niemasd (GPL-3.0). About 1,950 of these serials were not in the PS1 lists before.

### Changed
- Such discs are no longer offered the PS1 install prompt or listed on the PS1 tab.
- PS1 cover lookups: the GameDB-PSX title is the name sent to TheGamesDB and RAWG, and its release name is tried for libretro when Redump's disc list has no entry. Order is unchanged: libretro, then TheGamesDB, then RAWG.
- PS1 and PS2 games: the space around the cover, from the library panel's edge to the right side of the screen, is filled with the same cover, dimmed. The cover itself is still drawn as large as fits without distortion, on top of it.
- PS1 and PS2 games with no cover: that space is filled with the panel colour, so it reads as one panel. PS3 is unchanged.

## [v1.6.4] - 033a79c7 (helper 2.9 unchanged, 65549bc9)

Work in progress: not tested yet.

### Added
- PS4 and PS5 tabs. assets/launcher.ini has [system.ps4] and [system.ps5] sections. Copy the new launcher.ini to /data/PSLauncher to get the tabs.
- PS5 title_id in launcher.ini (for example title_id=PPSA01234): the PS5 tab lists that installed title as "PS5 game PPSA01234", and Cross starts it through the helper (LAUNCH). Needs the helper running. Leave it empty for no entry.
- Blu-ray discs (up to about 120 GB) are read. Before, a PS5 disc was rejected as "not a CD" and never listed.
- A disc with no ISO 9660 volume is listed on the PS4 and PS5 tabs, not offered the PS1 install.
- A disc with no SYSTEM.CNF in its root and no PS3_GAME is listed as "Unidentified disc" on the PS4 and PS5 tabs. Its root entries are logged as "disc check: root entry" in main.log. It cannot be started or installed yet.
- assets/gamedb/psx.tsv: PS1 titles and Redump-style names for 11,753 serials, from GameDB-PSX by niemasd (GPL-3.0). About 1,950 of these serials were not in the PS1 lists before.

### Changed
- Such discs are no longer offered the PS1 install prompt or listed on the PS1 tab.
- PS1 cover lookups: the GameDB-PSX title is the name sent to TheGamesDB and RAWG, and its release name is tried for libretro when Redump's disc list has no entry. Order is unchanged: libretro, then TheGamesDB, then RAWG.
- PS1 and PS2 games: the space around the cover, from the library panel's edge to the right side of the screen, is filled with the same cover, dimmed. The cover itself is still drawn as large as fits without distortion, on top of it.
- PS1 and PS2 games with no cover: that space is filled with the panel colour, so it reads as one panel. PS3 is unchanged.

## [v1.6.3] - c0e545a1 (helper 2.9 unchanged, 65549bc9)

Work in progress: the Settings changes are not tested yet.

### Changed
- Settings: "Check the drives again" is removed. Square (rescan) still checks the drives.
- Settings shows nine rows at a time and scrolls when there are more.
- Settings order: Cover Art, Disc Options, Disable Drive Check on Startup, Debug (when on), Version, Credits, Back. Credits is at the bottom, above Back; Version is just above it.
- "Download Cover Art" is renamed "Cover Art". Its page has the four downloads, "Use API for Cover Art Download" and "API to Use:".
- New "Disc Options" page, which holds "When a disc is inserted:".

## [v1.6.2] - dafd24fe (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups and the new cover settings are not tested yet.

### Added
- Settings > Use API for Cover Art Download (On/Off). On sets UseApi=1 in launcher.ini; Off sets UseApi=0. The default is 0.
- When it is On, Settings has a row API to Use: (libretro, TheGamesDB, RAWG or all). It is saved as ApiToUse in launcher.ini, and the default is all.

### Changed
- Cover downloads use libretro only while UseApi is 0. While it is 1, the source chosen in API to Use is used: one database, or all of them in turn (libretro, then TheGamesDB, then RAWG, each only if its key is set in launcher.ini).

## [v1.6.1] - 9b6ebc3d (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups, the Debug Menu, the drive check setting and the controls popup are not tested yet.

### Added
- A tap on the touchpad (pressed and let go within 400 ms) opens a Controls popup over the library, and another tap or Circle closes it. The popup shows the arrows and face-button pictures side by side, and the text for L1/R1, R2, Options and Hold Touchpad. The game list shows 15 lines (was 13), each 44 pixels tall (was 50), so the list and the bar both fit. The pictures are twice the size they were, and they are the updated arrows and face-button images. The popup ends with "Press Circle to close". Cross closes it without starting a game.

### Changed
- The button bar shows one line: "Tap Touchpad to See Controls". The pictures and the text for L1/R1, R2, Options and Hold Touchpad are no longer in the bar.

## [v1.5.9] - 0971f0ca (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups, the Debug Menu, the drive check setting and the button bar are not tested yet.

### Added
- The button bar shows the arrows picture (assets/images/arrows.png) on its left and the face-button picture (assets/images/cstc.png) on its right. The background of each is made transparent, whether it is white or black. Each keeps its own proportions, at the same height. The two pictures in the package are the full-size arrows and face-button images from the latest upload, kept as PNG (assets/images/arrows.png and assets/images/cstc.png).

### Changed
- The bar has no text: the arrows and face-button text, and L1/R1, R2, Options and Hold Touchpad, are all gone from it.
- The Debug row blinks five times, 0.3 seconds per blink, in the selection colour, when it has just been turned on.
- If the Debug row is still blinking when it is opened, the blinking stops.

## [v1.5.7] - 42c9375b (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups, the Debug Menu and the drive check setting are not tested yet.

### Changed
- Same as v1.5.6: the button bar is the text line again, and the list shows 13 rows. The button pictures tried in an earlier build were removed.
- The app is called TotalEmu, as in v1.5.5 and v1.5.6.

## [v1.5.6] - f265595d (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups, the Debug Menu and the drive check setting are not tested yet.

### Added
- Settings > Disable Drive Check on Startup (On/Off). On sets DC_onstartup=1 in launcher.ini, which skips the wait for the drives when the launcher starts. Off (the default, DC_onstartup=0) waits as before. The disc watcher still runs, so a disc put in later is still found.

### Changed
- The Debug row flashes twice, 0.3 seconds each, in the selection colour, when it has just been turned on. It used to stay highlighted for 2 seconds.

## [v1.5.5] - 96797016 (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups, the Debug Menu and the rename are not tested yet.

### Added
- A hidden Debug row in Settings. Press L2, R2, L2, R2 on the Version page to turn it on (Debug_Menu=1 in launcher.ini). The user is taken to Settings, and the Debug row flashes for two seconds the first time.
- Settings > Debug opens the Debug Menu:
  - Reset All Settings erases launcher.ini and writes the stock one from the app's bundled copy. The first Cross asks for a second Cross to confirm. It also turns the Debug row off, as the stock file has Debug_Menu=0.
  - Enable http-probe (On/Off) runs the network test when the launcher starts (http_probe=1 in launcher.ini).
  - Disable Debug Mode closes Settings, returns to the library, and sets Debug_Menu=0 in launcher.ini.
  - Back returns to Settings.
- launcher.ini has the two flags, Debug_Menu=0 and http_probe=0, under [launcher].

### Changed
- The network test at startup follows http_probe in launcher.ini. Before, it ran whenever the empty file /data/PSLauncher/http-probe existed, and that file is no longer checked. The startup delay stops unless http_probe is on.
- The Run in Debug Mode row and its debug.ini file from the earlier build are gone.
- The app is called TotalEmu: the home-screen name and the icon's label (the splash uses the same icon), the menu header, the Version and Credits pages, and the window title. The title ID, the data folder and the file names still say PSLauncher or PPSA97500 for now.
- The names the launcher shows for games are unchanged.

## [v1.5.4] - 6eb86eb1 (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups are not tested yet.

### Added
- PS2 titles from GameDB-PS2 by niemasd (GPL-3.0), in assets/gamedb2/gamedb-ps2.tsv, by serial. The title is used to search TheGamesDB and RAWG for a cover. Credited on the Credits page and in docs/THIRD-PARTY.txt.

### Changed
- The app is called TotalEmu: the home-screen name and the icon's label (the splash uses the same icon), the menu header, the Version and Credits pages, and the window title. The title ID, the data folder and the file names still say PSLauncher or PPSA97500 for now.
- The names the launcher shows for games are unchanged.

## [v1.5.3] - 16245b55 (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups are not tested yet.

### Changed
- Covers are looked up by the disc's name as Redump (and libretro) spell it, found from the game's serial in the disc list (assets/gamedb). The file name keeps the language tags that libretro's file names have, for example "Jak and Daxter - The Precursor Legacy (USA) (En,Fr,De,Es,It)". The title is the fallback, as before.

## [v1.5.2] - c84fcd84 (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups are not tested yet.

### Fixed
- Jak and Daxter (USA, serial SCUS-97124) is named "Jak and Daxter - The Precursor Legacy (USA)" in the PS2 list (assets/gamedb2/games.tsv), as the disc list already has it. Its name was "Jak & Daxter". No other name in the list was changed.

## [v1.5.1] - 9af87483 (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups are not tested yet.

### Added
- Settings > Download Cover Art opens a page with Download PS1 Cover Art, Download PS2 Cover Art, Download PS3 Cover Art and Download Cover Art for All Systems.
- Settings > Credits lists the emulators the launcher uses and their authors, the libraries and tools built into it, the game data and the APIs, with the PSSwanStation credit that its license requires.
- Settings > Version shows the version of the build.

### Changed
- The Credits page names the authors of PS5SX2 (Swordpdf, a port of PCSX2) and RPCS3-5 (kamb205, KB-RPCS3, a port of RPCS3).
- Settings rows: When a disc is inserted, Check the drives again, Download Cover Art, Credits, Version, Back. The credit lines at the bottom of the Settings screen are gone; they are on the Credits page.

## [v1.5.0] - f039a10c (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups are not tested yet.

### Added
- R2 downloads the cover of the selected game, as the Share button does.

### Changed
- The right field is the game's cover. The name, emulator title, BIOS line, game count, "Opens to its own menu" and the Circle line are gone from it.
- The cover field is wider (680 pixels, was 610). Every cover is fitted to the field at its own proportions, so it is never distorted. Nothing is drawn behind a cover, so it shows on its own.
- The game count is under the game list, with one empty line below it as padding.
- Circle: Install is in the controls at the bottom, greyed out when the selected game is not a disc. While an install runs it reads Circle: Cancel install.
- A missing BIOS, and a helper that is not running, are shown in the status line when the system is opened, not in the info panel.
- The game list shows 13 rows, one fewer than before, to make room for the count.

## [v1.4.9] - d6ee6969 (helper 2.9 unchanged, 65549bc9)

Work in progress: the cover lookups are not tested yet. Changes since v1.4.8, the last tested version.

### Fixed
- Covers were saved to `/data/PSLaunchercovers/`, not `/data/PSLauncher/covers/`, because the data folder's path had no `/` before `covers`. Covers now go to `/data/PSLauncher/covers/`.
- At startup, covers that v1.4.8 saved in `/data/PSLaunchercovers/` are moved into `/data/PSLauncher/covers/` (their ps1, ps2 and ps3 folders). A file already in the new folder is left where it is, and the old folder is not deleted. The result is written to main.log.

### Changed
- The TheGamesDB lookup uses the `/v1.1/Games/ByGameName` endpoint, the one that returns box art. The platform IDs are not checked against TheGamesDB yet.
- `/data/PSLauncher/covers.log` lists each game that has no cover, one line each, with why each source failed. Games that get a cover are not logged.

## [v1.4.8] - dc3638c3 (helper 2.9 unchanged, 65549bc9)

Tested on the console: the PS1 and PS2 covers downloaded, but to the wrong folder (fixed in v1.4.9). The PS3 and key-based lookups were not tested.

### Added
- Game covers. A cover is kept in covers/<platform>/ in the data folder, named after the game's title, as the PNG or JPEG it was downloaded as. The selected game's cover shows under the info panel.
- The Share button downloads the cover of the selected game.
- Settings: "Download covers" for PS1, PS2, PS3, and for all systems. Each runs in the background, one system at a time, and skips the games that already have a cover. The result is shown in the status line.
- Covers come from libretro's thumbnails first (no key). When the user's key is set in launcher.ini, TheGamesDB (`thegamesdb_key`) and then RAWG (`rawg_key`) are tried for the games libretro has no cover for. The reason each source failed is shown in the status line.
- A credits line on the settings screen: libretro thumbnails, TheGamesDB, RAWG and GameTDB.

## [v1.4.7] - 8de8d69d (helper 2.9 unchanged, 65549bc9)

Work in progress: the network test is not tested yet.

### Added
- A TLS client for HTTPS, for the network test. When the console's own client refuses a certificate (0x8095f00c, which it does after the sandbox elevator has run), the test makes the request again with mbedTLS 3.6.7, checking certificates against the console's list. The code is in src/tls/, from PSSwanStation's, with mbedTLS (Apache-2.0) in src/tls/mbedtls/. Both are credited in docs/THIRD-PARTY.txt.

## [v1.4.6] - f302dabe (helper 2.9 unchanged, 65549bc9)

Work in progress: the items below are not tested yet.

### Added
- An `[api]` section in launcher.ini, with `thegamesdb_key` and `rawg_key`. Each user enters their own keys; the app reads them but doesn't use them yet. The template is assets/launcher.ini; the console's /data/PSLauncher/launcher.ini needs the same section.
- A network test. If an empty file named `http-probe` exists in /data/PSLauncher, the launcher makes one HTTPS request to https://api.thegamesdb.net/ at startup and logs the result to main.log (`http probe: ...`). Nothing else uses the network yet. The first test build (93b74251) could not open the Sony network modules, because the launcher never called them; this build calls them directly, so they load.

## [v1.4.5] - f38e97a6 (helper 2.9 unchanged, 65549bc9)

### Added
- PS3 titles come from GameTDB's PS3 list (assets/gamedb/ps3.tsv, 10,766 serials), for ISOs, game folders and discs. The title in PARAM.SFO is used when the serial is not listed. Attribution is in docs/THIRD-PARTY.txt.

## [v1.4.4] - 1f2785df (helper 2.9 unchanged, 65549bc9)

### Changed
- PS3 games and discs show their region after the title, as the PS1 and PS2 names do: "Guitar Hero 5 (USA)". The region comes from the serial (BLUS and BCUS are USA, BLES and BCES are Europe, BLJM, BLJS, BCJB and BCJS are Japan). A serial with another prefix shows the title alone.

## [v1.4.3] - 27dc39ef (helper 2.9 unchanged, 65549bc9)

### Changed
- A disc in the drive is listed by the database's name, as the installed games are (for example "Star Wars - Demolition (USA)"). The serial is shown only for a disc that is not in the database.
- A PS3 disc is listed by its title from PARAM.SFO, without a serial.

## [v1.4.1] - 88709622 (helper 2.9 unchanged, 65549bc9)

### Changed
- PS1 games are named from the serial in `SYSTEM.CNF` (the `BOOT =` line) on the data track of their `.cue`'s `.bin`, looked up in the PS1 list. A raw `.bin` (2352-byte sectors) is read past its sector header. The file or folder name is still shown when the serial is not in the list or the disc cannot be read.

## [v1.4.0] - 0a5664f7 (helper 2.9 unchanged, 65549bc9)

### Changed
- PS2 ISOs are named from the game's serial in their `SYSTEM.CNF` (the `BOOT2` line), looked up in the PS2 list. The file name is still shown when the serial is not in the list or the file cannot be read. Only the bytes needed are read from the ISO.

## [v1.3.1] - 2cb0fefe (helper 2.9 unchanged, 65549bc9)

### Fixed
- A PS3 title with bytes the launcher's font cannot draw (for example UTF-8 marks) showed as noise. The title now keeps printable ASCII only, with the ends trimmed. If nothing printable is left, the file name is shown.

## [v1.3.0] - 21151475 (helper 2.9 unchanged, 65549bc9)

### Changed
- PS3 games are named by the title in their `PS3_GAME/PARAM.SFO`, read from the ISO or the game folder, instead of the file name. The file name is still shown when the title cannot be read. PARAM.SFO is plain text, so no key is needed. Only the bytes needed are read from the ISO.

## [v1.2.9] - 947154fd (helper 2.9 unchanged, 65549bc9)

### Fixed
- A PS1 or PS3 game that is in a game folder and also as an ISO in the same library folder now shows once, as the ISO. Before, both showed. The disc in the drive still shows even when an installed copy of the game exists. PS2 is unchanged, since its games are ISOs only.
- PS1 games are listed from game folders and `.cue` files only. PS1 does not play ISO files, so `.iso`, `.chd`, `.pbp`, `.img`, `.mdf` and `.ccd` are no longer listed for PS1. This is a change to `assets/launcher.ini`; the eboot is unchanged.

## [helper v2.9] - 65549bc9 (app unchanged, 686d6923)

### Added
- `FILESIZE <path>` and `FILEREAD <path> <offset> <n>`: read-only access to the PS3 games under `/data/PSLauncher/games/`, for the RPCS3 title, which cannot read that folder itself. A `FILEREAD` returns at most 16 MB, streamed through a 64 KB buffer, and the reply says how many bytes follow. Paths outside the games folder, and paths with `..`, are refused.

## [helper v2.8] - fa0a679c (app unchanged, 686d6923)

### Changed
- The memory line in `helper.log`, every 10 seconds, now comes from the PS5 kernel calls `sceKernelAvailableFlexibleMemorySize`, `sceKernelConfiguredFlexibleMemorySize` and `sceKernelGetDirectMemorySize` (loaded from `libkernel.sprx`). The flexible budget is the pool that anonymous mappings and libc draw on. The BSD VM counters from 2.7 are not on the console, so they are gone.

## [helper v2.7] - 38c194a4 (app unchanged, 686d6923)

### Added
- Memory in `helper.log`, every 10 seconds: free and total memory from the kernel's VM counters (`vm.stats.vm.v_free_count`, `vm.stats.vm.v_page_count`, `hw.pagesize`), and the physical memory (`hw.physmem`). If a counter is missing on the console, the log says so. This is for sizing the disc and file read buffers.

## [helper v2.6] - 048db101 (app unchanged, 686d6923)

### Changed
- The helper finds an old copy of itself by its process ID and start time, recorded in `helper.pid` as `<pid> <start seconds> <start microseconds>`. It stops that process only when both match. Before, it stopped any process named `payload` or `helper` at the recorded ID, and Payload Manager runs every payload as `payload.elf`, so the name could match another process.
- A `helper.pid` written by 2.5 has no start time. In that case the helper stops the old copy once, by name, as 2.5 did.

## [helper v2.5] - 738e7b60 (app unchanged, 686d6923)

### Fixed
- A launch that the system accepts is not repeated. Before, only a return of `0` stopped the helper, so a launch returning another value was called again, up to 8 times. The title could be started, closed and started again. Errors (top bit set, `0x8xxxxxxx`) are still retried.

## [helper v2.4] - 79c56b26 (app unchanged, 686d6923)

### Added
- `DISCSIZE <device>` and `DISCREAD <device> <lba> <n>` commands: the media size of a `/dev/cdN` drive, and up to 64 sectors of it (2048 bytes each), so the RPCS3 title can read discs it cannot open itself.

## [v1.2.8] - 686d6923

### Changed
- A PS3 disc is started as `--boot /dev/cd0` again, instead of `--boot /mnt/disc` (v1.2.4 to v1.2.7). The `/mnt/disc` mount check is removed with it.

## [v1.2.7] - 055fbcc3

### Added
- Settings has "Check the drives again". It clears what the launcher remembers about the drives and looks at them afresh, so a USB drive plugged in after PS Launcher started is found, and a drive that was rejected earlier is tried again. Square (rescan) does the same.

## [v1.2.6] - f6177382

### Added
- Install a PS3 disc from the disc menu (Install row). The disc is copied to `/data/PSLauncher/games/ps3/<Title> [SERIAL].iso` and decrypted: its encrypted sectors are decrypted with the key from RPCS3-5's `data/redump` folder that matches the disc (the first key that decrypts `LIC.DAT` and `EBOOT.BIN`). The copy is checked before it is kept. The copy is written as `<name>.iso.part` and renamed when done.
- The AES code is RPCS3's (PolarSSL, GPL-2.0-only), built without its AES-NI path.

## [v1.2.5] - 2ab09d13

### Added
- PS2 and PS3 lists use the PS1 layout: discs in the drives come first, under a "Disc:" heading, then the installed games under "Installed:".
- A PS3 disc is named by its game title from PARAM.SFO (for example "Guitar Hero 5 [BLUS-30292]"), instead of "PlayStation disc".

## [v1.2.4] - 3a61b804

### Changed
- A PS3 disc is started as `--boot /mnt/disc` (the folder the file manager mounts the disc at), instead of `--boot /dev/cd0`. Test build: if `/mnt/disc/PS3_GAME` isn't there, the launcher asks for the disc to be mounted first.

## [v1.2.3] - 34135068

### Added
- The log now says which step failed when a PS3 disc can't be identified: the ISO 9660 volume at sector 16, the PS3_GAME folder, PARAM.SFO, or its TITLE_ID.

## [v1.2.2] - 39ba8c74

### Fixed
- A disc whose table of contents the drive won't give (such as a PS3 Blu-ray) is no longer treated as "no disc". It is identified from the media's size instead, so it can be listed when PS Launcher starts or on rescan. Discs whose table of contents reads normally are unchanged.

## [v1.2.1] - 0951a06c

### Fixed
- Rescan (Square, or R on a keyboard) now re-reads both drives, so a disc that went in while PS Launcher wasn't watching is listed without restarting the launcher.

## [v1.2.0] - 66bfa8f7

### Changed
- RPCS3-5's "Automatically start games after boot" is turned off when PS Launcher starts, rather than before each PS3 launch. The helper (v2.3) turns it back on after a game started from PS Launcher closes.

## [v1.1.9] - 5d3ca382

### Added
- Before PS Launcher starts RPCS3-5, it turns off RPCS3-5's "Automatically start games after boot" in `/data/homebrew/PPSA99303/rpcs3/config.yml` (the PS3 entry's `config_file` setting), so a disc in the drive isn't started when RPCS3-5 opens. Games started from PS Launcher still start, since they're named with `--boot`.

## [v1.1.8] - a1ee2a16

### Added
- PS3 discs in the drive are listed under PlayStation 3 (named from their serial, read from `PS3_GAME/PARAM.SFO`). Cross on one starts it from the drive in RPCS3-5 (`--boot /dev/cdN`). PS3 discs don't get the insert prompt; their menu offers play from the disc or not now. Installing PS3 discs isn't supported yet.

## [v1.1.7] - e994309c

### Fixed
- A PS3 game in both `/data/PSLauncher/games/ps3` and RPCS3-5's games folder shows only the copy in `/data/PSLauncher/games/ps3` (matched by serial, as PS1 is).

## [v1.1.6] - fe38680e

### Added
- PS3 games are listed under PlayStation 3 from RPCS3-5's own games folder (`/data/homebrew/PPSA99303/rpcs3/games`, disc images as `.iso` or folders with `PS3_GAME`). Cross starts the game in RPCS3-5 with `--boot <path>`.
- The default PS3 games folders include `/data/PSLauncher/games/ps3` as well as RPCS3-5's own games folder.

## [v1.1.5] - 68fa56b2

### Added
- PS2 games: the PS2 disc's name comes from its serial in the PS2 game database (`Jak and Daxter: The Precursor Legacy [SCUS-97124]`).
- PS2 disc menu: Triangle on a PS2 disc opens the disc menu with play from the disc, install, and not now. Hold Triangle to eject, as on PS1.
- Install a PS2 disc: copied as an ISO, streamed, to `/data/PSLauncher/games/ps2/<name>/<name>.iso` through a `.part` file, with progress and cancel. Cancelling removes the partial files.

### Fixed
- The PS2 disc check reads the disc's user data directly, so the PS2 disc in the internal drive is recognised (v1.1.4).

## [v1.1.4] - 550d85cd

### Fixed
- The PS2 disc check reads the disc's user data directly (the same read PS5SX2 uses) instead of the reader's rebuilt sectors. The diagnostic showed the volume label is read correctly on the internal drive, so a PS2 disc in the internal drive is now recognised and listed under PlayStation 2.

## [v1.1.3] - 705dd2b3

### Changed
- Diagnostic logging for disc sector 16: each disc check logs what the device read (`pread`) and what READ(10) through the pass-through device return, to find why the PS2 disc in the internal drive isn't read as a volume.

## [v1.1.2] - 21b707df

### Changed
- Diagnostic logging for the PS2 disc check: each step (the volume label, the root directory, `SYSTEM.CNF` and its contents, and the BOOT2 result) is written to `main.log`.

## [v1.1.1] - 6f642f18

### Fixed
- A DVD in the drive is sized from the media's size (FreeBSD's `DIOCGMEDIASIZE`) when its table of contents doesn't give a usable size, so PS2 discs in the internal drive can be identified.

### Changed
- The disc log line for a disc rejected as "not a CD" shows the sector count, to diagnose remaining cases.

## [v1.1.0] - 4b1f8c79

### Fixed
- Discs too large for a CD (PS2 DVDs) are no longer rejected as "not a CD" when identified, so a PS2 disc in the console's internal drive can be listed under PlayStation 2. Only identification changed; copies are still limited to 1.5 GB.

## [v1.0.9] - f341c671

### Added
- PS2 discs in the drive are listed under PlayStation 2, named from their serial (read from the disc's `BOOT2`). Cross on one starts it from the drive in PS5SX2, using the launch file with the drive (`/dev/cdN`) as the game. PS2 discs don't get the insert prompt.

## [v1.0.8] - 59417ce0

### Fixed
- The PS2 BIOS check accepts any file that starts with `scph` and ends in `.bin` (or `.rom0`), not only names with five digits, so `scph10000.bin` is recognised.

## [helper v2.3] - d63273d2 (app unchanged, 5d3ca382)

### Added
- When PS Launcher starts RPCS3-5 with a game, the helper turns RPCS3-5's "Automatically start games after boot" back on once RPCS3-5 has closed. If RPCS3-5 never starts, it does so after five minutes.

## [helper v2.2] - 70468f64

### Added
- LAUNCH accepts a disc drive (`/dev/cdN`) after `--boot`, so RPCS3-5 can boot a PS3 disc from the drive.

## [helper v2.1] - c89d5d57

### Added
- LAUNCH accepts `--boot <path>`: the helper starts the title with two arguments, `--boot` and the path (RPCS3-5 reads them).

## [helper v2.0] - helper 0a2a3315 (app unchanged, ca76ed28)

### Added
- Holding L2 + R2 + touchpad for two seconds, while PS5SX2 runs from PS Launcher, closes PS5SX2 (SIGTERM, then SIGKILL after three seconds if it doesn't stop) and brings PS Launcher back to the front.
- The helper's readings are logged to `helper.log` (`pad:` and `close` lines).

### Notes
- Closing PS5SX2 ends its game without a normal exit, so unsaved progress may be lost. Experimental: not yet tested on the console.

## [helper 1.0.7] - helper only (app unchanged, ca76ed28)

### Added
- Helper (`pslauncher-helper.elf`): while PS5SX2 runs, holding L2 + R2 + Options for two seconds brings PS Launcher back to the front. The helper reads the controller only while PS5SX2's heartbeat (`logs/ps5sx2-alive.txt`) is fresh, and logs its readings to `helper.log`. Experimental: not yet tested on the console.

## [v1.0.7] - ca76ed28

### Added
- Pressing Cross on a PS2 game starts that game in PS5SX2. The launcher writes the game's path and serial to `/data/PCSX2/logs/disc-launch.txt` (PS5SX2's own launch file), then starts PS5SX2. The PS2 entry's `launch_file` setting sets this.

## [v1.0.6] - f6b09715

### Fixed
- Starting a title (such as PS5SX2 from the PS2 entry) failed with "Helper not running" when the app could read `/data` itself, because the helper connection was switched off. Launching now uses the helper whenever it is running.

## [v1.0.5] - db71cd81

### Fixed
- The PS2 BIOS check now accepts `scph-xxxxx.ROM0` files. v1.0.4 still rejected them, because its name-length check only allowed `.bin`.

## [v1.0.4] - aac0fa03

### Fixed
- The PS2 BIOS check also accepts a split BIOS dump, `scph-xxxxx.ROM0` (with its `.nvm` and `.mec` files), not only `.bin`.

## [v1.0.3] - 70b6927c

### Added
- PS2 (PS5SX2) entry in the default config: title ID `PPSA99203`, games in `/data/PSLauncher/games/ps2`.
- BIOS check by name: any file `scph-xxxxx.bin` or `scphxxxxx.bin` (any version, any case) in `/data/PSLauncher/bios/ps2` counts as a PS2 BIOS.

## [v1.0.2] - df61daa6

### Added
- "Do nothing" in the "When a disc is inserted" setting. No prompt opens when a disc goes in, but the library still lists the disc.
- The PS1 library list has headings: "Disc:" above a disc in the drive (shown only while a disc is inserted), and "Installed:" above the installed games.

## [v1.0.1] - 5f2db236

### Changed
- Holding Triangle for half a second on the library ejects the disc in the drive. A quick press of Triangle still opens the disc menu for a selected disc.
- The disc menu no longer has an "Eject the disc" option.

## [v1.0] - ca6a82c7 (release)

### Added
- PlayStation 1 games run inside the app with the built-in PSSwanStation core, drawn into the launcher's screen at 4:3 with pillarboxing, with sound.
- Analog sticks: a DualShock pad is passed to the core, with a small dead zone.
- USB disc drive support: the drive is watched at startup, and a disc in it is listed under PlayStation in the library.
- Game names by serial number from a database (CC BY-SA 4.0), so a disc shows as `Crash Bandicoot [SCUS-94900]`.
- A disc menu, opened when a disc goes in or by pressing Triangle on a selected disc: play from the disc, install to the internal disk, copy into memory and play, eject, or not now.
- A "When a disc is inserted" setting (ask, play from the disc, install, or copy into memory and play), saved to `disc.ini`, with the chosen option highlighted in the disc menu.
- Installing a disc (Circle on a selected disc, or from the disc menu): the disc is copied to `/data/PSLauncher/games/ps1/<Title [SERIAL]>/` as a `.bin` and `.cue` (and a `.sbi` for LibCrypt discs), with a percentage shown.
- Cancelling an install (Circle, then Cross to confirm; Circle keeps going). The partial files are removed.
- Games can't start while an install or a memory read is running.
- Copy into memory and play: the disc is read into memory and played, and the memory is freed when the game closes.
- Eject the disc from the disc menu. Refused while a game or copy is using the disc.
- A settings screen (Options) for the disc setting.
- A loading splash with the app icon, shown from startup until the library is ready.
- A "Checking the drives..." wait at startup, so a disc already in a drive shows its prompt on the first frame.
- A game installed in `games/ps1` hides the same game in the other folders, matched by serial number.
- The sandbox elevator, sent at startup so the app can see the USB drive; each step is logged.
- Circle installs the selected disc; Circle is greyed out when no disc is selected.
- The storage and helper status is written to `/data/PSLauncher/helper.log` instead of the info panel.

### Changed
- Options opens settings. Triangle opens the disc menu on a selected disc entry, and the emulator for PS2 and PS3. On PS1 games it does nothing.
- Holding Touchpad quits the launcher, or stops a game when one is running. After a game closes, Touchpad must be released and pressed again before it quits the launcher.
- Status messages clear after 2 seconds. A copy's progress stays on the status line while it runs.
- The menu waits for the first drive check before it appears.
- Installs are placed in `/data/PSLauncher/games/ps1/`, and the library scans that folder.

### Fixed
- Crashes in earlier builds at about frame 124 and frame 366 of a game, from the small libc heap. Memory now comes from the SDK's direct-memory heap.
- The game picture no longer relies on an SDL texture, which doesn't work on the console.
- The copy destination folder path now has its trailing slash.
