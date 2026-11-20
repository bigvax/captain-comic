# Changelog

## - 2026-11-20

### Добавлено
- Автоматизация управления версиями и публикации релизов на GitHub в `Makefile`: цели `make bump-patch`, `make bump-minor`, `make bump-major` (с единым источником правды в `src/cc.h`) и `make release` (сборка всех платформ, создание тега и загрузка артефактов через `gh release`).
- Шаблон описания релизов `packaging/RELEASE_NOTES.md` с подробным описанием вариантов поставки для Linux x64, Windows x64 и WebAssembly.
- Локальная сборка статической библиотеки `libSDL2.a` в `deps/linux/deps.sh` и её подключение в `Makefile` для автономной сборки `captain-comic-standalone` в современных дистрибутивах Linux без системного `libSDL2.a`.

### Исправлено
- Устранено падение (`SIGSEGV`) в интеграционном тесте `tools/ending_loop_check.c` при работе поверх прослойки `sdl2-compat` (`libSDL3`) путём прямого связывания тестового раннера со статической `libSDL2.a` и инициализации `SDL_StartTextInput()`.
- Добавлена недостающая линковка математической библиотеки (`-lm`) для статических архивов `libpng16.a` и `libSDL2.a` в `LINUX_STATIC_LIBS`.
