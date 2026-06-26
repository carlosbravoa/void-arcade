# Native C++ engine guide (SDL2/GL)

You are building a game on the native C++ engine in `native/src/engine`.
**The `space_invaders.cpp` reference shows every engine pattern you need** (Scene
subclass, pause/ESC, sprites, sfx, HUD, power-ups, bosses, juice, telemetry, bot).

## Philosophy — this is NOT a 1:1 port anymore
The pygame originals were the starting point and a guide to the *look and feel*.
They are NOT a spec. The whole reason for the SDL rewrite is **freedom, performance,
and room to add features.** So:
- **Optimize freely.** Restructure, cache, batch — whatever's faster/cleaner.
- **Change behavior to make it more fun.** New enemies, attacks, power-ups,
  progression, bosses, juice — all encouraged. Keep the neon-on-near-black CRT
  identity, but invent within it.
- Don't preserve a Python constant just because it was there; tune for feel.
- When in doubt, make it more awesome, not more faithful.

## Hard rules (still real)
- Write ONLY your game's files: `native/src/games/<name>/<name>.h` and `<name>.cpp`.
- Do NOT edit `CMakeLists.txt` or `registry.cpp` (the parent wires those; sources are globbed).
- Expose exactly one factory: `std::unique_ptr<Scene> make_<name>(const Context& ctx);`
- The code MUST compile against the headers below.

## Engine API (read the headers for exact signatures)

`engine/color.h`
- `struct Color { uint8_t r,g,b,a=255; Color(int,int,int,int=255); }`
- `Color lerp_color(Color,Color,float t); Color brighten(Color,float=1.4);`

`engine/surface.h`
- `struct Rect { int x,y,w,h; };`
- `class Surface { Surface(int w,int h,bool srcalpha=false); int width(),height();
   void fill(Color); void fill(Color,Rect); void set_at(int,int,Color); Color get_at(int,int);
   void set_alpha(int); void blit(const Surface&,int dx,int dy,Blend=Blend::Alpha);
   Surface scaled(int,int); };`
- `enum class Blend { Alpha, RgbaMult };`
- `namespace draw { rect(Surface&,Color,Rect,int width=0,int border_radius=0);
   circle(Surface&,Color,int cx,int cy,int r,int width=0);
   line(Surface&,Color,int x1,int y1,int x2,int y2,int width=1);
   polygon(Surface&,Color,const std::vector<std::pair<int,int>>&,int width=0); }`

`engine/sprite.h`
- `Surface make_sprite_surface(const std::vector<std::string>& lines, Color, int scale);`
- `std::pair<int,int> sprite_size(lines, int scale);`
- `std::shared_ptr<Surface> glow_surface(const Surface& base, Color, int radius=8, float intensity=0.7);`

`engine/text.h` (Fonts come from `ctx_.fonts.big/med/small`, which are `Font*`)
- `draw_text(Surface&, Font&, const std::string&, int x, int y, Color);`
- `draw_text_centered(Surface&, Font&, const std::string&, int cx, int y, Color);`
- `draw_text_right(Surface&, Font&, const std::string&, int x_right, int y, Color);`
- Need text width? `ctx_.fonts.med->text_width(str)` and `->render(str,color)` returns `shared_ptr<Surface>`.

`engine/particles.h` — `ParticleSystem`: `update(float)`, `draw(Surface&)`, `clear()`,
  `explode_invader(x,y,Color,count=28)`, `explode_player(x,y,count=50)`,
  `explode_brick(x,y,Color,count=18)`, `ball_trail(x,y,Color)`, `paddle_hit(x,y,count=10)`,
  `bullet_trail(x,y,Color,bool up=true)`, `bunker_hit(x,y,count=12)`, `ufo_explode(x,y,count=40)`,
  `ambient_star(int w,int h)`. (All coords are float/int.)

`engine/sound.h` — `SoundManager* ctx_.sfx` (may be used directly; it is never null in practice
  but guard with `if (ctx_.sfx)` like the reference). Methods:
  Invaders: `shoot, invader_hit, player_death, bunker_hit, march_tick, start_ufo, stop_ufo`.
  Breaker: `ball_wall, ball_paddle, brick_hit, brick_break, life_lost, level_clear, laser_shoot, powerup_collect`.
  Squadrons: `dive_warble, capture, rescue, boss_explosion, low_energy_pip, spawn_chime, start_tractor, stop_tractor`.
  Map the Python `sfx.<call>()` names 1:1 (they match).

`engine/input.h` — `struct Input { bool down(SDL_Keycode) const;   // held (pygame.key.get_pressed)
   std::vector<SDL_Keycode> pressed;   // KEYDOWN this frame
   bool just(SDL_Keycode) const; };`  Use `SDLK_LEFT, SDLK_RIGHT, SDLK_a, SDLK_d, SDLK_SPACE,
   SDLK_UP, SDLK_w, SDLK_q, SDLK_e, SDLK_RETURN, SDLK_ESCAPE, SDLK_p`, etc.
   NOTE: `M` (mute) and `F11` (fullscreen) are handled globally by the app — ignore them.

`engine/pause.h` — `PauseMenu(int W,int H,Fonts)`; `bool active()`, `void toggle()`,
  `PauseResult update(const Input&)` → `enum class PauseResult { None, Resume, QuitToMenu }`,
  `void draw(Surface&)`.

`engine/game.h` — base `class Scene { Scene(const Context&); Context ctx_; bool quit_to_menu;
   virtual void update(float dt,const Input&)=0; virtual void draw(Surface&)=0; virtual void on_exit(){} };`
  `struct Context { int W,H; Fonts fonts; SoundManager* sfx; ScoreManager* scores; };`

`engine/rng.h` — `rnd_uniform(a,b)`, `rnd_int(a,b)` (INCLUSIVE like random.randint),
  `rnd01()`, `rnd_choice<T>({...})`. Use these for Python's random.* .

`engine/scores.h` — `ScoreManager*`: `high_score(id)`, `total_plays(id)`,
  `best_level()` (arkanoid), `best_zone()` (void_raid), `best_stage()` (galaga),
  `submit_arkanoid(score,level)`, `submit_void_raid(score,zone)`, `submit_galaga(score,stage)` (all return bool=new high).

## Pause / ESC / quit pattern (copy from reference)
```cpp
void update(float dt, const Input& in) override {
  for (auto k : in.pressed)
    if (k == SDLK_p && state_ == /*playing*/) { pause_.toggle();
        if (ctx_.sfx && pause_.active()) ctx_.sfx->/*stop loops*/; }
  if (pause_.active()) {
    if (pause_.update(in) == PauseResult::QuitToMenu) quit_to_menu = true;
    return;
  }
  for (auto k : in.pressed) if (k == SDLK_ESCAPE) { quit_to_menu = true; return; }
  // ... normal update; fire on KEYDOWN via in.pressed; movement via in.down() ...
}
void on_exit() override { submit_scores(); /* stop any sfx loops */ }
```
Use a private `bool submitted_` so `submit_scores()` only counts a play once.

## pygame → engine cheatsheet
- `pygame.Surface((w,h), pygame.SRCALPHA)` → `Surface(w,h,true)`
- `pygame.Rect(x,y,w,h)` → `Rect{x,y,w,h}`; collision `a.colliderect(b)` → write a helper
  `bool overlap(Rect a,Rect b){return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;}`
- `pygame.draw.rect(s,c,(x,y,w,h),width,border_radius=r)` → `draw::rect(s,c,{x,y,w,h},width,r)`
- `surf.blit(src,(x,y))` → `s.blit(src,x,y)`;  BLEND_RGBA_MULT → `Blend::RgbaMult`
- `font.render(t,True,c)` → `ctx_.fonts.X->render(t,c)` (returns shared_ptr<Surface>); usually prefer `draw_text*`
- `f"{v:06d}"` → zero-pad helper; `f"{v:,}"` → comma helper (see reference `zpad`)
- `math.sin/cos/hypot` → `std::sin/cos/hypot` from `<cmath>`; `math.tau` = 6.2831853f
- Python list comprehensions filtering dead entities → `std::remove_if` + `erase`.

## Output checklist
- `<name>.h`: include guard, `#include "engine/game.h"`, declare `make_<name>(const Context&)`.
- `<name>.cpp`: implement everything in an anonymous namespace + the factory at the bottom.
- Compiles cleanly; no dependency on pygame semantics not provided above.
- Tune for fun, not fidelity. Reuse the original's pixel-art/sprites where they look
  good, but feel free to redraw, add, or replace anything to make the game better.
