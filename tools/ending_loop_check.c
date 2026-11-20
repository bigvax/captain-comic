/* Exercise crown -> conversion -> ending -> scores -> exit through SDL events. */
#include "cc.h"
#include <stdio.h>
#include <string.h>
int cc_application_main(int argc, char **argv);
void __real_game_draw(Game *g);
int __real_game_key(Game *g, SDL_Scancode key, bool repeat);
static int phase, completed, mode;
static const SDL_Scancode advance_keys[] = {SDL_SCANCODE_SPACE, SDL_SCANCODE_KP_ENTER,
	SDL_SCANCODE_RETURN, SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F4,
	SDL_SCANCODE_F5, SDL_SCANCODE_F11, SDL_SCANCODE_Q, SDL_SCANCODE_ESCAPE,
	SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RALT};
static const SDL_Scancode exit_keys[] = {SDL_SCANCODE_SPACE, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F2, SDL_SCANCODE_F11};
static void tap(SDL_Scancode key) {
	SDL_Event event = {0};
	event.type = SDL_KEYDOWN;
	event.key.keysym.scancode = key;
	SDL_PushEvent(&event);
	event.type = SDL_KEYUP;
	SDL_PushEvent(&event);
}
void __wrap_game_draw(Game *g) {
	if (phase == 0) {
		SDL_StartTextInput();
		g->r->scores = (ScoreTable){0};
		g->r->score_path[0] = 0;
		if (mode >= 6)
			for (int i = 0; i < SCORE_COUNT; i++)
				(void)score_add(&g->r->scores, 999999999, "Лидер");
		g->title = false;
		g->spawn_time = 0;
		/* Exercise all treasure combinations, always with only key and lantern. */
		static const unsigned treasures[] = {0, 1, 4, 5};
		g->treasures = treasures[mode % 4];
		g->key = g->lantern = true;
		g->boots = g->wand = g->corkscrew = false;
		g->cola = 0;
		game_collect(g, CROWN);
		phase = 1;
	}
	__real_game_draw(g);
	if (phase == 1 && g->won) {
		tap(advance_keys[mode]);
		phase = 2;
	} else if (phase == 2 && g->scores_open) {
		if (g->score_entry) {
			SDL_Event event = {0};
			event.type = SDL_TEXTINPUT;
			SDL_strlcpy(event.text.text, "Комик", sizeof event.text.text);
			SDL_PushEvent(&event);
			tap(SDL_SCANCODE_RETURN);
			phase = 3;
		} else {
			tap(exit_keys[mode % 4]);
			phase = 4;
		}
	} else if (phase == 3 && g->score_submitted && !g->score_entry) {
		if (!strcmp(g->r->scores.entries[0].name, "Комик")) {
			tap(exit_keys[mode % 4]);
			phase = 4;
		}
	}

}
int __wrap_game_key(Game *g, SDL_Scancode key, bool repeat) {
	int action = __real_game_key(g, key, repeat);
	if (phase == 4 && action == 1 && g->scores_open && !g->score_entry && !g->title) {
		completed = 1;
		phase = 5;
	}
	return action;
}
int main(void) {
	int failed = 0;
	for (mode = 0; mode < (int)(sizeof advance_keys / sizeof advance_keys[0]); mode++) {
		phase = completed = 0;
		char *args[] = {"captain-comic", "--headless", "--frames", "1000", "--assets", "assets", NULL};
		int result = cc_application_main(6, args);
		printf("Ending loop (%s%s): %s; phase %d\n", binding_key_name(advance_keys[mode]),
			mode >= 6 ? ", below top ten" : "", completed ? "PASS" : "FAIL", phase);
		failed |= result || !completed;
	}
	return failed;
}
