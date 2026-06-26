// telemetry.h — append-only JSON-lines event log for balance analysis.
// Enabled by pointing VOID_TELEMETRY at a file; games push events through it.
#pragma once

#include <cstdio>
#include <fstream>
#include <string>

// Tiny JSON object builder: J().add("ev","wave_clear").add("wave",12).str()
struct J {
    std::string s;
    bool first = true;
    void sep() { if (!first) s += ","; first = false; }
    J& add(const std::string& k, long long v) { sep(); s += "\"" + k + "\":" + std::to_string(v); return *this; }
    J& add(const std::string& k, int v) { return add(k, (long long)v); }
    J& add(const std::string& k, double v) {
        sep(); char b[32]; std::snprintf(b, sizeof b, "%.3f", v);
        s += "\"" + k + "\":" + b; return *this;
    }
    J& add(const std::string& k, const std::string& v) {
        sep(); s += "\"" + k + "\":\"" + v + "\""; return *this;
    }
    std::string str() const { return "{" + s + "}"; }
};

class Telemetry {
public:
    void open(const std::string& path) { f_.open(path, std::ios::app); }
    bool enabled() const { return f_.is_open(); }
    void event(const std::string& line) { if (f_.is_open()) { f_ << line << "\n"; f_.flush(); } }
    void event(const J& j) { event(j.str()); }

private:
    std::ofstream f_;
};
