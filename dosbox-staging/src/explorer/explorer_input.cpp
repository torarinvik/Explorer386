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

void SetInputRandomSeed(uint32_t seed)
{
    s_rng.seed(seed);
}

void InjectRandomKey()
{
    // List of commonly useful keys for games
    static const KBD_KEYS game_keys[] = {
        KBD_up, KBD_down, KBD_left, KBD_right,
        KBD_space, KBD_enter, KBD_esc,
        KBD_a, KBD_s, KBD_d, KBD_w,
        KBD_leftctrl, KBD_leftalt, KBD_leftshift,
        KBD_1, KBD_2, KBD_3, KBD_4, KBD_5,
        KBD_f1, KBD_f2, KBD_f3, KBD_f4, KBD_f5,
        KBD_y, KBD_n,  // Yes/No prompts
    };
    
    std::uniform_int_distribution<size_t> dist(0, sizeof(game_keys)/sizeof(game_keys[0]) - 1);
    KBD_KEYS key = game_keys[dist(s_rng)];
    InjectKeyPress(key);
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

} // namespace Explorer

#endif // EXPLORER_ENABLED
