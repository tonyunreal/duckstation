#pragma once
#include "common/hash_combine.h"
#include "common/image.h"
#include "common/rectangle.h"
#include "gpu_types.h"
#include "types.h"
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

struct TextureReplacementHash
{
  u64 low;
  u64 high;

  std::string ToString() const;
  bool ParseString(const std::string_view& sv);

  bool operator<(const TextureReplacementHash& rhs) const { return std::tie(low, high) < std::tie(rhs.low, rhs.high); }
  bool operator==(const TextureReplacementHash& rhs) const { return low == rhs.low && high == rhs.high; }
  bool operator!=(const TextureReplacementHash& rhs) const { return low != rhs.low || high != rhs.high; }
};

namespace std {
template<>
struct hash<TextureReplacementHash>
{
  size_t operator()(const TextureReplacementHash& h) const
  {
    size_t hash_hash = std::hash<u64>{}(h.low);
    hash_combine(hash_hash, h.high);
    return hash_hash;
  }
};
} // namespace std

using TextureReplacementTexture = Common::RGBA8Image;

class TextureReplacements
{
public:
  enum class ReplacmentType
  {
    VRAMWrite
  };

  TextureReplacements();
  ~TextureReplacements();

  const std::string GetGameID() const { return m_game_id; }
  void SetGameID(std::string game_id);

  void Reload();

  const TextureReplacementTexture* GetVRAMWriteReplacement(u32 width, u32 height, const void* pixels);
  void AddVRAMWrite(u32 x, u32 y, u32 width, u32 height, const void* pixels);

  void AddDraw(u16 draw_mode, u16 palette, u32 min_uv_x, u32 min_uv_y, u32 max_uv_x, u32 max_uv_y);

  void Shutdown();

private:
  struct ReplacementHashMapHash
  {
    size_t operator()(const TextureReplacementHash& hash);
  };

  struct PendingVRAMWrite
  {
    union PixelValue
    {
      u32 bits;

      BitField<u32, u32, 0, 10> palette_x;
      BitField<u32, u32, 10, 10> palette_y;
      BitField<u32, GPUTextureMode, 20, 2> mode;

      ALWAYS_INLINE bool IsValid() const { return (bits != 0xFFFFFFFFu); }
      ALWAYS_INLINE void SetInvalid() { bits = 0xFFFFFFFFu; }
      ALWAYS_INLINE void Set(u32 palette_x_, u32 palette_y_, GPUTextureMode mode_)
      {
        palette_x = palette_x_;
        palette_y = palette_y_;
        mode = mode_;
      }

      ALWAYS_INLINE static PixelValue InvalidValue() { return PixelValue{0xFFFFFFFFu}; }
    };

    TextureReplacementHash hash;
    Common::Rectangle<u32> rect;
    std::vector<PixelValue> palette_values;
  };

  using VRAMWriteReplacementMap = std::unordered_map<TextureReplacementHash, std::string>;
  using TextureCache = std::unordered_map<std::string, TextureReplacementTexture>;
  using PendingVRAMWriteList = std::vector<PendingVRAMWrite>;

  static bool ParseReplacementFilename(const std::string& filename, TextureReplacementHash* replacement_hash,
                                       ReplacmentType* replacement_type);

  std::string GetSourceDirectory() const;

  TextureReplacementHash GetVRAMWriteHash(u32 width, u32 height, const void* pixels) const;
  TextureReplacementHash GetVRAMHash(u32 left, u32 top, u32 width, u32 height) const;
  std::string GetVRAMWriteDumpFilename(u32 width, u32 height, const void* pixels) const;
  std::string GetTextureDumpFilename(const PendingVRAMWrite& vrw) const;
  std::optional<GPUTextureMode> GetTextureDumpMode(const PendingVRAMWrite& vrw) const;
  void DumpVRAMWriteForDisplay(u32 width, u32 height, const void* pixels);

  bool CanDumpPendingVRAMWrite(const PendingVRAMWrite& vrw, bool invalidating);
  void DumpVRAMWriteForTexture(const PendingVRAMWrite& vrw);
  void DumpPendingWrites();

  void FindTextures(const std::string& dir);

  const TextureReplacementTexture* LoadTexture(const std::string& filename);
  void PreloadTextures();
  void PurgeUnreferencedTexturesFromCache();

  std::string m_game_id;

  TextureCache m_texture_cache;

  VRAMWriteReplacementMap m_vram_write_replacements;

  PendingVRAMWriteList m_pending_vram_writes;

  std::array<u16, VRAM_WIDTH* VRAM_HEIGHT> m_vram_shadow{};
};

extern TextureReplacements g_texture_replacements;