#pragma once

#ifndef OOPSIE_GRAPHICS_H
#define OOPSIE_GRAPHICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
   uint8_t r, g, b, a;
} color_t;

typedef struct {
   uint16_t x, y;
} vec_t;

typedef struct {
   uint16_t x, y;
} point_t;

typedef struct {
   point_t pos;
   vec_t size;
} rect_t;
#pragma pack(pop)

typedef enum : size_t {
   LIGHT_POINTY = 0,
   LIGHT_ROUND = 11,
   HEAVY_HARD = 22,
   DOUBLE_LIGHT = 33
} rec_modes_t;

typedef enum : size_t {
   TOP_LEFT,
   H_LINE,
   TOP_RIGHT,
   V_LINE,
   CON_TOP,
   CON_BOT,
   CON_MID,
   CON_LEFT,
   CON_RIGHT,
   BOT_LEFT,
   BOT_RIGHT
} rec_idx_t;
static const char* REC_CELLS[44] = {
    "┌",
    "─",
    "┐",
    "│",
    "┬",
    "┴",
    "┼",
    "├",
    "┤",
    "└",
    "┘",
    "╭",
    "─",
    "╮",
    "│",
    "┬",
    "┴",
    "┼",
    "├",
    "┤",
    "╰",
    "╯",
    "┏",
    "━",
    "┓",
    "┃",
    "┳",
    "┻",
    "╋",
    "┣",
    "┫",
    "┗",
    "┛",
    "╔",
    "═",
    "╗",
    "║",
    "╦",
    "╩",
    "╬",
    "╠",
    "╣",
    "╚",
    "╝",
};

#define POINT(X, Y) ((point_t){.x = X, .y = Y})
#define VEC2D(X, Y) ((vec_t){.x = X, .y = Y})
#define COLOR(R, G, B, A) ((color_t){.r = R, .g = G, .b = B, .a = A})
#define RECT(X, Y, W, H) ((rect_t){.pos = {X, Y}, .size = {W, H}})

// STARTUP AND CLEANUP
int oopsie_graphics_init(void);
int oopsie_graphics_end(void);
void oopsie_graphics_set_target_fps(size_t fps);
// GET INFO
void oopsie_graphics_get_screen_size(void);
bool oopsie_graphics_check_screen_resize(void);

// MODES
void oopsie_graphics_begin_drawing(void);
void oopsie_graphics_end_drawing(void);

// BASIC DRAWS
void oopsie_graphics_set_bg_color(const color_t clr);
void oopsie_graphics_draw_char(const point_t point, const char c, const color_t fg_clr, const color_t bg_clr);
void oopsie_graphics_draw_string(const point_t point, const char* str, const color_t fg_clr, const color_t bg_clr);
void oopsie_graphics_draw_rect(const rect_t rect, const color_t clr);
void oopsie_graphics_draw_rect_lines(const rect_t rect, const color_t clr, rec_modes_t mode);

#endif