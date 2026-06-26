// void_raid.h — Void Raid (River Raid inspired). Native port.
#pragma once

#include <memory>

#include "engine/game.h"

std::unique_ptr<Scene> make_void_raid(const Context& ctx);
