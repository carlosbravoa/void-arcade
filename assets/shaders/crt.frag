#version 330 core

uniform sampler2D screen_texture;
uniform float time;
uniform vec2 resolution;

// Big-hit shockwave: an expanding ring of screen-space displacement.
uniform float shock;          // 0 = idle, else envelope 0..1 (fades as ring expands)
uniform float shock_radius;   // current ring radius in normalized space
uniform vec2  shock_center;   // top-left fractional coords (matches flipped uv)

// Global hue drift so deep waves/zones feel different.
uniform float hue;            // rotation in radians (0 = none)

// Player-tunable look (driven by the in-game Options screen).
uniform float glow;           // bloom intensity (≈0.95 default)
uniform float scan_strength;  // scanline depth (≈0.18 default)

in vec2 uv;
out vec4 fragColor;

// Rotate an RGB colour around the luma axis by `a` radians (cheap hue shift).
vec3 hue_rotate(vec3 c, float a) {
    const vec3 k = vec3(0.57735);  // 1/sqrt(3)
    float cosA = cos(a);
    return c * cosA + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - cosA);
}

vec2 crt_curve(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    // Larger divisors = subtler warp. Was vec2(6.0, 4.0) — very pronounced.
    vec2 offset = abs(uv.yx) / vec2(14.0, 10.0);
    uv = uv + uv * offset * offset;
    uv = uv * 0.5 + 0.5;
    return uv;
}

vec3 sample_chroma(sampler2D tex, vec2 uv, float strength) {
    vec2 offset = (uv - 0.5) * strength;
    float r = texture(tex, uv + offset * 1.0).r;
    float g = texture(tex, uv            ).g;
    float b = texture(tex, uv - offset * 1.0).b;
    return vec3(r, g, b);
}

vec3 blur(sampler2D tex, vec2 uv, float radius) {
    vec2 texel = radius / resolution;
    vec3 col = vec3(0.0);
    col += texture(tex, uv + vec2(-1,-1)*texel).rgb * 0.0625;
    col += texture(tex, uv + vec2( 0,-1)*texel).rgb * 0.125;
    col += texture(tex, uv + vec2( 1,-1)*texel).rgb * 0.0625;
    col += texture(tex, uv + vec2(-1, 0)*texel).rgb * 0.125;
    col += texture(tex, uv              ).rgb        * 0.25;
    col += texture(tex, uv + vec2( 1, 0)*texel).rgb * 0.125;
    col += texture(tex, uv + vec2(-1, 1)*texel).rgb * 0.0625;
    col += texture(tex, uv + vec2( 0, 1)*texel).rgb * 0.125;
    col += texture(tex, uv + vec2( 1, 1)*texel).rgb * 0.0625;
    return col;
}

void main() {
    // Flip Y: pygame surface origin is top-left, OpenGL is bottom-left
    vec2 flipped_uv = vec2(uv.x, 1.0 - uv.y);

    // Shockwave: push samples along the radius near the expanding ring so the
    // whole image ripples outward from the impact for a moment.
    if (shock > 0.001) {
        vec2 d = flipped_uv - shock_center;
        d.x *= resolution.x / resolution.y;        // aspect-correct so it's circular
        float dist = length(d);
        float ring = dist - shock_radius;
        float band = exp(-ring * ring * 130.0);    // thin moving band
        float disp = sin(ring * 46.0) * band * 0.014 * shock;
        vec2 dir = normalize(flipped_uv - shock_center + vec2(1e-5));
        flipped_uv += dir * disp;
    }

    vec2 curved_uv = crt_curve(flipped_uv);

    if (curved_uv.x < 0.0 || curved_uv.x > 1.0 ||
        curved_uv.y < 0.0 || curved_uv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 base = sample_chroma(screen_texture, curved_uv, 0.003);

    vec3 blurred1 = blur(screen_texture, curved_uv, 2.0);
    vec3 blurred2 = blur(screen_texture, curved_uv, 5.0);
    vec3 blurred3 = blur(screen_texture, curved_uv, 10.0);

    vec3 bright1 = max(blurred1 - 0.4, 0.0) * 2.5;
    vec3 bright2 = max(blurred2 - 0.3, 0.0) * 1.8;
    vec3 bright3 = max(blurred3 - 0.2, 0.0) * 1.2;

    vec3 bloom = bright1 * 0.5 + bright2 * 0.35 + bright3 * 0.15;
    // Glow gain is player-tunable (Options screen). Lower = more transparent.
    vec3 color = base + bloom * glow;

    float scanline = sin(curved_uv.y * resolution.y * 3.14159) * 0.5 + 0.5;
    scanline = pow(scanline, 0.6);
    color *= mix(1.0 - scan_strength, 1.0, scanline);

    float grid_x = sin(curved_uv.x * resolution.x * 3.14159) * 0.5 + 0.5;
    color *= mix(0.96, 1.0, grid_x);

    vec2 vig_uv = curved_uv * (1.0 - curved_uv.yx);
    float vignette = pow(vig_uv.x * vig_uv.y * 18.0, 0.35);
    color *= vignette;

    color *= vec3(0.96, 1.02, 0.92);

    float flicker = 1.0 - 0.015 * sin(time * 60.0 + 0.3);
    color *= flicker;

    float edge_glow = 1.0 - vignette;
    edge_glow = pow(edge_glow, 5.0) * 0.2;
    color += vec3(0.03, 0.18, 0.03) * edge_glow;

    // Per-wave/zone hue drift (subtle global tint shift).
    if (abs(hue) > 0.0001) color = hue_rotate(color, hue);

    // Bright bloom flash riding the shockwave ring.
    if (shock > 0.001) {
        vec2 d = curved_uv - shock_center;
        d.x *= resolution.x / resolution.y;
        float ring = length(d) - shock_radius;
        color += vec3(0.25, 0.45, 0.30) * exp(-ring * ring * 90.0) * shock * 0.6;
    }

    fragColor = vec4(color, 1.0);
}
