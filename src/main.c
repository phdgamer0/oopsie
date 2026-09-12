#define _GNU_SOURCE
#include <bits/types/timer_t.h>
#include <oopsie_graphics.h>
#include <oopsie_wal.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termbox2.h>
#include <time.h>
#include <unistd.h>

#define VIEW_SZ (size_t)500

volatile sig_atomic_t keep_running = 1;
static const char* row1 = "░░█▀█░█▀█░█▀█░█▀▀░▀█▀░█▀▀░█░█░░";
static const char* row2 = "░░█░█░█░█░█▀▀░▀▀█░░█░░█▀▀░█░█░░";
static const char* row3 = "░░▀▀▀░▀▀▀░▀░░░▀▀▀░▀▀▀░▀▀▀░▄░▄░░";

void handle_args(int argc, char* argv[]) {
   if (argc > 1) {
      if (strcmp(argv[1], "start") == 0) {
         const char* ushell = getenv("SHELL");
         if (!ushell)
            ushell = "sh";
         OopWalContext wal;
         mkdir("/tmp/oopsie", 0700);
         if (WalFile_Open(&wal, WAL_PATH)) {
            WalHeader* header = (WalHeader*)wal.map;
            if (__atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 1) {
               printf("[Oopsie] Already monitoring!\n");
            }
            else {
               __atomic_store_n(&header->is_monitoring, 1, __ATOMIC_RELAXED);
               __atomic_store_n(&header->start_time, (uint64_t)time(NULL), __ATOMIC_RELAXED);
            }
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
         execlp(ushell, ushell, NULL);
         perror("execlp");
         exit(1);
      }
      else if (strcmp(argv[1], "end") == 0) {
         OopWalContext wal;
         if (WalFile_Open(&wal, WAL_PATH)) {
            WalHeader* header = (WalHeader*)wal.map;
            if (__atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 0) {
               printf("[Oopsie] Already stopped!\n");
               WalFile_Close(&wal);
               exit(0);
            }
            __atomic_store_n(&header->is_monitoring, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&header->start_time, (uint64_t)0, __ATOMIC_RELAXED);
            WalFile_Close(&wal);
            printf("[Oopsie] Monitoring paused. Type 'exit' to end.\n");
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
      __atomic_store_n(&header->start_time, val ? 0 : (uint64_t)time(NULL), __ATOMIC_RELAXED);
      WalFile_Close(&wal);
   }
}

int main(int argc, char* argv[]) {
   OopWalRecordView views[VIEW_SZ] = {0};
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

   size_t selected_index = (size_t)-1;
   while (keep_running) {
      oopsie_graphics_check_screen_resize();
      size_t fsize = 0;
      uint64_t start_time = 0;
      bool wal_is_open = WalFile_Open(&wal, WAL_PATH);
      if (wal_is_open) {
         WalHeader* header = (WalHeader*)wal.map;
         is_mon = __atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 1;
         start_time = __atomic_load_n(&header->start_time, __ATOMIC_RELAXED);
         fsize = WalFile_Parse(&wal, views, VIEW_SZ);
         if (search_box.buffer && search_box.buffer[0] != '\0') {
            size_t filtered_count = 0;
            for (size_t i = 0; i < fsize; i++) {
               char temp_path[512];
               size_t len = views[i].rec->pathlen;
               if (len >= sizeof(temp_path))
                  len = sizeof(temp_path) - 1;
               memcpy(temp_path, views[i].path, len);
               temp_path[len] = '\0';
               if (strstr(temp_path, search_box.buffer)) {
                  views[filtered_count++] = views[i];
               }
            }
            fsize = filtered_count;
         }
         if (selected_index >= fsize)
            selected_index = (size_t)-1;
      }
      size_t old_fsize = log_scrollbar.total_items;
      log_scrollbar.total_items = fsize;

      // Auto-scroll logic: if we were at the bottom, stick to the bottom when new items appear.
      if (fsize > old_fsize && log_scrollbar.current_scroll + log_scrollbar.visible_items >= old_fsize) {
         log_scrollbar.current_scroll = (fsize > log_scrollbar.visible_items) ? (fsize - log_scrollbar.visible_items) : 0;
      }
      // Clamp current_scroll to bounds
      if (log_scrollbar.current_scroll + log_scrollbar.visible_items > fsize) {
         log_scrollbar.current_scroll = (fsize > log_scrollbar.visible_items) ? (fsize - log_scrollbar.visible_items) : 0;
      }
      // Hotkey handling
      if ((!search_box.is_focused) && (oopsie_graphics_is_key_pressed('q') || oopsie_graphics_is_key_pressed('Q'))) {
         keep_running = 0;
      }
      if (!search_box.is_focused && (oopsie_graphics_is_key_pressed('s') || oopsie_graphics_is_key_pressed('S'))) {
         my_toggle_cb();
      }
      if (!search_box.is_focused && (oopsie_graphics_is_key_pressed('r') || oopsie_graphics_is_key_pressed('R'))) {
         if (wal_is_open && selected_index < fsize) {
            OopWalRecordView* view = &views[selected_index];
            if (view->rec->action == OopAction_DELETE || view->rec->action == OopAction_MODIFY) {
               char vault_path[128];
               make_vault_path(vault_path, view->rec->inode);
               char target_path[512];
               size_t len = view->rec->pathlen;
               if (len >= sizeof(target_path))
                  len = sizeof(target_path) - 1;
               memcpy(target_path, view->path, len);
               target_path[len] = '\0';
               int srcFd = open(vault_path, O_RDONLY);
               if (srcFd >= 0) {
                  int destFd = open(target_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                  if (destFd >= 0) {
                     char buf[4096];
                     ssize_t bytes;
                     while ((bytes = read(srcFd, buf, sizeof(buf))) > 0) {
                        write(destFd, buf, bytes);
                     }
                     close(destFd);
                  }
                  close(srcFd);
               }
            }
            else if (view->rec->action == OopAction_RENAME) {
               char old_path[512];
               char new_path[512];
               size_t old_len = strlen(view->path);
               if (old_len < view->rec->pathlen) {
                  strcpy(old_path, view->path);
                  strcpy(new_path, view->path + old_len + 1);
                  rename(new_path, old_path);
               }
            }
            WalFile_Purge(&wal, view);
            WalFile_Compact(&wal);
            selected_index = (size_t)-1;
            fsize--;
         }
      }
      if (!search_box.is_focused && (oopsie_graphics_is_key_pressed('c') || oopsie_graphics_is_key_pressed('C'))) {
         if (wal_is_open) {
            WalFile_Compact(&wal);
         }
      }
      if (!search_box.is_focused && fsize > 0 && (oopsie_graphics_is_key_pressed('k') || oopsie_graphics_is_key_pressed('K'))) {
         if (selected_index == (size_t)-1 || selected_index == 0) {
            selected_index = fsize - 1;
         }
         else {
            selected_index--;
         }
         if (selected_index < log_scrollbar.current_scroll) {
            log_scrollbar.current_scroll = selected_index;
         }
         else if (selected_index >= log_scrollbar.current_scroll + log_scrollbar.visible_items) {
            log_scrollbar.current_scroll = (fsize > log_scrollbar.visible_items) ? fsize - log_scrollbar.visible_items : 0;
         }
      }
      if (!search_box.is_focused && fsize > 0 && (oopsie_graphics_is_key_pressed('j') || oopsie_graphics_is_key_pressed('J'))) {
         if (selected_index == (size_t)-1 || selected_index == fsize - 1) {
            selected_index = 0;
         }
         else {
            selected_index++;
         }
         if (selected_index >= log_scrollbar.current_scroll + log_scrollbar.visible_items) {
            log_scrollbar.current_scroll = selected_index - log_scrollbar.visible_items + 1;
         }
         else if (selected_index < log_scrollbar.current_scroll) {
            log_scrollbar.current_scroll = 0;
         }
      }
      if (!search_box.is_focused && (oopsie_graphics_is_key_pressed('p') || oopsie_graphics_is_key_pressed('P'))) {
         if (wal_is_open) {
            for (size_t i = 0; i < fsize; i++) {
               WalFile_Purge(&wal, &views[i]);
            }
            WalFile_Compact(&wal);
         }
      }
      if (oopsie_graphics_is_key_pressed(TB_KEY_ARROW_UP)) {
         if (log_scrollbar.current_scroll > 0)
            log_scrollbar.current_scroll--;
      }
      if (oopsie_graphics_is_key_pressed(TB_KEY_ARROW_DOWN)) {
         if (log_scrollbar.current_scroll + log_scrollbar.visible_items < log_scrollbar.total_items)
            log_scrollbar.current_scroll++;
      }
      // 1. UPDATE UI
      oopsie_ui_update_textbox(&search_box);
      oopsie_ui_update_scrollbar(&log_scrollbar);

      if (!search_box.is_focused && oopsie_graphics_is_key_pressed('/')) {
         search_box.is_focused = 1;
         search_box.buffer[strlen(search_box.buffer) - 1] = '\0';
      }
      if (search_box.is_focused && oopsie_graphics_is_key_pressed(TB_KEY_TAB)) {
         search_box.is_focused = 0;
      }
      if (oopsie_graphics_is_mouse_button_pressed(TB_KEY_MOUSE_LEFT)) {
         point_t mpos = oopsie_graphics_get_mouse_pos();
         uint16_t sb_width = 35;
         uint16_t sb_x = ScreenWH.x > sb_width ? ScreenWH.x - sb_width : 0;
         uint16_t log_width = sb_x > 2 ? sb_x - 1 : 0;
         if (mpos.x >= 2 && mpos.x < log_width - 2 && mpos.y >= 6 && mpos.y < 6 + log_scrollbar.visible_items) {
            size_t clicked_index = log_scrollbar.current_scroll + (mpos.y - 6);
            if (clicked_index < fsize) {
               selected_index = clicked_index;
            }
         }
      }

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
         log_scrollbar.visible_items = panel_h > 6 ? panel_h - 6 : 0;
         log_scrollbar.bounds = RECT(log_width - 1, 6, 1, log_scrollbar.visible_items);
         oopsie_graphics_draw_rect_lines(log_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(2, 0), " WAL Feed ", BLUE, BLACK);
         // Search bar
         oopsie_graphics_draw_string(POINT(2, 2), "/search: ", YELLOW, BLACK);
         oopsie_ui_draw_textbox(&search_box, GRAY, DARKGRAY, WHITE);
         // Columns
         oopsie_graphics_draw_string(POINT(2, 4), "TIME                     ACTION    PATH", DARKGRAY, BLACK);
         // Mock log entry
         for (size_t i = log_scrollbar.current_scroll; i < log_scrollbar.current_scroll + log_scrollbar.visible_items && i < fsize; i++) {
            const size_t offset = 6 + i - log_scrollbar.current_scroll;
            struct tm* t = localtime((const time_t*)&views[i].rec->timestamp);
            char time[64];
            const char* action;
            strftime(time, sizeof(time), "[%d-%m-%Y %H:%M:%S]", t); // 21
            oopsie_graphics_draw_string(POINT(2, offset), time, DARKGRAY, BgColor);
            color_t action_clr = RED;
            switch (views[i].rec->action) {
            case OopAction_DELETE:
               action_clr = RED;
               action = "DELETE";
               break;
            case OopAction_CREATE:
               action_clr = GREEN;
               action = "CREATE";
               break;
            case OopAction_MODIFY:
               action_clr = YELLOW;
               action = "MODIFY";
               break;
            case OopAction_RENAME:
               action_clr = ORANGE;
               action = "RENAME";
               break;
               unreachable();
            }
            oopsie_graphics_draw_string(POINT(27, offset), action, action_clr, BgColor);

            char disp_path[50];
            if (views[i].rec->pathlen < sizeof(disp_path)) {
               memcpy(disp_path, views[i].path, views[i].rec->pathlen);
               disp_path[views[i].rec->pathlen] = '\0';
            }
            else {
               strcpy(disp_path, ".../");
               size_t rem = sizeof(disp_path) - 5;
               memcpy(disp_path + 4, views[i].path + views[i].rec->pathlen - rem, rem);
               disp_path[sizeof(disp_path) - 1] = '\0';
            }
            oopsie_graphics_draw_string(POINT(36, offset), disp_path, WHITE, BgColor);
            if (i == selected_index) {
               oopsie_graphics_invert_color_in_rect(RECT(0, offset - 1, log_width, 3));
            }
         }
         oopsie_ui_draw_scrollbar(&log_scrollbar, DARKGRAY, BgColor);
      }
      // ----- STATS PANEL -----
      if (sb_width > 0 && panel_h > 15) {
         rect_t stats_rect = RECT(sb_x, 0, sb_width, 10);
         oopsie_graphics_draw_rect_lines(stats_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 0), " System Stats ", BLUE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 2), "Shim:", WHITE, BgColor);
         if (is_mon) {
            oopsie_graphics_draw_string(POINT(sb_x + 18, 2), "ACTIVE", GREEN, BgColor);
         }
         else {
            oopsie_graphics_draw_string(POINT(sb_x + 18, 2), "INACTIVE", RED, BgColor);
         }
         oopsie_graphics_draw_string(POINT(sb_x + 2, 3), "WAL Size:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 18, 3), "16 KB", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 4), "Events:", WHITE, BgColor);
         char s[16];
         snprintf(s, 16, "%lu+", fsize);
         oopsie_graphics_draw_string(POINT(sb_x + 18, 4), s, WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 5), "Tombstones:", WHITE, BgColor);
         snprintf(s, 16, "%u", ((const WalHeader*)wal.map)->toombstone);
         oopsie_graphics_draw_string(POINT(sb_x + 18, 5), s, WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 6), "Uptime:", WHITE, BgColor);
         char uptime_str[32] = "0s";
         if (is_mon && start_time > 0) {
            uint64_t uptime = (uint64_t)time(NULL) - start_time;
            uint64_t h = uptime / 3600;
            uint64_t m = (uptime % 3600) / 60;
            uint64_t s = uptime % 60;
            if (h > 0)
               snprintf(uptime_str, sizeof(uptime_str), "%luh %lum %lus", h, m, s);
            else if (m > 0)
               snprintf(uptime_str, sizeof(uptime_str), "%lum %lus", m, s);
            else
               snprintf(uptime_str, sizeof(uptime_str), "%lus", s);
         }
         oopsie_graphics_draw_string(POINT(sb_x + 18, 6), uptime_str, WHITE, BgColor);
         // ----- HOTKEYS PANEL -----
         rect_t hk_rect = RECT(sb_x, 10, sb_width, panel_h - 14);
         color_t resotre_col = DARKGRAY;
         if (wal_is_open && selected_index < fsize)
            resotre_col = WHITE;
         oopsie_graphics_draw_rect_lines(hk_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 10), " Hotkeys ", BLUE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 12), "P:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 12), "Purge All", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 13), "C:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 13), "Compact", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 14), "R:", resotre_col, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 14), "Restore", resotre_col, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 15), "S:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 15), "Toggle Shim", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 16), "K:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 16), "Up", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 17), "J:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 17), "Down", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 18), "Q:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 18), "Quit", WHITE, BgColor);
         // ----- LOGO PANEL -----
         rect_t lg_rect = RECT(sb_x, ScreenWH.y - 6, sb_width, 5);
         oopsie_graphics_draw_rect_lines(lg_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 5), row1, PINK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 4), row2, PINK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 3), row3, PINK, BgColor);
      }
      oopsie_graphics_end_drawing();
      if (wal_is_open) {
         WalFile_Close(&wal);
      }
   }
   oopsie_graphics_end();
   return 0;
}