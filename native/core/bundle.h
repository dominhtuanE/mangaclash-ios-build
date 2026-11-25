#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mc {

// Read-only view over the game's scriptN.ios bundles (ZIP, deflate, ZipCrypto). Later bundles override earlier ones.
class BundleFs {
public:
    bool addBundle(const std::string& path, const std::string& password);
    bool exists(const std::string& name) const;
    bool read(const std::string& name, std::string& out) const;
    size_t size() const { return entries_.size(); }
    bool readFirst(std::string& out) const { return !entries_.empty() && read(entries_.begin()->first, out); }

private:
    struct Entry { size_t bundle; uint32_t offset, compSize, size; uint16_t method, flags; };
    std::vector<std::string> data_;
    std::vector<std::string> passwords_;
    std::map<std::string, Entry> entries_;
};

bool readFile(const std::string& path, std::string& out);

}  // namespace mc
