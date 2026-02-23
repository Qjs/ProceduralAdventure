This is a very strong direction. It combines:

Your procedural island (terrain matters)

Your Box2D / fleet instincts (group dynamics)

AI systems (boids + tactical logic)

Role-based gameplay (archer, melee, healer, mage)

Let’s design this cleanly and technically so it’s implementable in C/SDL.

1. Core Game Concept

You control one unit directly (movement + special ability).

Your squad:

Moves using a modified boid system

Maintains formation relative to you

Chooses actions autonomously based on:

Enemy distance

Cooldowns

Role

Local danger

Enemies:

Spawn in camps / patrol groups

Also use simplified boid or formation logic

Have role archetypes too

Map:

Polygon island

Biomes affect movement, vision, maybe magic

Rivers slow movement or block line of sight

1. System Architecture

Think in 3 layers:

Movement layer (boids + steering)

Tactical layer (role decision logic)

Ability layer (cooldowns, projectiles, spells)

1. Movement: Tactical Boids

Classic boids have:

Separation

Alignment

Cohesion

You need a tactical version.

Base Steering Components

For each AI teammate:

force =
    w_sep * separation()

+ w_align * alignment()
+ w_coh * cohesion()
+ w_follow * follow_player()
+ w_avoid * avoid_enemies()
+ w_goal * attack_position()

But weights change by role.

Role-Based Movement Styles
Melee

Strong cohesion

Strong attack_position

Low separation near enemies

Aggressively closes distance

Archer

Maintain optimal radius from target

Avoid enemies strongly

Lower cohesion

High separation

Healer

Maintain proximity to player

Avoid enemies heavily

Seek injured allies

Mage

Medium range

Prefers clumping enemies

Avoid melee proximity

So each unit has:

struct RoleParams {
    float optimal_range;
    float cohesion_weight;
    float separation_weight;
    float attack_weight;
    float retreat_weight;
};
4. Tactical Decision Layer

Each unit runs a lightweight state machine:

States:

FOLLOW

ATTACK

RETREAT

CAST

HEAL

REPOSITION

Transitions based on:

distance_to_enemy
cooldown_ready
health_percent
ally_health
enemy_density

Example for archer:

IF enemy in optimal range AND cooldown_ready
-> ATTACK
ELSE IF enemy too close
-> RETREAT
ELSE
-> FOLLOW

This is deterministic and easy to debug.

---

# 5. Combat Model

Keep combat readable.

Each unit has:

struct CombatStats {
float hp;
float max_hp;
float attack_range;
float damage;
float cooldown;
float cooldown_timer;
};

Combat loop:

+ Reduce cooldown_timer by dt
+ If timer <= 0 and in range:
  + spawn projectile or apply effect
  + reset timer

Projectile system:

+ Simple ballistic or straight-line
+ Can use Box2D sensors or custom collision

---

# 6. Enemy AI

Enemies should be simpler but dangerous.

Option A: Mirror system (same boid structure)
Option B: Squad-based AI:

+ Leader target = player
+ Others orbit leader
+ Rush or kite depending on type

You can also have:

+ Camps that activate on proximity
+ Patrol routes along polygon edges
+ Territorial behavior per biome

---

# 7. How Terrain Matters

Your procedural island can influence gameplay.

Biome effects:

+ Forest: reduced vision
+ Desert: stamina drain
+ Mountain: slower movement
+ River: slows crossing
+ Swamp: healer bonus?

Polygon cells:

+ Control zones
+ Capture mechanics
+ Ambush zones

You could even:

+ Let mage draw power from certain biomes
+ Let archers gain height advantage from elevation

Now your map generation becomes gameplay-relevant.

---

# 8. Player Control Model

You control:

+ Movement (WASD or click-to-move)
+ One special ability (dash, shield, burst)

Team AI:

+ Automatically adjusts
+ You influence indirectly via position

Optional:

+ Tactical command wheel:
  + Aggressive
  + Defensive
  + Hold Position
  + Spread
  + Focus Target

This modifies boid weights globally.

---

# 9. Formation Layer (Advanced)

Instead of pure boids, add a soft formation anchor:

Each teammate has a preferred offset relative to player:

desired_pos = player_pos + role_offset

Then steering tries to:

+ Move toward desired_pos
+ Still obey separation
+ Adjust dynamically under pressure

This prevents chaotic swarm behavior.

---

# 10. Progression Systems

This is where the game becomes addictive.

Ideas:

+ Unlock new roles
+ Upgrade role parameters
+ Modify boid weights
+ Unlock synergy perks:
  + Healer increases cohesion
  + Mage increases AoE radius
  + Archer gains piercing

Island map progression:

+ Clear biome zones
+ Boss in mountain region
+ Necromancer swamp

---

# 11. Strategic Depth Possibilities

You could introduce:

Enemy composition counters:

+ Heavy melee punishes archer-heavy teams
+ Mage AoE punishes tight cohesion

So player must:

+ Move intelligently
+ Position for optimal AI behavior
+ Avoid exposing squishy units

You’re controlling the "brain" of the swarm.

---

# 12. Minimal Viable Version

To avoid scope explosion:

Phase 1:

+ 4 teammates (one of each role)
+ 2 enemy types
+ Flat terrain
+ Basic cooldown attacks
+ Steering + follow_player

Phase 2:

+ Terrain effects
+ River slowdown
+ Biome bonuses

Phase 3:

+ Formation system
+ Command modes
+ Boss fights

---

# 13. Why This Is Strong

It merges:

+ Your interest in boids and physics
+ Strategy without RTS micro overload
+ Emergent combat
+ Procedural replayability

It feels like:

+ A small-scale tactical RTS
+ But player-centered and physics-aware

 Tactical Squad Game — Phase 1

 Context

 Build the game layer on top of the existing procedural island map (SDL3 + cimgui, C). The player controls one unit via WASD.
 Four AI squad members (melee, archer, healer, mage) follow using tactical boids. Enemy camps on the island activate on
 proximity. Combat is cooldown-based. Terrain (elevation, water) affects movement. Defined in game_design.md.

 New Files

 All under src/game/:

 ┌─────────────────┬────────────────────────────────────────────────────────────────────────────────────────────────────────┐
 │      File       │                                                Purpose                                                 │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_types.h       │ All game structs: Unit, EnemyCamp, Projectile, GameState, enums, Vec2 math helpers                     │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_terrain.h/c   │ Reusable spatial grid for cell lookup (extracted from mg_raster.c pattern), elevation/water queries    │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_unit.h/c      │ Unit init (role defaults for HP/speed/damage/range/cooldown), state machine tick, movement application │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_boids.h/c     │ Steering behaviors: separation, cohesion, follow_player, seek, flee, avoid_water → combined velocity   │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_combat.h/c    │ Cooldown attacks, projectile spawn/update/collision, damage/heal                                       │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_enemy.h/c     │ Camp placement on mid-elevation land, proximity activation, enemy AI (seek + leash)                    │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_render.h/c    │ Draw units as colored rects, health bars, projectiles, camp markers — all in screen coords             │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_debug_panel.c │ ImGui panel: boid weight sliders per role, unit state display, spawn button, overlay toggles           │
 ├─────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
 │ g_game.h/c      │ Top-level orchestration: init, update (input→boids→camps→enemies→combat), render, shutdown             │
 └─────────────────┴────────────────────────────────────────────────────────────────────────────────────────────────────────┘

 Modified Files

 File: src/app.h
 Change: Add GameState game and f64 last_time to App struct, include game/g_game.h
 ────────────────────────────────────────
 File: src/app.c
 Change: Compute dt in app_update, call g_game_update; pass map_rect to g_game_render in app_render; init/shutdown game; rebuild
   terrain grid on map Regenerate
 ────────────────────────────────────────
 File: CMakeLists.txt
 Change: Add all src/game/*.c files

 Key Data Structures (g_types.h)

 Unit — role, team, state, pos/vel (continuous [0,1]²), speed, HP, attack stats (damage/range/cooldown/timer), ability stats,
 target_id, current_cell cache, BoidWeights, color, radius

 BoidWeights — separation, alignment, cohesion, follow_player, avoid_water, seek_target, flee_target, preferred_dist,
 separation_radius

 Projectile — pos, vel, damage, lifetime, source_team, color

 EnemyCamp — pos, activation_radius, spawn_radius, activated flag, enemy_ids[8], num_enemies/alive

 TerrainGrid — grid_res, cell_size, grid_cells[] (reusable nearest-center lookup)

 GameState — player Unit, squad[4], enemies[64], projectiles[128], camps[16], terrain_grid, role_weights[ROLE_COUNT], tuning
 globals (elevation_speed_factor, water_blocks_movement), debug flags

 Fixed arrays with MAX_* constants — small counts in Phase 1, avoids dynamic allocation.

 Vec2 math — inline helpers in g_types.h: add, sub, scale, len, dist, normalize

 Implementation Order

 Step 1: Delta time + skeleton

+ Add f64 last_time to App, compute dt via SDL_GetPerformanceCounter/Frequency in app_update, clamp to 0.1s
+ Create g_types.h, g_game.h/c with stub init/update/render/shutdown
+ Verify builds and runs

 Step 2: Terrain lookup (g_terrain.c)

+ Extract spatial grid from mg_raster.c pattern into persistent TerrainGrid
+ g_terrain_build_grid(grid, graph) — one center per grid cell, grid_res = ceil(sqrt(num_centers))
+ g_terrain_find_cell(grid, graph, pos) — 3x3 neighborhood search, brute-force fallback
+ Convenience: g_terrain_get_elevation, g_terrain_is_water

 Step 3: Player unit + rendering (g_unit.c, g_render.c)

+ g_unit_init_player(unit) — white circle, 150 HP, speed 0.08, spawns at map center (nearest land cell)
+ WASD input via SDL_GetKeyboardState in g_game_update
+ Terrain speed modifier: effective_speed = speed *(1 - elevation* elevation_speed_factor)
+ Block movement into water cells
+ g_render_game(renderer, gs, map_rect) — transform unit pos to screen via map_rect.x + pos.x * map_rect.w
+ Draw player as filled SDL rect with SDL_SetRenderDrawColor + SDL_RenderFillRect

 Step 4: Squad boids (g_boids.c)

+ Steering functions each return Vec2 force:
  + separation: repel from units within sep_radius
  + cohesion: steer toward ally centroid
  + follow_player: steer toward player_pos + offset (offset_angle = index * 2PI/4, at preferred_dist)
  + seek/flee: toward/away from target
  + avoid_water: repel from water cells in neighborhood
+ g_boid_compute_velocity: sum weighted forces → accel → integrate vel → clamp to speed → return
+ Init 4 squad members with role defaults, spawn near player
+ State: FOLLOW only initially

 Step 5: Enemy camps (g_enemy.c)

+ g_enemy_place_camps: scan centers for !water && !coast && elevation in [0.15, 0.6], min spacing 0.15, pick up to MAX_CAMPS
+ Spawn 3-5 enemies per camp (mix of ENEMY_MELEE + ENEMY_RANGED), STATE_IDLE
+ g_enemy_update_camps: if player within activation_radius → activate, set enemies to STATE_ATTACK
+ Enemy AI: seek nearest player-team unit, move toward, leash to 3× spawn_radius from camp

 Step 6: Combat (g_combat.c)

+ Each frame: decrement cooldown_timers by dt
+ If in ATTACK state, cooldown ready, target alive and in range:
  + Melee: direct damage via g_combat_deal_damage
  + Ranged: g_combat_spawn_projectile (straight-line, speed ~0.3, lifetime 2s)
+ Projectile update: move, lifetime check, circle-circle collision with opposing team → deal damage + deactivate
+ Death: alive = false, state = STATE_DEAD
+ Squad state transitions: FOLLOW→ATTACK (enemy within detect range ~0.08), ATTACK→RETREAT (HP<25%), RETREAT→FOLLOW (HP>50% or
 no enemies)
+ Healer: FOLLOW→HEAL (ally HP<60%), heal in range → FOLLOW (all allies>80%)

 Step 7: Debug ImGui panel (g_debug_panel.c)

+ Uses #define CIMGUI_DEFINE_ENUMS_AND_STRUCTS + #include "cimgui.h"
+ Sliders for each role's BoidWeights
+ Display: unit states, HP, positions
+ Button: spawn test enemies
+ Toggles: show_boid_vectors, show_cell_borders, paused

 Step 8: Polish

+ Health bars above units (colored rect: green→red)
+ Player ability on spacebar (e.g., dash: teleport 0.03 units forward, 3s cooldown)
+ Mage AoE: damage all enemies within ability_range of target pos
+ Camp visual markers (faint outline circles)

 Role Defaults

 ┌──────────────┬─────┬───────┬─────┬───────┬──────────┬─────────────┐
 │     Role     │ HP  │ Speed │ Dmg │ Range │ Cooldown │    Color    │
 ├──────────────┼─────┼───────┼─────┼───────┼──────────┼─────────────┤
 │ Player       │ 150 │ 0.08  │ 20  │ 0.02  │ 0.5s     │ White       │
 ├──────────────┼─────┼───────┼─────┼───────┼──────────┼─────────────┤
 │ Melee        │ 120 │ 0.07  │ 18  │ 0.015 │ 0.8s     │ Red         │
 ├──────────────┼─────┼───────┼─────┼───────┼──────────┼─────────────┤
 │ Archer       │ 80  │ 0.065 │ 12  │ 0.08  │ 1.2s     │ Green       │
 ├──────────────┼─────┼───────┼─────┼───────┼──────────┼─────────────┤
 │ Healer       │ 70  │ 0.06  │ 5   │ 0.06  │ 2.0s     │ Yellow      │
 ├──────────────┼─────┼───────┼─────┼───────┼──────────┼─────────────┤
 │ Mage         │ 60  │ 0.055 │ 25  │ 0.07  │ 3.0s     │ Blue        │
 ├──────────────┼─────┼───────┼─────┼───────┼──────────┼─────────────┤
 │ Enemy Melee  │ 60  │ 0.05  │ 10  │ 0.015 │ 1.0s     │ Dark Red    │
 ├──────────────┼─────┼───────┼─────┼───────┼──────────┼─────────────┤
 │ Enemy Ranged │ 40  │ 0.04  │ 8   │ 0.06  │ 1.5s     │ Dark Purple │
 └──────────────┴─────┴───────┴─────┴───────┴──────────┴─────────────┘

 Rendering Order (back to front)

 1. Map texture (existing)
 2. Camp markers
 3. Enemies
 4. Projectiles
 5. Squad
 6. Player
 7. Health bars
 8. Debug overlays
 9. ImGui (existing, always last)

 Gotchas

+ No Vec2 math helpers exist yet — must add inline functions in g_types.h
+ app_render computes map dst rect as a local var — extract before map draw so game render can use it
+ On map Regenerate: must rebuild terrain grid + re-place camps + reset game state
+ Check igGetIO()->WantCaptureKeyboard before reading WASD input (ImGui may be active)
+ Use SDL_GetPerformanceCounter not SDL_GetTicks for sub-ms precision dt
+ All coords in [0,1]², all rendering transforms through map_rect

 Verification

 1. bash build.sh native compiles
 2. White player circle at island center, moves with WASD
 3. Movement slows at high elevation, blocked by water
 4. Four colored squad circles follow player in formation
 5. Enemy camps visible; walking near activates enemies
 6. Melee and ranged combat works; units take damage and die
 7. Health bars visible above all units
 8. Debug panel shows/tweaks boid weights
 9. Map Regenerate resets game properly
