add_library(CommonWindowsRuntime STATIC LoadConfig.S)
add_subdirectory(CRT)
add_subdirectory(WinAPI)

target_link_libraries(CommonWindowsRuntime FEXCore_Base JemallocLibs)
target_compile_options(CommonWindowsRuntime PRIVATE -Wno-inconsistent-dllimport)
target_include_directories(CommonWindowsRuntime PRIVATE "${CMAKE_SOURCE_DIR}/Source/Windows/include/")

add_library(CommonWindows STATIC
  Allocator.cpp
  CPUFeatures.cpp
  DualView.cpp
  EnvironmentVariablesHandling.cpp
  FEXUnixLib.cpp
  SHMStats.cpp
  InvalidationTracker.cpp
  ImageTracker.cpp
  Logging.cpp
  Threads.cpp)

target_link_libraries(CommonWindows FEXCore_Base)
target_include_directories(CommonWindows PRIVATE "${CMAKE_SOURCE_DIR}/Source/Windows/include/")

# The CRT and Win32 API this runtime provides stay native code, as a libc does: code generation adds calls into them
# (__clear_cache's FlushInstructionCache, memcpy) after LTO has dropped what bitcode alone didn't call.
set_target_properties(CommonWindowsRuntime PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
