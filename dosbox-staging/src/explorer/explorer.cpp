// SPDX-License-Identifier: MIT
// Explorer instrumentation implementation for DOSBox-Staging

#include "explorer.h"

#include "explorer_input.h"
#include "explorer_log.h"

#include <cstring>
#include <algorithm>

namespace Explorer {

// =============================================================================
// Stop Reason Names
// =============================================================================

const char* StopReasonName(StopReason r) {
    switch (r) {
        case StopReason::None: return "none";
        case StopReason::BlockedInput: return "blocked_input";
        case StopReason::Budget: return "budget";
        case StopReason::ProgramExit: return "program_exit";
        case StopReason::Stall: return "stall";
        case StopReason::PCRepeat: return "pc_repeat";
        case StopReason::Other: return "other";
        default: return "unknown";
    }
}

// =============================================================================
// Instrumenter Implementation
// =============================================================================

Instrumenter::Instrumenter() = default;
Instrumenter::~Instrumenter() = default;

bool Instrumenter::Init(const Config& config) {
    config_ = config;
    
    if (!config_.enabled) {
        initialized_ = false;
        return true;
    }
    
    // Validate coverage size (must be power of 2)
    uint32_t cov_size = (config_.coverage_mode == CoverageMode::Physical) 
        ? config_.coverage_size 
        : config_.edge_coverage_size;
    
    if (cov_size == 0 || (cov_size & (cov_size - 1)) != 0) {
        return false;
    }
    
    coverage_mask_ = cov_size - 1;
    
    // Allocate coverage bitmaps
    global_coverage_ = std::make_unique<uint8_t[]>(cov_size);
    run_coverage_ = std::make_unique<uint8_t[]>(cov_size);
    std::memset(global_coverage_.get(), 0, cov_size);
    std::memset(run_coverage_.get(), 0, cov_size);
    
    run_touched_.reserve(cov_size / 16);  // Reasonable initial capacity
    
    // Allocate trace buffers if enabled
    if (config_.trace_length > 0) {
        trace_ring_.resize(config_.trace_length);
        trace_ring_inst_.resize(config_.trace_length);
        event_ring_.resize(std::min(config_.trace_length, 8192u));
    }
    
    // Initialize loop tracking
    ResetLoopTracking();
    
    initialized_ = true;
    return true;
}

void Instrumenter::Shutdown() {
    global_coverage_.reset();
    run_coverage_.reset();
    run_touched_.clear();
    trace_ring_.clear();
    trace_ring_inst_.clear();
    event_ring_.clear();
    initialized_ = false;
}

void Instrumenter::ResetRun() {
    // Clear run-specific coverage
    for (uint32_t idx : run_touched_) {
        run_coverage_[idx] = 0;
    }
    run_touched_.clear();
    
    // Reset counters
    inst_counter_ = 0;
    run_new_coverage_ = 0;
    last_new_coverage_inst_ = 0;
    timer_ticks_ = 0;
    
    // Reset trace buffers
    trace_ring_pos_ = 0;
    trace_ring_count_ = 0;
    event_ring_pos_ = 0;
    event_ring_count_ = 0;
    
    // Reset edge state
    prev_location_ = 0;
    
    // Reset loop tracking
    ResetLoopTracking();
    
    // Reset stop state
    stop_requested_ = false;
    stop_reason_ = StopReason::None;
}

// =============================================================================
// Coverage Tracking
// =============================================================================

uint32_t Instrumenter::HashU32(uint32_t x) {
    // MurmurHash3 finalizer
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
    return x;
}

bool Instrumenter::NoteCoveragePhys(uint32_t phys) {
    // Apply region filter if enabled
    if (config_.region_enabled) {
        if (phys < config_.region_start || phys >= config_.region_end) {
            return false;
        }
    }
    
    uint32_t idx = phys & coverage_mask_;
    
    if (run_coverage_[idx] == 0) {
        run_coverage_[idx] = 1;
        run_touched_.push_back(idx);
        
        if (global_coverage_[idx] == 0) {
            global_coverage_[idx] = 1;
            run_new_coverage_++;
            last_new_coverage_inst_ = static_cast<uint32_t>(inst_counter_);
            return true;
        }
    }
    return false;
}

bool Instrumenter::NoteCoverageEdge(uint32_t src, uint32_t dst) {
    // AFL-style edge coverage: hash of (src >> 1) ^ dst
    uint32_t edge = (src >> 1) ^ dst;
    uint32_t idx = edge & coverage_mask_;
    
    if (run_coverage_[idx] == 0) {
        run_coverage_[idx] = 1;
        run_touched_.push_back(idx);
        
        if (global_coverage_[idx] == 0) {
            global_coverage_[idx] = 1;
            run_new_coverage_++;
            last_new_coverage_inst_ = static_cast<uint32_t>(inst_counter_);
            return true;
        }
    }
    return false;
}

uint32_t Instrumenter::GetTotalCoverage() const {
    if (!global_coverage_) return 0;
    
    uint32_t count = 0;
    uint32_t size = coverage_mask_ + 1;
    for (uint32_t i = 0; i < size; i++) {
        if (global_coverage_[i]) count++;
    }
    return count;
}

uint64_t Instrumenter::HashRunCoverage() const {
    // FNV-1a hash over touched indices
    uint64_t h = 14695981039346656037ULL;
    for (uint32_t idx : run_touched_) {
        h ^= static_cast<uint64_t>(idx & 0xFF);
        h *= 1099511628211ULL;
        h ^= static_cast<uint64_t>((idx >> 8) & 0xFF);
        h *= 1099511628211ULL;
        h ^= static_cast<uint64_t>((idx >> 16) & 0xFF);
        h *= 1099511628211ULL;
        h ^= static_cast<uint64_t>((idx >> 24) & 0xFF);
        h *= 1099511628211ULL;
    }
    h ^= static_cast<uint64_t>(stop_reason_);
    h *= 1099511628211ULL;
    h ^= static_cast<uint64_t>(run_touched_.size());
    h *= 1099511628211ULL;
    return h;
}

// =============================================================================
// Trace Recording
// =============================================================================

void Instrumenter::NoteTrace(uint32_t phys) {
    if (trace_ring_.empty()) return;
    
    trace_ring_[trace_ring_pos_] = phys;
    trace_ring_inst_[trace_ring_pos_] = static_cast<uint32_t>(inst_counter_);
    trace_ring_pos_ = (trace_ring_pos_ + 1) % trace_ring_.size();
    if (trace_ring_count_ < trace_ring_.size()) {
        trace_ring_count_++;
    }
}

void Instrumenter::NoteEvent(EventKind kind, uint8_t int_no, uint16_t port, uint16_t value) {
    if (event_ring_.empty()) return;
    
    Event& e = event_ring_[event_ring_pos_];
    e.pc_phys = current_pc_;
    e.inst_num = static_cast<uint32_t>(inst_counter_);
    e.kind = kind;
    e.int_no = int_no;
    e.port = port;
    e.value = value;
    // Note: ax/bx/cx/dx/ds/es would need to be filled from CPU state
    // For now, leave them as-is (caller can fill if needed)
    
    event_ring_pos_ = (event_ring_pos_ + 1) % event_ring_.size();
    if (event_ring_count_ < event_ring_.size()) {
        event_ring_count_++;
    }
}

// =============================================================================
// Loop/PC Repeat Detection
// =============================================================================

void Instrumenter::ResetLoopTracking() {
    std::memset(loop_keys_, 0, sizeof(loop_keys_));
    std::memset(loop_counts_, 0, sizeof(loop_counts_));
}

bool Instrumenter::NoteLoopPC(uint32_t phys) {
    if (config_.pc_repeat_limit == 0) return false;
    
    uint32_t hash = HashU32(phys);
    uint32_t idx = hash % LOOP_MAP_SIZE;
    
    // Simple open-addressing lookup
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t probe = (idx + i) % LOOP_MAP_SIZE;
        if (loop_keys_[probe] == phys) {
            loop_counts_[probe]++;
            if (loop_counts_[probe] >= config_.pc_repeat_limit) {
                return true;  // PC repeat limit hit
            }
            return false;
        }
        if (loop_keys_[probe] == 0) {
            loop_keys_[probe] = phys;
            loop_counts_[probe] = 1;
            return false;
        }
    }
    
    // Table full, just overwrite first slot
    loop_keys_[idx] = phys;
    loop_counts_[idx] = 1;
    return false;
}

// =============================================================================
// Main Hooks
// =============================================================================

void Instrumenter::PreInstruction(uint32_t phys_pc) {
    current_pc_ = phys_pc;
    
    // Coverage tracking
    bool new_cov = false;
    if (config_.coverage_mode == CoverageMode::Physical) {
        new_cov = NoteCoveragePhys(phys_pc);
    }
    // Edge coverage is handled in PostInstruction
    
    // Trace
    NoteTrace(phys_pc);
    
    // Stall detection
    if (config_.stall_limit > 0) {
        uint32_t since_new = static_cast<uint32_t>(inst_counter_) - last_new_coverage_inst_;
        if (since_new >= config_.stall_limit) {
            RequestStop(StopReason::Stall);
            return;
        }
    }
    
    // PC repeat detection
    if (NoteLoopPC(phys_pc)) {
        RequestStop(StopReason::PCRepeat);
        return;
    }
}

void Instrumenter::PostInstruction(uint32_t prev_phys, uint32_t next_phys) {
    // Edge coverage
    if (config_.coverage_mode == CoverageMode::Edge) {
        NoteCoverageEdge(prev_location_, next_phys);
        prev_location_ = next_phys;
    }
    
    // Increment instruction counter
    inst_counter_++;
    
    // Timer tick check
    if (config_.tick_interval > 0 && (inst_counter_ % config_.tick_interval) == 0) {
        Tick();
    }
}

void Instrumenter::NoteRead(uint32_t phys_addr, uint8_t size, uint32_t pc_phys) {
    // Data tracking is handled separately in explorer_data.cpp
    // This is a hook point for the DataCollector
    (void)phys_addr;
    (void)size;
    (void)pc_phys;
}

void Instrumenter::NoteWrite(uint32_t phys_addr, uint8_t size, uint32_t pc_phys, uint32_t value) {
    // Data tracking is handled separately in explorer_data.cpp
    (void)phys_addr;
    (void)size;
    (void)pc_phys;
    (void)value;
}

void Instrumenter::NoteInterrupt(uint8_t int_no, uint32_t pc_phys, uint16_t ax) {
    NoteEvent(EventKind::Interrupt, int_no, 0, ax);
    (void)pc_phys;
}

void Instrumenter::NotePortIn(uint16_t port, uint16_t value, uint32_t pc_phys) {
    NoteEvent(EventKind::PortIn, 0, port, value);
    (void)pc_phys;
}

void Instrumenter::NotePortOut(uint16_t port, uint16_t value, uint32_t pc_phys) {
    NoteEvent(EventKind::PortOut, 0, port, value);
    (void)pc_phys;
}

void Instrumenter::Tick() {
    timer_ticks_++;
    
    // Fuzz input injection
    if (config_.fuzz_enabled) {
        // Keyboard fuzzing
        if (config_.fuzz_keyboard_enabled && 
            config_.fuzz_key_interval > 0 &&
            (timer_ticks_ % config_.fuzz_key_interval) == 0) {
            InjectRandomKey();
        }
        
        // Mouse fuzzing
        if (config_.fuzz_mouse_enabled &&
            config_.fuzz_mouse_interval > 0 &&
            (timer_ticks_ % config_.fuzz_mouse_interval) == 0) {
            InjectRandomMouseMove(config_.fuzz_mouse_max_delta);
            // Occasionally click
            if ((timer_ticks_ % (config_.fuzz_mouse_interval * 5)) == 0) {
                InjectRandomMouseClick();
            }
        }
    }

    Explorer::Log_OnTick(inst_counter_);
}

void Instrumenter::RequestStop(StopReason reason) {
    if (!stop_requested_) {
        stop_requested_ = true;
        stop_reason_ = reason;
    }
}

void Instrumenter::SignalProgress() {
    last_progress_inst_ = inst_counter_;
    is_stalled_ = false;
    repeated_pc_count_ = 0;
    max_loop_count_ = 0;
}

void Instrumenter::Reset() {
    // Full reset - clear global coverage too
    if (global_coverage_) {
        uint32_t cov_size = (config_.coverage_mode == CoverageMode::Physical) 
            ? config_.coverage_size 
            : config_.edge_coverage_size;
        std::memset(global_coverage_.get(), 0, cov_size);
    }
    
    global_new_bits_ = 0;
    coverage_hash_ = 0;
    
    ResetRun();
}

uint32_t Instrumenter::CommitGain() {
    uint32_t gain = 0;
    
    // The run coverage that differs from global is already tracked in run_new_coverage_
    // Actually commit by ensuring global has all run coverage
    for (uint32_t idx : run_touched_) {
        if (run_coverage_[idx] && !global_coverage_[idx]) {
            global_coverage_[idx] = 1;
            global_new_bits_++;
            gain++;
            
            // Update hash
            coverage_hash_ ^= idx;
            coverage_hash_ *= 0x100000001b3ULL;  // FNV-1a prime
        }
    }
    
    return gain;
}

// =============================================================================
// Global Singleton
// =============================================================================

static Instrumenter g_instrumenter;

Instrumenter& GetInstrumenter() {
    return g_instrumenter;
}

} // namespace Explorer
