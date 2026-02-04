/*
 *  Explorer Neural Network Policy
 *  Implementation
 */

#include "explorer_nn.h"
#include <iostream>
#include <cmath>
#include <random>

namespace Explorer {

// Global trainer instance
std::unique_ptr<PPOTrainer> g_trainer;

// =============================================================================
// Action Decoding
// =============================================================================

InputAction DecodeAction(int action_idx) {
    InputAction action;
    
    static const uint16_t key_scancodes[] = {
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,   // 0-3: arrows
        KEY_W, KEY_S, KEY_A, KEY_D,              // 4-7: WASD
        KEY_SPACE, KEY_ENTER, KEY_ESC,           // 8-10: common keys
        KEY_1, KEY_Q, KEY_E, KEY_F, KEY_TAB      // 11-15: other keys
    };
    
    if (action_idx >= 0 && action_idx < 16) {
        // Keyboard action
        action.type = InputAction::KeyDown;
        action.scancode = key_scancodes[action_idx];
    } else if (action_idx >= 16 && action_idx < 20) {
        // Mouse movement
        action.type = InputAction::MouseMove;
        switch (action_idx - 16) {
            case 0: action.mouse_dy = -10; break;  // Up
            case 1: action.mouse_dy = 10; break;   // Down
            case 2: action.mouse_dx = -10; break;  // Left
            case 3: action.mouse_dx = 10; break;   // Right
        }
    } else if (action_idx >= 20 && action_idx < 23) {
        // Mouse buttons
        action.type = InputAction::MouseButton;
        action.mouse_buttons = 1 << (action_idx - 20);
    } else {
        // No-op
        action.type = InputAction::None;
    }
    
    return action;
}

// =============================================================================
// Rollout Buffer
// =============================================================================

void RolloutBuffer::Add(const RolloutSample& sample) {
    samples_.push_back(sample);
}

void RolloutBuffer::Clear() {
    samples_.clear();
    advantages_.clear();
    returns_.clear();
}

void RolloutBuffer::ComputeAdvantages(float gamma, float gae_lambda) {
    size_t n = samples_.size();
    if (n == 0) return;
    
    advantages_.resize(n);
    returns_.resize(n);
    
    float last_gae = 0.0f;
    float last_value = 0.0f;
    
    for (int i = n - 1; i >= 0; --i) {
        float next_value = (i + 1 < (int)n) ? samples_[i + 1].value : last_value;
        float next_non_terminal = samples_[i].done ? 0.0f : 1.0f;
        
        float delta = samples_[i].reward + gamma * next_value * next_non_terminal - samples_[i].value;
        last_gae = delta + gamma * gae_lambda * next_non_terminal * last_gae;
        
        advantages_[i] = last_gae;
        returns_[i] = last_gae + samples_[i].value;
    }
}

RolloutBuffer::Batch RolloutBuffer::GetBatch() const {
    Batch batch;
    for (size_t i = 0; i < samples_.size(); i++) {
        batch.obs.push_back(samples_[i].obs);
        batch.actions.push_back(samples_[i].action);
        batch.old_log_probs.push_back(samples_[i].log_prob);
        
        if (i < advantages_.size()) {
            batch.advantages.push_back(advantages_[i]);
            batch.returns.push_back(returns_[i]);
        }
    }
    return batch;
}

// =============================================================================
// PPO Trainer
// =============================================================================

PPOTrainer::PPOTrainer() {}

PPOTrainer::~PPOTrainer() {
#ifdef EXPLORER_USE_LIBTORCH
    optimizer_.reset();
    network_.reset();
#endif
}

bool PPOTrainer::Initialize(const std::string& model_path) {
#ifdef EXPLORER_USE_LIBTORCH
    network_ = std::make_shared<PolicyValueNet>();
    
    if (!model_path.empty()) {
        if (!LoadModel(model_path)) {
            std::cerr << "Warning: Could not load model from " << model_path << std::endl;
        }
    }
    
    optimizer_ = std::make_unique<torch::optim::Adam>(
        network_->parameters(),
        torch::optim::AdamOptions(config.learning_rate)
    );
    
    return true;
#else
    (void)model_path;
    std::cerr << "LibTorch not available - using random policy" << std::endl;
    return true;
#endif
}

int PPOTrainer::Step(const Observation& obs, float reward, bool done) {
    // Store previous transition
    if (!first_step_) {
        RolloutSample sample;
        sample.obs = last_obs_;
        sample.action = last_action_;
        sample.log_prob = last_log_prob_;
        sample.value = last_value_;
        sample.reward = reward;
        sample.done = done;
        rollout_.Add(sample);
    }
    first_step_ = false;
    
    // Get new action
    int action = 0;
    float log_prob = 0.0f;
    float value = 0.0f;
    
#ifdef EXPLORER_USE_LIBTORCH
    if (network_) {
        auto [a, lp, v] = network_->act(obs);
        action = a;
        log_prob = lp;
        value = v;
    } else {
        // Random action fallback
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, NUM_ACTIONS - 1);
        action = dist(rng);
    }
#else
    // Random action when LibTorch not available
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, NUM_ACTIONS - 1);
    action = dist(rng);
#endif
    
    // Save for next step
    last_obs_ = obs;
    last_action_ = action;
    last_log_prob_ = log_prob;
    last_value_ = value;
    
    // If episode done, handle terminal state
    if (done) {
        first_step_ = true;
    }
    
    return action;
}

PPOTrainer::TrainStats PPOTrainer::Train() {
    TrainStats stats{};
    
#ifdef EXPLORER_USE_LIBTORCH
    if (!network_ || rollout_.Size() == 0) {
        return stats;
    }
    
    // Compute advantages
    rollout_.ComputeAdvantages(config.gamma, config.gae_lambda);
    
    auto batch = rollout_.GetBatch();
    size_t n = batch.obs.size();
    
    // Normalize advantages
    float mean_adv = 0.0f, std_adv = 0.0f;
    for (float a : batch.advantages) mean_adv += a;
    mean_adv /= n;
    for (float a : batch.advantages) std_adv += (a - mean_adv) * (a - mean_adv);
    std_adv = std::sqrt(std_adv / n + 1e-8f);
    for (size_t i = 0; i < n; i++) {
        batch.advantages[i] = (batch.advantages[i] - mean_adv) / std_adv;
    }
    
    // Convert to tensors
    std::vector<float> obs_flat;
    for (const auto& obs : batch.obs) {
        obs_flat.insert(obs_flat.end(), obs.screen.begin(), obs.screen.end());
        obs_flat.insert(obs_flat.end(), obs.coverage.begin(), obs.coverage.end());
        obs_flat.push_back(obs.instruction_count);
        obs_flat.push_back(obs.coverage_delta);
        obs_flat.push_back(obs.stall_indicator);
    }
    
    int obs_dim = OBS_WIDTH * OBS_HEIGHT + COV_SIZE + 3;
    auto obs_tensor = torch::from_blob(obs_flat.data(), {(int64_t)n, obs_dim}).clone();
    auto actions_tensor = torch::tensor(batch.actions);
    auto old_log_probs_tensor = torch::tensor(batch.old_log_probs);
    auto advantages_tensor = torch::tensor(batch.advantages);
    auto returns_tensor = torch::tensor(batch.returns);
    
    // PPO training loop
    for (int epoch = 0; epoch < config.epochs_per_rollout; epoch++) {
        // Shuffle indices
        auto indices = torch::randperm(n);
        
        for (int start = 0; start < (int)n; start += config.minibatch_size) {
            int end = std::min(start + config.minibatch_size, (int)n);
            auto mb_indices = indices.slice(0, start, end);
            
            auto mb_obs = obs_tensor.index_select(0, mb_indices);
            auto mb_actions = actions_tensor.index_select(0, mb_indices);
            auto mb_old_log_probs = old_log_probs_tensor.index_select(0, mb_indices);
            auto mb_advantages = advantages_tensor.index_select(0, mb_indices);
            auto mb_returns = returns_tensor.index_select(0, mb_indices);
            
            // Evaluate actions
            auto [log_probs, values, entropy] = network_->evaluate(mb_obs, mb_actions);
            
            // Policy loss (PPO clipped objective)
            auto ratio = (log_probs - mb_old_log_probs).exp();
            auto surr1 = ratio * mb_advantages;
            auto surr2 = torch::clamp(ratio, 1.0f - config.clip_ratio, 1.0f + config.clip_ratio) * mb_advantages;
            auto policy_loss = -torch::min(surr1, surr2).mean();
            
            // Value loss
            auto value_loss = torch::mse_loss(values.squeeze(), mb_returns);
            
            // Combined loss
            auto loss = policy_loss + config.value_coef * value_loss - config.entropy_coef * entropy.mean();
            
            // Optimize
            optimizer_->zero_grad();
            loss.backward();
            torch::nn::utils::clip_grad_norm_(network_->parameters(), 0.5);
            optimizer_->step();
            
            // Accumulate stats
            stats.policy_loss += policy_loss.item<float>();
            stats.value_loss += value_loss.item<float>();
            stats.entropy += entropy.mean().item<float>();
            
            auto approx_kl = ((ratio - 1) - ratio.log()).mean();
            stats.approx_kl += approx_kl.item<float>();
        }
    }
    
    // Average stats
    int num_updates = config.epochs_per_rollout * ((n + config.minibatch_size - 1) / config.minibatch_size);
    stats.policy_loss /= num_updates;
    stats.value_loss /= num_updates;
    stats.entropy /= num_updates;
    stats.approx_kl /= num_updates;
    
    rollout_.Clear();
#endif
    
    return stats;
}

bool PPOTrainer::SaveModel(const std::string& path) {
#ifdef EXPLORER_USE_LIBTORCH
    if (!network_) return false;
    try {
        torch::save(network_, path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save model: " << e.what() << std::endl;
        return false;
    }
#else
    (void)path;
    return false;
#endif
}

bool PPOTrainer::LoadModel(const std::string& path) {
#ifdef EXPLORER_USE_LIBTORCH
    if (!network_) return false;
    try {
        torch::load(network_, path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load model: " << e.what() << std::endl;
        return false;
    }
#else
    (void)path;
    return false;
#endif
}

// =============================================================================
// LibTorch Network Implementation
// =============================================================================

#ifdef EXPLORER_USE_LIBTORCH

PolicyValueNet::PolicyValueNet() {
    int input_dim = OBS_WIDTH * OBS_HEIGHT + COV_SIZE + 3;
    int hidden_dim = 256;
    
    features_ = torch::nn::Sequential(
        torch::nn::Linear(input_dim, hidden_dim),
        torch::nn::ReLU(),
        torch::nn::Linear(hidden_dim, hidden_dim),
        torch::nn::ReLU()
    );
    register_module("features", features_);
    
    policy_head_ = torch::nn::Linear(hidden_dim, NUM_ACTIONS);
    register_module("policy_head", policy_head_);
    
    value_head_ = torch::nn::Linear(hidden_dim, 1);
    register_module("value_head", value_head_);
}

std::tuple<torch::Tensor, torch::Tensor> PolicyValueNet::forward(torch::Tensor obs) {
    auto features = features_->forward(obs);
    auto policy_logits = policy_head_->forward(features);
    auto value = value_head_->forward(features);
    return {policy_logits, value};
}

std::tuple<int, float, float> PolicyValueNet::act(const Observation& obs) {
    torch::NoGradGuard no_grad;
    
    // Flatten observation
    std::vector<float> obs_flat;
    obs_flat.insert(obs_flat.end(), obs.screen.begin(), obs.screen.end());
    obs_flat.insert(obs_flat.end(), obs.coverage.begin(), obs.coverage.end());
    obs_flat.push_back(obs.instruction_count);
    obs_flat.push_back(obs.coverage_delta);
    obs_flat.push_back(obs.stall_indicator);
    
    auto obs_tensor = torch::from_blob(obs_flat.data(), {1, (int64_t)obs_flat.size()}).clone();
    
    auto [logits, value] = forward(obs_tensor);
    
    // Sample action
    auto probs = torch::softmax(logits, 1);
    auto action_tensor = torch::multinomial(probs, 1);
    int action = action_tensor.item<int>();
    
    // Log probability
    auto log_probs = torch::log_softmax(logits, 1);
    float log_prob = log_probs[0][action].item<float>();
    
    return {action, log_prob, value[0][0].item<float>()};
}

std::tuple<torch::Tensor, torch::Tensor, torch::Tensor>
PolicyValueNet::evaluate(torch::Tensor obs, torch::Tensor actions) {
    auto [logits, values] = forward(obs);
    
    auto log_probs = torch::log_softmax(logits, 1);
    auto action_log_probs = log_probs.gather(1, actions.unsqueeze(1)).squeeze(1);
    
    auto probs = torch::softmax(logits, 1);
    auto entropy = -(probs * log_probs).sum(1);
    
    return {action_log_probs, values, entropy};
}

#endif // EXPLORER_USE_LIBTORCH

// =============================================================================
// Global Interface
// =============================================================================

bool InitializeNNPolicy(const std::string& model_path) {
    g_trainer = std::make_unique<PPOTrainer>();
    return g_trainer->Initialize(model_path);
}

void ShutdownNNPolicy() {
    g_trainer.reset();
}

Observation BuildObservation() {
    Observation obs;
    
    // Initialize with proper sizes
    obs.screen.resize(OBS_WIDTH * OBS_HEIGHT, 0.0f);
    obs.coverage.resize(COV_SIZE, 0.0f);
    obs.instruction_count = 0.0f;
    obs.coverage_delta = 0.0f;
    obs.stall_indicator = 0.0f;
    
    // TODO: Fill from actual DOSBox state
    // - Read VRAM and downsample
    // - Read coverage map
    // - Compute delta and stall
    
    // Get instruction count (normalized)
    auto stats = GetStats();
    obs.instruction_count = static_cast<float>(stats.instructions) / 1e8f;
    
    // Coverage features
    if (g_run_coverage) {
        for (size_t i = 0; i < COV_SIZE && i < g_config.coverage_size; i++) {
            obs.coverage[i] = static_cast<float>(g_run_coverage[i]) / 255.0f;
        }
    }
    
    return obs;
}

void ExecutePolicyStep() {
    if (!g_trainer || !g_initialized.load()) return;
    
    // Build observation
    Observation obs = BuildObservation();
    
    // Compute reward (coverage-based)
    static uint64_t last_coverage = 0;
    uint64_t current_coverage = g_coverage_count.load();
    float reward = static_cast<float>(current_coverage - last_coverage) * 0.1f;
    last_coverage = current_coverage;
    
    // Step policy
    int action = g_trainer->Step(obs, reward, false);
    
    // Decode and queue action
    InputAction input = DecodeAction(action);
    QueueInput(input);
    
    // Train if rollout is full
    if (g_trainer->RolloutSize() >= g_trainer->config.rollout_length) {
        auto stats = g_trainer->Train();
        std::cout << "Training: policy_loss=" << stats.policy_loss 
                  << " value_loss=" << stats.value_loss
                  << " entropy=" << stats.entropy << std::endl;
    }
}

} // namespace Explorer
