#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace refare {
// GCTE51's scheduler enables interrupts, polls the runnable-thread flag until
// nonzero, then disables interrupts. It has no work to do inside this loop.
// Verify the surrounding instructions before enabling the core's existing
// idle-to-next-event optimization. Never guess an address for another revision.
inline constexpr std::uint32_t kSchedulerIdlePC = 0x800d7278;
inline bool HasSchedulerIdleLoop(const std::filesystem::path& dol) {
  std::ifstream file(dol, std::ios::binary);
  std::array<unsigned char, 0x100> header{};
  if (!file.read(reinterpret_cast<char*>(header.data()), header.size())) return false;
  const auto word = [](const unsigned char* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
  };
  constexpr std::uint32_t start = kSchedulerIdlePC - 4;
  constexpr std::array<std::uint32_t, 5> expected = {
    0x4bffa859, // bl OSEnableInterrupts (0x800d1acc)
    0x800d98b8, // lwz r0,-26440(r13)
    0x28000000, // cmplwi r0,0
    0x4182fff8, // beq back to load
    0x4bffa835  // bl OSDisableInterrupts (0x800d1ab8)
  };
  for (unsigned i = 0; i < 7; ++i) {
    const auto offset = word(header.data() + i*4);
    const auto address = word(header.data() + 0x48 + i*4);
    const auto size = word(header.data() + 0x90 + i*4);
    if (start < address || std::uint64_t(start) + 20 > std::uint64_t(address) + size) continue;
    std::array<unsigned char, 20> instructions{};
    file.seekg(std::uint64_t(offset) + start - address);
    if (!file.read(reinterpret_cast<char*>(instructions.data()), instructions.size())) return false;
    for (unsigned j = 0; j < expected.size(); ++j)
      if (word(instructions.data() + j*4) != expected[j]) return false;
    return true;
  }
  return false;
}
}
