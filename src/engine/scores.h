// scores.h — persistent high scores / stats, ported from shared/scores.py.
// Reads & writes a JSON file (scores.json) compatible with the Python format.
#pragma once

#include <string>
#include <utility>
#include <vector>

class ScoreManager {
public:
    struct Entry { std::string name; int score; };

    ScoreManager();

    void load();
    void save();

    int high_score(const std::string& game_id) const;
    int total_plays(const std::string& game_id) const;
    int total_score(const std::string& game_id) const;

    int best_wave()  const;  // space_invaders
    int best_level() const;  // arkanoid
    int best_zone()  const;  // void_raid
    int best_stage() const;  // galaga
    int best_drift_wave()  const;  // void_drift (asteroids)
    int best_gunner_wave() const;  // void_gunner (first-person)
    int best_storm_stage() const;  // void_storm (side-scroller)

    bool submit_space_invaders(int score, int wave);
    bool submit_arkanoid(int score, int level);
    bool submit_void_raid(int score, int zone);
    bool submit_galaga(int score, int stage);
    bool submit_void_drift(int score, int wave);
    bool submit_void_gunner(int score, int wave);
    bool submit_void_storm(int score, int stage);

    // ── leaderboard (top 10 per game) ──
    static constexpr int kBoardSize = 10;
    std::vector<Entry> leaderboard(const std::string& game_id) const;
    bool qualifies(const std::string& game_id, int score) const;
    void add_score(const std::string& game_id, const std::string& name, int score);

private:
    std::string path_;
    // pImpl-free: store the JSON document opaquely via a void* to avoid leaking
    // the heavy json header into every includer.
    void* doc_;
    int get(const std::string& g, const std::string& k, int def) const;
    void set(const std::string& g, const std::string& k, int v);
};
