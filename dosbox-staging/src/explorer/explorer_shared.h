// SPDX-License-Identifier: MIT
// Shared coverage for multi-worker training
// Allows parallel DOSBox workers to share coverage information

#ifndef DOSBOX_EXPLORER_SHARED_H
#define DOSBOX_EXPLORER_SHARED_H

#include <cstdint>
#include <string>
#include <memory>
#include <atomic>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace Explorer {

// Shared memory structure for coverage data
struct SharedCoverageHeader {
    std::atomic<uint64_t> version;           // Incremented on each update
    std::atomic<uint32_t> total_coverage;    // Total unique coverage points
    std::atomic<uint32_t> total_instructions; // Total instructions (billions)
    std::atomic<uint32_t> worker_count;      // Number of active workers
    std::atomic<uint32_t> reserved[5];       // Future use
    // Followed by coverage bitmap
};

class SharedCoverage {
public:
    static constexpr size_t COVERAGE_BITMAP_SIZE = 1 << 20;  // 1M bits = 128KB
    static constexpr size_t HEADER_SIZE = sizeof(SharedCoverageHeader);
    static constexpr size_t TOTAL_SIZE = HEADER_SIZE + COVERAGE_BITMAP_SIZE / 8;
    
    SharedCoverage() : shm_name_(), worker_id_(0), fd_(-1), mapped_(nullptr), header_(nullptr), bitmap_(nullptr), initialized_(false), is_creator_(false) {}
    ~SharedCoverage();
    
    // Non-copyable, non-movable
    SharedCoverage(const SharedCoverage&) = delete;
    SharedCoverage& operator=(const SharedCoverage&) = delete;
    SharedCoverage(SharedCoverage&&) = delete;
    SharedCoverage& operator=(SharedCoverage&&) = delete;
    
    // Initialize shared memory
    bool Init(const std::string& shm_name, int worker_id);
    void Shutdown();
    
    // Check if address is globally new (not seen by any worker)
    bool IsGloballyNew(uint32_t addr);
    
    // Atomically mark address as seen globally.
    // Returns true if this call observed it as globally new.
    bool TryMarkSeen(uint32_t addr);

    // Backwards-compatible helper (ignores whether it was new).
    void MarkSeen(uint32_t addr) { (void)TryMarkSeen(addr); }
    
    // Batch update: merge local coverage into shared
    uint32_t MergeCoverage(const uint8_t* local_bitmap, size_t size);
    
    // Get current global stats
    uint32_t GetTotalCoverage() const;
    uint64_t GetVersion() const;
    int GetWorkerCount() const;
    
    bool IsInitialized() const { return initialized_; }
    
private:
    int fd_ = -1;
    void* mapped_ = nullptr;
    SharedCoverageHeader* header_ = nullptr;
    uint8_t* bitmap_ = nullptr;
    int worker_id_ = -1;
    std::string shm_name_;
    bool initialized_ = false;
    bool is_creator_ = false;
    
    uint32_t HashAddress(uint32_t addr) const {
        // Simple hash to spread addresses across bitmap
        return (addr ^ (addr >> 12) ^ (addr >> 24)) & (COVERAGE_BITMAP_SIZE - 1);
    }
};

// Global shared coverage instance
SharedCoverage& GetSharedCoverage();

} // namespace Explorer

#endif // DOSBOX_EXPLORER_SHARED_H
