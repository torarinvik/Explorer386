/*
 *  Explorer - DOSBox Instrumentation Module
 *  Implementation
 */

#include "explorer.h"

#include <fstream>
#include <cstring>
#include <queue>
#include <mutex>

namespace Explorer {

// =============================================================================
// Global State
// =============================================================================

Config g_config;
std::atomic<bool> g_initialized{false};
std::atomic<uint64_t> g_instruction_count{0};
std::atomic<uint64_t> g_coverage_count{0};

uint8_t* g_global_coverage = nullptr;
uint8_t* g_run_coverage = nullptr;

uint32_t* g_trace_ring = nullptr;
size_t g_trace_pos = 0;

uint32_t g_prev_pc = 0;

// Input queue
static std::queue<InputAction> g_input_queue;
static std::mutex g_input_mutex;

// Data collection
static bool g_data_collection_enabled = false;
static std::vector<DataSample> g_samples;
static std::mutex g_samples_mutex;
static uint64_t g_last_tick = 0;

// =============================================================================
// Core API
// =============================================================================

bool Initialize(const Config& config) {
    if (g_initialized.load()) {
        return false;  // Already initialized
    }
    
    g_config = config;
    
    // Ensure coverage size is power of 2
    if ((g_config.coverage_size & (g_config.coverage_size - 1)) != 0) {
        // Round up to next power of 2
        size_t n = 1;
        while (n < g_config.coverage_size) n <<= 1;
        g_config.coverage_size = n;
    }
    
    // Allocate coverage bitmaps
    g_global_coverage = new uint8_t[g_config.coverage_size]();
    g_run_coverage = new uint8_t[g_config.coverage_size]();
    
    // Allocate trace ring
    g_trace_ring = new uint32_t[g_config.trace_length]();
    g_trace_pos = 0;
    
    // Reset counters
    g_instruction_count.store(0);
    g_coverage_count.store(0);
    g_prev_pc = 0;
    
    g_initialized.store(true);
    
    return true;
}

void Shutdown() {
    g_initialized.store(false);
    
    delete[] g_global_coverage;
    delete[] g_run_coverage;
    delete[] g_trace_ring;
    
    g_global_coverage = nullptr;
    g_run_coverage = nullptr;
    g_trace_ring = nullptr;
}

void ResetRun() {
    if (!g_initialized.load()) return;
    
    // Clear run coverage but keep global
    std::memset(g_run_coverage, 0, g_config.coverage_size);
    
    // Clear trace
    std::memset(g_trace_ring, 0, g_config.trace_length * sizeof(uint32_t));
    g_trace_pos = 0;
    
    g_prev_pc = 0;
    g_instruction_count.store(0);
}

Stats GetStats() {
    Stats s;
    s.instructions = g_instruction_count.load();
    s.coverage_unique = g_coverage_count.load();
    s.edges_seen = 0;  // TODO: implement edge counting
    s.last_pc = g_prev_pc;
    return s;
}

// =============================================================================
// Event Tracking
// =============================================================================

void NoteInterrupt(uint8_t num, uint32_t type, uint32_t ax_value) {
    if (!g_initialized.load()) return;
    // Could log to event trace here
    (void)num; (void)type; (void)ax_value;
}

void NotePortIn(uint16_t port, uint32_t value, uint8_t size) {
    if (!g_initialized.load()) return;
    (void)port; (void)value; (void)size;
}

void NotePortOut(uint16_t port, uint32_t value, uint8_t size) {
    if (!g_initialized.load()) return;
    (void)port; (void)value; (void)size;
}

// =============================================================================
// Coverage Operations
// =============================================================================

bool IsCovered(uint32_t addr) {
    if (!g_initialized.load() || !g_global_coverage) return false;
    uint32_t idx = addr & (g_config.coverage_size - 1);
    return g_global_coverage[idx] != 0;
}

size_t GetCoverageCount() {
    return g_coverage_count.load();
}

bool ExportCoverage(const std::string& path) {
    if (!g_initialized.load() || !g_global_coverage) return false;
    
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    
    // Write header
    uint32_t magic = 0x434F5645;  // "COVE"
    uint32_t size = static_cast<uint32_t>(g_config.coverage_size);
    f.write(reinterpret_cast<char*>(&magic), 4);
    f.write(reinterpret_cast<char*>(&size), 4);
    f.write(reinterpret_cast<char*>(g_global_coverage), g_config.coverage_size);
    
    return f.good();
}

bool ImportCoverage(const std::string& path) {
    if (!g_initialized.load() || !g_global_coverage) return false;
    
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    
    uint32_t magic, size;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&size), 4);
    
    if (magic != 0x434F5645) return false;
    if (size > g_config.coverage_size) size = static_cast<uint32_t>(g_config.coverage_size);
    
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    
    // Merge with existing
    MergeCoverage(buf.data(), size);
    
    return true;
}

void MergeCoverage(const uint8_t* other, size_t size) {
    if (!g_initialized.load() || !g_global_coverage || !other) return;
    
    size_t merge_size = std::min(size, g_config.coverage_size);
    for (size_t i = 0; i < merge_size; i++) {
        if (other[i] && !g_global_coverage[i]) {
            g_global_coverage[i] = 1;
            g_coverage_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// =============================================================================
// RL Policy Interface
// =============================================================================

void PolicyTick() {
    if (!g_initialized.load()) return;
    // Called periodically to invoke the RL policy
    // Implementation in explorer_nn.cpp
}

void QueueInput(const InputAction& action) {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    g_input_queue.push(action);
}

bool GetPendingInput(InputAction& action) {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (g_input_queue.empty()) return false;
    action = g_input_queue.front();
    g_input_queue.pop();
    return true;
}

// =============================================================================
// Data Collection
// =============================================================================

void SetDataCollectionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_samples_mutex);
    g_data_collection_enabled = enabled;
    if (!enabled) {
        g_samples.clear();
    }
}

std::vector<DataSample> CollectSamples() {
    std::lock_guard<std::mutex> lock(g_samples_mutex);
    std::vector<DataSample> result;
    result.swap(g_samples);
    return result;
}

} // namespace Explorer
