/* Integration check: exercise the real application loop with complete SDL taps.
 * Synthetic key events do not change SDL_GetKeyboardState, exposing lost taps. */
#include "cc.h"
#include <stdio.h>
#include <stdlib.h>
int cc_application_main(int argc, char **argv);
int __real_game_key(Game *g, SDL_Scancode key, bool repeat);
void __real_game_update(Game *g, double dt, Controls controls);
void __real_game_draw(Game *g);
static SDL_Thread *sender;
static SDL_atomic_t sent[20];
static int received, moves, shots, jumps, max_delay, audio_pass;
static int send_taps(void *unused) {
	(void)unused;
	SDL_Delay(10);
	for (int i = 0; i < 20; i++) {
		SDL_AtomicSet(&sent[i], (int)SDL_GetTicks());
		SDL_Event event = {0};
		if (i == 0) {
			event.type = SDL_KEYDOWN;
			event.key.keysym.scancode = SDL_SCANCODE_SPACE;
			SDL_PushEvent(&event);
			event.type = SDL_KEYUP;
			SDL_PushEvent(&event);
		}
		event.type = SDL_KEYDOWN;
		event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
		SDL_PushEvent(&event);
		event.type = SDL_KEYUP;
		SDL_PushEvent(&event);
		event.type = SDL_KEYDOWN;
		event.key.keysym.scancode = SDL_SCANCODE_INSERT;
		SDL_PushEvent(&event);
		event.type = SDL_KEYUP;
		SDL_PushEvent(&event);
		SDL_Delay(13);
	}
	SDL_Delay(650);
	SDL_Event quit = {0};
	quit.type = SDL_QUIT;
	SDL_PushEvent(&quit);
	return 0;
}
int __wrap_game_key(Game *g, SDL_Scancode key, bool repeat) {
	if (key == SDL_SCANCODE_RIGHT && received < 20) {
		int delay = (int)SDL_GetTicks() - SDL_AtomicGet(&sent[received++]);
		if (delay > max_delay)
			max_delay = delay;
	}
	int before = g->shot_count;
	double before_vy = g->vy;
	int result = __real_game_key(g, key, repeat);
	if (key == SDL_SCANCODE_SPACE && before_vy == 0 && g->vy < 0)
		jumps++;
	if (g->shot_count > before)
		shots++;
	return result;
}
void __wrap_game_update(Game *g, double dt, Controls controls) {
	double before = g->x;
	int previous_shots = g->shot_count;
	__real_game_update(g, dt, controls);
	if (g->shot_count > previous_shots)
		shots += g->shot_count - previous_shots;
	if (g->x > before && controls.right)
		moves++;
}
void __wrap_game_draw(Game *g) {
	if (!sender) {
		g->title = g->paused = false;
		g->spawn_time = 0;
		g->invincible = 1e9;
		sound_set_enabled(g->r, audio_pass != 0);
		if (audio_pass)
			sound_play(g->r, SOUND_PANORAMA);
		sender = SDL_CreateThread(send_taps, "input-check", NULL);
		if (!sender)
			exit(2);
	}
	__real_game_draw(g);
}
int main(void) {
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	SDL_setenv("XDG_DATA_HOME", "/tmp/cc-input-check-no-prefs", 1);
	char *args[] = {"captain-comic", "--assets", "assets", "--area", "forest", "--frames",
		"1000", NULL};
	int failed = 0;
	for (audio_pass = 0; audio_pass < 2; audio_pass++) {
		sender = NULL;
		received = moves = shots = jumps = max_delay = 0;
		for (int i = 0; i < 20; i++)
			SDL_AtomicSet(&sent[i], 0);
		int result = cc_application_main(7, args);
		if (sender)
			SDL_WaitThread(sender, NULL);
		printf("SDL input (%s): %d/20 taps received, %d/20 movement steps, %d shots, %d immediate jump; max dispatch %d ms\n",
			audio_pass ? "panorama sound" : "muted", received, moves, shots, jumps,
			max_delay);
		failed |= result || received != 20 || moves != 20 || shots != 5 || jumps != 1 ||
			  max_delay > 50;
	}
	return failed;
}
