#ifndef CC_H
#define CC_H
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>
#define VERSION "0.5.63"
#define WIDTH 320
#define HUD_HEIGHT 40
#define STATUS_HEIGHT 14
#define HEIGHT (HUD_HEIGHT + 160 + STATUS_HEIGHT)
#define STEP (1.0 / 120.0)
#define AREA_COUNT 8
#define SCENE_WIDTH 2048
#define WORLD_WIDTH 6144
#define MAX_SHOTS 5
#define MAX_WEAPON_ENERGY 12
/* One DOS game tick spans two standard BIOS timer interrupts. */
#define WEAPON_TICK (2.0 * 65536.0 / (14318180.0 / 12.0))
#define ARRIVAL_DURATION (12 * WEAPON_TICK)
#define ENEMY_HIT_DURATION (5 * WEAPON_TICK)
#define DEATH_ANIMATION_DURATION (8 * WEAPON_TICK)
#define DEATH_PAUSE (17 * WEAPON_TICK)
#define DEATH_DURATION (DEATH_ANIMATION_DURATION + DEATH_PAUSE)
#define TELEPORT_DURATION (6 * WEAPON_TICK)
#define DOOR_DURATION (12 * WEAPON_TICK)
#define MAX_GAME_LIVES 6
#define MAX_EDITOR_LIVES 99
#define SHAPE_COUNT 14
#define KEY 1
#define CORK 0
#define BOOTS 2
#define LANTERN 3
#define WAND 4
#define GEMS 5
#define CROWN 6
#define GOLD_ITEM 7
#define SHIELD 14
#define SETTINGS_PAGES 4

enum {
	LAKE,
	FOREST,
	MOON,
	BASE,
	CAVE,
	BUILDING,
	CASTLE,
	TUBES
};
enum {
	SET_COLA,
	SET_SHIELD,
	SET_LIVES,
	SET_KEY,
	SET_BOOTS,
	SET_CORK,
	SET_LANTERN,
	SET_WAND,
	SET_GEMS,
	SET_GOLD,
	SET_CROWN,
	SETTINGS_COUNT
};
typedef struct {
	double x, y, w, h;
} Box;
typedef struct {
	int x, y, area, scene;
} Door;
typedef struct {
	int kind, x, y, sprite;
	bool present;
} Item;
typedef struct {
	int shape, behavior;
	bool delayed;
} EnemySpec;
typedef struct {
	Item item;
	int left, right;
	Door doors[3];
	int door_count;
	EnemySpec enemies[4];
	int enemy_count;
} Scene;
typedef struct {
	SDL_Texture *texture;
	int count, height;
	uint16_t *opaque;
} SpriteBank;
typedef struct {
	char name[16];
	int last_passable, tile_count;
	uint8_t cells[3][1280];
	Scene scenes[3];
	SDL_Texture *tiles;
} Area;
enum {
	BIND_LEFT,
	BIND_RIGHT,
	BIND_JUMP,
	BIND_FIRE,
	BIND_DOOR,
	BIND_TELEPORT,
	BIND_PAUSE,
	BIND_COUNT
};
#define BIND_SLOTS 3
typedef struct {
	SDL_Scancode key[BIND_COUNT][BIND_SLOTS];
} KeyBindings;
#define SCORE_COUNT 10
#define SCORE_NAME_CHARS 16
#define SCORE_NAME_BYTES 65
#ifdef __EMSCRIPTEN__
void web_restore_saves(void);
#endif
typedef struct {
	int score;
	char name[SCORE_NAME_BYTES];
} ScoreEntry;
typedef struct {
	ScoreEntry entries[SCORE_COUNT];
	int count;
} ScoreTable;
typedef struct {
	SDL_Texture *texture;
	TTF_Font *font;
	SDL_Color color;
	char text[256];
	int w, h;
	double middle;
} TextCache;
enum {
	SOUND_SHOT,
	SOUND_HIT,
	SOUND_HURT,
	SOUND_DEATH,
	SOUND_PICKUP,
	SOUND_DOOR,
	SOUND_TELEPORT,
	SOUND_ARRIVAL,
	SOUND_LIFE,
	SOUND_THEME,
	SOUND_PANORAMA,
	SOUND_COUNT
};
typedef struct {
	int effect, frame, length, note, ticks;
	double phase;
} SoundVoice;
typedef struct {
	SDL_AudioDeviceID device;
	int rate;
	bool enabled;
	SoundVoice voices[1];
} AudioState;
typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *canvas, *intro_logo, *story_logo, *legend_logo, *ending_logo, *hud_icons, *odometer;
	SDL_Texture *life_icons[2];
	Area areas[8];
	SpriteBank player, items, projectile, arrival, death, teleport, enemy_hit, shapes[14];
	TTF_Font *fonts[513];
	TextCache text_cache[64];
	char asset_dir[4096], config_path[4096], score_path[4096];
	ScoreTable scores;
	KeyBindings bindings;
	AudioState audio;
} Resources;
typedef struct {
	double x, y, origin, phase, vx, vy, timer, hit_time, move_clock;
	int shape, scene, behavior, hit_frame, fall_speed;
	bool alive, move_step;
} Enemy;
typedef struct {
	double x, y, y0, ttl, age;
	int face;
} Shot;
enum { ENDING_NONE, ENDING_DEPART, ENDING_CONVERT, ENDING_WAIT, ENDING_PAGE };
typedef struct {
	int ending_stage;
	double ending_time;
	Resources *r;
	int area, face, hp, lives, cola, score;
	int energy, weapon_phase;
	unsigned treasures;
	double spawn_time, jump_boost, head_pause, death_time;
	double door_time, door_x, door_y, door_to_x, door_to_y;
	int door_to_area;
	double teleport_time, teleport_from_x, teleport_from_y, teleport_to_x, teleport_to_y;
	double x, y, vy, camera, time, weapon_clock, shot_cooldown, invincible, teleport_cooldown;
	bool weapon_pulse;
	bool grounded, moving, jump_full, key, boots, corkscrew, lantern, wand;
	bool title, paused, over, won, debug, location_menu, help_open, test_mode;
	int help_page, intro_page, settings_page, settings_row, location_row;
	int binding_row, binding_slot;
	bool binding_wait;
	bool scores_open, score_entry, score_after_game, score_submitted;
	char score_name[SCORE_NAME_BYTES], score_notice[160];
	int checkpoint_area;
	double checkpoint_x, checkpoint_y;
	bool taken[8][3];
	Enemy enemies[8][3][4];
	Shot shots[5];
	int shot_count, fire_pending;
	char message[160];
	double message_time;
} Game;
typedef struct {
	bool hit_x, hit_y, ground;
} Contacts;
typedef struct {
	bool left, right, jump, fire;
} Controls;
extern const char *const AREA_FILES[8];
extern const char *const AREA_TITLES[8];
extern const char *const AREA_NAMES_EN[8];
extern const char *const SHAPE_FILES[14];
extern const int MENU_AREAS[8];

bool replace_save_file(const char *temporary, const char *path);
bool resources_load(Resources *r, const char *dir);
void resources_destroy(Resources *r);
bool sound_init(Resources *r);
void sound_close(Resources *r);
void sound_stop(Resources *r);
void sound_play(Resources *r, int effect);
void sound_set_enabled(Resources *r, bool enabled);
void sound_mix(AudioState *audio, float *output, int frames);
#if defined(__GNUC__) || defined(__clang__)
#define CC_PRINTF(format_index, first_arg) __attribute__((format(printf, format_index, first_arg)))
#else
#define CC_PRINTF(format_index, first_arg)
#endif
void fail_message(const char *format, ...) CC_PRINTF(1, 2);
bool box_overlap(Box a, Box b);
bool tile_solid(const Resources *r, int area, int column, int row);
bool box_hits(const Resources *r, int area, Box body);
Contacts move_box(const Resources *r, int area, Box *body, double dx, double dy);
bool spawn_near(const Resources *r, int area, double desired, int segment, double *x, double *y);
extern const char *const BIND_TITLES[BIND_COUNT];
void bindings_defaults(KeyBindings *b);
bool bindings_match(const KeyBindings *b, int action, SDL_Scancode key);
bool bindings_assign(KeyBindings *b, int action, int slot, SDL_Scancode key);
bool bindings_load(KeyBindings *b, const char *path);
bool bindings_save(const KeyBindings *b, const char *path);
const char *binding_key_name(SDL_Scancode key);
void bindings_describe(const KeyBindings *b, int action, char *out, size_t size);
Controls bindings_controls(const KeyBindings *b, const Uint8 *keys, int count);
bool score_qualifies(const ScoreTable *table, int score);
bool score_add(ScoreTable *table, int score, const char *name);
bool scores_load(ScoreTable *table, const char *path);
bool scores_save(const ScoreTable *table, const char *path);
void game_scores_open(Game *g, bool after_game);
void game_score_text(Game *g, const char *text);
int game_scores_key(Game *g, SDL_Scancode key);
void game_reset(Game *g, Resources *r);
int game_scene(const Game *g);
int game_player_frame(const Game *g);
int game_enemy_frame(const Game *g, const Enemy *e);
int game_enemy_hit_frame(const Enemy *e);
int game_death_frame(const Game *g);
bool game_enemy_contact(const Game *g, const Enemy *e);
void game_camera(Game *g);
void game_update(Game *g, double dt, Controls controls);
void game_jump(Game *g);
void game_interact(Game *g);
bool game_teleport(Game *g);
int game_effect_frame(double remaining, int frames);
void game_explore(Game *g, int area, int scene);
bool game_enter_area(Game *g, int area);
void game_travel(Game *g, int area, double x, double y);
void game_hurt(Game *g, bool fall);
void game_collect(Game *g, int kind);
void game_score(Game *g, int points);
void game_cross(Game *g, int direction);
int game_setting_value(const Game *g, int field);
void game_setting_change(Game *g, int field, int delta);
int game_key(Game *g, SDL_Scancode key, bool repeat);
TTF_Font *resources_open_font(int size);
void resources_shutdown(Resources *r);
bool score_edit_key(Game *g, SDL_Scancode key, SDL_Keymod modifiers);
int game_key_event(Game *g, const SDL_KeyboardEvent *event);
void score_edit_text(Game *g, const char *text);
bool game_text_input(const Game *g);
void game_draw(Game *g);
int self_test(Resources *r);
bool save_screenshot(Resources *r, const char *path);

#endif

