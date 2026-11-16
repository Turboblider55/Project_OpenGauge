// A "Most játszik" képernyő logikájának tesztjei: szöveg-tisztítás, eltelt idő, időformátum.
#include <cstring>

#include "mini_test.h"
#include "music_model.h"

using namespace og;

static void clean(const char* in, const char* want) {
    char out[64];
    sanitize_text(in, out, sizeof out);
    CHECK(std::strcmp(out, want) == 0);
}

static void keeps_supported_accents() {
    clean("Sebességmámor", "Sebességmámor");
    clean("ŐÚŰÖÜÓÉÁÍ őúűöüóéáí", "ŐÚŰÖÜÓÉÁÍ őúűöüóéáí");
    clean("Mindent viv\xC3\xB5 sl\xC3\xA1" "ger", "Mindent viv\xC3\xB5 sl\xC3\xA1" "ger");  // õ (U+00F5) változatlan marad
    clean("Zażółć gęślą jaźń Łódź Ångström Dvořák", "Zażółć gęślą jaźń Łódź Ångström Dvořák");  // lengyel, svéd, cseh
    clean("Beyoncé – Halo… ‘x’ “y” •", "Beyoncé – Halo… ‘x’ “y” •");
}

static void maps_variants_and_unknown() {
    clean("a\xE2\x80\x90" "b\xE2\x80\x9E" "c", "a-b\xE2\x80\x9D" "c");  // kötőjel- és idézőjel-változatok
    clean("A\xE2\x82\xAC", "A?");  // euró jel: nincs a betűkészletben
    clean("\xD0\x9F\xD1\x80", "??");  // cirill betűk
    clean("\xF0\x9F\x8E\xB5 dal", "? dal");  // emoji (4 bájt)
    clean("hib\xC3", "hib?");  // csonka bájtsor a végén
    clean("a\tb", "a b");
    clean("\xFF", "?");
}

static void clean_truncates_on_character_boundary() {
    char out[6];
    sanitize_text("Árvíztűrő tükörfúrógép", out, sizeof out);
    CHECK(std::strcmp(out, "Árv") == 0);  // az í (2 bájt) már nem fér ki, félbevágva nem kerül be
    sanitize_text("abcdefgh", out, sizeof out);
    CHECK(std::strcmp(out, "abcde") == 0);
    sanitize_text(nullptr, out, sizeof out);
    CHECK(out[0] == 0);
}

static void elapsed_counts_only_while_playing() {
    CHECK_NEAR(music_elapsed(10, 1000, 1.0f, true, 6000, 200), 15.0, 0.001);
    CHECK_NEAR(music_elapsed(10, 1000, 1.0f, false, 6000, 200), 10.0, 0.001);
    CHECK_NEAR(music_elapsed(10, 1000, 0.0f, true, 6000, 200), 10.0, 0.001);
    CHECK_NEAR(music_elapsed(10, 1000, 2.0f, true, 6000, 200), 20.0, 0.001);
}

static void elapsed_is_clamped_and_wrap_safe() {
    CHECK_NEAR(music_elapsed(198, 0, 1.0f, true, 10000, 200), 200.0, 0.001);
    CHECK_NEAR(music_elapsed(5, 0xFFFFFC18u, 1.0f, true, 1000, 0), 7.0, 0.001);  // 32 bites túlcsordulás: 2,0 s telt el
}

static void progress_is_bounded() {
    CHECK_NEAR(music_progress(50, 200), 0.25, 1e-6);
    CHECK(music_progress(50, 0) == 0);
    CHECK(music_progress(250, 200) == 1);
}

static void formats_time() {
    char b[16];
    format_time(b, sizeof b, 0);
    CHECK(std::strcmp(b, "0:00") == 0);
    format_time(b, sizeof b, 65.9f);
    CHECK(std::strcmp(b, "1:05") == 0);
    format_time(b, sizeof b, 3725);
    CHECK(std::strcmp(b, "1:02:05") == 0);
    format_time(b, sizeof b, -3);
    CHECK(std::strcmp(b, "0:00") == 0);
}

int main() {
    RUN(keeps_supported_accents);
    RUN(maps_variants_and_unknown);
    RUN(clean_truncates_on_character_boundary);
    RUN(elapsed_counts_only_while_playing);
    RUN(elapsed_is_clamped_and_wrap_safe);
    RUN(progress_is_bounded);
    RUN(formats_time);
    TEST_MAIN_END();
}
