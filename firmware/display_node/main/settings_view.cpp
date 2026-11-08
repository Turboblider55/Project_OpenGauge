// A beállítások képernyője. Minden beállításhoz egy sor tartozik: felirat és vezérlő (kapcsoló, léptető vagy váltógomb).
#include "settings_view.h"

#include <cstdio>
#include <cstring>

#include "og_fonts.h"

namespace og {

namespace {

enum class Field : uint8_t {
    Music, Debug, MaxRpm, Redline, MaxSpeed, Sleep, Palette, SkipLogo, Brightness, SpeedUnit, TempUnit, AutoSwitch, SwitchPeriod,
    Count
};

enum class Kind : uint8_t { Switch, Stepper, Cycle };

struct FieldInfo {
    Field field;
    Kind kind;
    const char* label;
};

const FieldInfo kFields[] = {
    {Field::Music, Kind::Switch, "Zene képernyő"},
    {Field::Debug, Kind::Switch, "Debug mód"},
    {Field::MaxRpm, Kind::Stepper, "Max fordulatszám (1/perc)"},
    {Field::Redline, Kind::Stepper, "Redline kezdete (1/perc)"},
    {Field::MaxSpeed, Kind::Stepper, "Max sebesség (km/h)"},
    {Field::Sleep, Kind::Stepper, "Elalvási idő (s)"},
    {Field::Palette, Kind::Cycle, "Színpaletta"},
    {Field::SkipLogo, Kind::Switch, "Logó kihagyása"},
    {Field::Brightness, Kind::Stepper, "Fényerő (%)"},
    {Field::SpeedUnit, Kind::Cycle, "Sebesség egysége"},
    {Field::TempUnit, Kind::Cycle, "Hőmérséklet egysége"},
    {Field::AutoSwitch, Kind::Switch, "Automatikus lapozás"},
    {Field::SwitchPeriod, Kind::Stepper, "Lapozás ideje (s)"},
};
constexpr int kFieldCount = sizeof kFields / sizeof kFields[0];

constexpr int32_t kRowWidth = 250;

}  // namespace

// A nyilvános osztály a lv_event-ek C visszahívásai miatt egy segédstruktúrán keresztül éri el a belső állapotot.
struct SettingsViewImpl {
    struct Ctx {
        SettingsView* view;
        Field field;
        lv_obj_t* widget;  // kapcsoló, vagy az érték feliratú címke
        int dir;           // léptetőnél: -1 / +1
    };

    static inline Ctx ctx[kFieldCount * 3];
    static inline int ctx_n = 0;
    static inline UiColors colors;
    static inline lv_obj_t* widgets[kFieldCount];  // soronként a kapcsoló / érték-címke (open() frissíti)

    // Mit csinál: egy mező értékének szövege a léptető / váltógomb feliratához.
    static void text_of(const Settings& s, Field f, char* b, size_t cap) {
        switch (f) {
            case Field::MaxRpm: std::snprintf(b, cap, "%u", s.max_rpm); break;
            case Field::Redline: std::snprintf(b, cap, "%u", s.redline_rpm); break;
            case Field::MaxSpeed: std::snprintf(b, cap, "%u", s.max_speed_kmh); break;
            case Field::Sleep:
                if (s.sleep_s == 0) std::snprintf(b, cap, "gyári");
                else std::snprintf(b, cap, "%u", s.sleep_s);
                break;
            case Field::Palette: std::snprintf(b, cap, "%s", palette_get(s.palette).name); break;
            case Field::Brightness: std::snprintf(b, cap, "%u", s.brightness_pct); break;
            case Field::SpeedUnit: std::snprintf(b, cap, "%s", s.speed_unit == SpeedUnit::Mph ? "mph" : "km/h"); break;
            case Field::TempUnit: std::snprintf(b, cap, "%s", s.temp_unit == TempUnit::Fahrenheit ? "°F" : "°C"); break;
            case Field::SwitchPeriod: std::snprintf(b, cap, "%u", s.switch_period_s); break;
            default: b[0] = 0; break;
        }
    }

    static bool flag_of(const Settings& s, Field f) {
        switch (f) {
            case Field::Music: return s.music_enabled;
            case Field::Debug: return s.debug_mode;
            case Field::SkipLogo: return s.skip_logo;
            case Field::AutoSwitch: return s.auto_switch;
            default: return false;
        }
    }

    static void set_flag(Settings& s, Field f, bool v) {
        switch (f) {
            case Field::Music: s.music_enabled = v; break;
            case Field::Debug: s.debug_mode = v; break;
            case Field::SkipLogo: s.skip_logo = v; break;
            case Field::AutoSwitch: s.auto_switch = v; break;
            default: break;
        }
    }

    // Mit csinál: egy léptető mezőt a megadott irányba lép (a határokon megáll), a redline-t a skála végéhez igazítja.
    static void step(Settings& s, Field f, int dir) {
        auto stepped = [&](int v, int lo, int hi, int by) { return std::clamp(v + dir * by, lo, hi); };
        switch (f) {
            case Field::MaxRpm:
                s.max_rpm = static_cast<uint16_t>(stepped(s.max_rpm, 3000, 12000, 100));
                if (s.redline_rpm > s.max_rpm) s.redline_rpm = s.max_rpm;
                break;
            case Field::Redline: s.redline_rpm = static_cast<uint16_t>(stepped(s.redline_rpm, 2000, s.max_rpm, 100)); break;
            case Field::MaxSpeed: s.max_speed_kmh = static_cast<uint16_t>(stepped(s.max_speed_kmh, 100, 400, 10)); break;
            case Field::Sleep: {
                const int v = stepped(s.sleep_s, 0, 3600, 10);
                s.sleep_s = static_cast<uint16_t>(v < 10 ? 0 : v);  // 0: gyári érték
                break;
            }
            case Field::Brightness: s.brightness_pct = static_cast<uint8_t>(stepped(s.brightness_pct, 5, 100, 5)); break;
            case Field::SwitchPeriod: s.switch_period_s = static_cast<uint16_t>(stepped(s.switch_period_s, 5, 300, 5)); break;
            default: break;
        }
    }

    static void cycle(Settings& s, Field f) {
        switch (f) {
            case Field::Palette: s.palette = static_cast<uint8_t>((s.palette + 1) % kPaletteCount); break;
            case Field::SpeedUnit: s.speed_unit = s.speed_unit == SpeedUnit::Kmh ? SpeedUnit::Mph : SpeedUnit::Kmh; break;
            case Field::TempUnit: s.temp_unit = s.temp_unit == TempUnit::Celsius ? TempUnit::Fahrenheit : TempUnit::Celsius; break;
            default: break;
        }
    }

    // Mit csinál: a léptető, váltó és kapcsoló gombok eseménykezelői.
    static void on_switch(lv_event_t* e) {
        Ctx* c = static_cast<Ctx*>(lv_event_get_user_data(e));
        set_flag(c->view->edit_, c->field, lv_obj_has_state(c->widget, LV_STATE_CHECKED));
    }

    static void refresh_labels(SettingsView* v) {
        char b[24];
        if (v->back_label_) {  // a bájtformátumot hasonlítjuk (a struktúra kitöltő bájtjai nem számítanak)
            uint8_t x[kSettingsBlobSize], y[kSettingsBlobSize];
            settings_encode(v->edit_, x, sizeof x);
            settings_encode(v->orig_, y, sizeof y);
            lv_label_set_text(v->back_label_, std::memcmp(x, y, sizeof x) != 0 ? "ALKALMAZ" : "VISSZA");
        }
        for (int i = 0; i < kFieldCount; ++i) {
            if (kFields[i].kind == Kind::Switch) {
                lv_obj_set_state(widgets[i], LV_STATE_CHECKED, flag_of(v->edit_, kFields[i].field));
            } else {
                text_of(v->edit_, kFields[i].field, b, sizeof b);
                lv_label_set_text(widgets[i], b);
            }
        }
    }

    static void on_step(lv_event_t* e) {
        Ctx* c = static_cast<Ctx*>(lv_event_get_user_data(e));
        step(c->view->edit_, c->field, c->dir);
        refresh_labels(c->view);
    }

    static void on_cycle(lv_event_t* e) {
        Ctx* c = static_cast<Ctx*>(lv_event_get_user_data(e));
        cycle(c->view->edit_, c->field);
        refresh_labels(c->view);
    }

    static void on_back(lv_event_t* e) {
        SettingsView* v = static_cast<SettingsView*>(lv_event_get_user_data(e));
        Settings out = v->edit_;
        sanitize(out);
        if (v->on_exit_) v->on_exit_(out);
    }

    // Mit csinál: jobbra húzásra visszalép (mint a VISSZA gomb).
    static void on_gesture(lv_event_t* e) {
        lv_indev_t* in = lv_indev_active();
        if (in && lv_indev_get_gesture_dir(in) == LV_DIR_RIGHT) on_back(e);
    }

    static lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
        lv_obj_t* l = lv_label_create(parent);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, color, 0);
        lv_label_set_text(l, text);
        lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
        return l;
    }

    static lv_obj_t* make_button(lv_obj_t* parent, int32_t w, int32_t h, const char* text, const lv_font_t* font) {
        lv_obj_t* b = lv_button_create(parent);
        lv_obj_set_size(b, w, h);
        lv_obj_set_style_bg_color(b, colors.track, 0);
        lv_obj_set_style_bg_color(b, colors.accent, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_t* l = make_label(b, font, colors.text, text);
        lv_obj_center(l);
        return b;
    }

    static lv_obj_t* make_flex(lv_obj_t* parent, lv_flex_flow_t flow, int32_t w) {
        lv_obj_t* o = lv_obj_create(parent);
        lv_obj_set_size(o, w, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_pad_all(o, 0, 0);
        lv_obj_set_style_pad_gap(o, 6, 0);
        lv_obj_set_flex_flow(o, flow);
        lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
        return o;
    }

    static Ctx* new_ctx(SettingsView* v, Field f, lv_obj_t* w, int dir) {
        Ctx* c = &ctx[ctx_n++];
        *c = {v, f, w, dir};
        return c;
    }

    static void build_row(SettingsView* v, int i) {
        const FieldInfo& fi = kFields[i];
        lv_obj_t* row = make_flex(v->list_, LV_FLEX_FLOW_COLUMN, kRowWidth);
        lv_obj_t* name = make_label(row, &og_font_montserrat_20, colors.dim, fi.label);
        lv_obj_set_width(name, kRowWidth);  // a hosszú felirat körbe görög, nem lóg ki
        lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t* line = make_flex(row, LV_FLEX_FLOW_ROW, kRowWidth);
        switch (fi.kind) {
            case Kind::Switch: {
                lv_obj_t* sw = lv_switch_create(line);
                lv_obj_set_style_bg_color(sw, colors.track, LV_PART_MAIN);
                lv_obj_set_style_bg_color(sw, colors.accent, static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
                lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
                lv_obj_add_event_cb(sw, on_switch, LV_EVENT_VALUE_CHANGED, new_ctx(v, fi.field, sw, 0));
                widgets[i] = sw;
                break;
            }
            case Kind::Stepper: {
                lv_obj_t* minus = make_button(line, 64, 44, "-", &og_font_montserrat_28);
                lv_obj_t* val = make_label(line, &og_font_montserrat_28, colors.text, "");
                lv_obj_set_width(val, 100);
                lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_t* plus = make_button(line, 64, 44, "+", &og_font_montserrat_28);
                for (lv_obj_t* b : {minus, plus}) {
                    Ctx* c = new_ctx(v, fi.field, val, b == minus ? -1 : +1);
                    lv_obj_add_event_cb(b, on_step, LV_EVENT_SHORT_CLICKED, c);
                    lv_obj_add_event_cb(b, on_step, LV_EVENT_LONG_PRESSED_REPEAT, c);
                }
                widgets[i] = val;
                break;
            }
            case Kind::Cycle: {
                lv_obj_t* b = make_button(line, kRowWidth - 40, 44, "", &og_font_montserrat_28);
                lv_obj_t* label = lv_obj_get_child(b, 0);
                lv_obj_add_event_cb(b, on_cycle, LV_EVENT_CLICKED, new_ctx(v, fi.field, label, 0));
                widgets[i] = label;
                break;
            }
        }
    }
};

void SettingsView::build(const UiColors& c, ExitFn on_exit) {
    using I = SettingsViewImpl;
    I::colors = c;
    I::ctx_n = 0;
    on_exit_ = on_exit;
    scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, c.bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, I::on_gesture, LV_EVENT_GESTURE, this);

    list_ = lv_obj_create(scr);
    lv_obj_set_size(list_, 360, 360);
    lv_obj_center(list_);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    lv_obj_set_style_radius(list_, 0, 0);
    lv_obj_set_style_pad_top(list_, 70, 0);     // a kerek kijelző sarkai miatt a lista szélén üres hely kell
    lv_obj_set_style_pad_bottom(list_, 80, 0);
    lv_obj_set_style_pad_row(list_, 22, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_EVENT_BUBBLE);

    I::make_label(list_, &og_font_montserrat_28, c.accent, "BEÁLLÍTÁSOK");
    for (int i = 0; i < kFieldCount; ++i) I::build_row(this, i);
    lv_obj_t* back = I::make_button(list_, kRowWidth - 40, 48, "VISSZA", &og_font_montserrat_28);
    back_label_ = lv_obj_get_child(back, 0);
    lv_obj_add_event_cb(back, I::on_back, LV_EVENT_CLICKED, this);
}

void SettingsView::open(const Settings& current) {
    edit_ = current;
    orig_ = current;
    SettingsViewImpl::refresh_labels(this);
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
}

}  // namespace og
