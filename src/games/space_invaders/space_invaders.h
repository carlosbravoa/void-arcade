// space_invaders.h — Void Invaders (Space Invaders inspired). Reference port.
#pragma once

#include <memory>

#include "engine/game.h"

std::unique_ptr<Scene> make_space_invaders(const Context& ctx);
