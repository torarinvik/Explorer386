# Explorer DOSBox Port - Implementation Status

## Project Overview
Porting Explorer (8086tiny fork) instrumentation capabilities into DOSBox-Staging for 386+ CPU support.

## Implementation Status: PHASE 5 IN PROGRESS 🚧

### PHASE 1: Base Instrumentation ✅ COMPLETE
### PHASE 2: Headless Mode & Observability ✅ COMPLETE  
### PHASE 3: Data Access Tracking ✅ COMPLETE
### PHASE 4: Policy/RL Integration 📋 PENDING
### PHASE 5: Testing & Optimization 🚧 IN PROGRESS

## Phase 5 Progress

### Program Lifecycle Logging ✅ NEW
Added DOS program load/exit detection to help diagnose game compatibility:
- `EXPLORER_NOTE_PROGRAM_LOAD(name, success)` - Logs when DOS loads a program
- `EXPLORER_NOTE_PROGRAM_EXIT(exit_code, is_tsr)` - Logs when program terminates
- Automatic warning for quick exits with non-zero exit codes (likely missing files)

Files modified:
- `src/explorer/explorer_hooks.h` - Added macros
- `src/explorer/explorer.h` - Added function declarations
- `src/explorer/explorer.cpp` - Added implementations
- `src/explorer/explorer_log.h/.cpp` - Added Log_Event function
- `src/dos/dos_execute.cpp` - Added hooks in DOS_Execute and DOS_Terminate

### Game Testing Results

| Game | Type | Status | Notes |
|------|------|--------|-------|
| Castle Wolfenstein | Real Mode | ✅ Works | ~1500 coverage, millions of data accesses |
| Carmageddon Splat Pack | 386 PM (DOS/4GW) | ✅ Works | 3048 coverage, 68M data accesses |
| Duke Nukem 3D | 386 PM (DOS/4GW) | ❌ Missing Files | Exit code 40, needs GRP file |
| Heretic | 386 PM (DOS/4GW) | ❌ Missing Files | Missing WAD file |

### Carmageddon Test Metrics (15s run):
```
Coverage: 3,048 unique PCs
Instructions: 1,000,000+ 
Data accesses: 67,978,339
Data patterns: 11,605 unique buckets
DOS/4GW extender: Loaded successfully
```

### Sample Log Output (Duke3D):
```
[program_load] inst=22 name="duke3d.EXE" result=success
[program_exit] inst=53040 name="duke3d.EXE" exit_code=40 is_tsr=false instructions_executed=53018
[warning] POSSIBLE ISSUE: Program "duke3d.EXE" exited quickly (after 53018 instructions) with code 40 - may be missing data files
```

## All Files in dosbox-staging/src/explorer/:
- CMakeLists.txt
- explorer.h/.cpp - Main instrumentation, coverage, program lifecycle
- explorer_api.h/.cpp - Public API
- explorer_cpu.h/.cpp - CPU register access
- explorer_data.h/.cpp - Data access tracking
- explorer_headless.h/.cpp - Headless mode control
- explorer_hooks.h - CPU/DOS integration macros
- explorer_input.h/.cpp - Input injection
- explorer_log.h/.cpp - Host-side logging, event logging
- explorer_memory.h/.cpp - RAM/VRAM access
- explorer_nn.h/.cpp - Neural network policy (LibTorch)
- explorer_trace.h/.cpp - Instruction trace window

## DOSBox Integration Points:
1. **CMakeLists.txt** - OPT_EXPLORER and OPT_EXPLORER_LIBTORCH
2. **src/cpu/core_normal.cpp** - EXPLORER_PRE/POST_INSTRUCTION
3. **src/cpu/cpu.cpp** - EXPLORER_NOTE_INTERRUPT
4. **src/cpu/paging.h** - EXPLORER_NOTE_READ/WRITE (data tracking)
5. **src/dos/dos_execute.cpp** - EXPLORER_NOTE_PROGRAM_LOAD/EXIT ⬅️ NEW
6. **src/gui/sdl_gui.cpp** - Headless environment setup
7. **src/dosbox.cpp** - Headless env var handling

## Environment Variables:
```bash
EXPLORER_ENABLE=1           # Enable Explorer instrumentation
EXPLORER_HEADLESS=1         # Run without GUI (SDL dummy drivers)
EXPLORER_HEADLESS_AUDIO=1   # Keep audio in headless mode
EXPLORER_FUZZ=1             # Enable random keyboard input fuzzing
EXPLORER_LOG=path           # Enable logging to file
EXPLORER_LOG_EVERY=N        # Log every N instructions
```

## Build Commands:
```bash
cmake --build --preset debug-macos
```

## Test Commands:
```bash
# Test with program lifecycle logging
EXPLORER_ENABLE=1 EXPLORER_HEADLESS=1 EXPLORER_FUZZ=1 EXPLORER_LOG=/tmp/test.log \
  timeout 15 ./build/debug-macos/Debug/dosbox \
  -c "mount c /path/to/game" -c "c:" -c "game"

# Check log for issues
cat /tmp/test.log | grep -E "program_|warning"
```

## Next Steps (Phase 5):
- [ ] Profile instrumentation overhead (with/without Explorer)
- [ ] Test more 386 protected mode games with complete files
- [ ] Consider dynamic core support (core_dynrec hooks)

## Key Architecture Decisions:
- Explorer is a **separate module** that hooks into DOSBox, not a replacement CPU
- Uses compile-time EXPLORER_ENABLED flag for zero overhead when disabled
- Program lifecycle logging helps distinguish emulator issues from game issues
- Headless mode uses SDL dummy drivers via environment variables
- Environment variables checked early (before SDL init) for headless detection
- All observability APIs are header-only stubs when EXPLORER_ENABLED not defined
- Data tracking integrated at paging.h level for complete memory coverage
