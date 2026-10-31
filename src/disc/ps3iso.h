// PS3 disc decryption (PS Launcher). The sectors of a PS3 disc are encrypted in its odd regions with a
// per-disc key; the key is one of the keys in RPCS3-5's data/redump folder (the user's own).
// Decrypting follows RPCS3's Loader/ISO.cpp (PolarSSL AES-128 in CBC, IV from the sector number).
// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fe::ps3iso {

// Reads `count` 2048-byte sectors of user data starting at `lba` into `out`.
using ReadSectors = std::function<bool(uint32_t lba, uint32_t count, uint8_t* out)>;

// A run of sectors of the disc, with whether they are encrypted.
struct Region {
    uint64_t first = 0;
    uint64_t last = 0;	// inclusive
    bool encrypted = false;
};

// The regions from the first two sectors (4096 bytes). False when the disc has no PS3 region table.
bool readRegions(const uint8_t* sectors01, std::vector<Region>& out);

// True when sector `lba` lies in an encrypted region.
bool isEncrypted(const std::vector<Region>& regions, uint64_t lba);

// The AES IV of a sector: zeros, then its LBA as a big-endian 32-bit number.
void sectorIv(uint8_t iv[16], uint32_t lba);

// The file at `path` (for example "PS3_GAME/USRDIR/EBOOT.BIN"), found from the root of the ISO 9660 volume
// at sector 16. Its first sector and size. Directories are read as they lie on the disc.
bool findFile(const ReadSectors& read, const std::string& path, uint32_t& lba, uint32_t& size);

// A key from a key file: 16 bytes of binary, or the 32 hex digits of a text key. False when neither.
bool readKey(const std::string& path, std::array<uint8_t, 16>& key);

// The key is right when it decrypts the first block of the disc's well known files to their magic values:
// PS3_GAME/LICDIR/LIC.DAT starts "PS3LICDA" and PS3_GAME/USRDIR/EBOOT.BIN starts "SCE".
// `encrypted` is the first block of each file as it lies on the disc (16 bytes, sector-aligned start).
bool keyMatches(const std::array<uint8_t, 16>& key, const std::vector<Region>& regions,
                uint32_t licLba, const uint8_t* lic, uint32_t ebootLba, const uint8_t* eboot);

// Decrypts `count` sectors in place, starting at `lba`, the ones in an encrypted region only.
// Returns false if the AES context fails.
bool decryptSectors(const std::array<uint8_t, 16>& key, const std::vector<Region>& regions, uint32_t lba,
                    uint32_t count, uint8_t* sectors);

}  // namespace fe::ps3iso
