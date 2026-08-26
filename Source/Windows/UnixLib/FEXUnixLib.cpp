// SPDX-License-Identifier: MIT
#include <cstdint>
#include <type_traits>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#ifndef __APPLE__
#include <sys/prctl.h>
#endif
#include <unistd.h>

#include "FEXUnixLib.h"

// Equivalent to C++23's std::to_underlying.
template<typename Enum>
[[nodiscard]]
constexpr std::underlying_type_t<Enum> ToUnderlying(Enum e) noexcept {
  return static_cast<std::underlying_type_t<Enum>>(e);
}

using NTSTATUS = int32_t;
using unixlib_entry_t = NTSTATUS (*)(void*);

[[maybe_unused]] constexpr NTSTATUS STATUS_SUCCESS = 0;
[[maybe_unused]] constexpr NTSTATUS STATUS_NOT_SUPPORTED = 0xC00000BB;
[[maybe_unused]] constexpr NTSTATUS STATUS_INTERNAL_ERROR = 0xC00000E5;

#ifndef __APPLE__
#ifndef PR_GET_MEM_MODEL
#define PR_GET_MEM_MODEL 0x6d4d444c
#endif
#ifndef PR_SET_MEM_MODEL
#define PR_SET_MEM_MODEL 0x4d4d444c
#endif
#ifndef PR_SET_MEM_MODEL_DEFAULT
#define PR_SET_MEM_MODEL_DEFAULT 0
#endif
#ifndef PR_SET_MEM_MODEL_TSO
#define PR_SET_MEM_MODEL_TSO 1
#endif
#ifndef PR_ARM64_SET_UNALIGN_ATOMIC
#define PR_ARM64_SET_UNALIGN_ATOMIC 0x46455849
#endif
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif
#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif
#endif

static NTSTATUS FEXUnixLib_SetHardwareTSOControlHandler([[maybe_unused]] void* _Args) {
  auto Args = reinterpret_cast<const FEXUnixLib_SetHardwareTSOControlArgs*>(_Args);
#ifdef __APPLE__
  if (Args->Enable) {
    return STATUS_NOT_SUPPORTED;
  }

  return STATUS_SUCCESS;
#else
  if (Args->Enable) {
    int ret = prctl(PR_GET_MEM_MODEL, 0, 0, 0, 0);
    if (ret == PR_SET_MEM_MODEL_DEFAULT) {
      return prctl(PR_SET_MEM_MODEL, PR_SET_MEM_MODEL_TSO, 0, 0, 0) ? STATUS_NOT_SUPPORTED : STATUS_SUCCESS;
    }
    return ret == PR_SET_MEM_MODEL_TSO ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
  }

  prctl(PR_SET_MEM_MODEL, PR_SET_MEM_MODEL_DEFAULT, 0, 0, 0);
  return STATUS_SUCCESS;
#endif
}

static NTSTATUS FEXUnixLib_SetKernelUnalignedAtomicControlHandler([[maybe_unused]] void* _Args) {
#ifdef __APPLE__
  return STATUS_NOT_SUPPORTED;
#else
  auto Args = reinterpret_cast<const FEXUnixLib_SetKernelUnalignedAtomicControl*>(_Args);
  return prctl(PR_ARM64_SET_UNALIGN_ATOMIC, Args->Flags, 0, 0, 0) == 0 ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
#endif
}

static NTSTATUS FEXUnixLib_MadviseHandler([[maybe_unused]] void* _Args) {
  auto Args = reinterpret_cast<const FEXUnixLib_Madvise*>(_Args);
#ifdef __APPLE__
  constexpr int LINUX_MADV_NORMAL = 0;
  constexpr int LINUX_MADV_RANDOM = 1;
  constexpr int LINUX_MADV_SEQUENTIAL = 2;
  constexpr int LINUX_MADV_WILLNEED = 3;
  constexpr int LINUX_MADV_DONTNEED = 4;
  constexpr int LINUX_MADV_FREE = 8;

  int Advise {};
  switch (Args->Advise) {
    case LINUX_MADV_NORMAL: Advise = MADV_NORMAL; break;
    case LINUX_MADV_RANDOM: Advise = MADV_RANDOM; break;
    case LINUX_MADV_SEQUENTIAL: Advise = MADV_SEQUENTIAL; break;
    case LINUX_MADV_WILLNEED: Advise = MADV_WILLNEED; break;
    case LINUX_MADV_DONTNEED: Advise = MADV_DONTNEED; break;
    case LINUX_MADV_FREE: Advise = MADV_FREE; break;
    default:
      return STATUS_SUCCESS;
  }

  madvise(const_cast<void*>(Args->Addr), Args->Size, Advise);
  return STATUS_SUCCESS;
#else
  return madvise(const_cast<void*>(Args->Addr), Args->Size, Args->Advise) == 0 ? STATUS_SUCCESS : STATUS_INTERNAL_ERROR;
#endif
}

static NTSTATUS FEXUnixLib_SetVMANameHandler([[maybe_unused]] void* _Args) {
#ifdef __APPLE__
  return STATUS_SUCCESS;
#else
  auto Args = reinterpret_cast<const FEXUnixLib_SetVMAName*>(_Args);
  return prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, reinterpret_cast<unsigned long>(Args->Addr), Args->Size, Args->Name) == 0 ?
           STATUS_SUCCESS :
           STATUS_INTERNAL_ERROR;
#endif
}

#ifndef __APPLE__
constexpr static int USER_PERMS = S_IRWXU | S_IRWXG | S_IRWXO;
static inline void* InitializeSHM(uint32_t MapSize, uint32_t MaxSize) {
  void* Base {};
  const std::string Name = "fex-" + std::to_string(getpid()) + "-stats";
  int fd = shm_open(Name.c_str(), O_CREAT | O_TRUNC | O_RDWR, USER_PERMS);

  if (fd == -1) {
    return nullptr;
  }

  if (ftruncate(fd, MapSize) == -1) {
    goto err;
  }

  // Reserve a region of MAX_STATS_SIZE so we can grow the allocation buffer.
  // Number of thread slots when ThreadStatsHeader == 64bytes and ThreadStats == 40bytes:
  // 1 page: 99 slots
  // 1 MB: 26211 slots
  // 128 MB: 3355440 slots
  Base = mmap(nullptr, MaxSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (Base == MAP_FAILED) {
    Base = nullptr;
    goto err;
  }

  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, reinterpret_cast<void*>(Base), MaxSize, "FEXMem_Misc");

  // Allocate a small working shared space for now, grow as necessary.
  {
    auto SharedBase = mmap(Base, MapSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (SharedBase == MAP_FAILED) {
      ::munmap(Base, MaxSize);
      Base = nullptr;
      goto err;
    }
  }

err:
  close(fd);

  return Base;
}

static inline bool AllocateMoreSlots(void* SHMBase, uint32_t NewSize) {
  bool Result {};
  const std::string Name = "fex-" + std::to_string(getpid()) + "-stats";

  // When allocating more slots, open the fd without O_TRUNC | O_CREAT.
  int fd = shm_open(Name.c_str(), O_RDWR, USER_PERMS);
  if (fd == -1) {
    return false;
  }

  if (ftruncate(fd, NewSize) == -1) {
    goto err;
  }

  {
    auto SharedBase = mmap(SHMBase, NewSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (SharedBase == MAP_FAILED) {
      goto err;
    }
  }

  Result = true;

err:
  close(fd);
  return Result;
}
#endif

static NTSTATUS FEXUnixLib_GetSHMStatsVMAHandler([[maybe_unused]] void* _Args) {
#ifdef __APPLE__
  return STATUS_NOT_SUPPORTED;
#else
  auto Args = reinterpret_cast<FEXUnixLib_GetSHMStatsVMA*>(_Args);
  if (Args->SHMBase == nullptr) {
    Args->SHMBase = InitializeSHM(Args->MapSize, Args->MaxSize);
    return Args->SHMBase ? STATUS_SUCCESS : STATUS_INTERNAL_ERROR;
  }

  return AllocateMoreSlots(Args->SHMBase, Args->MapSize) ? STATUS_SUCCESS : STATUS_INTERNAL_ERROR;
#endif
}

static NTSTATUS FEXUnixLib_DeleteSHMStatsFileHandler([[maybe_unused]] void* _Args) {
#ifdef __APPLE__
  return STATUS_SUCCESS;
#else
  const std::string Name = "fex-" + std::to_string(getpid()) + "-stats";
  shm_unlink(Name.c_str());
  return STATUS_SUCCESS;
#endif
}

extern "C" const unixlib_entry_t __wine_unix_call_funcs[] = {
  FEXUnixLib_SetHardwareTSOControlHandler,
  FEXUnixLib_SetKernelUnalignedAtomicControlHandler,
  FEXUnixLib_MadviseHandler,
  FEXUnixLib_SetVMANameHandler,
  FEXUnixLib_GetSHMStatsVMAHandler,
  FEXUnixLib_DeleteSHMStatsFileHandler,
};

static_assert(sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) == ToUnderlying(FEXUnixLibFunctions::MAX));
