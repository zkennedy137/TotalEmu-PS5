# PS Launcher (PS5 build, experimental)

One home-screen app that starts separate emulator titles (PS1, PS2, PS3).
Title ID **PPSA97500**. GPL-3.0-or-later (see `LICENSE`, `docs/THIRD-PARTY.txt`).

## Install

1. **Helper payload** (`dist/pslauncher-helper.elf`). With OnionHEN, copy it to
   `/data/OnionHEN/payloads/` so it starts on every boot, then reboot (or send it once
   to the ELF loader on port 9021 for this session). A notification says
   "PS Launcher helper is running".
2. **App**: copy `dist/PPSA97500/` to `/data/homebrew/` over FTP, set permissions to
   777 recursively (otherwise CE-107750-0), restart ShadowMountPlus.
3. First start copies the default config to `/data/PSLauncher/launcher.ini`. Edit the
   TODO title IDs there.

The side panel's last line shows **Storage: helper 1 connected** when it works.

## Why a helper

A sandboxed PS5 app can't see other titles' folders under `/data`, and calling
`sceLncUtilLaunchApp` from inside it crashed the app (0.2, on hardware). The helper
runs outside the sandbox and serves the launcher on `127.0.0.1:9133`, one command per
connection: `PING`, `EXISTS`, `LIST`, `READ`, `WRITE`, `APPEND`, `MKDIR`, `LAUNCH`.
Reads are limited to `/data`, `/mnt/usb*`, `/mnt/ext*`, `/mnt/disc`; writes to
`/data/PSLauncher`; `..` is refused; `LAUNCH` takes only `PPSAnnnnn` IDs.

On Cross/Triangle the launcher sends `LAUNCH`, closes itself, and the helper starts the
emulator about 1.5 s later (retrying for a few seconds). Logs:
`/data/PSLauncher/main.log` and `/data/PSLauncher/helper.log`.

## Build (Linux / WSL, Ubuntu 24.04)

```
sudo apt install clang-18 lld-18 llvm-18 make ninja-build pkg-config python3 python3-venv tar unzip wget
make            # dist/PPSA97500/ (+ .zip) and dist/pslauncher-helper.elf
make desktop    # Linux builds of both, for testing without the console
```

Desktop test of the launcher and helper together (paths in the config must be absolute
and under the root):

```
PSL_HELPER_ROOT=$PWD/testdata build/desktop/pslauncher-helper &
PSL_USE_HELPER=1 build/desktop/pslauncher $PWD/testdata/data/launcher.ini
```

`make` downloads the pinned PS5 Payload SDK, zlib and PacBrew's SDL2 into `.deps/`,
then checks that `eboot.bin` imports no module that stops a title from launching.

## Console-specific notes

- SDL reads the controller only; the menu is drawn by SDL's software renderer into a
  CPU buffer that `src/platform/ps5_video.c` shows through VideoOut.
- The title closes through `sceSystemServiceLoadExec("exit")`; returning from `main`
  or calling `exit()` crashes a title.
- `src/platform/ps5_shims.cpp` keeps `libSceKeyboard`, `libSceImeDialog` and
  `libScePosixForWebKit` out of the imports (each stopped titles on hardware).

## PS1 games inside the app (v0.6, Vulkan display)

The PlayStation tab runs games with the built-in PSSwanStation core, in this app.
v0.6 fixes the v0.5 startup crash: the screen now goes through RADV's Vulkan
display (the path PSSwanStation uses, which works on the console), instead of the
app opening VideoOut itself, which collided with the linked Vulkan driver.

The menu and the emulator's frames are still drawn on the CPU and blitted to the
screen by Vulkan (`src/vk_present.cpp`). Upscaled GPU rendering comes later; this
step is about getting a stable picture again.

Cross on a PS1 game loads it; hold **Options + touchpad (2 s)** to return to the menu.

Before testing:
- PS1 BIOS in `/data/PSLauncher/bios/ps1/` (any valid dump; the core identifies it
  by content, so `SCPH1001.bin` is fine, no rename). Without one it uses OpenBIOS.
- A PS1 game where the launcher lists it (`/data/SwanStationPS5/games/<name>/` or a
  USB drive): `.cue`+`.bin`, `.chd`, `.pbp`, or `.m3u`.

The log now records `display: RADV <gpu>, <w>x<h> @ <hz>` when the screen opens. If
the app still dies before "display:", send `/data/PSLauncher/main.log`.

## Building the PS1 core
