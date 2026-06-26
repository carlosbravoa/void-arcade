// void_drift.h — Void Drift (Asteroids inspired): vector rocks, inertial drift.
#pragma once

#include <memory>

#include "engine/game.h"

std::unique_ptr<Scene> make_void_drift(const Context& ctx);
