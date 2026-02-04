/*
 *  Explorer Neural Network Policy
 *  LibTorch-based PPO agent for automated game exploration
 */

#ifndef DOSBOX_EXPLORER_NN_H
#define DOSBOX_EXPLORER_NN_H

#include "explorer.h"
#include <memory>
#include <string>
#include <vector>

#ifdef EXPLORER_USE_LIBTORCH
#include <torch/torch.h>
#include <torch/script.h>
#endif

namespace Explorer {

// =============================================================================
// Observation Space
// =============================================================================

// VRAM dimensions for VGA mode 13h (320x200x8)
constexpr int VRAM_WIDTH = 320;
constexpr int VRAM_HEIGHT = 200;
constexpr int VRAM_SIZE = VRAM_WIDTH * VRAM_HEIGHT;

// Downscaled observation dimensions (for efficiency)
constexpr int OBS_WIDTH = 80;   // 320/4
constexpr int OBS_HEIGHT = 50;  // 200/4

// Coverage map dimensions
constexpr int COV_SIZE = 1024;  // Compressed coverage representation

// Observation structure
struct Observation {
    std::vector<float> screen;      // Flattened downscaled screen
    std::vector<float> coverage;    // Coverage map features
    float instruction_count;        // Normalized instruction count
    float coverage_delta;           // Recent coverage change
    float stall_indicator;          // 1.0 if stalled, 0.0 otherwise
};

// =============================================================================
// Action Space
// =============================================================================

// Discrete action space
// 0-15: Common keyboard keys (WASD, arrows, space, enter, etc.)
// 16-19: Mouse movement (up/down/left/right)
// 20-22: Mouse buttons (left/right/middle)
// 23: No-op
constexpr int NUM_ACTIONS = 24;

// Map action index to InputAction
InputAction DecodeAction(int action_idx);

// Common scancodes
constexpr uint16_t KEY_ESC = 0x01;
constexpr uint16_t KEY_1 = 0x02;
constexpr uint16_t KEY_SPACE = 0x39;
constexpr uint16_t KEY_ENTER = 0x1C;
constexpr uint16_t KEY_UP = 0x48;
constexpr uint16_t KEY_DOWN = 0x50;
constexpr uint16_t KEY_LEFT = 0x4B;
constexpr uint16_t KEY_RIGHT = 0x4D;
constexpr uint16_t KEY_W = 0x11;
constexpr uint16_t KEY_A = 0x1E;
constexpr uint16_t KEY_S = 0x1F;
constexpr uint16_t KEY_D = 0x20;
constexpr uint16_t KEY_Q = 0x10;
constexpr uint16_t KEY_E = 0x12;
constexpr uint16_t KEY_F = 0x21;
constexpr uint16_t KEY_TAB = 0x0F;

// =============================================================================
// Rollout Buffer for PPO
// =============================================================================

struct RolloutSample {
    Observation obs;
    int action;
    float log_prob;
    float value;
    float reward;
    bool done;
};

class RolloutBuffer {
public:
    void Add(const RolloutSample& sample);
    void Clear();
    size_t Size() const { return samples_.size(); }
    
    // Compute advantages using GAE
    void ComputeAdvantages(float gamma = 0.99f, float gae_lambda = 0.95f);
    
    // Get batch for training
    struct Batch {
        std::vector<Observation> obs;
        std::vector<int> actions;
        std::vector<float> old_log_probs;
        std::vector<float> advantages;
        std::vector<float> returns;
    };
    Batch GetBatch() const;
    
private:
    std::vector<RolloutSample> samples_;
    std::vector<float> advantages_;
    std::vector<float> returns_;
};

// =============================================================================
// Policy Network (using LibTorch)
// =============================================================================

#ifdef EXPLORER_USE_LIBTORCH

// Actor-Critic network for PPO
class PolicyValueNet : public torch::nn::Module {
public:
    PolicyValueNet();
    
    // Forward pass returns (action_logits, value)
    std::tuple<torch::Tensor, torch::Tensor> forward(torch::Tensor obs);
    
    // Sample action from policy
    std::tuple<int, float, float> act(const Observation& obs);
    
    // Evaluate actions for training
    std::tuple<torch::Tensor, torch::Tensor, torch::Tensor> 
    evaluate(torch::Tensor obs, torch::Tensor actions);
    
private:
    // Shared feature extractor
    torch::nn::Sequential features_{nullptr};
    
    // Policy head
    torch::nn::Linear policy_head_{nullptr};
    
    // Value head
    torch::nn::Linear value_head_{nullptr};
};

#endif // EXPLORER_USE_LIBTORCH

// =============================================================================
// PPO Training Interface
// =============================================================================

class PPOTrainer {
public:
    PPOTrainer();
    ~PPOTrainer();
    
    // Initialize the trainer
    bool Initialize(const std::string& model_path = "");
    
    // Step the policy (returns action)
    int Step(const Observation& obs, float reward, bool done);
    
    // Train on collected rollout
    struct TrainStats {
        float policy_loss;
        float value_loss;
        float entropy;
        float approx_kl;
    };
    TrainStats Train();
    
    // Save/load model
    bool SaveModel(const std::string& path);
    bool LoadModel(const std::string& path);
    
    // Get current rollout buffer size
    size_t RolloutSize() const { return rollout_.Size(); }
    
    // Configuration
    struct Config {
        float learning_rate = 3e-4f;
        float gamma = 0.99f;
        float gae_lambda = 0.95f;
        float clip_ratio = 0.2f;
        float value_coef = 0.5f;
        float entropy_coef = 0.01f;
        int epochs_per_rollout = 4;
        int minibatch_size = 64;
        int rollout_length = 2048;
    };
    Config config;
    
private:
    RolloutBuffer rollout_;
    
#ifdef EXPLORER_USE_LIBTORCH
    std::shared_ptr<PolicyValueNet> network_;
    std::unique_ptr<torch::optim::Adam> optimizer_;
#endif
    
    Observation last_obs_;
    int last_action_ = 0;
    float last_log_prob_ = 0.0f;
    float last_value_ = 0.0f;
    bool first_step_ = true;
};

// =============================================================================
// Global Policy Instance
// =============================================================================

extern std::unique_ptr<PPOTrainer> g_trainer;

// Initialize the NN policy
bool InitializeNNPolicy(const std::string& model_path = "");

// Shutdown the NN policy
void ShutdownNNPolicy();

// Build observation from current state
Observation BuildObservation();

// Execute policy step and queue input
void ExecutePolicyStep();

} // namespace Explorer

#endif // DOSBOX_EXPLORER_NN_H
