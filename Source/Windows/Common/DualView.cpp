// SPDX-License-Identifier: MIT
#include "Windows/Common/DualView.h"

#include <CodeEmitter/Buffer.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/MathUtils.h>
#include <FEXCore/Utils/TypeDefines.h>

#include <bitset>
#include <cstdint>
#include <mutex>
#include <ntstatus.h>
#include <windef.h>
#include <winternl.h>

namespace FEX::Windows::DualView {
namespace {
  // ponytail: one fixed 1 GiB pool. Wine backs it with a sparse file, so pages never written cost nothing.
  constexpr size_t PoolSize = size_t {1} << 30;
  constexpr size_t PageSize = FEXCore::Utils::FEX_PAGE_SIZE;

  std::mutex Lock;
  // ponytail: one bit per page and a linear first-fit scan; executable allocations are rare (code buffers, the
  // dispatcher, code caches).
  std::bitset<PoolSize / PageSize> Used;
  bool SaidFull;
  uint8_t* Exec;

  uint32_t Code(NTSTATUS Status) {
    return static_cast<uint32_t>(Status);
  }

  // A failure is logged: a page left no-access here would fault inside a later allocation.
  void Protect(void* Ptr, SIZE_T Size, ULONG Prot) {
    void* Base = Ptr;
    SIZE_T Bytes = Size;
    ULONG Old;
    if (NTSTATUS Status = NtProtectVirtualMemory(NtCurrentProcess(), &Base, &Bytes, Prot, &Old); !NT_SUCCESS(Status)) {
      LogMan::Msg::EFmt("DualView: protecting {:#x} bytes at {} as {:#x} failed: {:#x}", Size, Ptr, Prot, Code(Status));
    }
  }
} // namespace

// Fatal when it fails: FEX has no other executable memory on ARM64EC, and a failure here means the platform or Wine
// (its patch 11: ARM64EC code marks on section views) is broken.
void Init() {
  HANDLE Section;
  LARGE_INTEGER Size {.QuadPart = PoolSize};
  NTSTATUS Status = NtCreateSection(&Section, SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_MAP_EXECUTE, nullptr, &Size,
                                    PAGE_EXECUTE_READWRITE, SEC_COMMIT, nullptr);
  if (!NT_SUCCESS(Status)) {
    ERROR_AND_DIE_FMT("DualView: NtCreateSection for the {} MiB code pool failed: {:#x}", PoolSize >> 20, Code(Status));
  }

  // Plain views, mapped once for the life of the process: Wine keeps a placeholder's ARM64EC marks, so none is used.
  void* RW = nullptr;
  void* RX = nullptr;
  SIZE_T ViewSize = 0;
  Status = NtMapViewOfSectionEx(Section, NtCurrentProcess(), &RW, nullptr, &ViewSize, MEM_TOP_DOWN, PAGE_READWRITE, nullptr, 0);
  if (!NT_SUCCESS(Status)) {
    ERROR_AND_DIE_FMT("DualView: NtMapViewOfSectionEx of the code pool's read-write view failed: {:#x}", Code(Status));
  }
  MEM_EXTENDED_PARAMETER EC {};
  EC.Type = MemExtendedParameterAttributeFlags;
  EC.ULong64 = MEM_EXTENDED_PARAMETER_EC_CODE;
  ViewSize = 0;
  Status = NtMapViewOfSectionEx(Section, NtCurrentProcess(), &RX, nullptr, &ViewSize, MEM_TOP_DOWN, PAGE_EXECUTE_READ, &EC, 1);
  if (!NT_SUCCESS(Status)) {
    ERROR_AND_DIE_FMT("DualView: NtMapViewOfSectionEx of the code pool's read-execute view, as ARM64EC code, failed: {:#x}",
                      Code(Status));
  }
  NtClose(Section); // the views keep it

  Exec = static_cast<uint8_t*>(RX);
  ARMEmitter::DualMap::WriteOffset = static_cast<uint8_t*>(RW) - Exec;
  ARMEmitter::DualMap::ExecBase = reinterpret_cast<uintptr_t>(Exec);
  ARMEmitter::DualMap::ExecSize = PoolSize;
}

void* AllocExec(size_t Size) {
  const size_t Pages = FEXCore::AlignUp(Size, PageSize) / PageSize;
  std::scoped_lock lk(Lock);
  for (size_t i = 0, Run = 0; i < Used.size(); i++) {
    Run = Used[i] ? 0 : Run + 1;
    if (Run == Pages) {
      for (size_t j = i + 1 - Pages; j <= i; j++) {
        Used.set(j);
      }
      return Exec + (i + 1 - Pages) * PageSize;
    }
  }
  // The caller falls back to RWX memory, which Wine flips W^X on every fault: slower, not broken. Said once per process.
  if (!SaidFull) {
    SaidFull = true;
    LogMan::Msg::EFmt("DualView: no room for {:#x} bytes of code ({:#x} of the pool's {:#x} bytes in use): falling back to RWX memory",
                      Size, Used.count() * PageSize, PoolSize);
  }
  return nullptr;
}

bool FreeExec(void* Ptr, size_t Size) {
  const auto Offset = reinterpret_cast<uintptr_t>(Ptr) - reinterpret_cast<uintptr_t>(Exec);
  if (Offset >= PoolSize) {
    return false;
  }
  Size = FEXCore::AlignUp(Size, PageSize);
  // A code buffer leaves its guard page no-access in both views: the next allocation here starts from the views' own.
  Protect(Ptr, Size, PAGE_EXECUTE_READ);
  Protect(ARMEmitter::DualMap::WriteAddr(Ptr), Size, PAGE_READWRITE);
  std::scoped_lock lk(Lock);
  for (size_t i = Offset / PageSize; i < (Offset + Size) / PageSize; i++) {
    Used.reset(i);
  }
  return true;
}
} // namespace FEX::Windows::DualView
