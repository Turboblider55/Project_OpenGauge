// A beépített márkalogók táblázata. Új logó: convert_logo.cjs futtatása, a fájl felvétele a CMakeLists.txt-be,
// és egy sor ide (a név megegyezik az og_vin.h kBrandNames elemével).
#include "og_logos.h"

#include <cstring>

#include "og_vin.h"

extern "C" {
extern const lv_image_dsc_t og_logo_opel, og_logo_volkswagen, og_logo_skoda, og_logo_suzuki, og_logo_toyota,
    og_logo_peugeot, og_logo_renault, og_logo_bmw;
}

namespace og {

namespace {
struct Entry {
    const char* brand;
    const lv_image_dsc_t* image;
};
const Entry kLogos[] = {
    {"Opel", &og_logo_opel},       {"Volkswagen", &og_logo_volkswagen}, {"Škoda", &og_logo_skoda},
    {"Suzuki", &og_logo_suzuki},   {"Toyota", &og_logo_toyota},         {"Peugeot", &og_logo_peugeot},
    {"Renault", &og_logo_renault}, {"BMW", &og_logo_bmw},
};
}  // namespace

const lv_image_dsc_t* logo_for_brand(int brand) {
    const char* name = vin_brand_name(brand);
    if (!name[0]) return nullptr;
    for (const Entry& e : kLogos)
        if (std::strcmp(e.brand, name) == 0) return e.image;
    return nullptr;
}

}  // namespace og
