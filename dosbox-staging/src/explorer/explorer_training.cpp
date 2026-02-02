// SPDX-License-Identifier: MIT
// Explorer PPO training implementation

#include "explorer_training.h"
#include "explorer.h"
#include "explorer_memory.h"
#include "explorer_input.h"
#include "explorer_cpu.h"
#include "explorer_data.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <fstream>

namespace Explorer {

// =============================================================================
// RolloutBuffer Implementation
// =============================================================================

void RolloutBuffer::Init(const TrainingConfig& config) {
    max_size_ = config.steps_per_update;
    Clear();
}

void RolloutBuffer::Clear() {
    experiences_.clear();
    advantages_.clear();
    returns_.clear();
}

void RolloutBuffer::Add(const std::vector<float>& obs, uint32_t action,
                        float reward, float value, float log_prob, bool done) {
    experiences_.push_back({obs, action, reward, value, log_prob, done});
}

void RolloutBuffer::ComputeReturnsAndAdvantages(float last_value, float gamma, float gae_lambda) {
    size_t n = experiences_.size();
    advantages_.resize(n);
    returns_.resize(n);
    
    float last_gae = 0.0f;
    
    for (int t = static_cast<int>(n) - 1; t >= 0; t--) {
        float next_value = (t == static_cast<int>(n) - 1) ? last_value : experiences_[t + 1].value;
        float next_non_terminal = (t == static_cast<int>(n) - 1) ? 
            (experiences_[t].done ? 0.0f : 1.0f) : 
            (experiences_[t + 1].done ? 0.0f : 1.0f);
        
        float delta = experiences_[t].reward + gamma * next_value * next_non_terminal - experiences_[t].value;
        advantages_[t] = last_gae = delta + gamma * gae_lambda * next_non_terminal * last_gae;
        returns_[t] = advantages_[t] + experiences_[t].value;
    }
}

RolloutBuffer::Batch RolloutBuffer::GetRandomBatch(size_t batch_size) {
    Batch batch;
    
    // Create shuffled indices
    std::vector<size_t> indices(experiences_.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    static std::mt19937 rng(42);
    std::shuffle(indices.begin(), indices.end(), rng);
    
    // Take first batch_size
    size_t actual_size = std::min(batch_size, indices.size());
    
    for (size_t i = 0; i < actual_size; i++) {
        size_t idx = indices[i];
        batch.observations.push_back(experiences_[idx].observation);
        batch.actions.push_back(experiences_[idx].action);
        batch.old_log_probs.push_back(experiences_[idx].log_prob);
        batch.advantages.push_back(advantages_[idx]);
        batch.returns.push_back(returns_[idx]);
    }
    
    return batch;
}

// =============================================================================
// Neural Network Implementation (LibTorch)
// =============================================================================

#ifdef EXPLORER_ENABLE_LIBTORCH

VRAMEncoderImpl::VRAMEncoderImpl(size_t input_size, size_t output_dim)
    : input_size_(input_size) {
    // 1D convolutions over flattened VRAM
    // Input: (batch, 1, vram_size)
    conv1 = register_module("conv1", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(1, 32, 8).stride(4).padding(2)));
    conv2 = register_module("conv2", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(32, 64, 4).stride(2).padding(1)));
    conv3 = register_module("conv3", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(64, 64, 3).stride(1).padding(1)));
    
    // Calculate output size after convolutions
    size_t size = input_size;
    size = (size + 2 * 2 - 8) / 4 + 1;  // conv1
    size = (size + 2 * 1 - 4) / 2 + 1;  // conv2
    size = (size + 2 * 1 - 3) / 1 + 1;  // conv3
    
    fc = register_module("fc", torch::nn::Linear(64 * size, output_dim));
}

torch::Tensor VRAMEncoderImpl::forward(torch::Tensor x) {
    // x: (batch, vram_size)
    x = x.unsqueeze(1);  // (batch, 1, vram_size)
    x = torch::relu(conv1->forward(x));
    x = torch::relu(conv2->forward(x));
    x = torch::relu(conv3->forward(x));
    x = x.flatten(1);    // (batch, features)
    return fc->forward(x);
}

RAMEncoderImpl::RAMEncoderImpl(size_t input_size, size_t output_dim) {
    fc1 = register_module("fc1", torch::nn::Linear(input_size, 256));
    fc2 = register_module("fc2", torch::nn::Linear(256, output_dim));
}

torch::Tensor RAMEncoderImpl::forward(torch::Tensor x) {
    x = torch::relu(fc1->forward(x));
    return torch::relu(fc2->forward(x));
}

PolicyValueNetImpl::PolicyValueNetImpl(const TrainingConfig& config) 
    : obs_config_(config.obs_config) {
    
    size_t vram_obs = obs_config_.GetVRAMObsSize();
    size_t ram_obs = obs_config_.GetRAMObsSize();
    
    vram_encoder_ = register_module("vram_encoder", VRAMEncoder(vram_obs, 256));
    ram_encoder_ = register_module("ram_encoder", RAMEncoder(ram_obs, 128));
    input_encoder_ = register_module("input_encoder", 
        torch::nn::Linear(obs_config_.input_state_size, 32));
    coverage_encoder_ = register_module("coverage_encoder",
        torch::nn::Linear(obs_config_.coverage_summary_size, 64));
    
    // Combined: 256 + 128 + 32 + 64 = 480
    size_t combined_dim = 256 + 128 + 32 + 64;
    
    shared1_ = register_module("shared1", torch::nn::Linear(combined_dim, config.hidden_dim));
    shared2_ = register_module("shared2", torch::nn::Linear(config.hidden_dim, config.hidden_dim / 2));
    
    policy_head_ = register_module("policy_head", 
        torch::nn::Linear(config.hidden_dim / 2, config.num_actions));
    value_head_ = register_module("value_head", 
        torch::nn::Linear(config.hidden_dim / 2, 1));
}

std::tuple<torch::Tensor, torch::Tensor> PolicyValueNetImpl::forward(torch::Tensor obs) {
    // Split observation into components
    size_t vram_size = obs_config_.GetVRAMObsSize();
    size_t ram_size = obs_config_.GetRAMObsSize();
    size_t input_size = obs_config_.input_state_size;
    size_t coverage_size = obs_config_.coverage_summary_size;
    
    auto vram = obs.narrow(1, 0, vram_size);
    auto ram = obs.narrow(1, vram_size, ram_size);
    auto input_state = obs.narrow(1, vram_size + ram_size, input_size);
    auto coverage = obs.narrow(1, vram_size + ram_size + input_size, coverage_size);
    
    // Encode each component
    auto vram_features = vram_encoder_->forward(vram);
    auto ram_features = ram_encoder_->forward(ram);
    auto input_features = torch::relu(input_encoder_->forward(input_state));
    auto coverage_features = torch::relu(coverage_encoder_->forward(coverage));
    
    // Combine
    auto combined = torch::cat({vram_features, ram_features, input_features, coverage_features}, 1);
    
    // Shared layers
    auto shared = torch::relu(shared1_->forward(combined));
    shared = torch::relu(shared2_->forward(shared));
    
    // Heads
    auto logits = policy_head_->forward(shared);
    auto value = value_head_->forward(shared).squeeze(-1);
    
    return {logits, value};
}

std::tuple<int64_t, torch::Tensor, torch::Tensor> PolicyValueNetImpl::GetAction(
    torch::Tensor obs, bool deterministic) {
    
    auto [logits, value] = forward(obs);
    
    if (deterministic) {
        auto action = logits.argmax(-1);
        auto log_prob = torch::zeros({1});
        return {action.item<int64_t>(), log_prob, value};
    } else {
        // Sample from categorical distribution
        auto probs = torch::softmax(logits, -1);
        auto dist = torch::multinomial(probs, 1);
        auto action = dist.squeeze(-1);
        
        // Compute log prob
        auto log_probs = torch::log_softmax(logits, -1);
        auto log_prob = log_probs.gather(-1, action.unsqueeze(-1)).squeeze(-1);
        
        return {action.item<int64_t>(), log_prob, value};
    }
}

std::tuple<torch::Tensor, torch::Tensor, torch::Tensor> PolicyValueNetImpl::EvaluateActions(
    torch::Tensor obs, torch::Tensor actions) {
    
    auto [logits, values] = forward(obs);
    
    // Log probs
    auto log_probs_all = torch::log_softmax(logits, -1);
    auto log_probs = log_probs_all.gather(-1, actions.unsqueeze(-1)).squeeze(-1);
    
    // Entropy
    auto probs = torch::softmax(logits, -1);
    auto entropy = -(probs * log_probs_all).sum(-1);
    
    return {log_probs, values, entropy};
}

#endif // EXPLORER_ENABLE_LIBTORCH

// =============================================================================
// PPOTrainer Implementation
// =============================================================================

PPOTrainer::PPOTrainer() = default;
PPOTrainer::~PPOTrainer() { Shutdown(); }

bool PPOTrainer::Init(const TrainingConfig& config) {
    config_ = config;
    buffer_.Init(config);
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    // Select device
    if (config.use_gpu) {
        if (torch::cuda::is_available()) {
            device_ = torch::kCUDA;
        } else if (torch::hasMPS()) {
            device_ = torch::kMPS;
        } else {
            device_ = torch::kCPU;
        }
    } else {
        device_ = torch::kCPU;
    }
    
    // Create model
    model_ = PolicyValueNet(config);
    model_->to(device_);
    
    // Create optimizer
    optimizer_ = std::make_shared<torch::optim::Adam>(
        model_->parameters(),
        torch::optim::AdamOptions(config.learning_rate)
    );
    
    initialized_ = true;
    return true;
#else
    // Without LibTorch, we can't train
    return false;
#endif
}

void PPOTrainer::Shutdown() {
#ifdef EXPLORER_ENABLE_LIBTORCH
    optimizer_.reset();
    model_ = nullptr;
#endif
    initialized_ = false;
}

bool PPOTrainer::SaveModel([[maybe_unused]] const std::string& path) {
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (!initialized_) return false;
    
    try {
        // Save as TorchScript for inference
        model_->eval();
        
        // Create dummy input
        size_t obs_size = config_.obs_config.GetTotalObsSize();
        auto dummy = torch::zeros({1, static_cast<long>(obs_size)}).to(device_);
        
        auto traced = torch::jit::trace(model_, dummy);
        traced.save(path);
        
        model_->train();
        return true;
    } catch (const c10::Error&) {
        return false;
    }
#else
    return false;
#endif
}

bool PPOTrainer::LoadModel([[maybe_unused]] const std::string& path) {
#ifdef EXPLORER_ENABLE_LIBTORCH
    // For training, we use native PyTorch modules, not TorchScript
    // This would require serializing/deserializing state_dict
    // For now, just return false - use checkpoint instead
    return false;
#else
    return false;
#endif
}

bool PPOTrainer::SaveCheckpoint([[maybe_unused]] const std::string& path) {
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (!initialized_) return false;
    
    try {
        torch::serialize::OutputArchive archive;
        model_->save(archive);
        archive.save_to(path + ".model");
        
        torch::serialize::OutputArchive opt_archive;
        optimizer_->save(opt_archive);
        opt_archive.save_to(path + ".optimizer");
        
        // Save training stats
        std::ofstream stats_file(path + ".stats");
        stats_file << total_steps_ << " " << total_updates_ << std::endl;
        
        return true;
    } catch (const c10::Error&) {
        return false;
    }
#else
    return false;
#endif
}

bool PPOTrainer::LoadCheckpoint([[maybe_unused]] const std::string& path) {
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (!initialized_) return false;
    
    try {
        torch::serialize::InputArchive archive;
        archive.load_from(path + ".model");
        model_->load(archive);
        
        torch::serialize::InputArchive opt_archive;
        opt_archive.load_from(path + ".optimizer");
        optimizer_->load(opt_archive);
        
        // Load training stats
        std::ifstream stats_file(path + ".stats");
        if (stats_file.good()) {
            stats_file >> total_steps_ >> total_updates_;
        }
        
        return true;
    } catch (const c10::Error&) {
        return false;
    }
#else
    return false;
#endif
}

std::vector<float> PPOTrainer::BuildObservation() {
    const auto& obs_cfg = config_.obs_config;
    std::vector<float> obs(obs_cfg.GetTotalObsSize(), 0.0f);
    
    size_t pos = 0;
    
    // 1. VRAM (downsampled)
    VRAMInfo vram_info = GetVRAMInfo();
    if (vram_info.linear && vram_info.size > 0) {
        size_t src_step = obs_cfg.vram_downsample;
        size_t dst_count = std::min(obs_cfg.GetVRAMObsSize(), vram_info.size / src_step);
        
        for (size_t i = 0; i < dst_count; i++) {
            obs[pos++] = static_cast<float>(vram_info.linear[i * src_step]) / 255.0f;
        }
    }
    // Pad if needed
    while (pos < obs_cfg.GetVRAMObsSize()) {
        obs[pos++] = 0.0f;
    }
    
    // 2. RAM window around CS:IP
    CPURegisters cpu = GetRegisters();
    uint32_t window_start = cpu.cs_base + cpu.eip;
    if (window_start > obs_cfg.ram_window_size / 2) {
        window_start -= obs_cfg.ram_window_size / 2;
    } else {
        window_start = 0;
    }
    
    uint8_t* mem_base = GetMemoryBase();
    size_t mem_size = GetMemorySize();
    size_t src_step = obs_cfg.ram_downsample;
    
    for (size_t i = 0; i < obs_cfg.GetRAMObsSize(); i++) {
        uint32_t addr = window_start + i * src_step;
        if (addr < mem_size && mem_base) {
            obs[pos++] = static_cast<float>(mem_base[addr]) / 255.0f;
        } else {
            obs[pos++] = 0.0f;
        }
    }
    
    // 3. Input state
    // Recent actions (as one-hot buckets), mouse position
    ActionSpaceInfo action_info = GetActionSpaceInfo();
    // For now, just pad with zeros - we'd track recent actions in practice
    for (size_t i = 0; i < obs_cfg.input_state_size; i++) {
        obs[pos++] = 0.0f;
    }
    
    // 4. Coverage summary
    const uint8_t* coverage_map = GetInstrumenter().GetCoverageMap();
    size_t coverage_map_size = GetInstrumenter().GetCoverageMapSize();
    
    if (coverage_map && coverage_map_size > 0) {
        size_t buckets = obs_cfg.coverage_summary_size;
        size_t per_bucket = coverage_map_size / buckets;
        
        for (size_t b = 0; b < buckets; b++) {
            uint32_t set_bits = 0;
            for (size_t i = 0; i < per_bucket; i++) {
                set_bits += __builtin_popcount(coverage_map[b * per_bucket + i]);
            }
            obs[pos++] = static_cast<float>(set_bits) / (per_bucket * 8.0f);
        }
    } else {
        for (size_t i = 0; i < obs_cfg.coverage_summary_size; i++) {
            obs[pos++] = 0.0f;
        }
    }
    
    return obs;
}

uint32_t PPOTrainer::SelectAction([[maybe_unused]] const std::vector<float>& obs,
                                  [[maybe_unused]] float* out_log_prob,
                                  [[maybe_unused]] float* out_value) {
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (!initialized_) return 379; // NO_OP
    
    torch::NoGradGuard no_grad;
    
    auto obs_tensor = torch::from_blob(
        const_cast<float*>(obs.data()),
        {1, static_cast<long>(obs.size())},
        torch::kFloat32
    ).clone().to(device_);
    
    auto [action, log_prob, value] = model_->GetAction(obs_tensor, false);
    
    if (out_log_prob) *out_log_prob = log_prob.item<float>();
    if (out_value) *out_value = value.item<float>();
    
    return static_cast<uint32_t>(action);
#else
    // Random action fallback
    static std::mt19937 rng(42);
    return rng() % config_.num_actions;
#endif
}

void PPOTrainer::AddExperience(const std::vector<float>& obs, uint32_t action,
                               float reward, float value, float log_prob, bool done) {
    buffer_.Add(obs, action, reward, value, log_prob, done);
    total_steps_++;
    
    // Track recent rewards
    recent_rewards_.push_back(reward);
    if (recent_rewards_.size() > 1000) {
        recent_rewards_.pop_front();
    }
}

bool PPOTrainer::ReadyForUpdate() const {
    return buffer_.IsFull();
}

PPOTrainer::TrainingStats PPOTrainer::Update() {
    TrainingStats stats = {0.0f, 0.0f, 0.0f, 0.0f};
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (!initialized_ || !ReadyForUpdate()) return stats;
    
    // Get last value for GAE computation
    auto last_obs = buffer_.GetRandomBatch(1).observations[0];
    auto obs_tensor = torch::from_blob(
        last_obs.data(),
        {1, static_cast<long>(last_obs.size())},
        torch::kFloat32
    ).clone().to(device_);
    
    auto [_, __, last_value] = model_->GetAction(obs_tensor, true);
    
    // Compute returns and advantages
    buffer_.ComputeReturnsAndAdvantages(
        last_value.item<float>(),
        config_.gamma,
        config_.gae_lambda
    );
    
    float total_policy_loss = 0.0f;
    float total_value_loss = 0.0f;
    float total_entropy = 0.0f;
    size_t num_batches = 0;
    
    // PPO epochs
    for (size_t epoch = 0; epoch < config_.epochs_per_update; epoch++) {
        auto batch = buffer_.GetRandomBatch(config_.batch_size);
        
        // Convert to tensors
        size_t batch_size = batch.actions.size();
        size_t obs_size = batch.observations[0].size();
        
        std::vector<float> obs_flat(batch_size * obs_size);
        for (size_t i = 0; i < batch_size; i++) {
            std::copy(batch.observations[i].begin(), 
                     batch.observations[i].end(),
                     obs_flat.begin() + i * obs_size);
        }
        
        auto obs_t = torch::from_blob(obs_flat.data(), 
            {static_cast<long>(batch_size), static_cast<long>(obs_size)},
            torch::kFloat32).clone().to(device_);
        
        auto actions_t = torch::from_blob(batch.actions.data(),
            {static_cast<long>(batch_size)},
            torch::kInt64).clone().to(device_);
        
        auto old_log_probs_t = torch::from_blob(batch.old_log_probs.data(),
            {static_cast<long>(batch_size)},
            torch::kFloat32).clone().to(device_);
        
        auto advantages_t = torch::from_blob(batch.advantages.data(),
            {static_cast<long>(batch_size)},
            torch::kFloat32).clone().to(device_);
        
        auto returns_t = torch::from_blob(batch.returns.data(),
            {static_cast<long>(batch_size)},
            torch::kFloat32).clone().to(device_);
        
        // Normalize advantages
        advantages_t = (advantages_t - advantages_t.mean()) / (advantages_t.std() + 1e-8);
        
        // Forward pass
        auto [log_probs, values, entropy] = model_->EvaluateActions(obs_t, actions_t);
        
        // Policy loss (PPO clip)
        auto ratio = torch::exp(log_probs - old_log_probs_t);
        auto surr1 = ratio * advantages_t;
        auto surr2 = torch::clamp(ratio, 1.0 - config_.clip_epsilon,
                                        1.0 + config_.clip_epsilon) * advantages_t;
        auto policy_loss = -torch::min(surr1, surr2).mean();
        
        // Value loss
        auto value_loss = torch::mse_loss(values, returns_t);
        
        // Entropy bonus
        auto entropy_loss = -entropy.mean();
        
        // Total loss
        auto loss = policy_loss + 
                   config_.value_coef * value_loss +
                   config_.entropy_coef * entropy_loss;
        
        // Backward and optimize
        optimizer_->zero_grad();
        loss.backward();
        torch::nn::utils::clip_grad_norm_(model_->parameters(), config_.max_grad_norm);
        optimizer_->step();
        
        total_policy_loss += policy_loss.item<float>();
        total_value_loss += value_loss.item<float>();
        total_entropy += entropy.mean().item<float>();
        num_batches++;
    }
    
    stats.policy_loss = total_policy_loss / num_batches;
    stats.value_loss = total_value_loss / num_batches;
    stats.entropy = total_entropy / num_batches;
    
    total_updates_++;
    
    // Clear buffer for next rollout
    buffer_.Clear();
    
    // Auto-save checkpoint
    if (total_updates_ % config_.save_interval == 0) {
        SaveCheckpoint(config_.checkpoint_path);
    }
    
#endif
    
    return stats;
}

float PPOTrainer::GetAverageReward() const {
    if (recent_rewards_.empty()) return 0.0f;
    return std::accumulate(recent_rewards_.begin(), recent_rewards_.end(), 0.0f) /
           recent_rewards_.size();
}

float PPOTrainer::GetAverageCoverage() const {
    if (recent_coverages_.empty()) return 0.0f;
    return std::accumulate(recent_coverages_.begin(), recent_coverages_.end(), 0.0f) /
           recent_coverages_.size();
}

// =============================================================================
// Global Training Control
// =============================================================================

static PPOTrainer g_trainer;
static bool g_training_mode = false;
static bool g_training_active = false;

PPOTrainer& GetTrainer() {
    return g_trainer;
}

void SetTrainingMode(bool training) {
    g_training_mode = training;
}

bool IsTrainingMode() {
    return g_training_mode;
}

void StartTraining(const TrainingConfig& config) {
    if (g_trainer.Init(config)) {
        g_training_active = true;
        g_training_mode = true;
    }
}

void StopTraining() {
    g_training_active = false;
    g_trainer.Shutdown();
}

bool IsTraining() {
    return g_training_active;
}

void TrainingTick(uint32_t coverage_gain, uint32_t data_gain, bool stalled, bool program_exit) {
    if (!g_training_active || !g_trainer.IsInitialized()) return;
    
    const auto& config = g_trainer.GetConfig();
    
    // Build observation
    auto obs = g_trainer.BuildObservation();
    
    // Get action from policy
    float log_prob, value;
    uint32_t action = g_trainer.SelectAction(obs, &log_prob, &value);
    
    // Execute action
    InjectAction(action);
    
    // Compute reward
    float reward = 0.0f;
    reward += config.coverage_reward * coverage_gain;
    reward += config.data_reward * data_gain;
    
    if (action == GetActionSpaceInfo().noop_action) {
        reward += config.noop_penalty;
    }
    
    if (stalled) {
        reward += config.stall_penalty;
    }
    
    // Add experience
    bool done = program_exit || stalled;
    g_trainer.AddExperience(obs, action, reward, value, log_prob, done);
    
    // Update if buffer full
    if (g_trainer.ReadyForUpdate()) {
        auto stats = g_trainer.Update();
        
        // Log progress
        // TODO: Use Explorer logging
    }
}

} // namespace Explorer
