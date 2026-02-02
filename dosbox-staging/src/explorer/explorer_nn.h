// SPDX-License-Identifier: MIT
// Explorer neural network policy support for DOSBox-staging
// Provides reinforcement learning based keyboard input selection

#ifndef EXPLORER_NN_H
#define EXPLORER_NN_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <memory>
#include <string>

#ifdef EXPLORER_ENABLE_LIBTORCH
#include <torch/script.h>
#endif

namespace Explorer {

// =============================================================================
// Constants
// =============================================================================

// Input observation dimensions (based on original Explorer)
constexpr size_t OBS_COVERAGE_BUCKETS = 256;    // Coverage bitmap summary
constexpr size_t OBS_DATA_BUCKETS = 128;        // Data access summary  
constexpr size_t OBS_SCREEN_W = 80;             // Text mode columns
constexpr size_t OBS_SCREEN_H = 25;             // Text mode rows
constexpr size_t OBS_STATE_DIM = 16;            // Emulator state features

// Total observation size (can be configured)
constexpr size_t DEFAULT_OBS_DIM = OBS_COVERAGE_BUCKETS + OBS_DATA_BUCKETS + OBS_STATE_DIM;

// Action space: keyboard scancodes + special actions
constexpr size_t NUM_SCANCODES = 128;           // Standard PC scancodes
constexpr size_t NUM_SPECIAL_ACTIONS = 8;       // Wait, combo keys, etc.
constexpr size_t DEFAULT_ACTION_DIM = NUM_SCANCODES + NUM_SPECIAL_ACTIONS;

// Special action indices
constexpr uint32_t ACTION_WAIT = NUM_SCANCODES + 0;
constexpr uint32_t ACTION_CTRL = NUM_SCANCODES + 1;
constexpr uint32_t ACTION_ALT = NUM_SCANCODES + 2;
constexpr uint32_t ACTION_SHIFT = NUM_SCANCODES + 3;
constexpr uint32_t ACTION_ENTER = NUM_SCANCODES + 4;
constexpr uint32_t ACTION_ESC = NUM_SCANCODES + 5;
constexpr uint32_t ACTION_RANDOM = NUM_SCANCODES + 6;
constexpr uint32_t ACTION_NOOP = NUM_SCANCODES + 7;

// =============================================================================
// Policy Configuration
// =============================================================================

struct PolicyConfig {
    bool enabled = false;
    std::string model_path;             // Path to TorchScript model
    
    size_t obs_dim = DEFAULT_OBS_DIM;
    size_t action_dim = DEFAULT_ACTION_DIM;
    
    float exploration_rate = 0.1f;      // Epsilon for epsilon-greedy
    float temperature = 1.0f;           // Softmax temperature
    
    bool use_gpu = false;               // Use CUDA if available
    bool deterministic = false;         // Always pick argmax action
    
    // Reward shaping
    float coverage_reward = 1.0f;       // Reward per new coverage bit
    float data_reward = 0.5f;           // Reward per new data bucket
    float time_penalty = -0.001f;       // Small penalty per step
    float stall_penalty = -1.0f;        // Penalty for detected stall
};

// =============================================================================
// Observation Builder
// =============================================================================

class ObservationBuilder {
public:
    ObservationBuilder() = default;
    
    void SetDimension(size_t dim);
    void Reset();
    
    // Build observation from current emulator state
    void AddCoverageSummary(const uint8_t* coverage_map, size_t map_size);
    void AddDataSummary(const uint8_t* data_flags, size_t bucket_count);
    void AddStateFeatures(uint32_t pc, uint32_t sp, uint32_t flags, 
                          uint64_t instruction_count, uint32_t stall_count);
    
    // Access the built observation
    const std::vector<float>& GetObservation() const { return obs_; }
    float* GetMutableData() { return obs_.data(); }
    size_t GetSize() const { return obs_.size(); }
    
private:
    std::vector<float> obs_;
    size_t write_pos_ = 0;
};

// =============================================================================
// Policy Network Wrapper
// =============================================================================

class PolicyNetwork {
public:
    PolicyNetwork();
    ~PolicyNetwork();
    
    // Non-copyable
    PolicyNetwork(const PolicyNetwork&) = delete;
    PolicyNetwork& operator=(const PolicyNetwork&) = delete;
    
    // Initialize with configuration
    bool Init(const PolicyConfig& config);
    void Shutdown();
    
    // Load model from file
    bool LoadModel(const std::string& path);
    bool IsLoaded() const { return model_loaded_; }
    
    // Get action from observation
    // Returns action index [0, action_dim)
    uint32_t SelectAction(const std::vector<float>& observation);
    uint32_t SelectAction(const float* obs_data, size_t obs_size);
    
    // Get action probabilities (for debugging/analysis)
    std::vector<float> GetActionProbs(const std::vector<float>& observation);
    
    // Random action selection (fallback or exploration)
    uint32_t RandomAction();
    
    // Convert action to scancode
    static uint8_t ActionToScancode(uint32_t action);
    static bool IsSpecialAction(uint32_t action);
    
    // Configuration access
    const PolicyConfig& GetConfig() const { return config_; }
    
    // Statistics
    uint64_t GetInferenceCount() const { return inference_count_; }
    uint64_t GetExplorationCount() const { return exploration_count_; }
    
private:
    PolicyConfig config_;
    bool initialized_ = false;
    bool model_loaded_ = false;
    
    uint64_t inference_count_ = 0;
    uint64_t exploration_count_ = 0;
    
    // Random state
    uint64_t rng_state_ = 0x12345678DEADBEEF;
    uint32_t RandomU32();
    float RandomFloat();
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    torch::jit::script::Module model_;
    torch::Device device_ = torch::kCPU;
    
    std::vector<float> action_logits_;
    
    uint32_t SampleFromLogits(const std::vector<float>& logits);
#endif
};

// =============================================================================
// Action Queue
// =============================================================================

// Manages a queue of actions to be executed
class ActionQueue {
public:
    ActionQueue() = default;
    
    void Clear();
    void Push(uint32_t action, uint32_t hold_frames = 1);
    void PushScancode(uint8_t scancode, uint32_t hold_frames = 1);
    
    bool HasPending() const { return !queue_.empty(); }
    uint32_t Peek() const;
    uint32_t Pop();
    
    size_t Size() const { return queue_.size(); }
    
private:
    struct QueuedAction {
        uint32_t action;
        uint32_t frames_remaining;
    };
    std::vector<QueuedAction> queue_;
};

// =============================================================================
// Global Access
// =============================================================================

PolicyNetwork& GetPolicyNetwork();
ActionQueue& GetActionQueue();

// Convenience functions
inline bool PolicyEnabled() { 
    return GetPolicyNetwork().GetConfig().enabled && GetPolicyNetwork().IsLoaded(); 
}

inline uint32_t GetNextAction(const std::vector<float>& obs) {
    return GetPolicyNetwork().SelectAction(obs);
}

} // namespace Explorer

#endif // EXPLORER_NN_H
