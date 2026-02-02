// SPDX-License-Identifier: MIT
// Explorer public API for external control and queries
// This is the main interface for controlling Explorer instrumentation

#ifndef EXPLORER_API_H
#define EXPLORER_API_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>

#ifdef EXPLORER_ENABLED

namespace Explorer {

// =============================================================================
// Initialization and Lifecycle
// =============================================================================

// Initialize Explorer with default configuration
// Call this early in DOSBox initialization
bool Initialize();

// Initialize with custom configuration
struct ExplorerConfig {
    // Coverage settings
    bool enable_coverage = true;
    size_t coverage_map_size = 65536;  // 64KB coverage bitmap
    bool track_edges = true;
    
    // Data tracking settings
    bool enable_data_tracking = true;
    uint8_t data_bucket_shift = 8;     // 256-byte buckets
    uint32_t max_data_address = 16 * 1024 * 1024;  // 16MB
    bool filter_vram = true;
    bool filter_bda = true;
    
    // Tracing settings
    bool enable_tracing = true;
    size_t trace_ring_size = 4096;
    
    // Stall detection
    bool enable_stall_detection = true;
    uint32_t pc_repeat_threshold = 50;
    uint32_t loop_threshold = 100;
    
    // RL policy (if LibTorch available)
    bool enable_policy = false;
    std::string policy_model_path;
    float exploration_rate = 0.1f;
};

bool InitializeWithConfig(const ExplorerConfig& config);

// Shutdown and cleanup
void Shutdown();

// Check if initialized
bool IsInitialized();

// =============================================================================
// Run Control
// =============================================================================

// Start a new exploration run
// Resets per-run counters but preserves global coverage
void StartRun();

// End current run and compute gain
// Returns coverage gain for this run
uint32_t EndRun();

// Commit run coverage to global map
void CommitRun();

// Reset all state (global + run)
void ResetAll();

// =============================================================================
// Coverage Queries
// =============================================================================

struct CoverageStats {
    uint64_t instructions_executed;
    uint32_t coverage_bits_set;
    uint32_t unique_pcs_visited;
    uint32_t unique_edges_visited;
    float coverage_percentage;
};

CoverageStats GetCoverageStats();

// Get raw coverage bitmap access (for external analysis)
const uint8_t* GetCoverageMap();
size_t GetCoverageMapSize();

// Get current coverage hash (for novelty comparison)
uint64_t GetCoverageHash();

// =============================================================================
// Data Access Queries
// =============================================================================

struct DataStats {
    uint64_t total_accesses;
    uint32_t unique_buckets_read;
    uint32_t unique_buckets_written;
    uint32_t run_new_bits;
    uint32_t global_new_bits;
};

DataStats GetDataStats();

// Get data flags for a specific bucket
uint8_t GetDataBucketFlags(uint32_t bucket_index);

// =============================================================================
// Stall Detection
// =============================================================================

// Check if emulator appears stalled
bool IsStalled();

// Get stall information
struct StallInfo {
    bool is_stalled;
    uint32_t repeated_pc_count;
    uint32_t loop_iteration_count;
    uint32_t last_pc;
    uint64_t cycles_since_progress;
};

StallInfo GetStallInfo();

// Manually signal that progress was made (resets stall counters)
void SignalProgress();

// =============================================================================
// Trace Access
// =============================================================================

struct TraceEntry {
    uint32_t pc;
    uint32_t timestamp;
    uint8_t event_type;  // 0=instruction, 1=interrupt, 2=port_in, 3=port_out
    uint8_t event_data;
};

// Get recent trace entries
// Returns number of entries written to buffer
size_t GetRecentTrace(TraceEntry* buffer, size_t max_entries);

// =============================================================================
// RL Policy Interface (if LibTorch enabled)
// =============================================================================

#ifdef EXPLORER_ENABLE_LIBTORCH

// Build observation vector from current state
// Returns observation dimension
size_t BuildObservation(float* buffer, size_t buffer_size);

// Get next action from policy
// Returns action index
uint32_t GetPolicyAction();

// Convert action to keyboard scancode
uint8_t ActionToScancode(uint32_t action);

// Load a new policy model
bool LoadPolicyModel(const std::string& path);

#endif

// =============================================================================
// Callbacks for External Integration
// =============================================================================

// Called when new coverage is discovered
using CoverageCallback = std::function<void(uint32_t pc, uint32_t bits_gained)>;
void SetCoverageCallback(CoverageCallback callback);

// Called when stall is detected
using StallCallback = std::function<void(const StallInfo& info)>;
void SetStallCallback(StallCallback callback);

// Called periodically (every N instructions)
using TickCallback = std::function<void(uint64_t instruction_count)>;
void SetTickCallback(TickCallback callback, uint64_t interval);

// =============================================================================
// Debug / Status
// =============================================================================

// Get status string for display
std::string GetStatusString();

// Dump state to file
bool DumpState(const std::string& filepath);

// Load state from file
bool LoadState(const std::string& filepath);

} // namespace Explorer

#else // !EXPLORER_ENABLED

// Stub implementations when Explorer is disabled
namespace Explorer {

inline bool Initialize() { return true; }
inline void Shutdown() {}
inline bool IsInitialized() { return false; }
inline void StartRun() {}
inline uint32_t EndRun() { return 0; }
inline void CommitRun() {}
inline void ResetAll() {}

struct CoverageStats { uint64_t instructions_executed; uint32_t coverage_bits_set; 
                       uint32_t unique_pcs_visited; uint32_t unique_edges_visited;
                       float coverage_percentage; };
inline CoverageStats GetCoverageStats() { return {}; }

struct DataStats { uint64_t total_accesses; uint32_t unique_buckets_read;
                   uint32_t unique_buckets_written; uint32_t run_new_bits; 
                   uint32_t global_new_bits; };
inline DataStats GetDataStats() { return {}; }

inline bool IsStalled() { return false; }
struct StallInfo { bool is_stalled; uint32_t repeated_pc_count; uint32_t loop_iteration_count;
                   uint32_t last_pc; uint64_t cycles_since_progress; };
inline StallInfo GetStallInfo() { return {}; }

inline std::string GetStatusString() { return "Explorer disabled"; }

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_API_H
