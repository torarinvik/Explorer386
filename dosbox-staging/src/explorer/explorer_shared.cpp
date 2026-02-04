// SPDX-License-Identifier: MIT
// Shared coverage implementation for multi-worker training

#include "explorer_shared.h"
#include <cstring>
#include <cerrno>
#include <cstdio>

namespace Explorer {

// =============================================================================
// SharedCoverage Implementation
// =============================================================================

SharedCoverage::~SharedCoverage() {
    Shutdown();
}

bool SharedCoverage::Init(const std::string& shm_name, int worker_id) {
    if (initialized_) {
        return true;
    }
    
    shm_name_ = "/" + shm_name;
    worker_id_ = worker_id;
    
    // Try to open existing shared memory first
    fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
    
    if (fd_ < 0) {
        // Create new shared memory
        fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) {
            fprintf(stderr, "SharedCoverage: Failed to create shared memory: %s\n", strerror(errno));
            return false;
        }
        
        // Set size
        if (ftruncate(fd_, TOTAL_SIZE) < 0) {
            fprintf(stderr, "SharedCoverage: Failed to set shared memory size: %s\n", strerror(errno));
            close(fd_);
            fd_ = -1;
            return false;
        }
        
        is_creator_ = true;
    }
    
    // Map shared memory
    mapped_ = mmap(nullptr, TOTAL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (mapped_ == MAP_FAILED) {
        fprintf(stderr, "SharedCoverage: Failed to map shared memory: %s\n", strerror(errno));
        close(fd_);
        fd_ = -1;
        mapped_ = nullptr;
        return false;
    }
    
    header_ = static_cast<SharedCoverageHeader*>(mapped_);
    bitmap_ = static_cast<uint8_t*>(mapped_) + HEADER_SIZE;
    
    // Initialize if we're the creator
    if (is_creator_) {
        std::memset(mapped_, 0, TOTAL_SIZE);
        header_->version.store(1);
        header_->worker_count.store(1);
        fprintf(stderr, "SharedCoverage: Created shared region (worker %d)\n", worker_id_);
    } else {
        header_->worker_count.fetch_add(1);
        fprintf(stderr, "SharedCoverage: Joined shared region (worker %d, total workers: %d)\n", 
                worker_id_, header_->worker_count.load());
    }
    
    initialized_ = true;
    return true;
}

void SharedCoverage::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Get info before unmapping
    uint32_t remaining = 0;
    uint32_t total_cov = 0;
    bool should_unlink = false;
    
    if (header_) {
        remaining = header_->worker_count.fetch_sub(1) - 1;
        total_cov = header_->total_coverage.load();
        should_unlink = is_creator_ && (remaining == 0);
        fprintf(stderr, "SharedCoverage: Worker %d leaving (remaining: %d, total coverage: %d)\n", 
                worker_id_, remaining, total_cov);
    }
    
    // Unmap first
    if (mapped_ && mapped_ != MAP_FAILED) {
        munmap(mapped_, TOTAL_SIZE);
        mapped_ = nullptr;
        header_ = nullptr;
        bitmap_ = nullptr;
    }
    
    // Then close fd and optionally unlink
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
        
        if (should_unlink) {
            shm_unlink(shm_name_.c_str());
        }
    }
    
    initialized_ = false;
}

bool SharedCoverage::IsGloballyNew(uint32_t addr) {
    if (!initialized_) return true;
    
    uint32_t bit_idx = HashAddress(addr);
    uint32_t byte_idx = bit_idx / 8;
    uint8_t bit_mask = 1 << (bit_idx % 8);
    
    return (bitmap_[byte_idx] & bit_mask) == 0;
}

bool SharedCoverage::TryMarkSeen(uint32_t addr) {
    if (!initialized_) return true;

    const uint32_t bit_idx = HashAddress(addr);
    const uint32_t byte_idx = bit_idx / 8;
    const uint8_t bit_mask = static_cast<uint8_t>(1u << (bit_idx % 8));

    const uint8_t old_val = __atomic_fetch_or(&bitmap_[byte_idx], bit_mask, __ATOMIC_RELAXED);
    if ((old_val & bit_mask) == 0) {
        // This was globally new.
        header_->total_coverage.fetch_add(1);
        header_->version.fetch_add(1);
        return true;
    }
    return false;
}

uint32_t SharedCoverage::MergeCoverage(const uint8_t* local_bitmap, size_t size) {
    if (!initialized_ || !local_bitmap) return 0;
    
    uint32_t new_global = 0;
    size_t bytes = std::min(size, COVERAGE_BITMAP_SIZE / 8);
    
    for (size_t i = 0; i < bytes; i++) {
        if (local_bitmap[i] == 0) continue;
        
        uint8_t old_val = __atomic_fetch_or(&bitmap_[i], local_bitmap[i], __ATOMIC_RELAXED);
        uint8_t new_bits = local_bitmap[i] & ~old_val;
        
        if (new_bits) {
            new_global += __builtin_popcount(new_bits);
        }
    }
    
    if (new_global > 0) {
        header_->total_coverage.fetch_add(new_global);
        header_->version.fetch_add(1);
    }
    
    return new_global;
}

uint32_t SharedCoverage::GetTotalCoverage() const {
    if (!initialized_) return 0;
    return header_->total_coverage.load();
}

uint64_t SharedCoverage::GetVersion() const {
    if (!initialized_) return 0;
    return header_->version.load();
}

int SharedCoverage::GetWorkerCount() const {
    if (!initialized_) return 0;
    return header_->worker_count.load();
}

// Global singleton
static std::unique_ptr<SharedCoverage> g_shared_coverage;

SharedCoverage& GetSharedCoverage() {
    if (!g_shared_coverage) {
        g_shared_coverage = std::make_unique<SharedCoverage>();
    }
    return *g_shared_coverage;
}

} // namespace Explorer
