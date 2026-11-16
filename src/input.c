#include "cc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
const char *const BIND_TITLES[BIND_COUNT] = {
	"Влево", "Вправо", "Прыжок", "Стрельба", "Открыть дверь", "Телепортация", "Пауза"};
void bindings_defaults(KeyBindings *b) {
	*b = (KeyBindings){
		.key = {{SDL_SCANCODE_LEFT, SDL_SCANCODE_A, 0},
			{SDL_SCANCODE_RIGHT, SDL_SCANCODE_D, 0},
			{SDL_SCANCODE_SPACE, SDL_SCANCODE_UP, 0},
			{SDL_SCANCODE_KP_0, SDL_SCANCODE_INSERT, SDL_SCANCODE_X},
			{SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, SDL_SCANCODE_E},
			{SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_T, 0}, {SDL_SCANCODE_P, 0, 0}}};
}
static bool reserved(SDL_Scancode key) {
	return key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F1 || key == SDL_SCANCODE_F2 ||
	       key == SDL_SCANCODE_F3 || key == SDL_SCANCODE_F4 || key == SDL_SCANCODE_F5 ||
	       key == SDL_SCANCODE_F11 || key == SDL_SCANCODE_Q || key == SDL_SCANCODE_RETURN ||
	       key == SDL_SCANCODE_KP_ENTER;
}
bool bindings_match(const KeyBindings *b, int action, SDL_Scancode key) {
	if (action < 0 || action >= BIND_COUNT || key == SDL_SCANCODE_UNKNOWN)
		return false;
	for (int i = 0; i < BIND_SLOTS; i++)
		if (b->key[action][i] == key)
			return true;
	return false;
}
bool bindings_assign(KeyBindings *b, int action, int slot, SDL_Scancode key) {
	if (action < 0 || action >= BIND_COUNT || slot < 0 || slot >= BIND_SLOTS || key < 0 ||
		key >= SDL_NUM_SCANCODES ||
		(key && (reserved(key) || !SDL_GetScancodeName(key)[0])))
		return false;
	for (int a = 0; key && a < BIND_COUNT; a++)
		for (int i = 0; i < BIND_SLOTS; i++)
			if ((a != action || i != slot) && b->key[a][i] == key)
				return false;
	b->key[action][slot] = key;
	return true;
}
const char *binding_key_name(SDL_Scancode key) {
	switch (key) {
	case SDL_SCANCODE_UNKNOWN:
		return "—";
	case SDL_SCANCODE_LEFT:
		return "Влево";
	case SDL_SCANCODE_RIGHT:
		return "Вправо";
	case SDL_SCANCODE_UP:
		return "Вверх";
	case SDL_SCANCODE_DOWN:
		return "Вниз";
	case SDL_SCANCODE_SPACE:
		return "Пробел";
	case SDL_SCANCODE_INSERT:
		return "Ins";
	case SDL_SCANCODE_KP_0:
		return "Num 0 (Ins)";
	case SDL_SCANCODE_LALT:
		return "Левый Alt";
	case SDL_SCANCODE_RALT:
		return "Правый Alt";
	case SDL_SCANCODE_LCTRL:
		return "Левый Ctrl";
	case SDL_SCANCODE_RCTRL:
		return "Правый Ctrl";
	case SDL_SCANCODE_LSHIFT:
		return "Левый Shift";
	case SDL_SCANCODE_RSHIFT:
		return "Правый Shift";
	default:
		return SDL_GetScancodeName(key);
	}
}
void bindings_describe(const KeyBindings *b, int action, char *out, size_t size) {
	if (!size)
		return;
	out[0] = 0;
	for (int i = 0; i < BIND_SLOTS; i++) {
		if (!b->key[action][i])
			continue;
		size_t used = strlen(out);
		if (used < size)
			snprintf(out + used, size - used, "%s%s", used ? " / " : "",
				binding_key_name(b->key[action][i]));
	}
	if (!out[0])
		snprintf(out, size, "Не задано");
}
static bool held(const KeyBindings *b, int action, const Uint8 *keys, int count) {
	for (int i = 0; i < BIND_SLOTS; i++) {
		int key = b->key[action][i];
		if (key > 0 && key < count && keys[key])
			return true;
	}
	return false;
}
Controls bindings_controls(const KeyBindings *b, const Uint8 *keys, int count) {
	return (Controls){held(b, BIND_LEFT, keys, count), held(b, BIND_RIGHT, keys, count),
		held(b, BIND_JUMP, keys, count), held(b, BIND_FIRE, keys, count)};
}
bool bindings_load(KeyBindings *b, const char *path) {
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	char header[32];
	KeyBindings loaded = {0};
	bool ok = fgets(header, sizeof header, f) && !strcmp(header, "CCKEYS1\n");
	for (int a = 0; ok && a < BIND_COUNT; a++)
		for (int i = 0; ok && i < BIND_SLOTS; i++) {
			char token[32], *end = NULL;
			ok = fscanf(f, "%31s", token) == 1;
			if (ok) {
				errno = 0;
				long key = strtol(token, &end, 10);
				ok = !errno && end != token && !*end && key >= 0 &&
				     key < SDL_NUM_SCANCODES;
				if (ok && (key == SDL_SCANCODE_F2 || key == SDL_SCANCODE_F5))
					key = SDL_SCANCODE_UNKNOWN;
				if (ok)
					ok = bindings_assign(&loaded, a, i, (SDL_Scancode)key);
			}
		}
	int ch;
	while (ok && (ch = fgetc(f)) != EOF)
		if (!isspace((unsigned char)ch))
			ok = false;
	if (ferror(f))
		ok = false;
	fclose(f);
	if (ok)
		*b = loaded;
	return ok;
}
bool bindings_save(const KeyBindings *b, const char *path) {
	char temporary[4608];
	if (snprintf(temporary, sizeof temporary, "%s.tmp.XXXXXX", path) >= (int)sizeof temporary)
		return false;
	int fd = mkstemp(temporary);
	if (fd < 0)
		return false;
	FILE *f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		unlink(temporary);
		return false;
	}
	bool ok = fputs("CCKEYS1\n", f) >= 0;
	for (int a = 0; ok && a < BIND_COUNT; a++)
		ok = fprintf(f, "%d %d %d\n", b->key[a][0], b->key[a][1], b->key[a][2]) > 0;
	if (fclose(f))
		ok = false;
	if (ok && !replace_save_file(temporary, path))
		ok = false;
	if (!ok)
		unlink(temporary);
	return ok;
}

/* Read modifiers from this event, even if Shift was released later in the queue. */
int game_key_event(Game *g, const SDL_KeyboardEvent *event) {
	SDL_Scancode key = event->keysym.scancode;
	bool final_animation = g->ending_stage > ENDING_NONE && g->ending_stage < ENDING_PAGE;
	if (!event->repeat && (event->keysym.mod & KMOD_SHIFT) &&
		g->location_menu && g->settings_page == 3 && !g->help_open && !g->scores_open &&
		!final_animation && (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT)) {
		(void)score_edit_key(g, key, true);
		return 0;
	}
	return game_key(g, key, event->repeat != 0);
}
