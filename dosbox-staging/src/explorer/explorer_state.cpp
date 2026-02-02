// SPDX-License-Identifier: MIT
// Explorer state save/restore implementation

#include "explorer_state.h"

#ifdef EXPLORER_ENABLED

#include "explorer.h"
#include "explorer_cpu.h"
#include "explorer_memory.h"
#include "explorer_log.h"

#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "hardware/memory.h"

#include <chrono>
#include <cstring>
#include <algorithm>

namespace Explorer {

// =============================================================================
// Internal State Storage
// =============================================================================

struct SavedCPUState {
    // General purpose registers
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint32_t eflags;
    
    // Segment registers
    uint16_t cs, ds, es, fs, gs, ss;
    
    // Segment descriptor cache (for protected mode)
    uint32_t cs_base, ds_base, es_base, fs_base, gs_base, ss_base;
    uint32_t cs_limit, ds_limit, es_limit, fs_limit, gs_limit, ss_limit;
    
    // Control registers
    uint32_t cr0, cr2, cr3;
    
    // CPU mode flags
    bool protected_mode;
    bool paging_enabled;
    uint8_t cpl;
};

struct SavedState {
    bool occupied = false;
    StateFlags flags = StateFlags::None;
    
    // Metadata
    uint64_t instruction_count = 0;
    uint32_t pc_physical = 0;
    uint64_t timestamp = 0;
    std::string program_name;
    
    // CPU state
    SavedCPUState cpu;
    
    // Memory state
    std::vector<uint8_t> memory;
    size_t memory_size = 0;
    
    // Optional VRAM
    std::vector<uint8_t> vram;
    size_t vram_size = 0;
    
    // Delta compression (if used)
    bool is_delta = false;
    int delta_base_slot = -1;
    std::vector<uint32_t> delta_offsets;  // Changed regions
    std::vector<uint8_t> delta_data;       // Changed data
};

// State slots
static SavedState g_states[MAX_STATE_SLOTS];

// Auto-restore settings
static bool g_auto_restore_enabled = false;
static int g_auto_restore_slot = 0;
static std::vector<uint8_t> g_auto_restore_exit_codes = {0};  // Default: only normal exit

// Statistics
static StateStats g_stats = {};

// Current program name (set by program load hook)
static std::string g_current_program;

// =============================================================================
// Helper Functions
// =============================================================================

static uint64_t GetCurrentTimestamp() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

static void SaveCPUState(SavedCPUState& cpu) {
    // Read all registers via DOSBox globals
    cpu.eax = reg_eax;
    cpu.ebx = reg_ebx;
    cpu.ecx = reg_ecx;
    cpu.edx = reg_edx;
    cpu.esi = reg_esi;
    cpu.edi = reg_edi;
    cpu.ebp = reg_ebp;
    cpu.esp = reg_esp;
    cpu.eip = reg_eip;
    
    // Flags
    cpu.eflags = reg_flags;
    
    // Segment selectors
    cpu.cs = SegValue(cs);
    cpu.ds = SegValue(ds);
    cpu.es = SegValue(es);
    cpu.fs = SegValue(fs);
    cpu.gs = SegValue(gs);
    cpu.ss = SegValue(ss);
    
    // Segment bases
    cpu.cs_base = SegPhys(cs);
    cpu.ds_base = SegPhys(ds);
    cpu.es_base = SegPhys(es);
    cpu.fs_base = SegPhys(fs);
    cpu.gs_base = SegPhys(gs);
    cpu.ss_base = SegPhys(ss);
    
    // Control registers (from global cpu block, not cpu_regs)
    cpu.cr0 = ::cpu.cr0;  // Use global cpu block
    cpu.cr2 = paging.cr2;
    cpu.cr3 = paging.cr3;
    
    // Mode flags
    cpu.protected_mode = ::cpu.cr0 & 1;
    cpu.paging_enabled = (::cpu.cr0 & 0x80000000) != 0;
    cpu.cpl = ::cpu.cpl;
}

static void RestoreCPUState(const SavedCPUState& cpu) {
    // Restore general purpose registers
    reg_eax = cpu.eax;
    reg_ebx = cpu.ebx;
    reg_ecx = cpu.ecx;
    reg_edx = cpu.edx;
    reg_esi = cpu.esi;
    reg_edi = cpu.edi;
    reg_ebp = cpu.ebp;
    reg_esp = cpu.esp;
    reg_eip = cpu.eip;
    
    // Restore flags
    reg_flags = cpu.eflags;
    
    // Restore segment registers
    // Note: In protected mode, this needs proper descriptor loading
    // For now, we do direct restoration which works for real mode
    // and simple protected mode cases
    SegSet16(cs, cpu.cs);
    SegSet16(ds, cpu.ds);
    SegSet16(es, cpu.es);
    SegSet16(fs, cpu.fs);
    SegSet16(gs, cpu.gs);
    SegSet16(ss, cpu.ss);
    
    // Control registers would need more careful handling
    // For now, assume mode hasn't changed
}

// =============================================================================
// Public API Implementation
// =============================================================================

bool SaveState(int slot, StateFlags flags) {
    if (slot < 0 || slot >= MAX_STATE_SLOTS) {
        Log_Event("state_error", "Invalid slot number");
        return false;
    }
    
    SavedState& state = g_states[slot];
    
    // Get memory info
    uint8_t* mem_base = GetMemoryBase();
    size_t mem_size = GetMemorySize();
    
    if (!mem_base || mem_size == 0) {
        Log_Event("state_error", "Memory not available");
        return false;
    }
    
    // Save metadata
    state.occupied = true;
    state.flags = flags;
    state.instruction_count = GetInstrumenter().GetInstructionCount();
    state.pc_physical = GetInstrumenter().GetCurrentPC();
    state.timestamp = GetCurrentTimestamp();
    state.program_name = g_current_program;
    
    // Save CPU state
    SaveCPUState(state.cpu);
    
    // Save memory (allocate and copy)
    state.memory.resize(mem_size);
    std::memcpy(state.memory.data(), mem_base, mem_size);
    state.memory_size = mem_size;
    
    // Optionally save VRAM
    if (HasFlag(flags, StateFlags::IncludeVRAM)) {
        VRAMInfo vram_info = GetVRAMInfo();
        if (vram_info.linear && vram_info.size > 0) {
            state.vram.resize(vram_info.size);
            std::memcpy(state.vram.data(), vram_info.linear, vram_info.size);
            state.vram_size = vram_info.size;
        }
    }
    
    // Update stats
    g_stats.saves_count++;
    g_stats.total_bytes_saved += state.memory_size + state.vram_size;
    
    // Log the save
    char buf[256];
    snprintf(buf, sizeof(buf), 
             "slot=%d pc=0x%x memory=%zuKB vram=%zuKB",
             slot, state.pc_physical, 
             state.memory_size / 1024,
             state.vram_size / 1024);
    Log_Event("state_save", buf);
    
    return true;
}

bool LoadState(int slot) {
    if (slot < 0 || slot >= MAX_STATE_SLOTS) {
        Log_Event("state_error", "Invalid slot number");
        return false;
    }
    
    SavedState& state = g_states[slot];
    
    if (!state.occupied) {
        Log_Event("state_error", "Slot is empty");
        return false;
    }
    
    // Get current memory info
    uint8_t* mem_base = GetMemoryBase();
    size_t mem_size = GetMemorySize();
    
    if (!mem_base || mem_size == 0) {
        Log_Event("state_error", "Memory not available");
        return false;
    }
    
    // Verify memory size matches
    if (state.memory_size != mem_size) {
        Log_Event("state_error", "Memory size mismatch");
        return false;
    }
    
    // Restore CPU state first
    RestoreCPUState(state.cpu);
    
    // Restore memory
    std::memcpy(mem_base, state.memory.data(), state.memory_size);
    
    // Restore VRAM if saved
    if (!state.vram.empty()) {
        VRAMInfo vram_info = GetVRAMInfo();
        if (vram_info.linear && state.vram_size <= vram_info.size) {
            std::memcpy(vram_info.linear, state.vram.data(), state.vram_size);
        }
    }
    
    // Update stats
    g_stats.loads_count++;
    g_stats.total_bytes_loaded += state.memory_size + state.vram_size;
    
    // Log the load
    char buf[256];
    snprintf(buf, sizeof(buf), 
             "slot=%d restored_pc=0x%x restored_inst=%lu",
             slot, state.pc_physical,
             static_cast<unsigned long>(state.instruction_count));
    Log_Event("state_load", buf);
    
    return true;
}

bool HasState(int slot) {
    if (slot < 0 || slot >= MAX_STATE_SLOTS) {
        return false;
    }
    return g_states[slot].occupied;
}

void ClearState(int slot) {
    if (slot >= 0 && slot < MAX_STATE_SLOTS) {
        g_states[slot] = SavedState();
    }
}

void ClearAllStates() {
    for (int i = 0; i < MAX_STATE_SLOTS; i++) {
        g_states[i] = SavedState();
    }
}

StateInfo GetStateInfo(int slot) {
    StateInfo info = {};
    info.slot = slot;
    
    if (slot >= 0 && slot < MAX_STATE_SLOTS && g_states[slot].occupied) {
        const SavedState& state = g_states[slot];
        info.occupied = true;
        info.instruction_count = state.instruction_count;
        info.pc_physical = state.pc_physical;
        info.timestamp = state.timestamp;
        info.memory_size = state.memory_size;
        info.total_size = state.memory_size + state.vram_size + sizeof(SavedCPUState);
        info.program_name = state.program_name;
        info.flags = state.flags;
    }
    
    return info;
}

std::vector<int> GetOccupiedSlots() {
    std::vector<int> slots;
    for (int i = 0; i < MAX_STATE_SLOTS; i++) {
        if (g_states[i].occupied) {
            slots.push_back(i);
        }
    }
    return slots;
}

// =============================================================================
// Auto-Restore Implementation
// =============================================================================

void SetAutoRestoreEnabled(bool enabled) {
    g_auto_restore_enabled = enabled;
    Log_Event("state_config", enabled ? "auto_restore=enabled" : "auto_restore=disabled");
}

bool IsAutoRestoreEnabled() {
    return g_auto_restore_enabled;
}

void SetAutoRestoreExitCodes(const std::vector<uint8_t>& codes) {
    g_auto_restore_exit_codes = codes;
}

void SetAutoRestoreSlot(int slot) {
    if (slot >= 0 && slot < MAX_STATE_SLOTS) {
        g_auto_restore_slot = slot;
    }
}

int GetAutoRestoreSlot() {
    return g_auto_restore_slot;
}

// Check if a program is a DOSBox internal command (skip for auto-restore)
static bool IsInternalProgramForRestore(const std::string& name) {
    if (name.empty()) return true;
    // DOSBox internal commands are on Z: drive
    if (name.length() > 2 && (name[0] == 'Z' || name[0] == 'z') && name[1] == ':') {
        return true;
    }
    if (name.length() > 3 && name[0] == 'Z' && name[1] == '\\') {
        return true;
    }
    return false;
}

bool OnProgramExit(uint8_t exit_code, bool is_tsr) {
    // Don't restore for TSR programs
    if (is_tsr) {
        return false;
    }
    
    // Skip internal DOSBox programs
    if (IsInternalProgramForRestore(g_current_program)) {
        return false;
    }
    
    // Check if auto-restore is enabled
    if (!g_auto_restore_enabled) {
        return false;
    }
    
    // Check if we have a saved state
    if (!HasState(g_auto_restore_slot)) {
        return false;
    }
    
    // Check if this exit code triggers restore
    bool should_restore = std::find(
        g_auto_restore_exit_codes.begin(),
        g_auto_restore_exit_codes.end(),
        exit_code
    ) != g_auto_restore_exit_codes.end();
    
    if (!should_restore) {
        return false;
    }
    
    // Log the auto-restore
    char buf[128];
    snprintf(buf, sizeof(buf), 
             "exit_code=%u slot=%d program=\"%s\"", 
             exit_code, g_auto_restore_slot, g_current_program.c_str());
    Log_Event("state_auto_restore", buf);
    
    // Perform the restore
    if (LoadState(g_auto_restore_slot)) {
        g_stats.auto_restores++;
        return true;
    }
    
    return false;
}

StateStats GetStateStats() {
    return g_stats;
}

// =============================================================================
// Delta Compression (placeholder - can be optimized later)
// =============================================================================

bool SaveStateDelta(int slot, int base_slot) {
    // For now, just do a full save
    // Future optimization: only save changed memory pages
    return SaveState(slot, StateFlags::None);
}

bool IsDeltaState(int slot) {
    if (slot >= 0 && slot < MAX_STATE_SLOTS) {
        return g_states[slot].is_delta;
    }
    return false;
}

int GetDeltaBaseSlot(int slot) {
    if (slot >= 0 && slot < MAX_STATE_SLOTS && g_states[slot].is_delta) {
        return g_states[slot].delta_base_slot;
    }
    return -1;
}

// =============================================================================
// Internal: Update current program name
// =============================================================================

void SetCurrentProgramName(const std::string& name) {
    g_current_program = name;
}

std::string GetCurrentProgramName() {
    return g_current_program;
}

// =============================================================================
// Environment Variable Initialization
// =============================================================================

static bool env_is_truthy(const char* var)
{
    const char* val = std::getenv(var);
    if (!val || !val[0]) {
        return false;
    }
    if (val[0] == '0' && val[1] == '\0') {
        return false;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "FALSE") == 0) {
        return false;
    }
    return true;
}

void State_InitFromEnv() {
    // Check for auto-restore enable
    if (env_is_truthy("EXPLORER_AUTO_RESTORE")) {
        SetAutoRestoreEnabled(true);
        Log_Event("state_init", "auto_restore enabled via EXPLORER_AUTO_RESTORE");
    }
    
    // Check for restore slot
    const char* slot_str = std::getenv("EXPLORER_RESTORE_SLOT");
    if (slot_str && slot_str[0]) {
        int slot = atoi(slot_str);
        if (slot >= 0 && slot < MAX_STATE_SLOTS) {
            SetAutoRestoreSlot(slot);
            char buf[64];
            snprintf(buf, sizeof(buf), "restore_slot=%d", slot);
            Log_Event("state_init", buf);
        }
    }
    
    // Check for exit codes that trigger restore
    const char* codes_str = std::getenv("EXPLORER_RESTORE_CODES");
    if (codes_str && codes_str[0]) {
        std::vector<uint8_t> codes;
        // Parse comma-separated list: "0,1,255"
        const char* p = codes_str;
        while (*p) {
            int code = atoi(p);
            if (code >= 0 && code <= 255) {
                codes.push_back(static_cast<uint8_t>(code));
            }
            // Find next comma or end
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        if (!codes.empty()) {
            SetAutoRestoreExitCodes(codes);
        }
    }
    
    // Check for auto-save on program load
    if (env_is_truthy("EXPLORER_AUTO_SAVE")) {
        SetAutoSaveOnLoad(true);
        Log_Event("state_init", "auto_save on program load enabled");
    }
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
