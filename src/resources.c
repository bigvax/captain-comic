#include "cc.h"
#include "miniz.h"
#ifdef CC_EMBED_ASSETS
#include "assets_data.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdarg.h>
#include <SDL_image.h>
#include <limits.h>

const char *const AREA_FILES[] = { "lake", "forest", "space", "base", "cave", "shed", "castle", "comp" };
const char *const AREA_TITLES[] = { "Озеро Сири", "Скалистый лес", "Луна Тамби", "Космическая база", "Пещера слепых жаб", "Ангар", "Замок", "Комплекс" };
const char *const AREA_NAMES_EN[] = { "Lake", "Forest", "Moon", "Base", "Cave", "Shed", "Castle", "Comp" };
const char *const SHAPE_FILES[SHAPE_COUNT] = {
	"CUBE.SHP", "FB.SHP", "STAR1.SHP", "STAR2.SHP", "STAR3.SHP", "BALL.SHP", "FROG.SHP",
	"BIRD.SHP", "BIRD2.SHP", "GLOBE.SHP", "SAUCER.SHP", "ROCK.SHP", "BUG.SHP", "BEE.SHP"
};
const int MENU_AREAS[] = { 1, 0, 4, 5, 3, 2, 7, 6 };

static const Uint32 EGA_PALETTE[] = {
	0x000000FF, 0x0000AAFF, 0x00AA00FF, 0x00AAAAFF,
	0xAA0000FF, 0xAA00AAFF, 0xAA5500FF, 0xAAAAAAFF,
	0x555555FF, 0x5555FFFF, 0x55FF55FF, 0x55FFFFFF,
	0xFF5555FF, 0xFF55FFFF, 0xFFFF55FF, 0xFFFFFFFF
};

void fail_message(const char *format, ...) {
	char msg[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(msg, sizeof(msg), format, args);
	va_end(args);
	fprintf(stderr, "ОШИБКА ИГРЫ: %s\n", msg);
#ifdef _WIN32
	MessageBoxA(NULL, msg, "Captain Comic — Ошибка", MB_OK | MB_ICONERROR);
#endif
}

static mz_zip_archive g_mem_zip;
static bool g_mem_zip_ok = false;
static unsigned char *g_font_mem = NULL;
static size_t g_font_mem_sz = 0;

static int case_insensitive_compare(const char *s1, const char *s2) {
	while (*s1 && *s2) {
		if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) return 0;
		s1++; s2++;
	}
	return *s1 == *s2;
}

static bool resolve_case_insensitive_path(const char *base_dir, const char *target_name, char *out_path, size_t max_len) {
	DIR *dir = opendir(base_dir);
	if (!dir) return false;
	struct dirent *entry;
	bool found = false;
	while ((entry = readdir(dir)) != NULL) {
		if (case_insensitive_compare(entry->d_name, target_name)) {
			if (strlen(base_dir) + strlen(entry->d_name) + 2 <= max_len) {
				snprintf(out_path, max_len, "%.3800s/%.255s", base_dir, entry->d_name);
				found = true;
			}
			break;
		}
	}
	closedir(dir);
	return found;
}

static bool vfs_resolve_disk_path(const char *root, const char *relative_path, char *out_path, size_t max_len) {
	char temp_root[4160], rel_copy[4160], next_root[4160];
	snprintf(temp_root, sizeof temp_root, "%.4100s", root);
	snprintf(rel_copy, sizeof rel_copy, "%.4100s", relative_path);
	for (char *token = strtok(rel_copy, "/\\"); token != NULL; token = strtok(NULL, "/\\")) {
		if (!resolve_case_insensitive_path(temp_root, token, next_root, sizeof next_root)) return false;
		snprintf(temp_root, sizeof temp_root, "%.4100s", next_root);
	}
	snprintf(out_path, max_len, "%.4100s", temp_root);
	return true;
}

static unsigned char *vfs_read_asset(const Resources *r, const char *rel_path, size_t *out_size) {
	if (out_size) *out_size = 0;
	if (g_mem_zip_ok) {
		char name[512];
		snprintf(name, sizeof name, "assets/%s", rel_path);
		int index = mz_zip_reader_locate_file(&g_mem_zip, name, NULL, 0);
		if (index < 0) index = mz_zip_reader_locate_file(&g_mem_zip, rel_path, NULL, 0);
		if (index < 0) return NULL;
		mz_zip_archive_file_stat info;
		if (!mz_zip_reader_file_stat(&g_mem_zip, (mz_uint)index, &info) ||
			info.m_uncomp_size == 0 || info.m_uncomp_size > 16 * 1024 * 1024) return NULL;
		return mz_zip_reader_extract_to_heap(&g_mem_zip, (mz_uint)index, out_size, 0);
	}
	char real_path[4160];
	if (!vfs_resolve_disk_path(r->asset_dir, rel_path, real_path, sizeof real_path)) return NULL;
	FILE *f = fopen(real_path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
	long fsize = ftell(f);
	if (fsize <= 0 || fsize > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
	unsigned char *buf = malloc((size_t)fsize);
	if (!buf || fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) { free(buf); fclose(f); return NULL; }
	fclose(f);
	if (out_size) *out_size = (size_t)fsize;
	return buf;
}

static void decode_ega_planar(const unsigned char *src, Uint32 *dst, int pitch_pixels, int width, int height, bool has_mask) {
	int row_bytes = width / 8, plane_size = row_bytes * height;
	const unsigned char *p0 = src, *p1 = src + plane_size, *p2 = src + plane_size * 2, *p3 = src + plane_size * 3;
	const unsigned char *pm = has_mask ? (src + plane_size * 4) : NULL;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int byte_idx = y * row_bytes + (x / 8), bit = 7 - (x % 8);
			if (pm && (((pm[byte_idx] >> bit) & 1) != 0)) dst[y * pitch_pixels + x] = 0x00000000;
			else {
				int color = ((p0[byte_idx] >> bit) & 1) | (((p1[byte_idx] >> bit) & 1) << 1) |
				            (((p2[byte_idx] >> bit) & 1) << 2) | (((p3[byte_idx] >> bit) & 1) << 3);
				dst[y * pitch_pixels + x] = EGA_PALETTE[color];
			}
		}
	}
}

static bool load_bank(Resources *r, SpriteBank *bank, const char *filename, int height) {
	size_t fsize = 0;
	unsigned char *raw = vfs_read_asset(r, filename, &fsize);
	if (!raw) { fail_message("Ресурс не найден (VFS): %s", filename); return false; }
	int frame_bytes_masked = 2 * height * 5, frame_bytes_solid = 2 * height * 4;
	unsigned char *data = raw; size_t data_size = fsize;
	if (fsize > 4 && (fsize % (size_t)frame_bytes_masked != 0) && (fsize % (size_t)frame_bytes_solid != 0)) { data = raw + 4; data_size = fsize - 4; }
	bool has_mask = (data_size % (size_t)frame_bytes_masked == 0);
	int frame_size = has_mask ? frame_bytes_masked : frame_bytes_solid, count = (int)(data_size / (size_t)frame_size);
	if (count < 1 || count > 256 || data_size % (size_t)frame_size) { free(raw); return false; }
	int cols = 16, rows = (count + cols - 1) / cols;
	SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, cols * 16, rows * height, 32, SDL_PIXELFORMAT_RGBA8888);
	if (surf) {
		SDL_LockSurface(surf); memset(surf->pixels, 0, (size_t)(surf->h * surf->pitch));
		for (int i = 0; i < count; i++) {
			int dx = (i % cols) * 16, dy = (i / cols) * height;
			Uint32 *dst_ptr = (Uint32 *)((unsigned char *)surf->pixels + dy * surf->pitch) + dx;
			decode_ega_planar(data + i * frame_size, dst_ptr, surf->pitch / 4, 16, height, has_mask);
		}
		SDL_UnlockSurface(surf);
		bank->texture = SDL_CreateTextureFromSurface(r->renderer, surf);
		SDL_SetTextureBlendMode(bank->texture, SDL_BLENDMODE_BLEND);
		bank->height = height; bank->count = count;
		free(bank->opaque); bank->opaque = calloc((size_t)(count * height), sizeof(uint16_t));
		if (bank->opaque) {
			for (int i = 0; i < count; i++) {
				int dx = (i % cols) * 16, dy = (i / cols) * height;
				for (int y = 0; y < height; y++) {
					uint16_t row_mask = 0;
					Uint32 *row_ptr = (Uint32 *)((unsigned char *)surf->pixels + (dy + y) * surf->pitch) + dx;
					for (int x = 0; x < 16; x++) if ((row_ptr[x] & 0xFF) != 0) row_mask |= (uint16_t)(1u << x);
					bank->opaque[i * height + y] = row_mask;
				}
			}
		}
		SDL_FreeSurface(surf);
	}
	free(raw);
	return bank->texture && bank->opaque;
}

static bool load_area(Resources *r, int index, const char *filename) {
	char name_buf[256];
	for (int s = 0; s < 3; s++) {
		size_t pt_sz = 0; snprintf(name_buf, sizeof name_buf, "dos/%s%d.PT", filename, s);
		unsigned char *pt = vfs_read_asset(r, name_buf, &pt_sz);
		if (!pt) { snprintf(name_buf, sizeof name_buf, "dos/%s%d_.PT", filename, s); pt = vfs_read_asset(r, name_buf, &pt_sz); }
		if (!pt || pt_sz != 1284) { free(pt); return false; }
		memcpy(r->areas[index].cells[s], pt + 4, 1280); free(pt);
	}
	size_t fsize = 0; snprintf(name_buf, sizeof name_buf, "dos/%s.TT2", filename);
	unsigned char *buf = vfs_read_asset(r, name_buf, &fsize);
	if (!buf) { snprintf(name_buf, sizeof name_buf, "dos/%s_.TT2", filename); buf = vfs_read_asset(r, name_buf, &fsize); }
	if (!buf || fsize <= 4 || (fsize - 4) % 128 || fsize > 4 + 256 * 128) { free(buf); return false; }
	if (buf) {
		int tile_bytes = 0x80, count = (int)((fsize - 4) / (size_t)tile_bytes);
		if (count > 256) count = 256;
		r->areas[index].last_passable = *buf; r->areas[index].tile_count = count;
		SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 256, 256, 32, SDL_PIXELFORMAT_RGBA8888);
		if (surf) {
			SDL_LockSurface(surf); memset(surf->pixels, 0, (size_t)(surf->h * surf->pitch));
			for (int i = 0; i < count; i++) {
				int dx = (i % 16) * 16, dy = (i / 16) * 16;
				Uint32 *dst_ptr = (Uint32 *)((unsigned char *)surf->pixels + dy * surf->pitch) + dx;
				decode_ega_planar(buf + 4 + i * tile_bytes, dst_ptr, surf->pitch / 4, 16, 16, false);
			}
			SDL_UnlockSurface(surf); r->areas[index].tiles = SDL_CreateTextureFromSurface(r->renderer, surf); SDL_FreeSurface(surf);
		}
	}
	free(buf);
	if (!r->areas[index].tiles) return false;
	for (int scene = 0; scene < 3; scene++)
		for (int i = 0; i < 1280; i++)
			if (r->areas[index].cells[scene][i] >= r->areas[index].tile_count) return false;
	return true;
}

static int item_kind_from_str(const char *s) {
	if (!strcmp(s, "corkscrew")) return CORK;
	if (!strcmp(s, "key")) return KEY;
	if (!strcmp(s, "boots")) return BOOTS;
	if (!strcmp(s, "lantern")) return LANTERN;
	if (!strcmp(s, "wand")) return WAND;
	if (!strcmp(s, "gems")) return GEMS;
	if (!strcmp(s, "crown")) return CROWN;
	if (!strcmp(s, "gold")) return GOLD_ITEM;
	if (!strcmp(s, "shield")) return SHIELD;
	return 8;
}
static int area_index_from_str(const char *s) { for (int i = 0; i < AREA_COUNT; i++) if (!strcmp(AREA_FILES[i], s)) return i; return -1; }
static int shape_index_from_str(const char *s) {
	if (!s || !*s) return 0;
	for (int i = 0; i < SHAPE_COUNT; i++) { size_t n = strlen(SHAPE_FILES[i]) - 4; if (!strncmp(s, SHAPE_FILES[i], n) && !s[n]) return i; }
	return 0;
}
static const char *skip_ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; return p; }
static const char *find_key(const char *p, const char *end, const char *key) {
	char pat[128]; snprintf(pat, sizeof pat, "\"%s\"", key); size_t plen = strlen(pat);
	for (const char *cur = p; cur && cur < end; ) {
		const char *f = strstr(cur, pat); if (!f || f >= end) break;
		const char *after = skip_ws(f + plen); if (*after == ':') return skip_ws(after + 1);
		cur = f + plen;
	}
	return NULL;
}
static int parse_int_val(const char *p, int def_val) { return (!p || !strncmp(p, "null", 4)) ? def_val : (int)strtol(p, NULL, 10); }
static void parse_str_val(const char *p, char *out, size_t max_len) {
	if (max_len > 0) *out = '\0';
	if (!p || *p != '"') return;
	p++; size_t i = 0;
	while (*p && *p != '"' && i + 1 < max_len) out[i++] = *p++;
	out[i] = '\0';
}
static const char *find_matching_bracket(const char *p, char open_ch, char close_ch) {
	int depth = 0; bool in_str = false;
	for (const char *s = p; *s; s++) {
		if (*s == '"' && (s == p || *(s - 1) != '\\')) in_str = !in_str;
		if (in_str) continue;
		if (*s == open_ch) depth++; else if (*s == close_ch && --depth == 0) return s;
	}
	return NULL;
}

static bool load_world_json(Resources *r) {
	size_t fsize = 0; unsigned char *raw = vfs_read_asset(r, "dos/world.json", &fsize);
	if (!raw || !fsize) { free(raw); return false; }
	char *json = realloc(raw, fsize + 1); if (!json) { free(raw); return false; }
	json[fsize] = '\0';
	char kbuf[64], abuf[64], sbuf[64];
	const char *json_end = json + fsize, *areas_p = find_key(json, json_end, "areas"); if (!areas_p) areas_p = json;
	for (int a = 0; a < AREA_COUNT; a++) {
		const char *arr_p = find_key(areas_p, json_end, AREA_FILES[a]); if (!arr_p || *arr_p != '[') continue;
		const char *arr_end = find_matching_bracket(arr_p, '[', ']'); if (!arr_end) arr_end = json_end;
		const char *cur = arr_p + 1;
		for (int s = 0; s < 3 && cur < arr_end; s++) {
			cur = skip_ws(cur); if (*cur != '{') break;
			const char *sc_end = find_matching_bracket(cur, '{', '}'); if (!sc_end || sc_end > arr_end) break;
			Scene *sc = &r->areas[a].scenes[s]; memset(sc, 0, sizeof *sc); sc->left = sc->right = -1;
			const char *it_p = find_key(cur, sc_end, "item");
			if (it_p && *it_p == '{') {
				const char *it_end = find_matching_bracket(it_p, '{', '}');
				if (it_end && it_end <= sc_end) {
					parse_str_val(find_key(it_p, it_end, "kind"), kbuf, sizeof kbuf);
					sc->item.kind = item_kind_from_str(kbuf); sc->item.sprite = parse_int_val(find_key(it_p, it_end, "sprite"), 8);
					sc->item.x = parse_int_val(find_key(it_p, it_end, "x"), 0); sc->item.y = parse_int_val(find_key(it_p, it_end, "y"), 0);
					if (a == FOREST && s == 0 && sc->item.x == 96) sc->item.x = 120;
					sc->item.present = true;
				}
			}
			sc->left = parse_int_val(find_key(cur, sc_end, "left"), -1); sc->right = parse_int_val(find_key(cur, sc_end, "right"), -1);
			const char *doors_p = find_key(cur, sc_end, "doors");
			if (doors_p && *doors_p == '[') {
				const char *doors_end = find_matching_bracket(doors_p, '[', ']');
				for (const char *dp = doors_p + 1; doors_end && dp < doors_end && sc->door_count < 3; ) {
					dp = skip_ws(dp); if (*dp != '{') { dp++; continue; }
					const char *d_end = find_matching_bracket(dp, '{', '}'); if (!d_end || d_end > doors_end) break;
					parse_str_val(find_key(dp, d_end, "area"), abuf, sizeof abuf);
					Door *d = &sc->doors[sc->door_count++];
					d->x = parse_int_val(find_key(dp, d_end, "x"), 0); d->y = parse_int_val(find_key(dp, d_end, "y"), 0);
					d->area = area_index_from_str(abuf); d->scene = parse_int_val(find_key(dp, d_end, "scene"), 0);
					dp = d_end + 1;
				}
			}
			const char *en_p = find_key(cur, sc_end, "enemies");
			if (en_p && *en_p == '[') {
				const char *en_end = find_matching_bracket(en_p, '[', ']');
				for (const char *ep = en_p + 1; en_end && ep < en_end && sc->enemy_count < 4; ) {
					ep = skip_ws(ep); if (*ep != '{') { ep++; continue; }
					const char *e_end = find_matching_bracket(ep, '{', '}'); if (!e_end || e_end > en_end) break;
					memset(sbuf, 0, sizeof sbuf); parse_str_val(find_key(ep, e_end, "shape"), sbuf, sizeof sbuf);
					const char *del_p = find_key(ep, e_end, "delayed");
					EnemySpec *es = &sc->enemies[sc->enemy_count++];
					es->shape = shape_index_from_str(sbuf); es->behavior = parse_int_val(find_key(ep, e_end, "behavior"), 1);
					es->delayed = (del_p && !strncmp(del_p, "true", 4)); ep = e_end + 1;
				}
			}
			cur = sc_end + 1; while (cur < arr_end && (*cur == ',' || *cur == ' ' || *cur == '\n' || *cur == '\r' || *cur == '\t')) cur++;
		}
	}
	free(json);
	/* Every shipped area has three scenes and one item per scene. */
	for (int a = 0; a < AREA_COUNT; a++)
		for (int i = 0; i < 3; i++) {
			Scene *scene = &r->areas[a].scenes[i];
			if (!scene->item.present || scene->item.sprite < 0 || scene->item.sprite >= r->items.count ||
				scene->left < -1 || scene->left > 2 || scene->right < -1 || scene->right > 2 ||
				scene->item.x < 0 || scene->item.x > 2032 || scene->item.y < 0 || scene->item.y > 144) return false;
			for (int d = 0; d < scene->door_count; d++)
				if (scene->doors[d].scene < 0 || scene->doors[d].scene > 2 ||
					scene->doors[d].area < 0 || scene->doors[d].area >= AREA_COUNT ||
					scene->doors[d].x < 0 || scene->doors[d].x > 2016 ||
					scene->doors[d].y < 0 || scene->doors[d].y > 128) return false;
		}
	return true;
}

static SDL_Surface *vfs_load_surface(const Resources *r, const char *rel_path) {
	size_t sz = 0; unsigned char *buf = vfs_read_asset(r, rel_path, &sz); if (!buf) return NULL;
	SDL_RWops *rw = SDL_RWFromConstMem(buf, (int)sz); SDL_Surface *surf = rw ? IMG_Load_RW(rw, 1) : NULL; free(buf); return surf;
}
static SDL_Texture *life_icon_texture(SDL_Renderer *renderer, SDL_Surface *atlas, int index) {
	int cell = atlas->h, size = cell + 2, count = size * size;
	SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
	unsigned char *known = calloc((size_t)count, 1);
	int *queue = malloc((size_t)count * sizeof *queue);
	if (!icon || !known || !queue) { SDL_FreeSurface(icon); free(known); free(queue); return NULL; }
	SDL_FillRect(icon, NULL, 0);
	for (int y = 0; y < cell; y++)
		memcpy((Uint8 *)icon->pixels + (y + 1) * icon->pitch + 4,
			(Uint8 *)atlas->pixels + y * atlas->pitch + index * cell * 4, (size_t)cell * 4);
	/* Extend edge RGB into transparent pixels while keeping alpha zero.
	   Linear filtering then blends the outline without a black fringe. */
	int head = 0, tail = 0;
	for (int y = 0; y < size; y++) for (int x = 0; x < size; x++)
		if (*((Uint8 *)icon->pixels + y * icon->pitch + x * 4 + 3)) {
			int pixel = y * size + x; known[pixel] = 1; queue[tail++] = pixel;
		}
	static const int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
	while (head < tail) {
		int pixel = queue[head++], x = pixel % size, y = pixel / size;
		for (int direction = 0; direction < 4; direction++) {
			int nx = x + dx[direction], ny = y + dy[direction];
			if (nx < 0 || nx >= size || ny < 0 || ny >= size || known[ny * size + nx]) continue;
			memcpy((Uint8 *)icon->pixels + ny * icon->pitch + nx * 4,
				(Uint8 *)icon->pixels + y * icon->pitch + x * 4, 3);
			known[ny * size + nx] = 1; queue[tail++] = ny * size + nx;
		}
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, icon);
	if (texture) {
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
	}
	SDL_FreeSurface(icon); free(known); free(queue);
	return texture;
}
static bool hud_icons_init(Resources *r) {
	SDL_Surface *source = vfs_load_surface(r, "icons/hud-icons.png");
	if (!source) return false;
	SDL_Surface *s = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(source);
	if (!s) return false;
	if (s->h < 1 || s->h > 256 || s->w < 12 * s->h) { SDL_FreeSurface(s); return false; }
	r->hud_icons = SDL_CreateTextureFromSurface(r->renderer, s);
	if (r->hud_icons) {
		SDL_SetTextureBlendMode(r->hud_icons, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(r->hud_icons, SDL_ScaleModeNearest);
		r->life_icons[0] = life_icon_texture(r->renderer, s, 2);
		r->life_icons[1] = life_icon_texture(r->renderer, s, 3);
	}
	SDL_FreeSurface(s);
	return r->hud_icons && r->life_icons[0] && r->life_icons[1];
}
static SDL_Texture *load_logo_texture(Resources *r, const char *base_name) {
	static const char *const exts[] = { ".png", ".jpg", ".jpeg", ".bmp" }; char rel_path[256];
	for (int i = 0; i < 4; i++) {
		snprintf(rel_path, sizeof rel_path, "%s%s", base_name, exts[i]); SDL_Surface *s = vfs_load_surface(r, rel_path);
		if (s) { SDL_Texture *tex = SDL_CreateTextureFromSurface(r->renderer, s); if (tex) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear); SDL_FreeSurface(s); return tex; }
	}
	return NULL;
}
TTF_Font *resources_open_font(int size) {
	if (g_font_mem && g_font_mem_sz > 0) { SDL_RWops *rw = SDL_RWFromConstMem(g_font_mem, (int)g_font_mem_sz); if (rw) return TTF_OpenFontRW(rw, 1, size); }
	return NULL;
}
static bool open_assets(Resources *r, const char *path) {
	struct stat st;
	if (stat(path, &st)) return false;
	if (S_ISDIR(st.st_mode)) {
		if (strlen(path) >= sizeof r->asset_dir) return false;
		snprintf(r->asset_dir, sizeof r->asset_dir, "%s", path);
		return true;
	}
	g_mem_zip_ok = mz_zip_reader_init_file(&g_mem_zip, path, 0) != 0;
	return g_mem_zip_ok;
}

bool resources_load(Resources *r, const char *directory) {
	if (!r) return false;
	resources_destroy(r);
	memset(&g_mem_zip, 0, sizeof g_mem_zip);
	bool found = false;
	if (directory) {
		found = open_assets(r, directory);
	} else {
#ifdef CC_EMBED_ASSETS
		g_mem_zip_ok = mz_zip_reader_init_mem(&g_mem_zip, assets_pak, assets_pak_len, 0) != 0;
		found = g_mem_zip_ok;
#endif
		char *base = SDL_GetBasePath();
		char path[4096];
		if (!found) {
			snprintf(path, sizeof path, "%sdata/assets.pak", base ? base : "./");
			found = open_assets(r, path);
		}
		if (!found) {
			snprintf(path, sizeof path, "%sassets", base ? base : "./");
			found = open_assets(r, path);
		}
		SDL_free(base);
	}
	if (!found) { fail_message("Не удалось открыть ресурсы: %s", directory ? directory : "data/assets.pak / assets"); return false; }
	SDL_Surface *icon_surf = vfs_load_surface(r, "icons/icon.png"); if (!icon_surf) icon_surf = vfs_load_surface(r, "icons/icon.bmp");
	if (icon_surf) { SDL_SetWindowIcon(r->window, icon_surf); SDL_FreeSurface(icon_surf); }
	r->intro_logo = load_logo_texture(r, "intro/title"); r->story_logo = load_logo_texture(r, "intro/story");
	r->legend_logo = load_logo_texture(r, "intro/legend"); r->ending_logo = load_logo_texture(r, "intro/ending");
	load_bank(r, &r->player, "dos/PLAYER.SPR", 32); load_bank(r, &r->items, "dos/ITEMS.SPR", 16);
	load_bank(r, &r->projectile, "dos/FIREBALL.SPR", 8); load_bank(r, &r->death, "dos/DEATH.SPR", 32);
	load_bank(r, &r->teleport, "dos/TELEPORT.SPR", 32); load_bank(r, &r->arrival, "dos/ARRIVAL.SPR", 32);
	load_bank(r, &r->enemy_hit, "dos/ENEMY_HIT.SPR", 16);
	for (int i = 0; i < SHAPE_COUNT; i++) { char p[256]; snprintf(p, sizeof p, "dos/%s", SHAPE_FILES[i]); load_bank(r, &r->shapes[i], p, 16); }
	for (int i = 0; i < AREA_COUNT; i++)
		if (!load_area(r, i, AREA_FILES[i])) { fail_message("Повреждена карта: %s", AREA_FILES[i]); return false; }
	if (!load_world_json(r) || !hud_icons_init(r)) { fail_message("Не удалось загрузить мир или HUD"); return false; }
	bindings_defaults(&r->bindings);
	if (*r->config_path != '\0') bindings_load(&r->bindings, r->config_path);
	g_font_mem = vfs_read_asset(r, "fonts/Magistral-Cond-Medium.ttf", &g_font_mem_sz);
	if (g_font_mem && g_font_mem_sz > 0) r->fonts[12] = resources_open_font(12);
	if (!r->intro_logo || !r->story_logo || !r->legend_logo || !r->ending_logo || !r->fonts[12]) return false;
	SpriteBank *banks[] = {&r->player, &r->items, &r->projectile, &r->arrival, &r->death, &r->teleport, &r->enemy_hit};
	for (size_t i = 0; i < sizeof banks / sizeof banks[0]; i++)
		if (!banks[i]->texture || !banks[i]->opaque || !banks[i]->count) return false;
	for (int i = 0; i < SHAPE_COUNT; i++)
		if (!r->shapes[i].texture || !r->shapes[i].opaque || !r->shapes[i].count) return false;
	return true;
}
void resources_destroy(Resources *r) {
	if (!r) return;
	SDL_Texture **texs[] = {&r->intro_logo, &r->story_logo, &r->legend_logo, &r->ending_logo, &r->player.texture,
		&r->items.texture, &r->projectile.texture, &r->death.texture, &r->teleport.texture, &r->arrival.texture, &r->enemy_hit.texture, &r->hud_icons, &r->odometer, &r->life_icons[0], &r->life_icons[1]};
	for (size_t i = 0; i < sizeof texs / sizeof texs[0]; i++) if (*texs[i]) { SDL_DestroyTexture(*texs[i]); *texs[i] = NULL; }
	uint16_t **opqs[] = {&r->player.opaque, &r->items.opaque, &r->projectile.opaque, &r->death.opaque, &r->teleport.opaque, &r->arrival.opaque, &r->enemy_hit.opaque};
	for (size_t i = 0; i < sizeof opqs / sizeof opqs[0]; i++) { free(*opqs[i]); *opqs[i] = NULL; }
	for (int i = 0; i < SHAPE_COUNT; i++) { if (r->shapes[i].texture) { SDL_DestroyTexture(r->shapes[i].texture); r->shapes[i].texture = NULL; } free(r->shapes[i].opaque); r->shapes[i].opaque = NULL; }
	for (int i = 0; i < 8; i++) if (r->areas[i].tiles) { SDL_DestroyTexture(r->areas[i].tiles); r->areas[i].tiles = NULL; }
	for (int i = 0; i < 513; i++) if (r->fonts[i]) { TTF_CloseFont(r->fonts[i]); r->fonts[i] = NULL; }
	for (int i = 0; i < 64; i++) if (r->text_cache[i].texture) { SDL_DestroyTexture(r->text_cache[i].texture); r->text_cache[i].texture = NULL; }
	free(g_font_mem); g_font_mem = NULL; g_font_mem_sz = 0;
	if (g_mem_zip_ok) { mz_zip_reader_end(&g_mem_zip); g_mem_zip_ok = false; }
	memset(r->areas, 0, sizeof r->areas);
	r->asset_dir[0] = 0;
}

void resources_shutdown(Resources *r) {
	if (!r) return;
	sound_close(r);
	resources_destroy(r);
	SDL_DestroyTexture(r->canvas);
	SDL_DestroyRenderer(r->renderer);
	SDL_DestroyWindow(r->window);
	r->canvas = NULL;
	r->renderer = NULL;
	r->window = NULL;
}
