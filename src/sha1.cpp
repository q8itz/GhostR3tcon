#include "sha1.h"

#include <algorithm>
#include <cstring>

namespace graw {

namespace {
inline uint32_t Rol(uint32_t v, int n) {
  return (v << n) | (v >> (32 - n));
}
inline uint32_t LoadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
}  // namespace

Sha1::Sha1() {
  state_[0] = 0x67452301;
  state_[1] = 0xEFCDAB89;
  state_[2] = 0x98BADCFE;
  state_[3] = 0x10325476;
  state_[4] = 0xC3D2E1F0;
}

void Sha1::Block(const uint8_t* block) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = LoadBe32(block + i * 4);
  }
  for (int i = 16; i < 80; ++i) {
    w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    uint32_t t = Rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = Rol(b, 30);
    b = a;
    a = t;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
}

void Sha1::Update(const void* data, size_t size) {
  auto* p = static_cast<const uint8_t*>(data);
  total_bytes_ += size;
  if (buffered_) {
    size_t take = std::min(size, sizeof(buffer_) - buffered_);
    std::memcpy(buffer_ + buffered_, p, take);
    buffered_ += take;
    p += take;
    size -= take;
    if (buffered_ < sizeof(buffer_)) {
      return;
    }
    Block(buffer_);
    buffered_ = 0;
  }
  while (size >= 64) {
    Block(p);
    p += 64;
    size -= 64;
  }
  std::memcpy(buffer_, p, size);
  buffered_ = size;
}

std::array<uint8_t, 20> Sha1::Finish() {
  uint64_t bit_count = total_bytes_ * 8;
  uint8_t pad[72] = {0x80};
  size_t pad_len = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
  for (int i = 0; i < 8; ++i) {
    pad[pad_len + i] = uint8_t(bit_count >> (56 - i * 8));
  }
  Update(pad, pad_len + 8);
  std::array<uint8_t, 20> digest;
  for (int i = 0; i < 5; ++i) {
    digest[i * 4 + 0] = uint8_t(state_[i] >> 24);
    digest[i * 4 + 1] = uint8_t(state_[i] >> 16);
    digest[i * 4 + 2] = uint8_t(state_[i] >> 8);
    digest[i * 4 + 3] = uint8_t(state_[i]);
  }
  return digest;
}

std::string Sha1::ToHex(const std::array<uint8_t, 20>& digest) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(40);
  for (uint8_t b : digest) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0xF]);
  }
  return out;
}

}  // namespace graw
