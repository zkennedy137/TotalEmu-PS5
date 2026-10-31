#!/usr/bin/env bash
# PS Launcher - builds the helper payload: dist/pslauncher-helper.elf.
# Put it in /data/OnionHEN/payloads (auto-start) or send it to the ELF loader (port 9021).
# Same toolchain call as SwanStationPS5's tools/build-disc-daemon.sh.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
[[ -x $sdk/bin/prospero-clang ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
mkdir -p "$root/dist"
"$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra \
    -o "$root/dist/pslauncher-helper.elf" "$root/payload/helper.c" -L"$sdk/target/lib"
python3 "$root/tools/validate-loader-elf.py" "$root/dist/pslauncher-helper.elf"
ls -l "$root/dist/pslauncher-helper.elf"
