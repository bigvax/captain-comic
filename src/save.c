#include "cc.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* POSIX rename replaces an existing file; the Windows CRT rename does not. */
bool replace_save_file(const char *temporary, const char *path) {
#ifdef _WIN32
	return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(temporary, path) == 0;
#endif
}
