#include "scores.h"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include "json.hpp"
#include "paths.h"

using json = nlohmann::json;

namespace {
json default_doc() {
    return json{
        {"space_invaders", {{"high_score", 0}, {"best_wave", 1}, {"total_plays", 0}, {"total_score", 0}}},
        {"arkanoid",       {{"high_score", 0}, {"best_level", 1}, {"total_plays", 0}, {"total_score", 0}}},
        {"void_raid",      {{"high_score", 0}, {"best_zone", 1},  {"total_plays", 0}, {"total_score", 0}}},
        {"galaga",         {{"high_score", 0}, {"best_stage", 1}, {"total_plays", 0}, {"total_score", 0}}},
        {"void_drift",     {{"high_score", 0}, {"best_wave", 1},  {"total_plays", 0}, {"total_score", 0}}},
        {"void_gunner",    {{"high_score", 0}, {"best_wave", 1},  {"total_plays", 0}, {"total_score", 0}}},
        {"void_storm",     {{"high_score", 0}, {"best_stage", 1}, {"total_plays", 0}, {"total_score", 0}}},
    };
}
}  // namespace

#define DOC (*reinterpret_cast<json*>(doc_))

ScoreManager::ScoreManager() : doc_(new json(default_doc())) {
    path_ = data_dir() + "scores.json";
    load();
}

void ScoreManager::load() {
    DOC = default_doc();
    std::ifstream f(path_);
    if (!f) return;
    try {
        json saved;
        f >> saved;
        if (!saved.is_object()) return;
        // Merge only well-typed values: stats must be integers and the board an
        // array, so a hand-edited or damaged file can never crash a lookup.
        for (auto& [game, vals] : saved.items()) {
            if (!DOC.contains(game) || !vals.is_object()) continue;
            for (auto& [k, v] : vals.items()) {
                if (k == "board") { if (v.is_array()) DOC[game][k] = v; }
                else if (v.is_number_integer()) DOC[game][k] = v;
            }
        }
    } catch (...) {
        DOC = default_doc();
    }
}

void ScoreManager::save() {
    write_file_atomic(path_, DOC.dump(2));
}

int ScoreManager::get(const std::string& g, const std::string& k, int def) const {
    const json& d = DOC;
    if (d.contains(g) && d[g].contains(k) && d[g][k].is_number_integer())
        return d[g][k].get<int>();
    return def;
}
void ScoreManager::set(const std::string& g, const std::string& k, int v) {
    DOC[g][k] = v;
}

int ScoreManager::high_score(const std::string& g) const { return get(g, "high_score", 0); }
int ScoreManager::total_plays(const std::string& g) const { return get(g, "total_plays", 0); }
int ScoreManager::total_score(const std::string& g) const { return get(g, "total_score", 0); }
int ScoreManager::best_wave()  const { return get("space_invaders", "best_wave", 1); }
int ScoreManager::best_level() const { return get("arkanoid", "best_level", 1); }
int ScoreManager::best_zone()  const { return get("void_raid", "best_zone", 1); }
int ScoreManager::best_stage() const { return get("galaga", "best_stage", 1); }
int ScoreManager::best_drift_wave()  const { return get("void_drift", "best_wave", 1); }
int ScoreManager::best_gunner_wave() const { return get("void_gunner", "best_wave", 1); }
int ScoreManager::best_storm_stage() const { return get("void_storm", "best_stage", 1); }

static bool submit(json& doc, const std::string& g, const std::string& best_key,
                   int score, int best_val) {
    bool changed = false;
    if (score > doc[g]["high_score"].get<int>()) {
        doc[g]["high_score"] = score;
        changed = true;
    }
    if (best_val > doc[g][best_key].get<int>()) doc[g][best_key] = best_val;
    doc[g]["total_plays"] = doc[g]["total_plays"].get<int>() + 1;
    doc[g]["total_score"] = doc[g]["total_score"].get<int>() + score;
    return changed;
}

bool ScoreManager::submit_space_invaders(int s, int w) {
    bool c = submit(DOC, "space_invaders", "best_wave", s, w); save(); return c;
}
bool ScoreManager::submit_arkanoid(int s, int l) {
    bool c = submit(DOC, "arkanoid", "best_level", s, l); save(); return c;
}
bool ScoreManager::submit_void_raid(int s, int z) {
    bool c = submit(DOC, "void_raid", "best_zone", s, z); save(); return c;
}
bool ScoreManager::submit_galaga(int s, int st) {
    bool c = submit(DOC, "galaga", "best_stage", s, st); save(); return c;
}
bool ScoreManager::submit_void_drift(int s, int w) {
    bool c = submit(DOC, "void_drift", "best_wave", s, w); save(); return c;
}
bool ScoreManager::submit_void_gunner(int s, int w) {
    bool c = submit(DOC, "void_gunner", "best_wave", s, w); save(); return c;
}
bool ScoreManager::submit_void_storm(int s, int st) {
    bool c = submit(DOC, "void_storm", "best_stage", s, st); save(); return c;
}

// ── leaderboard ──────────────────────────────────────────────────────────────
std::vector<ScoreManager::Entry> ScoreManager::leaderboard(
    const std::string& g) const {
    std::vector<Entry> out;
    const json& d = DOC;
    if (d.contains(g) && d[g].contains("board"))
        for (const auto& e : d[g]["board"]) {
            if (!e.is_object()) continue;
            auto n = e.find("name"); auto sc = e.find("score");
            out.push_back({n != e.end() && n->is_string() ? n->get<std::string>() : "AAA",
                           sc != e.end() && sc->is_number_integer() ? sc->get<int>() : 0});
        }
    return out;
}

bool ScoreManager::qualifies(const std::string& g, int score) const {
    if (score <= 0) return false;
    auto board = leaderboard(g);
    if ((int)board.size() < kBoardSize) return true;
    return score > board.back().score;
}

void ScoreManager::add_score(const std::string& g, const std::string& name,
                             int score) {
    auto board = leaderboard(g);
    board.push_back({name, score});
    std::sort(board.begin(), board.end(),
              [](const Entry& a, const Entry& b) { return a.score > b.score; });
    if ((int)board.size() > kBoardSize) board.resize(kBoardSize);
    json arr = json::array();
    for (auto& e : board) arr.push_back({{"name", e.name}, {"score", e.score}});
    DOC[g]["board"] = arr;
    save();
}
