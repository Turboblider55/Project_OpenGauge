// A "Most játszik" képernyő logikájának tesztjei: ékezetek lecserélése, eltelt idő, időformátum.
#include <cstring>

#include "mini_test.h"
#include "music_model.h"

using namespace og;

static void fold(const char* in, const char* want) {
    char out[64];
    fold_ascii(in, out, sizeof out);
    CHECK(std::strcmp(out, want) == 0);
}

static void folds_hungarian_accents() {
    fold("Sebességmámor", "Sebessegmamor");
    fold("Mindent vivő sláger", "Mindent vivo slager");
    fold("Mindent viv\xC3\xB5 sl\xC3\xA1ger", "Mindent vivo slager");  // õ (U+00F5), ahogy a lejátszó küldte
    fold("ŐÚŰÖÜÓÉÁÍ őúűöüóéáí", "OUUOUOEAI ouuouoeai");
}

static void folds_punctuation_and_unknown() {
    fold("It\xE2\x80\x99s \xE2\x80\x9Cok\xE2\x80\x9D \xE2\x80\x93 yes", "It's \"ok\" - yes");
    fold("A\xE2\x82\xAC", "A?");              // euró jel: nincs megfelelője
    fold("\xF0\x9F\x8E\xB5 dal", "? dal");   // emoji (4 bájt)
    fold("hib\xC3", "hib?");                  // csonka bájtsor a végén
    fold("a\tb", "a b");
}

static void fold_truncates_safely() {
    char out[6];
    fold_ascii("Árvíztűrő tükörfúrógép", out, sizeof out);
    CHECK(std::strcmp(out, "Arviz") == 0);
    fold_ascii(nullptr, out, sizeof out);
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
    RUN(folds_hungarian_accents);
    RUN(folds_punctuation_and_unknown);
    RUN(fold_truncates_safely);
    RUN(elapsed_counts_only_while_playing);
    RUN(elapsed_is_clamped_and_wrap_safe);
    RUN(progress_is_bounded);
    RUN(formats_time);
    TEST_MAIN_END();
}
