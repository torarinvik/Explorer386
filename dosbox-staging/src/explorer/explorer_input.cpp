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
// MAXIMUM GRANULARITY: Full key hold/release, fine mouse control, absolute positioning
// =============================================================================

// All available keyboard keys (complete DOS keyboard) - 93 keys
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
    
    // Modifiers (also in ALL_KEYS for full control) -> indices 92-98
    KBD_leftshift, KBD_rightshift,
    KBD_leftctrl, KBD_rightctrl,
    KBD_leftalt, KBD_rightalt,
    KBD_numlock,
};

constexpr size_t NUM_KEYS = sizeof(ALL_KEYS) / sizeof(ALL_KEYS[0]); // 99

// =============================================================================
// Action Space Layout (Total: 430 actions)
// 
// KEYBOARD ACTIONS:
//   Section 1: Key TAP (press+release)     [0, 98]       = 99 actions
//   Section 2: Key DOWN (hold start)       [99, 197]     = 99 actions
//   Section 3: Key UP (release)            [198, 296]    = 99 actions
//
// MOUSE MOVEMENT:
//   Section 4: Relative move (8 dirs × 5 speeds) [297, 336] = 40 actions
//   Section 5: Absolute position (15 regions)    [337, 351] = 15 actions
//
// MOUSE BUTTONS:
//   Section 6: Button DOWN                  [352, 354]    = 3 actions
//   Section 7: Button UP                    [355, 357]    = 3 actions
//   Section 8: Button CLICK                 [358, 360]    = 3 actions
//   Section 9: Button DOUBLE-CLICK          [361, 363]    = 3 actions
//   Section 10: Button TRIPLE-CLICK         [364, 366]    = 3 actions
//
// MOUSE WHEEL:
//   Section 11: Wheel (up/down × 3 amounts) [367, 372]    = 6 actions
//
// MOUSE DRAG COMBOS (convenience):
//   Section 12: Start drag (btn down + ready) [373, 375]  = 3 actions
//   Section 13: End drag (btn up)             [376, 378]  = 3 actions
//
// SPECIAL:
//   Section 14: NO_OP                       [379]         = 1 action
//   Section 15: RELEASE_ALL                 [380]         = 1 action
//
// TOTAL: 381 actions
// =============================================================================

constexpr size_t SEC_KEY_TAP = 0;
constexpr size_t SEC_KEY_DOWN = NUM_KEYS;                    // 99
constexpr size_t SEC_KEY_UP = NUM_KEYS * 2;                  // 198

constexpr size_t NUM_MOUSE_DIRS = 8;
constexpr size_t NUM_MOUSE_SPEEDS = 5;
constexpr size_t NUM_MOUSE_MOVES = NUM_MOUSE_DIRS * NUM_MOUSE_SPEEDS; // 40

constexpr size_t SEC_MOUSE_MOVE = NUM_KEYS * 3;              // 297
constexpr size_t NUM_MOUSE_POSITIONS = 15;
constexpr size_t SEC_MOUSE_ABS = SEC_MOUSE_MOVE + NUM_MOUSE_MOVES; // 337

constexpr size_t SEC_MOUSE_BTN_DOWN = SEC_MOUSE_ABS + NUM_MOUSE_POSITIONS; // 352
constexpr size_t SEC_MOUSE_BTN_UP = SEC_MOUSE_BTN_DOWN + 3;   // 355
constexpr size_t SEC_MOUSE_CLICK = SEC_MOUSE_BTN_UP + 3;      // 358
constexpr size_t SEC_MOUSE_DBLCLICK = SEC_MOUSE_CLICK + 3;    // 361
constexpr size_t SEC_MOUSE_TRICLICK = SEC_MOUSE_DBLCLICK + 3; // 364

constexpr size_t SEC_WHEEL = SEC_MOUSE_TRICLICK + 3;          // 367
constexpr size_t NUM_WHEEL_ACTIONS = 6;                        // up/down × small/med/large

constexpr size_t SEC_DRAG_START = SEC_WHEEL + NUM_WHEEL_ACTIONS; // 373
constexpr size_t SEC_DRAG_END = SEC_DRAG_START + 3;           // 376

constexpr size_t SEC_NOOP = SEC_DRAG_END + 3;                 // 379
constexpr size_t SEC_RELEASE_ALL = SEC_NOOP + 1;              // 380

constexpr size_t TOTAL_ACTIONS = SEC_RELEASE_ALL + 1;         // 381

// Mouse movement speeds (pixels)
static const float MOUSE_SPEEDS[] = { 1.0f, 5.0f, 15.0f, 40.0f, 100.0f }; // micro, slow, medium, fast, jump

// Mouse movement directions (dx, dy multipliers)
static const float MOUSE_DIRECTIONS[][2] = {
    { 0.0f, -1.0f },      // Up
    { 0.0f, 1.0f },       // Down
    { -1.0f, 0.0f },      // Left
    { 1.0f, 0.0f },       // Right
    { -0.707f, -0.707f }, // Up-Left
    { 0.707f, -0.707f },  // Up-Right
    { -0.707f, 0.707f },  // Down-Left
    { 0.707f, 0.707f },   // Down-Right
};

// Absolute mouse positions (normalized 0-1 coordinates)
// 15 positions: 3x3 grid + 4 edge centers + 2 special (center-left, center-right for menus)
static const float MOUSE_POSITIONS[][2] = {
    // 3x3 grid corners and center
    { 0.1f, 0.1f },   // Top-left
    { 0.5f, 0.1f },   // Top-center
    { 0.9f, 0.1f },   // Top-right
    { 0.1f, 0.5f },   // Middle-left
    { 0.5f, 0.5f },   // Center
    { 0.9f, 0.5f },   // Middle-right
    { 0.1f, 0.9f },   // Bottom-left
    { 0.5f, 0.9f },   // Bottom-center
    { 0.9f, 0.9f },   // Bottom-right
    // Edge midpoints (for scrollbars, toolbars)
    { 0.5f, 0.05f },  // Top edge
    { 0.5f, 0.95f },  // Bottom edge
    { 0.05f, 0.5f },  // Left edge
    { 0.95f, 0.5f },  // Right edge
    // Quarter positions (for dialog buttons)
    { 0.25f, 0.75f }, // Lower-left quarter (Cancel button area)
    { 0.75f, 0.75f }, // Lower-right quarter (OK button area)
};

// Wheel scroll amounts
static const int16_t WHEEL_AMOUNTS[] = { 1, 3, 10 }; // single line, few lines, page

// Track held keys for RELEASE_ALL
static bool s_held_keys[NUM_KEYS] = {false};
static bool s_held_mouse_buttons[3] = {false};

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
    return NUM_KEYS * 3; // tap + down + up
}

size_t GetNumMouseActions()
{
    return TOTAL_ACTIONS - NUM_KEYS * 3 - 2; // everything except keyboard and special
}

static const char* GetKeyNameSafe(size_t key_idx)
{
    if (key_idx >= NUM_KEYS) return "UNKNOWN";
    const char* name = KeyCodeToName(ALL_KEYS[key_idx]);
    return name ? name : "UNKNOWN";
}

const char* GetActionName(size_t action_id)
{
    static char buffer[64];
    
    if (action_id >= TOTAL_ACTIONS) {
        return "INVALID";
    }
    
    // Key TAP
    if (action_id < SEC_KEY_DOWN) {
        snprintf(buffer, sizeof(buffer), "TAP_%s", GetKeyNameSafe(action_id));
        return buffer;
    }
    
    // Key DOWN
    if (action_id < SEC_KEY_UP) {
        snprintf(buffer, sizeof(buffer), "DOWN_%s", GetKeyNameSafe(action_id - SEC_KEY_DOWN));
        return buffer;
    }
    
    // Key UP
    if (action_id < SEC_MOUSE_MOVE) {
        snprintf(buffer, sizeof(buffer), "UP_%s", GetKeyNameSafe(action_id - SEC_KEY_UP));
        return buffer;
    }
    
    // Mouse relative move
    if (action_id < SEC_MOUSE_ABS) {
        size_t move_idx = action_id - SEC_MOUSE_MOVE;
        size_t dir = move_idx / NUM_MOUSE_SPEEDS;
        size_t speed = move_idx % NUM_MOUSE_SPEEDS;
        static const char* dir_names[] = {
            "UP", "DOWN", "LEFT", "RIGHT", "UPLEFT", "UPRIGHT", "DOWNLEFT", "DOWNRIGHT"
        };
        static const char* speed_names[] = { "MICRO", "SLOW", "MED", "FAST", "JUMP" };
        snprintf(buffer, sizeof(buffer), "MOUSE_%s_%s", dir_names[dir], speed_names[speed]);
        return buffer;
    }
    
    // Mouse absolute position
    if (action_id < SEC_MOUSE_BTN_DOWN) {
        size_t pos_idx = action_id - SEC_MOUSE_ABS;
        static const char* pos_names[] = {
            "POS_TOPLEFT", "POS_TOPCENTER", "POS_TOPRIGHT",
            "POS_MIDLEFT", "POS_CENTER", "POS_MIDRIGHT",
            "POS_BOTLEFT", "POS_BOTCENTER", "POS_BOTRIGHT",
            "POS_TOPEDGE", "POS_BOTEDGE", "POS_LEFTEDGE", "POS_RIGHTEDGE",
            "POS_CANCEL", "POS_OK"
        };
        return pos_names[pos_idx];
    }
    
    // Mouse button DOWN
    if (action_id < SEC_MOUSE_BTN_UP) {
        static const char* names[] = { "MOUSE_L_DOWN", "MOUSE_R_DOWN", "MOUSE_M_DOWN" };
        return names[action_id - SEC_MOUSE_BTN_DOWN];
    }
    
    // Mouse button UP
    if (action_id < SEC_MOUSE_CLICK) {
        static const char* names[] = { "MOUSE_L_UP", "MOUSE_R_UP", "MOUSE_M_UP" };
        return names[action_id - SEC_MOUSE_BTN_UP];
    }
    
    // Mouse CLICK
    if (action_id < SEC_MOUSE_DBLCLICK) {
        static const char* names[] = { "CLICK_L", "CLICK_R", "CLICK_M" };
        return names[action_id - SEC_MOUSE_CLICK];
    }
    
    // Mouse DOUBLE-CLICK
    if (action_id < SEC_MOUSE_TRICLICK) {
        static const char* names[] = { "DBLCLICK_L", "DBLCLICK_R", "DBLCLICK_M" };
        return names[action_id - SEC_MOUSE_DBLCLICK];
    }
    
    // Mouse TRIPLE-CLICK
    if (action_id < SEC_WHEEL) {
        static const char* names[] = { "TRICLICK_L", "TRICLICK_R", "TRICLICK_M" };
        return names[action_id - SEC_MOUSE_TRICLICK];
    }
    
    // Mouse wheel
    if (action_id < SEC_DRAG_START) {
        size_t wheel_idx = action_id - SEC_WHEEL;
        static const char* names[] = {
            "WHEEL_UP_SMALL", "WHEEL_UP_MED", "WHEEL_UP_LARGE",
            "WHEEL_DOWN_SMALL", "WHEEL_DOWN_MED", "WHEEL_DOWN_LARGE"
        };
        return names[wheel_idx];
    }
    
    // Drag start
    if (action_id < SEC_DRAG_END) {
        static const char* names[] = { "DRAG_START_L", "DRAG_START_R", "DRAG_START_M" };
        return names[action_id - SEC_DRAG_START];
    }
    
    // Drag end
    if (action_id < SEC_NOOP) {
        static const char* names[] = { "DRAG_END_L", "DRAG_END_R", "DRAG_END_M" };
        return names[action_id - SEC_DRAG_END];
    }
    
    // Special actions
    if (action_id == SEC_NOOP) return "NO_OP";
    if (action_id == SEC_RELEASE_ALL) return "RELEASE_ALL";
    
    return "UNKNOWN";
}

void InjectAction(size_t action_id, float)
{
    if (action_id >= TOTAL_ACTIONS) {
        return;
    }
    
    // Key TAP (press + release)
    if (action_id < SEC_KEY_DOWN) {
        InjectKeyPress(ALL_KEYS[action_id]);
        return;
    }
    
    // Key DOWN (hold start)
    if (action_id < SEC_KEY_UP) {
        size_t key_idx = action_id - SEC_KEY_DOWN;
        InjectKey(ALL_KEYS[key_idx], true);
        s_held_keys[key_idx] = true;
        return;
    }
    
    // Key UP (release)
    if (action_id < SEC_MOUSE_MOVE) {
        size_t key_idx = action_id - SEC_KEY_UP;
        InjectKey(ALL_KEYS[key_idx], false);
        s_held_keys[key_idx] = false;
        return;
    }
    
    // Mouse relative movement (8 directions × 5 speeds)
    if (action_id < SEC_MOUSE_ABS) {
        size_t move_idx = action_id - SEC_MOUSE_MOVE;
        size_t dir = move_idx / NUM_MOUSE_SPEEDS;
        size_t speed_idx = move_idx % NUM_MOUSE_SPEEDS;
        float speed = MOUSE_SPEEDS[speed_idx];
        InjectMouseMove(MOUSE_DIRECTIONS[dir][0] * speed, MOUSE_DIRECTIONS[dir][1] * speed);
        return;
    }
    
    // Mouse absolute position
    if (action_id < SEC_MOUSE_BTN_DOWN) {
        size_t pos_idx = action_id - SEC_MOUSE_ABS;
        InjectMouseMoveTo(MOUSE_POSITIONS[pos_idx][0], MOUSE_POSITIONS[pos_idx][1]);
        return;
    }
    
    // Mouse button DOWN
    if (action_id < SEC_MOUSE_BTN_UP) {
        uint8_t btn = static_cast<uint8_t>(action_id - SEC_MOUSE_BTN_DOWN);
        InjectMouseButton(btn, true);
        s_held_mouse_buttons[btn] = true;
        return;
    }
    
    // Mouse button UP
    if (action_id < SEC_MOUSE_CLICK) {
        uint8_t btn = static_cast<uint8_t>(action_id - SEC_MOUSE_BTN_UP);
        InjectMouseButton(btn, false);
        s_held_mouse_buttons[btn] = false;
        return;
    }
    
    // Mouse CLICK (down + up)
    if (action_id < SEC_MOUSE_DBLCLICK) {
        uint8_t btn = static_cast<uint8_t>(action_id - SEC_MOUSE_CLICK);
        InjectMouseButton(btn, true);
        InjectMouseButton(btn, false);
        return;
    }
    
    // Mouse DOUBLE-CLICK
    if (action_id < SEC_MOUSE_TRICLICK) {
        uint8_t btn = static_cast<uint8_t>(action_id - SEC_MOUSE_DBLCLICK);
        InjectMouseButton(btn, true);
        InjectMouseButton(btn, false);
        InjectMouseButton(btn, true);
        InjectMouseButton(btn, false);
        return;
    }
    
    // Mouse TRIPLE-CLICK
    if (action_id < SEC_WHEEL) {
        uint8_t btn = static_cast<uint8_t>(action_id - SEC_MOUSE_TRICLICK);
        for (int i = 0; i < 3; i++) {
            InjectMouseButton(btn, true);
            InjectMouseButton(btn, false);
        }
        return;
    }
    
    // Mouse wheel (up/down × 3 amounts)
    if (action_id < SEC_DRAG_START) {
        size_t wheel_idx = action_id - SEC_WHEEL;
        bool is_up = wheel_idx < 3;
        size_t amount_idx = wheel_idx % 3;
        int16_t amount = WHEEL_AMOUNTS[amount_idx];
        InjectMouseWheel(is_up ? amount : -amount);
        return;
    }
    
    // Drag start (button down, ready for move)
    if (action_id < SEC_DRAG_END) {
        uint8_t btn = static_cast<uint8_t>(action_id - SEC_DRAG_START);
        InjectMouseButton(btn, true);
        s_held_mouse_buttons[btn] = true;
        return;
    }
    
    // Drag end (button up)
    if (action_id < SEC_NOOP) {
        uint8_t btn = static_cast<uint8_t>(action_id - SEC_DRAG_END);
        InjectMouseButton(btn, false);
        s_held_mouse_buttons[btn] = false;
        return;
    }
    
    // NO_OP
    if (action_id == SEC_NOOP) {
        return;
    }
    
    // RELEASE_ALL - release all held keys and mouse buttons
    if (action_id == SEC_RELEASE_ALL) {
        for (size_t i = 0; i < NUM_KEYS; i++) {
            if (s_held_keys[i]) {
                InjectKey(ALL_KEYS[i], false);
                s_held_keys[i] = false;
            }
        }
        for (int i = 0; i < 3; i++) {
            if (s_held_mouse_buttons[i]) {
                InjectMouseButton(static_cast<uint8_t>(i), false);
                s_held_mouse_buttons[i] = false;
            }
        }
        return;
    }
}

void InjectRandomKey()
{
    std::uniform_int_distribution<size_t> dist(0, NUM_KEYS - 1);
    KBD_KEYS key = ALL_KEYS[dist(s_rng)];
    InjectKeyPress(key);
}

void InjectRandomAction()
{
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
    ActionSpaceInfo info = {};
    info.total_actions = TOTAL_ACTIONS;
    
    // Keyboard
    info.num_keys = NUM_KEYS;
    info.key_tap_start = SEC_KEY_TAP;
    info.key_down_start = SEC_KEY_DOWN;
    info.key_up_start = SEC_KEY_UP;
    
    // Mouse movement
    info.num_mouse_dirs = NUM_MOUSE_DIRS;
    info.num_mouse_speeds = NUM_MOUSE_SPEEDS;
    info.mouse_move_start = SEC_MOUSE_MOVE;
    info.num_mouse_positions = NUM_MOUSE_POSITIONS;
    info.mouse_abs_start = SEC_MOUSE_ABS;
    
    // Mouse buttons
    info.mouse_btn_down_start = SEC_MOUSE_BTN_DOWN;
    info.mouse_btn_up_start = SEC_MOUSE_BTN_UP;
    info.mouse_click_start = SEC_MOUSE_CLICK;
    info.mouse_dblclick_start = SEC_MOUSE_DBLCLICK;
    info.mouse_triclick_start = SEC_MOUSE_TRICLICK;
    
    // Mouse wheel
    info.num_wheel_amounts = 3;
    info.wheel_start = SEC_WHEEL;
    
    // Drag
    info.drag_start_start = SEC_DRAG_START;
    info.drag_end_start = SEC_DRAG_END;
    
    // Special
    info.noop_action = SEC_NOOP;
    info.release_all_action = SEC_RELEASE_ALL;
    
    return info;
}

} // namespace Explorer

#endif // EXPLORER_ENABLED
