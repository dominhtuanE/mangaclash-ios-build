#include "bundle.h"

#include <cstdio>
#include <cstring>

#include "../third_party/miniz/miniz.h"

namespace mc {
namespace {

uint32_t crcStep(uint32_t crc, uint8_t b) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    return (crc >> 8) ^ table[(crc ^ b) & 0xFF];
}

struct ZipCryptoKeys {
    uint32_t k0 = 0x12345678, k1 = 0x23456789, k2 = 0x34567890;
    void update(uint8_t c) {
        k0 = crcStep(k0, c);
        k1 = (k1 + (k0 & 0xFF)) * 134775813u + 1;
        k2 = crcStep(k2, static_cast<uint8_t>(k1 >> 24));
    }
    uint8_t stream() const {
        uint16_t t = static_cast<uint16_t>(k2 | 2);
        return static_cast<uint8_t>((t * (t ^ 1)) >> 8);
    }
    uint8_t decrypt(uint8_t c) {
        uint8_t p = c ^ stream();
        update(p);
        return p;
    }
};

uint16_t u16(const std::string& d, size_t o) { return static_cast<uint8_t>(d[o]) | (static_cast<uint8_t>(d[o + 1]) << 8); }
uint32_t u32(const std::string& d, size_t o) { return u16(d, o) | (static_cast<uint32_t>(u16(d, o + 2)) << 16); }

}  // namespace

bool readFile(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n));
    size_t got = n ? std::fread(&out[0], 1, static_cast<size_t>(n), f) : 0;
    std::fclose(f);
    return got == static_cast<size_t>(n);
}

bool BundleFs::addBundle(const std::string& path, const std::string& password) {
    std::string d;
    if (!readFile(path, d) || d.size() < 22) return false;
    size_t eocd = std::string::npos;
    for (size_t i = d.size() - 22; i + 1 > 0; --i) {
        if (u32(d, i) == 0x06054b50u) { eocd = i; break; }
        if (i == 0) break;
    }
    if (eocd == std::string::npos) return false;
    uint16_t count = u16(d, eocd + 10);
    size_t p = u32(d, eocd + 16);
    size_t idx = data_.size();
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > d.size() || u32(d, p) != 0x02014b50u) return false;
        Entry e;
        e.bundle = idx;
        e.flags = u16(d, p + 8);
        e.method = u16(d, p + 10);
        e.compSize = u32(d, p + 20);
        e.size = u32(d, p + 24);
        uint16_t nameLen = u16(d, p + 28), extraLen = u16(d, p + 30), commentLen = u16(d, p + 32);
        e.offset = u32(d, p + 42);
        std::string name = d.substr(p + 46, nameLen);
        if (!name.empty() && name.back() != '/') entries_[name] = e;
        p += 46 + nameLen + extraLen + commentLen;
    }
    data_.push_back(std::move(d));
    passwords_.push_back(password);
    return true;
}

bool BundleFs::exists(const std::string& name) const { return entries_.count(name) != 0; }

bool BundleFs::read(const std::string& name, std::string& out) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return false;
    const Entry& e = it->second;
    const std::string& d = data_[e.bundle];
    if (e.offset + 30 > d.size() || u32(d, e.offset) != 0x04034b50u) return false;
    size_t start = e.offset + 30 + u16(d, e.offset + 26) + u16(d, e.offset + 28);
    if (start + e.compSize > d.size()) return false;
    std::string raw = d.substr(start, e.compSize);
    if (e.flags & 1) {
        if (raw.size() < 12) return false;
        ZipCryptoKeys k;
        for (unsigned char c : passwords_[e.bundle]) k.update(c);
        for (auto& c : raw) c = static_cast<char>(k.decrypt(static_cast<uint8_t>(c)));
        raw.erase(0, 12);
    }
    if (e.method == 0) { out = std::move(raw); return true; }
    if (e.method != 8) return false;
    out.resize(e.size);
    size_t inLen = raw.size(), outLen = e.size;
    tinfl_decompressor dec;
    tinfl_init(&dec);
    tinfl_status st = tinfl_decompress(&dec, reinterpret_cast<const mz_uint8*>(raw.data()), &inLen,
                                       reinterpret_cast<mz_uint8*>(&out[0]), reinterpret_cast<mz_uint8*>(&out[0]), &outLen,
                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return st == TINFL_STATUS_DONE && outLen == e.size;
}

}  // namespace mc
