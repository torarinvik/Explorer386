// SPDX-License-Identifier: MIT
// Explorer input injection implementation

#include "explorer_input.h"

#ifdef EXPLORER_ENABLED

#include <cstdlib>
#include <cstring>
#include <random>
#include <algorithm>

#include "explorer_headless.h"
#include "hardware/input/keyboard.h"
#include "hardware/input/mouse.h"
#include "ints/bios.h"
#include "misc/logging.h"

namespace Explorer {

// =============================================================================
// Static State
// =============================================================================

static InputStats s_input_stats = {};
static std::mt19937 s_rng;

// Delayed key queue
struct DelayedKeyEvent {
    int key;
    bool pressed;
    uint64_t trigger_instruction;
};

static std::vector<DelayedKeyEvent> s_delayed_keys;

// Key name lookup table
struct KeyNameEntry {
    const char* name;
    KBD_KEYS key;
};

static const KeyNameEntry s_key_names[] = {
    {"esc", KBD_esc}, {"escape", KBD_esc},
    {"1", KBD_1}, {"2", KBD_2}, {"3", KBD_3}, {"4", KBD_4}, {"5", KBD_5},
    {"6", KBD_6}, {"7", KBD_7}, {"8", KBD_8}, {"9", KBD_9}, {"0", KBD_0},
    {"q", KBD_q}, {"w", KBD_w}, {"e", KBD_e}, {"r", KBD_r}, {"t", KBD_t},
    {"y", KBD_y}, {"u", KBD_u}, {"i", KBD_i}, {"o", KBD_o}, {"p", KBD_p},
    {"a", KBD_a}, {"s", KBD_s}, {"d", KBD_d}, {"f", KBD_f}, {"g", KBD_g},
    {"h", KBD_h}, {"j", KBD_j}, {"k", KBD_k}, {"l", KBD_l},
    {"z", KBD_z}, {"x", KBD_x}, {"c", KBD_c}, {"v", KBD_v}, {"b", KBD_b},
    {"n", KBD_n}, {"m", KBD_m},
    {"f1", KBD_f1}, {"f2", KBD_f2}, {"f3", KBD_f3}, {"f4", KBD_f4},
    {"f5", KBD_f5}, {"f6", KBD_f6}, {"f7", KBD_f7}, {"f8", KBD_f8},
    {"f9", KBD_f9}, {"f10", KBD_f10}, {"f11", KBD_f11}, {"f12", KBD_f12},
    {"tab", KBD_tab}, {"backspace", KBD_backspace}, {"enter", KBD_enter},
    {"return", KBD_enter}, {"space", KBD_space},
    {"lalt", KBD_leftalt}, {"ralt", KBD_rightalt},
    {"leftalt", KBD_leftalt}, {"rightalt", KBD_rightalt},
    {"lctrl", KBD_leftctrl}, {"rctrl", KBD_rightctrl},
    {"leftctrl", KBD_leftctrl}, {"rightctrl", KBD_rightctrl},
    {"lshift", KBD_leftshift}, {"rshift", KBD_rightshift},
    {"leftshift", KBD_leftshift}, {"rightshift", KBD_rightshift},
    {"capslock", KBD_capslock}, {"numlock", KBD_numlock}, {"scrolllock", KBD_scrolllock},
    {"up", KBD_up}, {"down", KBD_down}, {"left", KBD_left}, {"right", KBD_right},
    {"insert", KBD_insert}, {"delete", KBD_delete}, {"home", KBD_home},
    {"end", KBD_end}, {"pageup", KBD_pageup}, {"pagedown", KBD_pagedown},
    {"kp0", KBD_kp0}, {"kp1", KBD_kp1}, {"kp2", KBD_kp2}, {"kp3", KBD_kp3},
    {"kp4", KBD_kp4}, {"kp5", KBD_kp5}, {"kp6", KBD_kp6}, {"kp7", KBD_kp7},
    {"kp8", KBD_kp8}, {"kp9", KBD_kp9},
    {"kpenter", KBD_kpenter}, {"kpplus", KBD_kpplus}, {"kpminus", KBD_kpminus},
    {"kpmultiply", KBD_kpmultiply}, {"kpdivide", KBD_kpdivide}, {"kpperiod", KBD_kpperiod},
    {"minus", KBD_minus}, {"equals", KBD_equals}, {"backslash", KBD_backslash},
    {"leftbracket", KBD_leftbracket}, {"rightbracket", KBD_rightbracket},
    {"semicolon", KBD_semicolon}, {"quote", KBD_quote}, {"grave", KBD_grave},
    {"period", KBD_period}, {"comma", KBD_comma}, {"slash", KBD_slash},
    {nullptr, KBD_NONE}
};

// ASCII to key mapping for text injection
static KBD_KEYS ascii_to_key(char c, bool& need_shift)
{
    need_shift = false;
    
    if (c >= 'a' && c <= 'z') {
        return static_cast<KBD_KEYS>(KBD_a + (c - 'a'));
    }
    if (c >= 'A' && c <= 'Z') {
        need_shift = true;
        return static_cast<KBD_KEYS>(KBD_a + (c - 'A'));
    }
    if (c >= '0' && c <= '9') {
        return static_cast<KBD_KEYS>(KBD_0 + (c - '0'));
    }
    
    switch (c) {
        case ' ': return KBD_space;
        case '\n': case '\r': return KBD_enter;
        case '\t': return KBD_tab;
        case '-': return KBD_minus;
        case '=': return KBD_equals;
        case '[': return KBD_leftbracket;
        case ']': return KBD_rightbracket;
        case ';': return KBD_semicolon;
        case '\'': return KBD_quote;
        case '`': return KBD_grave;
        case '\\': return KBD_backslash;
        case ',': return KBD_comma;
        case '.': return KBD_period;
        case '/': return KBD_slash;
        // Shifted characters
        case '!': need_shift = true; return KBD_1;
        case '@': need_shift = true; return KBD_2;
        case '#': need_shift = true; return KBD_3;
        case '$': need_shift = true; return KBD_4;
        case '%': need_shift = true; return KBD_5;
        case '^': need_shift = true; return KBD_6;
        case '&': need_shift = true; return KBD_7;
        case '*': need_shift = true; return KBD_8;
        case '(': need_shift = true; return KBD_9;
        case ')': need_shift = true; return KBD_0;
        case '_': need_shift = true; return KBD_minus;
        case '+': need_shift = true; return KBD_equals;
        case '{': need_shift = true; return KBD_leftbracket;
        case '}': need_shift = true; return KBD_rightbracket;
        case ':': need_shift = true; return KBD_semicolon;
        case '"': need_shift = true; return KBD_quote;
        case '~': need_shift = true; return KBD_grave;
        case '|': need_shift = true; return KBD_backslash;
        case '<': need_shift = true; return KBD_comma;
        case '>': need_shift = true; return KBD_period;
        case '?': need_shift = true; return KBD_slash;
        default: return KBD_NONE;
    }
}

// =============================================================================
// Keyboard Implementation
// =============================================================================

void InjectKey(int key, bool pressed)
{
    if (key <= KBD_NONE || key >= KBD_LAST) {
        return;
    }
    
    KEYBOARD_AddKey(static_cast<KBD_KEYS>(key), pressed);
    s_input_stats.keys_injected++;
}

void InjectKeyPress(int key)
{
    InjectKey(key, true);
    InjectKey(key, false);
}

void InjectText(const char* text)
{
    if (!text) return;
    
    while (*text) {
        bool need_shift = false;
        KBD_KEYS key = ascii_to_key(*text, need_shift);
        
        if (key != KBD_NONE) {
            if (need_shift) {
                InjectKey(KBD_leftshift, true);
            }
            InjectKeyPress(key);
            if (need_shift) {
                InjectKey(KBD_leftshift, false);
            }
        }
        text++;
    }
}

void InjectBIOSKey(uint16_t code)
{
    BIOS_AddKeyToBuffer(code);
    s_input_stats.bios_keys_injected++;
}

void InjectKeyDelayed(int key, bool pressed, uint64_t delay_instructions)
{
    // This would need the current instruction count from Explorer
    // For now, store with absolute trigger time
    // The ProcessDelayedKeys function will handle it
    DelayedKeyEvent event;
    event.key = key;
    event.pressed = pressed;
    event.trigger_instruction = delay_instructions; // Will be made absolute by caller
    s_delayed_keys.push_back(event);
}

void ProcessDelayedKeys(uint64_t current_instruction)
{
    auto it = s_delayed_keys.begin();
    while (it != s_delayed_keys.end()) {
        if (current_instruction >= it->trigger_instruction) {
            InjectKey(it->key, it->pressed);
            it = s_delayed_keys.erase(it);
        } else {
            ++it;
        }
    }
    s_input_stats.pending_delayed_keys = s_delayed_keys.size();
}

int KeyNameToCode(const char* name)
{
    if (!name) return -1;
    
    for (const auto& entry : s_key_names) {
        if (entry.name == nullptr) break;
        if (strcasecmp(name, entry.name) == 0) {
            return entry.key;
        }
    }
    return -1;
}

const char* KeyCodeToName(int key)
{
    for (const auto& entry : s_key_names) {
        if (entry.name == nullptr) break;
        if (entry.key == key) {
            return entry.name;
        }
    }
    return nullptr;
}

// =============================================================================
// Mouse Implementation
// =============================================================================

void InjectMouseMove(float dx, float dy)
{
    // In headless mode, skip mouse events that would try to capture the window
    if (IsHeadless()) {
        s_input_stats.mouse_moves_injected++;
        return;
    }
    MOUSE_EventMoved(dx, dy, 0.0f, 0.0f);
    s_input_stats.mouse_moves_injected++;
}

void InjectMouseMoveTo(float x_abs, float y_abs)
{
    if (IsHeadless()) {
        s_input_stats.mouse_moves_injected++;
        return;
    }
    MOUSE_EventMoved(0.0f, 0.0f, x_abs, y_abs);
    s_input_stats.mouse_moves_injected++;
}

void InjectMouseButton(uint8_t button, bool pressed)
{
    if (IsHeadless()) {
        s_input_stats.mouse_buttons_injected++;
        return;
    }
    MOUSE_EventButton(static_cast<MouseButtonId>(button), pressed);
    s_input_stats.mouse_buttons_injected++;
}

void InjectMouseWheel(int16_t delta)
{
    if (IsHeadless()) {
        return;
    }
    MOUSE_EventWheel(static_cast<float>(delta));
}

void InjectMouseClick(uint8_t button, float x, float y)
{
    InjectMouseMoveTo(x, y);
    InjectMouseButton(button, true);
    InjectMouseButton(button, false);
}

// =============================================================================
// Stats
// =============================================================================

InputStats GetInputStats()
{
    return s_input_stats;
}

void ResetInputStats()
{
    s_input_stats = {};
}

// =============================================================================
// Random Input Generation
// =============================================================================
// Complete Action Space for Neural Network Control
// Full granularity: separate press/release for drag-drop and modifier combos
// =============================================================================

// All available keyboard keys (complete DOS keyboard)
static const KBD_KEYS ALL_KEYS[] = {
    // Numbers (0-9) -> indices 0-9
    KBD_0, KBD_1, KBD_2, KBD_3, KBD_4, KBD_5, KBD_6, KBD_7, KBD_8, KBD_9,
    
    // Letters (a-z) -> indices 10-35
    KBD_a, KBD_b, KBD_c, KBD_d, KBD_e, KBD_f, KBD_g, KBD_h, KBD_i, KBD_j,
    KBD_k, KBD_l, KBD_m, KBD_n, KBD_o, KBD_p, KBD_q, KBD_r, KBD_s, KBD_t,
    KBD_u, KBD_v, KBD_w, KBD_x, KBD_y, KBD_z,
    
    // Function keys (F1-F12) -> indices 36-47
    KBD_f1, KBD_f2, KBD_f3, KBD_f4, KBD_f5, KBD_f6,
    KBD_f7, KBD_f8, KBD_f9, KBD_f10, KBD_f11, KBD_f12,
    
    // Arrow keys -> indices 48-51
    KBD_up, KBD_down, KBD_left, KBD_right,
    
    // Common control keys -> indices 52-58
    KBD_esc, KBD_tab, KBD_backspace, KBD_enter, KBD_space,
    KBD_insert, KBD_delete,
    
    // Navigation -> indices 59-62
    KBD_home, KBD_end, KBD_pageup, KBD_pagedown,
    
    // Numpad -> indices 63-78
    KBD_kp0, KBD_kp1, KBD_kp2, KBD_kp3, KBD_kp4,
    KBD_kp5, KBD_kp6, KBD_kp7, KBD_kp8, KBD_kp9,
    KBD_kpenter, KBD_kpplus, KBD_kpminus, KBD_kpmultiply, KBD_kpdivide, KBD_kpperiod,
    
    // Punctuation/symbols -> indices 79-91
    KBD_minus, KBD_equals, KBD_backslash,
    KBD_leftbracket, KBD_rightbracket,
    KBD_semicolon, KBD_quote, KBD_grave,
    KBD_period, KBD_comma, KBD_slash,
    KBD_capslock, KBD_scrolllock,
};

constexpr size_t NUM_REGULAR_KEYS = sizeof(ALL_KEYS) / sizeof(ALL_KEYS[0]);

// Modifier keys (separate for hold/release tracking)
static const KBD_KEYS MODIFIER_KEYS[] = {
    KBD_leftshift, KBD_rightshift,
    KBD_leftctrl, KBD_rightctrl,
    KBD_leftalt, KBD_rightalt,
    KBD_numlock,
};
constexpr size_t NUM_MODIFIER_KEYS = sizeof(MODIFIER_KEYS) / sizeof(MODIFIER_KEYS[0]);

// =============================================================================
// Action Space Layout:
// 
// Section 1: Regular key PRESS (down+up) - quick tap
//   [0, NUM_REGULAR_KEYS-1] = 92 actions
//
// Section 2: Modifier key DOWN (hold)
//   [92, 92+NUM_MODIFIER_KEYS-1] = 7 actions (shift L/R, ctrl L/R, alt L/R, numlock)
//
// Section 3: Modifier key UP (release)
//   [99, 99+NUM_MODIFIER_KEYS-1] = 7 actions
//
// Section 4: Mouse movement (8 directions × 3 speeds = 24)
//   [106, 129] = 24 actions
//
// Section 5: Mouse button DOWN (for drag start)
//   [130, 132] = 3 actions (left, right, middle)
//
// Section 6: Mouse button UP (for drag end)
//   [133, 135] = 3 actions
//
// Section 7: Mouse button CLICK (quick press+release)
//   [136, 138] = 3 actions
//
// Section 8: Mouse button DOUBLE-CLICK
//   [139, 141] = 3 actions
//
// Section 9: Mouse wheel
//   [142, 143] = 2 actions (up, down)
//
// Section 10: NO_OP
//   [144] = 1 action
//
// TOTAL: 145 actions
// =============================================================================

enum class ActionSection : int {
    KeyPress = 0,
    ModifierDown = NUM_REGULAR_KEYS,                              // 92
    ModifierUp = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS,            // 99
    MouseMove = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2,         // 106
    MouseButtonDown = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2 + 24,  // 130
    MouseButtonUp = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2 + 27,    // 133
    MouseClick = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2 + 30,       // 136
    MouseDoubleClick = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2 + 33, // 139
    MouseWheel = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2 + 36,       // 142
    NoOp = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2 + 38,             // 144
    TotalActions = NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2 + 39      // 145
};

constexpr size_t TOTAL_ACTIONS = static_cast<size_t>(ActionSection::TotalActions);

// Mouse movement speeds
static const float MOUSE_SPEEDS[] = { 5.0f, 20.0f, 50.0f }; // slow, medium, fast
constexpr size_t NUM_MOUSE_SPEEDS = 3;

// Mouse movement directions (dx, dy multipliers)
static const float MOUSE_DIRECTIONS[][2] = {
    { 0.0f, -1.0f },    // Up
    { 0.0f, 1.0f },     // Down
    { -1.0f, 0.0f },    // Left
    { 1.0f, 0.0f },     // Right
    { -0.707f, -0.707f }, // Up-Left
    { 0.707f, -0.707f },  // Up-Right
    { -0.707f, 0.707f },  // Down-Left
    { 0.707f, 0.707f },   // Down-Right
};
constexpr size_t NUM_MOUSE_DIRECTIONS = 8;

// =============================================================================

void SetInputRandomSeed(uint32_t seed)
{
    s_rng.seed(seed);
}

size_t GetNumActions()
{
    return TOTAL_ACTIONS;
}

size_t GetNumKeyboardActions()
{
    return NUM_REGULAR_KEYS + NUM_MODIFIER_KEYS * 2; // press + modifier down + modifier up
}

size_t GetNumMouseActions()
{
    return 24 + 3 + 3 + 3 + 3 + 2; // move + button down + button up + click + dblclick + wheel
}

const char* GetActionName(size_t action_id)
{
    static char buffer[64];
    
    if (action_id >= TOTAL_ACTIONS) {
        return "INVALID";
    }
    
    // NO_OP
    if (action_id == static_cast<size_t>(ActionSection::NoOp)) {
        return "NO_OP";
    }
    
    // Regular key press
    if (action_id < NUM_REGULAR_KEYS) {
        const char* name = KeyCodeToName(ALL_KEYS[action_id]);
        return name ? name : "UNKNOWN_KEY";
    }
    
    // Modifier DOWN
    size_t section_start = static_cast<size_t>(ActionSection::ModifierDown);
    if (action_id >= section_start && action_id < section_start + NUM_MODIFIER_KEYS) {
        size_t mod_idx = action_id - section_start;
        static const char* mod_names[] = {
            "LSHIFT_DOWN", "RSHIFT_DOWN", "LCTRL_DOWN", "RCTRL_DOWN",
            "LALT_DOWN", "RALT_DOWN", "NUMLOCK_DOWN"
        };
        return mod_names[mod_idx];
    }
    
    // Modifier UP
    section_start = static_cast<size_t>(ActionSection::ModifierUp);
    if (action_id >= section_start && action_id < section_start + NUM_MODIFIER_KEYS) {
        size_t mod_idx = action_id - section_start;
        static const char* mod_names[] = {
            "LSHIFT_UP", "RSHIFT_UP", "LCTRL_UP", "RCTRL_UP",
            "LALT_UP", "RALT_UP", "NUMLOCK_UP"
        };
        return mod_names[mod_idx];
    }
    
    // Mouse movement
    section_start = static_cast<size_t>(ActionSection::MouseMove);
    if (action_id >= section_start && action_id < section_start + 24) {
        size_t move_idx = action_id - section_start;
        size_t dir = move_idx / NUM_MOUSE_SPEEDS;
        size_t speed = move_idx % NUM_MOUSE_SPEEDS;
        static const char* dir_names[] = {
            "UP", "DOWN", "LEFT", "RIGHT", "UP_LEFT", "UP_RIGHT", "DOWN_LEFT", "DOWN_RIGHT"
        };
        static const char* speed_names[] = { "SLOW", "MED", "FAST" };
        snprintf(buffer, sizeof(buffer), "MOUSE_%s_%s", dir_names[dir], speed_names[speed]);
        return buffer;
    }
    
    // Mouse button DOWN
    section_start = static_cast<size_t>(ActionSection::MouseButtonDown);
    if (action_id >= section_start && action_id < section_start + 3) {
        static const char* names[] = { "MOUSE_LEFT_DOWN", "MOUSE_RIGHT_DOWN", "MOUSE_MIDDLE_DOWN" };
        return names[action_id - section_start];
    }
    
    // Mouse button UP
    section_start = static_cast<size_t>(ActionSection::MouseButtonUp);
    if (action_id >= section_start && action_id < section_start + 3) {
        static const char* names[] = { "MOUSE_LEFT_UP", "MOUSE_RIGHT_UP", "MOUSE_MIDDLE_UP" };
        return names[action_id - section_start];
    }
    
    // Mouse CLICK
    section_start = static_cast<size_t>(ActionSection::MouseClick);
    if (action_id >= section_start && action_id < section_start + 3) {
        static const char* names[] = { "MOUSE_LEFT_CLICK", "MOUSE_RIGHT_CLICK", "MOUSE_MIDDLE_CLICK" };
        return names[action_id - section_start];
    }
    
    // Mouse DOUBLE-CLICK
    section_start = static_cast<size_t>(ActionSection::MouseDoubleClick);
    if (action_id >= section_start && action_id < section_start + 3) {
        static const char* names[] = { "MOUSE_LEFT_DBLCLICK", "MOUSE_RIGHT_DBLCLICK", "MOUSE_MIDDLE_DBLCLICK" };
        return names[action_id - section_start];
    }
    
    // Mouse wheel
    section_start = static_cast<size_t>(ActionSection::MouseWheel);
    if (action_id >= section_start && action_id < section_start + 2) {
        static const char* names[] = { "WHEEL_UP", "WHEEL_DOWN" };
        return names[action_id - section_start];
    }
    
    return "UNKNOWN";
}

void InjectAction(size_t action_id, float)
{
    if (action_id >= TOTAL_ACTIONS) {
        return;
    }
    
    // NO_OP
    if (action_id == static_cast<size_t>(ActionSection::NoOp)) {
        return;
    }
    
    // Regular key press (down + up)
    if (action_id < NUM_REGULAR_KEYS) {
        InjectKeyPress(ALL_KEYS[action_id]);
        return;
    }
    
    // Modifier DOWN
    size_t section_start = static_cast<size_t>(ActionSection::ModifierDown);
    if (action_id >= section_start && action_id < section_start + NUM_MODIFIER_KEYS) {
        InjectKey(MODIFIER_KEYS[action_id - section_start], true);
        return;
    }
    
    // Modifier UP
    section_start = static_cast<size_t>(ActionSection::ModifierUp);
    if (action_id >= section_start && action_id < section_start + NUM_MODIFIER_KEYS) {
        InjectKey(MODIFIER_KEYS[action_id - section_start], false);
        return;
    }
    
    // Mouse movement (8 directions × 3 speeds)
    section_start = static_cast<size_t>(ActionSection::MouseMove);
    if (action_id >= section_start && action_id < section_start + 24) {
        size_t move_idx = action_id - section_start;
        size_t dir = move_idx / NUM_MOUSE_SPEEDS;
        size_t speed_idx = move_idx % NUM_MOUSE_SPEEDS;
        float speed = MOUSE_SPEEDS[speed_idx];
        InjectMouseMove(MOUSE_DIRECTIONS[dir][0] * speed, MOUSE_DIRECTIONS[dir][1] * speed);
        return;
    }
    
    // Mouse button DOWN (for drag start)
    section_start = static_cast<size_t>(ActionSection::MouseButtonDown);
    if (action_id >= section_start && action_id < section_start + 3) {
        InjectMouseButton(static_cast<uint8_t>(action_id - section_start), true);
        return;
    }
    
    // Mouse button UP (for drag end)
    section_start = static_cast<size_t>(ActionSection::MouseButtonUp);
    if (action_id >= section_start && action_id < section_start + 3) {
        InjectMouseButton(static_cast<uint8_t>(action_id - section_start), false);
        return;
    }
    
    // Mouse CLICK (down + up)
    section_start = static_cast<size_t>(ActionSection::MouseClick);
    if (action_id >= section_start && action_id < section_start + 3) {
        uint8_t btn = static_cast<uint8_t>(action_id - section_start);
        InjectMouseButton(btn, true);
        InjectMouseButton(btn, false);
        return;
    }
    
    // Mouse DOUBLE-CLICK
    section_start = static_cast<size_t>(ActionSection::MouseDoubleClick);
    if (action_id >= section_start && action_id < section_start + 3) {
        uint8_t btn = static_cast<uint8_t>(action_id - section_start);
        InjectMouseButton(btn, true);
        InjectMouseButton(btn, false);
        InjectMouseButton(btn, true);
        InjectMouseButton(btn, false);
        return;
    }
    
    // Mouse wheel
    section_start = static_cast<size_t>(ActionSection::MouseWheel);
    if (action_id >= section_start && action_id < section_start + 2) {
        InjectMouseWheel(action_id == section_start ? 3 : -3);
        return;
    }
}

void InjectRandomKey()
{
    // Use ALL regular keys
    std::uniform_int_distribution<size_t> dist(0, NUM_REGULAR_KEYS - 1);
    KBD_KEYS key = ALL_KEYS[dist(s_rng)];
    InjectKeyPress(key);
}

void InjectRandomAction()
{
    // Random action from entire action space (including NO_OP)
    std::uniform_int_distribution<size_t> dist(0, TOTAL_ACTIONS - 1);
    InjectAction(dist(s_rng));
}

void InjectRandomMouseMove(float max_delta)
{
    std::uniform_real_distribution<float> dist(-max_delta, max_delta);
    InjectMouseMove(dist(s_rng), dist(s_rng));
}

void InjectRandomMouseClick()
{
    std::uniform_int_distribution<int> button_dist(0, 2);
    std::uniform_real_distribution<float> pos_dist(0.1f, 0.9f);
    
    uint8_t button = static_cast<uint8_t>(button_dist(s_rng));
    InjectMouseClick(button, pos_dist(s_rng), pos_dist(s_rng));
}

// Get action space info for NN training
ActionSpaceInfo GetActionSpaceInfo()
{
    ActionSpaceInfo info;
    info.total_actions = TOTAL_ACTIONS;
    info.num_key_press = NUM_REGULAR_KEYS;
    info.num_modifier_down = NUM_MODIFIER_KEYS;
    info.num_modifier_up = NUM_MODIFIER_KEYS;
    info.num_mouse_move = 24;  // 8 dirs × 3 speeds
    info.num_mouse_button_down = 3;
    info.num_mouse_button_up = 3;
    info.num_mouse_click = 3;
    info.num_mouse_dblclick = 3;
    info.num_mouse_wheel = 2;
    
    info.key_press_start = 0;
    info.modifier_down_start = static_cast<size_t>(ActionSection::ModifierDown);
    info.modifier_up_start = static_cast<size_t>(ActionSection::ModifierUp);
    info.mouse_move_start = static_cast<size_t>(ActionSection::MouseMove);
    info.mouse_button_down_start = static_cast<size_t>(ActionSection::MouseButtonDown);
    info.mouse_button_up_start = static_cast<size_t>(ActionSection::MouseButtonUp);
    info.mouse_click_start = static_cast<size_t>(ActionSection::MouseClick);
    info.mouse_dblclick_start = static_cast<size_t>(ActionSection::MouseDoubleClick);
    info.mouse_wheel_start = static_cast<size_t>(ActionSection::MouseWheel);
    info.noop_action = static_cast<size_t>(ActionSection::NoOp);
    
    return info;
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
