# Explorer → DOSBox-Staging Porting Analysis

## Executive Summary

**Goal:** Port Explorer's automated exploration/RL instrumentation into DOSBox-Staging to gain 386+ instruction support (protected mode, paging, 32-bit ops).

**Key insight:** Explorer is an 8086/186-only emulator (8086tiny fork). DOSBox-Staging already has full 386/486/Pentium support. The port is about **transplanting Explorer's instrumentation layer**, not its CPU core.

---

## 1. Explorer Architecture (explorer/src/explorer.cpp)

### 1.1 Core Components

| Component | Location | Description |
|-----------|----------|-------------|
| **Memory** | `mem[RAM_SIZE]` (1.1MB) | Flat physical memory array |
| **Registers** | `regs16[14]`, `regs8[]`, `reg_ip` | 16-bit segment/GP registers mapped into memory at `REGS_BASE` (0xF0000) |
| **I/O Ports** | `io_ports[IO_PORT_COUNT]` | 64KB I/O port space |
| **Decode Tables** | `bios_table_lookup[20][256]` | BIOS-resident opcode translation tables |
| **Flags** | `regs8[FLAG_CF..FLAG_OF]` | Individual flag bytes (not packed EFLAGS) |

### 1.2 Instruction Decoder/Executor

```
Location: explorer.cpp lines ~6700-7400
Structure: Giant switch(xlat_opcode_id) with 48 cases
Opcodes:  8086 base + some 80186 (PUSH imm, ENTER, LEAVE, IMUL imm)
Missing:  All 386+ (32-bit ops, new segs, CR/DR, protected mode, paging)
```

### 1.3 Explorer Instrumentation (EXPLORER mode)

| Feature | Hook Point | Purpose |
|---------|------------|---------|
| **Coverage bitmap** | Pre-instruction | `exp_cov_note_exec(phys)` - edge/phys coverage |
| **Instruction recording** | Post-decode | `exp_record_instruction()` - per-PC stats, variants |
| **Write tracking** | Code page comparison | Detect self-modifying code |
| **PC trace ring** | Pre-instruction | `exp_trace_note(phys)` - rolling window |
| **Edge logging** | Post-instruction | `exp_edge_log(src, dst)` - control flow graph |
| **Interrupt tracing** | `pc_interrupt()` | `exp_evt_note(EXP_EVT_INT, ...)` |
| **I/O tracing** | IN/OUT opcodes | `exp_evt_note(EXP_EVT_IN/OUT, ...)` |
| **Data access tracking** | Memory ops | `exp_data_note_read/write()` (LibTorch mode) |
| **Stall detection** | Per-instruction | Coverage staleness, PC-repeat loops |
| **Policy invocation** | Timer tick | `exp_nn_policy_step()` for RL agent |

### 1.4 DOS/BIOS Intercepts

Explorer intercepts INT 10h/16h/21h/33h etc. headlessly (no real BIOS). These need adjustment since DOSBox has its own DOS layer.

---

## 2. DOSBox-Staging CPU Architecture

### 2.1 Core Files

| File | Purpose |
|------|---------|
| `src/cpu/cpu.cpp` | CPU state, mode switching, exceptions, descriptors |
| `src/cpu/cpu.h` | Registers (`cpu_regs`, `Segs`), CPU_Decoder interface |
| `src/cpu/registers.h` | Register accessors (`reg_eax`, `reg_ax`, `reg_al`, etc.) |
| `src/cpu/core_normal.cpp` | Interpreted CPU core (fetch-decode-execute loop) |
| `src/cpu/core_normal/*.h` | Opcode handlers by prefix (none, 0F, 66, 66_0F) |
| `src/cpu/instructions.h` | ALU operation macros (ADDB, ADDW, ADDD, etc.) |
| `src/cpu/paging.cpp` | 386 paging support |
| `src/cpu/flags.cpp` | EFLAGS management |
| `src/cpu/modrm.cpp` | ModR/M decoding |

### 2.2 Execution Loop (core_normal.cpp)

```cpp
Bits CPU_Core_Normal_Run() noexcept {
    while (CPU_Cycles-- > 0) {
        LOADIP;
        // ... setup prefixes, segments ...
restart_opcode:
        switch (core.opcode_index + Fetchb()) {
            #include "core_normal/prefix_none.h"   // 8086/186/286/386 opcodes
            #include "core_normal/prefix_0f.h"     // 0F-prefixed (386+)
            #include "core_normal/prefix_66.h"    // Operand size override
            #include "core_normal/prefix_66_0f.h" // 66 0F opcodes
            // ...
        }
        SAVEIP;
    }
    return CBRET_NONE;
}
```

### 2.3 Memory Model

```cpp
// Physical address calculation with paging support
PhysPt addr = SegPhys(seg) + offset;  // Linear address
if (cpu.cr0 & CR0_PAGING)
    addr = PAGING_GetPhysicalAddress(addr);  // Page translation

// Read/write via:
mem_readb/w/d(addr)
mem_writeb/w/d(addr, val)
```

### 2.4 Protected Mode / Paging

DOSBox fully supports:
- GDT/LDT/IDT descriptor tables
- Ring 0-3 privilege levels  
- Task switching (TSS)
- 386 paging (CR3, page tables)
- V86 mode

---

## 3. Porting Strategy

### 3.1 Approach: Instrumentation Layer, Not CPU Replacement

**DO NOT** port Explorer's 8086 decoder. Instead:
1. Keep DOSBox's full CPU implementation
2. Add Explorer's **instrumentation hooks** into DOSBox's core loop
3. Port Explorer's **policy/RL infrastructure** as a DOSBox module

### 3.2 Hook Points in DOSBox

| Explorer Feature | DOSBox Hook Location |
|-----------------|---------------------|
| Pre-instruction coverage | `CPU_Core_Normal_Run()` at loop start, after `LOADIP` |
| Post-instruction edge | After `SAVEIP` |
| Memory read tracking | `mem_readb/w/d_inline()` in paging.h |
| Memory write tracking | `mem_writeb/w/d_inline()` in paging.h |
| Interrupt tracing | `CPU_Interrupt()` in cpu.cpp |
| I/O tracing | `IO_ReadB/W/D()`, `IO_WriteB/W/D()` in hardware/port.cpp |
| Timer/policy tick | PIC timer callback or cycle-based |

### 3.3 New Files to Create

```
src/explorer/
├── explorer.h           # Main instrumentation API
├── explorer.cpp         # Coverage, edge, trace logic
├── explorer_data.h      # Data access tracking (from Explorer)
├── explorer_data.cpp
├── explorer_policy.h    # RL policy interface
├── explorer_policy.cpp
├── explorer_nn.h        # LibTorch agent (from Explorer)
├── explorer_nn.cpp
└── CMakeLists.txt
```

### 3.4 Integration Points

#### 3.4.1 Core Loop Hooks (core_normal.cpp)

```cpp
// At top of loop, after LOADIP:
#ifdef EXPLORER_ENABLED
    PhysPt phys_pc = SegPhys(cs) + reg_eip;
    if (cpu.cr0 & CR0_PAGING)
        phys_pc = PAGING_GetPhysicalPage(phys_pc);
    Explorer_PreInstruction(phys_pc);
#endif

// After SAVEIP:
#ifdef EXPLORER_ENABLED
    PhysPt next_phys = SegPhys(cs) + reg_eip;
    if (cpu.cr0 & CR0_PAGING)
        next_phys = PAGING_GetPhysicalPage(next_phys);
    Explorer_PostInstruction(phys_pc, next_phys);
#endif
```

#### 3.4.2 Memory Hooks (paging.h inline functions)

```cpp
static inline uint8_t mem_readb_inline(PhysPt addr) {
    // ... existing read logic ...
#ifdef EXPLORER_ENABLED
    Explorer_NoteRead(addr, 1, GetCurrentPC());
#endif
    return result;
}
```

#### 3.4.3 Interrupt Hook (cpu.cpp)

```cpp
void CPU_Interrupt(Bitu num, Bitu type, Bitu oldeip) {
#ifdef EXPLORER_ENABLED
    Explorer_NoteInterrupt(num, type, reg_eax);
#endif
    // ... existing logic ...
}
```

### 3.5 Build System Integration

Add to `src/CMakeLists.txt`:
```cmake
option(ENABLE_EXPLORER "Enable Explorer instrumentation" OFF)
if(ENABLE_EXPLORER)
    add_subdirectory(explorer)
    target_compile_definitions(dosbox-staging PRIVATE EXPLORER_ENABLED)
    target_link_libraries(dosbox-staging PRIVATE explorer)
endif()
```

### 3.6 Configuration

Add Explorer config section (similar to [cpu]):
```ini
[explorer]
enabled = true
coverage_mode = edge       # phys | edge
coverage_size = 65536
trace_length = 4096
policy_mode = nn           # none | json | nn
nn_model_path = model.pt
tick_interval = 20000
```

---

## 4. Migration Milestones

### Phase 1: Basic Instrumentation (1-2 weeks)
- [ ] Create `src/explorer/` skeleton
- [ ] Port coverage bitmap (`exp_global_cov`, `exp_run_cov`)
- [ ] Add pre/post instruction hooks to core_normal.cpp
- [ ] Verify coverage collection on simple DOS program

### Phase 2: Tracing & Analysis (1 week)
- [ ] Port PC trace ring buffer
- [ ] Port edge logging
- [ ] Port interrupt/IO event tracing
- [ ] Port instruction variant recording

### Phase 3: Data Access Tracking (1 week)
- [ ] Port `DataCollector` class
- [ ] Hook memory read/write functions
- [ ] Verify data boundary detection

### Phase 4: Policy/RL Integration (2 weeks)
- [ ] Port keyboard/mouse injection interface
- [ ] Adapt to DOSBox's input system (BIOS module)
- [ ] Port LibTorch `PolicyAgent`
- [ ] Add config file parsing

### Phase 5: Testing & Optimization (ongoing)
- [ ] Test with 386 protected mode games
- [ ] Profile instrumentation overhead
- [ ] Add dynamic core support (optional)

---

## 5. Key Differences to Handle

| Aspect | Explorer | DOSBox | Resolution |
|--------|----------|--------|------------|
| Address size | 20-bit (real mode) | 32-bit (pmode/paging) | Use PhysPt throughout |
| Registers | `regs16[]` array | `cpu_regs` struct | Adapt accessor macros |
| Flags | Individual bytes | Packed EFLAGS | Use DOSBox's FillFlags() |
| Memory | Direct `mem[]` array | Paging abstraction | Use mem_read/write |
| DOS intercepts | Built-in handlers | Separate DOS module | Remove, use DOSBox's DOS |
| Cycles | `inst_counter` | `CPU_Cycles` | Sync counters |

---

## 6. Files to Copy/Adapt from Explorer

| Source File | Target | Changes Needed |
|-------------|--------|----------------|
| `explorer_data.h` | `src/explorer/explorer_data.h` | Minimal (standalone) |
| `explorer_nn.h` | `src/explorer/explorer_nn.h` | Update observation building |
| Coverage logic | `src/explorer/explorer.cpp` | Extract from main file |
| Corpus/mutation | `src/explorer/explorer_corpus.cpp` | Extract, adapt |

---

## 7. Immediate Next Steps

1. **Create branch**: `feature/explorer-instrumentation`
2. **Scaffold files**: Empty `src/explorer/` with CMake integration
3. **Minimal hook**: Add a single counter in core_normal.cpp, verify it compiles
4. **Port coverage**: Copy bitmap logic, wire up pre-instruction hook
5. **First test**: Run `DEBUG.COM` with coverage, dump results

---

## Appendix: Explorer Command Line Reference

Useful for understanding feature scope:
```
--program PATH     DOS executable to run
--bios PATH        BIOS image (8086tiny format)
--maxinst N        Instruction budget per run
--maxtime N        Wall-clock timeout (seconds)
--runs N           Number of exploration runs
--cov phys|edge    Coverage mode
--covsz N          Coverage bitmap size (power of 2)
--trace N          Trace ring buffer length
--nn-policy        Enable LibTorch RL agent
--nn-load PATH     Load trained model
--nn-save PATH     Save model checkpoints
--datalog          Enable data access tracking
```
