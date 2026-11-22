#include "font.h"

#include <fstream>
#include <stdexcept>

#include "surface.h"

namespace {
// Minimal UTF-8 -> codepoint decoder (enough for the UI's arrows/triangles).
std::vector<int> decode_utf8(const std::string& s) {
    std::vector<int> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = s[i];
        int cp, extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else { cp = c; extra = 0; }
        ++i;
        for (int k = 0; k < extra && i < n; ++k, ++i)
            cp = (cp << 6) | (s[i] & 0x3F);
        out.push_back(cp);
    }
    return out;
}
}  // namespace

Font::Font(const std::string& ttf_path, float pixel_height) {
    std::ifstream f(ttf_path, std::ios::binary);
    if (!f) throw std::runtime_error("font not found: " + ttf_path);
    ttf_.assign(std::istreambuf_iterator<char>(f),
                std::istreambuf_iterator<char>());
    if (!stbtt_InitFont(&info_, ttf_.data(),
                        stbtt_GetFontOffsetForIndex(ttf_.data(), 0)))
        throw std::runtime_error("stbtt_InitFont failed: " + ttf_path);

    scale_ = stbtt_ScaleForPixelHeight(&info_, pixel_height);
    stbtt_GetFontVMetrics(&info_, &ascent_, &descent_, &line_gap_);
    baseline_ = int(ascent_ * scale_ + 0.5f);
    line_h_   = int((ascent_ - descent_ + line_gap_) * scale_ + 0.5f);
}

const Font::Glyph& Font::glyph(int cp) {
    auto it = cache_.find(cp);
    if (it != cache_.end()) return it->second;

    Glyph g;
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&info_, cp, &adv, &lsb);
    g.advance = int(adv * scale_ + 0.5f);

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&info_, cp, scale_, scale_, &x0, &y0, &x1, &y1);
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.xoff = x0;
    g.yoff = y0;  // relative to baseline (negative = above)
    if (g.w > 0 && g.h > 0) {
        g.cov.resize(size_t(g.w) * g.h);
        stbtt_MakeCodepointBitmap(&info_, g.cov.data(), g.w, g.h, g.w, scale_,
                                  scale_, cp);
    }
    auto res = cache_.emplace(cp, std::move(g));
    return res.first->second;
}

int Font::text_width(const std::string& text) const {
    int w = 0;
    for (int cp : decode_utf8(text)) {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info_, cp, &adv, &lsb);
        w += int(adv * scale_ + 0.5f);
    }
    return w;
}

std::shared_ptr<Surface> Font::render(const std::string& text, Color color) {
    int w = std::max(1, text_width(text));
    int h = std::max(1, line_h_);
    auto surf = std::make_shared<Surface>(w, h, /*srcalpha=*/true);

    int pen = 0;
    for (int cp : decode_utf8(text)) {
        const Glyph& g = glyph(cp);
        for (int yy = 0; yy < g.h; ++yy) {
            for (int xx = 0; xx < g.w; ++xx) {
                uint8_t cov = g.cov[size_t(yy) * g.w + xx];
                if (!cov) continue;
                int px = pen + g.xoff + xx;
                int py = baseline_ + g.yoff + yy;
                // Honour the colour's own alpha so callers can fade text.
                int a = color.a == 255 ? cov : cov * color.a / 255;
                surf->set_at(px, py, Color(color.r, color.g, color.b, a));
            }
        }
        pen += g.advance;
    }
    return surf;
}
