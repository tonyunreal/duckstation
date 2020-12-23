#include "texture_replacements.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/string_util.h"
#include "host_interface.h"
#include "settings.h"
#include "xxhash.h"
#include <cinttypes>
Log_SetChannel(TextureReplacements);

TextureReplacements g_texture_replacements;

static constexpr u32 RGBA5551ToRGBA8888(u16 color)
{
  u8 r = Truncate8(color & 31);
  u8 g = Truncate8((color >> 5) & 31);
  u8 b = Truncate8((color >> 10) & 31);
  u8 a = Truncate8((color >> 15) & 1);

  // 00012345 -> 1234545
  b = (b << 3) | (b & 0b111);
  g = (g << 3) | (g & 0b111);
  r = (r << 3) | (r & 0b111);
  a = a ? 255 : 0;

  return ZeroExtend32(r) | (ZeroExtend32(g) << 8) | (ZeroExtend32(b) << 16) | (ZeroExtend32(a) << 24);
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

const TextureReplacements::ReplacementTexture* TextureReplacements::GetVRAMWriteReplacement(u32 width, u32 height,
                                                                                            const u16* pixels)
{
  const HashType hash = GetVRAMWriteHash(width, height, pixels);

  const auto it = m_vram_write_replacements.find(hash);
  if (it == m_vram_write_replacements.end())
    return nullptr;

  return LoadTexture(it->second);
}

void TextureReplacements::DumpVRAMWrite(u32 width, u32 height, const u16* pixels)
{
  std::string filename = GetVRAMWriteDumpFilename(width, height, pixels);
  if (filename.empty())
    return;

  Common::RGBA8Image image;
  image.SetSize(width, height);

  for (u32 y = 0; y < height; y++)
  {
    for (u32 x = 0; x < width; x++)
    {
      image.SetPixel(x, y, RGBA5551ToRGBA8888(*pixels));
      pixels++;
    }
  }

  if (g_settings.texture_replacements.dump_vram_write_force_alpha_channel)
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

void TextureReplacements::Shutdown()
{
  m_texture_cache.clear();
  m_vram_write_replacements.clear();
  m_game_id.clear();
}

std::string TextureReplacements::GetSourceDirectory() const
{
  return g_host_interface->GetUserDirectoryRelativePath("textures/%s", m_game_id.c_str());
}

TextureReplacements::HashType TextureReplacements::GetVRAMWriteHash(u32 width, u32 height, const u16* pixels) const
{
  return XXH3_64bits(pixels, width * height * sizeof(u16));
}

std::string TextureReplacements::GetVRAMWriteDumpFilename(u32 width, u32 height, const u16* pixels) const
{
  if (m_game_id.empty())
    return {};

  const HashType hash = GetVRAMWriteHash(width, height, pixels);
  std::string filename = g_host_interface->GetUserDirectoryRelativePath("dump/textures/%s/vram-write-%" PRIX64 ".png",
                                                                        m_game_id.c_str(), hash);

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

  if (!g_settings.texture_replacements.AnyReplacementsEnabled())
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

bool TextureReplacements::ParseReplacementFilename(const std::string& filename, HashType* replacement_hash,
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

  // std::optional<HashType> hash = StringUtil::FromChars<HashType>(std::string_view(hashpart, extension - hashpart));
  // if (!hash.has_value())
  // return false;

  *replacement_hash = std::strtoull(hashpart, nullptr, 16);

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

    HashType hash;
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

  Log_InfoPrintf("%zu replacement VRAM writes for '%s'", m_vram_write_replacements.size(), m_game_id.c_str());
}

const TextureReplacements::ReplacementTexture* TextureReplacements::LoadTexture(const std::string& filename)
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
  for (const auto& it : m_vram_write_replacements)
    LoadTexture(it.second);
}