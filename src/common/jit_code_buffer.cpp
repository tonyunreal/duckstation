#include "jit_code_buffer.h"
#include "align.h"
#include "assert.h"
#include "cpu_detect.h"
#include "log.h"
#include "string_util.h"
#include <algorithm>

#if defined(WIN32)
#include "windows_headers.h"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

Log_SetChannel(JitCodeBuffer);

JitCodeBuffer::JitCodeBuffer() = default;

JitCodeBuffer::JitCodeBuffer(u32 size, u32 far_code_size, bool double_mapped /* = false */)
{
  if (!double_mapped)
  {
    if (!Allocate(size, far_code_size))
      Panic("Failed to allocate code space");
  }
  else
  {
    if (!AllocateDoubleMapped(size, far_code_size))
      Panic("Failed to allocate double-mapped code space");
  }
}

JitCodeBuffer::JitCodeBuffer(void* buffer, u32 size, u32 far_code_size, u32 guard_pages)
{
  if (!Initialize(buffer, size, far_code_size))
    Panic("Failed to initialize code space");
}

JitCodeBuffer::~JitCodeBuffer()
{
  Destroy();
}

bool JitCodeBuffer::Allocate(u32 size /* = 64 * 1024 * 1024 */, u32 far_code_size /* = 0 */)
{
  Destroy();

  m_total_size = size + far_code_size;

#if defined(WIN32)
  m_code_write_ptr = m_code_execute_ptr =
    static_cast<u8*>(VirtualAlloc(nullptr, m_total_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__) || defined(__HAIKU__)
  m_code_write_ptr = m_code_execute_ptr = static_cast<u8*>(
    mmap(nullptr, m_total_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
#else
  m_code_write_ptr = m_code_execute_ptr = nullptr;
#endif

  if (!m_code_write_ptr)
    return false;

  m_code_size = size;
  m_code_used = 0;

  m_far_code_write_ptr = static_cast<u8*>(m_code_write_ptr) + size;
  m_far_code_execute_ptr = static_cast<u8*>(m_code_execute_ptr) + size;
  m_far_code_size = far_code_size;
  m_far_code_used = 0;

  m_old_protection = 0;
  m_owns_buffer = true;
  return true;
}

bool JitCodeBuffer::AllocateDoubleMapped(u32 size /* = 64 * 1024 * 1024 */, u32 far_code_size /* = 0 */)
{
  Destroy();

#if defined(WIN32)
  const std::string file_mapping_name = StringUtil::StdStringFromFormat("duckstation_%u.jit", GetCurrentProcessId());
  m_file_handle = static_cast<void*>(
    CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, size, file_mapping_name.c_str()));
  if (!m_file_handle)
  {
    Log_ErrorPrintf("CreateFileMapping failed: %u", GetLastError());
    return false;
  }

  m_code_write_ptr =
    static_cast<u8*>(MapViewOfFile(static_cast<HANDLE>(m_file_handle), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size));
  if (!m_code_write_ptr)
  {
    Log_ErrorPrintf("MapViewOfFile for write failed: %u", GetLastError());
    CloseHandle(static_cast<HANDLE>(m_file_handle));
    m_file_handle = nullptr;
    return false;
  }
  m_code_execute_ptr =
    static_cast<u8*>(MapViewOfFile(static_cast<HANDLE>(m_file_handle), FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, size));
  if (!m_code_execute_ptr)
  {
    Log_ErrorPrintf("MapViewOfFile for execute failed: %u", GetLastError());
    UnmapViewOfFile(m_code_write_ptr);
    m_code_write_ptr = nullptr;
    CloseHandle(static_cast<HANDLE>(m_file_handle));
    m_file_handle = nullptr;
    return false;
  }
#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__) || defined(__HAIKU__)
  const std::string file_mapping_name = StringUtil::StdStringFromFormat("duckstation_%u.jit", getpid());

  const int shmem_fd = shm_open(file_mapping_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (shmem_fd < 0)
  {
    Log_ErrorPrintf("shm_open failed: %d", errno);
    return false;
  }

  // we're not going to be opening this mapping in other processes, so remove the file
  shm_unlink(file_mapping_name.c_str());

  // ensure it's the correct size
  if (ftruncate64(shmem_fd, static_cast<off64_t>(size)) < 0)
  {
    Log_ErrorPrintf("ftruncate64(%u) failed: %d", size, errno);
    return false;
  }

  m_file_handle = reinterpret_cast<void*>(static_cast<intptr_t>(shmem_fd));
  m_code_write_ptr = static_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0));
  if (m_code_write_ptr == reinterpret_cast<void*>(-1))
  {
    m_code_write_ptr = nullptr;
    close(shmem_fd);
    m_file_handle = nullptr;
    return false;
  }

  m_code_execute_ptr = static_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_SHARED, shmem_fd, 0));
  if (m_code_execute_ptr == reinterpret_cast<void*>(-1))
  {
    m_code_execute_ptr = nullptr;
    munmap(m_code_write_ptr, size);
    m_code_write_ptr = nullptr;
    close(shmem_fd);
    m_file_handle = nullptr;
    return false;
  }
#else
  return false;
#endif

  m_total_size = size;
  m_guard_size = 0;
  m_code_size = size - far_code_size;
  m_code_used = 0;

  m_far_code_write_ptr = static_cast<u8*>(m_code_write_ptr) + m_code_size;
  m_far_code_execute_ptr = static_cast<u8*>(m_code_execute_ptr) + m_code_size;
  m_far_code_size = far_code_size;
  m_far_code_used = 0;

  m_old_protection = 0;
  m_owns_buffer = true;
  return true;
}

bool JitCodeBuffer::Initialize(void* buffer, u32 size, u32 far_code_size /* = 0 */, u32 guard_size /* = 0 */)
{
  Destroy();

  if ((far_code_size > 0 && guard_size >= far_code_size) || (far_code_size + (guard_size * 2)) > size)
    return false;

#if defined(WIN32)
  DWORD old_protect = 0;
  if (!VirtualProtect(buffer, size, PAGE_EXECUTE_READWRITE, &old_protect))
    return false;

  if (guard_size > 0)
  {
    DWORD old_guard_protect = 0;
    u8* guard_at_end = (static_cast<u8*>(buffer) + size) - guard_size;
    if (!VirtualProtect(buffer, guard_size, PAGE_NOACCESS, &old_guard_protect) ||
        !VirtualProtect(guard_at_end, guard_size, PAGE_NOACCESS, &old_guard_protect))
    {
      return false;
    }
  }

  m_code_write_ptr = m_code_execute_ptr = static_cast<u8*>(buffer);
  m_old_protection = static_cast<u32>(old_protect);
#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__) || defined(__HAIKU__)
  if (mprotect(buffer, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    return false;

  if (guard_size > 0)
  {
    u8* guard_at_end = (static_cast<u8*>(buffer) + size) - guard_size;
    if (mprotect(buffer, guard_size, PROT_NONE) != 0 || mprotect(guard_at_end, guard_size, PROT_NONE) != 0)
      return false;
  }

  // reasonable default?
  m_code_write_ptr = m_code_execute_ptr = static_cast<u8*>(buffer);
  m_old_protection = PROT_READ | PROT_WRITE;
#else
  m_code_write_ptr = m_code_execute_ptr = nullptr;
#endif

  if (!m_code_write_ptr)
    return false;

  m_total_size = size;
  m_guard_size = guard_size;
  m_code_used = guard_size;
  m_code_size = size - far_code_size - guard_size;

  m_far_code_write_ptr = m_code_write_ptr + guard_size + m_code_size;
  m_far_code_execute_ptr = m_code_execute_ptr + guard_size + m_code_size;
  m_far_code_size = far_code_size - guard_size;
  m_far_code_used = 0;

  m_owns_buffer = false;
  return true;
}

void JitCodeBuffer::Destroy()
{
  if (m_owns_buffer)
  {
#if defined(WIN32)
    if (m_file_handle)
    {
      UnmapViewOfFile(m_code_execute_ptr);
      m_code_execute_ptr = nullptr;
      UnmapViewOfFile(m_code_write_ptr);
      m_code_write_ptr = nullptr;
      CloseHandle(static_cast<HANDLE>(m_file_handle));
      m_file_handle = nullptr;
    }
    else
    {
      VirtualFree(m_code_write_ptr, 0, MEM_RELEASE);
    }
#elif defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__) || defined(__HAIKU__)
    if (m_file_handle)
    {
      munmap(m_code_execute_ptr, m_total_size);
      m_code_execute_ptr = nullptr;
      munmap(m_code_write_ptr, m_total_size);
      m_code_write_ptr = nullptr;
      close(static_cast<int>(reinterpret_cast<intptr_t>(m_file_handle)));
      m_file_handle = nullptr;
    }
    else
    {
      munmap(m_code_write_ptr, m_total_size);
    }
#endif
  }
  else if (m_code_write_ptr)
  {
#if defined(WIN32)
    DWORD old_protect = 0;
    VirtualProtect(m_code_write_ptr, m_total_size, m_old_protection, &old_protect);
#else
    mprotect(m_code_write_ptr, m_total_size, m_old_protection);
#endif
  }
}

void JitCodeBuffer::CommitCode(u32 length)
{
  if (length == 0)
    return;

#if defined(CPU_AARCH32) || defined(CPU_AARCH64)
  // ARM instruction and data caches are not coherent, we need to flush after every block.
  FlushInstructionCache(GetFreeCodeExecutePointer(), length);
#endif

  Assert(length <= (m_code_size - m_code_used));
  m_code_used += length;
}

void JitCodeBuffer::CommitFarCode(u32 length)
{
  if (length == 0)
    return;

#if defined(CPU_AARCH32) || defined(CPU_AARCH64)
  // ARM instruction and data caches are not coherent, we need to flush after every block.
  FlushInstructionCache(GetFreeFarCodeExecutePointer(), length);
#endif

  Assert(length <= (m_far_code_size - m_far_code_used));
  m_far_code_used += length;
}

void JitCodeBuffer::Reset()
{
  m_code_used = m_guard_size;
  std::memset(GetFreeCodeWritePointer(), 0, m_code_size);
  FlushInstructionCache(GetFreeCodeExecutePointer(), m_code_size);

  if (m_far_code_size > 0)
  {
    m_far_code_used = 0;
    std::memset(GetFreeFarCodeWritePointer(), 0, m_far_code_size);
    FlushInstructionCache(GetFreeFarCodeExecutePointer(), m_far_code_size);
  }
}

void JitCodeBuffer::Align(u32 alignment, u8 padding_value)
{
  DebugAssert(Common::IsPow2(alignment));
  const u32 num_padding_bytes = std::min(Common::AlignUpPow2(m_code_used, alignment) - m_code_used, GetFreeCodeSpace());
  std::memset(m_code_write_ptr + m_code_used, padding_value, num_padding_bytes);
  m_code_used += num_padding_bytes;
}

void JitCodeBuffer::FlushInstructionCache(void* address, u32 size)
{
#if defined(WIN32)
  ::FlushInstructionCache(GetCurrentProcess(), address, size);
#elif defined(__GNUC__) || defined(__clang__)
  __builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address) + size);
#else
#error Unknown platform.
#endif
}
