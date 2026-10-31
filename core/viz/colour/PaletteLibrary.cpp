#include "viz/colour/PaletteLibrary.hpp"

#include <cstdio>

namespace geode::viz::colour {

namespace {

const std::array<MoodInfo, kMoodCount> kMoods = {{
    {"calm", "Calm", 0.2f, 0.65f},
    {"warm", "Warm", 0.55f, 0.75f},
    {"dark", "Dark", 0.65f, 0.2f},
    {"melancholic", "Melancholic", 0.25f, 0.2f},
    {"euphoric", "Euphoric", 0.9f, 0.85f},
    {"neon", "Neon", 0.85f, 0.45f},
    {"natural", "Natural", 0.4f, 0.6f},
}};

}  // namespace

const MoodInfo& moodInfo(Mood mood) { return kMoods[static_cast<size_t>(mood)]; }

const std::vector<Palette>& palettes() {
    static const std::vector<Palette> kPalettes = {
        // Calm: quiet, open, low energy.
        {"water-lilies", "Water lilies", Mood::Calm, {0x1E3448, 0x35626B, 0x6E9C8F, 0xA9C4B8, 0xD9CCE3}},
        {"prussian-wave", "Prussian wave", Mood::Calm, {0x0B1D3A, 0x1F4E79, 0x5B8DB8, 0xD9D2C0, 0xF3EEE2}},
        {"glacier", "Glacier", Mood::Calm, {0x0B1F2E, 0x1D4E6E, 0x5FA8C9, 0xB8E1EE, 0xF2FBFF}},
        {"blue-hour", "Blue hour", Mood::Calm, {0x0D1B2A, 0x1B263B, 0x415A77, 0x778DA9, 0xE0E1DD}},
        {"summer-camp", "Summer camp", Mood::Calm, {0x9C964A, 0xCDC08C, 0xFAD77B, 0xF4B5BD, 0x85D4E3}},
        // Warm: golden, glowing, mid energy.
        {"teal-orange", "Teal & orange", Mood::Warm, {0x0A1F26, 0x0F5C63, 0x3E9A9C, 0xF0A050, 0xFF6A13}},
        {"amber-haze", "Amber haze", Mood::Warm, {0x2A1206, 0x7A3410, 0xC4621D, 0xE99A3C, 0xF6D28B}},
        {"harbour-sunset", "Harbour sunset", Mood::Warm, {0x2E2A24, 0x7A5A33, 0xC8862F, 0xEDC66A, 0xF6EBD0}},
        {"gold-leaf", "Gold leaf", Mood::Warm, {0x1C1408, 0x6B4A12, 0xB8862B, 0xE3C25A, 0xF4E3A1}},
        {"hotel-pink", "Hotel pink", Mood::Warm, {0x5B1A18, 0xD67236, 0xFD6467, 0xF1BB7B, 0xE6A0C4}},
        {"desert-dusk", "Desert dusk", Mood::Warm, {0x2B1B3D, 0x6B2D5C, 0xC2465A, 0xF08A4B, 0xF9C784}},
        // Dark: heavy, brooding, high tension.
        {"corridor-red", "Corridor red", Mood::Dark, {0x120D0D, 0x4A1010, 0xA11D1D, 0xD96C3B, 0xEDE3D1}},
        {"maroon-murals", "Maroon murals", Mood::Dark, {0x160606, 0x3C0B0B, 0x6E1414, 0x9E3A1F, 0xC9672E}},
        {"abyss-glow", "Abyss glow", Mood::Dark, {0x00040A, 0x001B2E, 0x013A5A, 0x00787F, 0x3FE0C5}},
        {"sodium-noir", "Sodium noir", Mood::Dark, {0x0C0F0A, 0x1F2A1A, 0x46553A, 0x8A8F5A, 0xC9C28A}},
        {"ultraviolet", "Ultraviolet", Mood::Dark, {0x05010C, 0x1B0638, 0x3D1480, 0x7B3FE4, 0xC9A8FF}},
        // Melancholic: blue, slow, minor.
        {"rust-blue", "Rust & blue", Mood::Melancholic, {0x0E1A2B, 0x24365A, 0x4A5E8C, 0x8C7A9E, 0xC89B8C}},
        {"rain-city", "Rain city", Mood::Melancholic, {0x05070D, 0x14213D, 0x2E4A6B, 0x6C8FA8, 0xB8C7D1}},
        {"silk-red", "Silk red", Mood::Melancholic, {0x160404, 0x5A0B0E, 0x9E1A1F, 0xC9773A, 0xE9C48A}},
        {"starry-night", "Starry night", Mood::Melancholic, {0x0B1C3F, 0x1F4C8F, 0x4F86C6, 0xE5C243, 0xF5E7A1}},
        {"harbour-fog", "Harbour fog", Mood::Melancholic, {0x1A1F24, 0x3A4650, 0x6B7A85, 0xA3AFB5, 0xDDE3E3}},
        // Euphoric: bright, saturated, peaks and drops.
        {"aurora", "Aurora", Mood::Euphoric, {0x020B1A, 0x0B3D4F, 0x12A07A, 0x6CF0A4, 0xC07CFF}},
        {"sunset-pop", "Sunset pop", Mood::Euphoric, {0x2B0F54, 0xAB1F65, 0xFF4F69, 0xFF8E4D, 0xFFF7AD}},
        {"paper-cutouts", "Paper cut-outs", Mood::Euphoric, {0x12357A, 0x2F7BD9, 0xF4C430, 0xE8452C, 0xF6EEDF}},
        {"holi", "Holi", Mood::Euphoric, {0x3A0CA3, 0x7209B7, 0xF72585, 0xFF9E00, 0xFFE66D}},
        {"sea-expedition", "Sea expedition", Mood::Euphoric, {0x3B9AB2, 0x78B7C5, 0xEBCC2A, 0xE1AF00, 0xF21A00}},
        // Neon: electronic, night, club.
        {"synthwave", "Synthwave", Mood::Neon, {0x0D0221, 0x261447, 0x6A0DAD, 0xFF2A6D, 0x05D9E8}},
        {"tokyo-neon", "Tokyo neon", Mood::Neon, {0x05010F, 0x1A0B3B, 0x00F0FF, 0xFF00A0, 0xFAFF00}},
        {"night-market", "Night market", Mood::Neon, {0x04140C, 0x0B3D2A, 0x1F8A5B, 0xC7E85A, 0xF2F27A}},
        {"neon-rain", "Neon rain", Mood::Neon, {0x04060F, 0x0F2340, 0x1E6091, 0xE63946, 0xFFB4A2}},
        {"acid", "Acid", Mood::Neon, {0x000000, 0x0A2E1F, 0x00FF9C, 0xFF00E6, 0xFFFFFF}},
        // Natural: earth, leaf, water.
        {"forest-canopy", "Forest canopy", Mood::Natural, {0x0B1A0F, 0x1E3D21, 0x3F6B35, 0x8DB255, 0xD7E89C}},
        {"coral-reef", "Coral reef", Mood::Natural, {0x062A4A, 0x0E7C9B, 0x3CC8C0, 0xFF8C69, 0xFFD6A5}},
        {"lavender-fields", "Lavender fields", Mood::Natural, {0x2D1E4A, 0x5A3E8C, 0x9B7FCF, 0xD7C4EC, 0xF4E8C1}},
        {"autumn-maple", "Autumn maple", Mood::Natural, {0x2A1A10, 0x6B2E14, 0xB5521B, 0xE39B3A, 0xF2D58C}},
        {"sakura", "Sakura", Mood::Natural, {0x3B1F2B, 0x8E4A64, 0xD987A6, 0xF4C6D6, 0xFFF1F3}},
    };
    return kPalettes;
}

int paletteIndex(const std::string& id) {
    const auto& all = palettes();
    for (size_t i = 0; i < all.size(); i++) {
        if (id == all[i].id) return static_cast<int>(i);
    }
    return -1;
}

const std::array<Lab, 5>& paletteLab(int index) {
    static const std::vector<std::array<Lab, 5>> kLab = [] {
        std::vector<std::array<Lab, 5>> out;
        for (const Palette& p : palettes()) {
            std::array<Lab, 5> stops{};
            for (size_t i = 0; i < 5; i++) stops[i] = srgbToOklab(hexToRgb(p.stops[i]));
            out.push_back(stops);
        }
        return out;
    }();
    const int n = static_cast<int>(kLab.size());
    return kLab[static_cast<size_t>(((index % n) + n) % n)];
}

std::string paletteTable() {
    std::string out;
    char hex[8];
    for (const Palette& p : palettes()) {
        out += p.id;
        out += '\t';
        out += p.name;
        out += '\t';
        out += moodInfo(p.mood).id;
        out += '\t';
        for (size_t i = 0; i < p.stops.size(); i++) {
            std::snprintf(hex, sizeof(hex), "#%06x", static_cast<unsigned>(p.stops[i] & 0xFFFFFF));
            if (i > 0) out += ',';
            out += hex;
        }
        out += '\n';
    }
    return out;
}

}  // namespace geode::viz::colour
