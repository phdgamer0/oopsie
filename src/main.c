#define _GNU_SOURCE
#include <oopsie_graphics.h>
#include <oopsie_wal.h>
#include <signal.h>
volatile sig_atomic_t keep_running = 1;
void my_toggle_cb(void) {
   // callback logic here if needed
}
int main(void) {
   oopsie_graphics_init();
   oopsie_graphics_set_target_fps(60);
   oopsie_graphics_set_window_name("UI Demo");
   oopsie_graphics_set_bg_color(BLACK);

   char tb_buffer[256] = "Hello Oopsie";
   oopsie_textbox_t textbox = {
       .bounds = RECT(2, 2, 30, 3),
       .buffer = tb_buffer,
       .buffer_size = 256,
       .cursor_pos = 12,
       .scroll_offset = 0,
       .is_focused = false};

   oopsie_toggle_t toggle = {
       .bounds = RECT(2, 6, 15, 3),
       .label_a = "ENABLE",
       .label_b = "DISABLE",
       .is_toggled = false,
       .on_toggle = my_toggle_cb};

   oopsie_scrollbar_t scrollbar = {
       .bounds = RECT(35, 2, 3, 20),
       .total_items = 100,
       .visible_items = 10,
       .current_scroll = 0,
       .is_dragging = false};

   while (keep_running) {
      // 1. UPDATE UI
      oopsie_ui_update_textbox(&textbox);
      oopsie_ui_update_toggle(&toggle);
      oopsie_ui_update_scrollbar(&scrollbar);

      // 2. DRAW
      oopsie_graphics_begin_drawing();

      oopsie_ui_draw_textbox(&textbox, RED, GREEN, PURPLE);
      oopsie_ui_draw_toggle(&toggle, BLUE, BLACK);
      oopsie_ui_draw_scrollbar(&scrollbar, YELLOW, DARKGRAY);

      oopsie_graphics_end_drawing();
   }
   oopsie_graphics_end();
   return 0;
}