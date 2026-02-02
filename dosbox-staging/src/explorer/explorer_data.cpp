// SPDX-License-Identifier: MIT
// Explorer data access tracking implementation

#include "explorer_data.h"
#include <cstring>
#include <algorithm>

namespace Explorer {

// =============================================================================
// DataCollector Implementation
// =============================================================================

DataCollector::DataCollector() = default;
DataCollector::~DataCollector() = default;

bool DataCollector::Init(const DataConfig& config) {
    config_ = config;
    
    if (!config_.enabled) {
        initialized_ = false;
        return true;
    }
    
    bucket_size_ = 1u << config_.bucket_shift;
    bucket_count_ = (config_.max_address + bucket_size_ - 1) / bucket_size_;
    
    // Allocate bucket arrays
    run_bucket_flags_ = std::make_unique<uint8_t[]>(bucket_count_);
    global_bucket_flags_ = std::make_unique<uint8_t[]>(bucket_count_);
    
    // 6 count slots per bucket: R8, R16, R32, W8, W16, W32
    run_bucket_counts_ = std::make_unique<uint32_t[]>(bucket_count_ * 6);
    global_bucket_counts_ = std::make_unique<uint32_t[]>(bucket_count_ * 6);
    
    std::memset(run_bucket_flags_.get(), 0, bucket_count_);
    std::memset(global_bucket_flags_.get(), 0, bucket_count_);
    std::memset(run_bucket_counts_.get(), 0, bucket_count_ * 6 * sizeof(uint32_t));
    std::memset(global_bucket_counts_.get(), 0, bucket_count_ * 6 * sizeof(uint32_t));
    
    run_touched_buckets_.reserve(bucket_count_ / 16);
    
    initialized_ = true;
    return true;
}

void DataCollector::Shutdown() {
    run_bucket_flags_.reset();
    global_bucket_flags_.reset();
    run_bucket_counts_.reset();
    global_bucket_counts_.reset();
    run_touched_buckets_.clear();
    initialized_ = false;
}

void DataCollector::ResetRun() {
    // Clear only touched buckets (O(touched) instead of O(total))
    for (uint32_t bucket : run_touched_buckets_) {
        run_bucket_flags_[bucket] = 0;
        for (int i = 0; i < 6; i++) {
            run_bucket_counts_[bucket * 6 + i] = 0;
        }
    }
    run_touched_buckets_.clear();
    
    run_accesses_ = 0;
    run_new_bits_ = 0;
    prev_data_hash_ = data_hash_state_;
}

bool DataCollector::ShouldFilter(uint32_t addr) const {
    if (config_.filter_vram && addr >= VRAM_START && addr < VRAM_END) {
        return true;
    }
    if (config_.filter_bda && addr >= BIOS_DATA_AREA && addr < BDA_END) {
        return true;
    }
    return false;
}

uint32_t DataCollector::AddrToBucket(uint32_t addr) const {
    return addr >> config_.bucket_shift;
}

uint8_t DataCollector::SizeToKind(uint8_t size, bool is_write) const {
    uint8_t base = is_write ? 3 : 0;  // W8/W16/W32 start at index 3
    switch (size) {
        case 1: return base + 0;  // R8 or W8
        case 2: return base + 1;  // R16 or W16
        case 4: return base + 2;  // R32 or W32
        default: return base + 0; // Treat unknown as 8-bit
    }
}

void DataCollector::UpdateHash(uint32_t bucket, uint8_t kind) {
    // Simple hash update for novelty signal
    uint64_t val = (static_cast<uint64_t>(bucket) << 8) | kind;
    data_hash_state_ ^= val;
    data_hash_state_ *= 1099511628211ULL;
}

void DataCollector::NoteRead(uint32_t phys_addr, uint8_t size, uint32_t pc_phys) {
    (void)pc_phys;  // Could use for per-PC tracking later
    
    if (phys_addr >= config_.max_address) return;
    if (ShouldFilter(phys_addr)) return;
    
    run_accesses_++;
    total_accesses_++;
    
    uint32_t bucket = AddrToBucket(phys_addr);
    if (bucket >= bucket_count_) return;
    
    uint8_t kind_idx = SizeToKind(size, false);
    uint8_t kind_bit = 1u << kind_idx;
    
    // Track this bucket was touched this run
    if (run_bucket_flags_[bucket] == 0) {
        run_touched_buckets_.push_back(bucket);
    }
    
    // Check if this is a new access pattern
    if ((run_bucket_flags_[bucket] & kind_bit) == 0) {
        run_bucket_flags_[bucket] |= kind_bit;
        
        if ((global_bucket_flags_[bucket] & kind_bit) == 0) {
            run_new_bits_++;
            UpdateHash(bucket, kind_idx);
        }
    }
    
    // Increment count
    run_bucket_counts_[bucket * 6 + kind_idx]++;
}

void DataCollector::NoteWrite(uint32_t phys_addr, uint8_t size, uint32_t pc_phys) {
    (void)pc_phys;
    
    if (phys_addr >= config_.max_address) return;
    if (ShouldFilter(phys_addr)) return;
    
    run_accesses_++;
    total_accesses_++;
    
    uint32_t bucket = AddrToBucket(phys_addr);
    if (bucket >= bucket_count_) return;
    
    uint8_t kind_idx = SizeToKind(size, true);
    uint8_t kind_bit = 1u << kind_idx;
    
    if (run_bucket_flags_[bucket] == 0) {
        run_touched_buckets_.push_back(bucket);
    }
    
    if ((run_bucket_flags_[bucket] & kind_bit) == 0) {
        run_bucket_flags_[bucket] |= kind_bit;
        
        if ((global_bucket_flags_[bucket] & kind_bit) == 0) {
            run_new_bits_++;
            UpdateHash(bucket, kind_idx);
        }
    }
    
    run_bucket_counts_[bucket * 6 + kind_idx]++;
}

uint32_t DataCollector::CommitGain() {
    uint32_t gain = 0;
    
    for (uint32_t bucket : run_touched_buckets_) {
        uint8_t run_flags = run_bucket_flags_[bucket];
        uint8_t global_flags = global_bucket_flags_[bucket];
        uint8_t new_flags = run_flags & ~global_flags;
        
        if (new_flags) {
            global_bucket_flags_[bucket] |= run_flags;
            gain += __builtin_popcount(new_flags);
            global_new_bits_ += __builtin_popcount(new_flags);
            
            // Also commit counts
            for (int i = 0; i < 6; i++) {
                global_bucket_counts_[bucket * 6 + i] += run_bucket_counts_[bucket * 6 + i];
            }
        }
    }
    
    return gain;
}

// =============================================================================
// Global Singleton
// =============================================================================

static DataCollector g_data_collector;

DataCollector& GetDataCollector() {
    return g_data_collector;
}

} // namespace Explorer
