#include <png.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

static void build_system_palette(Color *palette) {
    int index = 0;
    const uint8_t cube[] = {0, 51, 102, 153, 204, 255};

    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) {
                palette[index++] = (Color){cube[r], cube[g], cube[b]};
            }
        }
    }

    for (int i = 0; i < 40; ++i) {
        uint8_t shade = (uint8_t)((i * 255) / 39);
        palette[index++] = (Color){shade, shade, shade};
    }
}

static uint8_t nearest_color(const Color *palette, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t best_distance = 0xFFFFFFFFu;
    uint8_t best_index = 0;

    for (int i = 0; i < 256; ++i) {
        int dr = (int)palette[i].r - r;
        int dg = (int)palette[i].g - g;
        int db = (int)palette[i].b - b;
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = (uint8_t)i;
        }
    }

    return best_index;
}

static png_bytep *read_png_rows(FILE *fp, int *width, int *height) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (!png || !info) {
        return NULL;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    *width = (int)png_get_image_width(png, info);
    *height = (int)png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);

    png_bytep *rows = malloc(sizeof(png_bytep) * (size_t)(*height));
    png_size_t rowbytes = png_get_rowbytes(png, info);
    for (int y = 0; y < *height; ++y) {
        rows[y] = malloc(rowbytes);
    }

    png_read_image(png, rows);
    png_destroy_read_struct(&png, &info, NULL);
    return rows;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s input.png output.bin\n", argv[0]);
        return 1;
    }

    FILE *input = fopen(argv[1], "rb");
    if (!input) {
        perror("open input");
        return 1;
    }

    int width = 0;
    int height = 0;
    png_bytep *rows = read_png_rows(input, &width, &height);
    fclose(input);

    if (!rows) {
        fprintf(stderr, "failed to decode %s\n", argv[1]);
        return 1;
    }

    if (width <= 0 || height <= 0 || width > 65535 || height > 65535) {
        fprintf(stderr, "%s has unsupported size %dx%d\n", argv[1], width, height);
        return 1;
    }

    Color palette[256];
    build_system_palette(palette);

    FILE *output = fopen(argv[2], "wb");
    if (!output) {
        perror("open output");
        return 1;
    }

    {
        uint16_t dims[2] = {(uint16_t)width, (uint16_t)height};
        fwrite(dims, sizeof(uint16_t), 2, output);
    }

    for (int y = 0; y < height; ++y) {
        png_bytep row = rows[y];
        for (int x = 0; x < width; ++x) {
            png_bytep px = &row[x * 4];
            uint8_t index = nearest_color(palette, px[0], px[1], px[2]);
            fwrite(&index, 1, 1, output);
        }
    }

    for (int y = 0; y < height; ++y) {
        png_bytep row = rows[y];
        for (int x = 0; x < width; ++x) {
            uint8_t alpha = row[x * 4 + 3];
            fwrite(&alpha, 1, 1, output);
        }
        free(rows[y]);
    }

    free(rows);
    fclose(output);
    return 0;
}
