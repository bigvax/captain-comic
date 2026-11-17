#include "cc.h"
#include <stdio.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, web_restore_saves, (void), {
	FS.mkdirTree('/persist');
	for (var name of ['keys.cfg', 'scores.dat']) {
		try {
			var data = localStorage.getItem('captain-comic:' + name);
			if (data !== null) FS.writeFile('/persist/' + name, data, {encoding: 'utf8'});
		} catch (error) {
			if (Module['ccStorageError']) Module['ccStorageError']();
		}
	}
});
EM_JS(int, web_persist_save, (const char *filename), {
	var path = UTF8ToString(filename);
	if (!path.startsWith('/persist/')) return 1;
	try {
		localStorage.setItem('captain-comic:' + path.slice(9), FS.readFile(path, {encoding: 'utf8'}));
		return 1;
	} catch (error) {
		if (Module['ccStorageError']) Module['ccStorageError']();
		return 0;
	}
});
#endif
#ifdef _WIN32
#include <windows.h>
#endif

/* POSIX rename replaces an existing file; the Windows CRT rename does not. */
bool replace_save_file(const char *temporary, const char *path) {
#ifdef _WIN32
	return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	if (rename(temporary, path)) return false;
#ifdef __EMSCRIPTEN__
	return web_persist_save(path) != 0;
#else
	return true;
#endif
#endif
}

