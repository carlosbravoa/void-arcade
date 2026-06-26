// void_storm.h — Void Storm (R-Type/Gradius inspired horizontal side-scroller).
#pragma once

#include <memory>

#include "engine/game.h"

std::unique_ptr<Scene> make_void_storm(const Context& ctx);
