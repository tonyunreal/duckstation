#include "assert.h"
#include "cd_image.h"
#include "cd_subchannel_replacement.h"
#include "file_system.h"
#include "log.h"
#include <algorithm>
#include <cerrno>
#include <map>
#include <unordered_map>
Log_SetChannel(CDImagePPF);

class CDImagePPF : public CDImage
{
public:
  CDImagePPF();
  ~CDImagePPF() override;

  bool Open(const char* filename, std::unique_ptr<CDImage> parent_image);

  bool ReadSubChannelQ(SubChannelQ* subq) override;
  bool HasNonStandardSubchannel() const override;

protected:
  bool ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index) override;

private:
  bool ReadV3Patch(std::FILE* fp);
  u32 ReadFileIDDiz(std::FILE* fp, u32 version);

  bool AddPatch(u64 offset, const u8* patch, u32 patch_size);

  std::unique_ptr<CDImage> m_parent_image;
  std::vector<u8> m_replacement_data;
  std::unordered_map<u32, u32> m_replacement_map;
  u32 m_replacement_offset = 0;
};

CDImagePPF::CDImagePPF() = default;

CDImagePPF::~CDImagePPF()
{
  //
}

bool CDImagePPF::Open(const char* filename, std::unique_ptr<CDImage> parent_image)
{
  auto fp = FileSystem::OpenManagedCFile(filename, "rb");
  if (!fp)
  {
    Log_ErrorPrintf("Failed to open '%s'", filename);
    return false;
  }

  u32 magic;
  if (std::fread(&magic, sizeof(magic), 1, fp.get()) != 1)
  {
    Log_ErrorPrintf("Failed to read magic from '%s'", filename);
    return false;
  }

  // work out the offset from the start of the parent image which we need to patch
  // i.e. the two second implicit pregap on data sectors
  if (parent_image->GetTrack(0).mode != TrackMode::Audio)
    m_replacement_offset = parent_image->GetIndex(1).start_lba_on_disc;

  // copy all the stuff from the parent image
  m_filename = filename;
  m_tracks = parent_image->GetTracks();
  m_indices = parent_image->GetIndices();
  m_parent_image = std::move(parent_image);

  if (magic == '3FPP')
    return ReadV3Patch(fp.get());

  Log_ErrorPrintf("Unknown PPF magic %08X", magic);
  return false;
}

u32 CDImagePPF::ReadFileIDDiz(std::FILE* fp, u32 version)
{
  const int lenidx = (version == 2) ? 4 : 2;

  u32 magic;
  if (std::fseek(fp, -(lenidx + 4), SEEK_END) != 0 || std::fread(&magic, sizeof(magic), 1, fp) != 1)
  {
    Log_WarningPrintf("Failed to read diz magic");
    return 0;
  }

  if (magic != 'ZID.')
    return 0;

  u32 dlen;
  if (std::fseek(fp, -lenidx, SEEK_END) != 0 || std::fread(&dlen, sizeof(dlen), 1, fp) != 1)
  {
    Log_WarningPrintf("Failed to read diz length");
    return 0;
  }

  if (dlen > static_cast<u32>(std::ftell(fp)))
  {
    Log_WarningPrintf("diz length out of range");
    return 0;
  }

  std::string fdiz;
  fdiz.resize(dlen);
  if (std::fseek(fp, -(lenidx + 16 + static_cast<int>(dlen)), SEEK_END) != 1 ||
      std::fread(fdiz.data(), 1, dlen, fp) != dlen)
  {
    Log_WarningPrintf("Failed to read fdiz");
    return 0;
  }

  Log_InfoPrintf("File_Id.diz: %s", fdiz.c_str());
  return dlen;
}

bool CDImagePPF::ReadV3Patch(std::FILE* fp)
{
  enum : u32
  {
    DESC_SIZE = 50
  };

  char desc[DESC_SIZE + 1] = {};
  if (std::fseek(fp, 0, SEEK_SET) != 0 || std::fread(desc, sizeof(char), DESC_SIZE, fp) != DESC_SIZE)
  {
    Log_ErrorPrintf("Failed to read description");
    return false;
  }

  Log_InfoPrintf("Patch description: %s", desc);

  u32 idlen = ReadFileIDDiz(fp, 3);

  u8 image_type;
  u8 block_check;
  u8 undo;
  if (std::fseek(fp, 56, SEEK_SET) != 0 || std::fread(&image_type, sizeof(image_type), 1, fp) != 1 ||
      std::fread(&block_check, sizeof(block_check), 1, fp) != 1 || std::fread(&undo, sizeof(undo), 1, fp) != 1)
  {
    Log_ErrorPrintf("Failed to read headers");
    return false;
  }

  // TODO: Blockcheck

  std::fseek(fp, 0, SEEK_END);
  u32 count = static_cast<u32>(std::ftell(fp));

  u32 seekpos = (block_check) ? 1084 : 60;
  if (seekpos >= count)
  {
    Log_ErrorPrintf("File is too short");
    return false;
  }

  count -= seekpos;

  if (std::fseek(fp, seekpos, SEEK_SET) != 0)
    return false;

  std::vector<u8> temp;

  while (count > 0)
  {
    u64 offset;
    u8 chunk_size;
    if (std::fread(&offset, sizeof(offset), 1, fp) != 1 || std::fread(&chunk_size, sizeof(chunk_size), 1, fp) != 1)
    {
      Log_ErrorPrintf("Incomplete ppf");
      return false;
    }

    temp.resize(chunk_size);
    if (std::fread(temp.data(), 1, chunk_size, fp) != chunk_size)
    {
      Log_ErrorPrintf("Failed to read patch data");
      return false;
    }

    if (!AddPatch(offset, temp.data(), chunk_size))
      return false;

    count -= sizeof(offset) + sizeof(chunk_size) + chunk_size;
  }

  return true;
}

bool CDImagePPF::AddPatch(u64 offset, const u8* patch, u32 patch_size)
{
  while (patch_size > 0)
  {
    const u32 sector_index = Truncate32(offset / RAW_SECTOR_SIZE) + m_replacement_offset;
    const u32 sector_offset = Truncate32(offset % RAW_SECTOR_SIZE);
    if (sector_index >= m_parent_image->GetLBACount())
    {
      Log_ErrorPrintf("Sector %u in patch is out of range", sector_index);
      return false;
    }

    const u32 bytes_to_patch = std::min(patch_size, RAW_SECTOR_SIZE - sector_offset);

    const u32 replacement_buffer_start = static_cast<u32>(m_replacement_data.size());
    m_replacement_data.resize(m_replacement_data.size() + RAW_SECTOR_SIZE);
    if (!m_parent_image->ReadRawSector(&m_replacement_data[replacement_buffer_start]))
    {
      Log_ErrorPrintf("Failed to read sector %u from parent image", sector_index);
      return false;
    }

    // patch it!
    Log_DevPrintf("Patching %u bytes at sector %u offset %u", bytes_to_patch, sector_index, sector_offset);
    std::memcpy(&m_replacement_data[replacement_buffer_start + sector_offset], patch, bytes_to_patch);
    offset += bytes_to_patch;
    patch += bytes_to_patch;
    patch_size -= bytes_to_patch;
  }

  return true;
}

bool CDImagePPF::ReadSubChannelQ(SubChannelQ* subq)
{
  return m_parent_image->ReadSubChannelQ(subq);
}

bool CDImagePPF::HasNonStandardSubchannel() const
{
  return m_parent_image->HasNonStandardSubchannel();
}

bool CDImagePPF::ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index)
{
  DebugAssert(index.file_index == 0);

  const u32 sector_number = index.start_lba_on_disc + lba_in_index;
  const auto it = m_replacement_map.find(sector_number);
  if (it == m_replacement_map.end())
    return false;

  std::memcpy(buffer, &m_replacement_data[it->second], RAW_SECTOR_SIZE);
  return true;
}

std::unique_ptr<CDImage> CDImage::OverlayPPFPatch(const char* filename, std::unique_ptr<CDImage> parent_image)
{
  std::unique_ptr<CDImagePPF> memory_image = std::make_unique<CDImagePPF>();
  if (!memory_image->Open(filename, std::move(parent_image)))
    return {};

  return memory_image;
}
