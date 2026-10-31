// PS3 disc decryption (PS Launcher), following RPCS3's Loader/ISO.cpp. See ps3iso.h.
// SPDX-License-Identifier: GPL-2.0-only
#include "ps3iso.h"

#include <cstdio>
#include <cstring>

#include "aes/aes.h"

namespace fe::ps3iso {
namespace {

constexpr uint32_t Sector = 2048;

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool readRegions(const uint8_t* sectors01, std::vector<Region>& out) {
    // Even regions are never encrypted; odd ones are (RPCS3's reading of the PS3 ISO layout).
    const uint32_t count = be32(sectors01);
    if (count < 1 || count > 127) return false;
    out.assign(count * 2 - 1, Region());
    for (size_t i = 0; i < out.size(); i++) {
        const uint32_t modulo = (uint32_t)(i % 2);
        const uint32_t end = be32(sectors01 + 12 + i * 4);
        if (end < modulo) return false;
        out[i].encrypted = modulo == 1;
        out[i].first = i == 0 ? 0 : out[i - 1].last + 1;
        out[i].last = end - modulo;
        if (out[i].last < out[i].first) return false;
    }
    return true;
}

bool isEncrypted(const std::vector<Region>& regions, uint64_t lba) {
    for (const Region& r : regions)
        if (lba >= r.first && lba <= r.last) return r.encrypted;
    return false;
}

void sectorIv(uint8_t iv[16], uint32_t lba) {
    std::memset(iv, 0, 12);
    iv[12] = (uint8_t)(lba >> 24);
    iv[13] = (uint8_t)(lba >> 16);
    iv[14] = (uint8_t)(lba >> 8);
    iv[15] = (uint8_t)lba;
}

bool findFile(const ReadSectors& read, const std::string& path, uint32_t& lba, uint32_t& size) {
    uint8_t pvd[Sector];
    if (!read(16, 1, pvd) || std::memcmp(pvd + 1, "CD001", 5) != 0) return false;
    uint32_t dirLba = le32(pvd + 156 + 2), dirSize = le32(pvd + 156 + 10);

    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const std::string want = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        const bool last = slash == std::string::npos;
        bool found = false, isDir = false;
        uint32_t entryLba = 0, entrySize = 0;
        uint8_t buf[Sector];
        for (uint32_t s = 0; !found && s * Sector < dirSize && s < 256; ++s) {
            if (!read(dirLba + s, 1, buf)) return false;
            for (uint32_t at = 0; !found && at < Sector && buf[at] != 0;) {
                const uint8_t len = buf[at];
                if (len < 34 || at + len > Sector) break;
                std::string name((const char*)(buf + at + 33), buf[at + 32]);
                name = name.substr(0, name.find(';'));
                if (name == want) {
                    found = true;
                    entryLba = le32(buf + at + 2);
                    entrySize = le32(buf + at + 10);
                    isDir = (buf[at + 25] & 2) != 0;
                }
                at += len;
            }
        }
        if (!found) return false;
        if (last) {
            if (isDir) return false;
            lba = entryLba;
            size = entrySize;
            return true;
        }
        if (!isDir) return false;
        dirLba = entryLba;
        dirSize = entrySize;
        start = slash + 1;
    }
    return false;
}

bool readKey(const std::string& path, std::array<uint8_t, 16>& key) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t data[64];
    const size_t n = std::fread(data, 1, sizeof(data), f);
    std::fclose(f);
    if (n == 16) {
        std::memcpy(key.data(), data, 16);
        return true;
    }
    if (n < 32) return false;
    for (size_t i = 0; i < 32; i++)
        if (hexValue((char)data[i]) < 0) return false;
    for (size_t i = 0; i < 16; i++)
        key[i] = (uint8_t)(hexValue((char)data[2 * i]) << 4 | hexValue((char)data[2 * i + 1]));
    return true;
}

bool keyMatches(const std::array<uint8_t, 16>& key, const std::vector<Region>& regions, uint32_t licLba,
                const uint8_t* lic, uint32_t ebootLba, const uint8_t* eboot) {
    (void)regions;
    aes_context ctx {};
    if (aes_setkey_dec(&ctx, key.data(), 128) != 0) return false;
    // Each file's first block is decrypted with the IV of the sector it starts in, as RPCS3 does.
    uint8_t iv[16], plain[16];
    sectorIv(iv, licLba);
    if (aes_crypt_cbc(&ctx, AES_DECRYPT, 16, iv, lic, plain) != 0) return false;
    if (std::memcmp(plain, "PS3LICDA", 8) != 0) return false;
    sectorIv(iv, ebootLba);
    if (aes_crypt_cbc(&ctx, AES_DECRYPT, 16, iv, eboot, plain) != 0) return false;
    return std::memcmp(plain, "SCE", 3) == 0;
}

bool decryptSectors(const std::array<uint8_t, 16>& key, const std::vector<Region>& regions, uint32_t lba,
                    uint32_t count, uint8_t* sectors) {
    aes_context ctx {};
    if (aes_setkey_dec(&ctx, key.data(), 128) != 0) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (!isEncrypted(regions, (uint64_t)lba + i)) continue;
        uint8_t iv[16];
        sectorIv(iv, lba + i);
        uint8_t* p = sectors + (size_t)i * Sector;
        if (aes_crypt_cbc(&ctx, AES_DECRYPT, Sector, iv, p, p) != 0) return false;
    }
    return true;
}

}  // namespace fe::ps3iso
