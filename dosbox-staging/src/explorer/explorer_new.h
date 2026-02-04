/*
 *  Explorer - DOSBox Instrumentation Module
 *  Provides coverage tracking, data access logging, and RL policy hooks
 */

#ifndef DOSBOX_EXPLORER_H
#define DOSBOX_EXPLORER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

// Forward declarations
namespace Explorer {

// Coverage mode types
enum class CoverageMode {
    Physical,   // Physical address only
    Edge        // src_addr XOR dst_addr edge coverage
};

// Event types for tracing
enum class EventType {
    Interrupt,
    PortIn,
    PortOut,
    MemRead,
    MemWrite
};

// Configuration structure
struct Config {
    bool enabled = false;
    CoverageMode coverage_mode = CoverageMode::Edge;
    size_t coverage_size = 65536;
    size_t trace_length = 4096;
    bool nn_policy_enabled = false;
    std::string nn_model_path;
    uint32_t tick_interval = 20000;
    bool datalog_enabled = false;
    std::string datalog_path;
};

// Global state
extern Config g_config;
extern std::atomic<bool> g_initialized;
extern std::atomic<uint64_t> g_instruction_count;
extern std::atomic<uint64_t> g_coverage_count;

// Coverage bitmaps
extern uint8_t* g_global_coverage;  // Persistent coverage across runs
extern uint8_t* g_run_coverage;     // Coverage for current run

// Trace ring buffer
extern uint32_t* g_trace_ring;
extern size_t g_trace_pos;

// Previous PC for edge coverage
extern uint32_t g_prev_pc;

// =============================================================================
// Core API
// =============================================================================

// Initialize the explorer module
bool Initialize(const Config& config);

// Shutdown and cleanup
void Shutdown();

// Reset for a new run (clears run coverage, trace, etc.)
void ResetRun();

// Get current statistics
struct Stats {
    uint64_t instructions;
    uint64_t coverage_unique;
    uint64_t edges_seen;
    uint32_t last_pc;
};
Stats GetStats();

// =============================================================================
// Instrumentation Hooks (called from CPU core)
// =============================================================================

// Called before each instruction executes
// phys_pc: physical address of the instruction
inline void PreInstruction(uint32_t phys_pc);

// Called after instruction executes, before moving to next
// prev_pc: PC of just-executed instruction
// next_pc: PC of next instruction
inline void PostInstruction(uint32_t prev_pc, uint32_t next_pc);

// Memory access tracking
inline void NoteRead(uint32_t addr, uint8_t size, uint32_t pc);
inline void NoteWrite(uint32_t addr, uint8_t size, uint32_t pc, uint32_t value);

// Interrupt tracking
void NoteInterrupt(uint8_t num, uint32_t type, uint32_t ax_value);

// I/O port tracking
void NotePortIn(uint16_t port, uint32_t value, uint8_t size);
void NotePortOut(uint16_t port, uint32_t value, uint8_t size);

// =============================================================================
// Coverage Operations
// =============================================================================

// Check if an address was covered
bool IsCovered(uint32_t addr);

// Get total coverage count
size_t GetCoverageCount();

// Export coverage to file
bool ExportCoverage(const std::string& path);

// Import coverage from file
bool ImportCoverage(const std::string& path);

// Merge coverage from another bitmap
void MergeCoverage(const uint8_t* other, size_t size);

// =============================================================================
// RL Policy Interface
// =============================================================================

// Tick the policy (called periodically)
void PolicyTick();

// Queue an input action from the policy
struct InputAction {
    enum Type { None, KeyDown, KeyUp, MouseMove, MouseButton };
    Type type = None;
    uint16_t scancode = 0;
    int16_t mouse_dx = 0;
    int16_t mouse_dy = 0;
    uint8_t mouse_buttons = 0;
};
void QueueInput(const InputAction& action);

// Get pending input (called by input subsystem)
bool GetPendingInput(InputAction& action);

// =============================================================================
// Data Collection for Training
// =============================================================================

struct DataSample {
    uint64_t tick;
    uint32_t pc;
    uint32_t coverage_delta;
    std::vector<uint8_t> vram_snapshot;
    InputAction action_taken;
    float reward;
};

// Enable/disable data collection
void SetDataCollectionEnabled(bool enabled);

// Get collected samples (moves ownership)
std::vector<DataSample> CollectSamples();

// =============================================================================
// Inline implementations for hot path
// =============================================================================

inline void PreInstruction(uint32_t phys_pc) {
    if (!g_initialized.load(std::memory_order_relaxed)) return;
    
    g_instruction_count.fetch_add(1, std::memory_order_relaxed);
    
    // Edge coverage: hash of (prev_pc, curr_pc)
    uint32_t idx;
    if (g_config.coverage_mode == CoverageMode::Edge) {
        idx = (g_prev_pc >> 1) ^ phys_pc;
    } else {
        idx = phys_pc;
    }
    idx &= (g_config.coverage_size - 1);
    
    // Increment coverage counter (saturate at 255)
    if (g_run_coverage[idx] < 255) {
        g_run_coverage[idx]++;
    }
    
    // Mark in global coverage (set bit)
    uint8_t old = g_global_coverage[idx];
    if (old == 0) {
        g_global_coverage[idx] = 1;
        g_coverage_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Update trace ring
    g_trace_ring[g_trace_pos] = phys_pc;
    g_trace_pos = (g_trace_pos + 1) % g_config.trace_length;
}

inline void PostInstruction(uint32_t prev_pc, uint32_t next_pc) {
    if (!g_initialized.load(std::memory_order_relaxed)) return;
    g_prev_pc = prev_pc;
    (void)next_pc;  // Could be used for additional edge tracking
}

inline void NoteRead(uint32_t addr, uint8_t size, uint32_t pc) {
    if (!g_initialized.load(std::memory_order_relaxed)) return;
    if (!g_config.datalog_enabled) return;
    // Data collection happens in separate system
    (void)addr; (void)size; (void)pc;
}

inline void NoteWrite(uint32_t addr, uint8_t size, uint32_t pc, uint32_t value) {
    if (!g_initialized.load(std::memory_order_relaxed)) return;
    if (!g_config.datalog_enabled) return;
    (void)addr; (void)size; (void)pc; (void)value;
}

} // namespace Explorer

#endif // DOSBOX_EXPLORER_H
