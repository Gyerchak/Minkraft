#include "PngLoader.h"
#include <png.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

bool loadPng(const char* path, std::vector<unsigned char>& rgba, int& width, int& height) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    unsigned char sig[8];
    if (fread(sig, 1, 8, f) != 8 || png_sig_cmp(sig, 0, 8) != 0) {
        fclose(f);
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (!png || !info) {
        if (png) png_destroy_read_struct(&png, &info, nullptr);
        fclose(f);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(f);
        return false;
    }

    png_init_io(png, f);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    width = (int)png_get_image_width(png, info);
    height = (int)png_get_image_height(png, info);
    int colorType = png_get_color_type(png, info);
    int bitDepth = png_get_bit_depth(png, info);

    // Normalize to 8-bit RGBA.
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    size_t rowbytes = png_get_rowbytes(png, info);
    std::vector<unsigned char> raw(rowbytes * (size_t)height);
    std::vector<png_bytep> rows(height);
    for (int y = 0; y < height; y++)
        rows[y] = raw.data() + (size_t)y * rowbytes;
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(f);

    // Copy to tightly packed RGBA (output is 4 bytes/pixel after transforms).
    rgba.resize((size_t)width * height * 4);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            std::memcpy(&rgba[(size_t)(y * width + x) * 4],
                        raw.data() + (size_t)y * rowbytes + (size_t)x * 4, 4);
    return true;
}

bool savePng(const char* path, const unsigned char* rgba, int width, int height) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (!png || !info) {
        if (png) png_destroy_write_struct(&png, &info);
        fclose(f);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(f);
        return false;
    }

    png_init_io(png, f);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows(height);
    for (int y = 0; y < height; y++)
        rows[y] = (png_bytep)(rgba + (size_t)y * width * 4);
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return true;
}
