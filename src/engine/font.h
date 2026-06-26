// font.h — monospace text rendering via stb_truetype.
//
// Mirrors the Python FreetypeWrapper: render(text, color) returns a Surface
// (SRCALPHA, glyph coverage in the alpha channel) ready to blit. Glyph bitmaps
// are cached per codepoint since the UI re-renders text every frame.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "color.h"
#include "stb_truetype.h"

class Surface;

class Font {
public:
    // Load a .ttf at the given pixel height. Throws std::runtime_error on fail.
    Font(const std::string& ttf_path, float pixel_height);

    // Rendered text as an SRCALPHA Surface tinted with `color`.
    std::shared_ptr<Surface> render(const std::string& text, Color color);

    int text_width(const std::string& text) const;
    int height() const { return line_h_; }

private:
    struct Glyph {
        std::vector<uint8_t> cov;  // coverage bitmap, w*h
        int w = 0, h = 0;
        int xoff = 0, yoff = 0;    // from baseline/pen
        int advance = 0;
    };
    const Glyph& glyph(int cp);

    std::vector<uint8_t>             ttf_;
    stbtt_fontinfo                   info_{};
    float                            scale_ = 1.0f;
    int                              ascent_ = 0, descent_ = 0, line_gap_ = 0;
    int                              line_h_ = 0, baseline_ = 0;
    std::unordered_map<int, Glyph>   cache_;
};
