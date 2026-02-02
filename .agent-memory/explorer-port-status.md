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

### Program Lifecycle Logging ✅ COMPLETE
Added DOS program load/exit detection to help diagnose game compatibility.

### State Save/Restore System ✅ NEW
Full savestate system for agent exploration:

**New Files:**
- `src/explorer/explorer_state.h` - State save/restore API
- `src/explorer/explorer_state.cpp` - Implementation (16 slots, ~16MB per state)

**Features:**
1. **SaveState(slot, flags)** - Save CPU registers, memory, optional VRAM
2. **LoadState(slot)** - Restore full emulator state
3. **Auto-Save on Program Load** - Saves state when game starts
4. **Auto-Restore on Exit** - When game exits (ESC, menu), restores to continue exploring
5. **Internal Program Filtering** - Skips DOSBox internal commands (Z: drive programs)

**Environment Variables:**
```bash
EXPLORER_AUTO_RESTORE=1     # Enable auto-restore when program exits
EXPLORER_AUTO_SAVE=1        # Auto-save when program loads
EXPLORER_RESTORE_SLOT=N     # Slot to use (0-15, default: 0)
EXPLORER_RESTORE_CODES=0,1  # Exit codes that trigger restore (default: 0)
```

**How It Works:**
1. Game loads (e.g., `carma.EXE`) → Auto-saves state to slot 0
2. Agent explores game, tries actions
3. User presses ESC or game exits normally (exit code 0)
4. System detects exit → Restores state from slot 0
5. Agent continues exploring from saved point

**Log Output Example:**
```
[program_load] inst=22 name="carma.EXE" result=success
[state_save] inst=22 slot=0 pc=0xf13e1 memory=16384KB vram=0KB
[auto_save] inst=22 Auto-saved state to slot 0 for program "carma.EXE"
...
[state_auto_restore] exit_code=0 slot=0 program="carma.EXE"
[state_load] slot=0 restored_pc=0xf13e1 restored_inst=22
[auto_restore] Program state restored, continuing execution
```

### Game Testing Results

| Game | Type | Status | Notes |
|------|------|--------|-------|
| Castle Wolfenstein | Real Mode | ✅ Works | ~1500 coverage |
| Carmageddon Splat Pack | 386 PM (DOS/4GW) | ✅ Works | 3048 coverage, savestate works |
| Duke Nukem 3D | 386 PM (DOS/4GW) | ❌ Missing Files | Exit code 40, needs GRP file |
| Heretic | 386 PM (DOS/4GW) | ❌ Missing Files | Missing WAD file |

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
- explorer_state.h/.cpp - **NEW** State save/restore system
- explorer_trace.h/.cpp - Instruction trace window

## DOSBox Integration Points:
1. **CMakeLists.txt** - OPT_EXPLORER and OPT_EXPLORER_LIBTORCH
2. **src/cpu/core_normal.cpp** - EXPLORER_PRE/POST_INSTRUCTION
3. **src/cpu/cpu.cpp** - EXPLORER_NOTE_INTERRUPT
4. **src/cpu/paging.h** - EXPLORER_NOTE_READ/WRITE (data tracking)
5. **src/dos/dos_execute.cpp** - EXPLORER_NOTE_PROGRAM_LOAD/EXIT
6. **src/gui/sdl_gui.cpp** - Headless environment setup
7. **src/dosbox.cpp** - Headless env var handling

## All Environment Variables:
```bash
# Core instrumentation
EXPLORER_ENABLE=1           # Enable Explorer
EXPLORER_HEADLESS=1         # Run without GUI
EXPLORER_HEADLESS_AUDIO=1   # Keep audio in headless
EXPLORER_FUZZ=1             # Enable input fuzzing

# Logging
EXPLORER_LOG=path           # Log file path
EXPLORER_LOG_EVERY=N        # Log every N instructions

# State save/restore (NEW)
EXPLORER_AUTO_RESTORE=1     # Auto-restore on exit
EXPLORER_AUTO_SAVE=1        # Auto-save on load
EXPLORER_RESTORE_SLOT=N     # Slot to use (0-15)
EXPLORER_RESTORE_CODES=0,1  # Exit codes triggering restore
```

## Build Commands:
```bash
cmake --build --preset debug-macos
```

## Test Commands:
```bash
# Test with auto-save/restore
EXPLORER_ENABLE=1 EXPLORER_HEADLESS=1 EXPLORER_FUZZ=1 \
EXPLORER_LOG=/tmp/test.log EXPLORER_AUTO_RESTORE=1 EXPLORER_AUTO_SAVE=1 \
  timeout 20 ./build/debug-macos/Debug/dosbox \
  -c "mount c /path/to/game" -c "c:" -c "game.exe"
```

## Next Steps:
- [ ] Test auto-restore with a game that has a working quit option
- [ ] Profile instrumentation overhead
- [ ] Phase 4: Policy/RL Integration (LibTorch PolicyAgent)

## Key Architecture Decisions:
- Explorer is a **separate module** that hooks into DOSBox
- Uses compile-time EXPLORER_ENABLED flag for zero overhead when disabled
- Savestate stores full memory (~16MB) + CPU state for accurate restoration
- Internal DOSBox programs (Z: drive) are filtered from auto-save/restore
- Headless mode uses SDL dummy drivers via environment variables
- Environment variables checked early (before SDL init) for headless detection
- All observability APIs are header-only stubs when EXPLORER_ENABLED not defined
- Data tracking integrated at paging.h level for complete memory coverage
