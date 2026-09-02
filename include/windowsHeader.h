#pragma once
#ifndef WINDOWS_HEADER_H
#define WINDOWS_HEADER_H

#if defined(_WIN32) || defined(_WIN64)

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static bool WOOPS_add_file_to_watchlist (FILE* f);   // Adds the file f to the watchlist
static bool WOOPS_del_file_from_watchlist (FILE* f); // Rrmoves the file f from the watchlist

#endif
#endif