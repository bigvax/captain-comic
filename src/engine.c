#include "cc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static double clamp(double value, double low, double high) {
	return fmax(low, fmin(high, value));
}
bool box_overlap(Box a, Box b) {
	return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
bool tile_solid(const Resources *r, int area, int column, int row) {
	if (area < 0 || area >= 8 || column < 0 || column >= 384 || row < 0 || row >= 10)
		return false;
	const Area *a = &r->areas[area];
	return a->cells[column / 128][row * 128 + column % 128] > a->last_passable;
}
bool box_hits(const Resources *r, int area, Box b) {
	int x0 = (int)floor(b.x / 16), x1 = (int)floor((b.x + b.w - 1e-7) / 16);
	int y0 = (int)floor(b.y / 16), y1 = (int)floor((b.y + b.h - 1e-7) / 16);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
			if (tile_solid(r, area, x, y))
				return true;
	return false;
}
Contacts move_box(const Resources *r, int area, Box *b, double dx, double dy) {
	Contacts c = {false, false, false};
	for (int axis = 0; axis < 2; axis++) {
		double amount = axis ? dy : dx;
		int count = (int)ceil(fabs(amount) / 4.);
		if (count < 1)
			count = 1;
		double step = amount / count;
		for (int i = 0; i < count; i++) {
			if (axis)
				b->y += step;
			else
				b->x += step;
			int x0 = (int)floor(b->x / 16), x1 = (int)floor((b->x + b->w - 1e-7) / 16);
			int y0 = (int)floor(b->y / 16), y1 = (int)floor((b->y + b->h - 1e-7) / 16);
			bool hit = false;
			double edge = step > 0 ? 1e12 : -1e12;
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++)
					if (tile_solid(r, area, x, y)) {
						hit = true;
						double value =
							axis ? (step > 0 ? y * 16. : (y + 1) * 16.)
							     : (step > 0 ? x * 16. : (x + 1) * 16.);
						edge = step > 0 ? fmin(edge, value)
								: fmax(edge, value);
					}
			if (hit) {
				if (axis) {
					if (step > 0) {
						b->y = edge - b->h;
						c.ground = true;
					} else if (step < 0)
						b->y = edge;
					c.hit_y = true;
				} else {
					if (step > 0)
						b->x = edge - b->w;
					else if (step < 0)
						b->x = edge;
					c.hit_x = true;
				}
				break;
			}
		}
	}
	return c;
}
bool spawn_near(const Resources *r, int area, double desired, int segment, double *px, double *py) {
	int low = segment < 0 ? 0 : segment * 2048, high = segment < 0 ? 6144 : low + 2048;
	desired = clamp(desired, low, high - 16);
	for (int distance = 0; distance < high - low; distance += 8)
		for (int sign = 0; sign < (distance ? 2 : 1); sign++) {
			double x = desired + (sign ? -distance : distance);
			if (x < low || x > high - 16)
				continue;
			for (int top = 144; top >= 32; top -= 16) {
				Box body = {x + 3, top - 30., 10, 30}, down = body;
				down.y += .1;
				if (!box_hits(r, area, body) && box_hits(r, area, down)) {
					*px = x;
					*py = top - 32.;
					return true;
				}
			}
		}
	return false;
}
int game_scene(const Game *g) {
	int s = (int)floor((g->x + 8) / 2048);
	return s < 0 ? 0 : s > 2 ? 2 : s;
}
void game_camera(Game *g) {
	double left = game_scene(g) * 2048.;
	g->camera = clamp(g->x - 145, left, left + 2048 - WIDTH);
}
static void say(Game *g, const char *message) {
	snprintf(g->message, sizeof g->message, "%s", message);
	g->message_time = 3.;
}
void game_reset(Game *g, Resources *r) {
	memset(g, 0, sizeof *g);
	g->r = r;
	if (r->audio.device)
		sound_play(r, SOUND_THEME);
	g->area = FOREST;
	g->face = 1;
	g->x = 104;
	g->y = 96;
	g->grounded = true;
	g->hp = 6;
	g->lives = 5;
	g->energy = MAX_WEAPON_ENERGY;
	g->weapon_phase = 2;
	g->weapon_clock = WEAPON_TICK;
	g->title = true;
	g->spawn_time = ARRIVAL_DURATION;
	g->invincible = 1.5;
	g->checkpoint_area = FOREST;
	g->checkpoint_x = 104;
	g->checkpoint_y = 96;
	for (int a = 0; a < 8; a++)
		for (int s = 0; s < 3; s++) {
			const Scene *sc = &r->areas[a].scenes[s];
			for (int i = 0; i < sc->enemy_count; i++) {
				Enemy *e = &g->enemies[a][s][i];
				EnemySpec spec = sc->enemies[i];
				e->x = s * 2048. + 260 + i * 220;
				e->origin = e->x;
				e->y = 64;
				e->phase = fmod((a * 17 + s * 23 + i * 31) * .37, 6.);
				e->shape = spec.shape;
				e->behavior = spec.behavior;
				e->scene = s;
				e->vx = (i % 2) ? 45. : -45.;
				e->alive = !spec.delayed && spec.behavior != 127;
				if (box_hits(r, a, (Box){e->x, e->y, 16, 16}))
					e->alive = false;
				e->timer = spec.delayed ? 1.5 : 0.;
			}
		}
	say(g, "Найдите колу. Исследуйте лес.");
	g->message_time = 5.;
}
int game_effect_frame(double remaining, int frames) {
	int frame = (int)floor((frames * WEAPON_TICK - remaining + 1e-10) / WEAPON_TICK);
	return frame < 0 ? 0 : frame >= frames ? frames - 1 : frame;
}
int game_death_frame(const Game *g) {
	return g->death_time > DEATH_PAUSE + 1e-10
		       ? game_effect_frame(g->death_time - DEATH_PAUSE, 8)
		       : -1;
}
void game_jump(Game *g) {
	if (g->grounded && g->spawn_time <= 0 && g->death_time <= 0 && g->teleport_time <= 0 &&
		g->door_time <= 0) {
		/* Match the DOS apex: 56/72px on Tambi, 88px on the Moon without boots. */
		g->vy = -(g->boots ? 245. : (g->area == MOON ? 192. : 220.));
		g->jump_full = true;
		g->grounded = false;
		g->head_pause = 0;
		/* Original boots change the support counter from 4 to 5 (0x23F5). */
		g->jump_boost = (g->boots ? 4 : 3) * WEAPON_TICK;
	}
}
static void clear_enemy_effects(Game *g) {
	for (int area = 0; area < AREA_COUNT; area++)
		for (int scene = 0; scene < 3; scene++)
			for (int i = 0; i < 4; i++)
				g->enemies[area][scene][i].hit_time = 0;
}
void game_travel(Game *g, int area, double x, double y) {
	clear_enemy_effects(g);
	g->death_time = g->teleport_time = g->door_time = 0;
	g->area = area;
	g->x = x;
	g->y = y;
	g->vy = 0.;
	g->grounded = true;
	g->shot_count = 0;
	g->fire_pending = 0;
	g->shot_cooldown = 0;
	g->weapon_pulse = false;
	g->jump_boost = 0;
	g->jump_full = false;
	g->head_pause = 0;
	g->invincible = 2.;
	g->checkpoint_area = area;
	g->checkpoint_x = x;
	g->checkpoint_y = y;
	game_camera(g);
	char message[100];
	snprintf(message, sizeof message, "%s: F1 — справка", AREA_TITLES[area]);
	say(g, message);
}
void game_interact(Game *g) {
	if (!g->grounded || g->door_time > 0 || g->spawn_time > 0 || g->death_time > 0 ||
		g->teleport_time > 0 || g->over)
		return;
	int scene = game_scene(g);
	Scene *sc = &g->r->areas[g->area].scenes[scene];
	for (int i = 0; i < sc->door_count; i++) {
		Door d = sc->doors[i];
		double x = scene * 2048. + d.x;
		if (fabs(g->x - (x + 8)) <= 20 && fabs(g->y - d.y) <= 12) {
			if (!g->key) {
				say(g, "Для этой двери нужен ключ.");
				return;
			}
			Scene *dest = &g->r->areas[d.area].scenes[d.scene];
			for (int j = 0; j < dest->door_count; j++) {
				Door back = dest->doors[j];
				if (back.area == g->area && back.scene == scene) {
					g->door_x = x;
					g->door_y = d.y;
					g->door_to_area = d.area;
					g->door_to_x = d.scene * 2048. + back.x + 8;
					g->door_to_y = back.y;
					g->door_time = DOOR_DURATION;
					sound_play(g->r, SOUND_DOOR);
					g->x = x + 8;
					g->y = d.y;
					g->vy = g->jump_boost = g->head_pause = 0;
					g->jump_full = false;
					g->shot_count = 0;
					g->fire_pending = 0;
					g->shot_cooldown = 0;
					g->weapon_pulse = false;
					game_camera(g);
					return;
				}
			}
		}
	}
}
void game_explore(Game *g, int area, int scene) {
	sound_stop(g->r);
	scene = scene < 0 ? 0 : scene > 2 ? 2 : scene;
	double x, y;
	if (!spawn_near(g->r, area, scene * 2048. + 104, scene, &x, &y)) {
		say(g, "На этой панораме нет безопасного места для появления.");
		return;
	}
	game_travel(g, area, x, y);
	g->title = g->paused = g->over = g->won = g->location_menu = g->help_open = false;
	g->spawn_time = 0;
	g->test_mode = true;
	g->cola = 5;
	g->key = g->boots = g->corkscrew = g->lantern = g->wand = true;
	g->hp = 6;
	if (g->lives < 5)
		g->lives = 5;
	say(g, "Исследование: F4 — меню / PgUp, PgDn — панорамы");
}
bool game_enter_area(Game *g, int area) {
	if (area < 0 || area >= AREA_COUNT)
		return false;
	/* Normal first entrances in the adventure route; tubes start from the shed. */
	static const int parent[AREA_COUNT] = {
		FOREST, CASTLE, LAKE, MOON, LAKE, LAKE, FOREST, BUILDING};
	static const int parent_scene[AREA_COUNT] = {2, 0, 2, 2, 2, 0, 0, 2};
	double x = 104, y = 96;
	bool found = area == FOREST;
	for (int scene = 0; !found && scene < 3; scene++) {
		const Scene *target = &g->r->areas[area].scenes[scene];
		for (int i = 0; i < target->door_count; i++) {
			Door door = target->doors[i];
			if (door.area == parent[area] && door.scene == parent_scene[area]) {
				x = scene * SCENE_WIDTH + door.x + 8;
				y = door.y;
				found = true;
				break;
			}
		}
	}
	if (!found)
		return false;
	sound_stop(g->r);
	game_travel(g, area, x, y);
	g->title = g->paused = g->over = g->won = g->location_menu = g->help_open = false;
	g->spawn_time = 0;
	g->test_mode = true;
	g->face = area == LAKE || area == CASTLE ? -1 : 1;
	return true;
}
bool game_teleport(Game *g) {
	if (!g->wand) {
		say(g, "Сначала найдите жезл телепортации.");
		return false;
	}
	if (!g->grounded || g->teleport_cooldown > 0 || g->teleport_time > 0 || g->death_time > 0 ||
		g->spawn_time > 0 || g->door_time > 0 || g->over)
		return false;
	int scene = game_scene(g);
	double x = floor((g->x + g->face * 48) / 16) * 16;
	if (x < scene * 2048 || x > scene * 2048 + 2032)
		return false;
	int column = (int)x / 16, row = 9;
	while (row >= 0 && !tile_solid(g->r, g->area, column, row))
		row--;
	if (row < 0)
		return false;
	while (row > 0 && tile_solid(g->r, g->area, column, row - 1))
		row--;
	double y = row * 16. - 32;
	Box body = {x + 3, y + 2, 10, 30};
	if (y < 0 || box_hits(g->r, g->area, body)) {
		say(g, "Нет свободного места для телепортации.");
		return false;
	}
	g->teleport_from_x = g->x;
	g->teleport_from_y = g->y;
	g->teleport_to_x = x;
	g->teleport_to_y = y;
	g->teleport_time = TELEPORT_DURATION;
	sound_play(g->r, SOUND_TELEPORT);
	g->teleport_cooldown = TELEPORT_DURATION + .2;
	g->vy = g->jump_boost = g->head_pause = 0;
	g->jump_full = false;

	return true;
}
static void extra_life(Game *g) {
	sound_play(g->r, SOUND_LIFE);
	/* Revision 5 stores up to five spare lives in addition to the current one. */
	if (g->lives < MAX_GAME_LIVES)
		g->lives++;
	else {
		g->hp = 6;
		game_score(g, 22500);
	}
}
void game_score(Game *g, int points) {
	if (points <= 0) return;
	points = (int)fmin(points, 999999999. - g->score);
	int old = g->score / 50000;
	g->score += points;
	int awards = g->score / 50000 - old;
	for (int i = 0; i < awards; i++)
		extra_life(g);
}

void game_collect(Game *g, int kind) {
	bool full_shield = g->hp == 6;
	game_score(g, 2000);
	sound_play(g->r, SOUND_PICKUP);
	char message[160];
	const char *name = "предмет";
	if (kind >= 8 && kind <= 12) {
		if (g->cola < 5)
			g->cola++;
		snprintf(message, sizeof message, "Бластер-кола: огненные шары — %d/5", g->cola);
		say(g, message);
		return;
	}
	switch (kind) {
	case SHIELD:
		if (full_shield)
			extra_life(g);
		g->hp = 6;
		name = "щит";
		break;
	case KEY:
		g->key = true;
		name = "ключ от двери";
		break;
	case BOOTS:
		g->boots = true;
		name = "ботинки";
		break;
	case CORK:
		g->corkscrew = true;
		say(g, "Штопор: огненные шары летят по волне.");
		return;
	case LANTERN:
		g->lantern = true;
		say(g, "Фонарь: теперь видно в тёмных помещениях.");
		return;
	case WAND:
		g->wand = true;
		say(g, "Телепортационная палочка! Клавиши — в справке F1.");
		return;
	case GEMS:
		g->treasures |= 1u;
		extra_life(g);
		name = "самоцветы";
		break;
	case CROWN:
		g->treasures |= 2u;
		extra_life(g);
		name = "Корона Эпох";
		break;
	case GOLD_ITEM:
		g->treasures |= 4u;
		extra_life(g);
		name = "мешок золота";
		break;
	default:
		break;
	}
	if (kind == CROWN && g->ending_stage == ENDING_NONE) {
		g->ending_stage = ENDING_DEPART;
		g->ending_time = 0;
		g->spawn_time = g->teleport_time = g->door_time = 0;
		g->shot_count = g->fire_pending = 0;
		sound_play(g->r, SOUND_ARRIVAL);
	}
	snprintf(message, sizeof message, "Получено: %s!", name);
	say(g, message);
}
static void finish_death(Game *g) {
	if (g->lives <= 0) {
		g->lives = 0;
		g->hp = 0;
		g->over = true;
		return;
	}
	g->hp = 6;
	g->energy = MAX_WEAPON_ENERGY;
	g->weapon_phase = 2;
	g->weapon_clock = WEAPON_TICK;
	g->area = g->checkpoint_area;
	g->x = g->checkpoint_x;
	g->y = g->checkpoint_y;
	g->vy = 0.;
	g->grounded = true;
	g->shot_count = 0;
	g->fire_pending = 0;
	g->shot_cooldown = 0;
	g->weapon_pulse = false;
	g->spawn_time = 0;
	g->jump_boost = 0;
	g->jump_full = false;
	g->head_pause = 0;
	g->invincible = 2.5;
	game_camera(g);
	say(g, "Снова в путь! Собранные предметы сохранены.");
}

void game_hurt(Game *g, bool fall) {
	if (g->death_time > 0 || g->teleport_time > 0 || g->spawn_time > 0 || g->door_time > 0 ||
		g->over || (g->invincible > 0 && !fall))
		return;
	g->invincible = 1.5;
	if (!fall && g->hp > 0) {
		g->hp--;
		sound_play(g->r, SOUND_HURT);
		say(g, "Щит повреждён!");
		return;
	}
	g->hp = 0;
	g->lives--;
	if (g->lives < 0)
		g->lives = 0;
	g->death_time = fall ? DEATH_PAUSE : DEATH_DURATION;
	sound_play(g->r, SOUND_DEATH);
	g->teleport_time = g->spawn_time = 0;
	g->vy = g->jump_boost = g->head_pause = 0;
	g->jump_full = false;
	g->shot_count = 0;
	g->fire_pending = 0;
	g->shot_cooldown = 0;
	g->weapon_pulse = false;
}

void game_cross(Game *g, int direction) {
	int scene = game_scene(g);
	Scene *sc = &g->r->areas[g->area].scenes[scene];
	int target = direction > 0 ? sc->right : sc->left;
	if (target < 0)
		return;
	double x = target * 2048. + (direction > 0 ? 0 : 2032);
	Box b = {x + 3, g->y + 2, 10, 30};
	if (!box_hits(g->r, g->area, b)) {
		clear_enemy_effects(g);
		g->checkpoint_area = g->area;
		g->checkpoint_x = x;
		g->checkpoint_y = g->y;
		g->x = x;
		g->shot_count = 0;
		g->fire_pending = 0;
		g->shot_cooldown = 0;
		g->weapon_pulse = false;
		game_camera(g);
		/* Revision 5, 0x1A87: DS:D3FF, priority 4, on either scene edge. */
		sound_play(g->r, SOUND_PANORAMA);
	}
}
int game_player_frame(const Game *g) {
	int pose = g->grounded ? (g->moving ? (int)(g->time * 10) % 3 + 1 : 0) : 4;
	return (g->face > 0 ? 0 : 5) + pose;
}
int game_enemy_frame(const Game *g, const Enemy *e) {
	if (e->shape < 0 || e->shape >= SHAPE_COUNT)
		return 0;
	const char *kind = SHAPE_FILES[e->shape];
	bool directional = strncmp(kind, "BIRD", 4) == 0 || strcmp(kind, "BEE") == 0;
	int count = g->r->shapes[e->shape].count;
	if (directional)
		count /= 2;
	if (!count)
		return 0;
	return (directional && e->vx > 0 ? count : 0) + (int)(g->time * 9) % count;
}
int game_enemy_hit_frame(const Enemy *e) {
	static const int sequence[] = {0, 1, 2, 1, 0};
	return e->hit_time > 0 ? sequence[game_effect_frame(e->hit_time, 5)] : -1;
}
static void enemy_die(Game *g, Enemy *e) {
	e->hit_frame = game_enemy_frame(g, e);
	e->hit_time = ENEMY_HIT_DURATION;
	sound_play(g->r, SOUND_HIT);
	e->alive = false;
	e->timer = 2.;
}
bool game_enemy_contact(const Game *g, const Enemy *e) {
	if (!e->alive || e->scene != game_scene(g) || e->shape < 0 || e->shape >= SHAPE_COUNT ||
		g->spawn_time > 0 || g->death_time > 0 || g->teleport_time > 0 || g->door_time > 0)
		return false;
	const SpriteBank *player = &g->r->player, *enemy = &g->r->shapes[e->shape];
	if (!player->opaque || !enemy->opaque)
		return false;
	int pf = game_player_frame(g), ef = game_enemy_frame(g, e);
	if (pf < 0 || pf >= player->count || ef < 0 || ef >= enemy->count)
		return false;
	/* The renderer uses this same world-pixel grid, independent of camera position. */
	int px = (int)lround(g->x), py = (int)lround(g->y), ex = (int)lround(e->x),
	    ey = (int)lround(e->y);
	int left = px > ex ? px : ex, right = px + 16 < ex + 16 ? px + 16 : ex + 16;
	int top = py > ey ? py : ey, bottom = py + player->height < ey + enemy->height
						      ? py + player->height
						      : ey + enemy->height;
	if (top < 0)
		top = 0;
	if (bottom > 160)
		bottom = 160;
	for (int y = top; y < bottom; y++) {
		uint16_t a = player->opaque[pf * player->height + y - py];
		uint16_t b = enemy->opaque[ef * enemy->height + y - ey];
		for (int x = left; x < right; x++)
			if ((a & (1u << (x - px))) && (b & (1u << (x - ex))))
				return true;
	}
	return false;
}
/* Revision 5 0x1AF3 dispatches on the map's behaviour byte, not the sprite.
 * Movement below follows their 8px coordinate grid and original tick dividers. */
static int enemy_behavior(const Enemy *e) {
	if (e->behavior)
		return e->behavior;
	/* Also support isolated sprite fixtures without a map record. */
	const char *kind = SHAPE_FILES[e->shape];
	if (!strcmp(kind, "BUG") || !strcmp(kind, "BALL") || !strcmp(kind, "FROG"))
		return 2;
	if (!strcmp(kind, "GLOBE"))
		return 3;
	if (!strcmp(kind, "BEE"))
		return 4;
	if (!strcmp(kind, "BIRD2") || !strcmp(kind, "CUBE"))
		return 5;
	return 1;
}
static bool enemy_spawn(Game *g, Enemy *e) {
	int left = e->scene * 2048;
	/* Try both screen edges, then heights around Comic; never appear in terrain. */
	for (int side = 0; side < 2; side++) {
		int edge = (side + (e->vx < 0 ? 1 : 0)) % 2;
		double x =
			floor(clamp(g->camera + (edge ? WIDTH - 16 : 0), left, left + 2032) / 8) *
			8;
		for (int offset = 0; offset < 160; offset += 16)
			for (int sign = 0; sign < (offset ? 2 : 1); sign++) {
				double y =
					floor(clamp(g->y + 8 + (sign ? -offset : offset), 0, 144) /
						8) *
					8;
				if (!box_hits(g->r, g->area, (Box){x, y, 16, 16}) &&
					!box_overlap(
						(Box){x, y, 16, 16}, (Box){g->x, g->y, 16, 32})) {
					if (enemy_behavior(e) == 3) {
						while (y + 8 <= 128 && !box_hits(g->r, g->area, (Box){x, y + 8, 16, 16}))
							y += 8;
					}
					e->x = x;
					e->y = y;
					e->vx = (g->x > x ? 1 : -1) * (4 / WEAPON_TICK);
					e->vy = enemy_behavior(e) == 1 ? -4 / WEAPON_TICK : 0;
					e->move_clock = 0;
					e->move_step = false;
					e->fall_speed = 0;
					e->alive = true;
					return true;
				}
			}
	}
	return false;
}
/* The DOS support probe is 3 coordinate units below the top (0xDBA).
 * Keep the original 8px lattice: a full 16px continuous AABB otherwise bridges
 * a one-tile hole forever, because it overlaps one bank on every small step. */
static bool enemy_floor(const Game *g, Box b, int probe_y) {
	return box_hits(g->r, g->area, (Box){b.x, b.y + probe_y, 16, 1});
}
static int enemy_direction(double delta) {
	return delta > 4 ? 1 : delta < -4 ? -1 : 0;
}
static void enemy_motion_tick(Game *g, Enemy *e, int behavior) {
	double speed = 4 / WEAPON_TICK;
	Box b = {e->x, e->y, 16, 16};
	Contacts c = {0};
	bool moving = !e->move_step;
	e->move_step = moving;
	if (behavior == 2) {
		/* 0x1C31: signed fixed-point velocity, +2 per tick, launch -7.
		 * Horizontal motion is one 8px step every other game tick. */
		if (!e->fall_speed && enemy_floor(g, b, behavior == 3 ? 16 : 24)) {
			e->vx = (g->x >= e->x ? 1 : -1) * speed;
			e->fall_speed = -7;
		} else {
			double dy = floor(e->fall_speed / 8.) * 8;
			c = move_box(g->r, g->area, &b, 0, dy);
			if (c.hit_y && dy < 0)
				e->fall_speed = 0;
			e->fall_speed += 2;
			if (e->fall_speed > 23)
				e->fall_speed = 23;
			if (moving)
				move_box(g->r, g->area, &b, e->vx > 0 ? 8 : -8, 0);
			if (e->fall_speed > 0 && enemy_floor(g, b, behavior == 3 ? 16 : 24)) {
				b.y = floor((b.y + 8) / 16) * 16;
				e->fall_speed = 0;
			}
		}
		e->vy = e->fall_speed / WEAPON_TICK;
	} else if (behavior == 3) {
		/* 0x1DB8: once falling, keep heading; descend 8px each tick.
		 * Select Comic's direction again only after landing. */
		if (e->vy > 0)
			b.y += 8;
		else
			e->vx = enemy_direction(g->x - e->x) * speed;
		if (moving && e->vx)
			move_box(g->r, g->area, &b, e->vx > 0 ? 8 : -8, 0);
		if (!enemy_floor(g, b, behavior == 3 ? 16 : 24))
			e->vy = 8 / WEAPON_TICK;
		else {
			if (e->vy > 0)
				b.y = floor((b.y + 8) / 16) * 16;
			e->vy = 0;
		}
	} else if (moving && behavior == 4) {
		/* 0x1E45: pursue along X first; use Y when aligned or blocked.
		 * The original bee does not move diagonally on every tick. */
		int direction = enemy_direction(g->x - e->x);
		e->vx = direction * speed;
		c = move_box(g->r, g->area, &b, direction * 8, 0);
		if (!direction || c.hit_x)
			move_box(g->r, g->area, &b, 0, enemy_direction(g->y - e->y) * 8);
	} else if (moving && (behavior == 1 || behavior == 5)) {
		if (!e->vx)
			e->vx = (g->x > e->x ? 1 : -1) * speed;
		else
			e->vx = copysign(speed, e->vx);
		if (behavior == 5) {
			/* 0x1F00: shy flyers rise when watched, otherwise approach Y. */
			bool watched = (g->x < e->x && g->face > 0) || (g->x > e->x && g->face < 0);
			e->vy = (watched ? -1 : enemy_direction(g->y - e->y)) * speed;
		} else
			e->vy = e->vy ? copysign(speed, e->vy) : -speed;
		c = move_box(g->r, g->area, &b, e->vx > 0 ? 8 : -8,
			e->vy > 0   ? 8
			: e->vy < 0 ? -8
				    : 0);
		if (c.hit_x)
			e->vx = -e->vx;
		if (c.hit_y || b.y <= 0 || b.y >= 144) {
			b.y = clamp(b.y, 0, 144);
			e->vy = -e->vy;
		}
	}
	e->x = clamp(b.x, e->scene * 2048., e->scene * 2048. + 2032);
	e->y = b.y;
	if (e->x != b.x)
		e->vx = -e->vx;
	/* Falling enemies disappear below the field; no kill score/explosion. */
	if ((behavior == 2 || behavior == 3) && e->y >= 144) {
		e->alive = false;
		e->timer = 2.;
	}
}
static void enemy_update(Game *g, Enemy *e, double dt) {
	e->hit_time = fmax(0, e->hit_time - dt);
	if (e->hit_time < 1e-10)
		e->hit_time = 0;
	int behavior = enemy_behavior(e);
	if (behavior == 127) {
		e->alive = false;
		return;
	}
	e->timer = fmax(0, e->timer - dt);
	if (!e->alive) {
		if (e->timer <= 0 && !enemy_spawn(g, e))
			e->timer = WEAPON_TICK;
		return;
	}
	e->move_clock += dt;
	while (e->move_clock + 1e-10 >= WEAPON_TICK && e->alive) {
		e->move_clock = fmax(0, e->move_clock - WEAPON_TICK);
		enemy_motion_tick(g, e, behavior);
	}
	if (e->x < g->camera - 64 || e->x > g->camera + WIDTH + 64) {
		e->alive = false;
		e->timer = 2. + fmod(e->phase, .4);
	}
	if (e->alive && game_enemy_contact(g, e)) {
		enemy_die(g, e);
		game_hurt(g, false);
	}
}
static void shot_remove(Game *g, int index) {
	for (int j = index; j < g->shot_count - 1; j++)
		g->shots[j] = g->shots[j + 1];
	g->shot_count--;
}
static bool fire_shot(Game *g) {
	if (g->title || g->paused || g->over || g->won || g->help_open || g->location_menu ||
		g->scores_open || g->spawn_time > 0 || g->death_time > 0 || g->teleport_time > 0 ||
		g->door_time > 0 || g->energy <= 0 || !g->cola || g->shot_cooldown > 1e-10)
		return false;
	/* Release invisible projectiles before testing the one-can firing slot.
	 * Do this on key-down too, so a short tap need not wait for a physics step. */
	for (int i = g->shot_count - 1; i >= 0; i--)
		if (g->shots[i].ttl <= 0 || g->shots[i].x + 8 < g->camera ||
			g->shots[i].x - 8 >= g->camera + WIDTH)
			shot_remove(g, i);
	if (g->shot_count >= g->cola || g->shot_count >= MAX_SHOTS)
		return false;

	double y = g->y + 14;
	g->shots[g->shot_count++] = (Shot){g->x + (g->face > 0 ? 16 : -4), y, y, 1.6, 0, g->face};
	g->shot_cooldown = WEAPON_TICK;
	sound_play(g->r, SOUND_SHOT);
	/* Preserve a short press until the independent energy clock samples it. */
	g->weapon_pulse = true;
	return true;
}
static void request_fire(Game *g) {
	if (fire_shot(g))
		return;
	/* Preserve distinct taps during the weapon's cadence interval. Reserve only
	 * currently available slots, so a one-can weapon cannot queue a barrage. */
	if (g->energy > 0 && g->cola > 0 && g->shot_cooldown > 1e-10 &&
		g->shot_count + g->fire_pending < g->cola &&
		g->shot_count + g->fire_pending < MAX_SHOTS)
		g->fire_pending++;
}
static void update_ending(Game *g, double dt) {
	g->ending_time += dt;
	if (g->ending_stage == ENDING_DEPART && g->ending_time >= 1.6) {
		g->ending_time -= 1.6;
		g->ending_stage = ENDING_CONVERT;
	}
	if (g->ending_stage == ENDING_CONVERT) {
		while (g->ending_time + 1e-10 >= .4) {
			g->ending_time = fmax(0, g->ending_time - .4);
			if (g->lives > 0) {
				g->lives--;
				/* Conversion awards points, never replacement lives. */
				g->score = (int)fmin(999999999., g->score + 1000.);
				sound_play(g->r, SOUND_PICKUP);
			} else {
				g->ending_stage = ENDING_WAIT;
				break;
			}
		}
	}
	if (g->ending_stage == ENDING_WAIT && g->ending_time >= 1.5) {
		g->ending_time = 0;
		g->ending_stage = ENDING_PAGE;
		g->won = true;
	}
}
void game_update(Game *g, double dt, Controls controls) {
	if (g->ending_stage > ENDING_NONE && g->ending_stage < ENDING_PAGE) {
		update_ending(g, dt);
		return;
	}
	if (g->title || g->paused || g->won || g->over || g->help_open || g->location_menu ||
		g->scores_open) {
		g->fire_pending = 0;
		return;
	}
	if (g->door_time > 0) {
		double before = g->door_time;
		g->door_time = fmax(0, g->door_time - dt);
		if (before > 5 * WEAPON_TICK + 1e-10 && g->door_time <= 5 * WEAPON_TICK + 1e-10)
			sound_play(g->r, SOUND_DOOR);
		if (before > 8 * WEAPON_TICK + 1e-10 && g->door_time <= 8 * WEAPON_TICK + 1e-10) {
			double remaining = g->door_time;
			game_travel(g, g->door_to_area, g->door_to_x, g->door_to_y);
			g->door_time = remaining;
			g->door_x = g->x - 8;
			g->door_y = g->y;
		}
		if (g->door_time < 1e-10)
			g->door_time = 0;
		return;
	}
	if (g->death_time > 0) {
		/* Contact explosions finish while Comic's fatal animation plays. */
		Scene *scene = &g->r->areas[g->area].scenes[game_scene(g)];
		for (int i = 0; i < scene->enemy_count; i++) {
			Enemy *e = &g->enemies[g->area][game_scene(g)][i];
			e->hit_time = fmax(0, e->hit_time - dt);
			if (e->hit_time < 1e-10)
				e->hit_time = 0;
		}
		g->death_time = fmax(0, g->death_time - dt);
		if (g->death_time < 1e-10) {
			g->death_time = 0;
			finish_death(g);
		}
		return;
	}
	if (g->teleport_time > 0) {
		g->teleport_time = fmax(0, g->teleport_time - dt);
		g->teleport_cooldown = fmax(0, g->teleport_cooldown - dt);
		if (g->teleport_time <= 3 * WEAPON_TICK + 1e-10) {
			g->x = g->teleport_to_x;
			g->y = g->teleport_to_y;
			game_camera(g);
		}
		if (g->teleport_time < 1e-10)
			g->teleport_time = 0;
		return;
	}
	if (g->spawn_time > 0) {
		g->spawn_time = fmax(0, g->spawn_time - dt);
		if (g->spawn_time < 1e-10)
			g->spawn_time = 0;
		return;
	}
	g->time += dt;
	/* DOS 0x18A4..0x18C2 samples jump only during its 3/4 support ticks.
	 * Releasing later must not shorten an already completed full jump. */
	if (g->jump_boost > 1e-10 && !controls.jump)
		g->jump_full = false;
	g->jump_boost = fmax(0, g->jump_boost - dt);
	g->head_pause = fmax(0, g->head_pause - dt);
	g->invincible = fmax(0, g->invincible - dt);
	g->message_time = fmax(0, g->message_time - dt);
	g->teleport_cooldown = fmax(0, g->teleport_cooldown - dt);
	g->shot_cooldown = fmax(0, g->shot_cooldown - dt);
	int direction = (controls.right ? 1 : 0) - (controls.left ? 1 : 0);
	g->moving = direction != 0;
	if (direction)
		g->face = direction;
	int scene = game_scene(g);
	double low = scene * 2048., high = low + 2032;
	double nx = clamp(g->x + direction * 80 * dt, low, high);
	/* Previously every descent used short-hop gravity, cutting full-jump range.
	 * Calibrate the smooth arc against DOS flight/apex measurements, not just height. */
	double gravity = g->jump_full ? (g->area == MOON ? (g->vy < 0 ? 210 : 260) : 420)
				      : (g->area == MOON ? 400 : 800);
	bool reached_apex = g->vy < 0 && g->vy + gravity * dt >= 0;
	g->vy = g->head_pause > 0 ? 0 : fmin(310, g->vy + gravity * dt);
	/* Boots add a zero-displacement apex tick in the original fixed-point arc. */
	if (reached_apex && g->jump_full && g->boots && g->area != MOON) {
		g->head_pause = WEAPON_TICK;
		g->vy = 0;
	}
	Box b = {g->x + 3, g->y + 2, 10, 30};
	Contacts c = move_box(g->r, g->area, &b, nx - g->x, g->vy * dt);
	g->x = b.x - 3;
	/* Original 0x18D1 clamps the sprite top to zero without cancelling its jump. */
	g->y = fmax(0, b.y - 2);
	g->grounded = c.ground;
	if (c.ground)
		g->jump_full = false;
	/* The original held-jump boost can push upward again after an early head bump.
	 * Keep that short boost window without allowing movement through the ceiling. */
	if (c.hit_y && (c.ground || !controls.jump || g->jump_boost <= 0)) {
		/* DOS movement has two zero-displacement ticks after an upward impact.
		 * Retain that interval instead of falling immediately at 120 Hz. */
		if (!c.ground && g->vy < 0)
			g->head_pause = 2 * WEAPON_TICK;
		g->vy = 0;
	}
	if (direction && ((direction < 0 && g->x <= low) || (direction > 0 && g->x >= high)))
		game_cross(g, direction);
	if (g->y > 190) {
		game_hurt(g, true);
		return;
	}
	if (g->energy <= 0 || !g->cola)
		g->fire_pending = 0;
	if ((controls.fire || g->fire_pending > 0) && fire_shot(g) && g->fire_pending > 0)
		g->fire_pending--;
	/* DOS 0x1786: shared two-tick drain/recharge phase, even when slots are full. */
	g->weapon_clock += dt;
	while (g->weapon_clock + 1e-12 >= WEAPON_TICK) {
		g->weapon_clock -= WEAPON_TICK;
		bool firing = controls.fire || g->weapon_pulse;
		g->weapon_pulse = false;
		if (firing) {
			if (g->energy == 0)
				continue;
			if (g->weapon_phase == 1) {
				g->weapon_phase = 2;
				continue;
			}
			g->energy--;
		}
		if (--g->weapon_phase == 0) {
			if (g->energy < MAX_WEAPON_ENERGY)
				g->energy++;
			g->weapon_phase = 2;
		}
	}

	scene = game_scene(g);
	Scene *sc = &g->r->areas[g->area].scenes[scene];
	for (int i = 0; i < g->shot_count;) {
		Shot *s = &g->shots[i];
		s->x += s->face * 210 * dt;
		s->age += dt;
		s->ttl -= dt;
		if (g->corkscrew)
			/* Original 0x207A..0x208C alternates +8/-8 world pixels.
			 * Interpolate between its two heights at the shared DOS tick. */
			s->y = s->y0 + 8 * (1 - fabs(fmod(s->age / WEAPON_TICK, 2) - 1));
		bool remove = s->ttl <= 0 || s->x < scene * 2048 || s->x >= (scene + 1) * 2048 ||
			      s->x + 8 < g->camera || s->x - 8 >= g->camera + WIDTH;
		if (!remove)
			for (int j = 0; j < sc->enemy_count; j++) {
				Enemy *e = &g->enemies[g->area][scene][j];
				if (e->alive && s->x >= e->x && s->x < e->x + 16 && s->y >= e->y &&
					s->y < e->y + 16) {
					enemy_die(g, e);
					game_score(g, 300);
					remove = true;
					break;
				}
			}
		if (remove)
			shot_remove(g, i);
		else
			i++;
	}
	int active_area = g->area, active_scene = scene;
	for (int i = 0; i < sc->enemy_count; i++) {
		enemy_update(g, &g->enemies[active_area][active_scene][i], dt);
		if (g->area != active_area || game_scene(g) != active_scene)
			break;
	}
	if (g->death_time > 0)
		return;
	// A transition may have changed the current location.
	scene = game_scene(g);
	sc = &g->r->areas[g->area].scenes[scene];
	if (sc->item.present && !g->taken[g->area][scene] &&
		box_overlap((Box){g->x + 3, g->y + 2, 10, 30},
			(Box){scene * 2048. + sc->item.x, sc->item.y, 16, 16})) {
		g->taken[g->area][scene] = true;
		game_collect(g, sc->item.kind);
	}
	game_camera(g);
}
int game_setting_value(const Game *g, int field) {
	switch (field) {
	case SET_COLA:
		return g->cola;
	case SET_SHIELD:
		return g->hp;
	case SET_LIVES:
		return g->lives;
	case SET_KEY:
		return g->key;
	case SET_BOOTS:
		return g->boots;
	case SET_CORK:
		return g->corkscrew;
	case SET_LANTERN:
		return g->lantern;
	case SET_WAND:
		return g->wand;
	case SET_GEMS:
		return (g->treasures & 1u) != 0;
	case SET_GOLD:
		return (g->treasures & 4u) != 0;
	case SET_CROWN:
		return (g->treasures & 2u) != 0;
	default:
		return 0;
	}
}
void game_setting_change(Game *g, int field, int delta) {
	if (field < 0 || field >= SETTINGS_COUNT || field == SET_CROWN)
		return;
	int step = delta ? delta : 1;
	if (field == SET_COLA) {
		g->cola = (int)clamp((double)g->cola + step, 0, MAX_SHOTS);
		if (g->shot_count > g->cola)
			g->shot_count = g->cola;
		return;
	}
	if (field == SET_SHIELD) {
		g->hp = (int)clamp((double)g->hp + step, 0, 6);
		return;
	}
	if (field == SET_LIVES) {
		g->lives = (int)clamp((double)g->lives + step, 0, MAX_EDITOR_LIVES);
		if (!g->lives) {
			g->death_time = g->teleport_time = g->spawn_time = g->door_time = 0;
			g->over = true;
			g->shot_count = 0;
			g->fire_pending = 0;
			g->shot_cooldown = 0;
			g->weapon_pulse = false;
		} else if (g->over) {
			g->over = false;
			g->hp = 6;
			g->area = g->checkpoint_area;
			g->x = g->checkpoint_x;
			g->y = g->checkpoint_y;
			g->vy = 0;
			g->grounded = true;
			g->spawn_time = ARRIVAL_DURATION;
			g->invincible = 2.;
			game_camera(g);
		}
		g->won = g->treasures == 7u && g->lives > 0;
		return;
	}
	bool enabled = delta ? delta > 0 : !game_setting_value(g, field);
	switch (field) {
	case SET_KEY:
		g->key = enabled;
		break;
	case SET_BOOTS:
		g->boots = enabled;
		break;
	case SET_CORK:
		g->corkscrew = enabled;
		break;
	case SET_LANTERN:
		g->lantern = enabled;
		break;
	case SET_WAND:
		g->wand = enabled;
		break;
	default: {
		unsigned bit = field == SET_GEMS ? 1u : field == SET_GOLD ? 4u : 2u;
		if (enabled)
			g->treasures |= bit;
		else
			g->treasures &= ~bit;
		g->won = g->treasures == 7u && g->lives > 0;
		break;
	}
	}
}
static void save_bindings(Game *g) {
	if (!g->r->config_path[0])
		say(g, "Клавиши изменены для текущего запуска.");
	else if (bindings_save(&g->r->bindings, g->r->config_path))
		say(g, "Настройки клавиш сохранены.");
	else
		say(g, "Клавиши изменены, но сохранить файл не удалось.");
}
int game_key(Game *g, SDL_Scancode key, bool repeat) {
	if (g->ending_stage > ENDING_NONE && g->ending_stage < ENDING_PAGE)
		return !repeat && key == SDL_SCANCODE_Q ? 1 : 0;
	if (g->scores_open) {
		if (repeat && (!g->score_entry || key != SDL_SCANCODE_BACKSPACE))
			return 0;
		return game_scores_key(g, key);
	}
	/* The final page owns every fresh key, including function keys and modifiers. */
	if (g->won && !g->help_open && !g->location_menu) {
		if (!repeat && key > SDL_SCANCODE_UNKNOWN && key < SDL_NUM_SCANCODES)
			game_scores_open(g, true);
		return 0;
	}
	if (key == SDL_SCANCODE_F2) {
		if (!repeat) {
			sound_set_enabled(g->r, !g->r->audio.enabled);
			say(g, g->r->audio.enabled ? "Звук включён." : "Звук выключен.");
		}
		return 0;
	}
	if (repeat)
		return 0;
	if (key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F1 || key == SDL_SCANCODE_F4 ||
		bindings_match(&g->r->bindings, BIND_PAUSE, key))
		g->fire_pending = 0;
	if (g->location_menu && g->settings_page == 2 && g->binding_wait) {
		if (key == SDL_SCANCODE_ESCAPE) {
			g->binding_wait = false;
			say(g, "Назначение клавиши отменено.");
		} else if (key != SDL_SCANCODE_UNKNOWN &&
			   bindings_assign(&g->r->bindings, g->binding_row, g->binding_slot, key)) {
			g->binding_wait = false;
			save_bindings(g);
		} else
			say(g, "Клавиша занята или служебная. Выберите другую; Esc — отмена.");
		return 0;
	}
	bool modal = g->title || g->paused || g->over || g->won || g->help_open || g->location_menu;
	if (key == SDL_SCANCODE_F5) {
		if (!g->title && !g->over && !g->won && g->death_time <= 0) {
			g->hp = 6;
			say(g, "Энергия щита восстановлена до максимума.");
		}
		return 0;
	}
	if (key == SDL_SCANCODE_Q && modal)
		return 1;
	if (key == SDL_SCANCODE_F11)
		return 2;
	if (key == SDL_SCANCODE_F1) {
		g->help_open = !g->help_open;
		return 0;
	}
	if (key == SDL_SCANCODE_ESCAPE) {
		if (g->help_open || g->location_menu) {
			g->help_open = g->location_menu = false;
			if (!g->title)
				g->paused = true;
		} else if (!g->title && !g->over && !g->won)
			g->paused = !g->paused;
		return 0;
	}
	if (key == SDL_SCANCODE_F4) {
		if (g->help_open) {
			g->help_open = false;
			g->location_menu = true;
		} else
			g->location_menu = !g->location_menu;
		if (g->location_menu)
			for (int row = 0; row < AREA_COUNT; row++)
				if (MENU_AREAS[row] == g->area) g->location_row = row;
		return 0;
	}
	if (g->help_open) {
		if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_PAGEDOWN)
			g->help_page = (g->help_page + 1) % 6;
		else if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_PAGEUP)
			g->help_page = (g->help_page + 5) % 6;
		return 0;
	}
	
	if (g->location_menu) {
		if (key == SDL_SCANCODE_TAB || key == SDL_SCANCODE_PAGEUP ||
			key == SDL_SCANCODE_PAGEDOWN) {
			g->settings_page =
				(g->settings_page + (key == SDL_SCANCODE_PAGEUP ? SETTINGS_PAGES - 1 : 1)) % SETTINGS_PAGES;
			return 0;
		}
		if (g->settings_page == 3) {
			(void)score_edit_key(g, key, false);
			return 0;
		}
		if (g->settings_page == 2) {
			if (key == SDL_SCANCODE_UP)
				g->binding_row = (g->binding_row + BIND_COUNT - 1) % BIND_COUNT;
			else if (key == SDL_SCANCODE_DOWN)
				g->binding_row = (g->binding_row + 1) % BIND_COUNT;
			else if (key == SDL_SCANCODE_LEFT)
				g->binding_slot = (g->binding_slot + BIND_SLOTS - 1) % BIND_SLOTS;
			else if (key == SDL_SCANCODE_RIGHT)
				g->binding_slot = (g->binding_slot + 1) % BIND_SLOTS;
			else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
				g->binding_wait = true;
			else if (key == SDL_SCANCODE_BACKSPACE) {
				(void)bindings_assign(&g->r->bindings, g->binding_row,
					g->binding_slot, SDL_SCANCODE_UNKNOWN);
				save_bindings(g);
			} else if (key == SDL_SCANCODE_R) {
				bindings_defaults(&g->r->bindings);
				save_bindings(g);
			}
			return 0;
		}
		if (g->settings_page == 1) {
			if (key == SDL_SCANCODE_UP)
				g->settings_row =
					(g->settings_row + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
			else if (key == SDL_SCANCODE_DOWN)
				g->settings_row = (g->settings_row + 1) % SETTINGS_COUNT;
			else if (key == SDL_SCANCODE_LEFT)
				game_setting_change(g, g->settings_row, -1);
			else if (key == SDL_SCANCODE_RIGHT)
				game_setting_change(g, g->settings_row, 1);
			else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER ||
				 key == SDL_SCANCODE_SPACE)
				game_setting_change(g, g->settings_row, 0);
			return 0;
		}
		if (key == SDL_SCANCODE_UP)
			g->location_row = (g->location_row + AREA_COUNT - 1) % AREA_COUNT;
		else if (key == SDL_SCANCODE_DOWN)
			g->location_row = (g->location_row + 1) % AREA_COUNT;
		else if (key == SDL_SCANCODE_HOME)
			g->location_row = 0;
		else if (key == SDL_SCANCODE_END)
			g->location_row = AREA_COUNT - 1;
		else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER || key == SDL_SCANCODE_SPACE)
			(void)game_enter_area(g, MENU_AREAS[g->location_row]);
		else if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_8)
			(void)game_enter_area(g, MENU_AREAS[key - SDL_SCANCODE_1]);
		else if (key == SDL_SCANCODE_R) {
			Resources *r = g->r;
			game_reset(g, r);
		}
		return 0;
	}
	
	if (g->title) {
		if (key == SDL_SCANCODE_R) {
			game_scores_open(g, false);
			return 0;
		}
		if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER ||
			key == SDL_SCANCODE_SPACE || key == SDL_SCANCODE_RIGHT ||
			key == SDL_SCANCODE_PAGEDOWN) {
			if (g->intro_page < 2)
				g->intro_page++;
			else {
				g->title = false;
				sound_play(g->r, SOUND_ARRIVAL);
			}
		} else if ((key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_BACKSPACE ||
				   key == SDL_SCANCODE_PAGEUP) &&
			   g->intro_page > 0)
			g->intro_page--;
		return 0;
	}
	if (key == SDL_SCANCODE_RETURN && (g->over || g->won)) {
		game_scores_open(g, true);
		return 0;
	}
	if (bindings_match(&g->r->bindings, BIND_PAUSE, key) && !g->title && !g->over && !g->won) {
		g->paused = !g->paused;
		return 0;
	}
	if (g->paused && key == SDL_SCANCODE_R) {
		g->paused = false;
		game_scores_open(g, false);
		return 0;
	}
	if (g->title || g->paused || g->over || g->won || g->spawn_time > 0 || g->death_time > 0 ||
		g->teleport_time > 0 || g->door_time > 0)
		return 0;
	if (g->test_mode && (key == SDL_SCANCODE_PAGEUP || key == SDL_SCANCODE_PAGEDOWN))
		game_explore(g, g->area, game_scene(g) + (key == SDL_SCANCODE_PAGEDOWN ? 1 : -1));
	else if (bindings_match(&g->r->bindings, BIND_LEFT, key))
		g->face = -1;
	else if (bindings_match(&g->r->bindings, BIND_RIGHT, key))
		g->face = 1;
	else if (bindings_match(&g->r->bindings, BIND_FIRE, key))
		request_fire(g);
	else if (bindings_match(&g->r->bindings, BIND_JUMP, key))
		game_jump(g);
	else if (bindings_match(&g->r->bindings, BIND_TELEPORT, key))
		(void)game_teleport(g);
	else if (bindings_match(&g->r->bindings, BIND_DOOR, key))
		game_interact(g);
	else if (key == SDL_SCANCODE_F3)
		g->debug = !g->debug;
	return 0;
}

