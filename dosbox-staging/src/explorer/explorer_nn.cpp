// SPDX-License-Identifier: MIT
// Explorer neural network policy implementation

#include "explorer_nn.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace Explorer {

// =============================================================================
// ObservationBuilder Implementation
// =============================================================================

void ObservationBuilder::SetDimension(size_t dim) {
    obs_.resize(dim, 0.0f);
}

void ObservationBuilder::Reset() {
    std::fill(obs_.begin(), obs_.end(), 0.0f);
    write_pos_ = 0;
}

void ObservationBuilder::AddCoverageSummary(const uint8_t* coverage_map, size_t map_size) {
    if (write_pos_ >= obs_.size()) return;
    
    // Downsample coverage map into buckets
    size_t buckets_to_write = std::min(OBS_COVERAGE_BUCKETS, obs_.size() - write_pos_);
    size_t samples_per_bucket = (map_size + buckets_to_write - 1) / buckets_to_write;
    
    for (size_t b = 0; b < buckets_to_write; b++) {
        size_t start = b * samples_per_bucket;
        size_t end = std::min(start + samples_per_bucket, map_size);
        
        uint32_t set_bits = 0;
        for (size_t i = start; i < end; i++) {
            set_bits += __builtin_popcount(coverage_map[i]);
        }
        
        // Normalize to [0, 1]
        float max_bits = static_cast<float>((end - start) * 8);
        obs_[write_pos_++] = max_bits > 0 ? (set_bits / max_bits) : 0.0f;
    }
}

void ObservationBuilder::AddDataSummary(const uint8_t* data_flags, size_t bucket_count) {
    if (write_pos_ >= obs_.size()) return;
    
    size_t buckets_to_write = std::min(OBS_DATA_BUCKETS, obs_.size() - write_pos_);
    size_t samples_per_bucket = (bucket_count + buckets_to_write - 1) / buckets_to_write;
    
    for (size_t b = 0; b < buckets_to_write; b++) {
        size_t start = b * samples_per_bucket;
        size_t end = std::min(start + samples_per_bucket, bucket_count);
        
        uint32_t active_count = 0;
        for (size_t i = start; i < end; i++) {
            if (data_flags[i] != 0) active_count++;
        }
        
        float max_count = static_cast<float>(end - start);
        obs_[write_pos_++] = max_count > 0 ? (active_count / max_count) : 0.0f;
    }
}

void ObservationBuilder::AddStateFeatures(uint32_t pc, uint32_t sp, uint32_t flags,
                                          uint64_t instruction_count, uint32_t stall_count) {
    if (write_pos_ + OBS_STATE_DIM > obs_.size()) return;
    
    // Normalize state values to reasonable ranges
    obs_[write_pos_++] = static_cast<float>(pc & 0xFFFF) / 65536.0f;
    obs_[write_pos_++] = static_cast<float>((pc >> 16) & 0xFFFF) / 65536.0f;
    obs_[write_pos_++] = static_cast<float>(sp & 0xFFFF) / 65536.0f;
    obs_[write_pos_++] = static_cast<float>((sp >> 16) & 0xFFFF) / 65536.0f;
    
    // Flag bits as individual features
    obs_[write_pos_++] = (flags & 0x0001) ? 1.0f : 0.0f;  // CF
    obs_[write_pos_++] = (flags & 0x0040) ? 1.0f : 0.0f;  // ZF
    obs_[write_pos_++] = (flags & 0x0080) ? 1.0f : 0.0f;  // SF
    obs_[write_pos_++] = (flags & 0x0800) ? 1.0f : 0.0f;  // OF
    obs_[write_pos_++] = (flags & 0x0200) ? 1.0f : 0.0f;  // IF
    obs_[write_pos_++] = (flags & 0x0004) ? 1.0f : 0.0f;  // PF
    
    // Instruction count (log scale)
    obs_[write_pos_++] = instruction_count > 0 ? 
        std::log10(static_cast<float>(instruction_count)) / 12.0f : 0.0f;
    
    // Stall indicator
    obs_[write_pos_++] = std::min(static_cast<float>(stall_count) / 1000.0f, 1.0f);
    
    // Padding
    while (write_pos_ < obs_.size() && 
           write_pos_ < OBS_COVERAGE_BUCKETS + OBS_DATA_BUCKETS + OBS_STATE_DIM) {
        obs_[write_pos_++] = 0.0f;
    }
}

// =============================================================================
// PolicyNetwork Implementation
// =============================================================================

PolicyNetwork::PolicyNetwork() = default;
PolicyNetwork::~PolicyNetwork() { Shutdown(); }

bool PolicyNetwork::Init(const PolicyConfig& config) {
    config_ = config;
    
    if (!config_.enabled) {
        initialized_ = false;
        return true;
    }
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    // Set up device - prefer MPS on macOS, CUDA elsewhere
    if (config_.use_gpu) {
#ifdef __APPLE__
        if (torch::hasMPS()) {
            device_ = torch::kMPS;
        } else {
            device_ = torch::kCPU;
        }
#elif defined(USE_CUDA) && USE_CUDA
        if (torch::cuda::is_available()) {
            device_ = torch::kCUDA;
        } else {
            device_ = torch::kCPU;
        }
#else
        // CPU-only LibTorch build
        device_ = torch::kCPU;
#endif
    } else {
        device_ = torch::kCPU;
    }
    
    action_logits_.resize(config_.action_dim);
#endif
    
    // Initialize RNG with time-based seed
    rng_state_ = 0x12345678DEADBEEF ^ static_cast<uint64_t>(
        std::hash<std::string>{}(config_.model_path));
    
    initialized_ = true;
    
    // Try to load model if path provided
    if (!config_.model_path.empty()) {
        return LoadModel(config_.model_path);
    }
    
    return true;
}

void PolicyNetwork::Shutdown() {
#ifdef EXPLORER_ENABLE_LIBTORCH
    // Model will be cleaned up automatically
#endif
    model_loaded_ = false;
    initialized_ = false;
}

bool PolicyNetwork::LoadModel([[maybe_unused]] const std::string& path) {
#ifdef EXPLORER_ENABLE_LIBTORCH
    try {
        model_ = torch::jit::load(path);
        model_.to(device_);
        model_.eval();
        model_loaded_ = true;
        return true;
    } catch (const c10::Error& e) {
        model_loaded_ = false;
        return false;
    }
#else
    // Without LibTorch, we can only do random actions
    model_loaded_ = false;
    return false;
#endif
}

uint32_t PolicyNetwork::RandomU32() {
    // xorshift64*
    rng_state_ ^= rng_state_ >> 12;
    rng_state_ ^= rng_state_ << 25;
    rng_state_ ^= rng_state_ >> 27;
    return static_cast<uint32_t>((rng_state_ * 0x2545F4914F6CDD1DULL) >> 32);
}

float PolicyNetwork::RandomFloat() {
    return static_cast<float>(RandomU32()) / 4294967296.0f;
}

uint32_t PolicyNetwork::RandomAction() {
    exploration_count_++;
    return RandomU32() % static_cast<uint32_t>(config_.action_dim);
}

uint32_t PolicyNetwork::SelectAction(const std::vector<float>& observation) {
    return SelectAction(observation.data(), observation.size());
}

uint32_t PolicyNetwork::SelectAction([[maybe_unused]] const float* obs_data, 
                                     [[maybe_unused]] size_t obs_size) {
    if (!initialized_) return ACTION_NOOP;
    
    // Epsilon-greedy exploration
    if (RandomFloat() < config_.exploration_rate) {
        return RandomAction();
    }
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (!model_loaded_) {
        return RandomAction();
    }
    
    try {
        // Create input tensor
        auto options = torch::TensorOptions().dtype(torch::kFloat32);
        torch::Tensor input = torch::from_blob(
            const_cast<float*>(obs_data),
            {1, static_cast<long>(obs_size)},
            options
        ).clone().to(device_);
        
        // Forward pass
        std::vector<torch::jit::IValue> inputs;
        inputs.push_back(input);
        
        torch::Tensor output = model_.forward(inputs).toTensor();
        output = output.to(torch::kCPU).squeeze(0);
        
        // Copy logits
        auto output_accessor = output.accessor<float, 1>();
        for (size_t i = 0; i < config_.action_dim && i < static_cast<size_t>(output.size(0)); i++) {
            action_logits_[i] = output_accessor[static_cast<long>(i)];
        }
        
        inference_count_++;
        
        if (config_.deterministic) {
            // Argmax
            return static_cast<uint32_t>(
                std::distance(action_logits_.begin(),
                             std::max_element(action_logits_.begin(), action_logits_.end())));
        } else {
            return SampleFromLogits(action_logits_);
        }
        
    } catch (const c10::Error&) {
        return RandomAction();
    }
#else
    return RandomAction();
#endif
}

#ifdef EXPLORER_ENABLE_LIBTORCH
uint32_t PolicyNetwork::SampleFromLogits(const std::vector<float>& logits) {
    // Apply temperature and softmax
    std::vector<float> probs(logits.size());
    
    float max_logit = *std::max_element(logits.begin(), logits.end());
    float sum = 0.0f;
    
    for (size_t i = 0; i < logits.size(); i++) {
        probs[i] = std::exp((logits[i] - max_logit) / config_.temperature);
        sum += probs[i];
    }
    
    // Normalize
    for (float& p : probs) {
        p /= sum;
    }
    
    // Sample
    float r = RandomFloat();
    float cumsum = 0.0f;
    
    for (size_t i = 0; i < probs.size(); i++) {
        cumsum += probs[i];
        if (r < cumsum) {
            return static_cast<uint32_t>(i);
        }
    }
    
    return static_cast<uint32_t>(probs.size() - 1);
}
#endif

std::vector<float> PolicyNetwork::GetActionProbs(const std::vector<float>& observation) {
    std::vector<float> probs(config_.action_dim, 1.0f / config_.action_dim);
    
#ifdef EXPLORER_ENABLE_LIBTORCH
    if (model_loaded_) {
        SelectAction(observation);  // This populates action_logits_
        
        // Softmax
        float max_logit = *std::max_element(action_logits_.begin(), action_logits_.end());
        float sum = 0.0f;
        
        for (size_t i = 0; i < action_logits_.size(); i++) {
            probs[i] = std::exp((action_logits_[i] - max_logit) / config_.temperature);
            sum += probs[i];
        }
        
        for (float& p : probs) {
            p /= sum;
        }
    }
#else
    (void)observation;
#endif
    
    return probs;
}

uint8_t PolicyNetwork::ActionToScancode(uint32_t action) {
    if (action < NUM_SCANCODES) {
        return static_cast<uint8_t>(action);
    }
    
    // Map special actions to common scancodes
    switch (action) {
        case ACTION_ENTER: return 0x1C;
        case ACTION_ESC:   return 0x01;
        case ACTION_CTRL:  return 0x1D;
        case ACTION_ALT:   return 0x38;
        case ACTION_SHIFT: return 0x2A;
        default:           return 0x00;  // No key
    }
}

bool PolicyNetwork::IsSpecialAction(uint32_t action) {
    return action >= NUM_SCANCODES;
}

// =============================================================================
// ActionQueue Implementation
// =============================================================================

void ActionQueue::Clear() {
    queue_.clear();
}

void ActionQueue::Push(uint32_t action, uint32_t hold_frames) {
    queue_.push_back({action, hold_frames});
}

void ActionQueue::PushScancode(uint8_t scancode, uint32_t hold_frames) {
    Push(static_cast<uint32_t>(scancode), hold_frames);
}

uint32_t ActionQueue::Peek() const {
    if (queue_.empty()) return ACTION_NOOP;
    return queue_.front().action;
}

uint32_t ActionQueue::Pop() {
    if (queue_.empty()) return ACTION_NOOP;
    
    uint32_t action = queue_.front().action;
    
    if (--queue_.front().frames_remaining == 0) {
        queue_.erase(queue_.begin());
    }
    
    return action;
}

// =============================================================================
// Global Singletons
// =============================================================================

static PolicyNetwork g_policy_network;
static ActionQueue g_action_queue;

PolicyNetwork& GetPolicyNetwork() {
    return g_policy_network;
}

ActionQueue& GetActionQueue() {
    return g_action_queue;
}

} // namespace Explorer
