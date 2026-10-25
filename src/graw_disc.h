#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace graw {

// The executables of Tom Clancy's Ghost Recon - Advanced Warfighter (USA, Europe)
// (En,Fr,De,Es,It), disc US-2005, which the recompiled code is built from.
// Keep in sync with FILES in scripts/extract-game-files.py.
struct DiscFile {
  const char* name;
  uint32_t size;
  const char* sha1;
};
inline constexpr DiscFile kDiscFiles[] = {
    {"default.xex", 8826880, "09619b6967b086f9eb7b0d48dffdb14142ae4faa"},
    {"Yeti_Final_Xenon.exe", 6922240, "5243428702092fa5a99a9ee5e1542ad71b003245"},
};

struct DiscCheck {
  enum class Result {
    kMatch,
    // A readable Xbox 360 disc whose executables differ: another version or region.
    kMismatch,
    // Not an Xbox 360 disc image, or it could not be read.
    kError,
  };
  Result result = Result::kError;
  std::string message;
};

// Reads the executables from the root of the image's XDVDFS volume and compares
// them with kDiscFiles. Only touches the directory and those two files.
DiscCheck CheckDisc(const std::filesystem::path& image);

}  // namespace graw
