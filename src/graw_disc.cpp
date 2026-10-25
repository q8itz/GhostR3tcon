#include "graw_disc.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

#include "sha1.h"

namespace graw {

namespace {

constexpr uint64_t kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// Game partition offsets for XGD1, XGD2 and XGD3 discs.
constexpr uint64_t kPartitionOffsets[] = {0, 0xFD90000, 0x2080000, 0x18300000};
constexpr uint32_t kMaxRootSize = 16u << 20;

struct Entry {
  uint32_t sector;
  uint32_t size;
};

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

bool ReadAt(std::ifstream& f, uint64_t offset, void* out, size_t size) {
  f.clear();
  f.seekg(std::streamoff(offset));
  f.read(static_cast<char*>(out), std::streamsize(size));
  return f.gcount() == std::streamsize(size);
}

std::map<std::string, Entry> ParseRoot(const std::vector<uint8_t>& data) {
  std::map<std::string, Entry> entries;
  size_t pos = 0;
  while (pos + 14 <= data.size()) {
    const uint8_t* p = data.data() + pos;
    if (Le16(p) == 0xFFFF && Le16(p + 2) == 0xFFFF) {
      pos = (pos / kSector + 1) * kSector;
      continue;
    }
    uint8_t name_length = p[13];
    if (pos + 14 + name_length > data.size()) {
      break;
    }
    std::string name(reinterpret_cast<const char*>(p + 14), name_length);
    entries[Lower(name)] = {Le32(p + 4), Le32(p + 8)};
    pos = (pos + 14 + name_length + 3) & ~size_t(3);
  }
  return entries;
}

}  // namespace

DiscCheck CheckDisc(const std::filesystem::path& image) {
  std::ifstream f(image, std::ios::binary);
  if (!f) {
    return {DiscCheck::Result::kError, "Could not open the file."};
  }

  uint64_t base = 0;
  uint32_t root_sector = 0, root_size = 0;
  bool found = false;
  for (uint64_t offset : kPartitionOffsets) {
    uint8_t header[sizeof(kMagic) - 1 + 8];
    if (ReadAt(f, offset + 32 * kSector, header, sizeof(header)) &&
        std::memcmp(header, kMagic, sizeof(kMagic) - 1) == 0) {
      base = offset;
      root_sector = Le32(header + sizeof(kMagic) - 1);
      root_size = Le32(header + sizeof(kMagic) - 1 + 4);
      found = true;
      break;
    }
  }
  if (!found) {
    return {DiscCheck::Result::kError,
            "This is not an Xbox 360 disc image (no Xbox filesystem found)."};
  }
  if (root_size == 0 || root_size > kMaxRootSize) {
    return {DiscCheck::Result::kError, "The disc's root directory is damaged."};
  }
  std::vector<uint8_t> root(root_size);
  if (!ReadAt(f, base + root_sector * kSector, root.data(), root.size())) {
    return {DiscCheck::Result::kError, "Could not read the disc's root directory."};
  }
  auto entries = ParseRoot(root);

  std::vector<uint8_t> data;
  for (const DiscFile& file : kDiscFiles) {
    auto it = entries.find(Lower(file.name));
    if (it == entries.end()) {
      return {DiscCheck::Result::kMismatch,
              std::string(file.name) + " is not on this disc, so it is not GRAW."};
    }
    if (it->second.size != file.size) {
      return {DiscCheck::Result::kMismatch,
              std::string(file.name) + " is " + std::to_string(it->second.size) +
                  " bytes; the supported release has " + std::to_string(file.size) + "."};
    }
    data.resize(file.size);
    if (!ReadAt(f, base + it->second.sector * kSector, data.data(), data.size())) {
      return {DiscCheck::Result::kError,
              std::string("Could not read ") + file.name + "; the image may be truncated."};
    }
    Sha1 sha1;
    sha1.Update(data.data(), data.size());
    std::string digest = Sha1::ToHex(sha1.Finish());
    if (digest != file.sha1) {
      return {DiscCheck::Result::kMismatch,
              std::string(file.name) + " differs from the supported release (SHA-1 " + digest +
                  ")."};
    }
  }
  return {DiscCheck::Result::kMatch, "Supported release: USA/Europe, disc US-2005."};
}

}  // namespace graw
