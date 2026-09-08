#define _GNU_SOURCE
#include <oopsie_graphics.h>
#include <oopsie_wal.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

volatile sig_atomic_t keep_running = 1;
static const char* row1 = "░░█▀█░█▀█░█▀█░█▀▀░▀█▀░█▀▀░█░█░░";
static const char* row2 = "░░█░█░█░█░█▀▀░▀▀█░░█░░█▀▀░█░█░░";
static const char* row3 = "░░▀▀▀░▀▀▀░▀░░░▀▀▀░▀▀▀░▀▀▀░▄░▄░░";

void handle_args(int argc, char* argv[]) {
   if (argc > 1) {
      if (strcmp(argv[1], "start") == 0) {
         OopWalContext wal;
         mkdir("/tmp/oopsie", 0700);
         if (WalFile_Open(&wal, WAL_PATH)) {
            WalHeader* header = (WalHeader*)wal.map;
            __atomic_store_n(&header->is_monitoring, 1, __ATOMIC_RELAXED);
            WalFile_Close(&wal);
         }
         char shim_path[512] = {0};
         char exe_path[512] = {0};
         ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
         if (len != -1) {
            exe_path[len] = '\0';
            char* last_slash = strrchr(exe_path, '/');
            if (last_slash) {
               *last_slash = '\0';
               snprintf(shim_path, sizeof(shim_path), "%s/liboopsie_shim.so", exe_path);
            }
         }
         setenv("LD_PRELOAD", shim_path, 1);
         printf("[Oopsie] Starting monitored shell... Type 'exit' to end.\n");
         execlp("bash", "bash", NULL);
         perror("execlp");
         exit(1);
      }
      else if (strcmp(argv[1], "end") == 0) {
         OopWalContext wal;
         if (WalFile_Open(&wal, WAL_PATH)) {
            WalHeader* header = (WalHeader*)wal.map;
            __atomic_store_n(&header->is_monitoring, 0, __ATOMIC_RELAXED);
            WalFile_Close(&wal);
            printf("[Oopsie] Monitoring paused.\n");
         }
         else {
            printf("[Oopsie] Failed to open WAL.\n");
         }
         exit(0);
      }
      else if (strcmp(argv[1], "uninstall") == 0) {
         system("rm -rf /tmp/oopsie");
         printf("[Oopsie] Vault uninstalled from /tmp/oopsie\n");
         exit(0);
      }
      else {
         printf("Unknown command: %s\n", argv[1]);
         printf("Usage: %s [start|end|uninstall]\n", argv[0]);
         exit(1);
      }
   }
}

// Dummy toggle logic for TUI
void my_toggle_cb(void) {
   OopWalContext wal;
   if (WalFile_Open(&wal, WAL_PATH)) {
      WalHeader* header = (WalHeader*)wal.map;
      uint32_t val = __atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED);
      __atomic_store_n(&header->is_monitoring, val ? 0 : 1, __ATOMIC_RELAXED);
      WalFile_Close(&wal);
   }
}

int main(int argc, char* argv[]) {
   handle_args(argc, argv);
   oopsie_graphics_init();
   oopsie_graphics_set_target_fps(60);
   oopsie_graphics_set_window_name("Oopsie TUI");
   oopsie_graphics_set_bg_color(BLACK);
   // Check initial state for toggle
   bool is_mon = true;
   OopWalContext wal;
   if (WalFile_Open(&wal, WAL_PATH)) {
      WalHeader* header = (WalHeader*)wal.map;
      is_mon = __atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 1;
      WalFile_Close(&wal);
   }

   char search_buf[256] = {0};
   oopsie_textbox_t search_box = {
       .bounds = RECT(13, 1, 30, 3),
       .buffer = search_buf,
       .buffer_size = 256,
       .cursor_pos = 0,
       .scroll_offset = 0,
       .is_focused = false};

   oopsie_scrollbar_t log_scrollbar = {
       .bounds = RECT(0, 0, 1, 10), // Will position dynamically
       .total_items = 0,
       .visible_items = 0,
       .current_scroll = 0,
       .is_dragging = false};

   while (keep_running) {
      oopsie_graphics_check_screen_resize();
      // Update toggle state
      if (WalFile_Open(&wal, WAL_PATH)) {
         WalHeader* header = (WalHeader*)wal.map;
         is_mon = __atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 1;
         WalFile_Close(&wal);
      }
      // Hotkey handling
      if (oopsie_graphics_is_key_pressed('q') || oopsie_graphics_is_key_pressed('Q')) {
         keep_running = 0;
      }
      if (oopsie_graphics_is_key_pressed('s') || oopsie_graphics_is_key_pressed('S')) {
         my_toggle_cb();
      }
      // 1. UPDATE UI
      oopsie_ui_update_textbox(&search_box);
      oopsie_ui_update_scrollbar(&log_scrollbar);
      // 2. DRAW
      oopsie_graphics_begin_drawing();
      oopsie_graphics_set_bg_color(BLACK);
      time_t now = time(NULL);
      struct tm* t = localtime(&now);
      char time_str[64];
      strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

      // ----- HEADER -----
      rect_t header_rect = RECT(0, ScreenWH.y - 2, ScreenWH.x, 3);
      oopsie_graphics_draw_text_in_rect("Oopsie TUI v0.1.1", header_rect, BLUE, BgColor, ALIGNED_LEFT);
      oopsie_graphics_draw_text_in_rect(time_str, header_rect, WHITE, BgColor, ALIGNED_RIGHT);
      // ----- SIDEBAR & LAYOUT CALCS -----
      uint16_t sb_width = 35;
      uint16_t sb_x = ScreenWH.x > sb_width ? ScreenWH.x - sb_width : 0;
      uint16_t log_width = sb_x > 2 ? sb_x - 1 : 0;
      uint16_t panel_h = ScreenWH.y > 4 ? ScreenWH.y - 2 : 0;
      // ----- LOG PANEL -----
      if (log_width > 10 && panel_h > 10) {
         rect_t log_rect = RECT(0, 0, log_width, panel_h + 1);
         oopsie_graphics_draw_rect_lines(log_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(2, 0), " WAL Feed ", BLUE, BLACK);
         // Search bar
         oopsie_graphics_draw_string(POINT(2, 2), "/search: ", YELLOW, BLACK);
         oopsie_ui_draw_textbox(&search_box, GRAY, DARKGRAY, WHITE);
         // Columns
         oopsie_graphics_draw_string(POINT(2, 4), "TIME         ACTION    PATH", DARKGRAY, BLACK);
         // Mock log entry
         oopsie_graphics_draw_string(POINT(2, 6), "[12:34:56.7] ", DARKGRAY, BLACK);
         oopsie_graphics_draw_string(POINT(15, 6), "CREATE    ", GREEN, BLACK);
         oopsie_graphics_draw_string(POINT(25, 6), "/tmp/mock_file.txt", WHITE, BLACK);
      }
      // ----- STATS PANEL -----
      if (sb_width > 0 && panel_h > 15) {
         rect_t stats_rect = RECT(sb_x, 0, sb_width, 10);
         oopsie_graphics_draw_rect_lines(stats_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 0), " System Stats ", BLUE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 2), "Shim:", WHITE, BLACK);
         if (is_mon) {
            oopsie_graphics_draw_string(POINT(sb_x + 18, 2), "ACTIVE", GREEN, BLACK);
         }
         else {
            oopsie_graphics_draw_string(POINT(sb_x + 18, 2), "INACTIVE", RED, BLACK);
         }
         oopsie_graphics_draw_string(POINT(sb_x + 2, 3), "WAL Size:", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 18, 3), "16 KB", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 4), "Events:", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 18, 4), "0", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 5), "Tombstones:", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 18, 5), "0", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 6), "Uptime:", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 18, 6), "0s", WHITE, BLACK);
         // ----- HOTKEYS PANEL -----
         rect_t hk_rect = RECT(sb_x, 10, sb_width, panel_h - 14);
         oopsie_graphics_draw_rect_lines(hk_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 10), " Hotkeys ", BLUE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 12), "P:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 12), "Purge All", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 13), "C:", BLACK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 13), "Compact", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 14), "S:", BLACK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 14), "Toggle Shim", WHITE, BLACK);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 15), "Q:", BLACK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 15), "Quit", WHITE, BLACK);
         // ----- LOGO PANEL -----
         rect_t lg_rect = RECT(sb_x, ScreenWH.y - 6, sb_width, 5);
         oopsie_graphics_draw_rect_lines(lg_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 5), row1, PINK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 4), row2, PINK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 3), row3, PINK, BgColor);
      }
      oopsie_graphics_end_drawing();
   }
   oopsie_graphics_end();
   return 0;
}