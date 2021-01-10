#include "texture_replacements.h"
#include "common/cpu_detect.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/string_util.h"
#include "common/timer.h"
#include "host_interface.h"
#include "settings.h"
#include "xxhash.h"
#if defined(CPU_X86) || defined(CPU_X64)
#include "xxh_x86dispatch.h"
#endif
#include <cinttypes>
Log_SetChannel(TextureReplacements);

TextureReplacements g_texture_replacements;

std::string TextureReplacementHash::ToString() const
{
  return StringUtil::StdStringFromFormat("%" PRIx64 "%" PRIx64, high, low);
}

bool TextureReplacementHash::ParseString(const std::string_view& sv)
{
  if (sv.length() != 32)
    return false;

  std::optional<u64> high_value = StringUtil::FromChars<u64>(sv.substr(0, 16), 16);
  std::optional<u64> low_value = StringUtil::FromChars<u64>(sv.substr(16), 16);
  if (!high_value.has_value() || !low_value.has_value())
    return false;

  low = low_value.value();
  high = high_value.value();
  return true;
}

TextureReplacements::TextureReplacements() = default;

TextureReplacements::~TextureReplacements() = default;

void TextureReplacements::SetGameID(std::string game_id)
{
  if (m_game_id == game_id)
    return;

  m_game_id = game_id;
  Reload();
}

const TextureReplacementTexture* TextureReplacements::GetVRAMWriteReplacement(u32 width, u32 height, const void* pixels)
{
  const TextureReplacementHash hash = GetVRAMWriteHash(width, height, pixels);

  const auto it = m_vram_write_replacements.find(hash);
  if (it == m_vram_write_replacements.end())
    return nullptr;

  return LoadTexture(it->second);
}

void TextureReplacements::AddVRAMWrite(u32 x, u32 y, u32 width, u32 height, const void* pixels)
{
  if (width >= g_settings.texture_replacements.dump_vram_write_width_threshold &&
      height >= g_settings.texture_replacements.dump_vram_write_height_threshold)
  {
    DumpVRAMWriteForDisplay(width, height, pixels);
  }

  // TODO: oversized copies
  if ((x + width) > VRAM_WIDTH || (y + height) > VRAM_HEIGHT)
  {
    Log_ErrorPrintf("Skipping %ux%u oversized write to %u,%u", width, height, x, y);
    return;
  }

  // purge overlapping copies
  const Common::Rectangle<u32> rect(x, y, x + width, y + height);
  for (auto iter = m_pending_vram_writes.begin(); iter != m_pending_vram_writes.end();)
  {
    PendingVRAMWrite& pvw = *iter;
    if (!pvw.rect.Intersects(rect))
    {
      ++iter;
      continue;
    }

    if (CanDumpPendingVRAMWrite(pvw, true))
      DumpVRAMWriteForTexture(pvw);

    iter = m_pending_vram_writes.erase(iter);
  }

  for (u32 row = 0; row < height; row++)
  {
    u16* dst = &m_vram_shadow[(y + row) * VRAM_WIDTH + x];
    const u8* src = reinterpret_cast<const u8*>(pixels) + ((row * width) * sizeof(u16));
    std::memcpy(dst, src, sizeof(u16) * width);
  }

  PendingVRAMWrite vrw;
  vrw.hash = GetVRAMWriteHash(width, height, pixels);
  vrw.rect.Set(x, y, x + width, y + height);
  vrw.palette_values.resize(width * height, PendingVRAMWrite::PixelValue::InvalidValue());
  m_pending_vram_writes.push_back(std::move(vrw));
}

void TextureReplacements::AddDraw(u16 draw_mode, u16 palette, u32 min_uv_x, u32 min_uv_y, u32 max_uv_x, u32 max_uv_y)
{
  const GPUDrawModeReg drawmode_reg{draw_mode};
  const GPUTextureMode texture_mode = drawmode_reg.texture_mode;
  const u32 page_x = drawmode_reg.GetTexturePageBaseX();
  const u32 page_y = drawmode_reg.GetTexturePageBaseY();

  u32 min_uv_x_vram = min_uv_x;
  u32 max_uv_x_vram = max_uv_x;
  switch (texture_mode)
  {
    case GPUTextureMode::Palette4Bit:
      min_uv_x_vram = (min_uv_x + 3) / 4;
      max_uv_x_vram = (max_uv_x + 3) / 4;
      break;

    case GPUTextureMode::Palette8Bit:
      min_uv_x_vram = (min_uv_x + 1) / 2;
      max_uv_x_vram = (max_uv_x + 1) / 2;
      break;

    default:
      break;
  }

  const Common::Rectangle<u32> uv_rect(page_x + min_uv_x_vram, page_y + min_uv_y, page_x + max_uv_x_vram + 1u,
                                       page_y + max_uv_y + 1u);
  const GPUTexturePaletteReg palette_reg{palette};
  const u32 palette_x = palette_reg.GetXBase();
  const u32 palette_y = palette_reg.GetYBase();

  PendingVRAMWrite::PixelValue ppv{};
  ppv.Set(palette_x, palette_y, texture_mode);

  for (auto iter = m_pending_vram_writes.begin(); iter != m_pending_vram_writes.end();)
  {
    PendingVRAMWrite& pvw = *iter;
    if (!pvw.rect.Intersects(uv_rect))
    {
      ++iter;
      continue;
    }

    Common::Rectangle<u32> cropped = pvw.rect;
    if (cropped.left < uv_rect.left)
      cropped.left += (uv_rect.left - cropped.left);
    if (cropped.right > uv_rect.right)
      cropped.right -= (cropped.right - uv_rect.right);
    if (cropped.top < uv_rect.top)
      cropped.top += (uv_rect.top - cropped.top);
    if (cropped.bottom > uv_rect.bottom)
      cropped.bottom -= (cropped.bottom - uv_rect.bottom);

    const u32 left_in_write = cropped.left - pvw.rect.left;
    const u32 top_in_write = cropped.top - pvw.rect.top;
    const u32 right_in_write = cropped.right - pvw.rect.left;
    const u32 bottom_in_write = cropped.bottom - pvw.rect.top;

    const u32 stride = pvw.rect.GetWidth();
    for (u32 row = top_in_write; row < bottom_in_write; row++)
    {
      PendingVRAMWrite::PixelValue* pvp = &pvw.palette_values[row * stride];
      for (u32 col = left_in_write; col < right_in_write; col++)
      {
        if (!pvp[col].IsValid())
          pvp[col].bits = ppv.bits;
      }
    }

    if (CanDumpPendingVRAMWrite(pvw, false))
    {
      DumpVRAMWriteForTexture(pvw);
      iter = m_pending_vram_writes.erase(iter);
    }
    else
    {
      ++iter;
    }
  }
}

std::optional<GPUTextureMode> TextureReplacements::GetTextureDumpMode(const PendingVRAMWrite& vrw) const
{
  GPUTextureMode mode = GPUTextureMode::Disabled;
  for (const PendingVRAMWrite::PixelValue& pv : vrw.palette_values)
  {
    if (!pv.IsValid() || pv.mode == mode)
      continue;

    if (mode == GPUTextureMode::Disabled)
    {
      mode = pv.mode;
      continue;
    }

    Log_ErrorPrintf("VRAM write has multiple texture modes");
    return std::nullopt;
  }

  return mode;
}

void TextureReplacements::DumpVRAMWriteForDisplay(u32 width, u32 height, const void* pixels)
{
  std::string filename = GetVRAMWriteDumpFilename(width, height, pixels);
  if (filename.empty())
    return;

  Common::RGBA8Image image;
  image.SetSize(width, height);

  const u16* src_pixels = reinterpret_cast<const u16*>(pixels);

  for (u32 y = 0; y < height; y++)
  {
    for (u32 x = 0; x < width; x++)
    {
      image.SetPixel(x, y, RGBA5551ToRGBA8888(*src_pixels));
      src_pixels++;
    }
  }

  if (g_settings.texture_replacements.dump_force_alpha_channel)
  {
    for (u32 y = 0; y < height; y++)
    {
      for (u32 x = 0; x < width; x++)
        image.SetPixel(x, y, image.GetPixel(x, y) | 0xFF000000u);
    }
  }

  Log_InfoPrintf("Dumping %ux%u VRAM write to '%s'", width, height, filename.c_str());
  if (!Common::WriteImageToFile(image, filename.c_str()))
    Log_ErrorPrintf("Failed to dump %ux%u VRAM write to '%s'", width, height, filename.c_str());
}

bool TextureReplacements::CanDumpPendingVRAMWrite(const PendingVRAMWrite& vrw, bool invalidating)
{
  const u32 total_pixels = vrw.rect.GetWidth() * vrw.rect.GetHeight();
  const u32 valid_pixels =
    static_cast<u32>(std::count_if(vrw.palette_values.begin(), vrw.palette_values.end(),
                                   [](const PendingVRAMWrite::PixelValue& pv) { return pv.IsValid(); }));
  const u32 percent = (valid_pixels * 100) / total_pixels;
  const u32 threshold = invalidating ? 10 : 80;
  return (percent >= threshold);
}

template<u32 size>
static constexpr std::array<u16, size> MakeGreyscalePalette()
{
  const u16 increment = static_cast<u16>(256u / size);
  u16 value = 0;
  std::array<u16, size> colours{};
  for (u32 i = 0; i < size; i++)
  {
    colours[i] = (value) | (value << 5) | (value << 10);
    value += increment;
  }

  return colours;
}

void TextureReplacements::DumpVRAMWriteForTexture(const PendingVRAMWrite& vrw)
{
  std::string filename = GetTextureDumpFilename(vrw);
  if (filename.empty())
    return;

  std::optional<GPUTextureMode> mode = GetTextureDumpMode(vrw);
  if (!mode.has_value())
    return;

  Common::RGBA8Image image;

  switch (mode.value())
  {
    case GPUTextureMode::Palette4Bit:
    {
      static constexpr std::array<u16, 16> fallback_palette = MakeGreyscalePalette<16>();

      const u32 left = vrw.rect.left;
      const u32 top = vrw.rect.top;
      const u32 stride = vrw.rect.GetWidth();
      const u32 width = stride * 4;
      const u32 height = vrw.rect.GetHeight();
      image.SetSize(width, height);

      for (u32 y = 0; y < height; y++)
      {
        const PendingVRAMWrite::PixelValue* pvs = &vrw.palette_values[y * stride];
        const u16* vram = &m_vram_shadow[((top + y) * VRAM_WIDTH) + left];

        for (u32 x = 0; x < width; x++)
        {
          const PendingVRAMWrite::PixelValue& pv = pvs[x / 4];
          const u16* palette =
            pv.IsValid() ? &m_vram_shadow[pv.palette_y * VRAM_WIDTH + pv.palette_x] : fallback_palette.data();
          const u8 shift = Truncate8(x % 4);
          const u8 index = Truncate8(vram[x / 4] >> ((x % 4) * 4) & 0x0F);
          image.SetPixel(x, y, RGBA5551ToRGBA8888(palette[index]));
        }
      }
    }
    break;

    case GPUTextureMode::Palette8Bit:
    {
      static constexpr std::array<u16, 256> fallback_palette = MakeGreyscalePalette<256>();

      const u32 left = vrw.rect.left;
      const u32 top = vrw.rect.top;
      const u32 stride = vrw.rect.GetWidth();
      const u32 width = stride * 2;
      const u32 height = vrw.rect.GetHeight();
      image.SetSize(width, height);

      for (u32 y = 0; y < height; y++)
      {
        const PendingVRAMWrite::PixelValue* pvs = &vrw.palette_values[y * stride];
        const u16* vram = &m_vram_shadow[((top + y) * VRAM_WIDTH) + left];

        for (u32 x = 0; x < width; x++)
        {
          const PendingVRAMWrite::PixelValue& pv = pvs[x / 2];
          const u16* palette =
            pv.IsValid() ? &m_vram_shadow[pv.palette_y * VRAM_WIDTH + pv.palette_x] : fallback_palette.data();
          const u8 shift = Truncate8(x % 2);
          const u8 index = Truncate8(vram[x / 2] >> ((x % 2) * 8) & 0xFF);
          image.SetPixel(x, y, RGBA5551ToRGBA8888(palette[index]));
        }
      }
    }
    break;

    case GPUTextureMode::Direct16Bit:
    {
      const u32 left = vrw.rect.left;
      const u32 top = vrw.rect.top;
      const u32 width = vrw.rect.GetWidth();
      const u32 height = vrw.rect.GetHeight();
      image.SetSize(width, height);
      for (u32 y = 0; y < height; y++)
      {
        const u16* vram = &m_vram_shadow[((top + y) * VRAM_WIDTH) + left];
        for (u32 x = 0; x < width; x++)
          image.SetPixel(x, y, RGBA5551ToRGBA8888(vram[x]));
      }
    }
    break;

    default:
      break;
  }

  if (!image.IsValid())
  {
    Log_ErrorPrintf("Image invalid");
    return;
  }

  if (g_settings.texture_replacements.dump_force_alpha_channel)
  {
    for (u32 y = 0; y < image.GetHeight(); y++)
    {
      for (u32 x = 0; x < image.GetWidth(); x++)
        image.SetPixel(x, y, image.GetPixel(x, y) | 0xFF000000u);
    }
  }

  Log_InfoPrintf("Dumping %ux%u texture to '%s'", image.GetWidth(), image.GetHeight(), filename.c_str());
  if (!Common::WriteImageToFile(image, filename.c_str()))
    Log_ErrorPrintf("Failed to dump %ux%u texture to '%s'", image.GetWidth(), image.GetHeight(), filename.c_str());
}

void TextureReplacements::DumpPendingWrites()
{
  for (auto iter = m_pending_vram_writes.begin(); iter != m_pending_vram_writes.end();)
  {
    PendingVRAMWrite& pvw = *iter;
    if (CanDumpPendingVRAMWrite(pvw, true))
    {
      DumpVRAMWriteForTexture(pvw);
      iter = m_pending_vram_writes.erase(iter);
    }
    else
    {
      ++iter;
    }
  }
}

void TextureReplacements::Shutdown()
{
  DumpPendingWrites();
  m_pending_vram_writes.clear();
  m_texture_cache.clear();
  m_vram_write_replacements.clear();
  m_game_id.clear();
  m_vram_shadow.fill(0);
}

std::string TextureReplacements::GetSourceDirectory() const
{
  return g_host_interface->GetUserDirectoryRelativePath("textures/%s", m_game_id.c_str());
}

TextureReplacementHash TextureReplacements::GetVRAMWriteHash(u32 width, u32 height, const void* pixels) const
{
  XXH128_hash_t hash = XXH3_128bits(pixels, width * height * sizeof(u16));
  return {hash.low64, hash.high64};
}

TextureReplacementHash TextureReplacements::GetVRAMHash(u32 left, u32 top, u32 width, u32 height) const
{
  XXH3_state_t* state = XXH3_createState();
  for (u32 y = 0; y < height; y++)
    XXH3_128bits_update(state, &m_vram_shadow[top * VRAM_WIDTH + left], width * sizeof(u16));

  XXH128_hash_t hash = XXH3_128bits_digest(state);
  XXH3_freeState(state);
  return {hash.low64, hash.high64};
}

std::string TextureReplacements::GetVRAMWriteDumpFilename(u32 width, u32 height, const void* pixels) const
{
  if (m_game_id.empty())
    return {};

  const TextureReplacementHash hash = GetVRAMWriteHash(width, height, pixels);
  std::string filename = g_host_interface->GetUserDirectoryRelativePath("dump/textures/%s/vram-write-%s.png",
                                                                        m_game_id.c_str(), hash.ToString().c_str());

  if (FileSystem::FileExists(filename.c_str()))
    return {};

  const std::string dump_directory =
    g_host_interface->GetUserDirectoryRelativePath("dump/textures/%s", m_game_id.c_str());
  if (!FileSystem::DirectoryExists(dump_directory.c_str()) &&
      !FileSystem::CreateDirectory(dump_directory.c_str(), false))
  {
    return {};
  }

  return filename;
}

std::string TextureReplacements::GetTextureDumpFilename(const PendingVRAMWrite& vrw) const
{
  if (m_game_id.empty())
    return {};

  std::string filename = g_host_interface->GetUserDirectoryRelativePath("dump/textures/%s/texture-%s.png",
                                                                        m_game_id.c_str(), vrw.hash.ToString().c_str());

  if (FileSystem::FileExists(filename.c_str()))
    return {};

  const std::string dump_directory =
    g_host_interface->GetUserDirectoryRelativePath("dump/textures/%s", m_game_id.c_str());
  if (!FileSystem::DirectoryExists(dump_directory.c_str()) &&
      !FileSystem::CreateDirectory(dump_directory.c_str(), false))
  {
    return {};
  }

  return filename;
}

void TextureReplacements::Reload()
{
  m_vram_write_replacements.clear();

  if (g_settings.texture_replacements.AnyReplacementsEnabled())
    FindTextures(GetSourceDirectory());

  if (g_settings.texture_replacements.preload_textures)
    PreloadTextures();

  PurgeUnreferencedTexturesFromCache();
}

void TextureReplacements::PurgeUnreferencedTexturesFromCache()
{
  TextureCache old_map = std::move(m_texture_cache);
  for (const auto& it : m_vram_write_replacements)
  {
    auto it2 = old_map.find(it.second);
    if (it2 != old_map.end())
    {
      m_texture_cache[it.second] = std::move(it2->second);
      old_map.erase(it2);
    }
  }
}

bool TextureReplacements::ParseReplacementFilename(const std::string& filename,
                                                   TextureReplacementHash* replacement_hash,
                                                   ReplacmentType* replacement_type)
{
  const char* extension = std::strrchr(filename.c_str(), '.');
  const char* title = std::strrchr(filename.c_str(), '/');
#ifdef WIN32
  const char* title2 = std::strrchr(filename.c_str(), '\\');
  if (title2 && (!title || title2 > title))
    title = title2;
#endif

  if (!title || !extension)
    return false;

  title++;

  const char* hashpart;

  if (StringUtil::Strncasecmp(title, "vram-write-", 11) == 0)
  {
    hashpart = title + 11;
    *replacement_type = ReplacmentType::VRAMWrite;
  }
  else
  {
    return false;
  }

  if (!replacement_hash->ParseString(std::string_view(hashpart, static_cast<size_t>(extension - hashpart))))
    return false;

  extension++;

  bool valid_extension = false;
  for (const char* test_extension : {"png", "jpg", "tga", "bmp"})
  {
    if (StringUtil::Strcasecmp(extension, test_extension) == 0)
    {
      valid_extension = true;
      break;
    }
  }

  return valid_extension;
}

void TextureReplacements::FindTextures(const std::string& dir)
{
  FileSystem::FindResultsArray files;
  FileSystem::FindFiles(dir.c_str(), "*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_RECURSIVE, &files);

  for (FILESYSTEM_FIND_DATA& fd : files)
  {
    if (fd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY)
      continue;

    TextureReplacementHash hash;
    ReplacmentType type;
    if (!ParseReplacementFilename(fd.FileName, &hash, &type))
      continue;

    switch (type)
    {
      case ReplacmentType::VRAMWrite:
      {
        auto it = m_vram_write_replacements.find(hash);
        if (it != m_vram_write_replacements.end())
        {
          Log_WarningPrintf("Duplicate VRAM write replacement: '%s' and '%s'", it->second.c_str(), fd.FileName.c_str());
          continue;
        }

        m_vram_write_replacements.emplace(hash, std::move(fd.FileName));
      }
      break;
    }
  }

  Log_InfoPrintf("Found %zu replacement VRAM writes for '%s'", m_vram_write_replacements.size(), m_game_id.c_str());
}

const TextureReplacementTexture* TextureReplacements::LoadTexture(const std::string& filename)
{
  auto it = m_texture_cache.find(filename);
  if (it != m_texture_cache.end())
    return &it->second;

  Common::RGBA8Image image;
  if (!Common::LoadImageFromFile(&image, filename.c_str()))
  {
    Log_ErrorPrintf("Failed to load '%s'", filename.c_str());
    return nullptr;
  }

  Log_InfoPrintf("Loaded '%s': %ux%u", filename.c_str(), image.GetWidth(), image.GetHeight());
  it = m_texture_cache.emplace(filename, std::move(image)).first;
  return &it->second;
}

void TextureReplacements::PreloadTextures()
{
  static constexpr float UPDATE_INTERVAL = 1.0f;

  Common::Timer last_update_time;
  u32 num_textures_loaded = 0;
  const u32 total_textures = static_cast<u32>(m_vram_write_replacements.size());

#define UPDATE_PROGRESS()                                                                                              \
  if (last_update_time.GetTimeSeconds() >= UPDATE_INTERVAL)                                                            \
  {                                                                                                                    \
    g_host_interface->DisplayLoadingScreen("Preloading replacement textures...", 0, static_cast<int>(total_textures),  \
                                           static_cast<int>(num_textures_loaded));                                     \
    last_update_time.Reset();                                                                                          \
  }

  for (const auto& it : m_vram_write_replacements)
  {
    UPDATE_PROGRESS();

    LoadTexture(it.second);
    num_textures_loaded++;
  }

#undef UPDATE_PROGRESS
}
