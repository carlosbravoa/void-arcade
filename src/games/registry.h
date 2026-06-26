// registry.h — factory mapping a game id to a playable Scene.
#pragma once

#include <memory>
#include <string>

#include "engine/game.h"

// Returns nullptr for ids not yet ported (caller stays in the launcher).
std::unique_ptr<Scene> make_game(const std::string& id, const Context& ctx);
