// SPDX-License-Identifier: MIT
#pragma once

#include <FEXCore/Utils/IntervalList.h>
#include <FEXCore/fextl/unordered_map.h>
#include <FEXCore/fextl/set.h>
#include <FEXCore/fextl/string.h>

#include <algorithm>
#include <string_view>

namespace FEX::VolatileMetadata {
struct ExtendedVolatileMetadata {
  FEXCore::IntervalList<uint64_t> VolatileValidRanges;
  fextl::set<uint64_t> VolatileInstructions;
  bool ModuleTSODisabled;
};

fextl::unordered_map<fextl::string, ExtendedVolatileMetadata> ParseExtendedVolatileMetadata(std::string_view ListOfDescriptors);

inline void ApplyFEXExtendedVolatileMetadata(FEX::VolatileMetadata::ExtendedVolatileMetadata& ExtendedMetaData,
                                             fextl::set<uint64_t>& VolatileInstructions, FEXCore::IntervalList<uint64_t>& VolatileValidRanges,
                                             uint64_t Address, uint64_t EndAddress, uint64_t FileOffset = 0, uint64_t FileOffsetEnd = ~0ULL) {
  // Load FEX extended volatile metadata.
  // Walk the volatile instructions first if they exist.
  for (const auto it_inst : ExtendedMetaData.VolatileInstructions) {
    const auto inst_address = it_inst + Address;
    if (inst_address < EndAddress) {
      VolatileInstructions.emplace(Address + it_inst);
    } else {
      LogMan::Msg::DFmt("Volatile instruction 0x{:x} couldn't fit in to module range [0x{:x}, 0x{:x}). Not adding anymore volatile "
                        "instructions. Inspect your config!",
                        inst_address, Address, EndAddress);
      return;
    }
  }

  // Walk the volatile list, each range clamped to [Address, EndAddress) so it can't outlive the mapping (batch Task 4).
  const uint64_t Size = EndAddress - Address;
  for (const auto it_ranges : ExtendedMetaData.VolatileValidRanges) {
    if (it_ranges.Offset >= FileOffset && it_ranges.End < FileOffsetEnd) {
      const uint64_t Begin = std::min(it_ranges.Offset - FileOffset, Size);
      const uint64_t End = std::min(it_ranges.End - FileOffset, Size);
      if (Begin < End) {
        VolatileValidRanges.Insert({Address + Begin, Address + End});
      }
    }
  }

  // If it is fully disabled, then set the entire module range
  if (ExtendedMetaData.ModuleTSODisabled) {
    VolatileValidRanges.Clear();
    VolatileValidRanges.Insert({Address, EndAddress});
  }
}

} // namespace FEX::VolatileMetadata
