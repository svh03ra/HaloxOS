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

static void render_3d_box(const Window *window) {
    int cx = window->x + window->w / 2;
    int cy = window->y + (window->h + 30) / 2;
    int px[8];
    int py[8];
    int pd[8];
    int i;

    fill_rect(window->x + 8, window->y + 24, window->w - 16, window->h - 32, color_black);
    draw_text(window->x + 12, window->y + 30, box3d_mode == 0 ? "MODE 1: WIREFRAME" : "MODE 2: SOLID",
              color_gray_light, color_black, true);
    draw_text(window->x + 12, window->y + window->h - 18, "LEFT/RIGHT: change mode", color_gray_light, color_black, true);

    /* project all 8 corners (animated XY tumble) */
    for (i = 0; i < 8; ++i) {
        int rx;
        int ry;
        int rz;

        box3d_rotate(box3d_vx[i], box3d_vy[i], box3d_vz[i], box3d_angle, box3d_angle_x, &rx, &ry, &rz);
        pd[i] = rz;
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
        /* real-time animated dithered shadow drop: sways with the yaw,
         * breathes with the visible width, dither phase shimmers as the
         * cube turns, so it reads as a live drop shadow */
        int sway = box3d_sin(box3d_angle) * 12 / 256;
        int half_w = 44 + box3d_cos(box3d_angle) * 18 / 256;
        int phase = box3d_angle / 3;

        for (int sy = 0; sy < 12; ++sy) {
            int row_half = half_w - sy * 2;

            if (row_half < 4) {
                row_half = 4;
            }
            for (int sx = -row_half; sx <= row_half; ++sx) {
                if (((sx + sy + phase) & 3) < 2) {
                    draw_pixel(cx + sway + sx, cy + 82 + sy, color_gray_dark);
                }
            }
        }

        /* solid faces: painter's algorithm, back faces culled by their
         * rotated normal, and each face dither-shaded in real time from
         * its live normal vs a fixed upper-left-front light */
        {
            int order[6] = {0, 1, 2, 3, 4, 5};

            for (i = 0; i < 6; ++i) {
                for (int j = i + 1; j < 6; ++j) {
                    const int *fa = box3d_faces[order[i]];
                    const int *fb = box3d_faces[order[j]];
                    int di = pd[fa[0]] + pd[fa[1]] + pd[fa[2]] + pd[fa[3]];
                    int dj = pd[fb[0]] + pd[fb[1]] + pd[fb[2]] + pd[fb[3]];

                    if (di > dj) {
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
                int min_y;
                int max_y;
                uint8_t base;
                int density;

                /* cull faces pointing away from the camera (+z is away) */
                box3d_rotate(box3d_normals[order[f]][0], box3d_normals[order[f]][1],
                             box3d_normals[order[f]][2], box3d_angle, box3d_angle_x, &nx, &ny, &nz);
                if (nz >= 0) {
                    continue;
                }

                /* face palette: red / orange / yellow round-robin */
                base = order[f] % 3 == 0 ? color_red :
                      (order[f] % 3 == 1 ? color_orange : color_yellow);

                /* light from upper-left-front: brightness = N.L */
                {
                    int dot = (-nx * 148 - ny * 148 - nz * 148) / 256;
                    int level = (dot + 148) / 74;

                    if (level > 3) {
                        level = 3;
                    }
                    if (level < 0) {
                        level = 0;
                    }
                    density = 3 - level;   /* 0 = lit solid, 3 = mostly dark */
                }

                min_y = py[face[0]];
                max_y = py[face[0]];
                for (int k = 0; k < 4; ++k) {
                    if (py[face[k]] < min_y) min_y = py[face[k]];
                    if (py[face[k]] > max_y) max_y = py[face[k]];
                }

                /* scanline fill with per-pixel dither shading; the
                 * pattern is anchored to face-local coords so it crawls
                 * as the face moves = live fading look */
                for (int sy = min_y; sy <= max_y; ++sy) {
                    int crossings[4];
                    int count = 0;

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

                        if (xa > xb) {
                            int t = xa;
                            xa = xb;
                            xb = t;
                        }
                        for (int sx = xa; sx <= xb; ++sx) {
                            if (density > 0 && ((sx + sy) & 3) < density) {
                                draw_pixel(cx + sx, cy + sy, color_gray_dark);
                            } else {
                                draw_pixel(cx + sx, cy + sy, base);
                            }
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
