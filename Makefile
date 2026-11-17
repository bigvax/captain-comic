.DEFAULT_GOAL := linux
.DELETE_ON_ERROR:
# Инструменты. Значения можно переопределять в командной строке make.
CC_LINUX ?= gcc
CC_WIN ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres
DLLTOOL ?= x86_64-w64-mingw32-dlltool
OBJDUMP_WIN ?= x86_64-w64-mingw32-objdump
PYTHON ?= python3
EMCC ?= emcc
PKG_CONFIG ?= pkg-config
WIN_SYSROOT ?= deps/win32/sysroot
WIN_FMT ?= zip
# Раздельные каталоги объектов исключают смешивание вариантов сборки.
SOURCES := main resources engine render input scores save sound tests miniz
VARIANTS := linux linux-standalone win win-standalone debug
OBJECTS_LINUX := $(addprefix build/linux/,$(addsuffix .o,$(SOURCES)))
OBJECTS_STANDALONE := $(addprefix build/linux-standalone/,$(addsuffix .o,$(SOURCES)))
OBJECTS_WIN := $(addprefix build/win/,$(addsuffix .o,$(SOURCES))) build/win/icon.o
OBJECTS_WIN_STANDALONE := $(addprefix build/win-standalone/,$(addsuffix .o,$(SOURCES))) build/win-standalone/icon.o
OBJECTS_DEBUG := $(addprefix build/debug/,$(addsuffix .o,$(SOURCES)))
# Без fast-math: физика и её проверки зависят от обычной семантики floating point.
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2
MINIZ_FLAGS := -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_APIS
COMMON_FLAGS := -std=c11 $(MINIZ_FLAGS) -ffunction-sections -fdata-sections
RELEASE_OPT ?= -O2
STANDALONE_OPT ?= -Os
DEBUG_OPT ?= -Og
RELEASE_FLAGS := -DNDEBUG
DEBUG_FLAGS := -g3 -fno-omit-frame-pointer
RELEASE_LINK := -Wl,--gc-sections -s
DEBUG_LINK := -Wl,--gc-sections
# Lazy expansion: make clean и Windows-сборки не требуют Linux pkg-config.
LINUX_PKG_CFLAGS = $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf SDL2_image)
LINUX_PKG_LIBS = $(shell $(PKG_CONFIG) --libs SDL2_ttf SDL2_image sdl2) -lm
LINUX_FLAGS = -D_POSIX_C_SOURCE=200809L $(LINUX_PKG_CFLAGS)
# Системные зависимости SDL2 определяются её установленной версией.
SDL2_SYSTEM_LIBS = $(filter-out -lSDL2,$(shell $(PKG_CONFIG) --static --libs sdl2))
LINUX_STATIC_LIBS = -Wl,--start-group \
	deps/linux/build_img/libSDL2_image.a deps/linux/build_ttf/libSDL2_ttf.a \
	-Wl,-Bstatic -lSDL2 -lfreetype -lpng16 -ljpeg -lbz2 -lbrotlidec -lbrotlicommon -lz \
	-Wl,--end-group -Wl,-Bdynamic -lharfbuzz $(SDL2_SYSTEM_LIBS)
WIN_FLAGS = -I$(WIN_SYSROOT)/include -I$(WIN_SYSROOT)/include/SDL2 -Dmain=SDL_main
WIN_LIBRARY_PATH = -L$(WIN_SYSROOT)/lib
WIN_DELAY_LIBS := $(addprefix build/win/delay_,SDL2.a SDL2_ttf.a SDL2_image.a)
WIN_RUNTIME := $(addprefix data/,$(notdir $(wildcard $(WIN_SYSROOT)/bin/*.dll)))
WIN_SYSTEM_LIBS := -mwindows -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm \
	-limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid -lrpcrt4 -lusp10
WIN_STATIC_LIBS = -Wl,-Bstatic -lmingw32 -lSDL2main -lSDL2_image -lSDL2_ttf -lSDL2 \
	-Wl,-Bdynamic $(WIN_SYSTEM_LIBS)
.PHONY: all linux linux-standalone static win win-standalone debug dist dist-7z web web-serve web-docs check-web \
	check check-input check-resources check-ending clean FORCE
all: linux linux-standalone win win-standalone
WEB_FLAGS := -O2 -D_POSIX_C_SOURCE=200809L
WEB_FLAGS += -sUSE_SDL=2 -sUSE_SDL_TTF=2 -sUSE_SDL_IMAGE=2
WEB_FLAGS += -sSDL2_IMAGE_FORMATS='["png","jpg"]' -sALLOW_MEMORY_GROWTH=1
WEB_FLAGS += -sMODULARIZE=1 -sEXPORT_NAME=createCaptainComic -sINVOKE_RUN=0
WEB_FLAGS += -sEXPORTED_RUNTIME_METHODS='["callMain","FS"]' -sENVIRONMENT=web
WEB_FLAGS += -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0
WEB_OUTPUT := build/web
check-web:
	@command -v "$(EMCC)" >/dev/null 2>&1 || { \
		printf '%s\n' 'Не найден Emscripten ($(EMCC)).' \
		'Установите Emsdk по инструкции README и подключите его в текущем терминале:' \
		'  source ~/emsdk/emsdk_env.sh' \
		'Затем повторите: make web'; exit 1; \
	}
# Как в нативных сборках: предупреждения стороннего miniz отключены отдельно.
build/web-support/miniz.o: src/miniz.c src/miniz.h Makefile | check-web
	@mkdir -p $(@D)
	$(EMCC) $(COMMON_FLAGS) -O2 $(CPPFLAGS) $(CFLAGS) -w -c $< -o $@
web-docs:
	$(PYTHON) tools/pack_web_docs.py $(WEB_OUTPUT)
web: check-web data/assets.pak build/web-support/miniz.o web-docs
	@mkdir -p $(WEB_OUTPUT)
	$(EMCC) $(COMMON_FLAGS) $(WARNINGS) $(WEB_FLAGS) $(CPPFLAGS) $(CFLAGS) \
		$(addprefix src/,$(addsuffix .c,$(filter-out miniz,$(SOURCES)))) build/web-support/miniz.o \
		--preload-file data/assets.pak@/data/assets.pak $(LDFLAGS) -lm $(LDLIBS) \
		-o $(WEB_OUTPUT)/captain-comic.js
	cp web/index.html web/app.js web/style.css $(WEB_OUTPUT)/
	cp assets/intro/title.png $(WEB_OUTPUT)/cover.png
	cp assets/icons/icon.png $(WEB_OUTPUT)/icon.png
web-serve: web
	$(PYTHON) -m http.server 8080 --bind 127.0.0.1 --directory $(WEB_OUTPUT)
linux: captain-comic
linux-standalone: captain-comic-standalone
static: captain-comic-static
win: captain-comic.exe $(WIN_RUNTIME)
win-standalone: captain-comic-standalone.exe
debug: captain-comic-debug
# pack_assets.py проверяет содержимое, включая удалённые файлы.
# Неизменившийся пакет сохраняет прежнее время модификации.
FORCE:
data:
	mkdir -p $@
data/assets.pak: FORCE tools/pack_assets.py | data
	$(PYTHON) tools/pack_assets.py pack
src/assets_data.h: data/assets.pak tools/pack_assets.py
	$(PYTHON) tools/pack_assets.py embed
build/linux-standalone/resources.o build/win-standalone/resources.o: src/assets_data.h
# Собственный код: release и debug не используют общие объектные файлы.
build/linux/%.o: src/%.c src/cc.h Makefile
	@mkdir -p $(@D)
	$(CC_LINUX) $(COMMON_FLAGS) $(WARNINGS) $(LINUX_FLAGS) $(RELEASE_OPT) $(RELEASE_FLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
build/linux-standalone/%.o: src/%.c src/cc.h Makefile
	@mkdir -p $(@D)
	$(CC_LINUX) $(COMMON_FLAGS) $(WARNINGS) $(LINUX_FLAGS) $(STANDALONE_OPT) $(RELEASE_FLAGS) -DCC_EMBED_ASSETS $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
build/win/%.o: src/%.c src/cc.h Makefile
	@mkdir -p $(@D)
	$(CC_WIN) $(COMMON_FLAGS) $(WARNINGS) $(WIN_FLAGS) $(RELEASE_OPT) $(RELEASE_FLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
build/win-standalone/%.o: src/%.c src/cc.h Makefile
	@mkdir -p $(@D)
	$(CC_WIN) $(COMMON_FLAGS) $(WARNINGS) $(WIN_FLAGS) $(STANDALONE_OPT) $(RELEASE_FLAGS) -DCC_EMBED_ASSETS $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
build/debug/%.o: src/%.c src/cc.h Makefile
	@mkdir -p $(@D)
	$(CC_LINUX) $(COMMON_FLAGS) $(WARNINGS) $(LINUX_FLAGS) $(DEBUG_OPT) $(DEBUG_FLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
# Для стороннего miniz подавлены предупреждения; отладочный вариант сохраняет символы.
$(addsuffix /miniz.o,$(addprefix build/,$(VARIANTS))): build/%/miniz.o: src/miniz.c src/miniz.h Makefile
	@mkdir -p $(@D)
	$(if $(filter win%,$*),$(CC_WIN),$(CC_LINUX)) $(COMMON_FLAGS) -D_LARGEFILE64_SOURCE $(if $(filter debug,$*),$(DEBUG_OPT) $(DEBUG_FLAGS),$(STANDALONE_OPT) $(RELEASE_FLAGS)) $(CPPFLAGS) $(CFLAGS) -w -MMD -MP -c $< -o $@
build/%/icon.o: src/icon.rc assets/icons/icon.ico Makefile
	@mkdir -p $(@D)
	$(WINDRES) $< -O coff -o $@
build/win/delay_%.a: $(WIN_SYSROOT)/bin/%.dll tools/make_delaylib.py Makefile
	@mkdir -p $(@D)
	$(PYTHON) tools/make_delaylib.py $< $(@:.a=.def) $*.dll $(OBJDUMP_WIN)
	$(DLLTOOL) -d $(@:.a=.def) -y $@
data/%.dll: $(WIN_SYSROOT)/bin/%.dll | data
	cp $< $@
# Linux: обычный релиз, встроенные ресурсы и отдельный отладочный бинарник.
captain-comic: $(OBJECTS_LINUX) | data/assets.pak
	$(CC_LINUX) $(RELEASE_LINK) $(LDFLAGS) $^ $(LINUX_PKG_LIBS) $(LDLIBS) -o $@
captain-comic-debug: $(OBJECTS_DEBUG) | data/assets.pak
	$(CC_LINUX) $(DEBUG_LINK) $(LDFLAGS) $^ $(LINUX_PKG_LIBS) $(LDLIBS) -o $@
deps/linux/build_img/libSDL2_image.a deps/linux/build_ttf/libSDL2_ttf.a &:
	sh deps/linux/deps.sh
captain-comic-standalone: $(OBJECTS_STANDALONE) deps/linux/build_img/libSDL2_image.a deps/linux/build_ttf/libSDL2_ttf.a
	$(CC_LINUX) $(RELEASE_LINK) -static-libgcc $(LDFLAGS) $(OBJECTS_STANDALONE) $(LINUX_STATIC_LIBS) $(LDLIBS) -o $@
# Совместимое имя: это копия standalone, а не полностью статический ELF.
captain-comic-static: captain-comic-standalone
	cp $< $@
# Windows: DLL берутся из data/; standalone использует статические библиотеки.
captain-comic.exe: $(OBJECTS_WIN) $(WIN_DELAY_LIBS) | data/assets.pak
	$(CC_WIN) $(RELEASE_LINK) $(LDFLAGS) $(OBJECTS_WIN) $(WIN_LIBRARY_PATH) -lmingw32 -lSDL2main $(WIN_DELAY_LIBS) -ldelayimp -mwindows $(LDLIBS) -o $@
captain-comic-standalone.exe: $(OBJECTS_WIN_STANDALONE)
	$(CC_WIN) $(RELEASE_LINK) -static-libgcc $(LDFLAGS) $^ $(WIN_LIBRARY_PATH) $(WIN_STATIC_LIBS) $(LDLIBS) -o $@
# Дистрибутивы всегда используют релизные имена бинарников.
dist: all
	$(PYTHON) tools/dist.py --windows-format $(WIN_FMT)
dist-7z:
	$(MAKE) dist WIN_FMT=7z
check: linux
	./captain-comic --self-test
check-resources: linux
	$(PYTHON) tools/check_resources.py
check-input: build/input-loop-check
	./build/input-loop-check
check-ending: build/ending-loop-check
	./build/ending-loop-check
# Интеграционные проверки выполняют настоящий цикл приложения.
build/input-main.o: src/main.c src/cc.h Makefile
	@mkdir -p $(@D)
	$(CC_LINUX) $(COMMON_FLAGS) $(WARNINGS) $(LINUX_FLAGS) $(RELEASE_OPT) $(CPPFLAGS) $(CFLAGS) -Dmain=cc_application_main -MMD -MP -c $< -o $@
build/input-loop-check: tools/input_loop_check.c build/input-main.o $(filter-out build/linux/main.o,$(OBJECTS_LINUX))
	$(CC_LINUX) $(WARNINGS) $(LINUX_FLAGS) $(CPPFLAGS) $(CFLAGS) -Isrc $^ $(LDFLAGS) $(LINUX_PKG_LIBS) $(LDLIBS) -Wl,--wrap=game_key -Wl,--wrap=game_update -Wl,--wrap=game_draw -o $@
build/ending-loop-check: tools/ending_loop_check.c build/input-main.o $(filter-out build/linux/main.o,$(OBJECTS_LINUX))
	$(CC_LINUX) $(WARNINGS) $(LINUX_FLAGS) $(CPPFLAGS) $(CFLAGS) -Isrc $^ $(LDFLAGS) $(LINUX_PKG_LIBS) $(LDLIBS) -Wl,--wrap=game_draw -Wl,--wrap=game_key -o $@
# Ресурсы для проверок читаются из assets/, упаковка им не требуется.
clean:
	rm -rf build dist
	rm -f captain-comic captain-comic-debug captain-comic-standalone captain-comic-static captain-comic.exe captain-comic-standalone.exe
	rm -f data/assets.pak src/assets_data.h data/*.dll
# Зависимости заголовков, включая sound_data.h и встроенный пакет ресурсов.
-include $(wildcard build/*/*.d build/*.d)

