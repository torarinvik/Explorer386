# Explorer DOSBox Port - Implementation Status

## Project Overview
Porting Explorer (8086tiny fork) instrumentation capabilities into DOSBox-Staging for 386+ CPU support.

## Implementation Status: PHASE 2 IN PROGRESS 🚧

### PHASE 1: Base Instrumentation ✅ COMPLETE
All files in `dosbox-staging/src/explorer/`:
- **CMakeLists.txt** - Build config with EXPLORER_ENABLED define
- **explorer.h** - Main instrumentation API (Instrumenter class, Config, hooks)
- **explorer.cpp** - Coverage tracking, edge logging, stall detection, loop detection
- **explorer_data.h** - Data access tracking header (32-bit read/write support)
- **explorer_data.cpp** - Data bucket tracking implementation
- **explorer_nn.h** - Neural network policy header (LibTorch optional)
- **explorer_nn.cpp** - Policy network and action queue implementation
- **explorer_hooks.h** - CPU core integration macros
- **explorer_api.h** - Public API for external control
- **explorer_api.cpp** - API implementation
- **explorer_log.h/.cpp** - Host-side logging to file

### PHASE 2: Headless Mode & Observability ✅ COMPLETE
New files added:
- **explorer_headless.h/.cpp** - Headless mode control (SDL dummy drivers)
- **explorer_input.h/.cpp** - Keyboard/mouse input injection APIs
- **explorer_cpu.h/.cpp** - CPU register access
- **explorer_memory.h/.cpp** - RAM/VRAM read access
- **explorer_trace.h/.cpp** - Instruction trace window (ring buffer)

### DOSBox Integration Points Modified:
1. **dosbox-staging/CMakeLists.txt** - Added OPT_EXPLORER and OPT_EXPLORER_LIBTORCH options
2. **dosbox-staging/src/CMakeLists.txt** - Added explorer subdirectory conditional
3. **dosbox-staging/src/cpu/core_normal.cpp** - Added EXPLORER_PRE/POST_INSTRUCTION hooks
4. **dosbox-staging/src/cpu/cpu.cpp** - Added EXPLORER_NOTE_INTERRUPT hook
5. **dosbox-staging/src/gui/sdl_gui.cpp** - Headless environment setup before SDL init
6. **dosbox-staging/src/dosbox.cpp** - Headless env var handling in InitModules

### Environment Variables:
```bash
EXPLORER_ENABLE=1         # Enable Explorer instrumentation
EXPLORER_HEADLESS=1       # Run without GUI (SDL dummy drivers)
EXPLORER_HEADLESS_AUDIO=1 # Keep audio in headless mode (default: disabled)
EXPLORER_FUZZ=1           # Enable random keyboard input fuzzing
EXPLORER_LOG=path         # Enable logging to file
EXPLORER_LOG_EVERY=N      # Log every N instructions
```

### Build & Test Status:
- ✅ Build succeeds with all new modules
- ✅ Headless mode verified: `SDL: dummy video initialised`
- ✅ DOSBox runs commands and exits cleanly in headless mode

### Current Working Commands:
```bash
# Build
cmake --build "dosbox-staging/build/debug-macos" -j 8

# Test headless mode
EXPLORER_ENABLE=1 EXPLORER_HEADLESS=1 ./dosbox-staging/build/debug-macos/Debug/dosbox -c "ver" -c "exit"
```

## Next Steps:
1. ✅ Test headless mode with a real game
2. ⬜ Integrate trace recording into instruction hooks
3. ✅ Test input injection APIs (keyboard fuzzing working)
4. ⬜ Test memory/register/VRAM access
5. ⬜ Create convenience Python/shell wrappers

## Key Architecture Decisions:
- Explorer is a **separate module** that hooks into DOSBox, not a replacement CPU
- Uses compile-time EXPLORER_ENABLED flag for zero overhead when disabled
- Headless mode uses SDL dummy drivers via environment variables
- Environment variables checked early (before SDL init) for headless detection
- All observability APIs are header-only stubs when EXPLORER_ENABLED not defined
