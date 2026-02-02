// SPDX-License-Identifier: MIT
// Explorer public API implementation

#include "explorer_api.h"

#ifdef EXPLORER_ENABLED

#include "explorer.h"
#include "explorer_data.h"
#include "explorer_nn.h"
#include "explorer_log.h"
#include "explorer_trace.h"
#include <sstream>
#include <fstream>
#include <iomanip>

namespace Explorer {

// =============================================================================
// Static state
// =============================================================================

static bool g_initialized = false;
static ExplorerConfig g_config;

static CoverageCallback g_coverage_callback;
static StallCallback g_stall_callback;
static TickCallback g_tick_callback;
static uint64_t g_tick_interval = 0;
static uint64_t g_last_tick = 0;

// =============================================================================
// Initialization
// =============================================================================

bool Initialize() {
    ExplorerConfig default_config;
    return InitializeWithConfig(default_config);
}

bool InitializeWithConfig(const ExplorerConfig& config) {
    if (g_initialized) {
        return true;  // Already initialized
    }
    
    g_config = config;
    
    // Initialize coverage/instrumentation
    Config instr_config;
    instr_config.enabled = config.enable_coverage;
    instr_config.coverage_mode = config.track_edges ? CoverageMode::Edge : CoverageMode::Physical;
    instr_config.coverage_size = static_cast<uint32_t>(config.coverage_map_size);
    instr_config.trace_length = config.enable_tracing ? static_cast<uint32_t>(config.trace_ring_size) : 0u;
    instr_config.data_tracking_enabled = config.enable_data_tracking;
    instr_config.pc_repeat_limit = config.enable_stall_detection ? config.pc_repeat_threshold : 0u;
    instr_config.stall_limit = config.enable_stall_detection ? config.loop_threshold : 0u;
    
    if (!GetInstrumenter().Init(instr_config)) {
        return false;
    }
    
    // Initialize data tracking
    DataConfig data_config;
    data_config.enabled = config.enable_data_tracking;
    data_config.bucket_shift = config.data_bucket_shift;
    data_config.max_address = config.max_data_address;
    data_config.filter_vram = config.filter_vram;
    data_config.filter_bda = config.filter_bda;
    
    if (!GetDataCollector().Init(data_config)) {
        GetInstrumenter().Shutdown();
        return false;
    }
    
    // Initialize policy if requested
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (config.enable_policy) {
        PolicyConfig policy_config;
        policy_config.enabled = true;
        policy_config.model_path = config.policy_model_path;
        policy_config.exploration_rate = config.exploration_rate;
        
        if (!GetPolicyNetwork().Init(policy_config)) {
            // Policy init failure is non-fatal, continue without it
        }
    }
#endif
    
    // Initialize trace recording
    if (config.enable_tracing && config.trace_ring_size > 0) {
        SetTraceDepth(config.trace_ring_size);
        SetTraceEnabled(true);
    }
    
    g_initialized = true;

    // Optional logging (controlled via env vars)
    Log_InitFromEnv();
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log_Shutdown();
    
    GetInstrumenter().Shutdown();
    GetDataCollector().Shutdown();
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    GetPolicyNetwork().Shutdown();
#endif
    
    g_coverage_callback = nullptr;
    g_stall_callback = nullptr;
    g_tick_callback = nullptr;
    
    g_initialized = false;
}

bool IsInitialized() {
    return g_initialized;
}

// =============================================================================
// Run Control
// =============================================================================

void StartRun() {
    if (!g_initialized) return;
    
    GetInstrumenter().ResetRun();
    GetDataCollector().ResetRun();
}

uint32_t EndRun() {
    if (!g_initialized) return 0;
    
    // Calculate coverage gain
    uint32_t coverage_gain = GetInstrumenter().GetRunNewBits();
    uint32_t data_gain = GetDataCollector().GetRunNewBits();
    
    return coverage_gain + data_gain;
}

void CommitRun() {
    if (!g_initialized) return;
    
    GetInstrumenter().CommitGain();
    GetDataCollector().CommitGain();
}

void ResetAll() {
    if (!g_initialized) return;
    
    GetInstrumenter().Reset();
    GetDataCollector().Shutdown();
    
    // Re-init data collector with same config
    DataConfig data_config;
    data_config.enabled = g_config.enable_data_tracking;
    data_config.bucket_shift = g_config.data_bucket_shift;
    data_config.max_address = g_config.max_data_address;
    data_config.filter_vram = g_config.filter_vram;
    data_config.filter_bda = g_config.filter_bda;
    GetDataCollector().Init(data_config);
}

// =============================================================================
// Coverage Queries
// =============================================================================

CoverageStats GetCoverageStats() {
    CoverageStats stats = {};
    
    if (!g_initialized) return stats;
    
    auto& inst = GetInstrumenter();
    
    stats.instructions_executed = inst.GetInstructionCount();
    stats.coverage_bits_set = inst.GetGlobalNewBits();
    
    // Count unique PCs from coverage map
    const uint8_t* map = inst.GetCoverageMap();
    size_t map_size = inst.GetCoverageMapSize();
    
    uint32_t total_bits = 0;
    for (size_t i = 0; i < map_size; i++) {
        total_bits += __builtin_popcount(map[i]);
    }
    
    stats.unique_pcs_visited = total_bits;  // Approximate
    stats.unique_edges_visited = total_bits;  // Same for now
    stats.coverage_percentage = (map_size > 0) ? 
        (100.0f * total_bits) / (map_size * 8) : 0.0f;
    
    return stats;
}

const uint8_t* GetCoverageMap() {
    if (!g_initialized) return nullptr;
    return GetInstrumenter().GetCoverageMap();
}

size_t GetCoverageMapSize() {
    if (!g_initialized) return 0;
    return GetInstrumenter().GetCoverageMapSize();
}

uint64_t GetCoverageHash() {
    if (!g_initialized) return 0;
    return GetInstrumenter().GetCoverageHash();
}

// =============================================================================
// Data Access Queries
// =============================================================================

DataStats GetDataStats() {
    DataStats stats = {};
    
    if (!g_initialized) return stats;
    
    auto& data = GetDataCollector();
    
    stats.total_accesses = data.GetTotalAccesses();
    stats.run_new_bits = data.GetRunNewBits();
    stats.global_new_bits = data.GetGlobalNewBits();
    
    // Count unique buckets (would need to iterate flags)
    // For now, use bit counts as approximation
    stats.unique_buckets_read = stats.global_new_bits / 2;
    stats.unique_buckets_written = stats.global_new_bits / 2;
    
    return stats;
}

uint8_t GetDataBucketFlags(uint32_t bucket_index) {
    if (!g_initialized) return 0;
    return GetDataCollector().GetBucketFlags(bucket_index);
}

// =============================================================================
// Stall Detection
// =============================================================================

bool IsStalled() {
    if (!g_initialized) return false;
    return GetInstrumenter().IsStalled();
}

StallInfo GetStallInfo() {
    StallInfo info = {};
    
    if (!g_initialized) return info;
    
    auto& inst = GetInstrumenter();
    
    info.is_stalled = inst.IsStalled();
    info.repeated_pc_count = inst.GetRepeatedPCCount();
    info.loop_iteration_count = inst.GetLoopIterationCount();
    info.last_pc = inst.GetLastPC();
    info.cycles_since_progress = inst.GetCyclesSinceProgress();
    
    return info;
}

void SignalProgress() {
    if (!g_initialized) return;
    GetInstrumenter().SignalProgress();
}

// =============================================================================
// Trace Access
// =============================================================================

size_t GetRecentTrace(TraceEntry* buffer, size_t max_entries) {
    if (!g_initialized || !buffer || max_entries == 0) return 0;
    
    // Get from trace ring - would need Instrumenter method
    // For now, return 0
    return 0;
}

// =============================================================================
// RL Policy Interface
// =============================================================================

#ifdef EXPLORER_ENABLE_LIBTORCH

size_t BuildObservation(float* buffer, size_t buffer_size) {
    if (!g_initialized || !buffer) return 0;
    
    ObservationBuilder builder;
    builder.SetDimension(buffer_size);
    builder.Reset();
    
    // Add coverage summary
    builder.AddCoverageSummary(
        GetInstrumenter().GetCoverageMap(),
        GetInstrumenter().GetCoverageMapSize()
    );
    
    // Add data summary
    builder.AddDataSummary(
        GetDataCollector().GetGlobalBucketFlags(),
        GetDataCollector().GetBucketCount()
    );
    
    // Add state features
    // Note: Would need access to CPU regs - pass through parameters
    builder.AddStateFeatures(0, 0, 0, 
        GetInstrumenter().GetInstructionCount(),
        GetInstrumenter().GetRepeatedPCCount());
    
    const auto& obs = builder.GetObservation();
    size_t copy_size = std::min(buffer_size, obs.size());
    std::copy(obs.begin(), obs.begin() + copy_size, buffer);
    
    return copy_size;
}

uint32_t GetPolicyAction() {
    if (!g_initialized) return ACTION_NOOP;
    
    std::vector<float> obs(DEFAULT_OBS_DIM);
    BuildObservation(obs.data(), obs.size());
    
    return GetPolicyNetwork().SelectAction(obs);
}

uint8_t ActionToScancode(uint32_t action) {
    return PolicyNetwork::ActionToScancode(action);
}

bool LoadPolicyModel(const std::string& path) {
    if (!g_initialized) return false;
    return GetPolicyNetwork().LoadModel(path);
}

#endif

// =============================================================================
// Callbacks
// =============================================================================

void SetCoverageCallback(CoverageCallback callback) {
    g_coverage_callback = callback;
}

void SetStallCallback(StallCallback callback) {
    g_stall_callback = callback;
}

void SetTickCallback(TickCallback callback, uint64_t interval) {
    g_tick_callback = callback;
    g_tick_interval = interval;
    g_last_tick = 0;
}

// Internal: call tick callback if needed
void CheckTickCallback() {
    if (!g_tick_callback || g_tick_interval == 0) return;
    
    uint64_t current = GetInstrumenter().GetInstructionCount();
    if (current - g_last_tick >= g_tick_interval) {
        g_tick_callback(current);
        g_last_tick = current;
    }
}

// =============================================================================
// Debug / Status
// =============================================================================

std::string GetStatusString() {
    if (!g_initialized) {
        return "Explorer: Not initialized";
    }
    
    std::ostringstream ss;
    auto cov = GetCoverageStats();
    auto data = GetDataStats();
    auto stall = GetStallInfo();
    
    ss << "Explorer Status\n"
       << "---------------\n"
       << "Instructions: " << cov.instructions_executed << "\n"
       << "Coverage bits: " << cov.coverage_bits_set 
       << " (" << std::fixed << std::setprecision(2) << cov.coverage_percentage << "%)\n"
       << "Data accesses: " << data.total_accesses << "\n"
       << "Data new bits: " << data.global_new_bits << "\n"
       << "Stalled: " << (stall.is_stalled ? "YES" : "no") << "\n";
    
    if (stall.is_stalled) {
        ss << "  PC repeats: " << stall.repeated_pc_count << "\n"
           << "  Loop iters: " << stall.loop_iteration_count << "\n";
    }
    
    return ss.str();
}

bool DumpState(const std::string& filepath) {
    if (!g_initialized) return false;
    
    std::ofstream file(filepath, std::ios::binary);
    if (!file) return false;
    
    // Write coverage map
    const uint8_t* map = GetCoverageMap();
    size_t size = GetCoverageMapSize();
    
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    file.write(reinterpret_cast<const char*>(map), size);
    
    // Write stats
    auto stats = GetCoverageStats();
    file.write(reinterpret_cast<const char*>(&stats), sizeof(stats));
    
    return file.good();
}

bool LoadState(const std::string& filepath) {
    // TODO: Implement state loading
    (void)filepath;
    return false;
}

// =============================================================================
// Input Fuzzing Control
// =============================================================================

void EnableFuzzing(bool enable) {
    if (!g_initialized) return;
    GetInstrumenter().SetFuzzEnabled(enable);
}

bool IsFuzzingEnabled() {
    if (!g_initialized) return false;
    return GetInstrumenter().IsFuzzEnabled();
}

void ConfigureFuzzing(const FuzzConfig& config) {
    if (!g_initialized) return;
    GetInstrumenter().SetFuzzKeyInterval(config.key_interval);
    GetInstrumenter().SetFuzzMouseInterval(config.mouse_interval);
    // Note: Additional config fields can be added to Instrumenter methods as needed
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
