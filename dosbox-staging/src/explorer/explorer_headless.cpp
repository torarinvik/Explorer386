// SPDX-License-Identifier: MIT
// Explorer headless mode implementation

#include "explorer_headless.h"

#ifdef EXPLORER_ENABLED

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

#include "misc/logging.h"

namespace Explorer {

// =============================================================================
// Static State
// =============================================================================

static HeadlessConfig s_headless_config;
static bool s_headless_initialized = false;
static std::atomic<bool> s_stop_requested{false};

// Frame timing
static std::chrono::steady_clock::time_point s_frame_start;
static uint64_t s_frame_count = 0;

// =============================================================================
// Helper functions
// =============================================================================

static bool env_is_truthy(const char* var)
{
    const char* val = std::getenv(var);
    if (!val || !val[0]) {
        return false;
    }
    // Consider "0" or "false" as falsy, everything else as truthy
    if (val[0] == '0' && val[1] == '\0') {
        return false;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "FALSE") == 0) {
        return false;
    }
    return true;
}

// =============================================================================
// Implementation
// =============================================================================

bool IsHeadless()
{
    return s_headless_config.enabled;
}

bool EnableHeadless(const HeadlessConfig& config)
{
    if (s_headless_initialized) {
        LOG_WARNING("EXPLORER: Headless mode already configured, ignoring");
        return s_headless_config.enabled;
    }
    
    s_headless_config = config;
    s_headless_config.enabled = true;
    
    LOG_MSG("EXPLORER: Headless mode enabled (audio=%s, unlimited_speed=%s)",
            config.disable_audio ? "disabled" : "enabled",
            config.unlimited_speed ? "yes" : "no");
    
    return true;
}

const HeadlessConfig& GetHeadlessConfig()
{
    return s_headless_config;
}

bool SetupHeadlessEnvironment()
{
    // Check environment variable directly since this is called before
    // DOSBOX_InitModules where EnableHeadless() would normally be called
    if (!s_headless_config.enabled) {
        // Check if headless was requested via environment variable
        if (env_is_truthy("EXPLORER_HEADLESS")) {
            // Auto-configure from environment
            HeadlessConfig config;
            config.disable_audio = !env_is_truthy("EXPLORER_HEADLESS_AUDIO");
            config.unlimited_speed = true;  // Default to unlimited in headless
            s_headless_config = config;
            s_headless_config.enabled = true;
        } else {
            return false;
        }
    }
    
    if (s_headless_initialized) {
        return true;
    }
    
    // Set SDL to use dummy video driver (no window)
    // This must be done BEFORE SDL_Init
#ifdef _WIN32
    _putenv_s("SDL_VIDEODRIVER", "dummy");
    if (s_headless_config.disable_audio) {
        _putenv_s("SDL_AUDIODRIVER", "dummy");
    }
#else
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (s_headless_config.disable_audio) {
        setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
#endif
    
    LOG_MSG("EXPLORER: Set SDL_VIDEODRIVER=dummy for headless mode");
    if (s_headless_config.disable_audio) {
        LOG_MSG("EXPLORER: Set SDL_AUDIODRIVER=dummy for headless mode");
    }
    
    s_headless_initialized = true;
    return true;
}

bool ShouldSkipWindow()
{
    return s_headless_config.enabled;
}

bool ShouldSkipAudio()
{
    return s_headless_config.enabled && s_headless_config.disable_audio;
}

void HeadlessFrameStart()
{
    if (!s_headless_config.enabled) {
        return;
    }
    
    s_frame_start = std::chrono::steady_clock::now();
    s_frame_count++;
}

void HeadlessFrameEnd()
{
    if (!s_headless_config.enabled) {
        return;
    }
    
    // If we have a target FPS and not in unlimited mode, throttle
    if (!s_headless_config.unlimited_speed && s_headless_config.target_fps > 0) {
        const auto frame_time_us = 1000000 / s_headless_config.target_fps;
        const auto elapsed = std::chrono::steady_clock::now() - s_frame_start;
        const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        
        if (elapsed_us < frame_time_us) {
            std::this_thread::sleep_for(std::chrono::microseconds(frame_time_us - elapsed_us));
        }
    }
}

void RequestStop()
{
    s_stop_requested.store(true, std::memory_order_release);
    LOG_MSG("EXPLORER: Stop requested");
}

bool IsStopRequested()
{
    return s_stop_requested.load(std::memory_order_acquire);
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
