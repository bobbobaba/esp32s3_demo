#pragma once

#include <cstddef>
#include "lvgl.h"

// This project-owned Source Han subset covers the watch UI and its cloud
// status text.  The upstream LVGL subset does not include enough simplified
// Chinese glyphs for installed-app names and live server responses.
LV_FONT_DECLARE(lv_font_watch_zh_14);

namespace watch_display {

inline bool text_has_non_ascii(const char *text)
{
    if (text == nullptr) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        if (*p >= 0x80) return true;
    }
    return false;
}

inline const lv_font_t *font_for_text(const char *text, const lv_font_t *latin_font)
{
    if (!text_has_non_ascii(text)) return latin_font;
    return &lv_font_watch_zh_14;
}

inline void apply_text_font(lv_obj_t *label, const char *text, const lv_font_t *latin_font)
{
    if (label != nullptr) lv_obj_set_style_text_font(label, font_for_text(text, latin_font), 0);
}

inline void set_label_text(lv_obj_t *label, const char *text, const lv_font_t *latin_font)
{
    if (label == nullptr) return;
    const char *safe_text = text != nullptr ? text : "";
    lv_label_set_text(label, safe_text);
    apply_text_font(label, safe_text, latin_font);
}

} // namespace watch_display
