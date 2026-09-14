/*
 * Ergohaven-style RuEn: the keyboard tracks OS layout and sends the physical
 * key that produces the same character in EN and RU, temporarily switching
 * layout for symbols that exist in only one of them.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_ruen

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keys.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/ruen.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

enum ruen_lang { LANG_EN = 0, LANG_RU = 1 };
enum ruen_mode { MODE_DEFAULT = 0, MODE_M1M2 = 1 };

struct behavior_ruen_config {
    uint32_t toggle_key;
    uint32_t mac_toggle_key;
    uint32_t en_key;
    uint32_t ru_key;
    int tapping_term_ms;
    uint8_t default_mode;
};

static const struct behavior_ruen_config *cfg;
static struct zmk_behavior_binding_event last_event;

static uint8_t cur_lang = LANG_EN;
static uint8_t stored_lang = LANG_EN;
static uint8_t tg_mode = MODE_DEFAULT;
static bool mac_layout;
static bool should_revert_ru;
static bool should_revert_macro;
static bool english_word;
static bool in_ruen_send;
static int64_t mod_press_time;
static bool mod_held;
static uint8_t num_prev_lang;

#define RUEN_MAX_HELD 8
static struct {
    uint32_t position;
    uint32_t key;
    bool used;
} held[RUEN_MAX_HELD];

static const uint32_t en_only_keys[] = {
    LBKT, RBKT, LBRC, RBRC, LT, GT, GRAVE, TILDE, AT, HASH, DLLR, CARET, AMPS, PIPE, SQT,
};

static void revert_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(revert_work, revert_fn);

static uint32_t default_toggle_key(void) {
    if (mac_layout) {
        return cfg->mac_toggle_key ? cfg->mac_toggle_key : LC(SPACE);
    }
    return cfg->toggle_key ? cfg->toggle_key : LG(SPACE);
}

static uint32_t default_en_key(void) { return cfg->en_key ? cfg->en_key : LS(LC(N1)); }

static uint32_t default_ru_key(void) { return cfg->ru_key ? cfg->ru_key : LS(LC(N2)); }

static void send_key(uint32_t encoded, bool pressed, int64_t timestamp) {
    in_ruen_send = true;
    raise_zmk_keycode_state_changed_from_encoded(encoded, pressed, timestamp);
    in_ruen_send = false;
}

static void tap_key(uint32_t encoded, int64_t timestamp) {
    send_key(encoded, true, timestamp);
    send_key(encoded, false, timestamp);
}

static void tap_combo(uint32_t encoded, int64_t timestamp) {
    zmk_mod_flags_t held_mods = zmk_hid_get_explicit_mods();
    if (held_mods) {
        zmk_hid_masked_modifiers_set(held_mods);
    }
    tap_key(encoded, timestamp);
    if (held_mods) {
        zmk_hid_masked_modifiers_clear();
    }
}

static void hold_start(uint32_t position, uint32_t key, int64_t timestamp) {
    send_key(key, true, timestamp);
    for (int i = 0; i < RUEN_MAX_HELD; i++) {
        if (!held[i].used) {
            held[i].used = true;
            held[i].position = position;
            held[i].key = key;
            return;
        }
    }
}

static void hold_end(uint32_t position, int64_t timestamp) {
    for (int i = 0; i < RUEN_MAX_HELD; i++) {
        if (held[i].used && held[i].position == position) {
            send_key(held[i].key, false, timestamp);
            held[i].used = false;
            return;
        }
    }
}

static void set_lang(const struct zmk_behavior_binding_event *event, uint8_t lang) {
    last_event = *event;

    switch (tg_mode) {
    case MODE_DEFAULT:
        if (cur_lang == lang) {
            return;
        }
        tap_combo(default_toggle_key(), event->timestamp);
        break;
    case MODE_M1M2:
        tap_combo(lang == LANG_EN ? default_en_key() : default_ru_key(), event->timestamp);
        break;
    default:
        break;
    }

    cur_lang = lang;
    LOG_DBG("ruen lang=%s", lang == LANG_EN ? "EN" : "RU");
}

static void lang_toggle(const struct zmk_behavior_binding_event *event) {
    set_lang(event, cur_lang == LANG_EN ? LANG_RU : LANG_EN);
}

static void cancel_revert(void) {
    should_revert_ru = false;
    k_work_cancel_delayable(&revert_work);
}

static void arm_revert(const struct zmk_behavior_binding_event *event) {
    last_event = *event;
    should_revert_ru = true;
    k_work_reschedule(&revert_work, K_MSEC(CONFIG_ZMK_RUEN_REVERT_MS));
}

static void revert_fn(struct k_work *work) {
    ARG_UNUSED(work);
    if (should_revert_macro) {
        return;
    }
    if (should_revert_ru) {
        should_revert_ru = false;
        set_lang(&last_event, LANG_RU);
    }
}

static uint32_t bilingual_key(uint32_t code) {
    const bool ru = cur_lang == LANG_RU;
    switch (code) {
    case RUEN_DOT:
        return !ru ? DOT : (mac_layout ? LS(N7) : FSLH);
    case RUEN_COMMA:
        return !ru ? COMMA : (mac_layout ? LS(N6) : LS(FSLH));
    case RUEN_SEMI:
        return !ru ? SEMI : (mac_layout ? LS(N8) : LS(N4));
    case RUEN_COLON:
        return !ru ? COLON : (mac_layout ? LS(N5) : LS(N6));
    case RUEN_DQT:
        return !ru ? DQT : LS(N2);
    case RUEN_QMARK:
        return (!ru || mac_layout) ? QMARK : LS(N7);
    case RUEN_FSLH:
        return (!ru || mac_layout) ? FSLH : LS(BSLH);
    default:
        return 0;
    }
}

static uint32_t russian_letter_key(uint32_t code) {
    switch (code) {
    case RUEN_BE:
        return COMMA;
    case RUEN_YU:
        return DOT;
    case RUEN_ZHE:
        return SEMI;
    case RUEN_E:
        return SQT;
    case RUEN_KHA:
        return LBKT;
    case RUEN_HRD_SGN:
        return RBKT;
    case RUEN_YO:
        return GRAVE;
    default:
        return 0;
    }
}

static void press_with_optional_lang(const struct zmk_behavior_binding_event *event, uint8_t lang,
                                     uint32_t key) {
    if (cur_lang != lang) {
        set_lang(event, lang);
    }
    hold_start(event->position, key, event->timestamp);
}

static int on_ruen_pressed(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    const uint32_t code = binding->param1;
    last_event = event;

    if (code == RUEN_MOD) {
        lang_toggle(&event);
        mod_press_time = event.timestamp;
        mod_held = true;
        return ZMK_BEHAVIOR_OPAQUE;
    }

    switch (code) {
    case RUEN_TOGGLE:
        cancel_revert();
        lang_toggle(&event);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_SYNC:
        cur_lang = cur_lang == LANG_EN ? LANG_RU : LANG_EN;
        LOG_DBG("ruen sync lang=%s", cur_lang == LANG_EN ? "EN" : "RU");
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_EN:
        cancel_revert();
        set_lang(&event, LANG_EN);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_RU:
        cancel_revert();
        set_lang(&event, LANG_RU);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_M1M2:
        tg_mode = MODE_M1M2;
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_DFLT:
        tg_mode = MODE_DEFAULT;
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_DOT:
    case RUEN_COMMA:
    case RUEN_SEMI:
    case RUEN_COLON:
    case RUEN_DQT:
    case RUEN_QMARK:
    case RUEN_FSLH:
        hold_start(event.position, bilingual_key(code), event.timestamp);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_PRCNT:
        hold_start(event.position, (cur_lang == LANG_RU && mac_layout) ? LS(N4) : LS(N5),
                   event.timestamp);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_TG_MAC:
        mac_layout = !mac_layout;
        LOG_DBG("ruen mac_layout=%d", mac_layout);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_NUM:
        num_prev_lang = cur_lang;
        set_lang(&event, LANG_RU);
        hold_start(event.position, LS(N3), event.timestamp);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_WORD:
        if (cur_lang == LANG_RU && !english_word) {
            english_word = true;
            bool shift = (zmk_hid_get_explicit_mods() & (MOD_LSFT | MOD_RSFT)) != 0;
            set_lang(&event, LANG_EN);
            if (shift) {
                struct zmk_behavior_binding cw = {.behavior_dev = "caps_word"};
                zmk_behavior_invoke_binding(&cw, event, true);
                zmk_behavior_invoke_binding(&cw, event, false);
            }
        }
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_STORE:
        stored_lang = cur_lang;
        should_revert_macro = true;
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_REVERT:
        should_revert_macro = false;
        set_lang(&event, stored_lang);
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_BE:
    case RUEN_YU:
    case RUEN_ZHE:
    case RUEN_E:
    case RUEN_KHA:
    case RUEN_HRD_SGN:
    case RUEN_YO:
        if (cur_lang == LANG_RU) {
            hold_start(event.position, russian_letter_key(code), event.timestamp);
        }
        return ZMK_BEHAVIOR_OPAQUE;
    case RUEN_RUBLE: {
        uint8_t prev = cur_lang;
        if (prev != LANG_RU) {
            set_lang(&event, LANG_RU);
        }
        tap_key(RA(N8), event.timestamp);
        if (prev != LANG_RU) {
            set_lang(&event, prev);
        }
        return ZMK_BEHAVIOR_OPAQUE;
    }
    default:
        break;
    }

    if (code >= RUEN_EN_FIRST && code <= RUEN_EN_LAST) {
        uint8_t prev = cur_lang;
        press_with_optional_lang(&event, LANG_EN, en_only_keys[code - RUEN_EN_FIRST]);
        if (prev == LANG_RU) {
            arm_revert(&event);
        }
        return ZMK_BEHAVIOR_OPAQUE;
    }

    LOG_WRN("unknown ruen code %u", code);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_ruen_released(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    hold_end(event.position, event.timestamp);
    if (binding->param1 == RUEN_MOD && mod_held) {
        mod_held = false;
        int64_t held_ms = event.timestamp - mod_press_time;
        if (held_ms >= cfg->tapping_term_ms) {
            lang_toggle(&event);
        }
    }
    if (binding->param1 == RUEN_NUM && cur_lang != num_prev_lang) {
        set_lang(&event, num_prev_lang);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int ruen_keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state || in_ruen_send) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (english_word && ev->usage_page == HID_USAGE_KEY) {
        switch (ev->keycode) {
        case HID_USAGE_KEY_KEYBOARD_SPACEBAR:
        case HID_USAGE_KEY_KEYBOARD_RETURN_ENTER:
        case HID_USAGE_KEY_KEYBOARD_ESCAPE:
        case HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE:
            english_word = false;
            set_lang(&last_event, LANG_RU);
            break;
        default:
            break;
        }
    }

    if (should_revert_ru && ev->usage_page == HID_USAGE_KEY &&
        ev->keycode >= HID_USAGE_KEY_KEYBOARD_A && ev->keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        cancel_revert();
        set_lang(&last_event, LANG_RU);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ruen, ruen_keycode_listener);
ZMK_SUBSCRIPTION(ruen, zmk_keycode_state_changed);

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "Toggle", .value = RUEN_TOGGLE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Sync", .value = RUEN_SYNC, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "En", .value = RUEN_EN, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Ru", .value = RUEN_RU, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "M1M2", .value = RUEN_M1M2, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Dflt", .value = RUEN_DFLT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "DOT", .value = RUEN_DOT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "COMMA", .value = RUEN_COMMA, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "SEMI", .value = RUEN_SEMI, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "COLON", .value = RUEN_COLON, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "DQT", .value = RUEN_DQT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "QMARK", .value = RUEN_QMARK, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "SLASH", .value = RUEN_FSLH, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "LBKT", .value = RUEN_LBKT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "RBKT", .value = RUEN_RBKT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "LBRC", .value = RUEN_LBRC, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "RBRC", .value = RUEN_RBRC, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "LT", .value = RUEN_LT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "GT", .value = RUEN_GT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "GRAVE", .value = RUEN_GRAVE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "TILDE", .value = RUEN_TILDE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "AT", .value = RUEN_AT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "HASH", .value = RUEN_HASH, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "DLLR", .value = RUEN_DLLR, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "CARET", .value = RUEN_CARET, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "AMPS", .value = RUEN_AMPS, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "PIPE", .value = RUEN_PIPE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "SQT", .value = RUEN_SQT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "NUM", .value = RUEN_NUM, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "word", .value = RUEN_WORD, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Tg/Mod", .value = RUEN_MOD, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Store", .value = RUEN_STORE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Revert", .value = RUEN_REVERT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "PRCNT", .value = RUEN_PRCNT, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Mac Tg", .value = RUEN_TG_MAC, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Б", .value = RUEN_BE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Ю", .value = RUEN_YU, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Ж", .value = RUEN_ZHE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Э", .value = RUEN_E, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Х", .value = RUEN_KHA, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Ъ", .value = RUEN_HRD_SGN, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Ё", .value = RUEN_YO, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "RUBLE", .value = RUEN_RUBLE, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif

static const struct behavior_driver_api behavior_ruen_driver_api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = on_ruen_pressed,
    .binding_released = on_ruen_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

static int behavior_ruen_init(const struct device *dev) {
    cfg = dev->config;
    tg_mode = cfg->default_mode == MODE_M1M2 ? MODE_M1M2 : MODE_DEFAULT;
    return 0;
}

static const struct behavior_ruen_config behavior_ruen_config_0 = {
    .toggle_key = DT_INST_PROP_OR(0, toggle_key, 0),
    .mac_toggle_key = DT_INST_PROP_OR(0, mac_toggle_key, 0),
    .en_key = DT_INST_PROP_OR(0, en_key, 0),
    .ru_key = DT_INST_PROP_OR(0, ru_key, 0),
    .tapping_term_ms = DT_INST_PROP_OR(0, tapping_term_ms, 200),
    .default_mode = DT_INST_PROP_OR(0, default_mode, 0),
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_ruen_init, NULL, NULL, &behavior_ruen_config_0, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_ruen_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
