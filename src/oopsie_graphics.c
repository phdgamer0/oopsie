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
// GLOBALS
static vec_t ScreenWH = VEC2D(0, 0);
static color_t BgColor = COLOR(0, 0, 0, 0);
static size_t FPS = (size_t)0;
static size_t last_farme_time = (size_t)0;
static size_t wait_time = 0;
static struct timespec t;
static struct tb_event ev;
static uint16_t exit_key = TB_KEY_ESC;
int oopsie_graphics_init(void) {
   int res = tb_init();
   tb_set_output_mode(TB_OUTPUT_TRUECOLOR);
   oopsie_graphics_get_screen_size();
   return res;
}

int oopsie_graphics_end(void) {
   return tb_shutdown();
}

void oopsie_graphics_set_target_fps(size_t fps) {
   FPS = fps;
   wait_time = (1000000000ULL + (fps / 2)) / fps;
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
   tb_set_clear_attrs(TB_DEFAULT, ((uint32_t)BgColor.r << 16) | ((uint32_t)BgColor.g << 8) | (uint32_t)BgColor.b);
   tb_clear();
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
   }
}

void oopsie_graphics_set_bg_color(const color_t clr) {
   BgColor = clr;
}

void oopsie_graphics_draw_char(const point_t point, const char c, const color_t fg_clr, const color_t bg_clr) {
   tb_set_cell(point.x, point.y, c, ((uint32_t)fg_clr.r << 16) | ((uint32_t)fg_clr.g << 8) | (uint32_t)fg_clr.b, ((uint32_t)bg_clr.r << 16) | ((uint32_t)bg_clr.g << 8) | (uint32_t)bg_clr.b);
}

void oopsie_graphics_draw_string(const point_t point, const char* str, const color_t fg_clr, const color_t bg_clr) {
   tb_print(point.x, point.y, ((uint32_t)fg_clr.r << 16) | ((uint32_t)fg_clr.g << 8) | (uint32_t)fg_clr.b, ((uint32_t)bg_clr.r << 16) | ((uint32_t)bg_clr.g << 8) | (uint32_t)bg_clr.b, str);
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