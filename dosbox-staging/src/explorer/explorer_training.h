// SPDX-License-Identifier: MIT
// Explorer training module for PPO-based reinforcement learning
// Trains directly in DOSBox using LibTorch for maximum efficiency

#ifndef EXPLORER_TRAINING_H
#define EXPLORER_TRAINING_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <string>
#include <deque>

#ifdef EXPLORER_ENABLE_LIBTORCH
#include <torch/torch.h>
#endif

namespace Explorer {

// =============================================================================
// Observation Dimensions (for 381-action space)
// =============================================================================

struct ObservationConfig {
    // VRAM observation (downsampled)
    size_t vram_size = 256 * 1024;       // 256KB VRAM
    size_t vram_downsample = 4;          // 4x downsample -> 64KB
    
    // RAM window around CS:IP
    size_t ram_window_size = 64 * 1024;  // 64KB window
    size_t ram_downsample = 4;           // 4x downsample -> 16KB
    
    // Input state (recent actions, mouse position)
    size_t input_state_size = 32;
    
    // Coverage summary
    size_t coverage_summary_size = 256;
    
    // Derived sizes
    size_t GetVRAMObsSize() const { return vram_size / vram_downsample; }
    size_t GetRAMObsSize() const { return ram_window_size / ram_downsample; }
    size_t GetTotalObsSize() const {
        return GetVRAMObsSize() + GetRAMObsSize() + input_state_size + coverage_summary_size;
    }
};

// =============================================================================
// Training Configuration
// =============================================================================

struct TrainingConfig {
    // PPO hyperparameters
    float learning_rate = 3e-4f;
    float gamma = 0.99f;               // Discount factor
    float gae_lambda = 0.95f;          // GAE lambda
    float clip_epsilon = 0.2f;         // PPO clip range
    float entropy_coef = 0.01f;        // Entropy bonus
    float value_coef = 0.5f;           // Value loss weight
    float max_grad_norm = 0.5f;        // Gradient clipping
    
    // Batch sizes
    size_t batch_size = 64;
    size_t epochs_per_update = 4;
    size_t steps_per_update = 2048;    // Rollout length
    
    // Network architecture
    size_t hidden_dim = 512;
    size_t num_actions = 381;          // Full action space
    
    // Observation config
    ObservationConfig obs_config;
    
    // Training control
    bool use_gpu = false;              // Use MPS/CUDA if available
    uint32_t save_interval = 1000;     // Save every N updates
    std::string model_path = "explorer_policy.pt";
    std::string checkpoint_path = "explorer_checkpoint.pt";
    
    // Reward shaping
    float coverage_reward = 1.0f;      // Per new coverage bit
    float data_reward = 0.1f;          // Per new data access
    float action_reward = 0.0f;        // Per action taken
    float noop_penalty = -0.001f;      // Penalty for NO_OP
    float stall_penalty = -1.0f;       // Penalty for stall detection
};

// =============================================================================
// Experience Buffer (for PPO)
// =============================================================================

struct Experience {
    std::vector<float> observation;
    uint32_t action;
    float reward;
    float value;
    float log_prob;
    bool done;
};

class RolloutBuffer {
public:
    RolloutBuffer() = default;
    
    void Init(const TrainingConfig& config);
    void Clear();
    
    void Add(const std::vector<float>& obs, uint32_t action, 
             float reward, float value, float log_prob, bool done);
    
    void ComputeReturnsAndAdvantages(float last_value, float gamma, float gae_lambda);
    
    size_t Size() const { return experiences_.size(); }
    bool IsFull() const { return experiences_.size() >= max_size_; }
    
    // Get batch for training
    struct Batch {
        std::vector<std::vector<float>> observations;
        std::vector<uint32_t> actions;
        std::vector<float> old_log_probs;
        std::vector<float> advantages;
        std::vector<float> returns;
    };
    
    Batch GetRandomBatch(size_t batch_size);
    
    // Access computed values
    const std::vector<float>& GetAdvantages() const { return advantages_; }
    const std::vector<float>& GetReturns() const { return returns_; }

private:
    std::vector<Experience> experiences_;
    std::vector<float> advantages_;
    std::vector<float> returns_;
    size_t max_size_ = 2048;
};

// =============================================================================
// Neural Network Model (LibTorch)
// =============================================================================

#ifdef EXPLORER_ENABLE_LIBTORCH

// VRAM encoder (1D convolutions since we treat VRAM as flattened)
struct VRAMEncoderImpl : torch::nn::Module {
    VRAMEncoderImpl(size_t input_size, size_t output_dim = 256);
    torch::Tensor forward(torch::Tensor x);
    
    torch::nn::Conv1d conv1{nullptr}, conv2{nullptr}, conv3{nullptr};
    torch::nn::Linear fc{nullptr};
    size_t input_size_;
};
TORCH_MODULE(VRAMEncoder);

// RAM encoder (simple MLP)
struct RAMEncoderImpl : torch::nn::Module {
    RAMEncoderImpl(size_t input_size, size_t output_dim = 128);
    torch::Tensor forward(torch::Tensor x);
    
    torch::nn::Linear fc1{nullptr}, fc2{nullptr};
};
TORCH_MODULE(RAMEncoder);

// Actor-Critic network
struct PolicyValueNetImpl : torch::nn::Module {
    PolicyValueNetImpl(const TrainingConfig& config);
    
    // Forward pass returns (action_logits, state_value)
    std::tuple<torch::Tensor, torch::Tensor> forward(torch::Tensor obs);
    
    // Convenience methods
    std::tuple<int64_t, torch::Tensor, torch::Tensor> GetAction(torch::Tensor obs, bool deterministic = false);
    std::tuple<torch::Tensor, torch::Tensor, torch::Tensor> EvaluateActions(
        torch::Tensor obs, torch::Tensor actions);
    
private:
    VRAMEncoder vram_encoder_{nullptr};
    RAMEncoder ram_encoder_{nullptr};
    torch::nn::Linear input_encoder_{nullptr};
    torch::nn::Linear coverage_encoder_{nullptr};
    
    torch::nn::Linear shared1_{nullptr}, shared2_{nullptr};
    torch::nn::Linear policy_head_{nullptr};
    torch::nn::Linear value_head_{nullptr};
    
    ObservationConfig obs_config_;
};
TORCH_MODULE(PolicyValueNet);

#endif // EXPLORER_ENABLE_LIBTORCH

// =============================================================================
// PPO Trainer
// =============================================================================

class PPOTrainer {
public:
    PPOTrainer();
    ~PPOTrainer();
    
    // Lifecycle
    bool Init(const TrainingConfig& config);
    void Shutdown();
    bool IsInitialized() const { return initialized_; }
    
    // Model management
    bool SaveModel(const std::string& path);
    bool LoadModel(const std::string& path);
    bool SaveCheckpoint(const std::string& path);
    bool LoadCheckpoint(const std::string& path);
    
    // Build observation from current emulator state
    std::vector<float> BuildObservation();
    
    // Get action from policy
    uint32_t SelectAction(const std::vector<float>& obs, float* out_log_prob = nullptr, 
                          float* out_value = nullptr);
    
    // Training step (called after collecting rollout)
    struct TrainingStats {
        float policy_loss;
        float value_loss;
        float entropy;
        float kl_divergence;
    };
    
    // Add experience to buffer
    void AddExperience(const std::vector<float>& obs, uint32_t action,
                       float reward, float value, float log_prob, bool done);
    
    // Perform PPO update when buffer is full
    TrainingStats Update();
    
    // Check if ready for update
    bool ReadyForUpdate() const;
    
    // Statistics
    uint64_t GetTotalSteps() const { return total_steps_; }
    uint64_t GetTotalUpdates() const { return total_updates_; }
    float GetAverageReward() const;
    float GetAverageCoverage() const;
    
    // Configuration
    const TrainingConfig& GetConfig() const { return config_; }

private:
    TrainingConfig config_;
    bool initialized_ = false;
    
    RolloutBuffer buffer_;
    
    uint64_t total_steps_ = 0;
    uint64_t total_updates_ = 0;
    
    // Reward tracking
    std::deque<float> recent_rewards_;
    std::deque<float> recent_coverages_;
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    PolicyValueNet model_{nullptr};
    std::shared_ptr<torch::optim::Adam> optimizer_;
    torch::Device device_{torch::kCPU};
#endif
};

// =============================================================================
// Global Training Control
// =============================================================================

// Get global trainer instance
PPOTrainer& GetTrainer();

// Training mode (vs inference-only mode)
void SetTrainingMode(bool training);
bool IsTrainingMode();

// Convenience: step the training loop
// Called from the main emulator tick
void TrainingTick(uint32_t coverage_gain, uint32_t data_gain, bool stalled, bool program_exit);

// Start/stop training
void StartTraining(const TrainingConfig& config);
void StopTraining();
bool IsTraining();

} // namespace Explorer

#endif // EXPLORER_TRAINING_H
