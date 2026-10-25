#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace graw {

class Sha1 {
 public:
  Sha1();
  void Update(const void* data, size_t size);
  std::array<uint8_t, 20> Finish();

  static std::string ToHex(const std::array<uint8_t, 20>& digest);

 private:
  void Block(const uint8_t* block);

  uint32_t state_[5];
  uint8_t buffer_[64];
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
};

}  // namespace graw
