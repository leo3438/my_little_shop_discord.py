#include <gtest/gtest.h>

#include <cctype>

#include "retromanager/core/FileName.hpp"

using namespace rm;

// Names of files the console's other tools must list (DBI, Tinfoil, the
// HOME menu's installer): plain ASCII letters, digits, spaces and dashes.

namespace {

bool isAsciiSafe(const std::string& name) {
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == ' ' || c == '-')) return false;
    }
    return !name.empty() && name.front() != ' ' && name.back() != ' ';
}

}  // namespace

TEST(AsciiFileName, AccentsBecomeTheirBaseLetter) {
    EXPECT_EQ(asciiFileName("Pokémon Black Version"), "Pokemon Black Version");
    EXPECT_EQ(asciiFileName("Ōkami"), "Okami");
    EXPECT_EQ(asciiFileName("ÀÉÎÕÜ àéîõü çÇñÑ ÿ"), "AEIOU aeiou cCnN y");
    EXPECT_EQ(asciiFileName("Œuvre Ærø straße Łódź Škoda"), "OEuvre AEro strasse Lodz Skoda");
    // Decomposed form: 'e' followed by a combining acute accent.
    EXPECT_EQ(asciiFileName("Poke\xCC\x81mon"), "Pokemon");
}

TEST(AsciiFileName, PunctuationBecomesSpacesAndApostrophesVanish) {
    EXPECT_EQ(asciiFileName("Tom & Jerry"), "Tom Jerry");
    EXPECT_EQ(asciiFileName("Zelda: Link's Awakening DX"), "Zelda Links Awakening DX");
    EXPECT_EQ(asciiFileName("Kirby\xE2\x80\x99s Adventure"), "Kirbys Adventure");  // typographic apostrophe
    EXPECT_EQ(asciiFileName("Mario Kart\xE2\x84\xA2 - Super Circuit (Europe) [!]"), "Mario Kart - Super Circuit Europe");
    EXPECT_EQ(asciiFileName("  a__b..c  "), "a b c");
    EXPECT_EQ(asciiFileName("Spider-Man 2"), "Spider-Man 2");
}

TEST(AsciiFileName, NothingUsableFallsBack) {
    EXPECT_EQ(asciiFileName("ポケモン"), "");
    EXPECT_EQ(asciiFileName("..."), "");
    EXPECT_EQ(asciiFileName("\xFF\xFE broken \xC3"), "broken");  // invalid UTF-8 bytes are dropped
}

TEST(AsciiFileName, OutputIsAlwaysAsciiSafeAndBounded) {
    const std::string samples[] = {"Pokémon™ Noire & Blanche: «Édition» n°2", "日本語 Title 123", std::string(400, 'x') + "é",
                                   "a\tb\nc\x01", "CON", "-leading dash-"};
    for (const auto& s : samples) {
        std::string out = asciiFileName(s);
        if (out.empty()) continue;
        EXPECT_TRUE(isAsciiSafe(out)) << s << " -> " << out;
        EXPECT_LE(out.size(), 200u);
    }
    EXPECT_EQ(asciiFileName("Pokémon™ Noire & Blanche: «Édition» n°2"), "Pokemon Noire Blanche Edition n 2");
}
