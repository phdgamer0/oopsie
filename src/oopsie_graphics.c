#define _GNU_SOURCE
#define TB_IMPL
#define TB_OPT_TRUECOLOR
#include <oopsie_graphics.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <termbox2.h>
#include <time.h>
extern int keep_running;
static struct tb_event ev;

// GLOBALS
vec_t ScreenWH = {0, 0};
color_t BgColor = {0, 0, 0, 0};
size_t FPS = 0;
size_t last_farme_time = 0;
size_t wait_time = 0;
struct timespec t;
uint16_t exit_key = 0;
point_t MousePos = {0, 0};
point_t CursorPos = {0, 0};
vec_t MouseDelta = {0, 0};
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

// STARTUP AND CLEANUP
int oopsie_graphics_init(void) {
   printf("\033[22;0t");
   fflush(stdout);
   int res = tb_init();
   tb_set_input_mode(TB_INPUT_ESC | TB_INPUT_MOUSE);
   tb_set_output_mode(TB_OUTPUT_TRUECOLOR);
   oopsie_graphics_get_screen_size();
   exit_key = TB_KEY_ESC;
   return res;
}

int oopsie_graphics_end(void) {
   int res = tb_shutdown();
   printf("\033[23;0t");
   fflush(stdout);
   return res;
}

// GET INFO
void oopsie_graphics_set_target_fps(size_t fps) {
   FPS = fps;
   wait_time = (1000000000ULL + (fps / 2)) / fps;
}

void oopsie_graphics_set_window_name(const char* name) {
   char buf[64];
   snprintf(buf, sizeof(buf), "\033]0;%.55s\007", name);
   printf("%s", buf);
   fflush(stdout);
}

void oopsie_graphics_get_screen_size(void) {
   ScreenWH = VEC2D(tb_width(), tb_height());
}

bool oopsie_graphics_check_screen_resize(void) {
   vec_t temp = ScreenWH;
   oopsie_graphics_get_screen_size();
   return (bool)(temp.x != ScreenWH.x || temp.y != ScreenWH.y);
}

void oopsie_graphics_begin_drawing() {
   timespec_get(&t, TIME_UTC); // we hope it doesnt fail
   last_farme_time = ((uint64_t)t.tv_sec * 1000000000ULL) + t.tv_nsec;
   tb_set_clear_attrs(TB_DEFAULT, COLOR2UINT32(BgColor));
   tb_clear();
   memset((void*)&ev, (int)0, (size_t)24);
}

void oopsie_graphics_end_drawing(void) {
   tb_present();
   timespec_get(&t, TIME_UTC);
   last_farme_time = ((uint64_t)t.tv_sec * 1000000000ULL) + t.tv_nsec - last_farme_time;
   if (wait_time > last_farme_time) {
      t.tv_sec = (time_t)0;
      t.tv_nsec = wait_time - last_farme_time;
      nanosleep(&t, NULL);
   }
   if (tb_peek_event(&ev, 0) == TB_OK) {
      if (ev.type == TB_EVENT_KEY && ev.key == exit_key) {
         keep_running = 0;
      }
      else if (ev.type == TB_EVENT_MOUSE) {
         MouseDelta = VEC2D((int16_t)ev.x - MouseDelta.x, (int16_t)ev.y - MouseDelta.y);
         MousePos = POINT(ev.x, ev.y);
      }
   }
}

void oopsie_graphics_set_bg_color(const color_t clr) {
   BgColor = clr;
}

void oopsie_graphics_draw_char(const point_t point, const char c, const color_t fg_clr, const color_t bg_clr) {
   tb_set_cell(point.x, point.y, c, COLOR2UINT32(fg_clr), COLOR2UINT32(bg_clr));
}

void oopsie_graphics_draw_string(const point_t point, const char* str, const color_t fg_clr, const color_t bg_clr) {
   tb_print(point.x, point.y, COLOR2UINT32(fg_clr), COLOR2UINT32(bg_clr), str);
}

void oopsie_graphics_draw_rect(const rect_t rect, const color_t clr) {
   char buff[512]; // just in case some dude has 500+ columns on his terminal
   memset(buff, (int)' ', rect.size.x);
   buff[(size_t)rect.size.x] = '\0';
   for (uint16_t j = (uint16_t)rect.pos.y; j < rect.pos.y + rect.size.y; j++) {
      oopsie_graphics_draw_string(POINT(rect.pos.x, j), buff, clr, clr);
   }
}

void oopsie_graphics_draw_rect_lines(const rect_t rect, const color_t clr, rec_modes_t mode) {
   if (rect.size.x == 0 || rect.size.y == 0)
      return;
   oopsie_graphics_draw_string(POINT(rect.pos.x, rect.pos.y), REC_CELLS[mode + TOP_LEFT], clr, BgColor);
   if (rect.size.x > 2) {
      char h_buf[512];
      char* ptr = h_buf;
      const char* h_char = REC_CELLS[mode + H_LINE];
      size_t char_len = strlen(h_char);
      for (uint16_t i = 1; i < rect.size.x - 1 && (ptr - h_buf) < (ptrdiff_t)(sizeof(h_buf) - char_len - 1); i++) {
         memcpy(ptr, h_char, char_len);
         ptr += char_len;
      }
      *ptr = '\0';
      oopsie_graphics_draw_string(POINT(rect.pos.x + 1, rect.pos.y), h_buf, clr, BgColor);
      if (rect.size.y > 1) {
         oopsie_graphics_draw_string(POINT(rect.pos.x + 1, rect.pos.y + rect.size.y - 1), h_buf, clr, BgColor);
      }
   }
   if (rect.size.x > 1) {
      oopsie_graphics_draw_string(POINT(rect.pos.x + rect.size.x - 1, rect.pos.y), REC_CELLS[mode + TOP_RIGHT], clr, BgColor);
   }
   for (uint16_t j = 1; j < rect.size.y - 1; j++) {
      oopsie_graphics_draw_string(POINT(rect.pos.x, rect.pos.y + j), REC_CELLS[mode + V_LINE], clr, BgColor);
      if (rect.size.x > 1) {
         oopsie_graphics_draw_string(POINT(rect.pos.x + rect.size.x - 1, rect.pos.y + j), REC_CELLS[mode + V_LINE], clr, BgColor);
      }
   }
   if (rect.size.y > 1) {
      oopsie_graphics_draw_string(POINT(rect.pos.x, rect.pos.y + rect.size.y - 1), REC_CELLS[mode + BOT_LEFT], clr, BgColor);
      if (rect.size.x > 1) {
         oopsie_graphics_draw_string(POINT(rect.pos.x + rect.size.x - 1, rect.pos.y + rect.size.y - 1), REC_CELLS[mode + BOT_RIGHT], clr, BgColor);
      }
   }
}

// KEYS
void oopsie_graphics_set_exit_key(uint16_t key) {
   exit_key = key;
}

bool oopsie_graphics_is_key_pressed(uint16_t key) {
   return (bool)(ev.type == TB_EVENT_KEY && (ev.key == key || ev.ch == key));
}

// MOUSE
point_t oopsie_graphics_get_mouse_pos(void) {
   return MousePos;
}
vec_t oopsie_graphics_get_mouse_delta(void) {
   return MouseDelta;
}
bool oopsie_graphics_is_mouse_button_pressed(uint16_t key) {
   return (bool)(ev.type == TB_EVENT_MOUSE && ev.key == key);
}
bool oopsie_graphics_is_mouse_in_rect(const rect_t rect) {
   return (bool)(MousePos.x >= rect.pos.x && MousePos.x <= rect.pos.x + (uint16_t)rect.size.x && MousePos.y >= rect.pos.y && MousePos.y <= rect.pos.y + (uint16_t)rect.size.y);
}

// CURSOR
void oopsie_graphics_hide_cursor(void) {
   tb_hide_cursor();
}
void oopsie_graphics_show_cursor(void) {
   tb_set_cursor(CursorPos.x, CursorPos.y);
}
point_t oopsie_graphics_get_cursor_pos(void) {
   return CursorPos;
}
void oopsie_graphics_set_cursor_pos(const point_t point) {
   CursorPos = point;
   tb_set_cursor(point.x, point.y);
}
bool oopsie_graphics_is_cursor_in_rect(const rect_t rect) {
   return (bool)(CursorPos.x >= rect.pos.x && CursorPos.x <= rect.pos.x + (uint16_t)rect.size.x && CursorPos.y >= rect.pos.y && CursorPos.y <= rect.pos.y + (uint16_t)rect.size.y);
}

// ADVANCED FUNCS
void oopsie_graphics_draw_text_in_rect(const char* text, rect_t rect, color_t fg_clr, color_t bg_color, alignment_t alignment) {
   if (!text || rect.size.x < 3 || rect.size.y < 3)
      return;
   uint16_t inner_w = rect.size.x - 2;
   uint16_t inner_h = rect.size.y - 2;
   uint16_t start_y = rect.pos.y + 1;
   const char* ptr = text;
   char buff[512]; // that one guy who will have 100x zoomed out with the 8k monitor im coming for you
   for (uint16_t line = 0; line < inner_h && *ptr != '\0'; line++) {
      size_t chunk_len = 0;
      while (ptr[chunk_len] != '\0' && ptr[chunk_len] != '\n' && chunk_len < inner_w) {
         chunk_len++;
      }
      if (chunk_len >= sizeof(buff)) {
         chunk_len = sizeof(buff) - 1;
      }
      memcpy(buff, ptr, chunk_len);
      buff[chunk_len] = '\0';
      uint16_t draw_x = rect.pos.x + 1;
      if (alignment == ALIGNED_MIDDLE) {
         draw_x += (inner_w - (uint16_t)chunk_len) / 2;
      }
      else if (alignment == ALIGNED_RIGHT) {
         draw_x += (inner_w - (uint16_t)chunk_len);
      }
      oopsie_graphics_draw_string(POINT(draw_x, start_y + line), buff, fg_clr, bg_color);
      ptr += chunk_len;
      if (*ptr == '\n') {
         ptr++;
      }
   }
}
void oopsie_graphics_invert_color_in_rect(const rect_t rect) {
   uint32_t tmp;
   struct tb_cell* cell;
   for (uint16_t j = rect.pos.y + (uint16_t)1; j < rect.pos.y + (uint16_t)rect.size.y - (uint16_t)1; j++) {
      for (uint16_t i = rect.pos.x + (uint16_t)1; i < rect.pos.x + (uint16_t)rect.size.x - (uint16_t)1; i++) {
         // GCC PLEASE OPTIMIZE THIS
         if (tb_get_cell(i, j, (int)1, &cell) == TB_OK) {
            tmp = cell->bg;
            cell->bg = cell->fg;
            cell->fg = tmp;
         }
      }
   }
}

// COLOR FUNCTIONS
color_t oopsie_graphics_color_grayscale(const color_t color) {
   // they call me the god of casting
   uint8_t gray = (uint8_t)(((uint32_t)color.r * (uint32_t)299 / (uint32_t)1000) + ((uint32_t)color.g * (uint32_t)587 / (uint32_t)1000) + ((uint32_t)color.b * (uint32_t)114 / (uint32_t)1000) & (uint32_t)0xFF);
   return COLOR(gray, gray, gray, 0xFF);
}
color_t oopsie_graphics_color_brightness(const color_t clr, const uint8_t percent) {
   uint8_t r, g, b;
   if (__builtin_mul_overflow(clr.r, percent, &r))
      r = 0xFF;
   if (__builtin_mul_overflow(clr.g, percent, &g))
      g = 0xFF;
   if (__builtin_mul_overflow(clr.b, percent, &b))
      b = 0xFF;
   return COLOR(r, g, b, clr.a);
}
color_t oopsie_graphics_color_mix(const color_t clr1, const color_t clr2) {
   uint8_t r, g, b;
   if (__builtin_add_overflow(clr1.r, clr2.r, &r))
      r = 0xFF;
   if (__builtin_add_overflow(clr1.g, clr2.g, &g))
      g = 0xFF;
   if (__builtin_add_overflow(clr1.b, clr2.b, &b))
      b = 0xFF;
   return COLOR(r, g, b, clr1.a);
}
color_t oopsie_graphics_color_invert(const color_t clr1) {
   return COLOR(0xFF - clr1.r, 0xFF - clr1.g, 0xFF - clr1.b, clr1.a);
}

// UI
uint32_t oopsie_graphics_get_char_pressed(void) {
   if (ev.type == TB_EVENT_KEY && ev.ch != 0) {
      return ev.ch;
   }
   return 0;
}

void oopsie_ui_update_textbox(oopsie_textbox_t* tb) {
   if (!tb || !tb->buffer || tb->buffer_size == 0)
      return;
   if (oopsie_graphics_is_mouse_button_pressed(TB_KEY_MOUSE_LEFT)) {
      tb->is_focused = oopsie_graphics_is_mouse_in_rect(tb->bounds);
   }
   if (!tb->is_focused)
      return;
   if (ev.type == TB_EVENT_KEY) {
      if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2) {
         if (tb->cursor_pos > 0) {
            tb->cursor_pos--;
            size_t len = strlen(tb->buffer);
            for (size_t i = tb->cursor_pos; i < len; i++) {
               tb->buffer[i] = tb->buffer[i + 1];
            }
         }
      }
      else if (ev.key == TB_KEY_DELETE) {
         size_t len = strlen(tb->buffer);
         if (tb->cursor_pos < len) {
            for (size_t i = tb->cursor_pos; i < len; i++) {
               tb->buffer[i] = tb->buffer[i + 1];
            }
         }
      }
      else if (ev.key == TB_KEY_ARROW_LEFT) {
         if (tb->cursor_pos > 0)
            tb->cursor_pos--;
      }
      else if (ev.key == TB_KEY_ARROW_RIGHT) {
         if (tb->cursor_pos < strlen(tb->buffer))
            tb->cursor_pos++;
      }
      else if (ev.ch != 0) {
         // Insert character
         size_t len = strlen(tb->buffer);
         if (len < tb->buffer_size - 1) {
            for (size_t i = len + 1; i > tb->cursor_pos; i--) {
               tb->buffer[i] = tb->buffer[i - 1];
            }
            tb->buffer[tb->cursor_pos] = (char)ev.ch;
            tb->cursor_pos++;
         }
      }
   }
   int inner_w = tb->bounds.size.x - 2;
   if (inner_w > 0) {
      if (tb->cursor_pos < tb->scroll_offset) {
         tb->scroll_offset = tb->cursor_pos;
      }
      else if (tb->cursor_pos - tb->scroll_offset >= (size_t)inner_w) {
         tb->scroll_offset = tb->cursor_pos - inner_w + 1;
      }
   }
}

void oopsie_ui_draw_textbox(const oopsie_textbox_t* tb, color_t active_clr, color_t inactive_clr, color_t text_clr) {
   if (!tb)
      return;
   color_t clr = tb->is_focused ? active_clr : inactive_clr;
   oopsie_graphics_draw_rect_lines(tb->bounds, clr, HEAVY_HARD);
   int inner_w = tb->bounds.size.x - 2;
   if (inner_w <= 0 || !tb->buffer)
      return;
   char visible_text[512];
   size_t len = strlen(tb->buffer);
   size_t draw_start = tb->scroll_offset;
   if (draw_start > len)
      draw_start = len;
   size_t draw_len = len - draw_start;
   if (draw_len > (size_t)inner_w)
      draw_len = inner_w;
   if (draw_len >= sizeof(visible_text))
      draw_len = sizeof(visible_text) - 1;
   memcpy(visible_text, tb->buffer + draw_start, draw_len);
   visible_text[draw_len] = '\0';
   oopsie_graphics_draw_string(POINT(tb->bounds.pos.x + 1, tb->bounds.pos.y + 1), visible_text, text_clr, BgColor);
   if (tb->is_focused) {
      uint16_t cur_x = tb->bounds.pos.x + 1 + (uint16_t)(tb->cursor_pos - tb->scroll_offset);
      if (cur_x < tb->bounds.pos.x + tb->bounds.size.x - 1) {
         oopsie_graphics_set_cursor_pos(POINT(cur_x, tb->bounds.pos.y + 1));
      }
   }
   else {
      oopsie_graphics_hide_cursor();
   }
}

void oopsie_ui_update_toggle(oopsie_toggle_t* tog) {
   if (!tog)
      return;
   if (oopsie_graphics_is_mouse_button_pressed(TB_KEY_MOUSE_LEFT)) {
      if (oopsie_graphics_is_mouse_in_rect(tog->bounds)) {
         tog->is_toggled = !tog->is_toggled;
         if (tog->on_toggle) {
            tog->on_toggle();
         }
      }
   }
}

void oopsie_ui_draw_toggle(const oopsie_toggle_t* tog, color_t fg_clr, color_t bg_clr) {
   if (!tog)
      return;
   oopsie_graphics_draw_rect_lines(tog->bounds, fg_clr, HEAVY_HARD);
   const char* text = tog->is_toggled ? tog->label_b : tog->label_a;
   if (text) {
      oopsie_graphics_draw_text_in_rect(text, tog->bounds, fg_clr, bg_clr, ALIGNED_MIDDLE);
   }
}

void oopsie_ui_update_scrollbar(oopsie_scrollbar_t* sb) {
   if (!sb || sb->total_items <= sb->visible_items)
      return;
   if (oopsie_graphics_is_mouse_in_rect(sb->bounds)) {
      if (oopsie_graphics_is_mouse_button_pressed(TB_KEY_MOUSE_WHEEL_UP)) {
         if (sb->current_scroll > 0)
            sb->current_scroll--;
      }
      else if (oopsie_graphics_is_mouse_button_pressed(TB_KEY_MOUSE_WHEEL_DOWN)) {
         if (sb->current_scroll + sb->visible_items < sb->total_items) {
            sb->current_scroll++;
         }
      }
   }
   if (oopsie_graphics_is_mouse_button_pressed(TB_KEY_MOUSE_LEFT)) {
      if (oopsie_graphics_is_mouse_in_rect(sb->bounds)) {
         sb->is_dragging = true;
      }
   }
   if (ev.type == TB_EVENT_MOUSE && ev.key == TB_KEY_MOUSE_RELEASE) {
      sb->is_dragging = false;
   }
   if (sb->is_dragging && ev.type == TB_EVENT_MOUSE) {
      int usable_height = sb->bounds.size.y - 2;
      if (usable_height > 0) {
         int rel_y = ev.y - (sb->bounds.pos.y + 1);
         if (rel_y < 0)
            rel_y = 0;
         if (rel_y >= usable_height)
            rel_y = usable_height - 1;

         float percentage = (float)rel_y / (float)usable_height;
         sb->current_scroll = (size_t)(percentage * (float)(sb->total_items - sb->visible_items + 1));
         if (sb->current_scroll + sb->visible_items > sb->total_items) {
            sb->current_scroll = sb->total_items - sb->visible_items;
         }
      }
   }
}

void oopsie_ui_draw_scrollbar(const oopsie_scrollbar_t* sb, color_t fg_clr, color_t bg_clr) {
   if (!sb)
      return;
   oopsie_graphics_draw_rect_lines(sb->bounds, fg_clr, HEAVY_HARD);
   if (sb->total_items <= sb->visible_items || sb->total_items == 0)
      return;
   int usable_height = sb->bounds.size.y - 2;
   if (usable_height <= 0)
      return;
   float visible_ratio = (float)sb->visible_items / (float)sb->total_items;
   int thumb_size = (int)(visible_ratio * (float)usable_height);
   if (thumb_size < 1)
      thumb_size = 1;
   float scroll_ratio = (float)sb->current_scroll / (float)(sb->total_items - sb->visible_items);
   int max_thumb_y = usable_height - thumb_size;
   int thumb_y = (int)(scroll_ratio * (float)max_thumb_y);
   for (int i = 0; i < thumb_size; i++) {
      oopsie_graphics_draw_string(POINT(sb->bounds.pos.x + 1, sb->bounds.pos.y + 1 + thumb_y + i), "█", fg_clr, bg_clr);
   }
}