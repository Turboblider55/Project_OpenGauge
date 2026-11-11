// A beállítások képernyői: fő lista, fokozatbeállítások és képernyősorrend (lásd settings_view.h).
// Minden beállításhoz egy sor tartozik: felirat és vezérlő (kapcsoló, léptető vagy váltógomb).
#include "settings_view.h"

#include <cstdio>
#include <cstring>

#include "og_fonts.h"

namespace og {

namespace {

enum class Page : uint8_t { Main, Gear };

enum class Field : uint8_t {
    // fő lista
    Music, Debug, MaxRpm, Redline, MaxSpeed, Sleep, Palette, SkipLogo, Brightness, SpeedUnit, TempUnit, SysVolt, AutoSwitch, SwitchPeriod,
    // fokozatbeállítások
    GearMode, GearCount, FinalDrive, TyreCirc, Ratio1, Ratio2, Ratio3, Ratio4, Ratio5, Ratio6, Ratio7, Ratio8,
    Count
};

enum class Kind : uint8_t { Switch, Stepper, Cycle };

struct FieldInfo {
    Field field;
    Kind kind;
    Page page;
    const char* label;
};

const FieldInfo kFields[] = {
    {Field::Music, Kind::Switch, Page::Main, "Zene képernyő"},
    {Field::Debug, Kind::Switch, Page::Main, "Debug mód"},
    {Field::MaxRpm, Kind::Stepper, Page::Main, "Max fordulatszám (1/perc)"},
    {Field::Redline, Kind::Stepper, Page::Main, "Redline kezdete (1/perc)"},
    {Field::MaxSpeed, Kind::Stepper, Page::Main, "Max sebesség (km/h)"},
    {Field::Sleep, Kind::Stepper, Page::Main, "Elalvási idő (s)"},
    {Field::Palette, Kind::Cycle, Page::Main, "Színpaletta"},
    {Field::SkipLogo, Kind::Switch, Page::Main, "Logó kihagyása"},
    {Field::Brightness, Kind::Stepper, Page::Main, "Fényerő (%)"},
    {Field::SpeedUnit, Kind::Cycle, Page::Main, "Sebesség egysége"},
    {Field::TempUnit, Kind::Cycle, Page::Main, "Hőmérséklet egysége"},
    {Field::SysVolt, Kind::Cycle, Page::Main, "Rendszerfeszültség"},
    {Field::AutoSwitch, Kind::Switch, Page::Main, "Automatikus lapozás"},
    {Field::SwitchPeriod, Kind::Stepper, Page::Main, "Lapozás ideje (s)"},
    {Field::GearMode, Kind::Cycle, Page::Gear, "Fokozat-meghatározás"},
    {Field::GearCount, Kind::Stepper, Page::Gear, "Fokozatok száma"},
    {Field::FinalDrive, Kind::Stepper, Page::Gear, "Főhajtás áttétele"},
    {Field::TyreCirc, Kind::Stepper, Page::Gear, "Kerékkerület (m)"},
    {Field::Ratio1, Kind::Stepper, Page::Gear, "1. fokozat áttétele"},
    {Field::Ratio2, Kind::Stepper, Page::Gear, "2. fokozat áttétele"},
    {Field::Ratio3, Kind::Stepper, Page::Gear, "3. fokozat áttétele"},
    {Field::Ratio4, Kind::Stepper, Page::Gear, "4. fokozat áttétele"},
    {Field::Ratio5, Kind::Stepper, Page::Gear, "5. fokozat áttétele"},
    {Field::Ratio6, Kind::Stepper, Page::Gear, "6. fokozat áttétele"},
    {Field::Ratio7, Kind::Stepper, Page::Gear, "7. fokozat áttétele"},
    {Field::Ratio8, Kind::Stepper, Page::Gear, "8. fokozat áttétele"},
};
constexpr int kFieldCount = sizeof kFields / sizeof kFields[0];

constexpr int32_t kRowWidth = 250;
constexpr uint32_t kPageAnimMs = 250;

// Az áttétel-mezők sorszáma (0..7), vagy -1, ha a mező nem áttétel.
constexpr int ratio_index(Field f) {
    return f >= Field::Ratio1 && f <= Field::Ratio8 ? static_cast<int>(f) - static_cast<int>(Field::Ratio1) : -1;
}

// A képernyők rövid nevei a sorrend-listában (azonosító szerint: lásd settings.h kScreenIds).
const char* const kScreenNames[kScreenIds] = {"Fő", "Részletek", "Kapcsolat", "Zene", "Fordulat", "Sebesség", "Akku+víz", "Fokozat"};

}  // namespace

// A nyilvános osztály a lv_event-ek C visszahívásai miatt egy segédstruktúrán keresztül éri el a belső állapotot.
struct SettingsViewImpl {
    struct Ctx {
        SettingsView* view;
        Field field;
        lv_obj_t* widget;  // kapcsoló, vagy az érték feliratú címke
        int dir;           // léptetőnél: -1 / +1
        int idx;           // képernyősoroknál: a sor sorszáma; navigációs gombnál: a cél (0 fokozat, 1 sorrend)
    };

    static inline Ctx main_ctx[kFieldCount * 3 + 8];   // a fő lista visszahívásainak adatai
    static inline int main_ctx_n = 0;
    static inline Ctx sub_ctx[kFieldCount * 3 + kScreenIds * 3 + 8];  // az al-képernyők adatai (egyszerre egy létezik)
    static inline int sub_ctx_n = 0;
    static inline UiColors colors;
    static inline lv_obj_t* widgets[kFieldCount];  // soronként a kapcsoló / érték-címke (refresh_labels frissíti)
    static inline lv_obj_t* rows[kFieldCount];     // a sor tartója (a fokozat-képernyőn elrejthető)
    static inline lv_obj_t* scr_name[kScreenIds];  // a képernyősorok felirata és láthatóság-kapcsolója
    static inline lv_obj_t* scr_sw[kScreenIds];

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
            case Field::GearCount: std::snprintf(b, cap, "%u", s.gear_count); break;
            case Field::GearMode: std::snprintf(b, cap, "%s", s.gear_manual ? "Kézi" : "Automatikus"); break;
            case Field::FinalDrive:
                if (s.final_drive_x100 == 0) std::snprintf(b, cap, "-");
                else std::snprintf(b, cap, "%u.%02u", s.final_drive_x100 / 100, s.final_drive_x100 % 100);
                break;
            case Field::TyreCirc:
                if (s.tyre_circ_mm == 0) std::snprintf(b, cap, "-");
                else std::snprintf(b, cap, "%u.%02u", s.tyre_circ_mm / 1000, s.tyre_circ_mm % 1000 / 10);
                break;
            case Field::Ratio1: case Field::Ratio2: case Field::Ratio3: case Field::Ratio4:
            case Field::Ratio5: case Field::Ratio6: case Field::Ratio7: case Field::Ratio8: {
                const unsigned r = s.gear_ratio_x100[ratio_index(f)];
                if (r == 0) std::snprintf(b, cap, "-");
                else std::snprintf(b, cap, "%u.%02u", r / 100, r % 100);
                break;
            }
            case Field::SpeedUnit: std::snprintf(b, cap, "%s", s.speed_unit == SpeedUnit::Mph ? "mph" : "km/h"); break;
            case Field::TempUnit: std::snprintf(b, cap, "%s", s.temp_unit == TempUnit::Fahrenheit ? "°F" : "°C"); break;
            case Field::SysVolt:
                std::snprintf(b, cap, "%s", s.system_voltage == SystemVoltage::V12 ? "12 V" : s.system_voltage == SystemVoltage::V24 ? "24 V" : "Auto");
                break;
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
            case Field::GearCount: s.gear_count = static_cast<uint8_t>(stepped(s.gear_count, 3, 8, 1)); break;
            case Field::FinalDrive:  // 0: nincs megadva; az első + egy tipikus értéket ad, a - a 100 alatt kikapcsol
                if (s.final_drive_x100 == 0) s.final_drive_x100 = dir > 0 ? 350 : 0;
                else s.final_drive_x100 = static_cast<uint16_t>(s.final_drive_x100 + dir * 5 < 100 ? 0 : std::min(800, s.final_drive_x100 + dir * 5));
                break;
            case Field::TyreCirc:
                if (s.tyre_circ_mm == 0) s.tyre_circ_mm = dir > 0 ? 1900 : 0;
                else s.tyre_circ_mm = static_cast<uint16_t>(s.tyre_circ_mm + dir * 10 < 1200 ? 0 : std::min(2800, s.tyre_circ_mm + dir * 10));
                break;
            case Field::Ratio1: case Field::Ratio2: case Field::Ratio3: case Field::Ratio4:
            case Field::Ratio5: case Field::Ratio6: case Field::Ratio7: case Field::Ratio8: {
                uint16_t& r = s.gear_ratio_x100[ratio_index(f)];
                if (r == 0) r = dir > 0 ? 100 : 0;
                else r = static_cast<uint16_t>(r + dir * 5 < 20 ? 0 : std::min(999, r + dir * 5));
                break;
            }
            case Field::SwitchPeriod: s.switch_period_s = static_cast<uint16_t>(stepped(s.switch_period_s, 5, 300, 5)); break;
            default: break;
        }
    }

    static void cycle(Settings& s, Field f) {
        switch (f) {
            case Field::Palette: s.palette = static_cast<uint8_t>((s.palette + 1) % kPaletteCount); break;
            case Field::SpeedUnit: s.speed_unit = s.speed_unit == SpeedUnit::Kmh ? SpeedUnit::Mph : SpeedUnit::Kmh; break;
            case Field::TempUnit: s.temp_unit = s.temp_unit == TempUnit::Celsius ? TempUnit::Fahrenheit : TempUnit::Celsius; break;
            case Field::SysVolt: s.system_voltage = static_cast<SystemVoltage>((static_cast<int>(s.system_voltage) + 1) % 3); break;
            case Field::GearMode: s.gear_manual = !s.gear_manual; break;
            default: break;
        }
    }

    // Mit csinál: igaz, ha a két beállítás bájtformátuma eltér (a struktúra kitöltő bájtjai nem számítanak).
    static bool differs(const Settings& a, const Settings& b) {
        uint8_t x[kSettingsBlobSize], y[kSettingsBlobSize];
        settings_encode(a, x, sizeof x);
        settings_encode(b, y, sizeof y);
        return std::memcmp(x, y, sizeof x) != 0;
    }

    // Mit csinál: a vezérlők állapotát a szerkesztett értékekhez igazítja (csak a létező képernyők elemeit).
    // Hogyan:     a fokozat-képernyőn kézi módban látszanak a főhajtás, a kerékkerület és az áttételek (utóbbiból csak a
    //             fokozatszámnak megfelelő darab); automata módban eltűnnek, és magyarázó szöveg jelenik meg.
    static void refresh_labels(SettingsView* v) {
        char b[24];
        if (v->back_label_) lv_label_set_text(v->back_label_, differs(v->edit_, v->orig_) ? "ALKALMAZ" : "VISSZA");
        if (v->sub_back_label_) lv_label_set_text(v->sub_back_label_, differs(v->edit_, v->page_orig_) ? "MENTÉS" : "VISSZA");
        for (int i = 0; i < kFieldCount; ++i) {
            if (!widgets[i]) continue;
            if (kFields[i].kind == Kind::Switch) {
                lv_obj_set_state(widgets[i], LV_STATE_CHECKED, flag_of(v->edit_, kFields[i].field));
            } else {
                text_of(v->edit_, kFields[i].field, b, sizeof b);
                lv_label_set_text(widgets[i], b);
            }
            if (rows[i] && kFields[i].page == Page::Gear) {
                const Field f = kFields[i].field;
                bool hide = false;
                if (f == Field::FinalDrive || f == Field::TyreCirc) hide = !v->edit_.gear_manual;
                else if (ratio_index(f) >= 0) hide = !v->edit_.gear_manual || ratio_index(f) >= v->edit_.gear_count;
                if (lv_obj_is_hidden(rows[i]) != hide) lv_obj_set_hidden(rows[i], hide);
            }
        }
        if (v->gear_hint_) {
            const bool hide = v->edit_.gear_manual;
            if (lv_obj_is_hidden(v->gear_hint_) != hide) lv_obj_set_hidden(v->gear_hint_, hide);
        }
        for (int i = 0; i < kScreenIds; ++i) {
            if (!scr_name[i]) continue;
            const uint8_t id = v->edit_.screen_order[i];
            if (id >= kScreenIds) continue;
            lv_label_set_text(scr_name[i], kScreenNames[id]);
            lv_obj_set_state(scr_sw[i], LV_STATE_CHECKED, (v->edit_.screen_mask >> id) & 1);
        }
    }

    // Mit csinál: a léptető, váltó és kapcsoló gombok eseménykezelői.
    static void on_switch(lv_event_t* e) {
        Ctx* c = static_cast<Ctx*>(lv_event_get_user_data(e));
        set_flag(c->view->edit_, c->field, lv_obj_has_state(c->widget, LV_STATE_CHECKED));
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

    // Mit csinál: a képernyősor ▲/▼ gombja: a képernyőt a listában egy hellyel feljebb/lejjebb teszi (felcseréli a szomszéddal).
    static void on_screen_move(lv_event_t* e) {
        Ctx* c = static_cast<Ctx*>(lv_event_get_user_data(e));
        const int j = c->idx + c->dir;
        if (j < 0 || j >= kScreenIds) return;
        std::swap(c->view->edit_.screen_order[c->idx], c->view->edit_.screen_order[j]);
        refresh_labels(c->view);
    }

    // Mit csinál: a képernyősor kapcsolója: be/ki veszi a képernyőt a lapozásból; az utolsó látható képernyő nem kapcsolható ki.
    static void on_screen_toggle(lv_event_t* e) {
        Ctx* c = static_cast<Ctx*>(lv_event_get_user_data(e));
        Settings& s = c->view->edit_;
        const uint8_t id = s.screen_order[c->idx];
        if (id >= kScreenIds) return;
        const uint8_t bit = static_cast<uint8_t>(1u << id);
        if (lv_obj_has_state(c->widget, LV_STATE_CHECKED)) s.screen_mask |= bit;
        else if ((s.screen_mask & ~bit) != 0) s.screen_mask &= static_cast<uint8_t>(~bit);
        refresh_labels(c->view);  // az elutasított kikapcsolás kapcsolóállását is visszaállítja
    }

    // Mit csinál: a fő lista ALKALMAZ / VISSZA gombja: a szerkesztett értékeket véglegesíti.
    static void on_back(lv_event_t* e) {
        SettingsView* v = static_cast<SettingsView*>(lv_event_get_user_data(e));
        Settings out = v->edit_;
        sanitize(out);
        if (v->on_exit_) v->on_exit_(out);
    }

    // Mit csinál: jobbra húzás a fő listán: a módosítások elvetése, vissza az előző képernyőre.
    static void on_gesture_main(lv_event_t* e) {
        SettingsView* v = static_cast<SettingsView*>(lv_event_get_user_data(e));
        lv_indev_t* in = lv_indev_active();
        if (in && lv_indev_get_gesture_dir(in) == LV_DIR_RIGHT && v->on_cancel_) v->on_cancel_();
    }

    // Mit csinál: az al-képernyő gombja / jobbra húzása: vissza a fő listára; a módosítások megmaradnak (a fő lista dönt róluk).
    static void sub_back(SettingsView* v) {
        refresh_labels(v);  // a fő lista ALKALMAZ / VISSZA feliratát frissíti
        lv_screen_load_anim(v->scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, kPageAnimMs, 0, true);  // az al-képernyő törlődik
    }

    static void on_sub_back(lv_event_t* e) { sub_back(static_cast<SettingsView*>(lv_event_get_user_data(e))); }

    static void on_gesture_sub(lv_event_t* e) {
        lv_indev_t* in = lv_indev_active();
        if (in && lv_indev_get_gesture_dir(in) == LV_DIR_RIGHT) sub_back(static_cast<SettingsView*>(lv_event_get_user_data(e)));
    }

    static lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
        lv_obj_t* l = lv_label_create(parent);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, color, 0);
        lv_label_set_text(l, text);
        lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
        return l;
    }

    // Mit csinál: egy körbeforgó (túl hosszú szövegnél görgetett) felirat a megadott szélességben, középre igazítva.
    static lv_obj_t* make_scroll_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text, int32_t w) {
        lv_obj_t* l = make_label(parent, font, color, text);
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
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

    // Mit csinál: a fő lista törlése után nullázza a törölt objektumokra mutató állapotot.
    static void reset_main(SettingsView* v) {
        v->list_ = nullptr;
        v->back_label_ = nullptr;
        main_ctx_n = 0;
        for (int i = 0; i < kFieldCount; ++i) {
            if (kFields[i].page == Page::Main) widgets[i] = rows[i] = nullptr;
        }
    }

    // Mit csinál: az al-képernyők törlése után ugyanez (a fokozat- és sorrend-képernyő elemeire).
    static void reset_sub(SettingsView* v, bool gear) {
        if (gear) {
            v->gear_scr_ = nullptr;
            v->gear_hint_ = nullptr;
            for (int i = 0; i < kFieldCount; ++i) {
                if (kFields[i].page == Page::Gear) widgets[i] = rows[i] = nullptr;
            }
        } else {
            v->order_scr_ = nullptr;
            for (int i = 0; i < kScreenIds; ++i) scr_name[i] = scr_sw[i] = nullptr;
        }
        v->sub_back_label_ = nullptr;
        sub_ctx_n = 0;
    }

    static Ctx* new_ctx(Ctx* pool, int& n, SettingsView* v, Field f, lv_obj_t* w, int dir) {
        Ctx* c = &pool[n++];
        *c = {v, f, w, dir, 0};
        return c;
    }

    // Mit csinál: egy képernyősor: ▲, a képernyő neve (körbeforgó, ha hosszú), a láthatóság kapcsolója és ▼.
    static void build_screen_row(SettingsView* v, lv_obj_t* list, int i) {
        lv_obj_t* line = make_flex(list, LV_FLEX_FLOW_ROW, kRowWidth);
        lv_obj_t* up = make_button(line, 40, 40, LV_SYMBOL_UP, &og_font_montserrat_20);
        lv_obj_t* name = make_scroll_label(line, &og_font_montserrat_20, colors.text, "", 100);
        lv_obj_t* sw = lv_switch_create(line);
        lv_obj_set_size(sw, 50, 26);
        lv_obj_set_style_bg_color(sw, colors.track, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, colors.accent, static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_t* down = make_button(line, 40, 40, LV_SYMBOL_DOWN, &og_font_montserrat_20);
        Ctx* cu = new_ctx(sub_ctx, sub_ctx_n, v, Field::Count, up, -1);
        cu->idx = i;
        Ctx* cd = new_ctx(sub_ctx, sub_ctx_n, v, Field::Count, down, +1);
        cd->idx = i;
        Ctx* cs = new_ctx(sub_ctx, sub_ctx_n, v, Field::Count, sw, 0);
        cs->idx = i;
        lv_obj_add_event_cb(up, on_screen_move, LV_EVENT_CLICKED, cu);
        lv_obj_add_event_cb(down, on_screen_move, LV_EVENT_CLICKED, cd);
        lv_obj_add_event_cb(sw, on_screen_toggle, LV_EVENT_VALUE_CHANGED, cs);
        scr_name[i] = name;
        scr_sw[i] = sw;
    }

    // Mit csinál: egy beállítás sora a megadott listán: körbeforgó felirat és a típusnak megfelelő vezérlő.
    static void build_row(SettingsView* v, lv_obj_t* list, Ctx* pool, int& n, int i) {
        const FieldInfo& fi = kFields[i];
        lv_obj_t* row = make_flex(list, LV_FLEX_FLOW_COLUMN, kRowWidth);
        make_scroll_label(row, &og_font_montserrat_20, colors.dim, fi.label, kRowWidth);
        lv_obj_t* line = make_flex(row, LV_FLEX_FLOW_ROW, kRowWidth);
        switch (fi.kind) {
            case Kind::Switch: {
                lv_obj_t* sw = lv_switch_create(line);
                lv_obj_set_style_bg_color(sw, colors.track, LV_PART_MAIN);
                lv_obj_set_style_bg_color(sw, colors.accent, static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
                lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
                lv_obj_add_event_cb(sw, on_switch, LV_EVENT_VALUE_CHANGED, new_ctx(pool, n, v, fi.field, sw, 0));
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
                    Ctx* c = new_ctx(pool, n, v, fi.field, val, b == minus ? -1 : +1);
                    lv_obj_add_event_cb(b, on_step, LV_EVENT_SHORT_CLICKED, c);
                    lv_obj_add_event_cb(b, on_step, LV_EVENT_LONG_PRESSED_REPEAT, c);
                }
                widgets[i] = val;
                break;
            }
            case Kind::Cycle: {
                lv_obj_t* b = make_button(line, kRowWidth - 40, 44, "", &og_font_montserrat_28);
                lv_obj_t* label = lv_obj_get_child(b, 0);
                lv_obj_add_event_cb(b, on_cycle, LV_EVENT_CLICKED, new_ctx(pool, n, v, fi.field, label, 0));
                widgets[i] = label;
                break;
            }
        }
        rows[i] = row;
    }

    // Mit csinál: egy görgethető listás képernyőt készít a megadott címmel (a kerek kijelző miatt a lista szélén üres hely van).
    static lv_obj_t* make_list_screen(lv_obj_t** list_out, const char* title) {
        lv_obj_t* scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(scr, colors.bg, 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* list = lv_obj_create(scr);
        lv_obj_set_size(list, 360, 360);
        lv_obj_center(list);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_radius(list, 0, 0);
        lv_obj_set_style_pad_top(list, 70, 0);
        lv_obj_set_style_pad_bottom(list, 80, 0);
        lv_obj_set_style_pad_row(list, 22, 0);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);
        make_label(list, &og_font_montserrat_28, colors.accent, title);
        *list_out = list;
        return scr;
    }

    // Mit csinál: az al-képernyő törlésekor (LV_EVENT_DELETE) nullázza a rá mutató állapotot.
    static void on_gear_deleted(lv_event_t* e) { reset_sub(static_cast<SettingsView*>(lv_event_get_user_data(e)), true); }
    static void on_order_deleted(lv_event_t* e) { reset_sub(static_cast<SettingsView*>(lv_event_get_user_data(e)), false); }

    // Mit csinál: megnyitja a fokozatbeállítások képernyőjét: a címet, a módot, a fokozatszámot, a főhajtást, a kerékkerületet,
    //             az áttételeket és a VISSZA / MENTÉS gombot.
    static void open_gear(SettingsView* v) {
        v->page_orig_ = v->edit_;
        sub_ctx_n = 0;
        lv_obj_t* list = nullptr;
        v->gear_scr_ = make_list_screen(&list, "FOKOZATOK");
        lv_obj_add_event_cb(v->gear_scr_, on_gear_deleted, LV_EVENT_DELETE, v);
        lv_obj_add_event_cb(v->gear_scr_, on_gesture_sub, LV_EVENT_GESTURE, v);
        for (int i = 0; i < kFieldCount; ++i) {
            if (kFields[i].page != Page::Gear) continue;
            build_row(v, list, sub_ctx, sub_ctx_n, i);
            if (kFields[i].field == Field::GearMode) {  // a mód alatt automata módban magyarázat látszik
                v->gear_hint_ = make_label(list, &og_font_montserrat_20, colors.dim, "A fokozatokat a készülék vezetés közben tanulja.");
                lv_obj_set_width(v->gear_hint_, kRowWidth);
                lv_label_set_long_mode(v->gear_hint_, LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(v->gear_hint_, LV_TEXT_ALIGN_CENTER, 0);
            }
        }
        lv_obj_t* back = make_button(list, kRowWidth - 40, 48, "VISSZA", &og_font_montserrat_28);
        v->sub_back_label_ = lv_obj_get_child(back, 0);
        lv_obj_add_event_cb(back, on_sub_back, LV_EVENT_CLICKED, v);
        refresh_labels(v);
        lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
        lv_screen_load_anim(v->gear_scr_, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, kPageAnimMs, 0, false);
    }

    // Mit csinál: megnyitja a képernyősorrend képernyőjét: a címet, a sorokat (▲ név kapcsoló ▼) és a VISSZA / MENTÉS gombot.
    static void open_order(SettingsView* v) {
        v->page_orig_ = v->edit_;
        sub_ctx_n = 0;
        lv_obj_t* list = nullptr;
        v->order_scr_ = make_list_screen(&list, "KÉPERNYŐK");
        lv_obj_add_event_cb(v->order_scr_, on_order_deleted, LV_EVENT_DELETE, v);
        lv_obj_add_event_cb(v->order_scr_, on_gesture_sub, LV_EVENT_GESTURE, v);
        for (int i = 0; i < kScreenIds; ++i) build_screen_row(v, list, i);
        lv_obj_t* back = make_button(list, kRowWidth - 40, 48, "VISSZA", &og_font_montserrat_28);
        v->sub_back_label_ = lv_obj_get_child(back, 0);
        lv_obj_add_event_cb(back, on_sub_back, LV_EVENT_CLICKED, v);
        refresh_labels(v);
        lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
        lv_screen_load_anim(v->order_scr_, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, kPageAnimMs, 0, false);
    }

    // Mit csinál: a fő lista navigációs gombja: a megfelelő al-képernyőt nyitja meg.
    static void on_nav(lv_event_t* e) {
        Ctx* c = static_cast<Ctx*>(lv_event_get_user_data(e));
        if (c->idx == 0) open_gear(c->view);
        else open_order(c->view);
    }
};

void SettingsView::init(const UiColors& c, ExitFn on_exit, CancelFn on_cancel) {
    SettingsViewImpl::colors = c;
    on_exit_ = on_exit;
    on_cancel_ = on_cancel;
}

// Mit csinál: a fő lista törlésekor (LV_EVENT_DELETE) nullázza a mutatókat, hogy a következő megnyitás újraépítsen.
static void on_scr_deleted(lv_event_t* e) {
    SettingsView* v = static_cast<SettingsView*>(lv_event_get_user_data(e));
    v->scr = nullptr;
    SettingsViewImpl::reset_main(v);
}

void SettingsView::build() {
    using I = SettingsViewImpl;
    I::main_ctx_n = 0;
    scr = I::make_list_screen(&list_, "BEÁLLÍTÁSOK");
    lv_obj_add_event_cb(scr, on_scr_deleted, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(scr, I::on_gesture_main, LV_EVENT_GESTURE, this);

    for (int i = 0; i < kFieldCount; ++i) {
        if (kFields[i].page == Page::Main) I::build_row(this, list_, I::main_ctx, I::main_ctx_n, i);
    }
    // Az al-képernyők gombjai.
    lv_obj_t* gear = I::make_button(list_, kRowWidth - 20, 48, "FOKOZATOK  " LV_SYMBOL_RIGHT, &og_font_montserrat_20);
    lv_obj_t* order = I::make_button(list_, kRowWidth - 20, 48, "KÉPERNYŐK  " LV_SYMBOL_RIGHT, &og_font_montserrat_20);
    I::Ctx* cg = I::new_ctx(I::main_ctx, I::main_ctx_n, this, Field::Count, gear, 0);
    cg->idx = 0;
    I::Ctx* co = I::new_ctx(I::main_ctx, I::main_ctx_n, this, Field::Count, order, 0);
    co->idx = 1;
    lv_obj_add_event_cb(gear, I::on_nav, LV_EVENT_CLICKED, cg);
    lv_obj_add_event_cb(order, I::on_nav, LV_EVENT_CLICKED, co);

    lv_obj_t* back = I::make_button(list_, kRowWidth - 40, 48, "VISSZA", &og_font_montserrat_28);
    back_label_ = lv_obj_get_child(back, 0);
    lv_obj_add_event_cb(back, I::on_back, LV_EVENT_CLICKED, this);
}

void SettingsView::open(const Settings& current) {
    if (!scr) build();
    edit_ = current;
    orig_ = current;
    SettingsViewImpl::refresh_labels(this);
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
}

}  // namespace og
