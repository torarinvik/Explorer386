// SPDX-License-Identifier: MIT
// Explorer input injection API
// Provides programmatic keyboard and mouse input for automated testing/fuzzing

#ifndef EXPLORER_INPUT_H
#define EXPLORER_INPUT_H

#include <cstdint>
#include <string>
#include <vector>
#include <queue>

#ifdef EXPLORER_ENABLED

// Include DOSBox keyboard header for KBD_KEYS enum
#include "hardware/input/keyboard.h"

// Forward declaration for DOSBox mouse button type
enum class MouseButtonId : uint8_t;

namespace Explorer {

// =============================================================================
// Keyboard Input
// =============================================================================

// Inject a key press or release event
// key: One of KBD_KEYS enum values
// pressed: true for key down, false for key up
void InjectKey(int key, bool pressed);

// Inject a complete key press (down then up)
void InjectKeyPress(int key);

// Inject a text string as keyboard input (ASCII only)
// Converts characters to appropriate key codes
void InjectText(const char* text);

// Inject directly into BIOS keyboard buffer
// code: High byte = scancode, low byte = ASCII
void InjectBIOSKey(uint16_t code);

// Queue a key event to be injected after a delay (in instructions)
void InjectKeyDelayed(int key, bool pressed, uint64_t delay_instructions);

// Process any pending delayed key events (called from instruction hook)
void ProcessDelayedKeys(uint64_t current_instruction);

// Convert key name string to KBD_KEYS value
// Returns -1 if not found
int KeyNameToCode(const char* name);

// Get key name from code
const char* KeyCodeToName(int key);

// =============================================================================
// Mouse Input
// =============================================================================

// Inject relative mouse movement
void InjectMouseMove(float dx, float dy);

// Inject absolute mouse position (0.0-1.0 normalized coordinates)
void InjectMouseMoveTo(float x_abs, float y_abs);

// Inject mouse button event
// button: MouseButtonId::Left, Right, Middle, Extra1, Extra2
void InjectMouseButton(uint8_t button, bool pressed);

// Inject mouse wheel scroll
void InjectMouseWheel(int16_t delta);

// Convenience: move to position and click
void InjectMouseClick(uint8_t button, float x, float y);

// =============================================================================
// Input State Queries
// =============================================================================

struct InputStats {
    uint64_t keys_injected;
    uint64_t mouse_moves_injected;
    uint64_t mouse_buttons_injected;
    uint64_t bios_keys_injected;
    uint64_t pending_delayed_keys;
};

InputStats GetInputStats();

// Reset input statistics
void ResetInputStats();

// =============================================================================
// Fuzz Input Generation (convenience helpers)
// =============================================================================

// Inject a random key press (from ALL available keys)
void InjectRandomKey();

// Inject random mouse movement
void InjectRandomMouseMove(float max_delta = 50.0f);

// Inject a random mouse button click
void InjectRandomMouseClick();

// Inject a completely random action (key, mouse, or NO_OP)
void InjectRandomAction();

// Set random seed for reproducible fuzzing
void SetInputRandomSeed(uint32_t seed);

// =============================================================================
// Neural Network Action Space API
// =============================================================================

// Get total number of discrete actions available
// This is the action space size for an NN controller
size_t GetNumActions();

// Get number of keyboard-only actions
size_t GetNumKeyboardActions();

// Get number of mouse-only actions  
size_t GetNumMouseActions();

// Get human-readable name for an action ID
const char* GetActionName(size_t action_id);

// Inject a specific action by ID
// action_id: 0 to GetNumActions()-1
// mouse_magnitude: movement amount for mouse actions (default 20.0)
void InjectAction(size_t action_id, float mouse_magnitude = 20.0f);

// Action space layout:
//   [0, NUM_KEYBOARD_ACTIONS-1] = Keyboard keys
//   [NUM_KEYBOARD_ACTIONS, NUM_KEYBOARD_ACTIONS+NUM_MOUSE_ACTIONS-1] = Mouse actions
//   [TOTAL_ACTIONS-1] = NO_OP (do nothing)

} // namespace Explorer

#else // !EXPLORER_ENABLED

namespace Explorer {

inline void InjectKey(int, bool) {}
inline void InjectKeyPress(int) {}
inline void InjectText(const char*) {}
inline void InjectBIOSKey(uint16_t) {}
inline void InjectKeyDelayed(int, bool, uint64_t) {}
inline void ProcessDelayedKeys(uint64_t) {}
inline int KeyNameToCode(const char*) { return -1; }
inline const char* KeyCodeToName(int) { return nullptr; }

inline void InjectMouseMove(float, float) {}
inline void InjectMouseMoveTo(float, float) {}
inline void InjectMouseButton(uint8_t, bool) {}
inline void InjectMouseWheel(int16_t) {}
inline void InjectMouseClick(uint8_t, float, float) {}

struct InputStats { uint64_t keys_injected; uint64_t mouse_moves_injected;
                    uint64_t mouse_buttons_injected; uint64_t bios_keys_injected;
                    uint64_t pending_delayed_keys; };
inline InputStats GetInputStats() { return {}; }
inline void ResetInputStats() {}

inline void InjectRandomKey() {}
inline void InjectRandomMouseMove(float = 50.0f) {}
inline void InjectRandomMouseClick() {}
inline void InjectRandomAction() {}
inline void SetInputRandomSeed(uint32_t) {}

inline size_t GetNumActions() { return 0; }
inline size_t GetNumKeyboardActions() { return 0; }
inline size_t GetNumMouseActions() { return 0; }
inline const char* GetActionName(size_t) { return nullptr; }
inline void InjectAction(size_t, float = 20.0f) {}

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_INPUT_H
