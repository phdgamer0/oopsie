#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <windows.h>

static bool WOOPS_add_file_to_watchlist(FILE* f); 	// Adds the file f to the watchlist
static bool WOOPS_del_file_from_watchlist(FILE* f);	// Rrmoves the file f from the watchlist

