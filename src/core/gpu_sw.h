#pragma once
#include "common/heap_array.h"
#include "gpu.h"
#include "gpu_sw_backend.h"
#include <array>
#include <memory>
#include <vector>

class HostDisplayTexture;

class GPU_SW final : public GPU
{
public:
  GPU_SW();
  ~GPU_SW() override;

  bool IsHardwareRenderer() const override;

  bool Initialize(HostDisplay* host_display) override;
  void Reset() override;

protected:
  void ReadVRAM(u32 x, u32 y, u32 width, u32 height) override;
  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color) override;
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data) override;
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height) override;

  void CopyOut15Bit(u32 src_x, u32 src_y, u32 width, u32 height, u32 field, bool interlaced, bool interleaved);
  void CopyOut24Bit(u32 src_x, u32 src_y, u32 skip_x, u32 width, u32 height, u32 field, bool interlaced, bool interleaved);

  void ClearDisplay() override;
  void UpdateDisplay() override;

  void DispatchRenderCommand() override;

  void FillBackendCommandParameters(GPUBackendCommand* cmd);
  void FillDrawCommand(GPUBackendDrawCommand* cmd, GPURenderCommand rc);

  HeapArray<u8, VRAM_WIDTH * VRAM_HEIGHT * sizeof(u32)> m_display_texture_buffer;

  GPU_SW_Backend m_backend;
};
