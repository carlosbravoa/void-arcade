// galaga.h — Void Squadrons (Galaga inspired). Ported from games/galaga/*.py.
#pragma once

#include <memory>

#include "engine/game.h"

std::unique_ptr<Scene> make_galaga(const Context& ctx);
