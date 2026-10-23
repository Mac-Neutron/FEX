// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <csignal>

namespace FEXCore {
namespace Core {
  struct InternalThreadState;

  enum SignalNumber {
#ifndef _WIN32
    FAULT_SIGSEGV = SIGSEGV,
    FAULT_SIGTRAP = SIGTRAP,
    FAULT_SIGILL = SIGILL,
#else
    FAULT_SIGSEGV = 11,
    FAULT_SIGTRAP = 5,
    FAULT_SIGILL = 4,
#endif
  };
} // namespace Core

struct SignalDelegatorConfig {
  using SRAIndexMapping = std::array<uint8_t, 16>;

  // Dispatcher information
  uint64_t DispatcherBegin;
  uint64_t DispatcherEnd;

  // Dispatcher entrypoint.
  uint64_t AbsoluteLoopTopAddress {};
  uint64_t AbsoluteLoopTopAddressFillSRA {};

  // Signal return pointers.
  uint64_t SignalHandlerReturnAddress {};
  uint64_t SignalHandlerReturnAddressRT {};

  // Pause handlers.
  uint64_t PauseReturnInstruction {};
  uint64_t ThreadPauseHandlerAddressSpillSRA {};
  uint64_t ThreadPauseHandlerAddress {};

  // Stop handlers.
  uint64_t ThreadStopHandlerAddressSpillSRA;
  uint64_t ThreadStopHandlerAddress {};

  // SRA information.
  uint16_t SRAGPRCount;
  uint16_t SRAFPRCount;

  // SRA index mapping.
  SRAIndexMapping SRAGPRMapping;
  SRAIndexMapping SRAFPRMapping;

  // Where the guest signal stubs (SIGILL, SIGTRAP, SIGSEGV) spill the static registers before they trap: [begin, end)
  // each. Until a stub's spill ends, the guest registers are in the host registers, as in JIT code.
  std::array<std::array<uint64_t, 2>, 3> GuestSignalSpillRanges {};
};

class SignalDelegator {
public:
  virtual ~SignalDelegator() = default;

  void SetConfig(const SignalDelegatorConfig& Config) {
    this->Config = Config;
  }

  const SignalDelegatorConfig& GetConfig() const {
    return Config;
  }

  virtual uintptr_t GetThunkCallbackRET() const {
    return 0;
  }

protected:
  SignalDelegatorConfig Config;
};
} // namespace FEXCore
