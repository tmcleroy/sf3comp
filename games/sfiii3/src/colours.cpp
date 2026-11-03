// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// The players' own colours for 3rd Strike's characters, and the colour editor that makes them (as cps2recomp's
// fight/colours.h has them).
//
// Where they are: as a match begins the game copies each fighter's palettes into colour RAM with the palette DMA,
// a line of 64 colours (or two or three) at a time from the graphics SIMMs, player 1's to colours 0x000-0x1FF (and a
// copy at 0x200), player 2's to 0x400-0x5FF (and 0x600): seen in the DMA log (CPS3RECOMP_PALETTE_LOG) for Chun-Li and
// Ryu, each with lines of its own and some shared (0x2F6AC00, 0x2F6AD00: effects). Each colour is 15 bits, five each
// of red, green and blue (red the lowest), as the board shows them.
//
// Custom colours (always on, so that what the editor makes is always seen): a colour read from a word of the graphics that a file in the
// folder "palettes" beside settings.ini gives another for is that one instead, as it is read, so the game's code is
// the same with them as without. A character's file (palettes/<Name>.txt, the editor writes it) is lines of
// "WORD = COLOUR" in hexadecimal (the graphics word, then the colour); "#" starts a remark. The editor (the model
// viewer's colour editor) changes player 1's colours: its changes are seen at once and written to the character's
// file a little after the last one.
#include "game.h"
#include "sfiii3.h"
#include "system.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace cps3recomp {

namespace {
    const char *const kNames[] = {"Gill", "Alex", "Ryu", "Yun", "Dudley", "Necro", "Hugo", "Ibuki", "Elena", "Oro", "Yang",
                                  "Ken", "Sean", "Urien", "Akuma", "Akuma (red)", "Chun-Li", "Makoto", "Q", "Twelve", "Remy"};
    std::map<uint32_t, uint16_t> g_custom;      // graphics word: colour
    std::map<uint32_t, uint32_t> g_wordOf;      // colour RAM index (player 1's and 2's): the graphics word it came from
    std::map<uint32_t, uint16_t> g_original;    // graphics word: the game's own colour there
    std::map<std::string, std::map<uint32_t, uint16_t>> g_byCharacter; // (for writing each character's file)
    bool g_loaded = false;
    bool g_forced = false; // (the editor on: custom colours whatever the feature says)

    std::filesystem::path Folder() {
        const std::string &settings = SettingsPath();
        const std::filesystem::path base = settings.empty() ? std::filesystem::path(".") : std::filesystem::path(settings).parent_path();
        return base / "palettes";
    }
    void Load() {
        g_loaded = true;
        g_custom.clear();
        g_byCharacter.clear();
        std::error_code error;
        for (std::filesystem::directory_iterator it(Folder(), error); !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
            if (it->path().extension() != ".txt") {
                continue;
            }
            const std::string name = it->path().stem().string();
            std::ifstream file(it->path());
            std::string line;
            while (std::getline(file, line)) {
                line = line.substr(0, line.find('#'));
                const size_t eq = line.find('=');
                if (eq == std::string::npos) {
                    continue;
                }
                const uint32_t word = static_cast<uint32_t>(strtoul(line.substr(0, eq).c_str(), nullptr, 16));
                const uint16_t colour = static_cast<uint16_t>(strtoul(line.substr(eq + 1).c_str(), nullptr, 16) & 0x7FFF);
                g_custom[word] = colour;
                g_byCharacter[name][word] = colour;
            }
        }
    }

    uint16_t PaletteColour(uint32_t word, uint32_t index, uint16_t colour) {
        if (index < 0x800) {
            g_wordOf[index] = word;
        }
        g_original[word] = static_cast<uint16_t>(colour & 0x7FFF);
        if (!g_loaded) {
            Load();
        }
        const auto it = g_custom.find(word);
        return it == g_custom.end() ? colour : static_cast<uint16_t>((colour & 0x8000) | it->second);
    }
} // namespace

std::string CharacterName(int number) {
    return number >= 0 && number < static_cast<int>(std::size(kNames)) ? kNames[number] : "Unknown";
}

// For the editor: the graphics word player 1's colour `index` (0-0x1FF) came from (false: none known)
bool ColourWord(uint32_t index, uint32_t &word) {
    const auto it = g_wordOf.find(index);
    if (it == g_wordOf.end()) {
        return false;
    }
    word = it->second;
    return true;
}

// A colour changed by the editor: kept for the character, and seen at once (both copies of player 1's colours)
void SetCustomColour(Machine &m, const std::string &character, uint32_t index, uint16_t colour) {
    uint32_t word;
    if (!ColourWord(index, word)) {
        return;
    }
    if (!g_loaded) {
        Load();
    }
    g_custom[word] = colour;
    g_byCharacter[character][word] = colour;
    m.board.WriteColour(index, colour);
    m.board.WriteColour(index + 0x200, colour);
}

// The character's colours (custom or the game's) back to the game's own: forgotten, and its file removed
void ForgetCustomColours(const std::string &character) {
    for (const auto &[word, colour] : g_byCharacter[character]) {
        g_custom.erase(word);
    }
    g_byCharacter.erase(character);
    std::error_code error;
    std::filesystem::remove(Folder() / (character + ".txt"), error);
}

// The character's file written (the editor: a little after its last change)
void SaveCustomColours(const std::string &character) {
    std::error_code error;
    std::filesystem::create_directories(Folder(), error);
    std::ofstream file(Folder() / (character + ".txt"));
    file << "# " << character << "'s own colours for sf3comp (3rd Strike): the graphics word, then the colour\n"
         << "# (15 bits: red the lowest five, then green, then blue), both in hexadecimal\n";
    char line[32];
    for (const auto &[word, colour] : g_byCharacter[character]) {
        snprintf(line, sizeof(line), "%07X = %04X\n", word, colour);
        file << line;
    }
}

void ForceCustomColours(bool on) {
    g_forced = on;
}

// The game's own colour at player 1's colour `index` (0-0x1FF), as its graphics word has it (false: none known)
bool GameColourAt(uint32_t index, uint16_t &colour) {
    uint32_t word;
    if (!ColourWord(index, word)) {
        return false;
    }
    const auto it = g_original.find(word);
    if (it == g_original.end()) {
        return false;
    }
    colour = it->second;
    return true;
}

// The character's colours written out again, every one of them the game's: its file removed (the editor's "Game's
// colours for this character"); and the folder's files read again
void ReloadCustomColours() {
    g_loaded = false;
}

// The character's library of sets (palettes/library/<name>/<saved>.txt, as cps2recomp's colours.h has it): sets of
// player 1's colours saved on their own, to be put into any set. A file: lines "INDEX = COLOUR" (player 1's colour,
// 000-1FF, then the colour), "#" a remark.
namespace {
    std::filesystem::path Library(const std::string &character) {
        return Folder() / "library" / character;
    }
} // namespace
std::vector<std::string> SavedSets(const std::string &character) {
    std::vector<std::string> names;
    std::error_code error;
    for (std::filesystem::directory_iterator it(Library(character), error); !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        if (it->path().extension() == ".txt") {
            names.push_back(it->path().stem().string());
        }
    }
    // (in order: "Ryu 2" before "Ryu 10")
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        const size_t sa = a.find_last_not_of("0123456789") + 1, sb = b.find_last_not_of("0123456789") + 1;
        if (a.compare(0, sa, b, 0, sb) == 0 && sa < a.size() && sb < b.size()) {
            return atoi(a.c_str() + sa) < atoi(b.c_str() + sb);
        }
        return a < b;
    });
    return names;
}
bool SaveSet(const std::string &character, const std::map<uint32_t, uint16_t> &colours, std::string &saved) {
    std::error_code error;
    std::filesystem::create_directories(Library(character), error);
    if (error) {
        return false;
    }
    for (int n = 1; n < 1000; n++) {
        saved = character + " " + std::to_string(n);
        if (!std::filesystem::exists(Library(character) / (saved + ".txt"))) {
            break;
        }
    }
    std::ofstream file(Library(character) / (saved + ".txt"));
    if (!file) {
        return false;
    }
    file << "# " << saved << ": a set of " << character << "'s colours for sf3comp (3rd Strike): player 1's colour (000-1FF), then the\n"
         << "# colour (15 bits: red the lowest five, then green, then blue), both in hexadecimal\n";
    char line[32];
    for (const auto &[index, colour] : colours) {
        snprintf(line, sizeof(line), "%03X = %04X\n", index, colour);
        file << line;
    }
    return static_cast<bool>(file);
}
bool ReadSavedSet(const std::string &character, const std::string &saved, std::map<uint32_t, uint16_t> &colours) {
    std::ifstream file(Library(character) / (saved + ".txt"));
    if (!file) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = line.substr(0, line.find('#'));
        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            colours[static_cast<uint32_t>(strtoul(line.substr(0, eq).c_str(), nullptr, 16)) & 0x1FF] =
                static_cast<uint16_t>(strtoul(line.substr(eq + 1).c_str(), nullptr, 16) & 0x7FFF);
        }
    }
    return true;
}
bool RemoveSavedSet(const std::string &character, const std::string &saved) {
    std::error_code error;
    return std::filesystem::remove(Library(character) / (saved + ".txt"), error) && !error;
}

void RegisterColours(Game &game) {

    if (Machine *machine = TheMachine()) {
        machine->board.paletteColour = PaletteColour;
    }
}

} // namespace cps3recomp
