#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windowsHeader.h>
#elif __linux__
#include <linuxHeader.h>
#else
#error OS_NOT_SUPPORTED
#endif
FILE* WATCHLIST_FILE;

int main(void){
	return 0;
}
