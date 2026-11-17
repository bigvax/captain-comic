#include "cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static int compare_scores(const void *a, const void *b) {
	const ScoreEntry *entry_a = (const ScoreEntry *)a;
	const ScoreEntry *entry_b = (const ScoreEntry *)b;
	if (entry_a->score > entry_b->score) return -1;
	if (entry_a->score < entry_b->score) return 1;
	return 0;
}

static int next_character(const char *s, unsigned *code) {
	unsigned char c = (unsigned char)s[0];
	if (!c) return 0;
	if (c < 0x80) { *code = c; return 1; }
	int size; unsigned value, minimum;
	if (c >= 0xc2 && c <= 0xdf) { size = 2; value = c & 31u; minimum = 0x80; }
	else if (c >= 0xe0 && c <= 0xef) { size = 3; value = c & 15u; minimum = 0x800; }
	else if (c >= 0xf0 && c <= 0xf4) { size = 4; value = c & 7u; minimum = 0x10000; }
	else return -1;
	for (int i = 1; i < size; i++) {
		c = (unsigned char)s[i];
		if (!c || (c & 0xc0) != 0x80) return -1;
		value = (value << 6) | (c & 63u);
	}
	if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return -1;
	*code = value; return size;
}

static bool valid_name(const char *name) {
	if (!name || strlen(name) >= SCORE_NAME_BYTES) return false;
	int characters = 0;
	while (*name) {
		unsigned code; int size = next_character(name, &code);
		if (size < 1 || code < 32 || (code >= 0x7f && code <= 0x9f)) return false;
		name += size;
		if (++characters > SCORE_NAME_CHARS) return false;
	}
	return true;
}

bool score_qualifies(const ScoreTable *table, int score) {
	return score > 0 && score <= 999999999 &&
	       (table->count < SCORE_COUNT || score > table->entries[SCORE_COUNT - 1].score);
}

bool score_add(ScoreTable *table, int score, const char *name) {
	if (!score_qualifies(table, score) || !valid_name(name)) return false;
	int place = 0;
	while (place < table->count && table->entries[place].score >= score) place++;
	int last = table->count < SCORE_COUNT ? table->count : SCORE_COUNT - 1;
	for (int i = last; i > place; i--) table->entries[i] = table->entries[i - 1];
	table->entries[place].score = score;
	snprintf(table->entries[place].name, SCORE_NAME_BYTES, "%s", name);
	if (table->count < SCORE_COUNT) table->count++;
	return true;
}

bool scores_load(ScoreTable *table, const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char line[4096];
	ScoreTable loaded = {0};
	bool ok = fgets(line, sizeof line, f) && !strcmp(line, "CCSCORES1\n");
	while (ok && fgets(line, sizeof line, f)) {
		if (loaded.count >= SCORE_COUNT || !strchr(line, '\n')) { ok = false; break; }
		line[strcspn(line, "\r\n")] = 0;
		char *end;
		errno = 0;
		long score = strtol(line, &end, 10);
		if (errno || end == line || *end != '\t' || score < 1 || score > 999999999 || !valid_name(end + 1)) {
			ok = false; break;
		}
		ScoreEntry *entry = &loaded.entries[loaded.count++];
		entry->score = (int)score;
		snprintf(entry->name, sizeof entry->name, "%s", end + 1);
	}
	if (ferror(f)) ok = false;
	if (fclose(f)) ok = false;
	if (ok) {
		*table = loaded;
		if (table->count > 1) qsort(table->entries, table->count, sizeof(ScoreEntry), compare_scores);
	}
	return ok;
}

bool scores_save(const ScoreTable *table, const char *path) {
	if (table->count < 0 || table->count > SCORE_COUNT) return false;
	for (int i = 0; i < table->count; i++)
		if (table->entries[i].score < 1 || table->entries[i].score > 999999999 ||
			!valid_name(table->entries[i].name)) return false;
	char temporary[4096];
	if (snprintf(temporary, sizeof temporary, "%s.tmp.XXXXXX", path) >= (int)sizeof temporary) return false;
	int fd = mkstemp(temporary);
	if (fd < 0) return false;
	FILE *f = fdopen(fd, "w");
	if (!f) { close(fd); unlink(temporary); return false; }
	ScoreTable temp = *table;
	if (temp.count > 1) qsort(temp.entries, temp.count, sizeof(ScoreEntry), compare_scores);
	bool ok = fputs("CCSCORES1\n", f) >= 0;
	for (int i = 0; ok && i < temp.count; i++)
		ok = fprintf(f, "%d\t%s\n", temp.entries[i].score, temp.entries[i].name) > 0;
	if (fclose(f)) ok = false;
	if (ok && !replace_save_file(temporary, path)) ok = false;
	if (!ok) unlink(temporary);
	return ok;
}

void game_scores_open(Game *g, bool after_game) {
	g->scores_open = true;
	g->score_after_game = after_game;
	g->score_entry = after_game && !g->score_submitted && score_qualifies(&g->r->scores, g->score);
	g->score_name[0] = '\0';
	g->score_notice[0] = '\0';
}

static void append_name(char *name, const char *text) {
	size_t used = strlen(name);
	int characters = 0;
	for (size_t i = 0; i < used; i++)
		if (((unsigned char)name[i] & 0xc0) != 0x80) characters++;
	while (*text) {
		unsigned code; int size = next_character(text, &code);
		if (size < 1) break;
		if (code >= 32 && !(code >= 0x7f && code <= 0x9f) &&
			characters < SCORE_NAME_CHARS && used + (size_t)size < SCORE_NAME_BYTES) {
			memcpy(name + used, text, (size_t)size);
			used += (size_t)size; name[used] = 0; characters++;
		}
		text += size;
	}
}

static void backspace_name(char *name) {
	size_t length = strlen(name);
	if (length) {
		do { length--; } while (length && ((unsigned char)name[length] & 0xc0) == 0x80);
		name[length] = 0;
	}
}

bool game_text_input(const Game *g) {
	return (g->scores_open && g->score_entry) ||
		(!g->scores_open && !g->help_open && g->location_menu && g->settings_page == 3);
}

static void save_edited_scores(Game *g) {
	if (g->r->score_path[0] && !scores_save(&g->r->scores, g->r->score_path)) {
		snprintf(g->message, sizeof g->message, "Не удалось сохранить таблицу рекордов.");
		g->message_time = 3;
	}
}

void score_edit_text(Game *g, const char *text) {
	if (!g->location_menu || g->settings_page != 3 || g->help_open || g->scores_open) return;
	int row = g->settings_row;
	if (row < 0 || row >= g->r->scores.count) return;
	append_name(g->r->scores.entries[row].name, text);
	save_edited_scores(g);
}

bool score_edit_key(Game *g, SDL_Scancode key, SDL_Keymod modifiers) {
	int step = (modifiers & KMOD_CTRL) ? 100000 : (modifiers & KMOD_SHIFT) ? 10000 : 1000;
	ScoreTable *table = &g->r->scores;
	int row = g->settings_row;
	if (row < 0) row = 0;
	if (row >= SCORE_COUNT) row = SCORE_COUNT - 1;
	g->settings_row = row;
	if (key == SDL_SCANCODE_UP) { if (row > 0) g->settings_row--; return true; }
	if (key == SDL_SCANCODE_DOWN) { if (row < SCORE_COUNT - 1) g->settings_row++; return true; }
	if (key == SDL_SCANCODE_INSERT) {
		if (table->count >= SCORE_COUNT) {
			snprintf(g->message, sizeof g->message, "Таблица заполнена. Сначала удалите запись (Del).");
			g->message_time = 3;
			return true;
		}
		g->settings_row = table->count++;
		table->entries[g->settings_row] = (ScoreEntry){.score = 1000, .name = "НОВЫЙ"};
	} else if (row < table->count) {
		ScoreEntry *entry = &table->entries[row];
		switch (key) {
		case SDL_SCANCODE_LEFT: entry->score = entry->score > step ? entry->score - step : 1; break;
		case SDL_SCANCODE_RIGHT: entry->score = entry->score <= 999999999 - step ? entry->score + step : 999999999; break;
		case SDL_SCANCODE_BACKSPACE: backspace_name(entry->name); break;
		case SDL_SCANCODE_DELETE:
			memmove(entry, entry + 1, (size_t)(table->count - row - 1) * sizeof *entry);
			table->entries[--table->count] = (ScoreEntry){0};
			break;
		default: return false;
		}
	} else return false;
	/* Stable ordering, keeping the cursor on the same edited record. */
	for (int i = 1; i < table->count; i++)
		for (int j = i; j > 0 && table->entries[j].score > table->entries[j - 1].score; j--) {
			ScoreEntry swap = table->entries[j];
			table->entries[j] = table->entries[j - 1];
			table->entries[j - 1] = swap;
			if (g->settings_row == j) g->settings_row--;
			else if (g->settings_row == j - 1) g->settings_row++;
		}
	save_edited_scores(g);
	return true;
}

void game_score_text(Game *g, const char *text) {
	if (!g->scores_open || !g->score_entry) return;
	append_name(g->score_name, text);
}

int game_scores_key(Game *g, SDL_Scancode key) {
	if (g->score_after_game && !g->score_entry && key != SDL_SCANCODE_UNKNOWN) return 1;
	if (key == SDL_SCANCODE_F11) return 2;
	if (g->score_entry) {
		if (key == SDL_SCANCODE_BACKSPACE) {
			backspace_name(g->score_name);
		} else if (key == SDL_SCANCODE_ESCAPE) {
			g->score_entry = false; g->score_submitted = true;
			snprintf(g->score_notice, sizeof g->score_notice, "Запись результата пропущена.");
		} else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) {
			char *name = g->score_name;
			while (*name == ' ') name++;
			size_t length = strlen(name);
			while (length && name[length - 1] == ' ') name[--length] = 0;
			if (score_add(&g->r->scores, g->score, name[0] ? name : "Игрок")) {
				bool saved = g->r->score_path[0] && scores_save(&g->r->scores, g->r->score_path);
				snprintf(g->score_notice, sizeof g->score_notice, "%s", saved ? "Результат сохранён." : "Результат добавлен; сохранить файл не удалось.");
			}
			g->score_entry = false; g->score_submitted = true;
		}
		return 0;
	}
	if (key == SDL_SCANCODE_Q) return 1;
	if (key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_R) g->scores_open = false;
	else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) {
		g->scores_open = false;
	}
	return 0;
}
