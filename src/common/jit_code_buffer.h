#pragma once
#include "types.h"

class JitCodeBuffer
{
public:
  JitCodeBuffer();
  JitCodeBuffer(u32 size, u32 far_code_size, bool double_mapped = false);
  JitCodeBuffer(void* buffer, u32 size, u32 far_code_size, u32 guard_size);
  ~JitCodeBuffer();

  bool Allocate(u32 size = 64 * 1024 * 1024, u32 far_code_size = 0);
  bool AllocateDoubleMapped(u32 size = 64 * 1024 * 1024, u32 far_code_size = 0);
  bool Initialize(void* buffer, u32 size, u32 far_code_size = 0, u32 guard_size = 0);
  void Destroy();
  void Reset();

  u8* GetCodeWritePointer() const { return m_code_write_ptr; }
  u8* GetCodeExecutePointer() const { return m_code_execute_ptr; }

  u8* GetFreeCodeWritePointer() const { return m_code_write_ptr + m_code_used; }
  u8* GetFreeCodeExecutePointer() const { return m_code_execute_ptr + m_code_used; }
  u32 GetFreeCodeSpace() const { return static_cast<u32>(m_code_size - m_code_used); }
  void CommitCode(u32 length);

  u8* GetFarCodeWritePointer() const { return m_far_code_write_ptr; }
  u8* GetFarCodeExecutePointer() const { return m_far_code_execute_ptr; }

  u8* GetFreeFarCodeWritePointer() const { return m_far_code_write_ptr + m_far_code_used; }
  u8* GetFreeFarCodeExecutePointer() const { return m_far_code_execute_ptr + m_far_code_used; }
  u32 GetFreeFarCodeSpace() const { return static_cast<u32>(m_far_code_size - m_far_code_used); }
  void CommitFarCode(u32 length);

  /// Adjusts the free code pointer to the specified alignment, padding with bytes.
  /// Assumes alignment is a power-of-two.
  void Align(u32 alignment, u8 padding_value);

  /// Flushes the instruction cache on the host for the specified range.
  static void FlushInstructionCache(void* address, u32 size);

private:
  // File mapping used for double-mapping.
  void* m_file_handle = nullptr;

  u8* m_code_write_ptr = nullptr;
  u8* m_code_execute_ptr = nullptr;
  u32 m_code_size = 0;
  u32 m_code_used = 0;

  u8* m_far_code_write_ptr = nullptr;
  u8* m_far_code_execute_ptr = nullptr;
  u32 m_far_code_size = 0;
  u32 m_far_code_used = 0;

  u32 m_total_size = 0;
  u32 m_guard_size = 0;
  u32 m_old_protection = 0;
  bool m_owns_buffer = false;
};

