// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: box3d.c, 3D rotating box demo rendering.

// This repository is licensed under the GNU General Public License.

/* Bresenham line into the backbuffer; clip is handled by draw_pixel. */
static void box3d_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = dx - dy;

    for (;;) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/*
 * Integer sin/cos (degrees, x256 fixed point). The table is exact
 * round(256*sin(deg)) for 0..90 so the cube's vertices land on a true
 * sphere of rotation - the earlier drifting table skewed the
 * wireframe corners badly.
 */
static int box3d_sin(int deg) {
    static const int table[91] = {
        0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49, 53, 58, 62, 66,
        71, 75, 79, 83, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132,
        136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187,
        190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228,
        230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251,
        252, 253, 254, 254, 255, 255, 255, 256, 256, 256, 256
    };
    int a = deg % 360;

    if (a < 0) {
        a += 360;
    }
    if (a <= 90) return table[a];
    if (a < 180) return table[180 - a];
    if (a < 270) return -table[a - 180];
    return -table[359 - a];
}

static int box3d_cos(int deg) {
    return box3d_sin(deg + 90);
}

/*
 * Rotation: animated yaw around Y (box3d_angle) then animated pitch
 * around X (box3d_angle_x), so the cube TUMBLES on both axes instead
 * of spinning flat around the vertical only. Used for both vertices
 * and face normals so lighting matches the drawn geometry exactly.
 */
static void box3d_rotate(int x, int y, int z, int yaw, int pitch, int *out_x, int *out_y, int *out_z) {
    int sa = box3d_sin(yaw);
    int ca = box3d_cos(yaw);
    int rx = (x * ca - z * sa) / 256;
    int rz = (x * sa + z * ca) / 256;
    int sb = box3d_sin(pitch);
    int cb = box3d_cos(pitch);
    int ry = (y * cb - rz * sb) / 256;
    int rz2 = (y * sb + rz * cb) / 256;

    *out_x = rx;
    *out_y = ry;
    *out_z = rz2;
}

/*
 * A torus is rotationally symmetric around its own hole axis, so a plain
 * yaw around Y would spin it invisibly.  Tilt the model once around X
 * before the shared yaw/pitch tumble: the silhouette and the projected
 * shadow then change with yaw exactly like the cube's do.
 */
#define BOX3D_DONUT_TILT 35

static void box3d_donut_rotate(int x, int y, int z, int *out_x, int *out_y, int *out_z) {
    int ct = box3d_cos(BOX3D_DONUT_TILT);
    int st = box3d_sin(BOX3D_DONUT_TILT);
    int ty = (y * ct - z * st) / 256;
    int tz = (y * st + z * ct) / 256;

    box3d_rotate(x, ty, tz, box3d_angle, box3d_angle_x, out_x, out_y, out_z);
}

/* Cube corners: 0-3 back ring (z=-64), 4-7 front ring (z=+64). */
static const int box3d_vx[8] = { -64,  64,  64, -64, -64,  64,  64, -64 };
static const int box3d_vy[8] = { -64, -64,  64,  64, -64, -64,  64,  64 };
static const int box3d_vz[8] = { -64, -64, -64, -64,  64,  64,  64,  64 };

/* Faces (consistent quads) with their model-space normals (x256). */
static const int box3d_faces[6][4] = {
    {0, 1, 2, 3},   /* back   z=-64 */
    {4, 5, 6, 7},   /* front  z=+64 */
    {0, 1, 5, 4},   /* top    y=-64 */
    {3, 2, 6, 7},   /* bottom y=+64 */
    {0, 3, 7, 4},   /* left   x=-64 */
    {1, 2, 6, 5}    /* right  x=+64 */
};
static const int box3d_normals[6][3] = {
    {0, 0, -256}, {0, 0, 256}, {0, -256, 0},
    {0, 256, 0}, {-256, 0, 0}, {256, 0, 0}
};

/*
 * 4x4 Bayer matrix for the solid cube's two-stage shading ramp.
 *
 * The old solid mode filled every face with one flat colour.  A checkerboard
 * dither between a dark, the face colour and a highlight makes the flat quad
 * read as a lit surface again, while every pixel stays opaque (the dark end
 * is gray, never the black backdrop) so faces behind it cannot show through.
 */
static const uint8_t box3d_bayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5}
};

static uint8_t box3d_highlight_color(uint8_t base) {
    if (base == color_red) return color_pink;
    if (base == color_orange) return color_yellow;
    return color_white;
}

/* 0..7 are the colour keys; 8 keeps the object's default palette and 9
 * walks a full hue cycle. */
static uint8_t box3d_named_color(int index) {
    switch (index % 8) {
        case 0: return color_red;
        case 1: return color_orange;
        case 2: return color_yellow;
        case 3: return color_green;
        case 4: return color_blue;
        case 5: return color_indigo;
        case 6: return color_violet;
        default: return color_white;
    }
}

static int box3d_hue_index(void) {
    /* 100 Hz timer: seven steps per ~2 s cycle. */
    return (int)((timer_ticks / 28u) % 7u);
}

static uint8_t box3d_cube_base(int face_order, int face_slot) {
    if (box3d_color_index <= 7) {
        return box3d_named_color(box3d_color_index);
    }
    if (box3d_color_index == 9) {
        return box3d_named_color((box3d_hue_index() + face_slot * 2) % 7);
    }
    return (face_order / 2) % 3 == 0 ? color_red :
           ((face_order / 2) % 3 == 1 ? color_orange : color_yellow);
}

static uint8_t box3d_donut_base(int u) {
    if (box3d_color_index <= 7) {
        return box3d_named_color(box3d_color_index);
    }
    if (box3d_color_index == 9) {
        return box3d_named_color((box3d_hue_index() + u / 2) % 7);
    }
    return color_orange;
}

#define BOX3D_SHADOW_POINTS 16

static int box3d_cross(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

/* Convex hull of the projected footprint.  The old shadow was a fixed
 * ellipse that did not care which way the cube was turned; this hull is
 * built from the actual projected silhouette, so the shadow follows the
 * rotating shape. */
static int box3d_build_hull(const int *xs, const int *ys, int n, int *hx, int *hy) {
    int order[BOX3D_SHADOW_POINTS];
    int i;
    int j;
    int k = 0;
    int lower;

    if (n < 3) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        order[i] = i;
    }
    for (i = 1; i < n; ++i) {
        int key = order[i];

        j = i - 1;
        while (j >= 0 &&
               (xs[order[j]] > xs[key] ||
                (xs[order[j]] == xs[key] && ys[order[j]] > ys[key]))) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    for (i = 0; i < n; ++i) {
        int p = order[i];

        while (k >= 2 && box3d_cross(hx[k - 2], hy[k - 2], hx[k - 1], hy[k - 1],
                                     xs[p], ys[p]) <= 0) {
            --k;
        }
        hx[k] = xs[p];
        hy[k] = ys[p];
        ++k;
    }
    lower = k + 1;
    for (i = n - 2; i >= 0; --i) {
        int p = order[i];

        while (k >= lower && box3d_cross(hx[k - 2], hy[k - 2], hx[k - 1], hy[k - 1],
                                         xs[p], ys[p]) <= 0) {
            --k;
        }
        hx[k] = xs[p];
        hy[k] = ys[p];
        ++k;
    }
    return k - 1;
}

static void box3d_draw_shadow(int cx, int cy, const int *xs, const int *ys, int n) {
    int hx[BOX3D_SHADOW_POINTS];
    int hy[BOX3D_SHADOW_POINTS];
    int count = box3d_build_hull(xs, ys, n, hx, hy);
    int min_y;
    int max_y;
    int center_x = 0;
    int center_y = 0;
    int max_d2 = 1;
    int phase = (box3d_angle / 8) & 3;

    if (count < 3) {
        return;
    }
    min_y = max_y = hy[0];
    for (int i = 0; i < count; ++i) {
        center_x += hx[i];
        center_y += hy[i];
        if (hy[i] < min_y) min_y = hy[i];
        if (hy[i] > max_y) max_y = hy[i];
    }
    center_x /= count;
    center_y /= count;
    for (int i = 0; i < count; ++i) {
        int dx = hx[i] - center_x;
        int dy = hy[i] - center_y;
        int d2 = dx * dx + dy * dy;

        if (d2 > max_d2) max_d2 = d2;
    }

    for (int y = min_y; y <= max_y; ++y) {
        int ix[BOX3D_SHADOW_POINTS];
        int ic = 0;

        for (int i = 0; i < count; ++i) {
            int j = (i + 1) % count;

            if ((hy[i] <= y && hy[j] > y) || (hy[j] <= y && hy[i] > y)) {
                ix[ic++] = hx[i] + (hx[j] - hx[i]) * (y - hy[i]) / (hy[j] - hy[i]);
            }
        }
        for (int i = 0; i + 1 < ic; i += 2) {
            int xa = ix[i];
            int xb = ix[i + 1];

            if (xa > xb) {
                int t = xa;
                xa = xb;
                xb = t;
            }
            for (int x = xa; x <= xb; ++x) {
                int dx = x - center_x;
                int dy = y - center_y;
                int d2 = dx * dx + dy * dy;
                int level = 15 - (d2 * 10) / max_d2;

                if (level < 3) level = 3;
                if (box3d_bayer4[(y + phase) & 3][(x + phase) & 3] * 16 + 8 < level * 16) {
                    draw_pixel(cx + x, cy + y, color_gray_dark);
                }
            }
        }
    }
}

/* Soft elliptical shadow for the torus.  A projected torus footprint can
 * collapse to a needle when the ring is edge-on; a radially fading ellipse
 * stays readable at every tumble angle instead. */
static void box3d_draw_soft_shadow(int cx, int cy, int sx, int sy, int rx, int ry, int phase) {
    if (rx < 6) rx = 6;
    if (ry < 4) ry = 4;
    for (int y = -ry; y <= ry; ++y) {
        int ny = y * 255 / ry;

        for (int x = -rx; x <= rx; ++x) {
            int nx = x * 255 / rx;
            int shade = 200 - (nx * nx) / 380 - (ny * ny) / 380;
            int threshold;

            if (shade <= 0) {
                continue;
            }
            threshold = box3d_bayer4[(y + phase) & 3][(x + phase) & 3] * 16 + 8;
            if (threshold < shade) {
                draw_pixel(cx + sx + x, cy + sy + y, color_gray_dark);
            }
        }
    }
}

static uint8_t box3d_donut_dark(uint8_t base) {
    if (base == color_orange) return color_red;
    if (base == color_yellow) return color_orange;
    return color_gray_dark;
}

/* Shared shaded-quad scanline fill for the torus.  The cube keeps its
 * face-specific palette above; this one uses the same two-stage Bayer
 * ramp so both shapes read as one renderer. */
static void box3d_fill_shaded_quad(int cx, int cy, const int *qx, const int *qy,
                                   int lam, int phase, uint8_t base, uint8_t light) {
    int min_x = qx[0];
    int max_x = qx[0];
    int min_y = qy[0];
    int max_y = qy[0];
    int quad_w;
    int quad_h;
    int pulse = box3d_sin((int)((timer_ticks * 2u) % 360u)) * 20 / 256;
    uint8_t dark = color_gray_dark;

    for (int i = 1; i < 4; ++i) {
        if (qx[i] < min_x) min_x = qx[i];
        if (qx[i] > max_x) max_x = qx[i];
        if (qy[i] < min_y) min_y = qy[i];
        if (qy[i] > max_y) max_y = qy[i];
    }
    quad_w = max_x - min_x;
    quad_h = max_y - min_y;
    if (quad_w < 1) quad_w = 1;
    if (quad_h < 1) quad_h = 1;

    for (int y = min_y; y <= max_y; ++y) {
        int ix[4];
        int ic = 0;
        int gy = (y - min_y) * 255 / quad_h;

        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) % 4;

            if ((qy[i] <= y && qy[j] > y) || (qy[j] <= y && qy[i] > y)) {
                ix[ic++] = qx[i] + (qx[j] - qx[i]) * (y - qy[i]) / (qy[j] - qy[i]);
            }
        }
        for (int i = 0; i + 1 < ic; i += 2) {
            int xa = ix[i];
            int xb = ix[i + 1];
            int gx;
            int gx_step;

            if (xa > xb) {
                int t = xa;
                xa = xb;
                xb = t;
            }
            gx = (xa - min_x) * 255 / quad_w;
            gx_step = 255 / quad_w;
            for (int x = xa; x <= xb; ++x) {
                int grad;
                int shade;
                int threshold;

                if (gx > 255) gx = 255;
                grad = 255 - (gx * 2 + gy) / 3;
                shade = (lam * 3 + grad + pulse * 4) / 4;
                if (shade < 0) {
                    shade = 0;
                } else if (shade > 255) {
                    shade = 255;
                }
                threshold = box3d_bayer4[(y - min_y + phase) & 3]
                                        [(x - min_x + phase) & 3] * 16 + 8;
                if (shade < 210) {
                    int mix = (shade - 40) * 255 / 170;

                    if (mix < 0) mix = 0;
                    draw_pixel(cx + x, cy + y, threshold < mix ? base : dark);
                } else {
                    int highlight = (shade - 220) * 255 / 35;

                    if (highlight < 0) highlight = 0;
                    draw_pixel(cx + x, cy + y, threshold < highlight ? light : base);
                }
                gx += gx_step;
            }
        }
    }
}

#define BOX3D_TORUS_U 16
#define BOX3D_TORUS_V 8

typedef struct {
    int u;
    int v;
    int depth;
    int nx;
    int ny;
    int nz;
} Box3dDonutQuad;

static void render_donut(int cx, int cy) {
    int px[BOX3D_TORUS_U][BOX3D_TORUS_V];
    int py[BOX3D_TORUS_U][BOX3D_TORUS_V];
    int pz[BOX3D_TORUS_U][BOX3D_TORUS_V];
    int min_px = 0;
    int max_px = 0;
    int min_py = 0;
    int max_py = 0;

    for (int u = 0; u < BOX3D_TORUS_U; ++u) {
        int au = u * 360 / BOX3D_TORUS_U;
        int cu = box3d_cos(au);
        int su = box3d_sin(au);

        for (int v = 0; v < BOX3D_TORUS_V; ++v) {
            int av = v * 360 / BOX3D_TORUS_V;
            int cv = box3d_cos(av);
            int sv = box3d_sin(av);
            int ring = 64 + (30 * cv) / 256;
            int x = ring * cu / 256;
            int y = 30 * sv / 256;
            int z = ring * su / 256;
            int rx;
            int ry;
            int rz;

            box3d_donut_rotate(x, y, z, &rx, &ry, &rz);
            px[u][v] = rx * 300 / (512 + (rz < -448 ? -448 : rz));
            py[u][v] = -ry * 300 / (512 + (rz < -448 ? -448 : rz));
            pz[u][v] = rz;
            if (u == 0 && v == 0) {
                min_px = max_px = px[u][v];
                min_py = max_py = py[u][v];
            } else {
                if (px[u][v] < min_px) min_px = px[u][v];
                if (px[u][v] > max_px) max_px = px[u][v];
                if (py[u][v] < min_py) min_py = py[u][v];
                if (py[u][v] > max_py) max_py = py[u][v];
            }
        }
    }

    if (box3d_mode == 1) {
        int shadow_rx = (max_px - min_px) / 2 + 8;
        int shadow_ry = shadow_rx / 3 + 4;

        box3d_draw_soft_shadow(cx, cy, (min_px + max_px) / 2, max_py + 8,
                               shadow_rx, shadow_ry, (box3d_angle / 6) & 3);
    }

    if (box3d_mode == 1) {
        Box3dDonutQuad quads[BOX3D_TORUS_U * BOX3D_TORUS_V];
        int count = 0;

        for (int u = 0; u < BOX3D_TORUS_U; ++u) {
            int u1 = (u + 1) % BOX3D_TORUS_U;
            int au = u * 360 / BOX3D_TORUS_U + 180 / BOX3D_TORUS_U;

            for (int v = 0; v < BOX3D_TORUS_V; ++v) {
                int v1 = (v + 1) % BOX3D_TORUS_V;
                int av = v * 360 / BOX3D_TORUS_V + 180 / BOX3D_TORUS_V;
                int cu = box3d_cos(au);
                int su = box3d_sin(au);
                int cv = box3d_cos(av);
                int sv = box3d_sin(av);
                int nx = cu * cv / 256;
                int ny = sv;
                int nz = su * cv / 256;
                int rnx;
                int rny;
                int rnz;

                box3d_donut_rotate(nx, ny, nz, &rnx, &rny, &rnz);
                if (rnz >= 0) {
                    continue;
                }
                quads[count].u = u;
                quads[count].v = v;
                quads[count].depth = pz[u][v] + pz[u1][v] + pz[u1][v1] + pz[u][v1];
                quads[count].nx = rnx;
                quads[count].ny = rny;
                quads[count].nz = rnz;
                ++count;
            }
        }

        for (int i = 0; i < count; ++i) {
            for (int j = i + 1; j < count; ++j) {
                if (quads[i].depth < quads[j].depth) {
                    Box3dDonutQuad tmp = quads[i];
                    quads[i] = quads[j];
                    quads[j] = tmp;
                }
            }
        }

        for (int i = 0; i < count; ++i) {
            int u = quads[i].u;
            int v = quads[i].v;
            int u1 = (u + 1) % BOX3D_TORUS_U;
            int v1 = (v + 1) % BOX3D_TORUS_V;
            int qx[4];
            int qy[4];
            int lam = (-(quads[i].nx + quads[i].ny + quads[i].nz) + 768) * 255 / 1536;

            qx[0] = px[u][v];
            qy[0] = py[u][v];
            qx[1] = px[u1][v];
            qy[1] = py[u1][v];
            qx[2] = px[u1][v1];
            qy[2] = py[u1][v1];
            qx[3] = px[u][v1];
            qy[3] = py[u][v1];
            box3d_fill_shaded_quad(cx, cy, qx, qy, lam, (u + v) & 3,
                                   box3d_donut_base(u), color_white);
        }
    } else {
        for (int u = 0; u < BOX3D_TORUS_U; ++u) {
            int u1 = (u + 1) % BOX3D_TORUS_U;

            for (int v = 0; v < BOX3D_TORUS_V; ++v) {
                int v1 = (v + 1) % BOX3D_TORUS_V;

                box3d_draw_line(cx + px[u][v], cy + py[u][v],
                                cx + px[u1][v], cy + py[u1][v], color_white);
                box3d_draw_line(cx + px[u][v], cy + py[u][v],
                                cx + px[u][v1], cy + py[u][v1], color_white);
            }
        }
    }
}

static void render_3d_box(const Window *window) {
    int cx = window->x + window->w / 2;
    int cy = window->y + (window->h + 30) / 2;
    int px[8];
    int py[8];
    int pd[8];
    int prx[8];
    int pry[8];
    int prz[8];
    int i;

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_black);
    draw_text(window->x + 12, window->y + 30, box3d_mode == 0 ? "MODE 1: WIREFRAME" : "MODE 2: SOLID",
              color_gray_light, color_black, true);
    draw_text(window->x + 172, window->y + 30, box3d_shape == 0 ? "CUBE" : "DONUT",
              color_yellow, color_black, true);
    {
        uint8_t swatch = box3d_color_index <= 7 ? box3d_named_color(box3d_color_index) :
                         (box3d_color_index == 9 ? box3d_named_color(box3d_hue_index()) : color_orange);

        fill_rect(window->x + 280, window->y + 29, 8, 8, swatch);
        draw_rect(window->x + 280, window->y + 29, 8, 8, color_white);
    }
    draw_text(window->x + 12, window->y + window->h - 30, "UP/DOWN: SHAPE  0-9: COLOR",
              color_gray_light, color_black, true);
    draw_text(window->x + 12, window->y + window->h - 18, "LEFT/RIGHT: MODE",
              color_gray_light, color_black, true);

    /* The black canvas ends above the footer strip.  Without this tighter
     * clip the projected shadow (and a wide face) can bleed into the light
     * gray footer, which looked like a detached L-shaped shadow. */
    set_window_clip(window->x + 8, window->y + 24, window->w - 16, window->h - 32);

    if (box3d_shape == 1) {
        render_donut(cx, cy);
        return;
    }

    /* project all 8 corners (animated XY tumble) */
    for (i = 0; i < 8; ++i) {
        int rx;
        int ry;
        int rz;

        box3d_rotate(box3d_vx[i], box3d_vy[i], box3d_vz[i], box3d_angle, box3d_angle_x, &rx, &ry, &rz);
        pd[i] = rz;
        prx[i] = rx;
        pry[i] = ry;
        prz[i] = rz;
        {
            int depth = 512 + rz;

            if (depth < 64) {
                depth = 64;
            }
            px[i] = rx * 300 / depth;
            py[i] = -ry * 300 / depth;
        }
    }

    if (box3d_mode == 1) {
        /* Real projected shadow: drop the eight rotated corners onto a
         * ground plane along the light vector, take their convex hull and
         * dither-fill that.  Unlike the old fixed ellipse this follows the
         * cube's actual silhouette at every tumble angle. */
        {
            int min_ry = pry[0];
            int ground_y;
            int shx[8];
            int shy[8];

            for (i = 1; i < 8; ++i) {
                if (pry[i] < min_ry) min_ry = pry[i];
            }
            ground_y = min_ry - 6;
            for (i = 0; i < 8; ++i) {
                int t16 = (ground_y - pry[i]) * 16 / -100;
                int wx = prx[i] + (-40 * t16) / 16;
                int wz = prz[i] + (-25 * t16) / 16;
                int depth = 512 + wz;

                if (depth < 64) depth = 64;
                shx[i] = wx * 300 / depth;
                shy[i] = -ground_y * 300 / depth;
            }
            box3d_draw_shadow(cx, cy, shx, shy, 8);
        }

        /* Solid faces: painter's algorithm, back faces culled by their
         * rotated normal, and every visible face filled with a two-stage
         * checkerboard ramp (dark -> face colour -> highlight).  The
         * gradient is a directional-light term from the rotated normal
         * combined with a top-left screen falloff and a slow light pulse;
         * because the normal changes as the cube tumbles, the dither
         * density genuinely animates instead of being a static pattern. */
        {
            int order[6] = {0, 1, 2, 3, 4, 5};

            for (i = 0; i < 6; ++i) {
                for (int j = i + 1; j < 6; ++j) {
                    const int *fa = box3d_faces[order[i]];
                    const int *fb = box3d_faces[order[j]];
                    int di = pd[fa[0]] + pd[fa[1]] + pd[fa[2]] + pd[fa[3]];
                    int dj = pd[fb[0]] + pd[fb[1]] + pd[fb[2]] + pd[fb[3]];

                    /* Painter's algorithm: farthest face first.  pd[] is
                     * the rotated z, so a larger value is farther away.
                     * The old comparison sorted nearest-first, which let an
                     * edge-on back face be painted over the front faces. */
                    if (di < dj) {
                        int tmp = order[i];
                        order[i] = order[j];
                        order[j] = tmp;
                    }
                }
            }
            for (int f = 0; f < 6; ++f) {
                const int *face = box3d_faces[order[f]];
                int nx;
                int ny;
                int nz;
                int min_x;
                int max_x;
                int min_y;
                int max_y;
                int face_w;
                int face_h;
                int lam;
                int pulse;
                int phase;
                uint8_t base;
                uint8_t light;
                uint8_t dark;

                /* cull faces pointing away from the camera (+z is away) */
                box3d_rotate(box3d_normals[order[f]][0], box3d_normals[order[f]][1],
                             box3d_normals[order[f]][2], box3d_angle, box3d_angle_x, &nx, &ny, &nz);
                if (nz >= 0) {
                    continue;
                }

                /* Solid face palette.  Opposite faces share one colour, so
                 * the three faces visible on a convex cube are always three
                 * different colours.  The old order[f] % 3 mapping gave
                 * opposite faces different colours and could put two
                 * same-coloured faces side by side, which made the solid
                 * mode look like a flat cut-out. */
                base = box3d_cube_base(order[f], f);
                light = box3d_highlight_color(base);
                dark = color_gray_dark;

                /* Rotated normal against a top-left-front light, mapped to
                 * 0..255.  A small time pulse keeps the faces breathing
                 * even while the cube is edge-on. */
                lam = (-(nx + ny + nz) + 768) * 255 / 1536;
                pulse = box3d_sin((int)((timer_ticks * 2u) % 360u)) * 20 / 256;
                phase = f & 3;

                min_x = max_x = px[face[0]];
                min_y = max_y = py[face[0]];
                for (int k = 1; k < 4; ++k) {
                    if (px[face[k]] < min_x) min_x = px[face[k]];
                    if (px[face[k]] > max_x) max_x = px[face[k]];
                    if (py[face[k]] < min_y) min_y = py[face[k]];
                    if (py[face[k]] > max_y) max_y = py[face[k]];
                }
                face_w = max_x - min_x;
                face_h = max_y - min_y;
                if (face_w < 1) face_w = 1;
                if (face_h < 1) face_h = 1;

                for (int sy = min_y; sy <= max_y; ++sy) {
                    int crossings[4];
                    int count = 0;
                    int gy = (sy - min_y) * 255 / face_h;

                    for (int e = 0; e < 4; ++e) {
                        int a = face[e];
                        int b = face[(e + 1) % 4];

                        if ((py[a] <= sy && py[b] > sy) || (py[b] <= sy && py[a] > sy)) {
                            int t = sy - py[a];

                            crossings[count++] = px[a] + (px[b] - px[a]) * t / (py[b] - py[a]);
                        }
                    }
                    for (int c = 0; c + 1 < count; c += 2) {
                        int xa = crossings[c];
                        int xb = crossings[c + 1];
                        int gx;
                        int gx_step;

                        if (xa > xb) {
                            int t = xa;
                            xa = xb;
                            xb = t;
                        }
                        gx = (xa - min_x) * 255 / face_w;
                        gx_step = 255 / face_w;
                        for (int sx = xa; sx <= xb; ++sx) {
                            int grad;
                            int shade;
                            int threshold;

                            if (gx > 255) {
                                gx = 255;
                            }
                            grad = 255 - (gx * 2 + gy) / 3;
                            shade = (lam * 3 + grad + pulse * 4) / 4;
                            if (shade < 0) {
                                shade = 0;
                            } else if (shade > 255) {
                                shade = 255;
                            }
                            threshold = box3d_bayer4[(sy - min_y + phase) & 3]
                                                    [(sx - min_x + phase) & 3] * 16 + 8;
                            if (shade < 210) {
                                /* Dark -> face-colour ramp over most of the
                                 * face.  The earlier 128 cutoff left the
                                 * midtones completely flat, so a small cube
                                 * showed no dither at all. */
                                int mix = (shade - 40) * 255 / 170;

                                if (mix < 0) {
                                    mix = 0;
                                }
                                draw_pixel(cx + sx, cy + sy, threshold < mix ? base : dark);
                            } else {
                                /* Only the brightest edge breaks through to
                                 * the highlight; otherwise white/pink dither
                                 * swallows the face colour. */
                                int highlight = (shade - 220) * 255 / 35;

                                if (highlight < 0) {
                                    highlight = 0;
                                }
                                draw_pixel(cx + sx, cy + sy, threshold < highlight ? light : base);
                            }
                            gx += gx_step;
                        }
                    }
                }
            }
        }
        /* solid faces only - no wire outline on top */
    } else {
        /* mode 1: pure white wireframe (math is exact now, so the
         * corners sit on a clean rotating cube) */
        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7}
        };

        for (int e = 0; e < 12; ++e) {
            int a = edges[e][0];
            int b = edges[e][1];

            box3d_draw_line(cx + px[a], cy + py[a], cx + px[b], cy + py[b], color_white);
        }
    }
}
