// SPDX-License-Identifier: MIT
// Explorer headless mode support
// Allows running DOSBox without a GUI window for automated testing/fuzzing

#ifndef EXPLORER_HEADLESS_H
#define EXPLORER_HEADLESS_H

#include <cstdint>
#include <string>

#ifdef EXPLORER_ENABLED

namespace Explorer {

// =============================================================================
// Headless Mode Configuration
// =============================================================================

struct HeadlessConfig {
    bool enabled = false;           // Run without GUI
    bool disable_audio = true;      // Disable audio output
    bool unlimited_speed = true;    // Run as fast as possible
    uint32_t target_fps = 0;        // If >0, throttle to this FPS
};

// =============================================================================
// Headless Mode API
// =============================================================================

// Check if headless mode is enabled
bool IsHeadless();

// Enable headless mode (must be called before SDL init)
// Returns true if successfully configured
bool EnableHeadless(const HeadlessConfig& config = HeadlessConfig{});

// Get current headless configuration
const HeadlessConfig& GetHeadlessConfig();

// Called internally before SDL_Init to set up environment
// Returns true if headless mode is active and env was configured
bool SetupHeadlessEnvironment();

// Called internally to check if we should skip certain SDL operations
bool ShouldSkipWindow();
bool ShouldSkipAudio();

// Frame timing control for headless mode
void HeadlessFrameStart();
void HeadlessFrameEnd();

// Request the emulator to stop (for use in automated runs)
void RequestStop();
bool IsStopRequested();

} // namespace Explorer

#else // !EXPLORER_ENABLED

namespace Explorer {

inline bool IsHeadless() { return false; }
inline bool EnableHeadless(const struct HeadlessConfig& = {}) { return false; }
inline bool SetupHeadlessEnvironment() { return false; }
inline bool ShouldSkipWindow() { return false; }
inline bool ShouldSkipAudio() { return false; }
inline void HeadlessFrameStart() {}
inline void HeadlessFrameEnd() {}
inline void RequestStop() {}
inline bool IsStopRequested() { return false; }

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_HEADLESS_H
