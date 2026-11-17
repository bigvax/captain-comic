#include "cc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
		
static void draw_ending(Resources *r);

typedef struct {
	char text[160];
	int x, y, max_width;
	SDL_Color color;
	double size;
	bool right, center_y, center_x;
} Label;

static Label labels[64];
static int label_count;
static const SDL_Color WHITE = {235, 235, 255, 255}, CYAN = {85, 255, 255, 255},
		       GOLD = {255, 220, 60, 255};

static void label(int x, int y, SDL_Color color, bool big, const char *format, ...) {
	if (label_count >= 64)
		return;
	Label *l = &labels[label_count++];
	l->x = x;
	l->y = y;
	l->max_width = 0;
	l->color = color;
	l->size = big ? 16 : 7;
	l->right = false;
	l->center_x = false;
	l->center_y = false;
	va_list args;
	va_start(args, format);
	vsnprintf(l->text, sizeof l->text, format, args);
	va_end(args);
}

static void fill(Resources *r, int x, int y, int w, int h, SDL_Color color) {
	SDL_SetRenderDrawColor(r->renderer, color.r, color.g, color.b, color.a);
	SDL_Rect box = {x, y, w, h};
	SDL_RenderFillRect(r->renderer, &box);
}

static void sprite(
	Resources *r, SpriteBank bank, int index, int x, int y, int w, int h, int alpha) {
	if (index < 0 || index >= bank.count)
		return;
	SDL_Rect source = {(index % 16) * 16, (index / 16) * bank.height, 16, bank.height},
		 dest = {x, y, w, h};
	SDL_SetTextureAlphaMod(bank.texture, (Uint8)alpha);
	SDL_RenderCopy(r->renderer, bank.texture, &source, &dest);
	SDL_SetTextureAlphaMod(bank.texture, 255);
}

typedef struct {
	SDL_Rect box;
} PanelFrame;

static PanelFrame frames[32];
static int frame_count;

static void recess(Resources *r, int x, int y, int w, int h) {
	fill(r, x, y, w, h, (SDL_Color){24, 26, 30, 255});
	if (frame_count < 32)
		frames[frame_count++].box = (SDL_Rect){x, y, w, h};
}

static void digits_scaled(Resources *r, int value, int x, int y, bool small) {
	static const unsigned masks[] = {
		0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
	static const SDL_Rect segments[] = {
		{1, 0, 3, 1}, {4, 1, 1, 3}, {4, 5, 1, 3}, {1, 8, 3, 1},
		{0, 5, 1, 3}, {0, 1, 1, 3}, {1, 4, 3, 1}
	};
	char number[16];
	snprintf(number, sizeof number, "%09d", value < 0 ? 0 : value % 1000000000);

	int ox = small ? (x + 18) : x;
	int oy = small ? (y - 1) : y;
	SDL_Color lit = {255, 75, 75, 255};

	for (int i = 0; i < 9; i++) {
		unsigned m = masks[number[i] - '0'];
		for (int j = 0; j < 7; j++) {
			if (m & (1u << j)) {
				SDL_Rect b = segments[j];
				fill(r, ox + i * 6 + b.x, oy + b.y, b.w, b.h, lit);
			}
		}
	}
}

static void digits(Resources *r, int value, int x, int y) {
	digits_scaled(r, value, x, y, false);
}
typedef struct {
	SDL_Texture *texture;
	SDL_Rect source;
	SDL_FRect box;
	Uint8 alpha;
} HudIcon;

static HudIcon hud_icons[32];
static int hud_icon_count;
static bool lives_visible;
enum { LIFE_ICON_PX = 32, LIFE_GAP_PX = 8 };

static void hud_icon(
	SDL_Texture *texture, SDL_Rect source, float x, float y, float w, float h, int alpha) {
	if (hud_icon_count >= 32 || !texture)
		return;
	hud_icons[hud_icon_count++] = (HudIcon){texture, source, {x, y, w, h}, (Uint8)alpha};
}

static void hud_item(Resources *r, int index, float x, float y, float size, int alpha) {
	hud_icon(r->items.texture, (SDL_Rect){index * 16, 0, 16, 16}, x, y, size, size, alpha);
}

enum {
	HUD_ICON_GAP = 4
};

static void draw_smooth_badge(SDL_Renderer *renderer, int x, int y, int w, int h, int radius) {
	if (radius * 2 > w) radius = w / 2;
	if (radius * 2 > h) radius = h / 2;
	for (int dy = 0; dy < h; dy++) {
		int inset = 0;
		float edge_alpha = 1.0f;
		int dist_y = dy < radius ? (radius - dy) : (dy >= h - radius ? (dy - (h - radius - 1)) : 0);
		if (dist_y > 0) {
			float fy = (float)dist_y - 0.5f;
			float r_f = (float)radius;
			if (fy < r_f) {
				float fx = sqrtf(r_f * r_f - fy * fy);
				float exact_inset = r_f - fx;
				inset = (int)floorf(exact_inset);
				edge_alpha = 1.0f - (exact_inset - (float)inset);
			} else {
				inset = radius;
				edge_alpha = 0.0f;
			}
		}
		int row_x = x + inset;
		int row_w = w - inset * 2;
		if (row_w <= 0) continue;
		Uint8 base_r = (dy == 0) ? 215 : (dy == h - 1) ? 120 : 170;
		Uint8 base_g = (dy == 0) ? 218 : (dy == h - 1) ? 124 : 172;
		Uint8 base_b = (dy == 0) ? 224 : (dy == h - 1) ? 130 : 176;
		if (inset > 0 && edge_alpha > 0.05f && row_w >= 2) {
			Uint8 ea = (Uint8)lround(edge_alpha * 255.0f);
			SDL_SetRenderDrawColor(renderer, base_r, base_g, base_b, ea);
			SDL_RenderDrawPoint(renderer, row_x, y + dy);
			SDL_RenderDrawPoint(renderer, row_x + row_w - 1, y + dy);
			if (row_w > 2) {
				SDL_SetRenderDrawColor(renderer, base_r, base_g, base_b, 255);
				SDL_RenderDrawLine(renderer, row_x + 1, y + dy, row_x + row_w - 2, y + dy);
			}
		} else {
			SDL_SetRenderDrawColor(renderer, base_r, base_g, base_b, 255);
			SDL_RenderDrawLine(renderer, row_x, y + dy, row_x + row_w - 1, y + dy);
		}
	}
}

static void hud_atlas_icon(Resources *r, int index, float x, float y, float size) {
	if (!r->hud_icons) return;
	if ((index == 2 || index == 3) && r->life_icons[index - 2]) {
		int width = 0; SDL_QueryTexture(r->life_icons[index - 2], NULL, NULL, &width, NULL);
		if (width > 2) {
			float pad = size / (width - 2);
			hud_icon(r->life_icons[index - 2], (SDL_Rect){0, 0, width, width},
				x - pad, y - pad, size + 2 * pad, size + 2 * pad, 255);
			return;
		}
	}
	int cell = 16;
	SDL_QueryTexture(r->hud_icons, NULL, NULL, NULL, &cell);
	if (cell < 1) cell = 16;
	hud_icon(r->hud_icons, (SDL_Rect){index * cell, 0, cell, cell}, x, y, size, size, 255);
}

static void gauge(Resources *r, int x, int y, double fraction, int icon) {
	recess(r, x, y, 12, 12);
	fill(r, x, y, 12, 12, (SDL_Color){170, 170, 170, 255});
	int bar_x = x + 12 + HUD_ICON_GAP;
	recess(r, bar_x, y, 38, 12);
	hud_atlas_icon(r, icon == SHIELD ? 1 : 0, x + 1, y + 1, 10);
	for (int i = 0; i < 12; i++)
		fill(r, bar_x + 1 + i * 3, y + 3, 2, 6,
			i < fraction * 12 ? (SDL_Color){205, 0, 0, 255}
					  : (SDL_Color){45, 0, 0, 255});
}

static void intro_text(int x, int y, double size, SDL_Color color, const char *text);

static void draw_hud(Game *g) {
	Resources *r = g->r;
	SDL_Color gray = {205, 205, 205, 255};
	fill(r, 0, 0, WIDTH, HUD_HEIGHT, (SDL_Color){170, 170, 170, 255});

	/* 1. Вертикальный механический счётчик (16x16) + индикатор очков (x=3..81) */
	recess(r, 3, 5, 12, 12);
	fill(r, 3, 5, 12, 12, (SDL_Color){170, 170, 170, 255});
	{
		if (!r->odometer) {
			SDL_Surface *os = SDL_CreateRGBSurfaceWithFormat(0, 160, 16, 32, SDL_PIXELFORMAT_RGBA8888);
			if (os) {
				static const uint8_t d3x5[10][5] = {
					{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
					{7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2}, {7,5,7,5,7}, {7,5,7,1,7}
				};
				SDL_FillRect(os, NULL, 0x00000000);
				Uint32 c_brd  = SDL_MapRGBA(os->format, 20, 24, 28, 255);
				Uint32 c_rim1 = SDL_MapRGBA(os->format, 75, 82, 90, 255);
				Uint32 c_rim2 = SDL_MapRGBA(os->format, 125, 132, 140, 255);
				Uint32 c_bg   = SDL_MapRGBA(os->format, 24, 26, 30, 255);
				Uint32 c_lit  = SDL_MapRGBA(os->format, 255, 75, 75, 255);
				for (int k = 0; k < 10; k++) {
					int bx = k * 16;
					SDL_Rect frame = {bx + 1, 0, 14, 16};
					SDL_FillRect(os, &frame, c_brd);
					int vals[3] = {0, 0, k};
					for (int d = 0; d < 3; d++) {
						int dx = bx + 2 + d * 4;
						SDL_Rect r_t1 = {dx, 1, 3, 1}, r_b1 = {dx, 14, 3, 1};
						SDL_Rect r_t2 = {dx, 2, 3, 1}, r_b2 = {dx, 13, 3, 1};
						SDL_Rect r_win = {dx, 3, 3, 10};
						SDL_FillRect(os, &r_t1, c_rim1); SDL_FillRect(os, &r_b1, c_rim1);
						SDL_FillRect(os, &r_t2, c_rim2); SDL_FillRect(os, &r_b2, c_rim2);
						SDL_FillRect(os, &r_win, c_bg);
						int dig = vals[d];
						for (int py = 0; py < 5; py++) {
							for (int px = 0; px < 3; px++) {
								if (d3x5[dig][py] & (1 << (2 - px))) {
									SDL_Rect dot = {dx + px, 5 + py, 1, 1};
									SDL_FillRect(os, &dot, c_lit);
								}
							}
						}
					}
				}
				r->odometer = SDL_CreateTextureFromSurface(r->renderer, os);
				if (r->odometer) {
					SDL_SetTextureBlendMode(r->odometer, SDL_BLENDMODE_BLEND);
					SDL_SetTextureScaleMode(r->odometer, SDL_ScaleModeNearest);
				}
				SDL_FreeSurface(os);
			}
		}
		if (r->odometer) {
			int frame_idx = (g->score > 0 ? (g->score / 100) : 0) % 10;
			hud_icon(r->odometer, (SDL_Rect){frame_idx * 16, 0, 16, 16}, 4, 6, 10, 10, 255);
		}
	}
	recess(r, 19, 5, 62, 12);
	digits(r, g->score, 23, 6);

	/* 2. Банки колы (x=85..119) */
	recess(r, 85, 5, 12, 12);
	fill(r, 85, 5, 12, 12, (SDL_Color){170, 170, 170, 255});
	hud_item(r, 8, 86, 6, 10, 255);
	recess(r, 101, 5, 18, 12);
	char cola[16];
	snprintf(cola, sizeof cola, "%d/5", g->cola);
	intro_text(110, 11, 5.5, gray, cola);
	labels[label_count - 1].center_x = true;
	labels[label_count - 1].center_y = true;

	/* 3. Заголовок строго по центру экрана (x=160) между левым (119) и правым (201) блоками */
	intro_text(WIDTH / 2, 11, 9, (SDL_Color){35, 40, 45, 255}, "Капитан КОМИК");
	labels[label_count - 1].center_x = true;
	labels[label_count - 1].center_y = true;

	/* 4. Правый блок: локация (x=201..279) + панорама (x=283..317), всего 116px */
	recess(r, 201, 5, 12, 12);
	fill(r, 201, 5, 12, 12, (SDL_Color){170, 170, 170, 255});
	hud_atlas_icon(r, 4 + (g->area % 8), 202, 6, 10);
	recess(r, 217, 5, 62, 12);
	intro_text(221, 11, 5.5, gray, AREA_TITLES[g->area]);
	labels[label_count - 1].max_width = 54;
	labels[label_count - 1].center_y = true;

	/* Слот иконки панорамы (индекс 13 в hud-icons.png или встроенная пиктограмма 3 экранов) */
	int seg = game_scene(g);
	recess(r, 283, 5, 12, 12);
	fill(r, 283, 5, 12, 12, (SDL_Color){170, 170, 170, 255});
	int atlas_w = 0, atlas_h = 16;
	if (r->hud_icons) SDL_QueryTexture(r->hud_icons, NULL, NULL, &atlas_w, &atlas_h);
	if (r->hud_icons && atlas_h > 0 && (atlas_w / atlas_h) > 13) {
		hud_atlas_icon(r, 13, 284, 6, 10);
	} else {
		static SDL_Texture *pano_tex[3] = {NULL, NULL, NULL};
		static SDL_Renderer *pano_ren = NULL;
		int idx = (seg >= 0 && seg < 3) ? seg : 0;
		if (pano_ren != r->renderer) {
			for (int k = 0; k < 3; k++) {
				if (pano_tex[k]) SDL_DestroyTexture(pano_tex[k]);
				SDL_Surface *ps = SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 32, SDL_PIXELFORMAT_RGBA8888);
				if (ps) {
					SDL_FillRect(ps, NULL, 0x00000000);
					Uint32 c_brd = SDL_MapRGBA(ps->format, 20, 24, 28, 255);
					Uint32 c_off = SDL_MapRGBA(ps->format, 65, 75, 85, 255);
					Uint32 c_on  = SDL_MapRGBA(ps->format, 85, 255, 255, 255);
					SDL_Rect outer = {0, 1, 16, 14};
					SDL_FillRect(ps, &outer, c_brd);
					for (int s = 0; s < 3; s++) {
						SDL_Rect cell = {1 + s * 5, 2, 4, 12};
						SDL_FillRect(ps, &cell, (s == k) ? c_on : c_off);
					}
					pano_tex[k] = SDL_CreateTextureFromSurface(r->renderer, ps);
					if (pano_tex[k]) {
						SDL_SetTextureBlendMode(pano_tex[k], SDL_BLENDMODE_BLEND);
						SDL_SetTextureScaleMode(pano_tex[k], SDL_ScaleModeNearest);
					}
					SDL_FreeSurface(ps);
				}
			}
			pano_ren = r->renderer;
		}
		if (pano_tex[idx]) {
			hud_icon(pano_tex[idx], (SDL_Rect){0, 0, 16, 16}, 284, 6, 10, 10, 255);
		}
	}

	recess(r, 299, 5, 18, 12);
	char scene_txt[16];
	snprintf(scene_txt, sizeof scene_txt, "%d/3", seg + 1);
	intro_text(308, 11, 5.5, gray, scene_txt);
	labels[label_count - 1].center_x = true;
	labels[label_count - 1].center_y = true;

	/* 5. Нижний ряд: шкалы оружия и щита, запасные жизни, инвентарь */
	gauge(r, 3, 23, g->energy / (double)MAX_WEAPON_ENERGY, 8);
	gauge(r, 65, 23, g->hp / 6., SHIELD);

	lives_visible = true;

	const int icon_size = 10, icon_padding = 4;
	const int inventory_width = 8 * icon_size + 7 * HUD_ICON_GAP + 2 * icon_padding;
	const int inventory_x = WIDTH - 3 - inventory_width;
	recess(r, inventory_x, 23, inventory_width, 12);
	int indices[] = {KEY, CORK, BOOTS, LANTERN, WAND, GEMS, GOLD_ITEM, CROWN};
	bool active[] = {g->key, g->corkscrew, g->boots, g->lantern, g->wand,
		(g->treasures & 1u) != 0, (g->treasures & 4u) != 0, (g->treasures & 2u) != 0};
	for (int i = 0; i < 8; i++)
		hud_item(r, indices[i], inventory_x + icon_padding + i * (icon_size + HUD_ICON_GAP),
			24, icon_size, active[i] ? 255 : 90);
}

static void intro_text(int x, int y, double size, SDL_Color color, const char *text) {
	label(x, y, color, false, "%s", text);
	if (label_count > 0)
		labels[label_count - 1].size = size;
}
static void draw_intro(Game *g) {
	Resources *r = g->r;
	SDL_Renderer *renderer = r->renderer;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (g->intro_page == 0) {
		SDL_Rect page = {0, 0, 320, 200};
		SDL_RenderCopy(renderer, r->intro_logo, NULL, &page);
	} else if (g->intro_page == 1) {
		SDL_Rect page = {0, 0, WIDTH, 200};
		SDL_RenderCopy(renderer, r->story_logo, NULL, &page);
	} else {
		SDL_Rect page = {0, 0, WIDTH, 200};
		SDL_RenderCopy(renderer, r->legend_logo, NULL, &page);
	}
	fill(r, 0, 200, 320, STATUS_HEIGHT, (SDL_Color){170, 170, 170, 255});
	recess(r, 0, 202, WIDTH, 10);
	intro_text(6, 207, 6, CYAN,
		g->intro_page == 2
			? "3/3   Enter / Пробел — начать,        Влево — назад,        R — рекорды,        Q — выход"
			: "Enter / Пробел — далее,        Влево — назад,        R — рекорды,        Q — выход");
	labels[label_count - 1].center_y = true;
}

static void draw_scores(Game *g) {
	Resources *r = g->r;
	fill(r, 0, 0, WIDTH, HEIGHT, (SDL_Color){170, 170, 170, 255});
	for (int row = 0; row < 2; row++) {
		int y = row ? 178 : 1;
		for (int col = 0; col < 3; col++) {
			int x = (col == 0) ? 16 : (col == 1) ? (WIDTH / 2) : 304;
			bool cx = (col == 1);
			bool rt = (col == 2);

			intro_text(x + 1, y + 1, 6.5, (SDL_Color){100, 100, 100, 255}, "КАПИТАН");
			labels[label_count - 1].center_x = cx;
			labels[label_count - 1].right = rt;

			intro_text(x, y, 6.5, (SDL_Color){230, 230, 230, 255}, "КАПИТАН");
			labels[label_count - 1].center_x = cx;
			labels[label_count - 1].right = rt;

			intro_text(x + 1, y + 8, 12, (SDL_Color){100, 100, 100, 255}, "КОМИК");
			labels[label_count - 1].center_x = cx;
			labels[label_count - 1].right = rt;

			intro_text(x, y + 7, 12, (SDL_Color){220, 220, 220, 255}, "КОМИК");
			labels[label_count - 1].center_x = cx;
			labels[label_count - 1].right = rt;
		}
	}
	intro_text(WIDTH / 2 + 1, 26, 8.5, (SDL_Color){100, 100, 100, 255}, "ЛУЧШИЕ РЕЗУЛЬТАТЫ");
	labels[label_count - 1].center_x = true;
	labels[label_count - 1].center_y = true;
	intro_text(WIDTH / 2, 25, 8.5, (SDL_Color){255, 255, 85, 255}, "ЛУЧШИЕ РЕЗУЛЬТАТЫ");
	labels[label_count - 1].center_x = true;
	labels[label_count - 1].center_y = true;

	recess(r, 16, 34, 288, 140);
	fill(r, 16, 34, 288, 140, (SDL_Color){0, 0, 0, 255});
	SDL_Color red = {255, 85, 85, 255};
	if (g->score_entry) {
		intro_text(WIDTH / 2, 48, 9, GOLD, "НОВЫЙ РЕКОРД!");
		labels[label_count - 1].center_x = true;
		label(WIDTH / 2, 66, red, false, "Счёт: %09d", g->score);
		labels[label_count - 1].center_x = true;
		intro_text(54, 81, 6, CYAN, "Введите имя (до 16 символов):");
		recess(r, 54, 92, 212, 20);
		char name[SCORE_NAME_BYTES + 4];
		snprintf(name, sizeof name, "%s%s", g->score_name,
			SDL_GetTicks() % 1000 < 500 ? "_" : "");
		intro_text(60, 102, 9, CYAN, name);
		labels[label_count - 1].center_y = true;
		intro_text(WIDTH / 2, 132, 6, WHITE, "Enter — сохранить,    Esc — пропустить");
		labels[label_count - 1].center_x = true;
		intro_text(WIDTH / 2, 148, 5.5, WHITE, "Backspace — удалить последнюю букву");
		labels[label_count - 1].center_x = true;
	} else {
		for (int i = 0; i < SCORE_COUNT; i++) {
			int y = 40 + i * 12;
			if (i > 0) {
				y += 6;
			}
			const ScoreEntry *entry = &r->scores.entries[i];
			if (i < r->scores.count) {
				intro_text(32, y + 8, 10.0, CYAN, entry->name);
				labels[label_count - 1].center_y = true;
			}
			digits_scaled(r, i < r->scores.count ? entry->score : 0, 214, y + 4, true);
		}
	}
	recess(r, 0, 202, WIDTH, 10);
	intro_text(6, 207, 5.5, CYAN,
		g->score_after_game && !g->score_entry ? "Любая клавиша — выход из игры"
		: g->score_notice[0] != '\0' ? g->score_notice
		: g->score_entry      ? "Введите имя и нажмите Enter."
		: "Enter / Esc — назад,    Q / Й — выход");
	labels[label_count - 1].center_y = true;
}

static const char *const help_titles[] = {
	"УПРАВЛЕНИЕ", "10 ВИДОВ ПРЕДМЕТОВ", "СНАРЯЖЕНИЕ И УЛУЧШЕНИЯ", "КЛАВИШИ И НАСТРОЙКИ",
	"БЕСТИАРИЙ (1/2)", "БЕСТИАРИЙ (2/2)"};
static const char *const help_lines[4][8] = {
	{NULL},
	{"Всего {c}10 видов{/c}: 3 сокровища, 5 улучшений, кола и щит.",
		"{c}Корона Эпох{/c} — Замок, экран 1.",
		"{c}Самоцветы{/c} — Луна Тамби, экран 3.",
		"{c}Мешок золота{/c} — Пещера слепых жаб, экран 2.",
		"{c}Бластер-кола{/c}: 5 банок — лес 1; озеро 1 и 3;\nЛуна 1; ангар 2. До 5 одновременных выстрелов.",
		"В этом порте {c}корона{/c} запускает финал независимо\nот остальных сокровищ; жизни превращаются в очки.",
		"Экран 1/2/3 соответствует файлу 0.pt/1.pt/2.pt.",
		"Расположение указано по текущим картам порта."},
	{"{c}Щит{/c} — восполняет энергию (при полном даёт жизнь).\nВсего 11 щитов в 8 локациях; полный список — в README.",
		"{c}Бластер-кола{/c} — оружие; прерывайте огонь для перезарядки.",
		"{c}Ключ от двери{/c} — Скалистый лес, экран 3; открывает двери.",
		"{c}Штопор{/c} — Космическая база, экран 2; волнистые выстрелы.",
		"{c}Ботинки{/c} — Космическая база, экран 3; высокий прыжок.",
		"{c}Стек телепортации{/c} — пещера, экран 3;\nуправляемая телепортация на 3 блока.",
		"{c}Фонарь{/c} — Комплекс, конец экрана 3; освещает тёмные залы.",
		"{c}Жизнь{/c} даётся за сокровище, полный щит или 50 000 очков."},
	{"{c}F1{/c} — открыть или закрыть эту справку.",
		"{c}F2{/c} — включить или выключить звук в игре.",
		"{c}F3{/c} — показать границы твёрдых блоков.",
		"{c}F4{/c} — меню настроек (локации, герой, клавиши, рекорды).\nЛокации: {g}Вверх/Вниз{/g} — выбор; {g}Enter{/g} — вход; {g}1–8{/g} — сразу.",
		"{c}F5{/c} — полностью восстановить энергию щита.",
		"{c}F11{/c} — переключить полноэкранный режим.",
		"В меню настроек ({c}F4{/c}): {g}Tab{/g} / {g}PgUp{/g} / {g}PgDn{/g} — вкладки,\n{g}R{/g} — начать обычную новую игру.",
		"На вкладке героя в меню {c}F4{/c}: {g}Вверх{/g} / {g}Вниз{/g} — выбор строки,\n{g}Влево{/g} / {g}Вправо{/g} — изменить значение или предмет."}};


static TTF_Font *font(Resources *r, int size) {
	if (size < 1) size = 1;
	if (size > 512) size = 512;
	if (!r->fonts[size]) r->fonts[size] = resources_open_font(size);
	return r->fonts[size];
}


static void draw_rich_line(Resources *r, int x, int y, double size, SDL_Color base_color, const char *text) {
	TTF_Font *f12 = font(r, 12);
	int cur_x = x;
	const char *p = text;
	while (p && *p) {
		SDL_Color col = base_color;
		const char *seg_start = p;
		const char *seg_end = NULL;
		if (strncmp(p, "{c}", 3) == 0) {
			col = CYAN;
			seg_start = p + 3;
			seg_end = strstr(seg_start, "{/c}");
			p = seg_end ? seg_end + 4 : NULL;
		} else if (strncmp(p, "{g}", 3) == 0) {
			col = GOLD;
			seg_start = p + 3;
			seg_end = strstr(seg_start, "{/g}");
			p = seg_end ? seg_end + 4 : NULL;
		} else {
			const char *next_c = strstr(p, "{c}");
			const char *next_g = strstr(p, "{g}");
			if (next_c && (!next_g || next_c < next_g)) seg_end = next_c;
			else if (next_g) seg_end = next_g;
			p = seg_end;
		}
		int len = seg_end ? (int)(seg_end - seg_start) : (int)strlen(seg_start);
		if (len > 0) {
			char buf[256];
			if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
			memcpy(buf, seg_start, (size_t)len);
			buf[len] = '\0';
			label(cur_x, y, col, false, "%s", buf);
			labels[label_count - 1].size = size;
			int w12 = 0;
			if (f12) TTF_SizeUTF8(f12, buf, &w12, NULL);
			cur_x += (int)lround(w12 * (size / 12.0));
		}
	}
}

static void door_leaves(Game *g, int camera) {
	Resources *r = g->r;
	Area *a = &r->areas[g->area];
	int scene = game_scene(g), col = ((int)g->door_x % 2048) / 16;
	int row = (int)g->door_y / 16;
	for (int y = 0; y < 2 && row + y < 10; y++)
		for (int side = 0; side < 2 && col + side < 128; side++) {
			int tile = a->cells[scene][(row + y) * 128 + col + side];
			SDL_Rect source = {
				(tile % 16) * 16 + (side ? 0 : 8), (tile / 16) * 16, 8, 16};
			SDL_Rect dest = {(int)lround(g->door_x) - camera + side * 24,
				HUD_HEIGHT + (int)g->door_y + y * 16, 8, 16};
			SDL_RenderCopy(r->renderer, a->tiles, &source, &dest);
		}
}

static void draw_lives_pixels(Game *g, SDL_Rect dest, double sx, double sy) {
	Resources *r = g->r;
	SDL_Renderer *renderer = r->renderer;
	int spares = g->lives > 0 ? g->lives - 1 : 0;
	int height = (int)lround(12 * sy);
	if (29 * sy >= LIFE_ICON_PX + 8 && height < LIFE_ICON_PX + 8)
		height = LIFE_ICON_PX + 8;
	SDL_Rect panel = {(int)lround(dest.x + 125 * sx),
		(int)lround(dest.y + 29 * sy - height / 2.), (int)lround(70 * sx), height};
	int available = panel.w - 8, slots = 5, count_width = 0;
	if (available < 1) return;
	if (29 * sy < LIFE_ICON_PX + 8) slots = 0;
	while (slots && slots * LIFE_ICON_PX + (slots - 1) * LIFE_GAP_PX > available) slots--;
	char count[16] = "";
	TTF_Font *count_font = font(r, (int)lround(5.5 * sx));
	while (spares > slots) {
		int ignored = 0;
		snprintf(count, sizeof count, "+%d", spares - slots);
		if (count_font) TTF_SizeUTF8(count_font, count, &count_width, &ignored);
		int required = slots ? slots * LIFE_ICON_PX + slots * LIFE_GAP_PX : 0;
		if (!slots || required + count_width <= available) break;
		slots--;
	}
	int icons_width = slots ? slots * LIFE_ICON_PX + (slots - 1) * LIFE_GAP_PX : 0;
	int total = icons_width + (count[0] && slots ? LIFE_GAP_PX : 0) + count_width;
	int left = panel.x + (panel.w - total) / 2;
	int top = (int)lround(dest.y + 29 * sy) - LIFE_ICON_PX / 2;
	SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255); SDL_RenderFillRect(renderer, &panel);
	for (int edge = 0; edge < 2; edge++) {
		int x = panel.x + edge, y = panel.y + edge;
		int right = panel.x + panel.w - 1 - edge, bottom = panel.y + panel.h - 1 - edge;
		SDL_SetRenderDrawColor(renderer, edge ? 80 : 45, edge ? 82 : 47, edge ? 86 : 51, 255);
		SDL_RenderDrawLine(renderer, x, y, right, y); SDL_RenderDrawLine(renderer, x, y, x, bottom);
		SDL_SetRenderDrawColor(renderer, edge ? 220 : 245, edge ? 222 : 247, edge ? 226 : 251, 255);
		SDL_RenderDrawLine(renderer, right, y + 1, right, bottom); SDL_RenderDrawLine(renderer, x + 1, bottom, right, bottom);
	}
	for (int i = 0; i < slots; i++) {
		SDL_Texture *texture = r->life_icons[i < spares ? 0 : 1];
		int size = 0; SDL_QueryTexture(texture, NULL, NULL, &size, NULL);
		/* Copy the original cell at integer coordinates; exclude its filter padding. */
		SDL_Rect source = {1, 1, size - 2, size - 2};
		SDL_Rect box = {left + i * (LIFE_ICON_PX + LIFE_GAP_PX), top, LIFE_ICON_PX, LIFE_ICON_PX};
		SDL_RenderCopy(renderer, texture, &source, &box);
	}
	if (count[0]) {
		label((int)lround((left + total - dest.x) / sx), 29, (SDL_Color){25, 25, 25, 255}, false, "%s", count);
		Label *text = &labels[label_count - 1];
		text->size = 5.5; text->right = true; text->center_y = true;
		text->max_width = (int)floor(available / sx);
	}
}

void game_draw(Game *g) {
	Resources *r = g->r; SDL_Renderer *renderer = r->renderer;
	label_count = 0; hud_icon_count = 0; frame_count = 0; lives_visible = false;
	SDL_SetRenderTarget(renderer, r->canvas);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 32, 48, 96, 255); SDL_RenderClear(renderer);
	if (g->scores_open) { draw_scores(g); goto present; }
	if (g->title && !g->help_open && !g->location_menu) { draw_intro(g); goto present; }
	
	if (g->won && !g->help_open && !g->location_menu) { draw_ending(r); goto present; }
	int scene = game_scene(g), camera = (int)lround(g->camera);
	Area *area = &r->areas[g->area]; Scene *sc = &area->scenes[scene];
	SDL_Rect field = {0, HUD_HEIGHT, WIDTH, 160}; SDL_RenderSetClipRect(renderer, &field);
	if (g->area == CASTLE && !g->lantern) fill(r, 0, HUD_HEIGHT, WIDTH, 160, (SDL_Color){0, 0, 0, 255});
	else {
		int first = camera / 16, last = (camera + WIDTH + 15) / 16;
		for (int y = 0; y < 10; y++) for (int x = first; x <= last && x < (scene + 1) * 128; x++) {
			if (x < scene * 128) continue;
			int index = area->cells[scene][y * 128 + x % 128];
			SDL_Rect source = {(index % 16) * 16, (index / 16) * 16, 16, 16}, dest = {x * 16 - camera, y * 16 + HUD_HEIGHT, 16, 16};
			SDL_RenderCopy(renderer, area->tiles, &source, &dest);
		}
	}
	if (sc->item.present && !g->taken[g->area][scene]) sprite(r, r->items, sc->item.sprite, scene * 2048 + sc->item.x - camera, sc->item.y + HUD_HEIGHT, 16, 16, 255);
	for (int i = 0; i < sc->enemy_count; i++) {
		Enemy *e = &g->enemies[g->area][scene][i]; if (!e->alive && !(e->hit_time > 0 && game_effect_frame(e->hit_time, 5) < 3)) continue;
		sprite(r, r->shapes[e->shape], e->alive ? game_enemy_frame(g, e) : e->hit_frame, (int)lround(e->x) - camera, (int)lround(e->y) + HUD_HEIGHT, 16, 16, 255);
	}
	if (g->door_time > 0) {
		int stage = game_effect_frame(g->door_time, 12); bool half = stage == 0 || stage == 2 || stage == 8 || stage == 10;
		bool open = half || stage == 1 || stage == 9; bool hero = stage < 3 || stage >= 8;
		if (open) fill(r, (int)lround(g->door_x) - camera, (int)g->door_y + HUD_HEIGHT, 32, 32, (SDL_Color){0, 0, 0, 255});
		if (hero) sprite(r, r->player, g->face > 0 ? 0 : 5, (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
		if (half) door_leaves(g, camera);
	} else if (g->death_time > 0) {
		int frame = game_death_frame(g); if (frame >= 0 && frame < 4) sprite(r, r->player, game_player_frame(g), (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
		if (frame >= 0) sprite(r, r->death, frame, (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
	} else if (g->teleport_time > 0) {
		enum { SEQ_LEN = 5 };
		static const int sequence[SEQ_LEN] = {0, 1, 2, 1, 0};
		int stage = game_effect_frame(g->teleport_time, 6);
		sprite(r, r->player, g->face > 0 ? 0 : 5, (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
		if (stage < 5) sprite(r, r->teleport, sequence[stage], (int)lround(g->teleport_from_x) - camera, (int)lround(g->teleport_from_y) + HUD_HEIGHT, 16, 32, 255);
		if (stage > 0) sprite(r, r->teleport, sequence[stage - 1], (int)lround(g->teleport_to_x) - camera, (int)lround(g->teleport_to_y) + HUD_HEIGHT, 16, 32, 255);
	} else if (g->spawn_time > 0) {
		int frame = game_effect_frame(g->spawn_time, 12); if (frame >= 6) sprite(r, r->player, g->face > 0 ? 0 : 5, (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
		sprite(r, r->arrival, frame, (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
	} else if (!g->over) {
		if (g->ending_stage == ENDING_DEPART) {
			int rev_frame = 11 - (int)(g->ending_time * 12 / 1.6);
			if (rev_frame < 0) rev_frame = 0;
			if (rev_frame > 11) rev_frame = 11;
			if (rev_frame >= 6) {
				sprite(r, r->player, g->face > 0 ? 0 : 5, (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
			}
			sprite(r, r->arrival, rev_frame, (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
		} else if ((g->ending_stage == ENDING_NONE)) {
			sprite(r, r->player, game_player_frame(g), (int)lround(g->x) - camera, (int)g->y + HUD_HEIGHT, 16, 32, 255);
		}
	}
	for (int i = 0; i < sc->enemy_count; i++) { Enemy *e = &g->enemies[g->area][scene][i]; int frame = game_enemy_hit_frame(e); if (frame >= 0) sprite(r, r->enemy_hit, frame, (int)lround(e->x) - camera, (int)lround(e->y) + HUD_HEIGHT, 16, 16, 255); }
	for (int i = 0; i < g->shot_count; i++) { Shot s = g->shots[i]; int x = (int)lround(s.x) - camera, y = (int)lround(s.y) + HUD_HEIGHT; sprite(r, r->projectile, (int)(s.age * 12) % r->projectile.count, x - 8, y - 4, 16, 8, 255); }
	if (g->debug) {
		SDL_SetRenderDrawColor(renderer, 255, 80, 90, 255);
		for (int y = 0; y < 10; y++) for (int x = camera / 16; x <= (camera + WIDTH) / 16; x++) if (tile_solid(r, g->area, x, y)) { SDL_Rect box = {x * 16 - camera, y * 16 + HUD_HEIGHT, 16, 16}; SDL_RenderDrawRect(renderer, &box); }
	}
	SDL_RenderSetClipRect(renderer, NULL); draw_hud(g);
	const char *footer = g->message_time > 0 ? g->message : g->test_mode ? "Исследование: F4 — меню / PgUp,   PgDn — панорамы" : "F1 — справка / F2 — звук / F4 — настройки / F5 — щит";
	fill(r, 0, HUD_HEIGHT + 160, WIDTH, STATUS_HEIGHT, (SDL_Color){170, 170, 170, 255}); recess(r, 0, HUD_HEIGHT + 162, WIDTH, 10); intro_text(6, HUD_HEIGHT + 167, 5.5, CYAN, footer); labels[label_count - 1].center_y = true;
	if (g->help_open) {
		fill(r, 0, HUD_HEIGHT, WIDTH, 160, (SDL_Color){0, 0, 0, 235});
		label(18, HUD_HEIGHT + 4, GOLD, true, "СПРАВКА %d/6", g->help_page + 1);
		label(18, HUD_HEIGHT + 28, CYAN, false, "%s", help_titles[g->help_page]);
		if (g->help_page == 0) {
			enum { KEYS_BUF = 256, LINE_BUF = 384 };
			char keys[KEYS_BUF], line[LINE_BUF];
			int cur_y = HUD_HEIGHT + 40;
			for (int i = 0; i < BIND_COUNT; i++) {
				bindings_describe(&r->bindings, i, keys, sizeof keys);
				snprintf(line, sizeof line, "{c}%s{/c} — %s", keys, BIND_TITLES[i]);
				intro_text(18, cur_y, 6.2, GOLD, "•");
				draw_rich_line(r, 26, cur_y, 6.2, WHITE, line);
				cur_y += 12;
			}
			intro_text(18, cur_y, 6.2, GOLD, "•");
			draw_rich_line(r, 26, cur_y, 6.2, WHITE, "{c}Влево / Вправо / PgUp / PgDn{/c} — листать страницы справки");
		} else if (g->help_page >= 4) {
			static const char *const enemy_desc[14] = {
				"{c}Вертушка{/c} (Spinner) — летит к герою, взмывая под взглядом",
				"{c}Огненный шар{/c} (Fireball) — скачет по дуге и отскакивает от стен",
				"{c}Искра{/c} (Spark) — носится по диагонали, рикошетя от стен",
				"{c}Атом{/c} (Atom) — быстро рикошетит и преследует героя",
				"{c}Братья Фейерверки{/c} (F.W. Bros) — скачут и рикошетят в проёмах",
				"{c}Пляжный мяч{/c} (Beach Ball) — непрерывно прыгает в сторону героя",
				"{c}Слепая жаба{/c} (Blind Toad) — совершает высокие прыжки к герою",
				"{c}Смелая птица{/c} (Brave Bird) — летит на героя, выравнивая высоту",
				"{c}Робкая птица{/c} (Shy Bird) — атакует со спины, но взмывает на виду",
				"{c}Синий шар{/c} (Glow Globe) — катится по полу ниже выстрелов (нужен штопор)",
				"{c}Летающая тарелка{/c} (Saucer) — сближается по прямой и обходит стены",
				"{c}Космическая пыльца{/c} (Space Pollen) — прыгает по дуге и отскакивает от препятствий",
				"{c}Пучеглаз{/c} (Bug-Eyes) — семенит мелкими прыжками по полу к герою",
				"{c}Пчела-убийца{/c} (Killer Bee) — летит к герою, огибая стены по вертикали"
			};
			int start_idx = (g->help_page - 4) * 7;
			int anim = (int)(SDL_GetTicks() / 160);
			for (int k = 0; k < 7; k++) {
				int idx = start_idx + k;
				int row_y = HUD_HEIGHT + 41 + k * 14;
				SpriteBank bank = r->shapes[idx];
				if (bank.texture && bank.count > 0) {
					sprite(r, bank, anim % bank.count, 18, row_y - 2, 12, 12, 255);
				}
				int before = label_count;
				draw_rich_line(r, 35, row_y + 4, 5.5, WHITE, enemy_desc[idx]);
				for (int j = before; j < label_count; j++) {
					labels[j].center_y = true;
				}
			}
		} else {
			int cur_y = (g->help_page == 3) ? (HUD_HEIGHT + 39) : (HUD_HEIGHT + 42);
			int item_gap = 10;
			for (int i = 0; i < 8 && help_lines[g->help_page][i]; i++) {
				intro_text(18, cur_y, 6.2, GOLD, "•");
				const char *p = help_lines[g->help_page][i];
				while (p && *p) {
					const char *nl = strchr(p, '\n');
					int len = nl ? (int)(nl - p) : (int)strlen(p);
					char sub[320];
					if (len >= (int)sizeof(sub)) len = (int)sizeof(sub) - 1;
					memcpy(sub, p, (size_t)len);
					sub[len] = '\0';
					draw_rich_line(r, 26, cur_y, 6.2, WHITE, sub);
					if (nl) {
						cur_y += 8;
						p = nl + 1;
					} else {
						cur_y += item_gap;
						p = NULL;
					}
				}
			}
		}
		label(18, HUD_HEIGHT + 146, GOLD, false, "F1 — назад,   Esc — пауза,   Q / Й — выход");
	}
	else if (g->location_menu) {
		fill(r, 0, HUD_HEIGHT, WIDTH, 160, (SDL_Color){0, 0, 0, 238}); label(14, HUD_HEIGHT + 6, GOLD, false, "НАСТРОЙКИ / %s", g->settings_page == 3 ? "ТАБЛИЦА РЕКОРДОВ (4/4)" : g->settings_page == 2 ? "КЛАВИШИ (3/4)" : g->settings_page == 1 ? "ГЕРОЙ (2/4)" : "ЛОКАЦИИ (1/4)");
		if (!g->settings_page) {
			intro_text(14, HUD_HEIGHT + 19, 5.5, WHITE, "Выберите локацию стрелками и нажмите Enter:");
			for (int i = 0; i < 8; i++) {
				int a = MENU_AREAS[i];
				int row_y = HUD_HEIGHT + 32 + i * 12;
				if (i == g->location_row) fill(r, 12, row_y - 1, 296, 11, (SDL_Color){40, 55, 80, 255});
				hud_atlas_icon(r, 4 + a, 16, row_y, 9);
				int before = label_count;
				char loc_line[128];
				snprintf(loc_line, sizeof loc_line, "{c}%d. %s{/c} (%s)", i + 1, AREA_TITLES[a], AREA_NAMES_EN[a]);
				draw_rich_line(r, 30, row_y + 4, 7.0, (SDL_Color){165, 170, 185, 255}, loc_line);
				for (int j = before; j < label_count; j++) {
					labels[j].center_y = true;
					if (i == g->location_row) labels[j].color = GOLD;
				}
			}
			intro_text(14, HUD_HEIGHT + 136, 5.5, WHITE, "Вверх/Вниз — выбор; Enter — вход; 1–8 — сразу; R — новая игра");
			intro_text(14, HUD_HEIGHT + 148, 5.5, WHITE, "Tab / PgUp / PgDn — страницы настроек (1–4),   F4 — назад");
		} else if (g->settings_page == 2) {
			intro_text(14, HUD_HEIGHT + 18, 6, WHITE, "Вверх / Вниз — действие; Влево / Вправо — клавиша");
			for (int slot = 0; slot < BIND_SLOTS; slot++) { intro_text(141 + slot * 64, HUD_HEIGHT + 30, 5.5, CYAN, slot == 0 ? "Основная" : slot == 1 ? "Вторая" : "Третья"); labels[label_count - 1].center_x = true; }
			for (int row = 0; row < BIND_COUNT; row++) {
				int y = HUD_HEIGHT + 40 + row * 11; intro_text(20, y + 5, 6, row == g->binding_row ? GOLD : WHITE, BIND_TITLES[row]); labels[label_count - 1].center_y = true;
				for (int slot = 0; slot < BIND_SLOTS; slot++) {
					bool selected = row == g->binding_row && slot == g->binding_slot; if (selected) fill(r, 110 + slot * 64, y, 62, 10, (SDL_Color){40, 55, 80, 255});
					intro_text(141 + slot * 64, y + 5, 5.5, selected ? GOLD : CYAN, selected && g->binding_wait ? "Нажмите..." : binding_key_name(r->bindings.key[row][slot]));
					labels[label_count - 1].center_x = true; labels[label_count - 1].center_y = true;
				}
			}
			intro_text(14, HUD_HEIGHT + 124, 5.8, g->binding_wait ? GOLD : WHITE,
				g->binding_wait ? "Теперь нажмите новую клавишу (или Esc для отмены)."
						: "Выберите ячейку стрелками, нажмите Enter, затем новую клавишу.");
			intro_text(14, HUD_HEIGHT + 137, 5.5, WHITE, "Backspace — очистить ячейку;   R — исходные клавиши.");
			intro_text(14, HUD_HEIGHT + 150, 5.5, WHITE, "Tab / PgUp / PgDn — страницы настроек (1–4),   F4 — назад");
		} else if (g->settings_page == 1) {
			intro_text(14, HUD_HEIGHT + 18, 6, WHITE, "Вверх / Вниз — выбор; Влево / Вправо — убрать / добавить");
			static const char *const names[SETTINGS_COUNT] = {"Бластер-кола", "Энергия щита", "Жизни всего", "Ключ от двери", "Ботинки", "Штопор", "Фонарь", "Стек телепортации", "Самоцветы", "Мешок золота", "Корона Эпох"};
			static const int icons[SETTINGS_COUNT] = {8, SHIELD, 0, KEY, BOOTS, CORK, LANTERN, WAND, GEMS, GOLD_ITEM, CROWN};
			for (int i = 0; i < SETTINGS_COUNT; i++) {
				int y = HUD_HEIGHT + 31 + i * 10;
				bool disabled = (i == SET_CROWN);
				if (i == g->settings_row) fill(r, 12, y, 296, 10, disabled ? (SDL_Color){30, 32, 38, 255} : (SDL_Color){40, 55, 80, 255});
				if (i == SET_LIVES) hud_atlas_icon(r, 2, 18, y + 1, 8);
				else hud_item(r, icons[i], 18, y + 1, 8, disabled ? 90 : 255);
				SDL_Color color = disabled ? (SDL_Color){115, 115, 125, 255} : (i == g->settings_row ? GOLD : WHITE);
				label(34, y + 5, color, false, "%s", names[i]); labels[label_count - 1].center_y = true;
				int value = game_setting_value(g, i);
				if (disabled) label(266, y + 5, color, false, "Финал");
				else if (i < SET_KEY) label(266, y + 5, color, false, "%d/%d", value, i == SET_COLA ? MAX_SHOTS : i == SET_SHIELD ? 6 : 5);
				else label(266, y + 5, value ? CYAN : WHITE, false, "%s", value ? "ДА" : "НЕТ");
				labels[label_count - 1].center_y = true;
			}
			intro_text(14, HUD_HEIGHT + 145, 5.5, WHITE, "Enter — изменить,   Tab / PgUp / PgDn — страницы настроек (1–4)");
		} else if (g->settings_page == 3) {
			if (g->settings_row >= SCORE_COUNT) g->settings_row = SCORE_COUNT - 1;
			for (int i = 0; i < SCORE_COUNT; i++) {
				int y = HUD_HEIGHT + 16 + i * 10; ScoreEntry *entry = &r->scores.entries[i];
				if (i == g->settings_row) fill(r, 12, y, 296, 10, (SDL_Color){40, 55, 80, 255});
				SDL_Color row_color = (i == g->settings_row) ? GOLD : CYAN;
				if (i < r->scores.count) { label(16, y + 5, row_color, false, "%d. %s", i + 1, entry->name); labels[label_count - 1].center_y = true; digits_scaled(r, entry->score, 218, y + 2, true); }
				else { label(16, y + 5, (SDL_Color){100, 100, 100, 255}, false, "%d. [ПУСТАЯ ЗАПИСЬ]", i + 1); labels[label_count - 1].center_y = true; digits_scaled(r, 0, 218, y + 2, true); }
			}
			draw_rich_line(r, 14, HUD_HEIGHT + 120, 5.3, WHITE,
				"{g}Ins{/g} — добавить   |   {g}Del{/g} — удалить   |   Цифровой блок: {g}Num Lock{/g} выкл.");
			draw_rich_line(r, 14, HUD_HEIGHT + 128, 5.3, WHITE,
				"{g}Влево/Вправо{/g} — очки (шаг 1000)   |   {g}Вверх/Вниз{/g} — строка");
			draw_rich_line(r, 14, HUD_HEIGHT + 136, 5.3, WHITE,
				"{g}Shift+Влево/Вправо{/g} — шаг ×10   |   {g}Ctrl+Влево/Вправо{/g} — шаг ×100");
			draw_rich_line(r, 14, HUD_HEIGHT + 144, 5.3, WHITE,
				"{g}Буквы{/g} — имя   |   {g}Backspace{/g} — стереть последнюю букву");
			draw_rich_line(r, 14, HUD_HEIGHT + 152, 5.3, WHITE,
				"{g}Tab / PgUp / PgDn{/g} — страницы настроек (1–4)   |   {g}F4{/g} — назад");
		}
	} else {
		if (g->paused || g->won || g->over) {
			bool pause_only = g->paused && !g->over && !g->won;
			int w = pause_only ? 180 : 240, h = pause_only ? 60 : 64;
			int x = (WIDTH - w) / 2, y = HUD_HEIGHT + (160 - h) / 2;
			fill(r, 0, HUD_HEIGHT, WIDTH, 160, (SDL_Color){0, 0, 0, 75});
			fill(r, x, y, w, h, (SDL_Color){0, 85, 85, 255}); fill(r, x + 1, y + 1, w - 2, h - 2, CYAN); fill(r, x + 2, y + 2, w - 4, h - 4, (SDL_Color){0, 170, 170, 255});
			SDL_SetRenderDrawColor(renderer, 0, 85, 85, 255); SDL_Rect inner = {x + 4, y + 4, w - 8, h - 8}; SDL_RenderDrawRect(renderer, &inner);
			sprite(r, r->player, 0, x + 8, y + (h - 32) / 2, 16, 32, 255);
			hud_item(r, 8, x + w - 25, y + (h - 16) / 2, 16, 255);
			const char *head = g->over ? "ИГРА ОКОНЧЕНА" : g->won ? "СОКРОВИЩА ВОЗВРАЩЕНЫ!" : "ПАУЗА";
			intro_text(WIDTH / 2, y + (pause_only ? 9 : 12), pause_only ? 9 : g->won ? 8 : 11, CYAN, head);
			labels[label_count - 1].center_x = true;
			if (pause_only) {
				static const char *const keys[] = {"Esc / P", "Q / Й", "R / К", "F1"};
				static const char *const actions[] = {
					"продолжить", "выход", "рекорды", "справка"
				};
				for (int i = 0; i < 4; i++) {
					int row_y = y + 21 + i * 8;
					intro_text(WIDTH / 2 - 35, row_y, 6, GOLD, keys[i]);
					intro_text(WIDTH / 2 - 10, row_y, 6, CYAN, "—");
					intro_text(WIDTH / 2 - 4, row_y, 6, CYAN, actions[i]);
				}
			} else {
				intro_text(WIDTH / 2, y + 32, 7, CYAN, "Enter — таблица рекордов");
				labels[label_count - 1].center_x = true;
				intro_text(WIDTH / 2, y + 46, 5.5, CYAN, "Q / Й — выход,       F1 — справка");
				labels[label_count - 1].center_x = true;
			}
		}
	}
present:
	SDL_SetRenderTarget(renderer, NULL); SDL_RenderSetClipRect(renderer, NULL); SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
	int ww, wh; SDL_GetRendererOutputSize(renderer, &ww, &wh); int content_height = HEIGHT;
	double scale = fmin(ww / (double)WIDTH, wh / (content_height * 1.2)); SDL_Rect source = {0, 0, WIDTH, content_height};
	SDL_Rect dest = { 0, 0, (int)lround(WIDTH * scale), (int)lround(content_height * scale * 1.2) };
	dest.x = (ww - dest.w) / 2; dest.y = (wh - dest.h) / 2; double sx = dest.w / (double)WIDTH, sy = dest.h / (double)content_height;

	SDL_RenderCopy(renderer, r->canvas, &source, &dest);
	if (!g->scores_open && !g->help_open && !g->location_menu) {
		SDL_Texture *hd_logo = NULL;
		int logo_h = 200;
		if (g->title) {
			hd_logo = (g->intro_page == 0) ? r->intro_logo :
			          (g->intro_page == 1) ? r->story_logo : r->legend_logo;
		} else if (g->won) {
			hd_logo = r->ending_logo;
			logo_h = HEIGHT;
		}
		if (hd_logo) {
			SDL_ScaleMode old_mode;
			SDL_GetTextureScaleMode(hd_logo, &old_mode);
			SDL_SetTextureScaleMode(hd_logo, SDL_ScaleModeLinear);
			SDL_Rect page = {dest.x, dest.y, dest.w, (int)lround(logo_h * sy)};
			SDL_RenderCopy(renderer, hd_logo, NULL, &page);
			SDL_SetTextureScaleMode(hd_logo, old_mode);
		}
	}
	for (int i = 0; i < frame_count; i++) {
		SDL_Rect b = frames[i].box; int left = (int)lround(dest.x + b.x * sx), top = (int)lround(dest.y + b.y * sy);
		int right = (int)lround(dest.x + (b.x + b.w) * sx) - 1; int bottom = (int)lround(dest.y + (b.y + b.h) * sy) - 1;
		for (int edge = 0; edge < 2; edge++) {
			SDL_SetRenderDrawColor(renderer, edge ? 80 : 45, edge ? 82 : 47, edge ? 86 : 51, 255); SDL_RenderDrawLine(renderer, left + edge, top + edge, right - edge, top + edge); SDL_RenderDrawLine(renderer, left + edge, top + edge, left + edge, bottom - edge);
			SDL_SetRenderDrawColor(renderer, edge ? 220 : 245, edge ? 222 : 247, edge ? 226 : 251, 255); SDL_RenderDrawLine(renderer, right - edge, top + edge + 1, right - edge, bottom - edge); SDL_RenderDrawLine(renderer, left + edge + 1, bottom - edge, right - edge, bottom - edge);
		}
	}
	if (g->location_menu && !g->settings_page && !g->help_open && !g->scores_open && !g->title) {
		int rad = (int)lround(1.8 * sx);
		if (rad < 2) rad = 2;
		for (int i = 0; i < 8; i++) {
			int row_y = HUD_HEIGHT + 32 + i * 12;
			float ic_w = (float)(9.0 * sx);
			float ic_cx = (float)(dest.x + (16.0 + 4.5) * sx);
			float ic_cy = (float)(dest.y + (row_y + 4.5) * sy);
			int pad = (int)lround(1.4 * sx);
			int bw = (int)lround(ic_w) + pad * 2;
			int bh = bw;
			int bx = (int)lround(ic_cx - bw / 2.0f);
			int by = (int)lround(ic_cy - bh / 2.0f);
			draw_smooth_badge(renderer, bx, by, bw, bh, rad);
		}
	}
	for (int i = 0; i < hud_icon_count; i++) {
		HudIcon *icon = &hud_icons[i];
		SDL_SetTextureAlphaMod(icon->texture, icon->alpha);
		float draw_w = (float)(icon->box.w * sx);
		float draw_h = (float)(icon->box.h * sx);
		float draw_x = (float)(dest.x + icon->box.x * sx);
		float draw_y = (float)(dest.y + (icon->box.y + icon->box.h / 2.0f) * sy - draw_h / 2.0f);
		if (icon->texture == r->life_icons[0] || icon->texture == r->life_icons[1]) {
			draw_x = roundf(draw_x); draw_y = roundf(draw_y);
			draw_w = roundf(draw_w); draw_h = draw_w;
		}
		SDL_FRect box = {draw_x, draw_y, draw_w, draw_h};
		SDL_RenderCopyF(renderer, icon->texture, &icon->source, &box);
		SDL_SetTextureAlphaMod(icon->texture, 255);
	}
	if (lives_visible) draw_lives_pixels(g, dest, sx, sy);
	for (int i = 0; i < label_count; i++) {
		Label *l = &labels[i]; TTF_Font *f = font(r, (int)lround(l->size * sx)); if (!f) continue;
		TextCache *cache = &r->text_cache[i];
		if (!cache->texture || cache->font != f || strcmp(cache->text, l->text) || cache->color.r != l->color.r || cache->color.g != l->color.g || cache->color.b != l->color.b || cache->color.a != l->color.a) {
			SDL_Surface *surface = TTF_RenderUTF8_Blended(f, l->text, l->color); if (!surface) continue;
			SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface); if (!texture) { SDL_FreeSurface(surface); continue; }
			SDL_DestroyTexture(cache->texture); cache->texture = texture; cache->font = f; cache->color = l->color; SDL_strlcpy(cache->text, l->text, sizeof cache->text);
			cache->w = surface->w; cache->h = surface->h; int top = surface->h, bottom = -1;
			if (surface->format->BytesPerPixel == 4 && surface->format->Amask) {
				for (int y = 0; y < surface->h; y++) for (int x = 0; x < surface->w; x++) {
						Uint32 pixel; memcpy(&pixel, (Uint8 *)surface->pixels + y * surface->pitch + x * 4, 4);
						if (pixel & surface->format->Amask) { if (y < top) top = y; if (y > bottom) bottom = y; }
					}
			}
			cache->middle = bottom >= top ? (top + bottom + 1) / 2. : surface->h / 2.; SDL_FreeSurface(surface);
		}
		double text_scale = l->max_width > 0 && cache->w > l->max_width * sx
			? l->max_width * sx / cache->w : 1.;
		SDL_Rect text = {(int)lround(dest.x + l->x * sx), (int)lround(dest.y + l->y * sy),
			(int)lround(cache->w * text_scale), (int)lround(cache->h * text_scale)};

		if (l->center_x) {
			text.x -= text.w / 2;
		}
		if (l->center_y) {
			text.y = (int)lround(dest.y + l->y * sy - cache->middle * text_scale);
		}
		if (l->right) {
			text.x -= text.w;
		}

		SDL_RenderCopy(renderer, cache->texture, NULL, &text);
	}
}

bool save_screenshot(Resources *r, const char *path) {
	int w, h; SDL_GetRendererOutputSize(r->renderer, &w, &h);
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32); if (!surface) return false;
	bool ok = SDL_RenderReadPixels(r->renderer, NULL, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch) == 0 && SDL_SaveBMP(surface, path) == 0;
	SDL_FreeSurface(surface); return ok;
}

static void draw_ending(Resources *r) {
	SDL_Rect page = {0, 0, WIDTH, HEIGHT}; SDL_RenderCopy(r->renderer, r->ending_logo, NULL, &page);
}
