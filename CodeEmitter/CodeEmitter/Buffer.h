// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ARMEmitter {
// Code memory that runs from one view and is written through another: FEX's ARM64EC frontend maps one section twice,
// read-execute and read-write (Source/Windows/Common/DualView.cpp), and sets these once, before any code is emitted.
// Code addresses are always the executable view's; a store into code goes to WriteAddr(address). Outside that view,
// and on every other host, WriteAddr is the identity.
namespace DualMap {
  inline uintptr_t ExecBase, ExecSize;
  inline intptr_t WriteOffset;

  template<typename T>
  T* WriteAddr(T* Addr) {
    const auto A = reinterpret_cast<uintptr_t>(Addr);
    return A - ExecBase < ExecSize ? reinterpret_cast<T*>(A + WriteOffset) : Addr;
  }
} // namespace DualMap

class Buffer {
public:
  Buffer() {
    SetBuffer(nullptr, 0);
  }

  Buffer(uint8_t* Base, uint64_t BaseSize) {
    SetBuffer(Base, BaseSize);
  }

  void SetBuffer(uint8_t* Base, uint64_t BaseSize) {
    BufferBase = Base;
    CurrentOffset = BufferBase;
    Size = BaseSize;
    WriteOffset = DualMap::WriteAddr(Base) - Base;
  }

  template<typename T>
  requires (std::is_trivially_copyable_v<T>)
  void dcn(const T& Data) {
    std::memcpy(WritePtr(CurrentOffset), &Data, sizeof(Data));
    CurrentOffset += sizeof(Data);
  }
  void dc8(uint8_t Data) {
    dcn(Data);
  }
  void dc16(uint16_t Data) {
    dcn(Data);
  }
  void dc32(uint32_t Data) {
    dcn(Data);
  }
  void dc64(uint64_t Data) {
    dcn(Data);
  }

  void EmitString(const char* String) {
    const auto StringLength = strlen(String);
    memcpy(WritePtr(CurrentOffset), String, StringLength);
    CurrentOffset += StringLength;
  }

  void Align(size_t Size = 4) {
    // Align the buffer to provided size.
    auto CurrentAlignment = reinterpret_cast<uint64_t>(CurrentOffset) & (Size - 1);
    if (!CurrentAlignment) {
      return;
    }
    std::memset(WritePtr(CurrentOffset), 0, Size - CurrentAlignment);
    CurrentOffset += Size - CurrentAlignment;
  }

  template<typename T>
  T GetCursorAddress() const {
    return reinterpret_cast<T>(CurrentOffset);
  }

  static void ClearICache(void* Begin, std::size_t Length) {
    __builtin___clear_cache(static_cast<char*>(Begin), static_cast<char*>(Begin) + Length);
  }

  size_t GetCursorOffset() const {
    return static_cast<size_t>(CurrentOffset - BufferBase);
  }

  uint8_t* GetBufferBase() const {
    return BufferBase;
  }

  void CursorIncrement(size_t Size) {
    CurrentOffset += Size;
  }

  void SetCursorOffset(size_t Offset) {
    CurrentOffset = BufferBase + Offset;
  }

  uint64_t GetBufferSize() const {
    return Size;
  }

  template<typename T>
  size_t GetCursorOffsetFromAddress(const T* Address) const {
    return static_cast<size_t>(reinterpret_cast<const uint8_t*>(Address) - BufferBase);
  }

protected:
  // Where a store to Addr, an address in this buffer, goes (see DualMap).
  template<typename T>
  T* WritePtr(T* Addr) const {
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(Addr) + WriteOffset);
  }

  uint8_t* BufferBase;
  uint8_t* CurrentOffset;
  uint64_t Size;
  ptrdiff_t WriteOffset;
};
} // namespace ARMEmitter
