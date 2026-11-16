// A VIN-segédek tesztjei: karakterellenőrzés, ellenőrző számjegy, márkafelismerés, mintagenerálás.
#include <cstring>

#include "mini_test.h"
#include "og_vin.h"

using namespace og;

static void chars_are_validated() {
    CHECK(vin_chars_valid("WVWZZZ1KZ6W612345"));
    CHECK(!vin_chars_valid("WVWZZZ1KZ6W61234"));    // rövid
    CHECK(!vin_chars_valid("WVWZZZ1KZ6W6123456"));  // hosszú
    CHECK(!vin_chars_valid("WVWZZZ1KZ6W61234O"));   // O tiltott
    CHECK(!vin_chars_valid("wvwzzz1kz6w612345"));   // kisbetű
    CHECK(!vin_chars_valid(nullptr));
}

static void check_digit_matches_known_vin() {
    CHECK(vin_check_digit("1M8GDM9AXKP042788") == 'X');  // ismert érvényes példa (X a 9. helyen)
    CHECK(vin_check_digit("11111111111111111") == '1');  // csupa 1: a súlyok összege 89, maradék 1
}

static void brand_from_wmi() {
    CHECK(vin_brand_id("W0LJD6EC8SB123456") == 0);
    CHECK(std::strcmp(vin_brand_name(vin_brand_id("W0LJD6EC8SB123456")), "Opel") == 0);
    CHECK(std::strcmp(vin_brand_name(vin_brand_id("TMBAG7NE1G0123456")), "Škoda") == 0);
    CHECK(vin_brand_id("XXXAAAAAAAAAAAAAA") == -1);
    CHECK(vin_brand_id("W0") == -1);
    CHECK(vin_brand_id(nullptr) == -1);
    CHECK(std::strcmp(vin_brand_name(-1), "") == 0);
    CHECK(std::strcmp(vin_brand_name(kBrandCount), "") == 0);
}

static void wmi_table_is_consistent() {
    for (size_t i = 0; i < kWmiCount; ++i) {
        CHECK(kWmi[i].brand < kBrandCount);
        CHECK(std::strlen(kWmi[i].wmi) == 3);
        for (size_t j = i + 1; j < kWmiCount; ++j) CHECK(std::strcmp(kWmi[i].wmi, kWmi[j].wmi) != 0);  // nincs ismétlődő kód
    }
}

static void sample_vins_are_valid_and_map_back_to_their_brand() {
    for (int b = 0; b < kBrandCount; ++b) {
        for (uint32_t seed = 0; seed < 20; ++seed) {
            char v[18];
            vin_make_sample(b, seed, v);
            CHECK(vin_chars_valid(v));
            CHECK(v[8] == vin_check_digit(v));
            CHECK(vin_brand_id(v) == b);
        }
    }
    char a[18], c[18];
    vin_make_sample(0, 1, a);
    vin_make_sample(0, 2, c);
    CHECK(std::strcmp(a, c) != 0);  // a mag változtatja a számot
}

int main() {
    RUN(chars_are_validated);
    RUN(check_digit_matches_known_vin);
    RUN(brand_from_wmi);
    RUN(wmi_table_is_consistent);
    RUN(sample_vins_are_valid_and_map_back_to_their_brand);
    TEST_MAIN_END();
}
