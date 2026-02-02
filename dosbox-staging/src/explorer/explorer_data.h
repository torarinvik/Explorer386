// SPDX-License-Identifier: MIT
// Explorer data access tracking for DOSBox-Staging
// Ported from explorer_data.h for memory access pattern analysis

#ifndef DOSBOX_EXPLORER_DATA_H
#define DOSBOX_EXPLORER_DATA_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

namespace Explorer {

// =============================================================================
// Access Type Masks
// =============================================================================

constexpr uint8_t DATA_R8  = 0x01;  // 8-bit read
constexpr uint8_t DATA_R16 = 0x02;  // 16-bit read
constexpr uint8_t DATA_R32 = 0x04;  // 32-bit read (386+)
constexpr uint8_t DATA_W8  = 0x08;  // 8-bit write
constexpr uint8_t DATA_W16 = 0x10;  // 16-bit write
constexpr uint8_t DATA_W32 = 0x20;  // 32-bit write (386+)

constexpr uint8_t DATA_READ  = DATA_R8 | DATA_R16 | DATA_R32;
constexpr uint8_t DATA_WRITE = DATA_W8 | DATA_W16 | DATA_W32;
constexpr uint8_t DATA_8BIT  = DATA_R8 | DATA_W8;
constexpr uint8_t DATA_16BIT = DATA_R16 | DATA_W16;
constexpr uint8_t DATA_32BIT = DATA_R32 | DATA_W32;

// =============================================================================
// Memory Regions
// =============================================================================

constexpr uint32_t CONVENTIONAL_START = 0x00000;
constexpr uint32_t CONVENTIONAL_END   = 0xA0000;   // 640KB
constexpr uint32_t VRAM_START         = 0xA0000;
constexpr uint32_t VRAM_END           = 0xC0000;   // 128KB VRAM
constexpr uint32_t BIOS_DATA_AREA     = 0x00400;
constexpr uint32_t BDA_END            = 0x00500;

// Extended memory (386+)
// Note: DOSBox defines an XMS_START macro; avoid name collisions.
constexpr uint32_t kXmsStart          = 0x100000;  // 1MB
constexpr uint32_t kXmsEnd            = 0x1000000; // 16MB (typical max for tracking)

// =============================================================================
// Data Collector Configuration
// =============================================================================

struct DataConfig {
    bool enabled = false;
    uint32_t bucket_shift = 6;       // 64-byte buckets by default
    float reward_weight = 0.1f;      // Weight in RL reward
    bool filter_vram = true;         // Ignore VRAM accesses
    bool filter_bda = true;          // Ignore BIOS Data Area
    uint32_t max_address = 0x1000000; // Track up to 16MB
};

// =============================================================================
// Data Collector Class
// =============================================================================

class DataCollector {
public:
    DataCollector();
    ~DataCollector();
    
    bool Init(const DataConfig& config);
    void Shutdown();
    void ResetRun();
    
    bool IsEnabled() const { return config_.enabled && initialized_; }
    
    // Access tracking
    void NoteRead(uint32_t phys_addr, uint8_t size, uint32_t pc_phys);
    void NoteWrite(uint32_t phys_addr, uint8_t size, uint32_t pc_phys);
    
    // Commit run results to global state
    uint32_t CommitGain();
    
    // Statistics
    uint32_t GetRunNewBits() const { return static_cast<uint32_t>(run_new_bits_); }
    uint32_t GetGlobalNewBits() const { return static_cast<uint32_t>(global_new_bits_); }
    uint64_t GetTotalAccesses() const { return total_accesses_; }
    uint64_t GetRunAccesses() const { return run_accesses_; }
    
    // Hash for RL reward
    uint64_t GetDataHashState() const { return data_hash_state_; }
    
    // Access to bucket flags (for API)
    const uint8_t* GetGlobalBucketFlags() const { return global_bucket_flags_.get(); }
    uint32_t GetBucketCount() const { return bucket_count_; }
    uint8_t GetBucketFlags(uint32_t index) const {
        if (index < bucket_count_ && global_bucket_flags_) {
            return global_bucket_flags_[index];
        }
        return 0;
    }

private:
    DataConfig config_;
    bool initialized_ = false;
    
    uint32_t bucket_size_ = 0;
    uint32_t bucket_count_ = 0;
    
    // Per-bucket access flags
    std::unique_ptr<uint8_t[]> run_bucket_flags_;
    std::unique_ptr<uint8_t[]> global_bucket_flags_;
    
    // Per-bucket access counts (for detailed analysis)
    // Layout: counts_[bucket * 6 + kind_idx] where kind_idx = R8,R16,R32,W8,W16,W32
    std::unique_ptr<uint32_t[]> run_bucket_counts_;
    std::unique_ptr<uint32_t[]> global_bucket_counts_;
    
    // Touched bucket tracking for efficient reset
    std::vector<uint32_t> run_touched_buckets_;
    
    // Statistics
    uint64_t total_accesses_ = 0;
    uint64_t run_accesses_ = 0;
    uint64_t run_new_bits_ = 0;
    uint64_t global_new_bits_ = 0;
    
    // Hash state for reward signal
    uint64_t data_hash_state_ = 0;
    uint64_t prev_data_hash_ = 0;
    
    // Helpers
    bool ShouldFilter(uint32_t addr) const;
    uint32_t AddrToBucket(uint32_t addr) const;
    uint8_t SizeToKind(uint8_t size, bool is_write) const;
    void UpdateHash(uint32_t bucket, uint8_t kind);
};

// =============================================================================
// Global Instance
// =============================================================================

DataCollector& GetDataCollector();

// Convenience hooks
inline void DataNoteRead(uint32_t addr, uint8_t size, uint32_t pc) {
    if (GetDataCollector().IsEnabled())
        GetDataCollector().NoteRead(addr, size, pc);
}

inline void DataNoteWrite(uint32_t addr, uint8_t size, uint32_t pc) {
    if (GetDataCollector().IsEnabled())
        GetDataCollector().NoteWrite(addr, size, pc);
}

} // namespace Explorer

#endif // DOSBOX_EXPLORER_DATA_H
