#!/usr/bin/env bash
# PS Launcher - builds the sandbox elevator (sandbox-elevator.elf) into assets/, so the
# package carries it. The payload is BlackBearReloaded's one-request helper, as
# PSSwanStation carries it (third_party/psswanstation/ps5/elevator), for PPSA97500.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
ps5="$root/third_party/psswanstation/ps5"
out="$root/assets/sandbox-elevator.elf"
"$sdk/bin/prospero-clang++" -std=c++20 -O2 -Wall -Wextra -Werror -Wl,--build-id=none \
    -ffile-prefix-map="$ps5"=ps5 \
    -o "$out" "$ps5/elevator/payload/main.cpp"
python3 "$ps5/elevator/validate-elevation-helper.py" "$out" >&2
echo "==> [elevator] $out"
