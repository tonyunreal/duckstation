#pragma once
#include "common/image.h"
#include "types.h"
#include <string>
#include <unordered_map>
#include <vector>

class TextureReplacements
{
public:
  using HashType = u64;
  using ReplacementTexture = Common::RGBA8Image;

  enum class ReplacmentType
  {
    VRAMWrite
  };

  TextureReplacements();
  ~TextureReplacements();

  const std::string GetGameID() const { return m_game_id; }
  void SetGameID(std::string game_id);

  const ReplacementTexture* GetVRAMWriteReplacement(u32 width, u32 height, const u16* pixels);
  void DumpVRAMWrite(u32 width, u32 height, const u16* pixels);

  void Shutdown();

private:
  using VRAMWriteReplacementMap = std::unordered_map<HashType, std::string>;
  using TextureCache = std::unordered_map<std::string, ReplacementTexture>;

  static bool ParseReplacementFilename(const std::string& filename, HashType* replacement_hash,
                                       ReplacmentType* replacement_type);

  std::string GetSourceDirectory() const;

  HashType GetVRAMWriteHash(u32 width, u32 height, const u16* pixels) const;
  std::string GetVRAMWriteDumpFilename(u32 width, u32 height, const u16* pixels) const;

  void Reload();

  void FindTextures(const std::string& dir);

  const ReplacementTexture* LoadTexture(const std::string& filename);
  void PreloadTextures();
  void PurgeUnreferencedTexturesFromCache();

  std::string m_game_id;

  TextureCache m_texture_cache;

  VRAMWriteReplacementMap m_vram_write_replacements;
};

extern TextureReplacements g_texture_replacements;