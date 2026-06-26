// void_gunner.h — Void Gunner (first-person pseudo-3D rail shooter).
#pragma once

#include <memory>

#include "engine/game.h"

std::unique_ptr<Scene> make_void_gunner(const Context& ctx);
