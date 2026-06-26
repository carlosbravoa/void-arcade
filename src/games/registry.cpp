#include "registry.h"

#include "games/arkanoid/arkanoid.h"
#include "games/galaga/galaga.h"
#include "games/space_invaders/space_invaders.h"
#include "games/void_drift/void_drift.h"
#include "games/void_gunner/void_gunner.h"
#include "games/void_raid/void_raid.h"
#include "games/void_storm/void_storm.h"

std::unique_ptr<Scene> make_game(const std::string& id, const Context& ctx) {
    if (id == "space_invaders") return make_space_invaders(ctx);
    if (id == "arkanoid") return make_arkanoid(ctx);
    if (id == "void_raid") return make_void_raid(ctx);
    if (id == "galaga") return make_galaga(ctx);
    if (id == "void_drift") return make_void_drift(ctx);
    if (id == "void_gunner") return make_void_gunner(ctx);
    if (id == "void_storm") return make_void_storm(ctx);
    return nullptr;
}
