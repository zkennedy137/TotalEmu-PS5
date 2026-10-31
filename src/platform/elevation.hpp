/*
 * PS Launcher - elevation client header.
 * Ported from ps5-native-app-boilerplate by BlackBearReloaded.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sends /app0/lapy.elf to the ELF loader (port 9021) to unsandbox this
 * process so the emulator core can read /data directly.
 */
#pragma once

#include <cstdint>

namespace elevation {

enum class Capability : std::uint32_t { filesystem = 1 };

enum class Status : std::uint32_t {
    ok = 0,
    invalid_request = 1,
    unsupported_version = 2,
    unsupported_capability = 3,
    target_mismatch = 4,
    unavailable = 5,
    prepare_failed = 6,
    apply_failed = 7,
    rollback_failed = 8,
    transport_error = 9,
    protocol_error = 10,
    timeout = 11,
};

// Call once during single-threaded startup (before SDL starts threads).
// Sends lapy.elf to the ELF loader, which unsandboxes this process.
// Returns ok when /data becomes directly readable.
[[nodiscard]] Status request(Capability capability,
                             const char *helper_path = "/app0/lapy.elf") noexcept;

[[nodiscard]] const char *statusString(Status s) noexcept;
[[nodiscard]] const char *path() noexcept;

}  // namespace elevation
