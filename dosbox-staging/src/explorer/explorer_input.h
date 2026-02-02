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
// Neural Network Action Space API (381 discrete actions)
// MAXIMUM GRANULARITY for full game control
// =============================================================================

// Action space info for NN training
struct ActionSpaceInfo {
    size_t total_actions;          // 381
    
    // Keyboard (99 keys × 3 modes = 297 keyboard actions)
    size_t num_keys;               // 99 (all keys including modifiers)
    size_t key_tap_start;          // 0   - quick press+release
    size_t key_down_start;         // 99  - hold key down
    size_t key_up_start;           // 198 - release key
    
    // Mouse movement
    size_t num_mouse_dirs;         // 8 directions
    size_t num_mouse_speeds;       // 5 speeds (micro/slow/med/fast/jump)
    size_t mouse_move_start;       // 297 (40 actions: 8×5)
    size_t num_mouse_positions;    // 15 screen regions
    size_t mouse_abs_start;        // 337 (15 actions)
    
    // Mouse buttons (3 buttons × various modes)
    size_t mouse_btn_down_start;   // 352 (3 actions)
    size_t mouse_btn_up_start;     // 355 (3 actions)
    size_t mouse_click_start;      // 358 (3 actions)
    size_t mouse_dblclick_start;   // 361 (3 actions)
    size_t mouse_triclick_start;   // 364 (3 actions)
    
    // Mouse wheel
    size_t num_wheel_amounts;      // 3 (small/med/large)
    size_t wheel_start;            // 367 (6 actions: up/down × 3)
    
    // Drag operations
    size_t drag_start_start;       // 373 (3 actions)
    size_t drag_end_start;         // 376 (3 actions)
    
    // Special actions
    size_t noop_action;            // 379
    size_t release_all_action;     // 380 - release all held keys/buttons
};

// Get total number of discrete actions available
size_t GetNumActions();

// Get number of keyboard-only actions
size_t GetNumKeyboardActions();

// Get number of mouse-only actions  
size_t GetNumMouseActions();

// Get human-readable name for an action ID
const char* GetActionName(size_t action_id);

// Inject a specific action by ID (0 to 380)
void InjectAction(size_t action_id, float unused = 0.0f);

// Get detailed action space info for NN training
ActionSpaceInfo GetActionSpaceInfo();

// =============================================================================
// Action Space Layout (381 actions total):
//
// KEYBOARD (297 actions):
//   [0-98]     Key TAP:   Quick press+release for any key
//   [99-197]   Key DOWN:  Start holding a key (for games needing held keys)
//   [198-296]  Key UP:    Release a held key
//
// MOUSE MOVEMENT (55 actions):
//   [297-336]  Relative:  8 directions × 5 speeds (micro/slow/med/fast/jump)
//   [337-351]  Absolute:  15 screen positions (corners, edges, dialog buttons)
//
// MOUSE BUTTONS (21 actions):
//   [352-354]  Button DOWN:    left/right/middle (drag start)
//   [355-357]  Button UP:      left/right/middle (drag end)
//   [358-360]  CLICK:          left/right/middle (quick down+up)
//   [361-363]  DOUBLE-CLICK:   left/right/middle
//   [364-366]  TRIPLE-CLICK:   left/right/middle (select line/paragraph)
//
// MOUSE WHEEL (6 actions):
//   [367-369]  Wheel UP:   small/medium/large scroll
//   [370-372]  Wheel DOWN: small/medium/large scroll
//
// DRAG CONVENIENCE (6 actions):
//   [373-375]  DRAG_START: left/right/middle (same as btn down, semantic)
//   [376-378]  DRAG_END:   left/right/middle (same as btn up, semantic)
//
// SPECIAL (2 actions):
//   [379]      NO_OP:       Do nothing
//   [380]      RELEASE_ALL: Release all held keys and mouse buttons
//
// Example sequences for NN to learn:
//   - Hold arrow for movement: DOWN_left → (wait) → UP_left
//   - Ctrl+C: DOWN_leftctrl → TAP_c → UP_leftctrl
//   - Drag file: POS_TOPLEFT → DRAG_START_L → MOUSE_RIGHT_FAST → DRAG_END_L
//   - Menu click: POS_TOPCENTER → CLICK_L
//   - Scroll document: WHEEL_DOWN_LARGE (multiple times)
//   - Reset state: RELEASE_ALL
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
    size_t total_actions;
    size_t num_keys; size_t key_tap_start; size_t key_down_start; size_t key_up_start;
    size_t num_mouse_dirs; size_t num_mouse_speeds; size_t mouse_move_start;
    size_t num_mouse_positions; size_t mouse_abs_start;
    size_t mouse_btn_down_start; size_t mouse_btn_up_start;
    size_t mouse_click_start; size_t mouse_dblclick_start; size_t mouse_triclick_start;
    size_t num_wheel_amounts; size_t wheel_start;
    size_t drag_start_start; size_t drag_end_start;
    size_t noop_action; size_t release_all_action;
};
inline size_t GetNumActions() { return 0; }
inline size_t GetNumKeyboardActions() { return 0; }
inline size_t GetNumMouseActions() { return 0; }
inline const char* GetActionName(size_t) { return nullptr; }
inline void InjectAction(size_t, float = 0.0f) {}
inline ActionSpaceInfo GetActionSpaceInfo() { return {}; }

} // namespace Explorer

#endif // EXPLORER_ENABLED

#endif // EXPLORER_INPUT_H
