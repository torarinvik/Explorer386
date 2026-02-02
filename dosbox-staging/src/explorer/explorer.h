// SPDX-License-Identifier: MIT
// Explorer instrumentation for DOSBox-Staging
// Ported from 8086tiny Explorer mode for 386+ support

#ifndef DOSBOX_EXPLORER_H
#define DOSBOX_EXPLORER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

namespace Explorer {

// =============================================================================
// Configuration
// =============================================================================

enum class CoverageMode {
    Physical = 0,  // Track unique physical addresses
    Edge = 1,      // Track unique (src, dst) edges
};

struct Config {
    bool enabled = false;
    
    // Coverage settings
    CoverageMode coverage_mode = CoverageMode::Physical;
    uint32_t coverage_size = 1 << 20;  // 1M entries for physical coverage
    uint32_t edge_coverage_size = 1 << 16;  // 64K for edge coverage
    
    // Optional region filtering
    bool region_enabled = false;
    uint32_t region_start = 0;
    uint32_t region_end = 0;
    
    // Trace settings
    uint32_t trace_length = 0;  // 0 = disabled
    uint32_t trace_max_stored = 16;
    
    // Stall detection
    uint32_t stall_limit = 0;  // Instructions without new coverage before stall
    uint32_t pc_repeat_limit = 0;  // Same PC repeat count before stall
    
    // Data access tracking
    bool data_tracking_enabled = false;
    uint32_t data_bucket_shift = 6;  // 64-byte buckets
    float data_reward_weight = 0.1f;
    bool data_filter_vram = true;
    bool data_filter_bda = true;
    
    // Policy settings
    uint32_t tick_interval = 20000;  // Instructions between policy ticks
    
    // Fuzz input settings
    bool fuzz_enabled = false;         // Enable random input injection
    uint32_t fuzz_key_interval = 5;    // Ticks between random key presses
    uint32_t fuzz_mouse_interval = 10; // Ticks between random mouse moves
    float fuzz_mouse_max_delta = 30.0f; // Max mouse movement per tick
    bool fuzz_mouse_enabled = true;    // Enable mouse fuzzing
    bool fuzz_keyboard_enabled = true; // Enable keyboard fuzzing
    
    // Output
    std::string output_path = "explore_out.json";
};

// =============================================================================
// Stop Reasons
// =============================================================================

enum class StopReason {
    None = 0,
    BlockedInput = 1,
    Budget = 2,
    ProgramExit = 3,
    Stall = 4,
    PCRepeat = 5,
    Other = 6,
};

const char* StopReasonName(StopReason r);

// =============================================================================
// Event Types (for tracing)
// =============================================================================

enum class EventKind : uint8_t {
    Interrupt = 0,
    PortIn = 1,
    PortOut = 2,
};

struct Event {
    uint32_t pc_phys;
    uint32_t inst_num;
    EventKind kind;
    uint8_t int_no;      // For interrupts
    uint16_t port;       // For I/O
    uint16_t value;      // AL/AX value
    uint16_t ax, bx, cx, dx;
    uint16_t ds, es;
};

// =============================================================================
// Main Instrumentation Class
// =============================================================================

class Instrumenter {
public:
    Instrumenter();
    ~Instrumenter();
    
    // Lifecycle
    bool Init(const Config& config);
    void Shutdown();
    void ResetRun();
    
    // Configuration
    const Config& GetConfig() const { return config_; }
    bool IsEnabled() const { return config_.enabled && initialized_; }
    
    // Pre/Post instruction hooks (called from CPU core)
    void PreInstruction(uint32_t phys_pc);
    void PostInstruction(uint32_t prev_phys, uint32_t next_phys);
    
    // Memory access hooks
    void NoteRead(uint32_t phys_addr, uint8_t size, uint32_t pc_phys);
    void NoteWrite(uint32_t phys_addr, uint8_t size, uint32_t pc_phys, uint32_t value);
    
    // Event hooks
    void NoteInterrupt(uint8_t int_no, uint32_t pc_phys, uint16_t ax);
    void NotePortIn(uint16_t port, uint16_t value, uint32_t pc_phys);
    void NotePortOut(uint16_t port, uint16_t value, uint32_t pc_phys);
    
    // Timer tick (called periodically)
    void Tick();
    
    // Coverage queries
    uint32_t GetRunCoverageGain() const { return run_new_coverage_; }
    uint32_t GetTotalCoverage() const;
    uint64_t HashRunCoverage() const;
    
    // Additional coverage access for API
    const uint8_t* GetCoverageMap() const { return global_coverage_.get(); }
    size_t GetCoverageMapSize() const { 
        return (config_.coverage_mode == CoverageMode::Physical) 
            ? config_.coverage_size : config_.edge_coverage_size; 
    }
    uint64_t GetCoverageHash() const { return coverage_hash_; }
    uint32_t GetRunNewBits() const { return run_new_coverage_; }
    uint32_t GetGlobalNewBits() const { return global_new_bits_; }
    uint64_t GetInstructionCount() const { return inst_counter_; }
    
    // Stall detection queries
    bool IsStalled() const { return is_stalled_; }
    uint32_t GetRepeatedPCCount() const { return repeated_pc_count_; }
    uint32_t GetLoopIterationCount() const { return max_loop_count_; }
    uint32_t GetLastPC() const { return current_pc_; }
    uint64_t GetCyclesSinceProgress() const { return inst_counter_ - last_progress_inst_; }
    
    // Manual progress signal
    void SignalProgress();
    
    // Full reset (global + run state)
    void Reset();
    
    // Commit run coverage to global
    uint32_t CommitGain();
    
    // Stop control
    void RequestStop(StopReason reason);
    bool ShouldStop() const { return stop_requested_; }
    StopReason GetStopReason() const { return stop_reason_; }
    
    // Instruction counter
    uint64_t GetInstCounter() const { return inst_counter_; }
    void IncrementInstCounter() { inst_counter_++; }
    
    // Fuzzing control
    void SetFuzzEnabled(bool enabled) { config_.fuzz_enabled = enabled; }
    bool IsFuzzEnabled() const { return config_.fuzz_enabled; }
    void SetFuzzKeyInterval(uint32_t interval) { config_.fuzz_key_interval = interval; }
    void SetFuzzMouseInterval(uint32_t interval) { config_.fuzz_mouse_interval = interval; }
    
    // Current PC (for other modules)
    uint32_t GetCurrentPC() const { return current_pc_; }
    
    // Trace access
    const std::vector<uint32_t>& GetTraceRing() const { return trace_ring_; }
    const std::vector<Event>& GetEventRing() const { return event_ring_; }

private:
    Config config_;
    bool initialized_ = false;
    
    // Coverage bitmaps
    std::unique_ptr<uint8_t[]> global_coverage_;
    std::unique_ptr<uint8_t[]> run_coverage_;
    std::vector<uint32_t> run_touched_;
    uint32_t coverage_mask_ = 0;
    
    // Edge coverage state
    uint32_t prev_location_ = 0;
    
    // Counters
    uint64_t inst_counter_ = 0;
    uint32_t run_new_coverage_ = 0;
    uint32_t global_new_bits_ = 0;
    uint32_t last_new_coverage_inst_ = 0;
    uint32_t timer_ticks_ = 0;
    uint64_t coverage_hash_ = 0;
    
    // Stall detection state
    bool is_stalled_ = false;
    uint32_t repeated_pc_count_ = 0;
    uint32_t max_loop_count_ = 0;
    uint64_t last_progress_inst_ = 0;
    
    // PC tracking for stall/repeat detection
    uint32_t current_pc_ = 0;
    static constexpr uint32_t LOOP_MAP_SIZE = 4096;
    uint32_t loop_keys_[LOOP_MAP_SIZE] = {};
    uint16_t loop_counts_[LOOP_MAP_SIZE] = {};
    
    // Trace ring buffers
    std::vector<uint32_t> trace_ring_;
    std::vector<uint32_t> trace_ring_inst_;
    uint32_t trace_ring_pos_ = 0;
    uint32_t trace_ring_count_ = 0;
    
    std::vector<Event> event_ring_;
    uint32_t event_ring_pos_ = 0;
    uint32_t event_ring_count_ = 0;
    
    // Stop control
    bool stop_requested_ = false;
    StopReason stop_reason_ = StopReason::None;
    
    // Helper functions
    bool NoteCoveragePhys(uint32_t phys);
    bool NoteCoverageEdge(uint32_t src, uint32_t dst);
    void NoteTrace(uint32_t phys);
    void NoteEvent(EventKind kind, uint8_t int_no, uint16_t port, uint16_t value);
    bool NoteLoopPC(uint32_t phys);
    void ResetLoopTracking();
    
    static uint32_t HashU32(uint32_t x);
};

// =============================================================================
// Global Instance
// =============================================================================

// Singleton accessor
Instrumenter& GetInstrumenter();

// Quick enable checks (avoid function call overhead when disabled)
inline bool InstrumentationEnabled() {
    return GetInstrumenter().IsEnabled();
}

inline bool DataTrackingEnabled() {
    return GetInstrumenter().IsEnabled() && 
           GetInstrumenter().GetConfig().data_tracking_enabled;
}

// Convenience functions (call global instance)
inline void PreInstruction(uint32_t phys_pc) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().PreInstruction(phys_pc);
}

inline void PostInstruction() {
    // Simple version without args - stall detection handled internally
}

inline void PostInstruction(uint32_t prev_phys, uint32_t next_phys) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().PostInstruction(prev_phys, next_phys);
}

inline void NoteRead(uint32_t addr, uint8_t size, uint32_t pc) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().NoteRead(addr, size, pc);
}

inline void NoteWrite(uint32_t addr, uint8_t size, uint32_t pc, uint32_t val) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().NoteWrite(addr, size, pc, val);
}

inline void NoteInterrupt(uint8_t num) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().NoteInterrupt(num, 0, 0);
}

inline void NoteInterrupt(uint8_t num, uint32_t pc, uint16_t ax) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().NoteInterrupt(num, pc, ax);
}

inline void NotePortIn(uint16_t port, uint16_t val) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().NotePortIn(port, val, 0);
}

inline void NotePortIn(uint16_t port, uint16_t val, uint32_t pc) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().NotePortIn(port, val, pc);
}

inline void NotePortOut(uint16_t port, uint16_t val) {
    if (GetInstrumenter().IsEnabled())
        GetInstrumenter().NotePortOut(port, val, 0);
}

// Program lifecycle notifications
void NoteProgramLoad(const char* name, bool success);
void NoteProgramExit(uint8_t exit_code, bool is_tsr);

// Auto-save on program load (used with auto-restore)
void SetAutoSaveOnLoad(bool enabled);
bool IsAutoSaveOnLoadEnabled();

inline bool ShouldStop() {
    return GetInstrumenter().IsEnabled() && GetInstrumenter().ShouldStop();
}

inline uint32_t GetCurrentPC() {
    return GetInstrumenter().GetCurrentPC();
}

} // namespace Explorer

#endif // DOSBOX_EXPLORER_H
