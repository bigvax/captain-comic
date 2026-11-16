#include "cc.h"
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR_DATA(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR_DATA(p) mkdir(p, 0755)
#endif

#ifdef _WIN32
#define PLATFORM_NAME "Windows"
#define EXE_NAME "captain-comic.exe"
#define PREF_ORG "CaptainComicWin"
#else
#define PLATFORM_NAME "Linux"
#define EXE_NAME "captain-comic"
#define PREF_ORG "CaptainComicLinux"
#endif


#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

static void resolve_save_paths(Resources *r) {
	if (!r) return;
	char *base = SDL_GetBasePath();
	bool portable_ok = false;
	if (base) {
		char data_dir[4096], probe[4096];
		snprintf(data_dir, sizeof data_dir, "%sdata", base);
		MKDIR_DATA(data_dir);
		snprintf(probe, sizeof probe, "%.4070s/.cc_write_test", data_dir);
		FILE *pf = fopen(probe, "wb");
		if (pf) {
			fclose(pf);
			remove(probe);
			if (snprintf(r->config_path, sizeof r->config_path, "%s/keys.cfg", data_dir) >= (int)sizeof r->config_path)
				*r->config_path = '\0';
			if (snprintf(r->score_path, sizeof r->score_path, "%s/scores.dat", data_dir) >= (int)sizeof r->score_path)
				*r->score_path = '\0';
			portable_ok = true;
		}
		SDL_free(base);
	}
	if (!portable_ok) {
		char *pref = SDL_GetPrefPath(PREF_ORG, "CaptainComic");
		if (pref) {
			if (snprintf(r->config_path, sizeof r->config_path, "%skeys.cfg", pref) >= (int)sizeof r->config_path)
				*r->config_path = '\0';
			if (snprintf(r->score_path, sizeof r->score_path, "%sscores.dat", pref) >= (int)sizeof r->score_path)
				*r->score_path = '\0';
			SDL_free(pref);
		}
	}
}

static void usage(void) {
	puts("Captain Comic " PLATFORM_NAME " " VERSION
	     " (нативная версия C / GCC)\nЗапуск: " EXE_NAME " [--assets DIR] [--headless] [--frames N]\n                     [--area forest|lake|space|base|cave|shed|comp|castle]\n                     [--segment 0|1|2] [--screenshot FILE.bmp] [--self-test]\n                     [--help-screen 1|2|3|4|5|6] [--intro-page 1|2|3] [--settings-screen 1|2|3|4] [--ending-screen] [--scores-screen] [--version]");
}

static bool parse_number(const char *text, int low, int high, int *out) {
	char *end = NULL;
	errno = 0;
	long value = strtol(text, &end, 10);
	if (errno || end == text || *end || value < low || value > high)
		return false;
	*out = (int)value;
	return true;
}

static void sync_text_input(const Game *g, bool *active) {
	bool wanted = game_text_input(g);
	if (*active == wanted)
		return;
	*active = wanted;
	if (wanted)
		SDL_StartTextInput();
	else
		SDL_StopTextInput();
}

#ifdef _WIN32
/* SDL2main calls SDL before SDL_main, so set the DLL path in CRT startup. */
static void configure_dll_path(void) __attribute__((constructor));
static void configure_dll_path(void) {
	wchar_t path[32768];
	DWORD length = GetModuleFileNameW(NULL, path, sizeof path / sizeof path[0]);
	if (!length || length >= sizeof path / sizeof path[0] - 5) return;
	wchar_t *slash = wcsrchr(path, L'\\');
	if (!slash) return;
	wcscpy(slash + 1, L"data");
	SetDllDirectoryW(path);
}
#endif

int main(int argc, char **argv) {
	bool headless = false, testing = false, ending_screen = false, scores_screen = false;
	int frames = 0, start_area = -1, segment = 0, help_screen = 0, intro_page = 0,
	    settings_screen = 0;
	const char *assets = NULL, *screenshot = NULL;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--headless") == 0) headless = true;
		else if (strcmp(arg, "--scores-screen") == 0) scores_screen = true;
		else if (strcmp(arg, "--ending-screen") == 0) ending_screen = true;
		else if (strcmp(arg, "--self-test") == 0) { testing = true; headless = true; }
		else if (strcmp(arg, "--version") == 0) {
			puts("Captain Comic " PLATFORM_NAME " " VERSION " / нативная версия C11"); return 0;
		} else if (strcmp(arg, "--help") == 0) { usage(); return 0; }
		else if (i + 1 < argc && strcmp(arg, "--assets") == 0) assets = argv[++i];
		else if (i + 1 < argc && strcmp(arg, "--screenshot") == 0) screenshot = argv[++i];
		else if (i + 1 < argc && strcmp(arg, "--frames") == 0) {
			if (!parse_number(argv[++i], 1, INT_MAX, &frames)) {
				fail_message("--frames: требуется положительное число"); return 2;
			}
		} else if (i + 1 < argc && strcmp(arg, "--segment") == 0) {
			if (!parse_number(argv[++i], 0, 2, &segment)) {
				fail_message("--segment: допустимы 0, 1 или 2"); return 2;
			}
		} else if (i + 1 < argc && strcmp(arg, "--help-screen") == 0) {
			if (!parse_number(argv[++i], 1, 6, &help_screen)) {
				fail_message("--help-screen: допустимы 1–6"); return 2;
			}
		} else if (i + 1 < argc && strcmp(arg, "--intro-page") == 0) {
			if (!parse_number(argv[++i], 1, 3, &intro_page)) {
				fail_message("--intro-page: допустимы 1–3"); return 2;
			}
		} else if (i + 1 < argc && strcmp(arg, "--settings-screen") == 0) {
			if (!parse_number(argv[++i], 1, SETTINGS_PAGES, &settings_screen)) {
				fail_message("--settings-screen: допустимы 1–4"); return 2;
			}
		} else if (i + 1 < argc && strcmp(arg, "--area") == 0) {
			const char *name = argv[++i]; start_area = -1;
			for (int a = 0; a < 8; a++) if (strcmp(name, AREA_FILES[a]) == 0) start_area = a;
			if (start_area < 0) { fail_message("Неизвестная локация: %s", name); return 2; }
		} else { fail_message("Неизвестный или неполный параметр: %s", arg); usage(); return 2; }
	}

	if (headless) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
		fail_message("SDL: %s", SDL_GetError()); return 1;
	}
	if (TTF_Init() != 0) { fail_message("SDL_ttf: %s", TTF_GetError()); SDL_Quit(); return 1; }
	int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
	if ((IMG_Init(img_flags) & img_flags) != img_flags) {
		fail_message("SDL_image: %s", IMG_GetError());
		TTF_Quit();
		SDL_Quit();
		return 1;
	}
	
	Resources *r = calloc(1, sizeof *r);
	if (!r) { IMG_Quit(); TTF_Quit(); SDL_Quit(); return 1; }

	if (!headless) {
		if (!sound_init(r)) fail_message("Звук недоступен: %s", SDL_GetError());
	}

	r->window = SDL_CreateWindow("Captain Comic " PLATFORM_NAME " " VERSION " - C / GCC",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 770,
		SDL_WINDOW_RESIZABLE | (headless ? SDL_WINDOW_HIDDEN : 0));
	if (r->window) r->renderer = SDL_CreateRenderer(r->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	if (!r->renderer && r->window) r->renderer = SDL_CreateRenderer(r->window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
	if (r->renderer) r->canvas = SDL_CreateTexture(r->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, WIDTH, HEIGHT);
	if (!r->window || !r->renderer || !r->canvas) {
		fail_message("Ошибка окна или отрисовки: %s", SDL_GetError());
		resources_shutdown(r); free(r); IMG_Quit(); TTF_Quit(); SDL_Quit(); return 1;
	}

	Game game;
	memset(&game, 0, sizeof game);

	if (!headless) {
		resolve_save_paths(r);
		if (r->score_path[0]) (void)scores_load(&r->scores, r->score_path);
	}

	if (!resources_load(r, assets)) {
		resources_shutdown(r); free(r); IMG_Quit(); TTF_Quit(); SDL_Quit(); return 1;
	}
	if (testing) {
		int result = self_test(r);
		resources_shutdown(r); free(r); IMG_Quit(); TTF_Quit(); SDL_Quit(); return result;
	}

	/* Запускаем сброс игры ПОСЛЕ загрузки всех ресурсов и world.json */
	game_reset(&game, r);
	
	if (start_area >= 0) game_explore(&game, start_area, segment);
	if (headless) game.title = false;
	if (intro_page) { game.title = true; game.intro_page = intro_page - 1; }
	if (help_screen) { game.help_open = true; game.help_page = help_screen - 1; }
	if (settings_screen) { game.location_menu = true; game.settings_page = settings_screen - 1; }
	if (ending_screen) { game.title = false; game.spawn_time = 0; game.treasures = 7; game.won = true; }
	if (scores_screen) game_scores_open(&game, false);

	bool running = true, fullscreen = false, text_input = false;
	int count = 0, exit_status = 0;
	SDL_Event event;
	bool pending_event = false;
	Uint8 pressed[SDL_NUM_SCANCODES] = {0};
	double accumulator = 0.;
	Uint64 previous = SDL_GetPerformanceCounter();
	double frequency = (double)SDL_GetPerformanceFrequency();

	while (running) {
		Uint64 now = SDL_GetPerformanceCounter();
		double elapsed = headless ? 1. / 60. : (double)(now - previous) / frequency;
		previous = now; if (elapsed > .1) elapsed = .1;
		sync_text_input(&game, &text_input);

		while (pending_event || SDL_PollEvent(&event)) {
			pending_event = false;
			if (event.type == SDL_QUIT) running = false;
			else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !game.title) {
				game.paused = true; memset(pressed, 0, sizeof pressed);
			} else if (event.type == SDL_TEXTINPUT) {
				if (game.location_menu && game.settings_page == 3)
					score_edit_text(&game, event.text.text);
				else
					game_score_text(&game, event.text.text);
			} else if (event.type == SDL_KEYDOWN) {
				if (!event.key.repeat && event.key.keysym.scancode > SDL_SCANCODE_UNKNOWN && event.key.keysym.scancode < SDL_NUM_SCANCODES)
					pressed[event.key.keysym.scancode] = 1;
				int action = game_key_event(&game, &event.key);
				sync_text_input(&game, &text_input);
				if (action == 1) { running = false; break; }
				if (action == 2) {
					fullscreen = !fullscreen;
					if (SDL_SetWindowFullscreen(r->window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
						fail_message("Ошибка полноэкранного режима: %s", SDL_GetError()); fullscreen = !fullscreen;
					}
				}
			}
		}
		int key_count;
		const Uint8 *keys = SDL_GetKeyboardState(&key_count);
		Controls controls = bindings_controls(&r->bindings, keys, key_count);
		accumulator += elapsed;
		while (accumulator >= STEP) {
			Controls taps = bindings_controls(&r->bindings, pressed, SDL_NUM_SCANCODES);
			Controls sampled = controls;
			sampled.left |= taps.left; sampled.right |= taps.right;
			memset(pressed, 0, sizeof pressed);
			game_update(&game, STEP, sampled);
			accumulator -= STEP;
		}
		game_draw(&game);
		count++;
		if (frames > 0 && count >= frames) running = false;
		if (!running && screenshot && !save_screenshot(r, screenshot)) {
			fail_message("Ошибка сохранения снимка: %s", SDL_GetError()); exit_status = 1;
		}
		SDL_RenderPresent(r->renderer);
		if (!headless && running) {
			double spent = (double)(SDL_GetPerformanceCounter() - previous) / frequency;
			double remaining = STEP - accumulator - spent;
			if (remaining > 0) {
				int timeout = (int)ceil(remaining * 1000.);
				pending_event = SDL_WaitEventTimeout(&event, timeout) != 0;
			}
		}
	}
	
	resources_shutdown(r); free(r); IMG_Quit(); TTF_Quit(); SDL_Quit();
	return exit_status;
}
