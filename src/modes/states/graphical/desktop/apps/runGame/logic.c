// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: logic.c, Run! Run game logic.

// This repository is licensed under the GNU General Public License.

/* Subpixel helpers: 1/16 px internal units so motion stays smooth. */
#define RUN_SUB 16
#define RUN_PLAYER_W 24
#define RUN_PLAYER_H 30
#define RUN_SCROLL_SPEED 32      /* 2 px/tick camera push (x16) */
#define RUN_SKULL_SPEED 22       /* ~1.4 px/tick, escapable (x16) */
#define RUN_SKULL_SIZE 40

/* Random-generated level: grass/dirt platforms with gaps, coins in
 * the sky and low air, 2-4 brick solid obstacles. Water below. */
static void run_generate_level(void) {
    int x = 0;

    run_level_len = 0;
    for (int i = 0; i < RUN_PLATFORMS; ++i) {
        int width = 64 + rand_range(3) * 32;      /* 64/96/128 px */
        int gap = (i > 0 && rand_range(3) == 0) ? 40 + rand_range(2) * 24 : 0;
        int py = 120 + rand_range(3) * 20;        /* 120/140/160 */

        x += gap;
        run_platforms[i].x = x;
        run_platforms[i].w = width;
        run_platforms[i].y = py;
        run_platforms[i].kind = 0;
        x += width;
    }
    run_level_len = x + 320;

    /* coins: sky + air-ground rows */
    for (int i = 0; i < RUN_COINS; ++i) {
        int region = (i * run_level_len) / RUN_COINS;

        run_coins[i].x = region + rand_range(48);
        run_coins[i].y = (rand_range(2) == 0) ? (60 + rand_range(30)) : (100 + rand_range(30));
        run_coins[i].frame = 0;
        run_coins[i].taken = false;
    }

    /* bricks: 2-4 solid obstacles standing on platforms */
    for (int i = 0; i < RUN_BRICKS; ++i) {
        int p = rand_range(RUN_PLATFORMS);

        run_bricks[i].x = run_platforms[p].x + 16 + rand_range(32);
        run_bricks[i].y = run_platforms[p].y - 16;
    }
}

static void reset_run(void) {
    run_generate_level();
    run_player_wx = 96 * RUN_SUB;
    run_py16 = 80 * RUN_SUB;
    run_vy16 = 0;
    run_scroll16 = 0;
    run_player_grounded = false;
    run_lives = 3;
    run_coins_collected = 0;
    run_time_left = 60 * TIMER_HZ;
    run_countdown = 5 * TIMER_HZ;
    run_state = 0;
    run_death_anim_t = 0;
    run_jump_anim_t = RUN_JUMP_FRAMES;
    run_skull_active = false;
    run_skull_cooldown = (uint32_t)(3 + rand_range(3)) * TIMER_HZ;   /* 3-5s */
    run_last_tick = timer_ticks;
    run_last_coin_frame = timer_ticks;
}

/* Player AABB (world px) vs solid brick (kept for spawn checks). */
static bool run_rect_hits_brick(int wx, int wy, int w, int h) {
    for (int i = 0; i < RUN_BRICKS; ++i) {
        if (wx + w > run_bricks[i].x && wx < run_bricks[i].x + 16 &&
            wy + h > run_bricks[i].y && wy < run_bricks[i].y + 16) {
            return true;
        }
    }
    return false;
}

/* Platform ground test at the player's feet (world px). */
static bool run_feet_on_platform(int wx, int wy, int *ground_y_out) {
    for (int i = 0; i < RUN_PLATFORMS; ++i) {
        if (wx + RUN_PLAYER_W > run_platforms[i].x && wx < run_platforms[i].x + run_platforms[i].w) {
            if (wy >= run_platforms[i].y - RUN_PLAYER_H && wy <= run_platforms[i].y + 12) {
                if (ground_y_out != NULL) {
                    *ground_y_out = run_platforms[i].y;
                }
                return true;
            }
        }
    }
    return false;
}

/* integer sine (degrees, x256 fixed point) - shared 3D box table */
static int run_sin_x256(int deg) {
    return box3d_sin(deg);
}

static void run_kill_player(void) {
    run_state = 2;
    run_death_anim_t = 0;
    run_death_start_y = run_player_y;
}

static void update_run(void) {
    if (!windows[APP_RUN_GAME].open || cpu_halted_overlay || debug_overlay_open) {
        return;
    }
    if (timer_ticks == run_last_tick) {
        return;
    }
    uint32_t dt = timer_ticks - run_last_tick;
    run_last_tick = timer_ticks;

    /* coin spin: 10 frames, 8 ticks each (also during countdown) */
    if (timer_ticks - run_last_coin_frame >= 8u) {
        run_last_coin_frame = timer_ticks;
        for (int i = 0; i < RUN_COINS; ++i) {
            if (!run_coins[i].taken) {
                run_coins[i].frame = (run_coins[i].frame + 1) % 10;
            }
        }
    }

    if (run_state == 0) {
        if (run_countdown > dt) {
            run_countdown -= dt;
        } else {
            run_countdown = 0;
            run_state = 1;
        }
        return;
    }

    if (run_state == 2) {
        /* dying: sudden jump up then smooth sin fall into the water */
        run_death_anim_t += (int)dt * 6;
        if (run_death_anim_t < 90) {
            run_player_y = run_death_start_y - run_sin_x256(run_death_anim_t) * 60 / 256;
        } else if (run_death_anim_t < 200) {
            int t = run_death_anim_t - 90;
            int drop = (run_sin_x256(90 + t / 2) * 90) / 256;

            run_player_y = run_death_start_y - 60 + drop;
            if (run_player_y >= RUN_WATER_Y - 8) {
                run_player_y = RUN_WATER_Y - 8;
            }
        } else {
            if (run_lives > 0) {
                run_lives--;
            }
            if (run_lives <= 0) {
                run_state = 3;
            } else {
                /* respawn: pushed back to a safe platform behind the camera */
                int safe_x = (run_scroll16 / RUN_SUB) - 40;

                if (safe_x < 0) {
                    safe_x = 0;
                }
                /* snap to the nearest platform left of the camera */
                for (int i = 0; i < RUN_PLATFORMS; ++i) {
                    if (run_platforms[i].x + run_platforms[i].w > safe_x) {
                        safe_x = run_platforms[i].x + 8;
                        break;
                    }
                }
                run_player_wx = safe_x * RUN_SUB;
                run_py16 = 40 * RUN_SUB;
                run_vy16 = 0;
                run_scroll16 -= 100 * RUN_SUB;
                if (run_scroll16 < 0) {
                    run_scroll16 = 0;
                }
                run_jump_anim_t = RUN_JUMP_FRAMES;
                run_state = 1;
            }
        }
        return;
    }

    if (run_state != 1) {
        return;   /* dead (3) / win (4): frozen until ENTER */
    }

    /* timer */
    if (run_time_left > dt) {
        run_time_left -= dt;
    } else {
        run_time_left = 0;
        run_state = 4;
        return;
    }

    /* --- camera: constant smooth push right, player auto-runs at the
     *     same speed. Idle alone never kills: the left edge catches
     *     you only when a brick (or a bad landing) holds you back. --- */
    run_scroll16 += RUN_SCROLL_SPEED * (int)dt;
    run_player_wx += RUN_SCROLL_SPEED * (int)dt;

    /* --- skull chaser: spawn after cooldown, then hunt --- */
    if (!run_skull_active) {
        if (run_skull_cooldown > dt) {
            run_skull_cooldown -= dt;
        } else {
            /* spawn behind the camera left edge, at platform height */
            int sx = run_scroll16 / RUN_SUB - 60;

            if (sx < 0) {
                sx = 0;
            }
            run_skull_wx = sx * RUN_SUB;
            run_skull_active = true;
        }
    } else {
        /* forward-hunting skull, slightly slower than the camera so
         * skilled jumps escape it; despawn + cooldown if it falls too
         * far behind the camera edge */
        run_skull_wx += RUN_SKULL_SPEED * (int)dt;
        if (run_skull_wx / RUN_SUB + RUN_SKULL_SIZE < run_scroll16 / RUN_SUB - 80) {
            run_skull_active = false;
            run_skull_cooldown = (uint32_t)(3 + rand_range(3)) * TIMER_HZ;
        }
    }

    /* --- physics: eased jump arc overrides gravity --- */
    if (run_jump_anim_t < RUN_JUMP_FRAMES) {
        run_jump_anim_t += (int)dt;
        if (run_jump_anim_t > RUN_JUMP_FRAMES) {
            run_jump_anim_t = RUN_JUMP_FRAMES;
        }
        {
            int angle = run_jump_anim_t * 180 / RUN_JUMP_FRAMES;
            int lift = run_sin_x256(angle) * 64 / 256;   /* peak 64 px */

            run_py16 = run_jump_start_y16 - lift * RUN_SUB;
        }
    } else {
        run_vy16 += 48 * (int)dt;              /* gravity, subpixel */
        if (run_vy16 > 80 * RUN_SUB / 8) {
            run_vy16 = 80 * RUN_SUB / 8;
        }
        run_py16 += run_vy16 / 8;
    }

    /* --- solid brick collision: stop the player at the brick's left
    *     face (full touch, no pass-through), in world px --- */
    {
        int pwx = run_player_wx / RUN_SUB;
        int pwy = run_py16 / RUN_SUB;

        for (int i = 0; i < RUN_BRICKS; ++i) {
            if (pwx + RUN_PLAYER_W > run_bricks[i].x && pwx < run_bricks[i].x + 16 &&
                pwy + RUN_PLAYER_H > run_bricks[i].y && pwy < run_bricks[i].y + 16) {
                /* standing on top? land on the brick instead */
                if (run_py16 >= (run_bricks[i].y - RUN_PLAYER_H) * RUN_SUB &&
                    run_py16 <= (run_bricks[i].y - RUN_PLAYER_H + 6) * RUN_SUB) {
                    run_py16 = (run_bricks[i].y - RUN_PLAYER_H) * RUN_SUB;
                    run_vy16 = 0;
                    run_jump_anim_t = RUN_JUMP_FRAMES;   /* end arc on top */
                    run_player_grounded = true;
                } else {
                    /* blocked: snap behind the brick's left face */
                    run_player_wx = (run_bricks[i].x - RUN_PLAYER_W - 1) * RUN_SUB;
                }
                pwx = run_player_wx / RUN_SUB;
            }
        }
    }

    /* --- landing on platforms --- */
    if (run_jump_anim_t >= RUN_JUMP_FRAMES) {
        int ground_y = RUN_WATER_Y;

        if (run_feet_on_platform(run_player_wx / RUN_SUB, run_py16 / RUN_SUB, &ground_y)) {
            run_py16 = (ground_y - RUN_PLAYER_H) * RUN_SUB;
            run_vy16 = 0;
            run_player_grounded = true;
        } else {
            run_player_grounded = false;
        }
    }

    /* --- coin pickup --- */
    {
        int pwx = run_player_wx / RUN_SUB;
        int pwy = run_py16 / RUN_SUB;

        for (int i = 0; i < RUN_COINS; ++i) {
            if (!run_coins[i].taken) {
                if (pwx + RUN_PLAYER_W > run_coins[i].x && pwx < run_coins[i].x + 16 &&
                    pwy + RUN_PLAYER_H > run_coins[i].y && pwy < run_coins[i].y + 16) {
                    run_coins[i].taken = true;
                    ++run_coins_collected;
                }
            }
        }
    }

    /* --- skull catch = death --- */
    if (run_skull_active) {
        int sx = run_skull_wx / RUN_SUB;
        int pwx = run_player_wx / RUN_SUB;
        int pwy = run_py16 / RUN_SUB;

        if (sx + RUN_SKULL_SIZE > pwx && sx < pwx + RUN_PLAYER_W &&
            pwy < 120 /* skull hugs ground level; jump over to escape */) {
            run_kill_player();
            return;
        }
    }

    /* --- left camera edge squeeze: a blocked or idle player gets
     *     pushed off the left screen edge by the scroll -> lose a
     *     life and respawn (classic side-scroller pressure) --- */
    if (run_player_wx < run_scroll16) {
        run_kill_player();
        return;
    }

    /* --- fell into the water --- */
    if (run_py16 / RUN_SUB >= RUN_WATER_Y - RUN_PLAYER_H) {
        run_kill_player();
        return;
    }

    /* --- render values (whole px) --- */
    run_player_x = run_player_wx / RUN_SUB - run_scroll16 / RUN_SUB;
    run_player_y = run_py16 / RUN_SUB;
    run_scroll = run_scroll16 / RUN_SUB;

    /* --- reached the level end: win --- */
    if (run_player_wx / RUN_SUB > run_level_len) {
        run_state = 4;
    }
}
