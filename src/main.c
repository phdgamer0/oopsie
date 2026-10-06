#define _GNU_SOURCE
#include <bits/types/timer_t.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <oopsie_graphics.h>
#include <oopsie_wal.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <termbox2.h>
#include <time.h>
#include <unistd.h>
#define VIEW_SZ (size_t)500

volatile sig_atomic_t keep_running = 1;
static const char* row1 = "░░█▀█░█▀█░█▀█░█▀▀░▀█▀░█▀▀░█░█░░";
static const char* row2 = "░░█░█░█░█░█▀▀░▀▀█░░█░░█▀▀░█░█░░";
static const char* row3 = "░░▀▀▀░▀▀▀░▀░░░▀▀▀░▀▀▀░▀▀▀░▄░▄░░";

typedef enum {
   SORT_TIME = 0,
   SORT_ACTION,
   SORT_PATH
} sort_mode_t;
static sort_mode_t g_current_sort = SORT_TIME;
static bool g_sort_descending = true;

int compare_views(const void* a, const void* b) {
   const OopWalRecordView* va = (const OopWalRecordView*)a;
   const OopWalRecordView* vb = (const OopWalRecordView*)b;
   int cmp = 0;
   switch (g_current_sort) {
   case SORT_TIME:
      if (va->rec->timestamp < vb->rec->timestamp)
         cmp = -1;
      else if (va->rec->timestamp > vb->rec->timestamp)
         cmp = 1;
      break;
   case SORT_ACTION:
      if (va->rec->action < vb->rec->action)
         cmp = -1;
      else if (va->rec->action > vb->rec->action)
         cmp = 1;
      break;
   case SORT_PATH: {
      size_t min_len = va->rec->pathlen < vb->rec->pathlen ? va->rec->pathlen : vb->rec->pathlen;
      cmp = memcmp(va->path, vb->path, min_len);
      if (cmp == 0) {
         if (va->rec->pathlen < vb->rec->pathlen)
            cmp = -1;
         else if (va->rec->pathlen > vb->rec->pathlen)
            cmp = 1;
      }
      break;
   }
   }
   if (cmp == 0 && g_current_sort != SORT_TIME) {
      if (va->rec->timestamp < vb->rec->timestamp)
         cmp = -1;
      else if (va->rec->timestamp > vb->rec->timestamp)
         cmp = 1;
   }
   return g_sort_descending ? -cmp : cmp;
}

typedef struct {
   OopWalRecordView v;
   size_t abs_idx;
} LsView;

int compare_ls_views(const void* a, const void* b) {
   const LsView* la = (const LsView*)a;
   const LsView* lb = (const LsView*)b;
   return compare_views(&la->v, &lb->v);
}

static bool blob_has_xattr(const char* blob, size_t blob_len, const char* name) {
   size_t name_len = strlen(name);
   size_t off = 0;
   while (off + 6 <= blob_len) {
      uint16_t rec_name_len = 0;
      uint32_t value_len = 0;
      memcpy(&rec_name_len, blob + off, sizeof(rec_name_len));
      off += sizeof(rec_name_len);
      memcpy(&value_len, blob + off, sizeof(value_len));
      off += sizeof(value_len);
      if (rec_name_len == 0 || off + (size_t)rec_name_len + (size_t)value_len > blob_len) {
         break;
      }
      if (rec_name_len == name_len && memcmp(blob + off, name, name_len) == 0) {
         return true;
      }
      off += (size_t)rec_name_len + (size_t)value_len;
   }
   return false;
}

static void prune_extra_xattrs(const char* target_path, const char* blob, size_t blob_len) {
   ssize_t need = llistxattr(target_path, NULL, 0);
   if (need <= 0) {
      return;
   }
   char* names = (char*)malloc((size_t)need);
   if (names == NULL) {
      return;
   }
   ssize_t got = llistxattr(target_path, names, (size_t)need);
   size_t offset = 0;
   while (got > 0 && offset < (size_t)got) {
      const char* name = names + offset;
      size_t name_len = strlen(name);
      offset += name_len + 1;
      if (!blob_has_xattr(blob, blob_len, name)) {
         (void)lremovexattr(target_path, name);
      }
   }
   free(names);
}

static bool apply_meta_blob(const char* target_path, unsigned long ino) {
   char meta_path[256];
   make_vault_meta_path(meta_path, ino);
   int fd = open(meta_path, O_RDONLY);
   if (fd < 0) {
      return false;
   }
   OopMetaBlob meta;
   bool applied = false;
   if (read(fd, &meta, sizeof(meta)) == (ssize_t)sizeof(meta) && meta.magic == METABLOB_MAGIC) {
      char* blob = NULL;
      if (meta.xattr_len > 0) {
         blob = (char*)malloc(meta.xattr_len);
         if (blob != NULL && read(fd, blob, meta.xattr_len) != (ssize_t)meta.xattr_len) {
            free(blob);
            blob = NULL;
         }
      }
      else {
         blob = (char*)malloc(1);
      }
      if (blob != NULL) {
         size_t off = 0;
         while (off + 6 <= meta.xattr_len) {
            uint16_t name_len = 0;
            uint32_t value_len = 0;
            memcpy(&name_len, blob + off, sizeof(name_len));
            off += sizeof(name_len);
            memcpy(&value_len, blob + off, sizeof(value_len));
            off += sizeof(value_len);
            if (name_len == 0 || name_len >= 256 || off + (size_t)name_len + (size_t)value_len > meta.xattr_len) {
               break;
            }
            char name[256];
            memcpy(name, blob + off, name_len);
            name[name_len] = '\0';
            off += name_len;
            (void)lsetxattr(target_path, name, blob + off, value_len, 0);
            off += value_len;
         }
         prune_extra_xattrs(target_path, blob, meta.xattr_len);
         free(blob);
      }
      (void)chmod(target_path, (mode_t)(meta.mode & 07777));
      (void)chown(target_path, (uid_t)meta.uid, (gid_t)meta.gid);
      struct timeval times[2];
      times[0].tv_sec = (time_t)meta.atime_sec;
      times[0].tv_usec = (suseconds_t)(meta.atime_nsec / 1000);
      times[1].tv_sec = (time_t)meta.mtime_sec;
      times[1].tv_usec = (suseconds_t)(meta.mtime_nsec / 1000);
      (void)utimes(target_path, times);
      applied = true;
   }
   close(fd);
   return applied;
}

bool do_restore(OopWalRecordView* view) {
   bool success = false;
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
            success = true;
         }
         close(srcFd);
      }
      if (view->rec->inode != 0 && apply_meta_blob(target_path, (unsigned long)view->rec->inode)) {
         success = true;
      }
   }
   else if (view->rec->action == OopAction_RENAME) {
      char old_path[512];
      char new_path[512];
      size_t old_len = strlen(view->path);
      if (old_len < view->rec->pathlen) {
         strcpy(old_path, view->path);
         strcpy(new_path, view->path + old_len + 1);
         if (rename(new_path, old_path) == 0) {
            success = true;
         }
      }
   }
   else if (view->rec->action == OopAction_CREATE) {
      char target_path[512];
      size_t len = view->rec->pathlen;
      if (len >= sizeof(target_path))
         len = sizeof(target_path) - 1;
      memcpy(target_path, view->path, len);
      target_path[len] = '\0';

      // Restoring a file creation means undoing it (deleting the file)
      if (unlink(target_path) == 0) {
         success = true;
      }
      else {
         if (errno == ENOENT) {
            success = true;
         }
         else if (errno == EISDIR || errno == EPERM) {
            struct stat dir_st;
            if (lstat(target_path, &dir_st) == 0 && S_ISDIR(dir_st.st_mode)) {
               if (rmdir(target_path) == 0 || errno == ENOENT) {
                  success = true;
               }
            }
         }
      }
   }
   return success;
}

bool parse_indices(const char* str, size_t* indices, size_t* num_indices) {
   char* s = strdup(str);
   char* tok = strtok(s, ",");
   *num_indices = 0;
   while (tok) {
      // trim spaces
      while (isspace(*tok))
         tok++;
      char* dash = strchr(tok, '-');
      if (dash) {
         *dash = '\0';
         char* endptr1;
         char* endptr2;
         long start = strtol(tok, &endptr1, 10);
         long end = strtol(dash + 1, &endptr2, 10);
         while (isspace(*endptr1))
            endptr1++;
         while (isspace(*endptr2))
            endptr2++;
         if (start < 0 || end < 0 || start > end || *endptr1 != '\0' || *endptr2 != '\0') {
            free(s);
            return false;
         }
         for (long i = start; i <= end; i++) {
            indices[(*num_indices)++] = (size_t)i;
         }
      }
      else {
         char* endptr;
         long val = strtol(tok, &endptr, 10);
         while (isspace(*endptr))
            endptr++;
         if (val < 0 || *endptr != '\0') {
            free(s);
            return false;
         }
         indices[(*num_indices)++] = (size_t)val;
      }
      tok = strtok(NULL, ",");
   }
   free(s);
   return true;
}

bool prompt_and_restore(OopWalContext* wal, LsView* view, bool silent) {
   if (!silent) {
      struct tm* t = localtime((const time_t*)&view->v.rec->timestamp);
      char time_str[64];
      strftime(time_str, sizeof(time_str), "[%d-%m-%Y %H:%M:%S]", t);
      const char* action_str = "UNKNOWN";
      switch (view->v.rec->action) {
      case OopAction_DELETE:
         action_str = "DELETE";
         break;
      case OopAction_CREATE:
         action_str = "CREATE";
         break;
      case OopAction_MODIFY:
         action_str = "MODIFY";
         break;
      case OopAction_RENAME:
         action_str = "RENAME";
         break;
      }
      char disp_path[1024];
      size_t plen = view->v.rec->pathlen;
      if (plen >= sizeof(disp_path))
         plen = sizeof(disp_path) - 1;
      memcpy(disp_path, view->v.path, plen);
      disp_path[plen] = '\0';
      printf("Index: %zu\nTime: %s\nAction: %s\nPath: %s\n", view->abs_idx, time_str, action_str, disp_path);
      printf("Restore this item? [y/N]: ");
      fflush(stdout);
      char answer[16];
      if (!fgets(answer, sizeof(answer), stdin))
         return false;
      if (answer[0] != 'y' && answer[0] != 'Y') {
         printf("Skipped.\n");
         return false;
      }
   }

   if (do_restore(&view->v)) {
      WalFile_Purge(wal, &view->v);
      if (!silent)
         printf("Successfully restored.\n");
      return true;
   }
   else {
      if (!silent)
         printf("Failed to restore.\n");
      return false;
   }
}

void handle_args(int argc, char* argv[]) {
   if (argc > 1) {
      if (strcmp(argv[1], "start") == 0) {
         const char* ushell = getenv("SHELL");
         if (!ushell)
            ushell = "sh";
         OopWalContext wal;
         mkdir("/tmp/oopsie", 0700);
         bool already_in_shell = (getenv("OOPSIE_SHIM_ACTIVE") != NULL);
         if (WalFile_Open(&wal, WAL_PATH)) {
            WalHeader* header = (WalHeader*)wal.map;
            if (__atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 1) {
               printf("[Oopsie] Already monitoring!\n");
               WalFile_Close(&wal);
               if (already_in_shell)
                  exit(0);
            }
            else {
               __atomic_store_n(&header->is_monitoring, 1, __ATOMIC_RELAXED);
               __atomic_store_n(&header->start_time, (uint64_t)time(NULL), __ATOMIC_RELAXED);
               WalFile_Close(&wal);
               if (already_in_shell) {
                  printf("[Oopsie] Monitoring resumed.\n");
                  exit(0);
               }
            }
         }
         if (!already_in_shell) {
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
            setenv("OOPSIE_SHIM_ACTIVE", "1", 1);
            printf("[Oopsie] Starting monitored shell... Type 'exit' to end.\n");
            execlp(ushell, ushell, NULL);
            perror("execlp");
            exit(1);
         }
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
      else if (strcmp(argv[1], "ls") == 0) {
         // flags:
         // -c --colored,
         // -s --sort [action|path|time] (desc),
         // -S --Sort [action|path|time] (asc),
         // -l --limit [N] (shows N results from the top)
         // -L --Limit [N] (shows N results from bottom)
         // -n --numbered (shows indexes for piping to awk) (active by default)
         // -N --Numbered (relative to the shown file) (use this if you wanna purge or restore by index)
         // -a --action (does not show action)
         // -t --time (does not show time)
         // -p --path (does not show path)
         // -o --output (faster redirection than > or >>)

         bool opt_colored = false;
         bool opt_num_abs = true;
         bool opt_num_rel = false;
         bool opt_show_action = true;
         bool opt_show_time = true;
         bool opt_show_path = true;
         size_t opt_limit_top = 0;
         size_t opt_limit_bottom = 0;
         const char* opt_output = NULL;

         for (int i = 2; i < argc; i++) {
            if (argv[i][0] == '-') {
               if (argv[i][1] == '-') {
                  if (strcmp(argv[i], "--colored") == 0)
                     opt_colored = true;
                  else if (strcmp(argv[i], "--numbered") == 0)
                     opt_num_abs = true;
                  else if (strcmp(argv[i], "--Numbered") == 0)
                     opt_num_rel = true;
                  else if (strcmp(argv[i], "--action") == 0)
                     opt_show_action = false;
                  else if (strcmp(argv[i], "--time") == 0)
                     opt_show_time = false;
                  else if (strcmp(argv[i], "--path") == 0)
                     opt_show_path = false;
                  else if (strcmp(argv[i], "--sort") == 0 || strcmp(argv[i], "--Sort") == 0) {
                     bool asc = (strcmp(argv[i], "--Sort") == 0);
                     i++;
                     if (i >= argc) {
                        printf("Invalid option\n");
                        exit(1);
                     }
                     if (strcmp(argv[i], "time") == 0)
                        g_current_sort = SORT_TIME;
                     else if (strcmp(argv[i], "action") == 0)
                        g_current_sort = SORT_ACTION;
                     else if (strcmp(argv[i], "path") == 0)
                        g_current_sort = SORT_PATH;
                     else {
                        printf("Invalid flag or option\n");
                        exit(1);
                     }
                     g_sort_descending = !asc;
                  }
                  else if (strcmp(argv[i], "--limit") == 0 || strcmp(argv[i], "--Limit") == 0) {
                     bool top = (strcmp(argv[i], "--limit") == 0);
                     i++;
                     if (i >= argc) {
                        printf("Invalid option\n");
                        exit(1);
                     }
                     if (top)
                        opt_limit_top = (size_t)atoi(argv[i]);
                     else
                        opt_limit_bottom = (size_t)atoi(argv[i]);
                  }
                  else if (strcmp(argv[i], "--output") == 0) {
                     i++;
                     if (i >= argc) {
                        printf("Invalid option\n");
                        exit(1);
                     }
                     opt_output = argv[i];
                  }
                  else {
                     printf("Invalid flag or option\n");
                     exit(1);
                  }
               }
               else {
                  for (size_t j = 1; argv[i][j] != '\0'; j++) {
                     char c = argv[i][j];
                     if (c == 'c')
                        opt_colored = true;
                     else if (c == 'n')
                        opt_num_abs = true;
                     else if (c == 'N') {
                        opt_num_rel = true;
                        opt_num_abs = false;
                     }
                     else if (c == 'a')
                        opt_show_action = false;
                     else if (c == 't')
                        opt_show_time = false;
                     else if (c == 'p')
                        opt_show_path = false;
                     else if (c == 's' || c == 'S' || c == 'l' || c == 'L' || c == 'o') {
                        i++;
                        if (i >= argc) {
                           printf("Invalid option\n");
                           exit(1);
                        }
                        if (c == 's' || c == 'S') {
                           if (strcmp(argv[i], "time") == 0)
                              g_current_sort = SORT_TIME;
                           else if (strcmp(argv[i], "action") == 0)
                              g_current_sort = SORT_ACTION;
                           else if (strcmp(argv[i], "path") == 0)
                              g_current_sort = SORT_PATH;
                           else {
                              printf("Invalid flag or option\n");
                              exit(1);
                           }
                           g_sort_descending = (c == 's');
                        }
                        else if (c == 'l') {
                           opt_limit_top = (size_t)atoi(argv[i]);
                        }
                        else if (c == 'L') {
                           opt_limit_bottom = (size_t)atoi(argv[i]);
                        }
                        else if (c == 'o') {
                           opt_output = argv[i];
                        }
                        break;
                     }
                     else {
                        printf("Invalid flag or option\n");
                        exit(1);
                     }
                  }
               }
            }
            else {
               printf("Invalid flag or option\n");
               exit(1);
            }
         }

         OopWalContext wal;
         if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) {
            printf("[Oopsie] Failed to open WAL.\n");
            exit(1);
         }

         OopWalRecordView temp_views[VIEW_SZ];
         size_t fsize = WalFile_Parse(&wal, temp_views, VIEW_SZ);

         LsView ls_views[VIEW_SZ];
         for (size_t i = 0; i < fsize; i++) {
            ls_views[i].v = temp_views[i];
            ls_views[i].abs_idx = i;
         }

         if (fsize > 0) {
            qsort(ls_views, fsize, sizeof(LsView), compare_ls_views);
         }

         FILE* out = stdout;
         if (opt_output) {
            out = fopen(opt_output, "w");
            if (!out) {
               printf("Failed to open output file: %s\n", opt_output);
               exit(1);
            }
            opt_colored = false;
         }

         size_t start1 = 0, end1 = fsize;
         size_t start2 = fsize, end2 = fsize;
         if (opt_limit_top > 0 || opt_limit_bottom > 0) {
            if (opt_limit_top > 0)
               end1 = (opt_limit_top < fsize) ? opt_limit_top : fsize;
            else
               end1 = 0;
            if (opt_limit_bottom > 0)
               start2 = (fsize > opt_limit_bottom) ? fsize - opt_limit_bottom : 0;
            if (end1 > start2)
               start2 = end1;
         }

         size_t rel_idx = 1;
         for (size_t i = 0; i < fsize; i++) {
            if (!((i >= start1 && i < end1) || (i >= start2 && i < end2)))
               continue;

            if (opt_num_abs)
               fprintf(out, "%-4zu ", ls_views[i].abs_idx);
            if (opt_num_rel)
               fprintf(out, "%-4zu ", rel_idx++);

            if (opt_show_time) {
               struct tm* t = localtime((const time_t*)&ls_views[i].v.rec->timestamp);
               char time_str[64];
               strftime(time_str, sizeof(time_str), "[%d-%m-%Y %H:%M:%S]", t);
               fprintf(out, "%s ", time_str);
            }
            if (opt_show_action) {
               const char* action_str = "UNKNOWN";
               const char* color_code = "";
               if (opt_colored) {
                  switch (ls_views[i].v.rec->action) {
                  case OopAction_DELETE:
                     color_code = "\x1b[38;2;230;41;55m";
                     break;
                  case OopAction_CREATE:
                     color_code = "\x1b[38;2;0;228;48m";
                     break;
                  case OopAction_MODIFY:
                     color_code = "\x1b[38;2;253;249;0m";
                     break;
                  case OopAction_RENAME:
                     color_code = "\x1b[38;2;255;161;0m";
                     break;
                  }
               }
               switch (ls_views[i].v.rec->action) {
               case OopAction_DELETE:
                  action_str = "DELETE";
                  break;
               case OopAction_CREATE:
                  action_str = "CREATE";
                  break;
               case OopAction_MODIFY:
                  action_str = "MODIFY";
                  break;
               case OopAction_RENAME:
                  action_str = "RENAME";
                  break;
               }
               if (opt_colored)
                  fprintf(out, "%s%-7s\x1b[0m ", color_code, action_str);
               else
                  fprintf(out, "%-7s ", action_str);
            }
            if (opt_show_path) {
               char disp_path[1024];
               size_t plen = ls_views[i].v.rec->pathlen;
               if (plen >= sizeof(disp_path))
                  plen = sizeof(disp_path) - 1;
               memcpy(disp_path, ls_views[i].v.path, plen);
               disp_path[plen] = '\0';
               fprintf(out, "%s", disp_path);
            }
            fprintf(out, "\n");
         }

         if (out != stdout)
            fclose(out);
         WalFile_Close(&wal);
         exit(0);
      }
      else if (strcmp(argv[1], "restore") == 0) {
         bool opt_silent = false;
         char* opt_number = NULL;
         char* opt_path = NULL;

         for (int i = 2; i < argc; i++) {
            if (argv[i][0] == '-') {
               if (argv[i][1] == '-') {
                  if (strcmp(argv[i], "--silent") == 0)
                     opt_silent = true;
                  else if (strcmp(argv[i], "--number") == 0) {
                     i++;
                     if (i >= argc) {
                        printf("Invalid flag or option\n");
                        exit(1);
                     }
                     opt_number = argv[i];
                  }
                  else if (strcmp(argv[i], "--path") == 0) {
                     i++;
                     if (i >= argc) {
                        printf("Invalid flag or option\n");
                        exit(1);
                     }
                     opt_path = argv[i];
                  }
                  else {
                     printf("Invalid flag or option\n");
                     exit(1);
                  }
               }
               else {
                  for (size_t j = 1; argv[i][j] != '\0'; j++) {
                     char c = argv[i][j];
                     if (c == 's')
                        opt_silent = true;
                     else if (c == 'n' || c == 'p') {
                        i++;
                        if (i >= argc) {
                           printf("Invalid flag or option\n");
                           exit(1);
                        }
                        if (c == 'n')
                           opt_number = argv[i];
                        else if (c == 'p')
                           opt_path = argv[i];
                        break;
                     }
                     else {
                        printf("Invalid flag or option\n");
                        exit(1);
                     }
                  }
               }
            }
            else {
               printf("Invalid flag or option\n");
               exit(1);
            }
         }

         OopWalContext wal;
         if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) {
            printf("[Oopsie] Failed to open WAL.\n");
            exit(1);
         }

         // Pause monitoring so our restorations aren't intercepted by shim!
         WalHeader* header = (WalHeader*)wal.map;
         uint32_t was_monitoring = __atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED);
         if (was_monitoring) {
            __atomic_store_n(&header->is_monitoring, 0, __ATOMIC_RELAXED);
         }

         OopWalRecordView temp_views[VIEW_SZ];
         size_t fsize = WalFile_Parse(&wal, temp_views, VIEW_SZ);

         LsView ls_views[VIEW_SZ];
         for (size_t i = 0; i < fsize; i++) {
            ls_views[i].v = temp_views[i];
            ls_views[i].abs_idx = i;
         }

         bool did_restore = false;
         bool any_failed = false;

         if (opt_path) {
            LsView matched[VIEW_SZ];
            size_t m_count = 0;
            for (size_t i = 0; i < fsize; i++) {
               char disp_path[1024];
               size_t plen = ls_views[i].v.rec->pathlen;
               if (plen >= sizeof(disp_path))
                  plen = sizeof(disp_path) - 1;
               memcpy(disp_path, ls_views[i].v.path, plen);
               disp_path[plen] = '\0';
               if (fnmatch(opt_path, disp_path, 0) == 0) {
                  matched[m_count++] = ls_views[i];
               }
            }
            if (m_count == 0) {
               printf("No matches found for path %s\n", opt_path);
               WalFile_Close(&wal);
               exit(1);
            }
            g_current_sort = SORT_TIME;
            g_sort_descending = true;
            qsort(matched, m_count, sizeof(LsView), compare_ls_views);

            if (opt_silent) {
               if (do_restore(&matched[0].v)) {
                  WalFile_Purge(&wal, &matched[0].v);
                  did_restore = true;
               }
               else {
                  any_failed = true;
               }
            }
            else {
               for (size_t i = 0; i < m_count; i++) {
                  struct tm* t = localtime((const time_t*)&matched[i].v.rec->timestamp);
                  char time_str[64];
                  strftime(time_str, sizeof(time_str), "[%d-%m-%Y %H:%M:%S]", t);
                  char disp_path[1024];
                  size_t plen = matched[i].v.rec->pathlen;
                  if (plen >= sizeof(disp_path))
                     plen = sizeof(disp_path) - 1;
                  memcpy(disp_path, matched[i].v.path, plen);
                  disp_path[plen] = '\0';
                  printf("%-4zu %s %s\n", matched[i].abs_idx, time_str, disp_path);
               }
               printf("Enter indices to restore (e.g. 1,3-5,7): ");
               fflush(stdout);
               char input_buf[256];
               if (fgets(input_buf, sizeof(input_buf), stdin)) {
                  input_buf[strcspn(input_buf, "\n")] = '\0';
                  size_t indices[1024];
                  size_t num_indices = 0;
                  if (!parse_indices(input_buf, indices, &num_indices)) {
                     printf("Invalid indices format\n");
                     WalFile_Close(&wal);
                     exit(1);
                  }
                  for (size_t i = 0; i < num_indices; i++) {
                     size_t idx = indices[i];
                     if (idx >= fsize) {
                        printf("Index %zu out of bounds\n", idx);
                        WalFile_Close(&wal);
                        exit(1);
                     }
                     if (prompt_and_restore(&wal, &ls_views[idx], false))
                        did_restore = true;
                  }
               }
            }
         }
         else if (opt_number) {
            size_t indices[1024];
            size_t num_indices = 0;
            if (!parse_indices(opt_number, indices, &num_indices)) {
               printf("Invalid flag or option\n");
               WalFile_Close(&wal);
               exit(1);
            }
            for (size_t i = 0; i < num_indices; i++) {
               size_t idx = indices[i];
               if (idx >= fsize) {
                  printf("Index %zu out of bounds\n", idx);
                  WalFile_Close(&wal);
                  exit(1);
               }
               bool res = prompt_and_restore(&wal, &ls_views[idx], opt_silent);
               if (res)
                  did_restore = true;
               else if (opt_silent)
                  any_failed = true;
            }
         }
         else {
            if (fsize == 0) {
               printf("No events to restore.\n");
               WalFile_Close(&wal);
               exit(1);
            }
            g_current_sort = SORT_TIME;
            g_sort_descending = true;
            qsort(ls_views, fsize, sizeof(LsView), compare_ls_views);

            bool res = prompt_and_restore(&wal, &ls_views[0], opt_silent);
            if (res)
               did_restore = true;
            else if (opt_silent)
               any_failed = true;
         }

         if (did_restore) {
            WalFile_Compact(&wal);
         }
         if (was_monitoring) {
            __atomic_store_n(&header->is_monitoring, 1, __ATOMIC_RELAXED);
         }
         WalFile_Close(&wal);
         if (opt_silent && any_failed)
            exit(1);
         exit(0);
      }
      else {
         printf("Unknown command: %s\n", argv[1]);
         printf("Usage: %s [start|end|uninstall]\n", argv[0]);
         exit(1);
      }
   }
}

typedef struct {
   int32_t sw;
   int32_t sh;
   int32_t fsize;
   int32_t views_changed;
   int32_t sel;
   int32_t scroll;
   int32_t vis;
   int32_t sort_mode;
   int32_t desc;
   int32_t is_mon;
   int32_t focused;
   int32_t cur_pos;
   int32_t scroll_off;
   int32_t ev_type;
   int32_t ev_x;
   int32_t ev_y;
   uint64_t start_time;
   uint64_t clock_sec;
   uint64_t search_hash;
} draw_sig_t;

static uint64_t hash_search(const char* str) {
   uint64_t h = 1469598103934665603ULL;
   while (str && *str) {
      h = (h ^ (uint64_t)(unsigned char)*str++) * 1099511628211ULL;
   }
   return h;
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
   draw_sig_t last_sig;
   memset(&last_sig, 0, sizeof(last_sig));
   last_sig.clock_sec = (uint64_t)-1;
   OopWalRecordView raw_views[VIEW_SZ];
   OopWalRecordView sorted_views[VIEW_SZ];
   size_t sorted_n = 0;
   sort_mode_t sorted_mode = SORT_TIME;
   bool sorted_desc = true;
   bool sorted_valid = false;
   handle_args(argc, argv);
   oopsie_graphics_init();
   oopsie_graphics_set_target_fps(60);
   oopsie_graphics_set_window_name("Oopsie TUI");
   oopsie_graphics_set_bg_color(BLACK);
   // Check initial state for toggle
   bool is_mon = true;
   OopWalContext wal;
   bool wal_is_open = WalFile_Open(&wal, WAL_PATH);
   if (wal_is_open) {
      WalHeader* header = (WalHeader*)wal.map;
      is_mon = __atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 1;
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
      bool views_changed = true;
      uint64_t start_time = 0;
      if (!wal_is_open) {
         wal_is_open = WalFile_Open(&wal, WAL_PATH);
      }
      if (wal_is_open) {
         WalHeader* header = (WalHeader*)wal.map;
         is_mon = __atomic_load_n(&header->is_monitoring, __ATOMIC_RELAXED) == 1;
         start_time = __atomic_load_n(&header->start_time, __ATOMIC_RELAXED);
         fsize = WalFile_Parse(&wal, raw_views, VIEW_SZ);
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
                  raw_views[filtered_count++] = raw_views[i];
               }
            }
            fsize = filtered_count;
         }
         if (selected_index >= fsize)
            selected_index = (size_t)-1;
         if (!sorted_valid || fsize != sorted_n || g_current_sort != sorted_mode ||
             g_sort_descending != sorted_desc ||
             memcmp(raw_views, sorted_views, fsize * sizeof(OopWalRecordView)) != 0) {
            if (fsize > 0) {
               qsort(raw_views, fsize, sizeof(OopWalRecordView), compare_views);
            }
            memcpy(sorted_views, raw_views, fsize * sizeof(OopWalRecordView));
            sorted_n = fsize;
            sorted_mode = g_current_sort;
            sorted_desc = g_sort_descending;
            sorted_valid = true;
            views_changed = true;
         }
         else {
            views_changed = false;
         }
         memcpy(views, sorted_views, fsize * sizeof(OopWalRecordView));
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
            if (do_restore(view)) {
               // Success
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
      if (!search_box.is_focused && (oopsie_graphics_is_key_pressed('l') || oopsie_graphics_is_key_pressed('L'))) {
         g_current_sort = (sort_mode_t)((g_current_sort + 1) % 3);
      }
      if (!search_box.is_focused && (oopsie_graphics_is_key_pressed('h') || oopsie_graphics_is_key_pressed('H'))) {
         g_current_sort = (sort_mode_t)((g_current_sort - 1) % 3);
      }
      if (!search_box.is_focused && (oopsie_graphics_is_key_pressed('t') || oopsie_graphics_is_key_pressed('T'))) {
         g_sort_descending = !g_sort_descending;
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
      }
      if (search_box.is_focused && oopsie_graphics_is_key_pressed(TB_KEY_TAB)) {
         search_box.is_focused = 0;
      }
      if (oopsie_graphics_is_mouse_button_pressed(TB_KEY_MOUSE_LEFT)) {
         point_t mpos = oopsie_graphics_get_mouse_pos();
         uint16_t sb_width = 35;
         uint16_t sb_x = ScreenWH.x > sb_width ? ScreenWH.x - sb_width : 0;
         uint16_t log_width = sb_x > 2 ? sb_x - 1 : 0;
         if (oopsie_graphics_is_mouse_in_rect(RECT(2, 4, 10, 1))) {
            if (g_current_sort == SORT_TIME)
               g_sort_descending = !g_sort_descending;
            else {
               g_current_sort = SORT_TIME;
               g_sort_descending = true;
            }
         }
         else if (oopsie_graphics_is_mouse_in_rect(RECT(27, 4, 8, 1))) {
            if (g_current_sort == SORT_ACTION)
               g_sort_descending = !g_sort_descending;
            else {
               g_current_sort = SORT_ACTION;
               g_sort_descending = true;
            }
         }
         else if (oopsie_graphics_is_mouse_in_rect(RECT(37, 4, 6, 1))) {
            if (g_current_sort == SORT_PATH)
               g_sort_descending = !g_sort_descending;
            else {
               g_current_sort = SORT_PATH;
               g_sort_descending = true;
            }
         }
         else if (oopsie_graphics_is_mouse_in_rect(RECT(2, 6, log_width - 4, log_scrollbar.visible_items))) {
            size_t clicked_index = log_scrollbar.current_scroll + (mpos.y - 6);
            if (clicked_index < fsize) {
               selected_index = clicked_index;
            }
         }
      }

      // 2. DRAW
      draw_sig_t sig;
      memset(&sig, 0, sizeof(sig));
      sig.sw = (int32_t)ScreenWH.x;
      sig.sh = (int32_t)ScreenWH.y;
      sig.fsize = (int32_t)fsize;
      sig.views_changed = views_changed ? 1 : 0;
      sig.sel = (int32_t)selected_index;
      sig.scroll = (int32_t)log_scrollbar.current_scroll;
      sig.vis = (int32_t)log_scrollbar.visible_items;
      sig.sort_mode = (int32_t)g_current_sort;
      sig.desc = g_sort_descending ? 1 : 0;
      sig.is_mon = is_mon ? 1 : 0;
      sig.focused = search_box.is_focused ? 1 : 0;
      sig.cur_pos = (int32_t)search_box.cursor_pos;
      sig.scroll_off = (int32_t)search_box.scroll_offset;
      sig.ev_type = (int32_t)oopsie_graphics_event_type();
      sig.ev_x = (int32_t)oopsie_graphics_event_x();
      sig.ev_y = (int32_t)oopsie_graphics_event_y();
      sig.start_time = start_time;
      sig.clock_sec = (uint64_t)time(NULL);
      sig.search_hash = hash_search(search_box.buffer);
      if (memcmp(&sig, &last_sig, sizeof(sig)) == 0) {
         oopsie_graphics_skip_frame();
         continue;
      }
      last_sig = sig;
      oopsie_graphics_begin_drawing();
      oopsie_graphics_set_bg_color(BLACK);
      time_t now = (time_t)sig.clock_sec;
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
         char time_hdr[32] = "TIME";
         char action_hdr[32] = "ACTION";
         char path_hdr[32] = "PATH";
         if (g_current_sort == SORT_TIME)
            strcat(time_hdr, g_sort_descending ? " ↓" : " ↑");
         if (g_current_sort == SORT_ACTION)
            strcat(action_hdr, g_sort_descending ? " ↓" : " ↑");
         if (g_current_sort == SORT_PATH)
            strcat(path_hdr, g_sort_descending ? " ↓" : " ↑");
         color_t time_clr = (g_current_sort == SORT_TIME) ? WHITE : DARKGRAY;
         color_t action_clr = (g_current_sort == SORT_ACTION) ? WHITE : DARKGRAY;
         color_t path_clr = (g_current_sort == SORT_PATH) ? WHITE : DARKGRAY;
         oopsie_graphics_draw_string(POINT(2, 4), time_hdr, time_clr, BLACK);
         oopsie_graphics_draw_string(POINT(27, 4), action_hdr, action_clr, BLACK);
         oopsie_graphics_draw_string(POINT(37, 4), path_hdr, path_clr, BLACK);
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
         snprintf(s, 16, "%u", wal_is_open ? ((const WalHeader*)wal.map)->toombstone : 0);
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
         oopsie_graphics_draw_string(POINT(sb_x + 2, 18), "H:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 18), "Left", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 19), "L:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 19), "Right", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 20), "T:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 20), "Toggle", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, 21), "Q:", WHITE, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 6, 21), "Quit", WHITE, BgColor);
         // ----- LOGO PANEL -----
         rect_t lg_rect = RECT(sb_x, ScreenWH.y - 6, sb_width, 5);
         oopsie_graphics_draw_rect_lines(lg_rect, GRAY, LIGHT_POINTY);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 5), row1, PINK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 4), row2, PINK, BgColor);
         oopsie_graphics_draw_string(POINT(sb_x + 2, ScreenWH.y - 3), row3, PINK, BgColor);
      }
      oopsie_graphics_end_drawing();
   }
   if (wal_is_open) {
      WalFile_Close(&wal);
   }
   oopsie_graphics_end();
   return 0;
}
