#pragma once

#ifndef OOPSIE_GRAPHICS_H
#define OOPSIE_GRAPHICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct {
   uint8_t r, g, b, a;
} color_t;

typedef struct {
   int16_t x, y;
} vec_t;

typedef struct {
   uint16_t x, y;
} point_t;

typedef struct {
   point_t pos;
   vec_t size;
} rect_t;

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

typedef enum : uint8_t {
   ALIGNED_LEFT,
   ALIGNED_MIDDLE,
   ALIGNED_RIGHT
} alignment_t;

typedef void (*oopsie_graphics_callback_function_t)(void);
typedef oopsie_graphics_callback_function_t oopsie_cbf_t;

#define POINT(X, Y) ((point_t){.x = X, .y = Y})
#define VEC2D(X, Y) ((vec_t){.x = X, .y = Y})
#define COLOR(R, G, B, A) ((color_t){.r = R, .g = G, .b = B, .a = A})
#define RECT(X, Y, W, H) ((rect_t){.pos = {X, Y}, .size = {W, H}})
#define COLOR2UINT32(COLOR) ((uint32_t)(((uint32_t)COLOR.r << 16) | ((uint32_t)COLOR.g << 8) | (uint32_t)COLOR.b))

// Some Basic Colors
#define LIGHTGRAY COLOR(200, 200, 200, 255) // Light Gray
#define GRAY COLOR(130, 130, 130, 255)      // Gray
#define DARKGRAY COLOR(80, 80, 80, 255)     // Dark Gray
#define YELLOW COLOR(253, 249, 0, 255)      // Yellow
#define GOLD COLOR(255, 203, 0, 255)        // Gold
#define ORANGE COLOR(255, 161, 0, 255)      // Orange
#define PINK COLOR(255, 109, 194, 255)      // Pink
#define RED COLOR(230, 41, 55, 255)         // Red
#define MAROON COLOR(190, 33, 55, 255)      // Maroon
#define GREEN COLOR(0, 228, 48, 255)        // Green
#define LIME COLOR(0, 158, 47, 255)         // Lime
#define DARKGREEN COLOR(0, 117, 44, 255)    // Dark Green
#define SKYBLUE COLOR(102, 191, 255, 255)   // Sky Blue
#define BLUE COLOR(0, 121, 241, 255)        // Blue
#define DARKBLUE COLOR(0, 82, 172, 255)     // Dark Blue
#define PURPLE COLOR(200, 122, 255, 255)    // Purple
#define VIOLET COLOR(135, 60, 190, 255)     // Violet
#define DARKPURPLE COLOR(112, 31, 126, 255) // Dark Purple
#define BEIGE COLOR(211, 176, 131, 255)     // Beige
#define BROWN COLOR(127, 106, 79, 255)      // Brown
#define DARKBROWN COLOR(76, 63, 47, 255)    // Dark Brown
#define WHITE COLOR(255, 255, 255, 255)     // White
#define BLACK COLOR(0, 0, 0, 255)           // Black
#define BLANK COLOR(0, 0, 0, 0)             // Blank (Transparent)
#define MAGENTA COLOR(255, 0, 255, 255)     // Magenta

// STARTUP AND CLEANUP
int oopsie_graphics_init(void);
int oopsie_graphics_end(void);
void oopsie_graphics_set_target_fps(size_t fps);
void oopsie_graphics_set_window_name(const char* name);

// GET INFO
void oopsie_graphics_get_screen_size(void);
bool oopsie_graphics_check_screen_resize(void);

// MODES
void oopsie_graphics_begin_drawing(void);
int oopsie_graphics_event_type(void);
int oopsie_graphics_event_x(void);
int oopsie_graphics_event_y(void);
void oopsie_graphics_end_drawing(void);
void oopsie_graphics_end_frame(void);
void oopsie_graphics_skip_frame(void);

// BASIC DRAWS
void oopsie_graphics_set_bg_color(const color_t clr);
void oopsie_graphics_draw_char(const point_t point, const char c, const color_t fg_clr, const color_t bg_clr);
void oopsie_graphics_draw_string(const point_t point, const char* str, const color_t fg_clr, const color_t bg_clr);
void oopsie_graphics_draw_rect(const rect_t rect, const color_t clr);
void oopsie_graphics_draw_rect_lines(const rect_t rect, const color_t clr, rec_modes_t mode);

// KEYS
void oopsie_graphics_set_exit_key(uint16_t key);
bool oopsie_graphics_is_key_pressed(uint16_t key);

// MOUSE
point_t oopsie_graphics_get_mouse_pos(void);
vec_t oopsie_graphics_get_mouse_delta(void);
bool oopsie_graphics_is_mouse_button_pressed(uint16_t key);
bool oopsie_graphics_is_mouse_in_rect(const rect_t rect);

// CURSOR
void oopsie_graphics_hide_cursor(void);
void oopsie_graphics_show_cursor(void);
point_t oopsie_graphics_get_cursor_pos(void);
void oopsie_graphics_set_cursor_pos(const point_t point);
bool oopsie_graphics_is_cursor_in_rect(const rect_t rect);

// ADVANCED FUNCS
void oopsie_graphics_draw_text_in_rect(const char* text, rect_t rect, color_t fg_clr, color_t bg_color, alignment_t alignment);
void oopsie_graphics_invert_color_in_rect(const rect_t rect);

// COLOR FUNCTIONS
color_t oopsie_graphics_color_grayscale(const color_t color);
color_t oopsie_graphics_color_brightness(const color_t clr, const uint8_t percent);
color_t oopsie_graphics_color_mix(const color_t clr1, const color_t clr2);
color_t oopsie_graphics_color_invert(const color_t clr1);

// UI FRAMEWORK
typedef struct {
   rect_t bounds;
   char* buffer;         // Pointer to the text buffer
   size_t buffer_size;   // Max length of the buffer
   size_t cursor_pos;    // Where the typing cursor is
   size_t scroll_offset; // Horizontal scroll if text exceeds width
   bool is_focused;      // Is the user actively typing in it?
} oopsie_textbox_t;

typedef struct {
   rect_t bounds;
   const char* label_a;
   const char* label_b;
   bool is_toggled;        // false = label_a, true = label_b
   oopsie_cbf_t on_toggle; // Callback fired when clicked
} oopsie_toggle_t;

typedef struct {
   rect_t bounds;
   size_t total_items;    // Total elements in the list
   size_t visible_items;  // How many elements fit on screen
   size_t current_scroll; // Current top element index
   bool is_dragging;      // For mouse drag support
} oopsie_scrollbar_t;

void oopsie_ui_update_textbox(oopsie_textbox_t* tb);
void oopsie_ui_draw_textbox(const oopsie_textbox_t* tb, color_t active_clr, color_t inactive_clr, color_t text_clr);

void oopsie_ui_update_toggle(oopsie_toggle_t* tog);
void oopsie_ui_draw_toggle(const oopsie_toggle_t* tog, color_t fg_clr, color_t bg_clr);

void oopsie_ui_update_scrollbar(oopsie_scrollbar_t* sb);
void oopsie_ui_draw_scrollbar(const oopsie_scrollbar_t* sb, color_t fg_clr, color_t bg_clr);

uint32_t oopsie_graphics_get_char_pressed(void); // Needed to type letters into the textbox!

// GLOBALS
extern vec_t ScreenWH;
extern color_t BgColor;
extern size_t FPS;
extern size_t last_farme_time;
extern size_t wait_time;
extern struct timespec t;
extern uint16_t exit_key;
extern point_t CursorPos;
extern vec_t CursorDelta;

#endif