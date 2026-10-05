#pragma once
// Type-only stand-in for epdiy's public headers (rect, font, rotation).
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x;
    int y;
    int width;
    int height;
} EpdRect;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t advance_x;
    int16_t left;
    int16_t top;
    uint16_t compressed_size;
    uint32_t data_offset;
} EpdGlyph;

typedef struct {
    uint32_t first;
    uint32_t last;
    uint32_t offset;
} EpdUnicodeInterval;

typedef struct {
    const uint8_t* bitmap;
    const EpdGlyph* glyph;
    const EpdUnicodeInterval* intervals;
    uint32_t interval_count;
    bool compressed;
    uint16_t advance_y;
    int ascender;
    int descender;
} EpdFont;

enum EpdFontFlags {
    EPD_DRAW_BACKGROUND = 0x1,
    EPD_DRAW_ALIGN_LEFT = 0x2,
    EPD_DRAW_ALIGN_RIGHT = 0x4,
    EPD_DRAW_ALIGN_CENTER = 0x8,
};

typedef struct {
    uint32_t fg_color : 4;
    uint32_t bg_color : 4;
    uint32_t fallback_glyph;
    enum EpdFontFlags flags;
} EpdFontProperties;

enum EpdDrawError {
    EPD_DRAW_SUCCESS = 0x0,
    EPD_DRAW_INVALID_FONT_FLAGS = 0x1,
    EPD_DRAW_NO_such_GLYPH = 0x2,
    EPD_DRAW_STRING_INVALID = 0x4,
    EPD_DRAW_GLYPH_FALLBACK_FAILED = 0x8,
};

enum EpdRotation {
    EPD_ROT_LANDSCAPE = 0,
    EPD_ROT_PORTRAIT = 1,
    EPD_ROT_INVERTED_LANDSCAPE = 2,
    EPD_ROT_INVERTED_PORTRAIT = 3,
};

#ifdef __cplusplus
}
#endif
