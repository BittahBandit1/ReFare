#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace refare {
// Keyboard-aware on-screen button prompts. Crazy Taxi's prompts are baked
// into textures ("Use the A Button to decide."). At startup this builds
// keyboard variants from the player's own game files and registers them with
// the texture cache. Which variant shows follows the last-used device: any
// key press switches to keyboard prompts, any controller button or stick
// switches back.
class ButtonPrompts {
public:
  explicit ButtonPrompts(const std::filesystem::path& game_root);
  ~ButtonPrompts();
  ButtonPrompts(const ButtonPrompts&) = delete;
  ButtonPrompts& operator=(const ButtonPrompts&) = delete;
private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// Exposed for tests: one texture from a Crazy Taxi .all sprite archive.
struct ArchiveTexture {
  std::string name;
  unsigned width = 0, height = 0, format = 0;
  std::vector<unsigned char> texels;  // raw GX texel data as loaded into RAM
};
std::vector<ArchiveTexture> ReadSpriteArchive(const std::filesystem::path& path);
// Linear RGBA8 (rows as stored) <-> GX RGBA8 tiles.
std::vector<unsigned char> DecodeRGBA8(const std::vector<unsigned char>& texels, unsigned w, unsigned h);
// "Use the A Button t" (ST_mode_s_eng02, linear RGBA as stored) -> "Use the X Key t".
std::vector<unsigned char> MakeKeyboardDecidePrompt(const std::vector<unsigned char>& rgba,
                                                     unsigned w, unsigned h);
}  // namespace refare
