// rng.h — shared random helpers mirroring Python's random module.
#pragma once

#include <initializer_list>
#include <random>

inline std::mt19937& rng() {
    static std::mt19937 e(0x1234ABCD);
    return e;
}
inline float rnd_uniform(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng());
}
inline int rnd_int(int a, int b) {  // inclusive, like random.randint
    std::uniform_int_distribution<int> d(a, b);
    return d(rng());
}
inline float rnd01() { return rnd_uniform(0.0f, 1.0f); }

template <typename T>
T rnd_choice(std::initializer_list<T> items) {
    int i = rnd_int(0, int(items.size()) - 1);
    return *(items.begin() + i);
}
