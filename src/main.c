#include "cc.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
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
#elif defined(__EMSCRIPTEN__)
#define PLATFORM_NAME "Web"
#define EXE_NAME "captain-comic"
#define PREF_ORG "CaptainComicWeb"
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
#ifdef __EMSCRIPTEN__
	web_restore_saves();
	snprintf(r->config_path, sizeof r->config_path, "/persist/keys.cfg");
	snprintf(r->score_path, sizeof r->score_path, "/persist/scores.dat");
#else
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
#endif
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

typedef struct {
	Resources *r;
	Game game;
	bool headless, running, fullscreen, text_input, pending_event;
	int count, frames, exit_status;
	const char *screenshot;
	SDL_Event event;
	Uint8 pressed[SDL_NUM_SCANCODES];
	double accumulator, frequency;
	Uint64 previous;
} Application;

#ifdef __EMSCRIPTEN__
static Application *web_app;
EM_JS(void, web_notify_stopped, (void), {
	if (Module['ccStopped']) Module['ccStopped']();
})
EMSCRIPTEN_KEEPALIVE void cc_web_action(int action) {
	if (!web_app || !web_app->running) return;
	static const SDL_Scancode keys[] = {SDL_SCANCODE_F1, SDL_SCANCODE_F2,
		SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_ESCAPE};
	if (action < 0 || action >= (int)(sizeof keys / sizeof keys[0])) return;
	(void)game_key(&web_app->game, keys[action], false);
	sync_text_input(&web_app->game, &web_app->text_input);
}
EMSCRIPTEN_KEEPALIVE void cc_web_suspend(void) {
	if (!web_app || !web_app->running) return;
	Game *g = &web_app->game;
	if (!g->title && !g->over && !g->won) g->paused = true;
	g->fire_pending = 0;
	memset(web_app->pressed, 0, sizeof web_app->pressed);
	web_app->accumulator = 0;
	web_app->previous = SDL_GetPerformanceCounter();
}
#endif

static void application_frame(void *userdata) {
	Application *app = userdata;
	Game *game = &app->game;

	Uint64 now = SDL_GetPerformanceCounter();
	double elapsed = app->headless ? 1. / 60. : (double)(now - app->previous) / app->frequency;
	app->previous = now; if (elapsed > .1) elapsed = .1;
	sync_text_input(game, &app->text_input);

	while (app->pending_event || SDL_PollEvent(&app->event)) {
		app->pending_event = false;
		if (app->event.type == SDL_QUIT) app->running = false;
		else if (app->event.type == SDL_WINDOWEVENT && app->event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !game->title) {
			game->paused = true; memset(app->pressed, 0, sizeof app->pressed);
		} else if (app->event.type == SDL_TEXTINPUT) {
			if (game->location_menu && game->settings_page == 3)
				score_edit_text(game, app->event.text.text);
			else
				game_score_text(game, app->event.text.text);
		} else if (app->event.type == SDL_KEYDOWN) {
			if (!app->event.key.repeat && app->event.key.keysym.scancode > SDL_SCANCODE_UNKNOWN && app->event.key.keysym.scancode < SDL_NUM_SCANCODES)
				app->pressed[app->event.key.keysym.scancode] = 1;
			int action = game_key_event(game, &app->event.key);
			sync_text_input(game, &app->text_input);
			if (action == 1) { app->running = false; break; }
			if (action == 2) {
				app->fullscreen = !app->fullscreen;
				if (SDL_SetWindowFullscreen(app->r->window, app->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
					fail_message("Ошибка полноэкранного режима: %s", SDL_GetError()); app->fullscreen = !app->fullscreen;
				}
			}
		}
	}
	int key_count;
	const Uint8 *keys = SDL_GetKeyboardState(&key_count);
	Controls controls = bindings_controls(&app->r->bindings, keys, key_count);
	app->accumulator += elapsed;
	while (app->accumulator >= STEP) {
		Controls taps = bindings_controls(&app->r->bindings, app->pressed, SDL_NUM_SCANCODES);
		Controls sampled = controls;
		sampled.left |= taps.left; sampled.right |= taps.right;
		memset(app->pressed, 0, sizeof app->pressed);
		game_update(game, STEP, sampled);
		app->accumulator -= STEP;
	}
	game_draw(game);
	app->count++;
	if (app->frames > 0 && app->count >= app->frames) app->running = false;
	if (!app->running && app->screenshot && !save_screenshot(app->r, app->screenshot)) {
		fail_message("Ошибка сохранения снимка: %s", SDL_GetError()); app->exit_status = 1;
	}
	SDL_RenderPresent(app->r->renderer);
#ifndef __EMSCRIPTEN__
	if (!app->headless && app->running) {
		double spent = (double)(SDL_GetPerformanceCounter() - app->previous) / app->frequency;
		double remaining = STEP - app->accumulator - spent;
		if (remaining > 0) {
			int timeout = (int)ceil(remaining * 1000.);
			app->pending_event = SDL_WaitEventTimeout(&app->event, timeout) != 0;
		}
	}
#endif

#ifdef __EMSCRIPTEN__
	if (!app->running) {
		emscripten_cancel_main_loop();
		SDL_StopTextInput();
		resources_shutdown(app->r); free(app->r);
		IMG_Quit(); TTF_Quit(); SDL_Quit();
		web_app = NULL;
		free(app);
		web_notify_stopped();
	}
#endif
}

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

	Application *app = calloc(1, sizeof *app);
	if (!app) {
		resources_shutdown(r); free(r); IMG_Quit(); TTF_Quit(); SDL_Quit(); return 1;
	}
	app->r = r;
	Game *game = &app->game;

	if (!headless) {
		resolve_save_paths(r);
		if (r->score_path[0]) (void)scores_load(&r->scores, r->score_path);
	}

	if (!resources_load(r, assets)) {
		resources_shutdown(r); free(r); free(app); IMG_Quit(); TTF_Quit(); SDL_Quit(); return 1;
	}
	if (testing) {
		int result = self_test(r);
		resources_shutdown(r); free(r); free(app); IMG_Quit(); TTF_Quit(); SDL_Quit(); return result;
	}

	/* Запускаем сброс игры ПОСЛЕ загрузки всех ресурсов и world.json */
	game_reset(game, r);
	
	if (start_area >= 0) game_explore(game, start_area, segment);
	if (headless) game->title = false;
	if (intro_page) { game->title = true; game->intro_page = intro_page - 1; }
	if (help_screen) { game->help_open = true; game->help_page = help_screen - 1; }
	if (settings_screen) { game->location_menu = true; game->settings_page = settings_screen - 1; }
	if (ending_screen) { game->title = false; game->spawn_time = 0; game->treasures = 7; game->won = true; }
	if (scores_screen) game_scores_open(game, false);

	app->running = true;
	app->headless = headless;
	app->frames = frames;
	app->screenshot = screenshot;
	app->previous = SDL_GetPerformanceCounter();
	app->frequency = (double)SDL_GetPerformanceFrequency();

#ifdef __EMSCRIPTEN__
	web_app = app;
	emscripten_set_main_loop_arg(application_frame, app, 0, true);
#else
	while (app->running) application_frame(app);
#endif

	
	resources_shutdown(r); free(r); IMG_Quit(); TTF_Quit(); SDL_Quit();
	int result = app->exit_status;
	free(app);
	return result;
}

