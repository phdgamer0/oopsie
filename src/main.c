#define _GNU_SOURCE
#include <oopsie_graphics.h>
#include <oopsie_wal.h>
#include <signal.h>
volatile sig_atomic_t keep_running = 1;
void handle_sigint(int signum) {
   (void)signum;
   keep_running = 0;
}
int main() {
   struct sigaction sa;
   sa.sa_handler = handle_sigint; // Point to our handler function
   sigemptyset(&sa.sa_mask);      // Clear mask to not block other signals
   sa.sa_flags = 0;               // No special flags needed
   if (sigaction(SIGINT, &sa, NULL) == -1) {
      return 1;
   }
   oopsie_graphics_init();
   oopsie_graphics_set_target_fps(60);
   while (keep_running) {
      oopsie_graphics_begin_drawing();
      oopsie_graphics_draw_rect_lines(RECT(5, 4, 16, 8), COLOR(0x00, 0xFF, 0xFF, 0xFF), HEAVY_HARD);
      oopsie_graphics_draw_rect_lines(RECT(25, 14, 16, 8), COLOR(0x00, 0xFF, 0xFF, 0xFF), DOUBLE_LIGHT);
      oopsie_graphics_draw_rect_lines(RECT(5, 24, 16, 8), COLOR(0x00, 0xFF, 0xFF, 0xFF), LIGHT_POINTY);
      oopsie_graphics_draw_rect_lines(RECT(25, 24, 16, 8), COLOR(0x00, 0xFF, 0xFF, 0xFF), LIGHT_ROUND);
      oopsie_graphics_end_drawing();
   }
   oopsie_graphics_end();
   return 0;
}