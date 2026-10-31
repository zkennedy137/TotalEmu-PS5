# Credits

TotalEmu (PS Launcher, title ID PPSA97500) is free software under the GNU General Public License,
version 3 or later (see LICENSE). It is a separate program from the emulators it starts, and it
contains no game, BIOS, firmware or key files. Users supply those themselves.

Each component below keeps its own license. Where a component's license asks for an attribution
or a notice to be kept, the notice is kept in its files or in the licence notices of this project.
The same credits are shown in the app under Settings > Credits.

## Emulators

- **PSSwanStation by Press5elect** (https://github.com/Press5elect/PSSwanStation): the PS1 emulator
  built into this launcher. GPL-3.0-or-later, with an additional term under section 7(b) of the
  GPL: attributions must be kept, and "PSSwanStation by Press5elect" must be credited wherever
  the work shows its credits. The full term is in
  `third_party/psswanstation/ps5/licenses/ADDITIONAL-TERMS.txt`.
  Based on PSSwanStation by Press5elect: the HTTPS client and JSON reader (`src/tls/`,
  `src/json.h`), the disc drive support (`src/disc/`) and the PS1 game-title lookup
  (`src/disc/gamedb.cpp`).
- **SwanStation** (libretro's fork of DuckStation), on which PSSwanStation is based. GPL-3.0.
  https://github.com/libretro/swanstation
- **PS5SX2** by Swordpdf, the PS2 emulator this launcher starts. A port of PCSX2 by the PCSX2 Dev
  Team. GPL-3.0-or-later. (Started by the launcher, not bundled.)
- **RPCS3-5 (KB-RPCS3)** by kamb205, the PS3 emulator this launcher starts. A port of RPCS3 by the
  RPCS3 team. GPL-2.0-only core. (Started by the launcher, not bundled.)

## Build tooling and runtime

- **ps5-native-app-boilerplate** by BlackBearReloaded, as vendored in SwanStationPS5 by darkxex:
  build tooling (`tools/`, `tooling/`), runtime shim and VideoOut presenter (`runtime/`,
  `src/platform/ps5_video.c`), the sandbox elevation client (`src/disc/elevator/`), the Lapy
  elevation client (`src/platform/elevation.cpp`), and the sandbox elevator
  (`assets/sandbox-elevator.elf`, as carried by PSSwanStation, with this title's ID). GPL-3.0-or-later.
  https://github.com/darkxex/SwanStationPS5
- **Stand-ins** in `src/platform/ps5_shims.cpp`, following SwanStationPS5's `src/platform/ps5_shims.c`.
  GPL-3.0-or-later.
- **lapy.elf** (`assets/lapy.elf`) by BlackBearReloaded and ArkSama (Team PHU), built on libhijacker
  by illusion and LightingMod, and on ps5-payload-sdk by John Törnblom. MIT License.
  https://github.com/ArkSama/PS5-Lapy-JB-Daemon. Original from PSSwanStation (PPSA99248) by Press5elect.
- **PS5 Payload SDK** by John Törnblom (ps5-payload-dev), including LLVM libc++.
  Apache-2.0 WITH LLVM-exception for libc++; MIT for the SDK. https://github.com/ps5-payload-dev/sdk
- **SDL2**, PacBrew PS5 port. zlib license. https://www.libsdl.org/
- **GNU libiconv**, linked by PacBrew's SDL2. LGPL-2.1-or-later. https://www.gnu.org/software/libiconv/
- **libsamplerate**, linked by PacBrew's SDL2. BSD-2-Clause. https://github.com/libsndfile/libsamplerate
- **mbedTLS 3.6.7** by the Mbed TLS project, the TLS library of the HTTPS fallback (`src/tls/mbedtls/`).
  Apache-2.0. Its LICENSE is in `src/tls/mbedtls/LICENSE`. https://github.com/Mbed-TLS/mbedtls
- **stb_easy_font** and **stb_image** by Sean Barrett. Public domain or MIT (your choice).
  https://github.com/nothings/stb
- **RADV Vulkan driver for PS5** by Mihawk-99 (Mesa, MIT), and the PS5 fork's platform layer and
  headers (GPL-3.0-or-later), downloaded at build time.

## Data

- **GameDB-PS2** by niemasd (https://github.com/niemasd/GameDB-PS2): PS2 game titles by serial,
  `assets/gamedb2/gamedb-ps2.tsv`. GPL-3.0. Sources: GameFAQs, MiSTer Addons, PlayStation
  Datacenter, Redump and VGCollect. Changed: the file is a plain tab-separated list.
- **GameDB-PSX** by niemasd (https://github.com/niemasd/GameDB-PSX): PS1 game titles and
  Redump-style names by serial, `assets/gamedb/psx.tsv`. GPL-3.0. Sources: GameFAQs, Glitchwave,
  MiSTer Addons, MobyGames, PlayStation Datacenter, Redump, Redump Wiki, ScreenScraper,
  SerialStation and VGArchive. Changed: the file is a plain tab-separated list of serial, title and
  release name.
- **libretro-database** (https://github.com/libretro/libretro-database): PS1 and PS2 game names
  (the lists used to build `assets/gamedb/` and `assets/gamedb2/`). CC BY-SA 4.0
  (`licenses/CC-BY-SA-4.0.txt`). Changed: the lists are kept as plain text, not in a zip.
- **GameTDB** (https://www.gametdb.com): PS3 game titles by serial, `assets/gamedb/ps3.tsv`, generated
  from GameTDB's ps3tdb.xml. Used with GameTDB's permission. Serial and title only.
- **libretro thumbnails** (https://thumbnails.libretro.com): cover art, downloaded when the user asks.
- **TheGamesDB** (https://thegamesdb.net): box art, downloaded with the user's own key.
- **RAWG** (https://rawg.io): pictures, downloaded with the user's own key.

Each source's own terms apply to the pictures it provides.

## Sony

"PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc. TotalEmu is not
affiliated with or endorsed by Sony.
