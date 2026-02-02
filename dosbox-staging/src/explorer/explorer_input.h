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
// Neural Network Action Space API (145 discrete actions)
// =============================================================================

// Action space info for NN training
struct ActionSpaceInfo {
    size_t total_actions;          // 145
    
    // Action counts per category
    size_t num_key_press;          // 92 (regular keys: tap down+up)
    size_t num_modifier_down;      // 7 (shift/ctrl/alt L/R + numlock)
    size_t num_modifier_up;        // 7
    size_t num_mouse_move;         // 24 (8 directions × 3 speeds)
    size_t num_mouse_button_down;  // 3 (left/right/middle)
    size_t num_mouse_button_up;    // 3
    size_t num_mouse_click;        // 3 (down+up)
    size_t num_mouse_dblclick;     // 3
    size_t num_mouse_wheel;        // 2 (up/down)
    
    // Starting indices for each category
    size_t key_press_start;        // 0
    size_t modifier_down_start;    // 92
    size_t modifier_up_start;      // 99
    size_t mouse_move_start;       // 106
    size_t mouse_button_down_start;// 130
    size_t mouse_button_up_start;  // 133
    size_t mouse_click_start;      // 136
    size_t mouse_dblclick_start;   // 139
    size_t mouse_wheel_start;      // 142
    size_t noop_action;            // 144
};

// Get total number of discrete actions available
size_t GetNumActions();

// Get number of keyboard-only actions
size_t GetNumKeyboardActions();

// Get number of mouse-only actions  
size_t GetNumMouseActions();

// Get human-readable name for an action ID
const char* GetActionName(size_t action_id);

// Inject a specific action by ID
// action_id: 0 to GetNumActions()-1
void InjectAction(size_t action_id, float mouse_magnitude = 20.0f);

// Get detailed action space info for NN training
ActionSpaceInfo GetActionSpaceInfo();

// =============================================================================
// Action Space Layout (145 actions):
//
// [0-91]    Key press (tap): 0-9, a-z, F1-F12, arrows, esc/tab/enter/space, etc.
// [92-98]   Modifier DOWN:   LSHIFT, RSHIFT, LCTRL, RCTRL, LALT, RALT, NUMLOCK
// [99-105]  Modifier UP:     same as above (release)
// [106-129] Mouse move:      8 directions × 3 speeds (slow/med/fast)
// [130-132] Mouse btn DOWN:  left, right, middle (for drag start)
// [133-135] Mouse btn UP:    left, right, middle (for drag end)
// [136-138] Mouse CLICK:     left, right, middle (down+up combo)
// [139-141] Mouse DBLCLICK:  left, right, middle
// [142-143] Mouse wheel:     up, down
// [144]     NO_OP:           do nothing
//
// Drag-drop example: MOUSE_LEFT_DOWN → MOUSE_RIGHT_FAST → MOUSE_LEFT_UP
// Shift-select:      LSHIFT_DOWN → Arrow keys → LSHIFT_UP
// Ctrl+C:            LCTRL_DOWN → key 'c' → LCTRL_UP
// =============================================================================

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

struct ActionSpaceInfo {
    size_t total_actions; size_t num_key_press; size_t num_modifier_down;
    size_t num_modifier_up; size_t num_mouse_move; size_t num_mouse_button_down;
    size_t num_mouse_button_up; size_t num_mouse_click; size_t num_mouse_dblclick;
    size_t num_mouse_wheel; size_t key_press_start; size_t modifier_down_start;
    size_t modifier_up_start; size_t mouse_move_start; size_t mouse_button_down_start;
    size_t mouse_button_up_start; size_t mouse_click_start; size_t mouse_dblclick_start;
    size_t mouse_wheel_start; size_t noop_action;
};
inline size_t GetNumActions() { return 0; }
inline size_t GetNumKeyboardActions() { return 0; }
inline size_t GetNumMouseActions() { return 0; }
inline const char* GetActionName(size_t) { return nullptr; }
inline void InjectAction(size_t, float = 20.0f) {}
inline ActionSpaceInfo GetActionSpaceInfo() { return {}; }

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_INPUT_H
