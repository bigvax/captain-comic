#include "cc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
static int checks, failures, scenarios;
#define CHECK(e)                                                                                   \
	do {                                                                                       \
		checks++;                                                                          \
		if (!(e)) {                                                                        \
			failures++;                                                                \
			fprintf(stderr, "Ошибка в строке %d: %s\n", __LINE__, #e);                 \
		}                                                                                  \
	} while (0)
static void quiet(Game *g) {
	for (int a = 0; a < 8; a++)
		for (int s = 0; s < 3; s++)
			for (int i = 0; i < 4; i++) {
				g->enemies[a][s][i].alive = false;
				g->enemies[a][s][i].timer = 1e9;
			}
}
static void reset(Game *g, Resources *r) {
	game_reset(g, r);
	g->title = false;
	g->spawn_time = 0;
	g->invincible = 0;
	quiet(g);
}
static void tick(Game *g, int count, Controls c) {
	for (int i = 0; i < count; i++)
		game_update(g, STEP, c);
}
/* Independent fixed-point DOS arc, 0x18A4..0x18FC. The native port keeps
 * smooth motion; compare its flight envelope with the original tick sequence. */
static void dos_jump_at(bool moon, bool boots, int start_y, double *height, double *duration) {
	int origin = start_y / 8;
	int y = origin, velocity = 0, support = boots ? 5 : 4, top = y;
	for (int frame = 1; frame < 40; frame++) {
		if (--support)
			velocity -= 7;
		else
			support++;
		/* Arithmetic right shift in the original means floor, including negatives. */
		y += (int)floor(velocity / 8.);
		if (y < 0)
			y = 0;
		if (y < top)
			top = y;
		velocity += moon ? 3 : 5;
		if (velocity > 23)
			velocity = 23;
		if (velocity > 0 && y + 5 >= origin + 4) {
			*height = (origin - top) * 8;
			*duration = frame * WEAPON_TICK;
			return;
		}
	}
	*height = *duration = 0;
}
static bool take(Game *g, int expected) {
	int s = game_scene(g);
	Item item = g->r->areas[g->area].scenes[s].item;
	if (!item.present || item.kind != expected || g->taken[g->area][s])
		return false;
	g->taken[g->area][s] = true;
	game_collect(g, item.kind);
	return true;
}
static bool door(Game *g, int index) {
	int s = game_scene(g);
	Scene *sc = &g->r->areas[g->area].scenes[s];
	if (index >= sc->door_count)
		return false;
	Door d = sc->doors[index];
	g->x = s * 2048. + d.x + 8;
	g->y = d.y;
	g->grounded = true;
	game_interact(g);
	if (g->door_time > 0)
		game_update(g, DOOR_DURATION, (Controls){0});
	return g->area == d.area && game_scene(g) == d.scene;
}
static bool edge(Game *g, int direction) {
	int s = game_scene(g);
	Scene *sc = &g->r->areas[g->area].scenes[s];
	int target = direction > 0 ? sc->right : sc->left;
	if (target < 0)
		return false;
	double x = target * 2048. + (direction > 0 ? 0 : 2032);
	for (int y = 0; y <= 128; y += 8)
		if (!box_hits(g->r, g->area, (Box){x + 3, y + 2, 10, 30})) {
			g->y = y;
			game_cross(g, direction);
			return game_scene(g) == target;
		}
	return false;
}
static bool walkthrough(Game *g) {
	return take(g, 8) && edge(g, 1) && edge(g, 1) && take(g, KEY) && door(g, 0) && take(g, 8) &&
	       edge(g, -1) && edge(g, -1) && take(g, 8) && door(g, 1) && take(g, 8) && edge(g, 1) &&
	       edge(g, 1) && take(g, GEMS) && door(g, 0) && edge(g, -1) && take(g, CORK) &&
	       edge(g, 1) && edge(g, -1) && door(g, 1) && door(g, 0) && take(g, BOOTS) &&
	       door(g, 1) && door(g, 0) && door(g, 0) && door(g, 2) && take(g, WAND) &&
	       edge(g, -1) && take(g, GOLD_ITEM) && door(g, 0) && door(g, 0) && edge(g, 1) &&
	       edge(g, 1) && door(g, 1) && edge(g, 1) && take(g, 8) && door(g, 0) && door(g, 1) &&
	       edge(g, 1) && edge(g, 1) && take(g, LANTERN) && door(g, 0) && edge(g, -1) &&
	       edge(g, -1) && door(g, 0) && door(g, 2) && door(g, 1) && door(g, 1) &&
	       take(g, CROWN);
}
int self_test(Resources *r) {
	Game g;
	checks = failures = scenarios = 0;
	scenarios++; /* Every native map, solidity threshold, species and item. */
	int item_count = 0, cola_count = 0;
	bool kinds[14] = {false};
	for (int a = 0; a < 8; a++) {
		/* TT2 owns the passability threshold; custom tilesets may change it. */
		const Area *area = &r->areas[a];
		bool valid_threshold = area->last_passable >= 0 && area->last_passable < area->tile_count;
		if (!valid_threshold)
			fprintf(stderr, "Area %d (%s): invalid last_passable=%d for tile_count=%d\n",
				a, AREA_FILES[a], area->last_passable, area->tile_count);
		CHECK(valid_threshold);
		for (int s = 0; s < 3; s++) {
			Scene *sc = &r->areas[a].scenes[s];
			for (int i = 0; i < 1280; i++)
				CHECK(r->areas[a].cells[s][i] < r->areas[a].tile_count);
			if (sc->item.present) {
				item_count++;
				if (sc->item.kind >= 8 && sc->item.kind <= 12)
					cola_count++;
				CHECK(sc->item.x % 8 == 0 && sc->item.y % 8 == 0);
			}
			for (int i = 0; i < sc->enemy_count; i++)
				kinds[sc->enemies[i].shape] = true;
		}
	}
	CHECK(item_count == 24);
	CHECK(cola_count == 5);
	for (int i = 0; i < 14; i++)
		CHECK(kinds[i]);
	CHECK(r->player.count == 10);
	CHECK(r->items.count == 16);
	CHECK(r->areas[FOREST].scenes[2].item.kind == KEY);
	CHECK(r->areas[FOREST].scenes[2].item.x == 1280);
	CHECK(r->areas[FOREST].scenes[2].item.y == 16);
	CHECK(r->areas[BASE].scenes[1].item.kind == CORK);
	CHECK(r->areas[BASE].scenes[2].item.kind == BOOTS);
	CHECK(r->areas[CAVE].scenes[2].item.kind == WAND);
	scenarios++; /* Door traversal and clear arrival locations. */
	reset(&g, r);
	g.key = true;
	int doors = 0;
	for (int a = 0; a < 8; a++)
		for (int s = 0; s < 3; s++)
			for (int d = 0; d < r->areas[a].scenes[s].door_count; d++) {
				g.area = a;
				g.x = s * 2048.;
				CHECK(door(&g, d));
				CHECK(!box_hits(r, g.area, (Box){g.x + 3, g.y + 2, 10, 30}));
				doors++;
			}
	CHECK(doors > 20);
	scenarios++; /* Clear supported spawn on all 24 panoramas. */
	for (int a = 0; a < 8; a++)
		for (int s = 0; s < 3; s++) {
			reset(&g, r);
			game_explore(&g, a, s);
			CHECK(g.area == a && game_scene(&g) == s);
			CHECK(!box_hits(r, a, (Box){g.x + 3, g.y + 2, 10, 30}));
			CHECK(box_hits(r, a, (Box){g.x + 3, g.y + 2.1, 10, 30}));
			double y = g.y;
			tick(&g, 30, (Controls){0});
			CHECK(fabs(g.y - y) < .001 && g.grounded);
		}
	scenarios++; /* Initial cola and empty inventory before starting. */
	reset(&g, r);
	CHECK(g.x == 104 && g.y == 96 && g.lives == 5 && g.cola == 0);
	CHECK(!g.key && !g.boots && !g.wand);
	CHECK(r->areas[FOREST].scenes[0].item.x == 120 && r->areas[FOREST].scenes[0].item.y == 112);
	game_update(&g, STEP, (Controls){0});
	CHECK(g.cola == 0 && !g.taken[FOREST][0]);
	g.x = 128;
	game_update(&g, STEP, (Controls){0});
	CHECK(g.cola == 1 && g.taken[FOREST][0]);
	scenarios++; /* Leftwards lake topology and isolated castle rooms. */
	reset(&g, r);
	g.area = LAKE;
	g.x = 0;
	g.y = 112;
	game_cross(&g, -1);
	CHECK(game_scene(&g) == 1);
	game_cross(&g, 1);
	CHECK(game_scene(&g) == 0);
	g.area = CASTLE;
	g.x = 2032;
	game_cross(&g, 1);
	CHECK(g.x == 2032);
	scenarios++; /* Key gate. */
	reset(&g, r);
	game_interact(&g);
	CHECK(g.area == FOREST);
	g.key = true;
	game_interact(&g);
	CHECK(g.door_time > 0 && g.area == FOREST);
	game_update(&g, DOOR_DURATION, (Controls){0});
	CHECK(g.area == CASTLE && g.x == 1976 && g.y == 112);
	game_interact(&g);
	game_update(&g, DOOR_DURATION, (Controls){0});
	CHECK(g.area == FOREST && g.x == 104 && g.y == 96);
	scenarios++; /* Exact reported forest trap; no boots. */
	reset(&g, r);
	g.x = 2048. + 1904;
	g.y = 112;
	game_jump(&g);
	bool escaped = false;
	double best = 112;
	for (int i = 0; i < 180; i++) {
		game_update(&g, STEP, (Controls){.left = true, .jump = true});
		best = fmin(best, g.y);
		if (g.grounded && g.x + 13 <= 2048 + 1888) {
			escaped = true;
			break;
		}
	}
	CHECK(escaped && 112 - best > 48 && !g.boots && g.lives == 5);
	scenarios++; /* Lake scene 2: jump from dock to raised tile below a low bridge. */
	CHECK(tile_solid(r, LAKE, 2 * 128 + 124, 6));
	CHECK(tile_solid(r, LAKE, 2 * 128 + 126, 5));
	CHECK(tile_solid(r, LAKE, 2 * 128 + 124, 2));
	for (int start = 1984; start <= 2000; start += 2) {
		reset(&g, r);
		g.area = LAKE;
		g.x = 4096 + start;
		g.y = 64;
		game_jump(&g);
		bool reached = false;
		for (int i = 0; i < 120; i++) {
			game_update(&g, STEP, (Controls){.right = g.x < 4096 + 2018, .jump = true});
			CHECK(!box_hits(r, LAKE, (Box){g.x + 3, g.y + 2, 10, 30}));
			if (g.grounded && g.y == 48 && g.x + 13 > 4096 + 2016) {
				reached = true;
				break;
			}
		}
		CHECK(reached && !g.boots && g.lives == 5);
	}
	reset(&g, r);
	g.area = LAKE;
	g.x = 4096 + 1984;
	g.y = 64;
	game_jump(&g);
	tick(&g, 15, (Controls){.jump = true});
	CHECK(g.y >= 46 && g.y < 47 && g.vy < 0);
	game_update(&g, STEP, (Controls){0});
	tick(&g, 60, (Controls){0});
	CHECK(g.grounded && g.y == 64 && g.jump_boost == 0);
	scenarios++; /* Short and held jump, then boots. */
	double short_height, long_height, boot_height;
	reset(&g, r);
	game_jump(&g);
	best = g.y;
	for (int i = 0; i < 100; i++) {
		game_update(&g, STEP, (Controls){0});
		best = fmin(best, g.y);
	}
	short_height = 96 - best;
	reset(&g, r);
	game_jump(&g);
	best = g.y;
	for (int i = 0; i < 100; i++) {
		game_update(&g, STEP, (Controls){.jump = true});
		best = fmin(best, g.y);
	}
	long_height = 96 - best;
	reset(&g, r);
	g.boots = true;
	game_jump(&g);
	best = g.y;
	for (int i = 0; i < 100; i++) {
		game_update(&g, STEP, (Controls){.jump = true});
		best = fmin(best, g.y);
	}
	boot_height = 96 - best;
	CHECK(long_height > short_height);
	CHECK(boot_height > long_height);
	CHECK(long_height > 48);
	scenarios++; /* Physical moon 128px and castle 64px gaps. */
	reset(&g, r);
	g.area = MOON;
	g.x = 1260;
	g.y = 112;
	game_jump(&g);
	bool landed = false;
	for (int i = 0; i < 240; i++) {
		game_update(&g, STEP, (Controls){.right = true, .jump = true});
		if (g.grounded && g.x > 1380) {
			landed = true;
			break;
		}
	}
	CHECK(landed && g.lives == 5);
	reset(&g, r);
	g.area = CASTLE;
	g.x = 2048. + 204;
	g.y = 16;
	g.boots = true;
	game_jump(&g);
	landed = false;
	for (int i = 0; i < 200; i++) {
		game_update(&g, STEP, (Controls){.left = true, .jump = true});
		if (g.grounded && g.x < 2048 + 141) {
			landed = true;
			break;
		}
	}
	CHECK(landed && g.lives == 5);
	scenarios++; /* Fire, five simultaneous balls, energy and corkscrew. */
	reset(&g, r);
	g.x = 200;
	tick(&g, 120, (Controls){.fire = true});
	CHECK(g.shot_count == 0);
	for (int i = 0; i < 8; i++) {
		game_collect(&g, 8);
	}
	CHECK(g.cola == 5);
	g.energy = MAX_WEAPON_ENERGY;
	tick(&g, 130, (Controls){.fire = true});
	CHECK(g.shot_count == 5);
	int energy = g.energy;
	tick(&g, 30, (Controls){0});
	CHECK(g.energy > energy);
	reset(&g, r);
	g.cola = 1;
	g.corkscrew = true;
	game_update(&g, STEP, (Controls){.fire = true});
	tick(&g, 20, (Controls){0});
	CHECK(g.shot_count == 1 && fabs(g.shots[0].y - g.shots[0].y0) > 2);
	scenarios++; /* Original discrete weapon divider and empty-trigger behaviour. */
	reset(&g, r);
	g.weapon_clock = 0;
	game_update(&g, WEAPON_TICK / 2, (Controls){.fire = true});
	CHECK(g.energy == 12);
	game_update(&g, WEAPON_TICK / 2, (Controls){.fire = true});
	CHECK(g.energy == 11 && g.weapon_phase == 1 && g.shot_count == 0);
	game_update(&g, WEAPON_TICK, (Controls){.fire = true});
	CHECK(g.energy == 11 && g.weapon_phase == 2);
	for (int i = 0; i < 22; i++)
		game_update(&g, WEAPON_TICK, (Controls){.fire = true});
	CHECK(g.energy == 0);
	int phase = g.weapon_phase;
	for (int i = 0; i < 30; i++)
		game_update(&g, WEAPON_TICK, (Controls){.fire = true});
	CHECK(g.energy == 0 && g.weapon_phase == phase);
	for (int i = 0; i < 24; i++)
		game_update(&g, WEAPON_TICK, (Controls){0});
	CHECK(g.energy == 12);
	g.paused = true;
	double clock = g.weapon_clock;
	game_update(&g, 1, (Controls){.fire = true});
	CHECK(g.energy == 12 && g.weapon_clock == clock);
	g.paused = false;
	g.cola = 1;
	g.shot_count = 1;
	g.shots[0] = (Shot){g.x + 16, g.y + 14, g.y + 14, 1.6, 0, 1};
	g.weapon_phase = 2;
	g.weapon_clock = 0;
	game_update(&g, WEAPON_TICK, (Controls){.fire = true});
	CHECK(g.energy == 11 && g.shot_count == 1);

	scenarios++; /* Full shield bonus, hit grace, zero shield and death. */
	reset(&g, r);
	g.hp = 3;
	game_collect(&g, SHIELD);
	CHECK(g.hp == 6 && g.lives == 5);
	game_collect(&g, SHIELD);
	CHECK(g.lives == 6);
	reset(&g, r);
	g.hp = 1;
	game_hurt(&g, false);
	CHECK(g.hp == 0 && g.lives == 5);
	game_hurt(&g, false);
	CHECK(g.lives == 5);
	g.invincible = 0;
	game_hurt(&g, false);
	CHECK(g.lives == 4 && g.death_time > 0 && !g.over);
	game_update(&g, DEATH_DURATION, (Controls){0});
	CHECK(g.hp == 6 && g.spawn_time == 0 && g.death_time == 0);
	game_update(&g, ARRIVAL_DURATION, (Controls){0});
	g.key = g.boots = true;
	game_hurt(&g, true);
	game_update(&g, DEATH_DURATION, (Controls){0});
	CHECK(g.lives == 3 && g.key && g.boots && g.x == 104 && g.y == 96);
	scenarios++; /* Treasure and score milestone lives. */
	reset(&g, r);
	game_collect(&g, GEMS);
	CHECK(g.lives == 6);
	g.score = 49999;
	game_score(&g, 1);
	CHECK(g.lives == MAX_GAME_LIVES && g.score == 72500);
	scenarios++; /* Teleport crosses a wall but not a pit. */
	uint8_t backup[3][1280];
	memcpy(backup, r->areas[FOREST].cells, sizeof backup);
	memset(r->areas[FOREST].cells, 0, sizeof backup);
	int solid = r->areas[FOREST].last_passable + 1;
	for (int x = 0; x < 128; x++)
		r->areas[FOREST].cells[0][9 * 128 + x] = (uint8_t)solid;
	for (int y = 6; y < 9; y++)
		r->areas[FOREST].cells[0][y * 128 + 6] = (uint8_t)solid;
	scenarios++; /* Collision resolution on both sides, ceiling and landing. */
	Box probe = {64, 100, 10, 30};
	Contacts contacts = move_box(r, FOREST, &probe, 40, 0);
	CHECK(contacts.hit_x && fabs(probe.x + probe.w - 96) < .001);
	probe = (Box){120, 100, 10, 30};
	contacts = move_box(r, FOREST, &probe, -40, 0);
	CHECK(contacts.hit_x && fabs(probe.x - 112) < .001);
	probe = (Box){100, 50, 10, 30};
	contacts = move_box(r, FOREST, &probe, 0, 100);
	CHECK(contacts.ground && fabs(probe.y + probe.h - 96) < .001);
	r->areas[FOREST].cells[0][8 * 128 + 6] = 0;
	r->areas[FOREST].cells[0][7 * 128 + 6] = 0;
	probe = (Box){100, 120, 10, 16};
	contacts = move_box(r, FOREST, &probe, 0, -40);
	CHECK(contacts.hit_y && !contacts.ground && fabs(probe.y - 112) < .001);
	r->areas[FOREST].cells[0][8 * 128 + 6] = (uint8_t)solid;
	r->areas[FOREST].cells[0][7 * 128 + 6] = (uint8_t)solid;
	reset(&g, r);
	g.x = 64;
	g.y = 112;
	g.wand = true;
	CHECK(game_teleport(&g));
	CHECK(g.x == 64 && g.teleport_time > 0);
	game_update(&g, TELEPORT_DURATION, (Controls){0});
	CHECK(g.x == 112 && g.y == 112);
	CHECK(!game_teleport(&g));
	g.teleport_cooldown = 0;
	g.face = -1;
	CHECK(game_teleport(&g));
	game_update(&g, TELEPORT_DURATION, (Controls){0});
	CHECK(g.x == 64);
	for (int y = 0; y < 10; y++)
		r->areas[FOREST].cells[0][y * 128 + 7] = 0;
	g.teleport_cooldown = 0;
	g.face = 1;
	CHECK(!game_teleport(&g));
	CHECK(g.x == 64);
	memcpy(r->areas[FOREST].cells, backup, sizeof backup);
	scenarios++; /* Modal routing, physical scancodes, key repeat and pause. */
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	CHECK(g.paused);
	game_key(&g, SDL_SCANCODE_ESCAPE, true);
	CHECK(g.paused);
	CHECK(game_key(&g, SDL_SCANCODE_Q, false) == 1);
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	CHECK(g.paused && !g.location_menu);
	CHECK(game_key(&g, SDL_SCANCODE_Q, false) == 1);
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F1, false);
	double old_x = g.x, old_time = g.time;
	game_update(&g, STEP, (Controls){.right = true, .fire = true});
	CHECK(g.x == old_x && g.time == old_time);
	game_key(&g, SDL_SCANCODE_PAGEDOWN, false);
	CHECK(g.help_page == 1);
	game_key(&g, SDL_SCANCODE_F1, false);
	CHECK(!g.help_open && !g.paused);
	game_key(&g, SDL_SCANCODE_F1, false);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	CHECK(g.paused && !g.help_open);
	CHECK(game_key(&g, SDL_SCANCODE_Q, false) == 1);
	scenarios++; /* All help pages, glyphs and map rendering. */
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F1, false);
	for (int i = 0; i < 6; i++) {
		CHECK(g.help_page == i);
		game_draw(&g);
		game_key(&g, SDL_SCANCODE_PAGEDOWN, false);
	}
	CHECK(g.help_page == 0);
	for (int i = 0; i < 8; i++) {
		g.help_open = false;
		g.location_menu = true;
		game_key(&g, (SDL_Scancode)(SDL_SCANCODE_1 + i), false);
		CHECK(g.area == MENU_AREAS[i] && g.cola == 0 && !g.wand && !g.lantern);
		game_draw(&g);
	}
	/* HUD smoothing must never leak into the pixel-art world textures. */
	SDL_ScaleMode world_scale;
	CHECK(SDL_GetTextureScaleMode(r->items.texture, &world_scale) == 0);
	CHECK(world_scale == SDL_ScaleModeNearest);
	CHECK(SDL_GetTextureScaleMode(r->hud_icons, &world_scale) == 0 && world_scale == SDL_ScaleModeNearest);
	for (int life = 0; life < 2; life++) {
		SDL_Texture *icon = r->life_icons[life];
		CHECK(icon != NULL);
		CHECK(SDL_GetTextureScaleMode(icon, &world_scale) == 0 && world_scale == SDL_ScaleModeLinear);
		int width = 0, height = 0, cell = 0;
		SDL_QueryTexture(icon, NULL, NULL, &width, &height);
		SDL_QueryTexture(r->hud_icons, NULL, NULL, NULL, &cell);
		CHECK(width == cell + 2 && height == width);
		SDL_Texture *target = SDL_CreateTexture(r->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
		unsigned char *pixels = malloc((size_t)width * height * 4);
		CHECK(target && pixels);
		if (target && pixels) {
			SDL_Texture *previous = SDL_GetRenderTarget(r->renderer);
			SDL_SetRenderTarget(r->renderer, target);
			SDL_SetTextureBlendMode(icon, SDL_BLENDMODE_NONE);
			CHECK(SDL_RenderCopy(r->renderer, icon, NULL, NULL) == 0);
			CHECK(SDL_RenderReadPixels(r->renderer, NULL, SDL_PIXELFORMAT_RGBA32, pixels, width * 4) == 0);
			CHECK(pixels[3] == 0 && (pixels[0] || pixels[1] || pixels[2]));
			bool partial = false;
			for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
				unsigned char alpha = pixels[(y * width + x) * 4 + 3];
				if (x == 0 || y == 0 || x == width - 1 || y == height - 1) CHECK(alpha == 0);
				partial |= alpha > 0 && alpha < 255;
			}
			CHECK(partial);
			SDL_SetTextureBlendMode(icon, SDL_BLENDMODE_BLEND);
			SDL_SetRenderTarget(r->renderer, previous);
		}
		free(pixels); SDL_DestroyTexture(target);
	}
	CHECK(r->intro_logo != NULL && r->story_logo != NULL && r->legend_logo != NULL && r->ending_logo != NULL);
	TTF_Font *loaded_font = NULL;
	for (int fz = 0; fz < 64; fz++) if (r->fonts[fz]) { loaded_font = r->fonts[fz]; break; }
	CHECK(loaded_font != NULL && TTF_GlyphIsProvided(loaded_font, 0x0416) != 0);
	scenarios++; /* Darkness and the lantern in the actual renderer. */
	reset(&g, r);
	g.area = CASTLE;
	g.camera = 0;
	g.lantern = false;
	game_draw(&g);
	int output_w, output_h;
	SDL_GetRendererOutputSize(r->renderer, &output_w, &output_h);
	double view_scale = fmin(output_w / (double)WIDTH, output_h / (HEIGHT * 1.2));
	SDL_Rect point = {(int)lround((output_w - WIDTH * view_scale) / 2),
		(int)lround((output_h - HEIGHT * view_scale * 1.2) / 2 +
			    (HUD_HEIGHT + 1) * view_scale * 1.2),
		1, 1};
	Uint8 pixel[4] = {255, 255, 255, 255};
	CHECK(SDL_RenderReadPixels(r->renderer, &point, SDL_PIXELFORMAT_RGBA32, pixel, 4) == 0);
	CHECK(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0);
	g.lantern = true;
	game_draw(&g);
	CHECK(SDL_RenderReadPixels(r->renderer, &point, SDL_PIXELFORMAT_RGBA32, pixel, 4) == 0);
	CHECK(pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0);
	scenarios++; /* Route/inventory audit; endpoints rather than every physical jump. */
	reset(&g, r);
	CHECK(walkthrough(&g));
	CHECK((g.won || g.ending_stage != ENDING_NONE) && g.cola == 5 && g.key && g.boots && g.wand && g.corkscrew && g.lantern);
	scenarios++; /* Intro navigation and no gameplay before the final page. */
	game_reset(&g, r);
	CHECK(g.title && g.intro_page == 0);
	old_x = g.x;
	old_time = g.time;
	for (int page = 0; page < 3; page++) {
		CHECK(g.title && g.intro_page == page);
		game_draw(&g);
		tick(&g, 60, (Controls){.right = true, .jump = true, .fire = true});
		CHECK(g.x == old_x && g.time == old_time && g.cola == 0 && g.shot_count == 0);
		game_key(&g, SDL_SCANCODE_RETURN, true);
		CHECK(g.intro_page == page && g.title);
		game_key(&g, SDL_SCANCODE_RETURN, false);
	}
	CHECK(!g.title);
	CHECK(g.area == FOREST && g.x == 104 && g.y == 96);
	scenarios++; /* Back navigation, help round trip, and new-game entry points. */
	game_reset(&g, r);
	game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.intro_page == 0);
	game_key(&g, SDL_SCANCODE_SPACE, false);
	CHECK(g.intro_page == 1);
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.intro_page == 2);
	game_key(&g, SDL_SCANCODE_BACKSPACE, false);
	CHECK(g.intro_page == 1);
	game_key(&g, SDL_SCANCODE_F1, false);
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.intro_page == 1 && g.help_page == 1);
	game_key(&g, SDL_SCANCODE_F1, false);
	CHECK(g.title && !g.help_open && g.intro_page == 1);
	game_key(&g, SDL_SCANCODE_PAGEUP, false);
	CHECK(g.intro_page == 0);
	CHECK(game_key(&g, SDL_SCANCODE_Q, false) == 1);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_R, false);
	CHECK(g.title && g.intro_page == 0 && !g.test_mode);
	g.title = false;
	g.over = true;
	game_key(&g, SDL_SCANCODE_RETURN, false);
	CHECK(g.scores_open && !g.score_entry);
	CHECK(game_key(&g, SDL_SCANCODE_RETURN, false) == 1);
	CHECK(g.scores_open && g.over && !g.title);
	scenarios++; /* Authentic effects, protected arrival and no automatic cola pickup. */
	CHECK(r->projectile.count == 2 && r->projectile.height == 8);
	CHECK(r->arrival.count == 12 && r->arrival.height == 32);
	game_reset(&g, r);
	g.title = false;
	quiet(&g);
	double spawn_x = g.x, spawn_y = g.y;
	for (int frame = 0; frame < 12; frame++) {
		CHECK(g.spawn_time > 0);
		CHECK(game_effect_frame(g.spawn_time, 12) == frame);
		game_draw(&g);
		game_key(&g, SDL_SCANCODE_SPACE, false);
		game_update(&g, WEAPON_TICK, (Controls){.left = true, .jump = true, .fire = true});
		CHECK(g.x == spawn_x && g.y == spawn_y && g.cola == 0);
	}
	tick(&g, 2, (Controls){0});
	CHECK(g.spawn_time == 0 && g.cola == 0 && !g.taken[FOREST][0]);
	game_hurt(&g, false);
	CHECK(g.hp == 6);
	g.invincible = 0;
	g.x = 128;
	game_update(&g, STEP, (Controls){0});
	CHECK(g.cola == 1);
	g.corkscrew = true;
	tick(&g, 14, (Controls){.fire = true});
	double wave = 0;
	for (int i = 0; i < 15; i++) {
		game_update(&g, STEP, (Controls){0});
		if (g.shot_count)
			wave = fmax(wave, fabs(g.shots[0].y - g.shots[0].y0));
	}
	CHECK(g.shot_count == 1);
	CHECK(wave > 2);
	game_draw(&g);
	g.spawn_time = 0;
	game_hurt(&g, true);
	CHECK(g.death_time > 0 && g.spawn_time == 0);
	game_update(&g, DEATH_DURATION, (Controls){0});
	CHECK(g.spawn_time == 0 && g.death_time == 0 && g.x == 104 && g.cola == 1);
	game_draw(&g);
	scenarios++; /* F4 keyboard editing, numeric bounds and paused simulation. */
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_TAB, false);
	CHECK(g.location_menu && g.settings_page == 1 && g.settings_row == SET_COLA);
	double edit_x = g.x, edit_time = g.time;
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.cola == 1);
	game_key(&g, SDL_SCANCODE_RETURN, false);
	CHECK(g.cola == 2);
	game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.cola == 1);
	for (int i = 0; i < 8; i++)
		game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.cola == 5);
	g.shot_count = 5;
	game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.cola == 4 && g.shot_count == 4);
	for (int i = 0; i < 8; i++)
		game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.cola == 0 && g.shot_count == 0);
	tick(&g, 60, (Controls){.right = true, .jump = true, .fire = true});
	CHECK(g.x == edit_x && g.time == edit_time);
	game_key(&g, SDL_SCANCODE_DOWN, false);
	CHECK(g.settings_row == SET_SHIELD);
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.hp == 6);
	game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.hp == 5);
	game_key(&g, SDL_SCANCODE_RETURN, false);
	CHECK(g.hp == 6);
	for (int i = 0; i < 8; i++)
		game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.hp == 0);
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.hp == 1);
	scenarios++; /* Life limits, death at zero and recovery from the F4 editor. */
	game_key(&g, SDL_SCANCODE_DOWN, false);
	CHECK(g.settings_row == SET_LIVES);
	for (int i = 0; i < 120; i++)
		game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.lives == MAX_EDITOR_LIVES);
	game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.lives == MAX_EDITOR_LIVES - 1);
	for (int i = 0; i < 120; i++)
		game_key(&g, SDL_SCANCODE_LEFT, false);
	CHECK(g.lives == 0 && g.over && g.location_menu);
	g.y = 200;
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.lives == 1 && !g.over && g.location_menu && g.hp == 6 && g.spawn_time > 0);
	CHECK(g.x == g.checkpoint_x && g.y == g.checkpoint_y);
	game_key(&g, SDL_SCANCODE_RIGHT, true);
	CHECK(g.lives == 1);
	scenarios++; /* All equipment toggles, treasure victory and no pickup side effects. */
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_TAB, false);
	int edit_score = g.score, edit_lives = g.lives;
	bool edit_taken = g.taken[FOREST][0];
	for (int field = SET_KEY; field < SET_CROWN; field++) {
		g.settings_row = field;
		CHECK(!game_setting_value(&g, field));
		game_key(&g, SDL_SCANCODE_RIGHT, false);
		CHECK(game_setting_value(&g, field) == 1);
		game_key(&g, SDL_SCANCODE_LEFT, false);
		CHECK(game_setting_value(&g, field) == 0);
		game_key(&g, SDL_SCANCODE_RETURN, false);
		CHECK(game_setting_value(&g, field) == 1);
		game_key(&g, SDL_SCANCODE_SPACE, false);
		CHECK(game_setting_value(&g, field) == 0);
		CHECK(g.score == edit_score && g.lives == edit_lives &&
			g.taken[FOREST][0] == edit_taken);
	}
	g.settings_row = SET_CROWN;
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(game_setting_value(&g, SET_CROWN) == 0);
	game_key(&g, SDL_SCANCODE_1, false);
	CHECK(g.area == FOREST && g.location_menu);
	game_setting_change(&g, SET_GEMS, 1);
	game_setting_change(&g, SET_GOLD, 1);
	game_setting_change(&g, SET_CROWN, 1);
	CHECK(g.treasures == 5 && !g.won && g.location_menu);
	game_setting_change(&g, SET_LIVES, -g.lives);
	CHECK(g.over && !g.won);
	game_setting_change(&g, SET_LIVES, edit_lives);
	CHECK(!g.won && !g.over && g.lives == edit_lives);
	game_setting_change(&g, SET_GOLD, -1);
	CHECK(g.treasures == 1 && !g.won);
	g.taken[FOREST][0] = true;
	game_setting_change(&g, SET_KEY, 1);
	game_setting_change(&g, SET_KEY, -1);
	CHECK(g.taken[FOREST][0] && !g.key && g.score == edit_score && g.lives == edit_lives);
	scenarios++; /* Selection wrap, tabs/help and rendering every state-editor row. */
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_TAB, false);
	game_key(&g, SDL_SCANCODE_UP, false);
	CHECK(g.settings_row == SET_CROWN);
	game_key(&g, SDL_SCANCODE_DOWN, false);
	CHECK(g.settings_row == SET_COLA);
	game_key(&g, SDL_SCANCODE_F1, false);
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.help_page == 1 && g.settings_row == SET_COLA);
	game_key(&g, SDL_SCANCODE_F1, false);
	CHECK(!g.help_open && g.location_menu && g.settings_page == 1);
	for (int row = 0; row < SETTINGS_COUNT; row++) {
		CHECK(g.settings_row == row);
		game_draw(&g);
		game_key(&g, SDL_SCANCODE_DOWN, false);
	}
	CHECK(g.settings_row == 0);
	game_key(&g, SDL_SCANCODE_F4, false);
	CHECK(!g.location_menu && !g.paused);
	game_key(&g, SDL_SCANCODE_F4, false);
	CHECK(g.settings_page == 1);
	game_key(&g, SDL_SCANCODE_TAB, false);
	CHECK(g.settings_page == 2);
	game_key(&g, SDL_SCANCODE_TAB, false);
	CHECK(g.settings_page == 3);
	game_key(&g, SDL_SCANCODE_TAB, false);
	CHECK(g.settings_page == 0);
	game_draw(&g);
	int entry_cola = g.cola;
	bool entry_key = g.key;
	game_key(&g, SDL_SCANCODE_2, false);
	CHECK(g.area == LAKE && !g.location_menu && g.cola == entry_cola && g.key == entry_key);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_PAGEDOWN, false);
	CHECK(g.settings_page == 1);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	CHECK(!g.location_menu && g.paused);
	scenarios++; /* Transparent frame padding cannot hurt; poses share renderer masks. */
	reset(&g, r);
	g.x = 100;
	g.y = 80;
	uint16_t player_pixels[320] = {0}, enemy_pixels[4096] = {0};
	uint16_t *saved_player_pixels = r->player.opaque, *saved_enemy_pixels = r->shapes[8].opaque;
	r->player.opaque = player_pixels;
	r->shapes[8].opaque = enemy_pixels;
	player_pixels[10] = (uint16_t)(1u << 8);
	enemy_pixels[0] = 1;
	Enemy contact = {.x = 100, .y = 80, .shape = 8, .scene = 0, .alive = true};
	CHECK(box_overlap((Box){g.x, g.y, 16, 32}, (Box){contact.x, contact.y, 16, 16}));
	CHECK(!game_enemy_contact(&g, &contact));
	contact.x = 108;
	contact.y = 90;
	CHECK(game_enemy_contact(&g, &contact));
	contact.x = 109;
	CHECK(!game_enemy_contact(&g, &contact));
	contact.x = 108.49;
	CHECK(game_enemy_contact(&g, &contact));
	contact.x = 108.51;
	CHECK(!game_enemy_contact(&g, &contact));
	contact.x = 108;
	contact.alive = false;
	CHECK(!game_enemy_contact(&g, &contact));
	contact.alive = true;
	contact.scene = 1;
	CHECK(!game_enemy_contact(&g, &contact));
	contact.scene = 0;
	g.spawn_time = .5;
	CHECK(!game_enemy_contact(&g, &contact));
	g.spawn_time = 0;
	g.y = 155;
	contact.y = 165;
	CHECK(!game_enemy_contact(&g, &contact));
	g.y = 80;
	contact.y = 90;
	g.moving = true;
	g.time = .1;
	CHECK(game_player_frame(&g) == 2);
	CHECK(!game_enemy_contact(&g, &contact));
	player_pixels[2 * 32 + 10] = (uint16_t)(1u << 8);
	CHECK(game_enemy_contact(&g, &contact));
	g.face = -1;
	CHECK(game_player_frame(&g) == 7);
	CHECK(!game_enemy_contact(&g, &contact));
	player_pixels[7 * 32 + 10] = (uint16_t)(1u << 8);
	CHECK(game_enemy_contact(&g, &contact));
	r->player.opaque = saved_player_pixels;
	r->shapes[8].opaque = saved_enemy_pixels;
	scenarios++; /* Actual enemy banks at a distance, contact damage and grace period. */
	reset(&g, r);
	CHECK(r->areas[FOREST].scenes[0].enemy_count > 0);
	for (int shape = 0; shape < SHAPE_COUNT; shape++) {
		CHECK(r->shapes[shape].opaque != NULL);
		Enemy far = {.x = g.x + 64,
			.y = g.y,
			.shape = shape,
			.scene = 0,
			.alive = true,
			.timer = 1e9};
		CHECK(!game_enemy_contact(&g, &far));
		g.enemies[FOREST][0][0] = far;
		g.invincible = 0;
		game_update(&g, STEP, (Controls){0});
		CHECK(g.hp == 6);
		quiet(&g);
	}
	reset(&g, r);
	Enemy touching = {
		.x = g.x, .y = g.y + 8, .shape = 8, .scene = 0, .alive = true, .timer = 1e9};
	CHECK(game_enemy_contact(&g, &touching));
	g.enemies[FOREST][0][0] = touching;
	game_update(&g, STEP, (Controls){0});
	CHECK(g.hp == 5 && g.invincible > 0);
	CHECK(!g.enemies[FOREST][0][0].alive && g.enemies[FOREST][0][0].timer == 2.);
	CHECK(g.score == 0);
	game_update(&g, STEP, (Controls){0});
	CHECK(g.hp == 5);
	reset(&g, r);
	touching = (Enemy){
		.x = g.x, .y = g.y + 8, .shape = 8, .scene = 1, .alive = true, .timer = 1e9};
	g.enemies[FOREST][0][0] = touching;
	game_update(&g, STEP, (Controls){0});
	CHECK(g.hp == 6);
	for (int i = 1; i <= 3; i++) {
		Enemy facing = {.shape = i, .vx = -1};
		g.time = 0;
		CHECK(game_enemy_frame(&g, &facing) == 0);
		facing.vx = 1;
		CHECK(game_enemy_frame(&g, &facing) >= 0 && game_enemy_frame(&g, &facing) < r->shapes[i].count);
	}
	scenarios++; /* Contact consumes the enemy even during damage immunity; normal respawn. */
	reset(&g, r);
	g.invincible = 1.;
	touching = (Enemy){
		.x = g.x, .y = g.y + 8, .shape = 8, .scene = 0, .alive = true, .timer = 1e9};
	g.enemies[FOREST][0][0] = touching;
	game_update(&g, STEP, (Controls){0});
	CHECK(g.hp == 6 && !g.enemies[FOREST][0][0].alive && g.score == 0);
	tick(&g, 120, (Controls){0});
	CHECK(!g.enemies[FOREST][0][0].alive);
	tick(&g, 122, (Controls){0});
	CHECK(g.enemies[FOREST][0][0].alive);
	CHECK(!game_enemy_contact(&g, &g.enemies[FOREST][0][0]) && g.hp == 6);
	reset(&g, r);
	g.hp = 0;
	g.lives = 2;
	touching = (Enemy){
		.x = g.x, .y = g.y + 8, .shape = 8, .scene = 0, .alive = true, .timer = 1e9};
	g.enemies[FOREST][0][0] = touching;
	game_update(&g, STEP, (Controls){0});
	CHECK(g.lives == 1 && g.death_time > 0 && !g.enemies[FOREST][0][0].alive);
	game_update(&g, DEATH_DURATION, (Controls){0});
	CHECK(g.spawn_time == 0 && g.death_time == 0);
	reset(&g, r);
	g.hp = 0;
	g.lives = 1;
	g.enemies[FOREST][0][0] = touching;
	game_update(&g, STEP, (Controls){0});
	CHECK(!g.over && g.death_time > 0 && g.lives == 0 && !g.enemies[FOREST][0][0].alive);
	game_update(&g, DEATH_DURATION, (Controls){0});
	CHECK(g.over && g.death_time == 0);
	scenarios++; /* Eight original death frames, blank pause, then direct respawn or game over. */
	CHECK(r->death.count == 8 && r->death.height == 32);
	CHECK(r->teleport.count == 3 && r->teleport.height == 32);
	reset(&g, r);
	g.x = 256;
	g.hp = 0;
	g.lives = 2;
	game_hurt(&g, false);
	for (int frame = 0; frame < 8; frame++) {
		CHECK(game_death_frame(&g) == frame);
		CHECK(g.x == 256 && g.lives == 1 && !g.over && g.spawn_time == 0);
		game_draw(&g);
		game_hurt(&g, true);
		game_jump(&g);
		CHECK(!game_teleport(&g));
		game_update(&g, WEAPON_TICK, (Controls){.right = true, .fire = true});
	}
	CHECK(fabs(g.death_time - DEATH_PAUSE) < 1e-8 && g.x == 256);
	CHECK(game_death_frame(&g) == -1 && g.spawn_time == 0);
	game_update(&g, DEATH_PAUSE, (Controls){.right = true});
	CHECK(g.death_time == 0 && g.spawn_time == 0 && g.x == 104);
	reset(&g, r);
	g.lives = 1;
	g.hp = 0;
	game_hurt(&g, false);
	g.paused = true;
	game_draw(&g);
	game_update(&g, DEATH_DURATION, (Controls){0});
	CHECK(g.death_time == DEATH_DURATION && !g.over);
	g.paused = false;
	game_update(&g, DEATH_DURATION, (Controls){0});
	CHECK(g.over && g.spawn_time == 0);
	game_draw(&g);

	scenarios++; /* Six-stage teleport: source/destination overlap and midpoint transfer. */
	reset(&g, r);
	g.wand = true;
	CHECK(game_teleport(&g));
	double tx = g.teleport_to_x, ty = g.teleport_to_y;
	for (int stage = 0; stage < 6; stage++) {
		CHECK(game_effect_frame(g.teleport_time, 6) == stage);
		CHECK(g.x == (stage < 3 ? 104 : tx));
		game_draw(&g);
		CHECK(!game_teleport(&g));
		game_hurt(&g, false);
		game_update(&g, WEAPON_TICK, (Controls){.left = true, .jump = true});
	}
	CHECK(g.teleport_time == 0 && g.x == tx && g.y == ty && g.shot_count == 0);
	CHECK(g.hp == 6 && g.lives == 5);
	scenarios++; /* Three F4 pages, key capture, duplicate rejection, cancel and reset. */
	KeyBindings saved_bindings = r->bindings;
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_PAGEUP, false);
	CHECK(g.settings_page == 3);
	game_key(&g, SDL_SCANCODE_PAGEUP, false);
	CHECK(g.settings_page == 2);
	game_draw(&g);
	game_key(&g, SDL_SCANCODE_RETURN, false);
	CHECK(g.binding_wait);
	game_key(&g, SDL_SCANCODE_F4, false);
	CHECK(g.binding_wait && g.location_menu);
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.binding_wait && r->bindings.key[BIND_LEFT][0] == SDL_SCANCODE_LEFT);
	game_key(&g, SDL_SCANCODE_J, true);
	CHECK(g.binding_wait);
	game_key(&g, SDL_SCANCODE_J, false);
	CHECK(!g.binding_wait && r->bindings.key[BIND_LEFT][0] == SDL_SCANCODE_J);
	game_key(&g, SDL_SCANCODE_DOWN, false);
	game_key(&g, SDL_SCANCODE_RETURN, false);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	CHECK(!g.binding_wait && g.location_menu && g.binding_row == BIND_RIGHT);
	game_key(&g, SDL_SCANCODE_RETURN, false);
	game_key(&g, SDL_SCANCODE_TAB, false);
	CHECK(!g.binding_wait && g.settings_page == 2 &&
		r->bindings.key[BIND_RIGHT][0] == SDL_SCANCODE_TAB);
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	game_key(&g, SDL_SCANCODE_BACKSPACE, false);
	CHECK(r->bindings.key[BIND_RIGHT][1] == SDL_SCANCODE_UNKNOWN);
	game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_F1, false);
	game_draw(&g);
	game_key(&g, SDL_SCANCODE_F4, false);
	CHECK(!g.help_open && g.location_menu);
	game_reset(&g, r);
	CHECK(r->bindings.key[BIND_LEFT][0] == SDL_SCANCODE_J);
	g.title = false;
	g.location_menu = true;
	g.settings_page = 2;
	game_key(&g, SDL_SCANCODE_R, false);
	CHECK(!memcmp(&r->bindings, &saved_bindings, sizeof saved_bindings));

	scenarios++; /* Both continuous and edge-triggered actions follow the same bindings. */
	CHECK(bindings_assign(&r->bindings, BIND_JUMP, 0, SDL_SCANCODE_C));
	CHECK(bindings_assign(&r->bindings, BIND_FIRE, 0, SDL_SCANCODE_V));
	CHECK(bindings_assign(&r->bindings, BIND_PAUSE, 0, SDL_SCANCODE_F6));
	CHECK(bindings_assign(&r->bindings, BIND_TELEPORT, 0, SDL_SCANCODE_TAB));
	Uint8 key_state[SDL_NUM_SCANCODES] = {0};
	key_state[SDL_SCANCODE_SPACE] = key_state[SDL_SCANCODE_KP_0] = 1;
	Controls held = bindings_controls(&r->bindings, key_state, SDL_NUM_SCANCODES);
	CHECK(!held.jump && !held.fire);
	key_state[SDL_SCANCODE_C] = key_state[SDL_SCANCODE_V] = 1;
	held = bindings_controls(&r->bindings, key_state, SDL_NUM_SCANCODES);
	CHECK(held.jump && held.fire);
	CHECK(!bindings_controls(&r->bindings, key_state, 0).fire);
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_SPACE, false);
	CHECK(g.grounded && g.vy == 0);
	game_key(&g, SDL_SCANCODE_C, false);
	CHECK(!g.grounded && g.vy < 0);
	game_key(&g, SDL_SCANCODE_P, false);
	CHECK(!g.paused);
	game_key(&g, SDL_SCANCODE_F6, false);
	CHECK(g.paused);
	game_draw(&g);
	game_key(&g, SDL_SCANCODE_F6, false);
	CHECK(!g.paused);
	reset(&g, r);
	g.wand = true;
	game_key(&g, SDL_SCANCODE_TAB, false);
	CHECK(g.teleport_time > 0);
	CHECK(bindings_assign(&r->bindings, BIND_FIRE, 1, SDL_SCANCODE_J));
	CHECK(!bindings_assign(&r->bindings, BIND_FIRE, 1, SDL_SCANCODE_F1));
	CHECK(!bindings_assign(&r->bindings, BIND_FIRE, 1, SDL_SCANCODE_Q));
	CHECK(!bindings_assign(&r->bindings, -1, 0, SDL_SCANCODE_B));
	char binding_text[128];
	bindings_describe(&r->bindings, BIND_JUMP, binding_text, sizeof binding_text);
	CHECK(strstr(binding_text, "C") && !strstr(binding_text, "Пробел"));

	scenarios++; /* Atomic config round-trip; invalid input leaves live bindings unchanged. */
#ifndef _WIN32
	char temp_dir[] = "/tmp/cc-keys-test-XXXXXX", config[256];
	bool made_dir = mkdtemp(temp_dir) != NULL;
	CHECK(made_dir);
	if (made_dir) {
		snprintf(config, sizeof config, "%s/keys.cfg", temp_dir);
		CHECK(bindings_save(&r->bindings, config));
		KeyBindings loaded;
		bindings_defaults(&loaded);
		CHECK(bindings_load(&loaded, config));
		CHECK(!memcmp(&loaded, &r->bindings, sizeof loaded));
		KeyBindings legacy = r->bindings;
		legacy.key[BIND_PAUSE][0] = SDL_SCANCODE_F5;
		CHECK(bindings_save(&legacy, config));
		CHECK(bindings_load(&loaded, config));
		legacy.key[BIND_PAUSE][0] = SDL_SCANCODE_UNKNOWN;
		CHECK(!memcmp(&loaded, &legacy, sizeof loaded));
		loaded = r->bindings;
		const char *bad[] = {"CCKEYS1\n99999999999999999999999999999\n", "CCKEYS1\n4 4 0\n",
			"CCKEYS1\n-1\n", "CCKEYS2\n", "CCKEYS1\n1junk\n"};
		for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
			FILE *f = fopen(config, "w");
			CHECK(f != NULL);
			if (f) {
				fputs(bad[i], f);
				fclose(f);
			}
			CHECK(!bindings_load(&loaded, config));
			CHECK(!memcmp(&loaded, &r->bindings, sizeof loaded));
		}
		CHECK(!bindings_save(&loaded, temp_dir));
		unlink(config);
		rmdir(temp_dir);
	}
#endif
	r->bindings = saved_bindings;
	scenarios++; /* Video/Revision 5: 2000 per item, 300 per shot kill, spare-life cap. */
	for (int kind = 0; kind <= 14; kind++) {
		if (kind == 13 || kind == 14 || kind == CROWN)
			continue;
		reset(&g, r);
		game_collect(&g, kind);
		CHECK(g.score == 2000);
	}
	reset(&g, r);
	g.cola = 1;
	g.enemies[FOREST][0][0] = (Enemy){
		.x = g.x + 30, .y = g.y + 14, .shape = 8, .scene = 0, .alive = true, .timer = 1e9};
	g.shot_count = 1;
	g.shots[0] = (Shot){g.x + 31, g.y + 15, g.y + 15, 1.6, 0, 1};
	game_update(&g, STEP, (Controls){0});
	CHECK(g.score == 300 && !g.enemies[FOREST][0][0].alive && g.shot_count == 0);
	reset(&g, r);
	g.lives = MAX_GAME_LIVES;
	game_collect(&g, GEMS);
	CHECK(g.score == 24500 && g.lives == MAX_GAME_LIVES && g.hp == 6);
	reset(&g, r);
	g.lives = MAX_GAME_LIVES;
	g.hp = 2;
	game_collect(&g, SHIELD);
	CHECK(g.score == 2000 && g.lives == MAX_GAME_LIVES && g.hp == 6);
	game_collect(&g, SHIELD);
	CHECK(g.score == 26500 && g.lives == MAX_GAME_LIVES && g.hp == 6);
	reset(&g, r);
	g.lives = MAX_GAME_LIVES;
	g.score = 49900;
	g.hp = 2;
	game_collect(&g, SHIELD);
	CHECK(g.score == 74400 && g.hp == 6 && g.lives == MAX_GAME_LIVES);
	reset(&g, r);
	g.lives = MAX_GAME_LIVES;
	g.score = 99900;
	g.hp = 0;
	game_score(&g, 100);
	CHECK(g.score == 122500 && g.hp == 6 && g.lives == MAX_GAME_LIVES);
	reset(&g, r);
	g.lives = MAX_GAME_LIVES;
	g.score = 40000;
	game_collect(&g, GEMS);
	CHECK(g.score == 87000 && g.lives == MAX_GAME_LIVES && g.hp == 6);
	reset(&g, r);
	g.lives = MAX_EDITOR_LIVES;
	game_collect(&g, GEMS);
	CHECK(g.lives == MAX_EDITOR_LIVES && g.score == 24500);
	scenarios++; /* Key-down fires immediately, independently of the energy clock phase. */
	for (int fire_phase = 0; fire_phase < 16; fire_phase++) {
		reset(&g, r);
		g.cola = 5;
		g.weapon_clock = fire_phase * WEAPON_TICK / 16;
		game_key(&g, SDL_SCANCODE_INSERT, false);
		CHECK(g.shot_count == 1 && g.time == 0 && g.weapon_pulse);
		game_key(&g, SDL_SCANCODE_KP_0, false);
		game_key(&g, SDL_SCANCODE_INSERT, true);
		CHECK(g.shot_count == 1);
		game_update(&g, STEP, (Controls){.fire = true});
		CHECK(g.shot_count == 1);
	}
	scenarios++; /* A down/up tap between state polls is retained and costs energy. */
	reset(&g, r);
	g.cola = 1;
	g.weapon_clock = 0;
	game_key(&g, SDL_SCANCODE_KP_0, false);
	game_update(&g, STEP, (Controls){0});
	CHECK(g.shot_count == 1 && g.weapon_pulse && g.energy == 12);
	game_update(&g, WEAPON_TICK - STEP, (Controls){0});
	CHECK(g.shot_count == 1 && !g.weapon_pulse && g.energy == 11);
	game_update(&g, WEAPON_TICK, (Controls){0});
	CHECK(g.energy == 12);

	scenarios++; /* Held fire keeps a cadence, without a second shot on the first clock tick. */
	reset(&g, r);
	g.cola = 5;
	g.weapon_clock = 0;
	game_key(&g, SDL_SCANCODE_INSERT, false);
	tick(&g, 12, (Controls){.fire = true});
	CHECK(g.shot_count == 1);
	tick(&g, 2, (Controls){.fire = true});
	CHECK(g.shot_count == 2);
	tick(&g, 60, (Controls){.fire = true});
	CHECK(g.shot_count == 5);

	scenarios++; /* Modal/transition guards, ammo/energy/slot limits and remapped fire. */
	for (int state = 0; state < 12; state++) {
		reset(&g, r);
		g.cola = 1;
		switch (state) {
		case 0:
			g.paused = true;
			break;
		case 1:
			g.title = true;
			break;
		case 2:
			g.over = true;
			break;
		case 3:
			g.won = true;
			break;
		case 4:
			g.help_open = true;
			break;
		case 5:
			g.location_menu = true;
			break;
		case 6:
			g.spawn_time = ARRIVAL_DURATION;
			break;
		case 7:
			g.death_time = DEATH_DURATION;
			break;
		case 8:
			g.teleport_time = TELEPORT_DURATION;
			break;
		case 9:
			g.cola = 0;
			break;
		case 10:
			g.energy = 0;
			break;
		case 11:
			g.shot_count = 1;
			g.shots[0] = (Shot){g.x + 32, g.y + 14, g.y + 14, 1, 0, 1};
			break;
		}
		int before = g.shot_count;
		game_key(&g, SDL_SCANCODE_INSERT, false);
		CHECK(g.shot_count == before && !g.weapon_pulse);
	}
	KeyBindings original_keys = r->bindings;
	CHECK(bindings_assign(&r->bindings, BIND_FIRE, 0, SDL_SCANCODE_V));
	reset(&g, r);
	g.cola = 1;
	game_key(&g, SDL_SCANCODE_KP_0, false);
	CHECK(g.shot_count == 0);
	game_key(&g, SDL_SCANCODE_V, false);
	CHECK(g.shot_count == 1);
	game_travel(&g, FOREST, 104, 96);
	CHECK(g.shot_count == 0 && g.shot_cooldown == 0 && !g.weapon_pulse);
	r->bindings = original_keys;
	scenarios++; /* High ledges: the complete player sprite stays below the viewport top. */
	const int upper_areas[] = {FOREST, MOON};
	for (int a = 0; a < 2; a++) {
		int area = upper_areas[a];
		uint8_t upper_cells[1280];
		memcpy(upper_cells, r->areas[area].cells[0], sizeof upper_cells);
		for (int boots = 0; boots < 2; boots++)
			for (int held_jump = 0; held_jump < 2; held_jump++)
				for (int ledge = 0; ledge < 2; ledge++)
					for (int direction = -1; direction <= 1; direction += 2) {
						memset(r->areas[area].cells[0], 0,
							sizeof upper_cells);
						for (int x = 0; x < 128; x++)
							r->areas[area]
								.cells[0][(ledge + 2) * 128 + x] =
								(uint8_t)(r->areas[area]
										  .last_passable +
									  1);
						reset(&g, r);
						g.area = area;
						g.x = 256;
						g.y = ledge * 16;
						g.boots = boots != 0;
						game_jump(&g);
						bool touched_top = false, landed_upper = false;
						for (int frame = 0; frame < 240; frame++) {
							game_update(&g, STEP,
								(Controls){.left = direction < 0,
									.right = direction > 0,
									.jump = held_jump != 0});
							CHECK(g.y >= 0);
							CHECK(!box_hits(r, area,
								(Box){g.x + 3, g.y + 2, 10, 30}));
							touched_top |= g.y == 0;
							if (g.grounded) {
								landed_upper = true;
								break;
							}
						}
						CHECK(touched_top && landed_upper &&
							g.y == ledge * 16);
						CHECK(g.lives == 5 && (g.x - 256) * direction > 0);
					}
		memcpy(r->areas[area].cells[0], upper_cells, sizeof upper_cells);
	}
	scenarios++; /* Cave scene 2: raised pillar below a low ceiling, wearing boots. */
	CHECK(tile_solid(r, CAVE, 2 * 128 + 56, 6));
	CHECK(tile_solid(r, CAVE, 2 * 128 + 59, 9));
	CHECK(tile_solid(r, CAVE, 2 * 128 + 60, 3));
	for (int start_x = 5040; start_x <= 5044; start_x += 2)
		for (int turn_delay = 0; turn_delay <= 36; turn_delay += 4) {
			reset(&g, r);
			g.area = CAVE;
			g.x = start_x;
			g.y = 112;
			g.boots = true;
			game_jump(&g);
			bool reached_pillar = false;
			for (int frame = 0; frame < 180; frame++) {
				game_update(&g, STEP,
					(Controls){.left = frame >= turn_delay && g.x > 4996,
						.jump = true});
				CHECK(!box_hits(r, CAVE, (Box){g.x + 3, g.y + 2, 10, 30}));
				if (g.grounded && g.y == 64 && g.x < 5005 && g.x + 13 > 4992) {
					reached_pillar = true;
					break;
				}
			}
			CHECK(reached_pillar && g.lives == 5);
		}
	scenarios++; /* Door animation has source and destination phases; no teleport sparkles. */
	reset(&g, r);
	g.key = g.wand = true;
	game_interact(&g);
	CHECK(g.door_time == DOOR_DURATION && g.area == FOREST);
	g.paused = true;
	game_update(&g, DOOR_DURATION, (Controls){.fire = true});
	CHECK(g.door_time == DOOR_DURATION && g.area == FOREST);
	g.paused = false;
	for (int stage = 0; stage < 12; stage++) {
		CHECK(game_effect_frame(g.door_time, 12) == stage);
		CHECK(g.area == (stage < 4 ? FOREST : CASTLE));
		CHECK(g.x == (stage < 4 ? 104 : 1976));
		CHECK(g.spawn_time == 0 && g.teleport_time == 0);
		double remaining_door = g.door_time;
		game_key(&g, SDL_SCANCODE_INSERT, false);
		game_jump(&g);
		game_hurt(&g, true);
		game_interact(&g);
		CHECK(!game_teleport(&g));
		CHECK(g.door_time == remaining_door && g.hp == 6 && g.lives == 5 &&
			g.shot_count == 0 && g.vy == 0);
		game_draw(&g);
		game_update(&g, WEAPON_TICK, (Controls){.right = true, .fire = true});
	}
	CHECK(g.door_time == 0 && g.area == CASTLE && g.x == g.checkpoint_x);
	game_interact(&g);
	CHECK(g.door_time > 0);
	game_explore(&g, LAKE, 0);
	CHECK(g.area == LAKE && g.door_time == 0);
	scenarios++; /* Localised full-screen ending uses normal victory and navigation. */
	CHECK(r->ending_logo != NULL);
	reset(&g, r);
	game_collect(&g, GEMS);
	game_collect(&g, GOLD_ITEM);
	CHECK(!g.won);
	game_collect(&g, CROWN);
	CHECK(g.won || g.ending_stage != ENDING_NONE);
	g.ending_stage = ENDING_NONE;
	g.won = true;
	game_draw(&g);
	double ending_x = g.x;
	game_update(&g, 1, (Controls){.right = true, .fire = true});
	CHECK(g.x == ending_x && g.won);
	CHECK(game_key(&g, SDL_SCANCODE_F1, false) == 0);
	CHECK(g.scores_open && g.score_entry && !g.help_open && g.won);
	game_draw(&g);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	CHECK(game_key(&g, SDL_SCANCODE_RETURN, false) == 1);
	CHECK(g.scores_open && g.won);
	scenarios++; /* Top ten ranking, ties and rejection of invalid score/name records. */
	ScoreTable table = {0};
	for (int score = 1000; score <= 10000; score += 1000)
		CHECK(score_add(&table, score, "Игрок"));
	CHECK(table.count == 10 && table.entries[0].score == 10000 &&
		table.entries[9].score == 1000);
	CHECK(!score_qualifies(&table, 1000) && score_qualifies(&table, 1001));
	CHECK(score_add(&table, 1001, "Макс") && table.entries[9].score == 1001);
	CHECK(score_add(&table, 11000, "Победитель") && table.entries[0].score == 11000);
	CHECK(score_add(&table, 10000, "Повтор"));
	CHECK(table.entries[1].score == 10000 && !strcmp(table.entries[2].name, "Повтор"));
	CHECK(!score_add(&table, -1, "Игрок"));
	CHECK(!score_add(&table, 0, "Игрок"));
	CHECK(!score_add(&table, 1000000000, "Игрок"));
	CHECK(!score_add(&table, 20000, "Имя\tс табуляцией"));
	CHECK(!score_add(&table, 20000, "\xc0\xaf"));

	scenarios++; /* Russian text input, UTF-8 backspace, persistence and one submission per game. */
	ScoreTable previous_scores = r->scores;
	char previous_score_path[sizeof r->score_path];
	memcpy(previous_score_path, r->score_path, sizeof previous_score_path);
	memset(&r->scores, 0, sizeof r->scores);
#ifndef _WIN32
	char score_dir[] = "/tmp/cc-scores-test-XXXXXX";
	bool made_score_dir = mkdtemp(score_dir) != NULL;
	CHECK(made_score_dir);
	if (made_score_dir) {
		snprintf(r->score_path, sizeof r->score_path, "%s/scores.dat", score_dir);
		reset(&g, r);
		g.score = 12345;
		g.over = true;
		game_key(&g, SDL_SCANCODE_RETURN, false);
		CHECK(g.scores_open && g.score_entry);
		CHECK(game_key(&g, SDL_SCANCODE_Q, false) == 0);
		game_score_text(&g, "Максим");
		game_key(&g, SDL_SCANCODE_BACKSPACE, false);
		CHECK(!strcmp(g.score_name, "Макси"));
		game_score_text(&g, "м");
		game_draw(&g);
		game_key(&g, SDL_SCANCODE_RETURN, false);
		CHECK(!g.score_entry && g.score_submitted && r->scores.count == 1);
		CHECK(!strcmp(r->scores.entries[0].name, "Максим") &&
			r->scores.entries[0].score == 12345);
		ScoreTable loaded_scores = {0};
		CHECK(scores_load(&loaded_scores, r->score_path));
		CHECK(!memcmp(&loaded_scores, &r->scores, sizeof loaded_scores));
		game_draw(&g);
		CHECK(game_key(&g, SDL_SCANCODE_ESCAPE, false) == 1);
		CHECK(g.scores_open && g.over);
		CHECK(game_key(&g, SDL_SCANCODE_RETURN, false) == 1);
		CHECK(g.scores_open && !g.score_entry && r->scores.count == 1);
		game_reset(&g, r);
		CHECK(g.title && !g.over && r->scores.count == 1);
		game_key(&g, SDL_SCANCODE_R, false);
		CHECK(g.scores_open && !g.score_entry && !g.score_after_game);
		game_key(&g, SDL_SCANCODE_ESCAPE, false);
		CHECK(!g.scores_open && g.title);
		reset(&g, r);
		g.score = 15000;
		g.won = true;
		game_scores_open(&g, true);
		game_score_text(&g, "ЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯ");
		CHECK(strlen(g.score_name) == 32);
		game_key(&g, SDL_SCANCODE_BACKSPACE, true);
		CHECK(strlen(g.score_name) == 30);
		game_key(&g, SDL_SCANCODE_ESCAPE, false);
		CHECK(r->scores.count == 1 && g.score_submitted && !g.score_entry);
		reset(&g, r);
		g.score = 16000;
		g.over = true;
		game_scores_open(&g, true);
		game_score_text(&g, "   ");
		game_key(&g, SDL_SCANCODE_RETURN, false);
		CHECK(r->scores.count == 2 && !strcmp(r->scores.entries[0].name, "Игрок"));
		const char *bad_scores[] = {"CCSCORES2\n", "CCSCORES1\n-1\tИгрок\n",
			"CCSCORES1\n9999999999999999999999999\tИгрок\n",
			"CCSCORES1\n5\tИмя\tплохое\n", "CCSCORES1\n5\tИгрок\n10\tДругой\n"};
		ScoreTable loaded_before = loaded_scores;
		for (size_t n = 0; n < sizeof bad_scores / sizeof bad_scores[0]; n++) {
			FILE *f = fopen(r->score_path, "w");
			CHECK(f != NULL);
			if (f) {
				fputs(bad_scores[n], f);
				fclose(f);
			}
			bool loaded_ok = scores_load(&loaded_scores, r->score_path);
			if (n + 1 == sizeof bad_scores / sizeof bad_scores[0]) {
				CHECK(loaded_ok && loaded_scores.count == 2 && loaded_scores.entries[0].score == 10);
			} else {
				CHECK(!loaded_ok);
				CHECK(!memcmp(&loaded_scores, &loaded_before, sizeof loaded_before));
			}
		}
		CHECK(!scores_save(&r->scores, score_dir));
		unlink(r->score_path);
		rmdir(score_dir);
	}
#endif
	r->scores = previous_scores;
	memcpy(r->score_path, previous_score_path, sizeof previous_score_path);

	scenarios++; /* Same elapsed time produces the same pace in every panorama. */
	for (int area = 0; area < AREA_COUNT; area++) {
		uint8_t saved_cells[3][1280];
		memcpy(saved_cells, r->areas[area].cells, sizeof saved_cells);
		memset(r->areas[area].cells, 0, sizeof saved_cells);
		for (int scene = 0; scene < 3; scene++) {
			memset(r->areas[area].cells[scene] + 9 * 128,
				r->areas[area].last_passable + 1, 128);
			for (int fps = 30; fps <= 240; fps *= 2) {
				reset(&g, r);
				g.area = area;
				g.x = scene * 2048. + 512;
				g.y = 112;
				g.invincible = 1e9;
				game_camera(&g);
				double start = g.x;
				Enemy *pace = &g.enemies[area][scene][0];
				*pace = (Enemy){.x = start + 200,
					.y = 64,
					.shape = 1,
					.scene = scene,
					.behavior = 4,
					.alive = true};
				for (int frame = 0; frame < fps; frame++)
					game_update(&g, 1. / fps, (Controls){.right = true});
				CHECK(fabs(g.x - start - 80) < 1e-7);
				CHECK(pace->alive && fabs(pace->x - start - 200 + 40) < 1e-7);
				CHECK(fabs(g.time - 1) < 1e-7);
			}
		}
		memcpy(r->areas[area].cells, saved_cells, sizeof saved_cells);
	}
	scenarios++; /* Corkscrew uses the original 8px two-height path, in both directions. */
	for (int facing = -1; facing <= 1; facing += 2) {
		reset(&g, r);
		g.x = 600;
		g.y = 32;
		g.corkscrew = true;
		g.shot_count = 1;
		g.shots[0] = (Shot){600, 46, 46, 10, 0, facing};
		game_camera(&g);
		for (int sample = 1; sample <= 32; sample++) {
			game_update(&g, WEAPON_TICK / 16, (Controls){0});
			CHECK(g.shot_count == 1);
			CHECK(g.shots[0].y >= 46 - 1e-8 && g.shots[0].y <= 54 + 1e-8);
			if (sample == 16)
				CHECK(fabs(g.shots[0].y - 54) < 1e-8);
			if (sample == 32)
				CHECK(fabs(g.shots[0].y - 46) < 1e-8);
		}
	}
	scenarios++; /* Map behaviour is honoured independently of the sprite graphic. */
	{
		uint8_t saved_cells[3][1280];
		memcpy(saved_cells, r->areas[FOREST].cells, sizeof saved_cells);
		memset(r->areas[FOREST].cells, 0, sizeof saved_cells);
		memset(r->areas[FOREST].cells[0] + 9 * 128, r->areas[FOREST].last_passable + 1,
			128);
		for (int behavior = 1; behavior <= 5; behavior++) {
			reset(&g, r);
			g.x = 1000;
			g.y = 112;
			g.face = 1;
			game_camera(&g);
			Enemy *e = &g.enemies[FOREST][0][0];
			*e = (Enemy){.x = 1100,
				.y = 64,
				.shape = 2,
				.scene = 0,
				.behavior = behavior,
				.alive = true,
				.vx = -45};
			game_update(&g, 5 * WEAPON_TICK, (Controls){0});
			CHECK(e->alive && e->x < 1100);
			CHECK(!box_hits(r, FOREST, (Box){e->x, e->y, 16, 16}));
			if (behavior == 1 || behavior == 5)
				CHECK(e->y < 64);
			else if (behavior == 4)
				CHECK(e->y == 64);
			else
				CHECK(e->y > 64);
			if (behavior == 5) {
				g.face = -1;
				double y = e->y;
				game_update(&g, 2 * WEAPON_TICK, (Controls){0});
				CHECK(e->y > y);
			}
		}
		reset(&g, r);
		g.x = 1000;
		g.y = 112;
		game_camera(&g);
		Enemy *e = &g.enemies[FOREST][0][0];
		*e = (Enemy){.x = 1100,
			.y = 128,
			.shape = 2,
			.scene = 0,
			.behavior = 2,
			.alive = true,
			.vx = 45};
		game_update(&g, WEAPON_TICK, (Controls){0});
		CHECK(e->vx < 0 && e->vy < 0); /* Choose Comic's direction on landing. */
		e->behavior = 127;
		game_update(&g, 2, (Controls){0});
		CHECK(!e->alive);
		e->behavior = 4;
		e->timer = 0;
		game_update(&g, STEP, (Controls){0});
		CHECK(e->alive && !box_hits(r, FOREST, (Box){e->x, e->y, 16, 16}));
		CHECK(!game_enemy_contact(&g, e));
		memcpy(r->areas[FOREST].cells, saved_cells, sizeof saved_cells);
	}

	scenarios++; /* Shot/contact kills share the original five white explosion phases. */
	CHECK(r->enemy_hit.count == 3 && r->enemy_hit.height == 16);
	for (int cause = 0; cause < 3; cause++) {
		reset(&g, r);
		g.x = 600;
		g.y = 80;
		g.grounded = false;
		g.invincible = cause == 2 ? 1 : 0;
		g.hp = cause == 1 ? 0 : 6;
		game_camera(&g);
		Enemy *e = &g.enemies[FOREST][0][0];
		*e = (Enemy){.x = g.x,
			.y = g.y + 8,
			.shape = 8,
			.scene = 0,
			.behavior = 4,
			.alive = true};
		if (cause == 0) {
			g.shot_count = 1;
			g.shots[0] = (Shot){g.x, g.y + 8, g.y + 8, 1.6, 0, 1};
		}
		game_update(&g, STEP, (Controls){0});
		CHECK(!e->alive && e->hit_time > 0);
		CHECK(!game_enemy_contact(&g, e));
		CHECK(g.score == (cause == 0 ? 300 : 0));
		CHECK(cause != 1 || g.death_time > 0);
		CHECK(cause != 2 || g.hp == 6);
		double x = e->x, y = e->y, remaining = e->hit_time;
		g.paused = true;
		game_update(&g, 1, (Controls){0});
		CHECK(e->hit_time == remaining);
		g.paused = false;
		static const int phases[] = {0, 1, 2, 1, 0};
		for (int stage = 0; stage < 5; stage++) {
			e->hit_time = (5 - stage) * WEAPON_TICK;
			CHECK(game_enemy_hit_frame(e) == phases[stage]);
			game_draw(&g);
			game_update(&g, WEAPON_TICK, (Controls){0});
			CHECK(e->x == x && e->y == y && !e->alive);
		}
		CHECK(e->hit_time == 0 && game_enemy_hit_frame(e) == -1);
		CHECK(g.score == (cause == 0 ? 300 : 0));
		e->hit_time = ENEMY_HIT_DURATION;
		game_explore(&g, LAKE, 0);
		CHECK(e->hit_time == 0);
	}

	scenarios++; /* A one-can short tap immediately replaces an offscreen shot. */
	for (int face = -1; face <= 1; face += 2) {
		reset(&g, r);
		g.x = 600;
		g.face = face;
		g.cola = 1;
		game_camera(&g);
		g.shot_count = 1;
		g.shots[0] = (Shot){g.camera + (face > 0 ? WIDTH + 9 : -9), 60, 60, 1, .5, face};
		game_key(&g, SDL_SCANCODE_INSERT, false);
		CHECK(g.shot_count == 1 && g.shots[0].age == 0);
		CHECK(g.shots[0].face == face && g.shot_cooldown == WEAPON_TICK);
		CHECK(g.time == 0);
	}
	reset(&g, r);
	g.cola = 1;
	game_key(&g, SDL_SCANCODE_LEFT, false);
	game_key(&g, SDL_SCANCODE_INSERT, false);
	CHECK(g.face == -1 && g.shots[0].face == -1); /* Turn/fire in one event batch. */

	scenarios++; /* Audio effects are bounded, terminate, and F2 mutes without repeat toggles. */
	AudioState saved_audio = r->audio;
	r->audio = (AudioState){.rate = 48000, .enabled = true};
	float samples[1024];
	for (int effect = 0; effect < SOUND_COUNT; effect++) {
		sound_set_enabled(r, false);
		sound_set_enabled(r, true);
		sound_play(r, effect);
		double power = 0;
		bool valid = true;
		int blocks = (r->audio.voices[0].length + 1023) / 1024 + 1;
		CHECK(blocks > 0 && blocks < 2000);
		for (int block = 0; block < blocks; block++) {
			sound_mix(&r->audio, samples, 1024);
			for (int n = 0; n < 1024; n++) {
				valid &= isfinite(samples[n]) && fabs(samples[n]) <= .8;
				power += samples[n] * samples[n];
			}
		}
		CHECK(valid && power > 0);
		sound_mix(&r->audio, samples, 1024);
		bool silent = true;
		for (int n = 0; n < 1024; n++)
			silent &= samples[n] == 0;
		CHECK(silent);
	}
	reset(&g, r);
	game_key(&g, SDL_SCANCODE_F2, false);
	CHECK(!r->audio.enabled);
	game_key(&g, SDL_SCANCODE_F2, true);
	CHECK(!r->audio.enabled);
	sound_play(r, SOUND_SHOT);
	sound_mix(&r->audio, samples, 1024);
	bool silent = true;
	for (int n = 0; n < 1024; n++)
		silent &= samples[n] == 0;
	CHECK(silent);
	g.paused = true;
	game_key(&g, SDL_SCANCODE_F2, false);
	CHECK(r->audio.enabled && g.paused);
	game_reset(&g, r);
	CHECK(r->audio.enabled);
	CHECK(!bindings_assign(&r->bindings, BIND_FIRE, 0, SDL_SCANCODE_F2));
	r->audio = saved_audio;

	scenarios++; /* Distinct rapid taps survive cadence without exceeding cola capacity. */
	for (int cola = 1; cola <= 5; cola++) {
		reset(&g, r);
		g.cola = cola;
		g.x = 600;
		game_camera(&g);
		for (int tap = 0; tap < 5; tap++)
			game_key(&g, SDL_SCANCODE_INSERT, false);
		CHECK(g.shot_count == 1 && g.fire_pending == cola - 1);
		game_key(&g, SDL_SCANCODE_INSERT, true);
		CHECK(g.fire_pending == cola - 1);
		tick(&g, 60, (Controls){0});
		CHECK(g.shot_count == cola && g.fire_pending == 0);
	}
	reset(&g, r);
	g.cola = 5;
	game_key(&g, SDL_SCANCODE_INSERT, false);
	game_key(&g, SDL_SCANCODE_INSERT, false);
	CHECK(g.fire_pending == 1);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	CHECK(g.paused && g.fire_pending == 0);
	game_key(&g, SDL_SCANCODE_ESCAPE, false);
	tick(&g, 20, (Controls){0});
	CHECK(g.shot_count == 1);
	game_key(&g, SDL_SCANCODE_INSERT, false);
	game_key(&g, SDL_SCANCODE_INSERT, false);
	CHECK(g.fire_pending == 1);
	g.energy = 0;
	tick(&g, 1, (Controls){0});
	CHECK(g.fire_pending == 0);

	scenarios++; /* F4 starts at real incoming doors and preserves the hero's inventory. */
	const double entry_x[AREA_COUNT] = {952, 104, 24, 1112, 72, 40, 1976, 56};
	const double entry_y[AREA_COUNT] = {80, 96, 96, 64, 96, 96, 112, 64};
	for (int row = 0; row < AREA_COUNT; row++) {
		int area = MENU_AREAS[row];
		reset(&g, r);
		g.cola = 2;
		g.key = true;
		g.hp = 3;
		g.lives = 4;
		g.score = 12345;
		g.location_menu = true;
		game_key(&g, (SDL_Scancode)(SDL_SCANCODE_1 + row), false);
		CHECK(g.area == area && g.x == entry_x[area] && g.y == entry_y[area]);
		CHECK(!g.location_menu && g.spawn_time == 0 && g.door_time == 0);
		CHECK(g.cola == 2 && g.key && !g.boots && !g.wand && !g.lantern);
		CHECK(g.hp == 3 && g.lives == 4 && g.score == 12345);
		CHECK(g.checkpoint_area == area && g.checkpoint_x == g.x && g.checkpoint_y == g.y);
		CHECK(!box_hits(r, area, (Box){g.x + 3, g.y + 2, 10, 30}));
		CHECK(g.camera >= game_scene(&g) * SCENE_WIDTH &&
			g.camera <= (game_scene(&g) + 1) * SCENE_WIDTH - WIDTH);
	}
	/* F4 arrows select without teleporting; all confirmation keys enter the selected area. */
	const SDL_Scancode confirm_location[] = {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_SPACE};
	for (size_t confirm = 0; confirm < sizeof confirm_location / sizeof confirm_location[0]; confirm++) {
		for (int row = 0; row < AREA_COUNT; row++) {
			reset(&g, r); g.key = true; g.score = 12345;
			game_key(&g, SDL_SCANCODE_F4, false);
			CHECK(g.location_menu && g.location_row == 0);
			for (int step = 0; step < row; step++) game_key(&g, SDL_SCANCODE_DOWN, false);
			CHECK(g.location_row == row && g.area == FOREST && g.location_menu);
			game_key(&g, confirm_location[confirm], false);
			int area = MENU_AREAS[row];
			CHECK(!g.location_menu && g.area == area && g.x == entry_x[area] && g.y == entry_y[area]);
			CHECK(g.key && g.score == 12345);
			game_key(&g, SDL_SCANCODE_F4, false);
			CHECK(g.location_row == row && g.location_menu);
			game_key(&g, SDL_SCANCODE_TAB, false);
			game_key(&g, SDL_SCANCODE_PAGEUP, false);
			CHECK(g.settings_page == 0 && g.location_row == row);
			game_key(&g, SDL_SCANCODE_ESCAPE, false);
			CHECK(!g.location_menu && g.area == area);
		}
	}
	reset(&g, r); game_key(&g, SDL_SCANCODE_F4, false);
	game_key(&g, SDL_SCANCODE_UP, false); CHECK(g.location_row == 7);
	game_key(&g, SDL_SCANCODE_DOWN, false); CHECK(g.location_row == 0);
	game_key(&g, SDL_SCANCODE_END, false); CHECK(g.location_row == 7);
	game_key(&g, SDL_SCANCODE_HOME, false); CHECK(g.location_row == 0);

	scenarios++; /* Original PC speaker priorities: a pickup is not mixed with a shot. */
	saved_audio = r->audio;
	r->audio = (AudioState){.rate = 48000, .enabled = true};
	sound_play(r, SOUND_PICKUP);
	sound_play(r, SOUND_SHOT);
	CHECK(r->audio.voices[0].effect == SOUND_PICKUP);
	sound_play(r, SOUND_DEATH);
	CHECK(r->audio.voices[0].effect == SOUND_DEATH);
	sound_play(r, SOUND_DOOR);
	CHECK(r->audio.voices[0].effect == SOUND_DOOR);
	sound_stop(r);
	sound_play(r, SOUND_SHOT);
	CHECK(r->audio.voices[0].length == (int)llround(2 * WEAPON_TICK / 2 * 48000));
	sound_stop(r);
	sound_play(r, SOUND_THEME);
	CHECK(r->audio.voices[0].length == (int)llround(392 * WEAPON_TICK / 2 * 48000));
	r->audio = saved_audio;

	scenarios++; /* All 24 panoramas: full-jump envelope against the DOS tick model. */
	for (int area = 0; area < AREA_COUNT; area++) {
		uint8_t saved_cells[3][1280];
		memcpy(saved_cells, r->areas[area].cells, sizeof saved_cells);
		memset(r->areas[area].cells, 0, sizeof saved_cells);
		for (int scene = 0; scene < 3; scene++) {
			memset(r->areas[area].cells[scene] + 9 * 128,
				r->areas[area].last_passable + 1, 128);
			for (int boots = 0; boots < 2; boots++) {
				double dos_height, dos_duration;
				dos_jump_at(
					area == MOON, boots != 0, 112, &dos_height, &dos_duration);
				for (int fps = 30; fps <= 240; fps *= 2) {
					reset(&g, r);
					g.area = area;
					g.x = scene * 2048. + 512;
					g.y = 112;
					g.boots = boots != 0;
					game_jump(&g);
					memset(g.taken, 1, sizeof g.taken);
					double apex = g.y, start = g.x;
					bool landed_full = false;
					for (int frame = 0; frame < 4 * fps; frame++) {
						/* Release after the original support window; the full arc persists. */
						game_update(&g, 1. / fps,
							(Controls){.right = true,
								.jump = frame / (double)fps <
									(boots ? 4 : 3) *
										WEAPON_TICK});
						apex = fmin(apex, g.y);
						CHECK(g.y >= 0 &&
							!box_hits(r, area,
								(Box){g.x + 3, g.y + 2, 10, 30}));
						if (g.grounded) {
							landed_full = true;
							break;
						}
					}
					if (!landed_full || g.lives != 5)
						fprintf(stderr,
							"Arc area=%d scene=%d boots=%d fps=%d grounded=%d lives=%d y=%f x=%f\n",
							area, scene, boots, fps, g.grounded,
							g.lives, g.y, g.x);
					CHECK(landed_full && g.lives == 5);
					CHECK(fabs(112 - apex - dos_height) <= 5);
					/* One DOS horizontal step plus sampling tolerance at 30Hz. */
					CHECK(fabs(g.x - start - dos_duration / WEAPON_TICK * 8) <=
						11);
				}
			}
		}
		memcpy(r->areas[area].cells, saved_cells, sizeof saved_cells);
	}

	scenarios++; /* Reported Moon ravine: take off up to 16px before the lip, both ways. */
	CHECK(tile_solid(r, MOON, 78, 9) && !tile_solid(r, MOON, 79, 9));
	CHECK(!tile_solid(r, MOON, 86, 9) && tile_solid(r, MOON, 87, 9));
	for (int start = 1248; start <= 1260; start += 2)
		for (int direction = -1; direction <= 1; direction += 2) {
			reset(&g, r);
			g.area = MOON;
			g.x = direction > 0 ? start : 2640 - start;
			g.y = 112;
			game_jump(&g);
			bool reached = false;
			for (int frame = 0; frame < 360; frame++) {
				game_update(&g, STEP,
					(Controls){.left = direction < 0,
						.right = direction > 0,
						.jump = frame * STEP < 3 * WEAPON_TICK});
				CHECK(g.y >= 0 &&
					!box_hits(r, MOON, (Box){g.x + 3, g.y + 2, 10, 30}));
				if (g.grounded &&
					(direction > 0 ? g.x + 13 > 1392 : g.x + 3 < 1264)) {
					reached = true;
					break;
				}
			}
			CHECK(reached && !g.boots && g.lives == 5);
		}

	scenarios++; /* Every open, level map gap within the original jump envelope. */
	int audited_gaps[AREA_COUNT] = {0};
	for (int area = 0; area < AREA_COUNT; area++)
		for (int scene = 0; scene < 3; scene++)
			for (int row = 2; row < 10; row++)
				for (int begin = 1; begin < 127; begin++) {
					int column = scene * 128 + begin;
					if (!tile_solid(r, area, column - 1, row) ||
						tile_solid(r, area, column, row))
						continue;
					int end = begin;
					while (end < 128 &&
						!tile_solid(r, area, scene * 128 + end, row))
						end++;
					if (end == 128)
						continue;
					for (int boots = 0; boots < 2; boots++) {
						double height, duration;
						dos_jump_at(area == MOON, boots != 0, row * 16 - 32,
							&height, &duration);
						if ((end - begin) * 16 >
							duration / WEAPON_TICK * 8 - 8)
							continue;
						double left = column * 16. - 16;
						double right = (scene * 128 + end) * 16.;
						double y = row * 16 - 32,
						       top = fmax(0, y - height - 8);
						/* Low ceilings/raised pillars have dedicated regressions above. */
						if (box_hits(r, area,
							    (Box){left + 3, top, right - left + 10,
								    y + 32 - top}))
							continue;
						for (int direction = -1; direction <= 1;
							direction += 2) {
							reset(&g, r);
							g.area = area;
							g.x = direction > 0 ? left : right;
							g.y = y;
							g.boots = boots != 0;
							game_jump(&g);
							bool reached = false;
							for (int frame = 0; frame < 480; frame++) {
								double target = direction > 0
											? right
											: left;
								bool moving =
									direction > 0
										? g.x < target
										: g.x > target;
								game_update(&g, STEP,
									(Controls){
										.left = moving &&
											direction <
												0,
										.right = moving &&
											 direction >
												 0,
										.jump = frame * STEP <
											(boots ? 4
											       : 3) *
												WEAPON_TICK});
								if (g.grounded &&
									(direction > 0 ? g.x + 13 >
												 right
										       : g.x + 3 <
												 left + 16)) {
									reached = true;
									break;
								}
								if (g.death_time > 0)
									break;
							}
							if (!reached)
								fprintf(stderr,
									"Прыжок: область %d, панорама %d, ряд %d, разрыв %d..%d, ботинки %d, направление %d\n",
									area, scene, row, begin,
									end, boots, direction);
							CHECK(reached && g.lives == 5);
							audited_gaps[area]++;
						}
					}
				}
	printf("Проверка разрывов: ");
	for (int area = 0; area < AREA_COUNT; area++)
		printf("%s %d%s", AREA_TITLES[area], audited_gaps[area],
			area + 1 < AREA_COUNT ? "; " : "\n");
	scenarios++; /* Real BASE0 one-tile holes: globes fall instead of bridging, both ways. */
	for (int column = 1; column < 127; column++) {
		if (tile_solid(r, BASE, column, 9) || !tile_solid(r, BASE, column - 1, 9) ||
			!tile_solid(r, BASE, column + 1, 9))
			continue;
		for (int direction = -1; direction <= 1; direction += 2)
			for (int fps = 30; fps <= 240; fps *= 2) {
				reset(&g, r);
				g.area = BASE;
				g.x = column * 16. + direction * 160;
				CHECK(spawn_near(r, BASE, g.x, 0, &g.x, &g.y));
				memset(g.taken, 1, sizeof g.taken);
				g.invincible = 1e9;
				game_camera(&g);
				Enemy *e = &g.enemies[BASE][0][3];
				*e = (Enemy){.x = column * 16. - direction * 16,
					.y = 128,
					.shape = 8,
					.scene = 0,
					.behavior = 3,
					.alive = true,
					.vx = direction * 4 / WEAPON_TICK};
				int score = g.score;
				bool fell = false;
				for (int frame = 0; frame < fps; frame++) {
					game_update(&g, 1. / fps, (Controls){0});
					fell |= e->y > 128;
					if (!e->alive)
						break;
				}
				if (!(fell && !e->alive && e->hit_time == 0 && g.score == score))
					fprintf(stderr,
						"Globe gap=%d direction=%d fps=%d x=%f y=%f live=%d timer=%f camera=%f\n",
						column, direction, fps, e->x, e->y, e->alive,
						e->timer, g.camera);
				CHECK(fell && !e->alive && e->hit_time == 0 && g.score == score);
				CHECK(e->timer > 0 && e->timer <= 2);
			}
	}

	scenarios++; /* All mapped species use their mode, independent of frame rate. */
	int species_checked = 0;
	bool checked_species[SHAPE_COUNT] = {0};
	for (int area = 0; area < AREA_COUNT; area++)
		for (int scene = 0; scene < 3; scene++)
			for (int index = 0; index < r->areas[area].scenes[scene].enemy_count;
				index++) {
				EnemySpec spec = r->areas[area].scenes[scene].enemies[index];
				if (spec.behavior == 127 || checked_species[spec.shape])
					continue;
				checked_species[spec.shape] = true;
				species_checked++;
				uint8_t cells[1280];
				memcpy(cells, r->areas[FOREST].cells[0], sizeof cells);
				memset(r->areas[FOREST].cells[0], 0, sizeof cells);
				memset(r->areas[FOREST].cells[0] + 9 * 128,
					r->areas[FOREST].last_passable + 1, 128);
				double expected_x = 0, expected_y = 0;
				for (int fps = 30; fps <= 240; fps *= 2) {
					reset(&g, r);
					g.x = 640;
					g.y = 112;
					g.face = 1;
					game_camera(&g);
					Enemy *e = &g.enemies[FOREST][0][0];
					*e = (Enemy){.x = 736,
						.y = 112,
						.shape = spec.shape,
						.scene = 0,
						.behavior = spec.behavior,
						.alive = true,
						.vx = -4 / WEAPON_TICK};
					for (int frame = 0; frame < fps; frame++)
						game_update(&g, 1. / fps, (Controls){0});
					CHECK(e->alive && e->x < 736);
					CHECK(!box_hits(r, FOREST, (Box){e->x, e->y, 16, 16}));
					if (fps == 30) {
						expected_x = e->x;
						expected_y = e->y;
					} else
						CHECK(e->x == expected_x && e->y == expected_y);
				}
				memcpy(r->areas[FOREST].cells[0], cells, sizeof cells);
			}
	CHECK(species_checked == SHAPE_COUNT);
	game_reset(&g, r);
	for (int area = 0; area < AREA_COUNT; area++)
		for (int scene = 0; scene < 3; scene++)
			for (int index = 0; index < r->areas[area].scenes[scene].enemy_count;
				index++) {
				Enemy *e = &g.enemies[area][scene][index];
				CHECK(!e->alive || !box_hits(r, area, (Box){e->x, e->y, 16, 16}));
				CHECK(e->behavior != 127 || !e->alive);
			}

	scenarios++; /* Independent DOS hopper trace and walls for all five movement modes. */
	{
		uint8_t cells[1280];
		memcpy(cells, r->areas[FOREST].cells[0], sizeof cells);
		memset(r->areas[FOREST].cells[0], 0, sizeof cells);
		memset(r->areas[FOREST].cells[0] + 9 * 128, r->areas[FOREST].last_passable + 1,
			128);
		reset(&g, r);
		g.x = 640;
		g.y = 112;
		game_camera(&g);
		Enemy *e = &g.enemies[FOREST][0][0];
		*e = (Enemy){
			.x = 736, .y = 128, .shape = 4, .scene = 0, .behavior = 2, .alive = true};
		const int hopper_y[] = {128, 120, 112, 104, 96, 96, 96, 96, 96, 104, 112, 128};
		for (int frame = 0; frame < 12; frame++) {
			game_update(&g, WEAPON_TICK, (Controls){0});
			CHECK(e->alive && e->y == hopper_y[frame]);
		}
		for (int row = 0; row < 9; row++)
			r->areas[FOREST].cells[0][row * 128 + 48] =
				r->areas[FOREST].last_passable + 1;
		for (int behavior = 1; behavior <= 5; behavior++) {
			reset(&g, r);
			g.x = 840;
			g.y = 112;
			g.face = -1;
			game_camera(&g);
			*e = (Enemy){.x = 736,
				.y = 64,
				.shape = 2,
				.scene = 0,
				.behavior = behavior,
				.alive = true,
				.vx = 4 / WEAPON_TICK};
			for (int frame = 0; frame < 12; frame++) {
				game_update(&g, WEAPON_TICK, (Controls){0});
				CHECK(e->alive && e->x <= 752);
				CHECK(!box_hits(r, FOREST, (Box){e->x, e->y, 16, 16}));
			}
			if (behavior == 4)
				CHECK(e->y > 64); /* Bee switches axis at the wall. */
		}
		reset(&g, r);
		g.x = 640;
		g.y = 112;
		game_camera(&g);
		*e = (Enemy){.scene = 0,
			.shape = 8,
			.behavior = 3,
			.timer = .1,
			.move_clock = 10,
			.move_step = true,
			.fall_speed = 23};
		game_update(&g, .05, (Controls){0});
		CHECK(!e->alive);
		game_update(&g, .06, (Controls){0});
		CHECK(e->alive && !game_enemy_contact(&g, e));
		CHECK(e->move_clock == 0 && !e->move_step && e->fall_speed == 0);
		CHECK(fmod(e->x, 8) == 0 && fmod(e->y, 8) == 0);
		g.paused = true;
		Enemy frozen = *e;
		game_update(&g, 1, (Controls){0});
		CHECK(!memcmp(e, &frozen, sizeof frozen));
		memcpy(r->areas[FOREST].cells[0], cells, sizeof cells);
	}
	CHECK(r->story_logo != NULL && r->legend_logo != NULL);

	scenarios++; /* F5 refills only shield energy, including at pause; no resurrection. */
	reset(&g, r);
	for (int hp = 0; hp <= 6; hp++) {
		g.hp = hp;
		game_key(&g, SDL_SCANCODE_F5, false);
		CHECK(g.hp == 6 && g.lives == 5 && g.score == 0);
	}
	g.hp = 1;
	game_key(&g, SDL_SCANCODE_F5, true);
	CHECK(g.hp == 1);
	g.paused = true;
	game_key(&g, SDL_SCANCODE_F5, false);
	CHECK(g.hp == 6 && g.paused);
	g.hp = 2;
	g.death_time = DEATH_DURATION;
	game_key(&g, SDL_SCANCODE_F5, false);
	CHECK(g.hp == 2 && g.death_time == DEATH_DURATION);
	g.death_time = 0;
	g.over = true;
	game_key(&g, SDL_SCANCODE_F5, false);
	CHECK(g.hp == 2 && g.over);
	CHECK(!bindings_assign(&r->bindings, BIND_FIRE, 0, SDL_SCANCODE_F5));

	scenarios++; /* Panorama tune on successful crossings, including the return trip. */
	saved_audio = r->audio;
	r->audio = (AudioState){.rate = 48000, .enabled = true};
	reset(&g, r);
	g.area = LAKE;
	g.x = 0;
	g.y = 112;
	sound_play(r, SOUND_SHOT);
	game_cross(&g, -1);
	CHECK(game_scene(&g) == 1 && r->audio.voices[0].effect == SOUND_PANORAMA);
	CHECK(r->audio.voices[0].length == (int)llround(33 * WEAPON_TICK / 2 * 48000));
	sound_mix(&r->audio, samples, 1024);
	game_cross(&g, 1);
	CHECK(game_scene(&g) == 0 && r->audio.voices[0].effect == SOUND_PANORAMA);
	CHECK(r->audio.voices[0].frame == 0);
	sound_play(r, SOUND_SHOT);
	CHECK(r->audio.voices[0].effect == SOUND_PANORAMA);
	sound_stop(r);
	g.area = CASTLE;
	g.x = 2032;
	game_cross(&g, 1);
	CHECK(g.x == 2032 && r->audio.voices[0].length == 0);
	g.area = LAKE;
	g.x = 0;
	uint8_t edge_tile = r->areas[LAKE].cells[1][7 * 128 + 127];
	r->areas[LAKE].cells[1][7 * 128 + 127] = r->areas[LAKE].last_passable + 1;
	game_cross(&g, -1);
	CHECK(g.x == 0 && r->audio.voices[0].length == 0);
	r->areas[LAKE].cells[1][7 * 128 + 127] = edge_tile;
	sound_set_enabled(r, false);
	game_cross(&g, -1);
	CHECK(game_scene(&g) == 1 && r->audio.voices[0].length == 0);
	r->audio = saved_audio;
	scenarios++; /* Fourth settings page: UTF-8 editing, bounds and persistence. */
	ScoreTable saved_scores = r->scores;
	char saved_path[sizeof r->score_path];
	memcpy(saved_path, r->score_path, sizeof saved_path);
	r->score_path[0] = 0;
	r->scores = (ScoreTable){.count = 1, .entries = {{.score = 5000, .name = "Макс"}}};
	reset(&g, r);
	g.location_menu = true;
	g.settings_page = 3;
	CHECK(game_text_input(&g));
	game_key(&g, SDL_SCANCODE_BACKSPACE, false);
	CHECK(!strcmp(r->scores.entries[0].name, "Мак"));
	score_edit_text(&g, "сим");
	CHECK(!strcmp(r->scores.entries[0].name, "Максим"));
	score_edit_text(&g, "ЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯЯ");
	CHECK(strlen(r->scores.entries[0].name) == 32);
	game_key(&g, SDL_SCANCODE_BACKSPACE, false);
	CHECK(strlen(r->scores.entries[0].name) == 30);
	for (int i = 0; i < 20; i++) game_key(&g, SDL_SCANCODE_INSERT, false);
	CHECK(r->scores.count == SCORE_COUNT && g.settings_row == SCORE_COUNT - 1);
	game_key(&g, SDL_SCANCODE_DELETE, false);
	CHECK(r->scores.count == SCORE_COUNT - 1);
	g.settings_row = r->scores.count - 1;
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(g.settings_row == 1 && r->scores.entries[1].score == 2000);
	CHECK(r->scores.entries[r->scores.count - 1].score == 1000);
	game_key(&g, SDL_SCANCODE_TAB, false);
	CHECK(g.settings_page == 0 && !game_text_input(&g));
	g.settings_page = 3;
	g.help_open = true;
	CHECK(!game_text_input(&g));
	scenarios++; /* Physical and keypad Ins/Del edit empty and populated tables. */
	reset(&g, r);
	g.location_menu = true;
	g.settings_page = 3;
	r->scores = (ScoreTable){0};
	SDL_KeyboardEvent score_key = {0};
	score_key.keysym.scancode = SDL_SCANCODE_KP_0;
	game_key_event(&g, &score_key);
	CHECK(r->scores.count == 1 && g.settings_row == 0 && !g.fire_pending);
	CHECK(!strcmp(r->scores.entries[0].name, "НОВЫЙ") && r->scores.entries[0].score == 1000);
	scenarios++; /* Untouched default survives navigation; Backspace clears it at once. */
	game_key(&g, SDL_SCANCODE_RIGHT, false);
	CHECK(!strcmp(r->scores.entries[g.settings_row].name, "НОВЫЙ"));
	game_key(&g, SDL_SCANCODE_BACKSPACE, false);
	CHECK(!r->scores.entries[g.settings_row].name[0]);
	game_key(&g, SDL_SCANCODE_BACKSPACE, false);
	CHECK(!r->scores.entries[g.settings_row].name[0]);
	score_edit_text(&g, "Максим");
	game_key(&g, SDL_SCANCODE_BACKSPACE, false);
	CHECK(!strcmp(r->scores.entries[g.settings_row].name, "Макси"));
	/* Restore the untouched insertion for the keypad and persistence checks below. */
	snprintf(r->scores.entries[g.settings_row].name, SCORE_NAME_BYTES, "%s", "НОВЫЙ");
	r->scores.entries[g.settings_row].score = 1000;
	score_key.repeat = 1;
	game_key_event(&g, &score_key);
	CHECK(r->scores.count == 1);
	score_key.repeat = 0;
	score_key.keysym.mod = KMOD_NUM;
	game_key_event(&g, &score_key);
	score_key.keysym.scancode = SDL_SCANCODE_KP_PERIOD;
	game_key_event(&g, &score_key);
	CHECK(r->scores.count == 1);
	score_key.keysym.mod = KMOD_NONE;
	game_key_event(&g, &score_key);
	CHECK(r->scores.count == 0 && !r->scores.entries[0].name[0]);
	score_key.keysym.scancode = SDL_SCANCODE_INSERT;
	game_key_event(&g, &score_key);
	score_key.keysym.scancode = SDL_SCANCODE_KP_0;
	game_key_event(&g, &score_key);
	CHECK(r->scores.count == 2 && g.settings_row == 1);
	scenarios++; /* Event modifiers select x1/x10/x100; Ctrl wins over Shift. */
	score_key.keysym.scancode = SDL_SCANCODE_RIGHT;
	score_key.keysym.mod = KMOD_LCTRL;
	game_key_event(&g, &score_key);
	CHECK(r->scores.entries[g.settings_row].score == 101000);
	score_key.keysym.scancode = SDL_SCANCODE_LEFT;
	score_key.keysym.mod = KMOD_RCTRL;
	game_key_event(&g, &score_key);
	CHECK(r->scores.entries[g.settings_row].score == 1000);
	score_key.keysym.scancode = SDL_SCANCODE_RIGHT;
	score_key.keysym.mod = KMOD_LSHIFT;
	game_key_event(&g, &score_key);
	CHECK(r->scores.entries[g.settings_row].score == 11000);
	score_key.keysym.mod = KMOD_RCTRL | KMOD_RSHIFT;
	game_key_event(&g, &score_key);
	CHECK(r->scores.entries[g.settings_row].score == 111000);
	score_key.repeat = 1;
	game_key_event(&g, &score_key);
	CHECK(r->scores.entries[g.settings_row].score == 111000);
	score_key.repeat = 0;
	r->scores.entries[g.settings_row].score = 999999990;
	game_key_event(&g, &score_key);
	CHECK(r->scores.entries[g.settings_row].score == 999999999);
	r->scores.entries[g.settings_row].score = 1;
	score_key.keysym.scancode = SDL_SCANCODE_LEFT;
	game_key_event(&g, &score_key);
	CHECK(r->scores.entries[g.settings_row].score == 1);
	score_key.keysym.mod = KMOD_NONE;
	char editor_path[] = "/tmp/cc-score-editor-XXXXXX";
	int editor_fd = mkstemp(editor_path);
	CHECK(editor_fd >= 0);
	if (editor_fd >= 0) {
		close(editor_fd);
		snprintf(r->score_path, sizeof r->score_path, "%s", editor_path);
		score_key.keysym.scancode = SDL_SCANCODE_RIGHT;
		game_key_event(&g, &score_key);
		ScoreTable editor_loaded = {0};
		CHECK(scores_load(&editor_loaded, editor_path));
		CHECK(!memcmp(&editor_loaded, &r->scores, sizeof editor_loaded));
		unlink(editor_path);
		r->score_path[0] = 0;
	}
	g.settings_row = 0;
	score_key.keysym.scancode = SDL_SCANCODE_DELETE;
	game_key_event(&g, &score_key);
	CHECK(r->scores.count == 1 && !strcmp(r->scores.entries[0].name, "НОВЫЙ"));
	for (int i = 0; i < SCORE_COUNT; i++) game_key(&g, SDL_SCANCODE_INSERT, false);
	CHECK(r->scores.count == SCORE_COUNT && strstr(g.message, "Del") && g.message_time > 0);
	g.help_open = true;
	score_key.keysym.scancode = SDL_SCANCODE_KP_PERIOD;
	game_key_event(&g, &score_key);
	CHECK(r->scores.count == SCORE_COUNT);
	r->scores = saved_scores;
	memcpy(r->score_path, saved_path, sizeof saved_path);

	scenarios++; /* Ending state is local; conversion neither creates lives nor depends on FPS. */
	for (int fps = 30; fps <= 240; fps *= 2) {
		reset(&g, r);
		g.treasures = 5;
		game_collect(&g, CROWN);
		CHECK(g.treasures == 7 && g.ending_stage == ENDING_DEPART && !g.won);
		int lives_before = g.lives, score_before = g.score;
		double before_x = g.x;
		for (int frame = 0; frame < fps * 45; frame++)
			game_update(&g, 1. / fps, (Controls){.right = true});
		CHECK(g.won && g.ending_stage == ENDING_PAGE && g.lives == 0);
		CHECK(g.score == score_before + lives_before * 1000 && g.x == before_x);
		game_draw(&g);
		game_reset(&g, r);
		CHECK(g.ending_stage == ENDING_NONE && !g.won && g.lives == 5);
	}
	reset(&g, r);
	game_collect(&g, CROWN);
	CHECK(g.treasures == 2 && g.ending_stage == ENDING_DEPART && !g.won);
	g.score = 999999999;
	game_score(&g, 2000);
	CHECK(g.score == 999999999);

	scenarios++; /* Any final-page key opens scores before global shortcuts can run. */
	const SDL_Scancode ending_keys[] = {SDL_SCANCODE_SPACE, SDL_SCANCODE_KP_ENTER,
		SDL_SCANCODE_RETURN, SDL_SCANCODE_RIGHT, SDL_SCANCODE_Q, SDL_SCANCODE_ESCAPE,
		SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F4, SDL_SCANCODE_F5,
		SDL_SCANCODE_F11, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT,
		SDL_SCANCODE_LCTRL, SDL_SCANCODE_RALT, SDL_SCANCODE_LGUI, SDL_SCANCODE_A};
	for (size_t i = 0; i < sizeof ending_keys / sizeof ending_keys[0]; i++) {
		reset(&g, r);
		g.won = true;
		g.ending_stage = ENDING_PAGE;
		g.score = 999999999;
		game_key(&g, ending_keys[i], true);
		CHECK(!g.scores_open);
		bool audio_enabled = r->audio.enabled;
		CHECK(game_key(&g, ending_keys[i], false) == 0);
		CHECK(g.scores_open && g.score_after_game && game_text_input(&g));
		CHECK(!g.help_open && !g.location_menu && r->audio.enabled == audio_enabled);
		game_score_text(&g, "Комик");
		CHECK(!strcmp(g.score_name, "Комик"));
		game_key(&g, SDL_SCANCODE_ESCAPE, false);
		CHECK(game_key(&g, SDL_SCANCODE_RETURN, false) == 1);
		CHECK(g.scores_open && g.won && !g.score_entry);
	}
	ScoreTable before_final_scores = r->scores;
	r->scores = (ScoreTable){0};
	for (int i = 0; i < SCORE_COUNT; i++)
		(void)score_add(&r->scores, 999999999, "Лидер");
	for (size_t i = 0; i < sizeof ending_keys / sizeof ending_keys[0]; i++) {
		reset(&g, r); g.won = true; g.ending_stage = ENDING_PAGE; g.score = 1000;
		CHECK(game_key(&g, ending_keys[i], true) == 0 && !g.scores_open);
		CHECK(game_key(&g, ending_keys[i], false) == 0);
		CHECK(g.scores_open && g.score_after_game && !g.score_entry && !game_text_input(&g));
		CHECK(!g.help_open && !g.location_menu);
		CHECK(game_key(&g, SDL_SCANCODE_RETURN, false) == 1);
	}
	r->scores = before_final_scores;

	scenarios++; /* A displayed post-game scoreboard exits on any fresh key, never while editing. */
	const SDL_Scancode exit_keys[] = {SDL_SCANCODE_SPACE, SDL_SCANCODE_ESCAPE,
		SDL_SCANCODE_R, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_F2,
		SDL_SCANCODE_F11, SDL_SCANCODE_LEFT, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_BACKSPACE};
	for (size_t i = 0; i < sizeof exit_keys / sizeof exit_keys[0]; i++) {
		reset(&g, r);
		g.over = true;
		g.score = 999999999;
		game_scores_open(&g, true);
		CHECK(g.score_entry && game_text_input(&g));
		game_score_text(&g, "Комик");
		CHECK(game_key(&g, SDL_SCANCODE_ESCAPE, false) == 0);
		CHECK(!g.score_entry && g.scores_open);
		CHECK(game_key(&g, exit_keys[i], true) == 0);
		CHECK(game_key(&g, exit_keys[i], false) == 1);
		CHECK(g.over && g.scores_open && !g.title);
	}

	scenarios++; /* Crown always ends the game, with no inventory or treasure prerequisites. */
	const unsigned before_crown[] = {0, 1, 4, 5};
	for (size_t i = 0; i < sizeof before_crown / sizeof before_crown[0]; i++) {
		reset(&g, r);
		g.treasures = before_crown[i];
		g.key = g.lantern = true;
		CHECK(!g.boots && !g.wand && !g.corkscrew && g.cola == 0);
		game_collect(&g, CROWN);
		int final_lives = g.lives, final_score = g.score;
		CHECK(g.ending_stage == ENDING_DEPART && g.treasures == (before_crown[i] | 2u));
		for (int frame = 0; frame < 120 * 15; frame++)
			game_update(&g, STEP, (Controls){0});
		CHECK(g.won && g.ending_stage == ENDING_PAGE && g.lives == 0);
		CHECK(g.score == final_score + final_lives * 1000);
		CHECK(g.key && g.lantern && !g.boots && !g.wand && !g.corkscrew && g.cola == 0);
		game_key(&g, SDL_SCANCODE_SPACE, false);
		CHECK(g.scores_open && g.score_after_game);
		if (g.score_entry) game_key(&g, SDL_SCANCODE_ESCAPE, false);
		CHECK(game_key(&g, SDL_SCANCODE_SPACE, false) == 1);
	}

	scenarios++; /* Editor uses each event's Shift state, stable selection and bounded steps. */
	ScoreTable before_shift_scores = r->scores;
	char before_shift_path[sizeof r->score_path];
	memcpy(before_shift_path, r->score_path, sizeof before_shift_path);
	r->score_path[0] = 0;
	r->scores = (ScoreTable){.count = 2, .entries = {
		{.score = 20000, .name = "Первый"}, {.score = 15000, .name = "Выбранный"}}};
	reset(&g, r);
	g.location_menu = true; g.settings_page = 3; g.settings_row = 1;
	SDL_KeyboardEvent shift_event = {.type = SDL_KEYDOWN};
	shift_event.keysym.scancode = SDL_SCANCODE_RIGHT;
	shift_event.keysym.mod = KMOD_LSHIFT;
	CHECK(game_key_event(&g, &shift_event) == 0);
	CHECK(g.settings_row == 0 && r->scores.entries[0].score == 25000);
	CHECK(!strcmp(r->scores.entries[0].name, "Выбранный"));
	shift_event.keysym.mod = KMOD_NONE;
	game_key_event(&g, &shift_event);
	CHECK(r->scores.entries[0].score == 26000);
	shift_event.keysym.scancode = SDL_SCANCODE_LEFT;
	shift_event.keysym.mod = KMOD_RSHIFT;
	game_key_event(&g, &shift_event);
	CHECK(g.settings_row == 1 && r->scores.entries[1].score == 16000);
	game_key_event(&g, &shift_event);
	CHECK(r->scores.entries[1].score == 6000);
	shift_event.keysym.mod = KMOD_NONE;
	game_key_event(&g, &shift_event);
	CHECK(r->scores.entries[1].score == 5000);
	shift_event.keysym.mod = KMOD_SHIFT;
	game_key_event(&g, &shift_event);
	CHECK(r->scores.entries[1].score == 1);
	shift_event.keysym.scancode = SDL_SCANCODE_RIGHT;
	r->scores.entries[0].score = 999995000; g.settings_row = 0;
	game_key_event(&g, &shift_event);
	CHECK(r->scores.entries[0].score == 999999999);
	shift_event.keysym.scancode = SDL_SCANCODE_LEFT; shift_event.repeat = 1;
	game_key_event(&g, &shift_event);
	CHECK(r->scores.entries[0].score == 999999999);
	r->scores = before_shift_scores;
	memcpy(r->score_path, before_shift_path, sizeof before_shift_path);

	printf("Встроенная проверка: сценариев — %d, проверок — %d, ошибок — %d\n", scenarios,
		checks, failures);
	return failures ? 1 : 0;
}
