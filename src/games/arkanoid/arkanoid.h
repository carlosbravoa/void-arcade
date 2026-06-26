// arkanoid.h — Void Breaker (Arkanoid inspired). Ported from games/arkanoid/*.py.
#pragma once

#include <memory>

#include "engine/game.h"

std::unique_ptr<Scene> make_arkanoid(const Context& ctx);
