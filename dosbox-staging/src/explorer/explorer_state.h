// SPDX-License-Identifier: MIT
// Explorer state save/restore API
// Allows saving and restoring emulator state for exploration

#ifndef EXPLORER_STATE_H
#define EXPLORER_STATE_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#ifdef EXPLORER_ENABLED

namespace Explorer {

// =============================================================================
// State Slot Management
// =============================================================================

// Maximum number of state slots
constexpr int MAX_STATE_SLOTS = 16;

// State flags
enum class StateFlags : uint32_t {
    None         = 0,
    IncludeVRAM  = 1 << 0,  // Include video RAM (larger, but captures graphics)
    Compressed   = 1 << 1,  // Compress memory (slower but smaller)
    AutoRestore  = 1 << 2,  // Auto-restore on program exit
};

inline StateFlags operator|(StateFlags a, StateFlags b) {
    return static_cast<StateFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline StateFlags operator&(StateFlags a, StateFlags b) {
    return static_cast<StateFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(StateFlags flags, StateFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// =============================================================================
// Saved State Info
// =============================================================================

struct StateInfo {
    int slot;                    // Slot number (0-15)
    bool occupied;               // True if slot has saved state
    uint64_t instruction_count;  // Instruction count when saved
    uint32_t pc_physical;        // Physical PC when saved
    uint64_t timestamp;          // Unix timestamp when saved
    size_t memory_size;          // Size of saved memory
    size_t total_size;           // Total state size in bytes
    std::string program_name;    // Name of running program
    StateFlags flags;            // Flags used when saving
};

// =============================================================================
// State Save/Restore API
// =============================================================================

// Save current emulator state to a slot
// Returns true on success
bool SaveState(int slot, StateFlags flags = StateFlags::None);

// Restore state from a slot
// Returns true on success
bool LoadState(int slot);

// Check if a slot has saved state
bool HasState(int slot);

// Clear a saved state slot
void ClearState(int slot);

// Clear all saved states
void ClearAllStates();

// Get info about a saved state
StateInfo GetStateInfo(int slot);

// Get list of all occupied slots
std::vector<int> GetOccupiedSlots();

// =============================================================================
// Quick Save/Load (slot 0)
// =============================================================================

inline bool QuickSave(StateFlags flags = StateFlags::None) { 
    return SaveState(0, flags); 
}
inline bool QuickLoad() { 
    return LoadState(0); 
}
inline bool HasQuickSave() { 
    return HasState(0); 
}

// =============================================================================
// Auto-Restore on Exit
// =============================================================================

// Enable auto-restore: when program exits with specific codes, restore state
void SetAutoRestoreEnabled(bool enabled);
bool IsAutoRestoreEnabled();

// Set which exit codes trigger auto-restore (default: 0 = normal exit)
void SetAutoRestoreExitCodes(const std::vector<uint8_t>& codes);

// Set the slot to auto-restore from (default: 0)
void SetAutoRestoreSlot(int slot);
int GetAutoRestoreSlot();

// Called internally when program exits - checks if should auto-restore
// Returns true if state was restored
bool OnProgramExit(uint8_t exit_code, bool is_tsr);

// =============================================================================
// State Statistics
// =============================================================================

struct StateStats {
    uint32_t saves_count;        // Number of saves performed
    uint32_t loads_count;        // Number of loads performed
    uint32_t auto_restores;      // Number of auto-restores triggered
    uint64_t total_bytes_saved;  // Total bytes saved across all operations
    uint64_t total_bytes_loaded; // Total bytes loaded across all operations
};

StateStats GetStateStats();

// =============================================================================
// Memory Delta Compression (optional optimization)
// =============================================================================

// Save only memory regions that changed since last save
// Much faster for incremental saves
bool SaveStateDelta(int slot, int base_slot);

// Check if slot contains a delta (requires base slot to restore)
bool IsDeltaState(int slot);
int GetDeltaBaseSlot(int slot);

// =============================================================================
// Internal: Current Program Name (set by program load hook)
// =============================================================================

void SetCurrentProgramName(const std::string& name);
std::string GetCurrentProgramName();

// =============================================================================
// Environment Variable Initialization
// =============================================================================

// Initialize state system from environment variables:
//   EXPLORER_AUTO_RESTORE=1    Enable auto-restore on exit
//   EXPLORER_RESTORE_SLOT=N    Slot to restore from (default: 0)
//   EXPLORER_RESTORE_CODES=0,1 Comma-separated exit codes that trigger restore
void State_InitFromEnv();

} // namespace Explorer

#else // !EXPLORER_ENABLED

namespace Explorer {

constexpr int MAX_STATE_SLOTS = 16;

enum class StateFlags : uint32_t { None = 0, IncludeVRAM = 1, Compressed = 2, AutoRestore = 4 };
inline StateFlags operator|(StateFlags a, StateFlags b) { return StateFlags::None; }
inline StateFlags operator&(StateFlags a, StateFlags b) { return StateFlags::None; }
inline bool HasFlag(StateFlags, StateFlags) { return false; }

struct StateInfo {
    int slot; bool occupied; uint64_t instruction_count; uint32_t pc_physical;
    uint64_t timestamp; size_t memory_size; size_t total_size;
    std::string program_name; StateFlags flags;
};

inline bool SaveState(int, StateFlags = StateFlags::None) { return false; }
inline bool LoadState(int) { return false; }
inline bool HasState(int) { return false; }
inline void ClearState(int) {}
inline void ClearAllStates() {}
inline StateInfo GetStateInfo(int) { return {}; }
inline std::vector<int> GetOccupiedSlots() { return {}; }

inline bool QuickSave(StateFlags = StateFlags::None) { return false; }
inline bool QuickLoad() { return false; }
inline bool HasQuickSave() { return false; }

inline void SetAutoRestoreEnabled(bool) {}
inline bool IsAutoRestoreEnabled() { return false; }
inline void SetAutoRestoreExitCodes(const std::vector<uint8_t>&) {}
inline void SetAutoRestoreSlot(int) {}
inline int GetAutoRestoreSlot() { return 0; }
inline bool OnProgramExit(uint8_t, bool) { return false; }

struct StateStats { uint32_t saves_count, loads_count, auto_restores; uint64_t total_bytes_saved, total_bytes_loaded; };
inline StateStats GetStateStats() { return {}; }

inline bool SaveStateDelta(int, int) { return false; }
inline bool IsDeltaState(int) { return false; }
inline int GetDeltaBaseSlot(int) { return -1; }

inline void SetCurrentProgramName(const std::string&) {}
inline std::string GetCurrentProgramName() { return ""; }

inline void State_InitFromEnv() {}

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_STATE_H
