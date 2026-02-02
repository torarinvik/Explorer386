# Explorer DOSBox Port - Implementation Status

## Project Overview
Porting Explorer (8086tiny fork) instrumentation capabilities into DOSBox-Staging for 386+ CPU support.

## Implementation Status: PHASE 1 COMPLETE ✅

### Completed Files in `dosbox-staging/src/explorer/`:
1. **CMakeLists.txt** - Build config with EXPLORER_ENABLED define
2. **explorer.h** - Main instrumentation API (Instrumenter class, Config, hooks)
3. **explorer.cpp** - Coverage tracking, edge logging, stall detection, loop detection
4. **explorer_data.h** - Data access tracking header (32-bit read/write support)
5. **explorer_data.cpp** - Data bucket tracking implementation
6. **explorer_nn.h** - Neural network policy header (LibTorch optional)
7. **explorer_nn.cpp** - Policy network and action queue implementation
8. **explorer_hooks.h** - CPU core integration macros
9. **explorer_api.h** - Public API for external control
10. **explorer_api.cpp** - API implementation

### DOSBox Integration Points Modified:
1. **dosbox-staging/CMakeLists.txt** - Added OPT_EXPLORER and OPT_EXPLORER_LIBTORCH options
2. **dosbox-staging/src/CMakeLists.txt** - Added explorer subdirectory conditional
3. **dosbox-staging/src/cpu/core_normal.cpp** - Added EXPLORER_PRE/POST_INSTRUCTION hooks
4. **dosbox-staging/src/cpu/cpu.cpp** - Added EXPLORER_NOTE_INTERRUPT hook

### Build Configuration:
```bash
cmake .. -DOPT_EXPLORER=ON           # Enable basic instrumentation
cmake .. -DOPT_EXPLORER=ON -DOPT_EXPLORER_LIBTORCH=ON  # With RL policy support
```

### All Files Pass Syntax Check ✅
Tested with: `clang++ -std=c++20 -fsyntax-only -DEXPLORER_ENABLED`

## Next Steps for Full Integration:
1. Add memory data hooks to paging.h (optional, for detailed data tracking)
2. Add port I/O hooks in port.cpp
3. Add hooks to other CPU cores (core_dyn_x86, core_dynrec) if needed
4. Create test harness / CLI for Explorer mode
5. Port Python training scripts from original Explorer

## Key Architecture Decisions:
- Explorer is a **separate module** that hooks into DOSBox, not a replacement CPU
- Uses compile-time EXPLORER_ENABLED flag for zero overhead when disabled
- Follows DOSBox-staging's CMake build structure
- Supports both physical coverage (unique PCs) and edge coverage (PC transitions)
- Data tracking has bucket-based design with configurable granularity
- LibTorch support is optional and detected at configure time
