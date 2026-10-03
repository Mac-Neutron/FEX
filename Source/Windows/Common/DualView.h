// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>

// FEX's executable memory on ARM64EC: one section mapped twice. Code runs from the read-execute view, which is marked
// ARM64EC code (MEM_EXTENDED_PARAMETER_EC_CODE), and is written through the read-write view (ARMEmitter::DualMap), so
// no page is ever writable and executable at once: macOS refuses that, and Wine would flip the page on every fault.
namespace FEX::Windows::DualView {
// Maps both views and points ARMEmitter::DualMap at them. Once, at process init, before any code is allocated; a
// failure is fatal.
void Init();
// First fit in the read-execute view; nullptr when it's full. Takes and returns read-execute addresses.
void* AllocExec(size_t Size);
// False when Ptr isn't in the read-execute view.
bool FreeExec(void* Ptr, size_t Size);
} // namespace FEX::Windows::DualView
