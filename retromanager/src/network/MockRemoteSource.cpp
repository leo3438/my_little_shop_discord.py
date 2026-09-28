#include "retromanager/network/MockRemoteSource.hpp"

#include <thread>

namespace rm {

const char* MockRemoteSource::demoIndex() {
    // Relative URLs on purpose: they resolve against indexUrl(), like a real
    // index sitting next to its ROMs on a NAS.
    return R"json({
  "version": 1,
  "name": "Boutique de démonstration",
  "success": "Bienvenue sur la boutique de démonstration RetroManager !",
  "games": [
    {"title": "Pokémon Platine", "system": "nds", "region": "EUR", "size": 134217728,
     "url": "roms/nds/Pokemon%20Platine%20(France).nds", "boxart": "boxart/nds/pokemon-platine.png", "year": 2009},
    {"title": "Mario Kart DS", "system": "nds", "region": "EUR", "size": 33554432,
     "url": "roms/nds/Mario%20Kart%20DS%20(Europe).nds", "year": 2005},
    {"title": "The Legend of Zelda: A Link to the Past", "system": "snes", "region": "USA", "size": 1048576,
     "url": "roms/snes/Legend%20of%20Zelda,%20The%20-%20A%20Link%20to%20the%20Past%20(USA).sfc", "year": 1991},
    {"title": "Super Mario World", "system": "snes", "region": "USA", "size": 524288,
     "url": "roms/snes/Super%20Mario%20World%20(USA).sfc", "year": 1990},
    {"title": "Super Metroid", "system": "snes", "region": "EUR", "size": 3145728,
     "url": "roms/snes/Super%20Metroid%20(Europe).sfc", "year": 1994},
    {"title": "Chrono Trigger", "system": "snes", "region": "USA", "size": 4194304,
     "url": "roms/snes/Chrono%20Trigger%20(USA).sfc", "year": 1995},
    {"title": "Advance Wars", "system": "gba", "region": "USA", "size": 8388608,
     "url": "roms/gba/Advance%20Wars%20(USA).gba", "year": 2001},
    {"title": "Metroid Fusion", "system": "gba", "region": "EUR", "size": 8388608,
     "url": "roms/gba/Metroid%20Fusion%20(Europe).gba", "year": 2002},
    {"title": "Pokémon Émeraude", "system": "gba", "region": "EUR", "size": 16777216,
     "url": "roms/gba/Pokemon%20Emeraude%20(France).gba", "year": 2005},
    {"title": "Tetris", "system": "gb", "region": "WLD", "size": 32768,
     "url": "roms/gb/Tetris%20(World).gb", "year": 1989},
    {"title": "The Legend of Zelda: Link's Awakening DX", "system": "gbc", "region": "EUR", "size": 1048576,
     "url": "roms/gbc/Legend%20of%20Zelda,%20The%20-%20Link's%20Awakening%20DX%20(Europe).gbc", "year": 1998},
    {"title": "Sonic the Hedgehog", "system": "megadrive", "region": "EUR", "size": 524288,
     "url": "roms/megadrive/Sonic%20The%20Hedgehog%20(USA,%20Europe).md", "year": 1991},
    {"title": "Streets of Rage 2", "system": "megadrive", "region": "EUR", "size": 2097152,
     "url": "roms/megadrive/Streets%20of%20Rage%202%20(Europe).md", "year": 1992},
    {"title": "Super Mario Bros. 3", "system": "nes", "region": "USA", "size": 393232,
     "url": "roms/nes/Super%20Mario%20Bros.%203%20(USA).nes", "year": 1990},
    {"title": "Castlevania", "system": "nes", "region": "EUR", "size": 131088,
     "url": "roms/nes/Castlevania%20(Europe).nes", "year": 1988}
  ]
})json";
}

MockRemoteSource::MockRemoteSource(std::string document, std::string indexUrl)
    : document_(std::move(document)), indexUrl_(std::move(indexUrl)) {}

std::string MockRemoteSource::describe() const { return "mock: " + indexUrl_; }

Result<std::string> MockRemoteSource::fetchIndex() {
    ++fetchCount_;
    if (latency_.count() > 0) std::this_thread::sleep_for(latency_);
    if (failure_) return *failure_;
    return document_;
}

}  // namespace rm
