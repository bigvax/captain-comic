# Changelog

## [0.5.64] - 2026-11-20

### Добавлено
- Автоматизация управления версиями и публикации релизов на GitHub в `Makefile`: цели `make bump-patch`, `make bump-minor`, `make bump-major` (с единым источником правды в `src/cc.h`) и `make release` (сборка всех платформ, создание тега и загрузка артефактов через `gh release`).
- Шаблон описания релизов `packaging/RELEASE_NOTES.md` с подробным описанием вариантов поставки для Linux x64, Windows x64 и WebAssembly.
- Локальная сборка статической библиотеки `libSDL2.a` в `deps/linux/deps.sh` и её подключение в `Makefile` для автономной сборки `captain-comic-standalone` в современных дистрибутивах Linux без системного `libSDL2.a` (коммит `e1328ae`).

### Исправлено
- Устранено падение (`SIGSEGV`) в интеграционном тесте `tools/ending_loop_check.c` при работе поверх прослойки `sdl2-compat` (`libSDL3`) путём прямого связывания тестового раннера со статической `libSDL2.a` и инициализации `SDL_StartTextInput()`.
- Добавлена недостающая линковка математической библиотеки (`-lm`) для статических архивов `libpng16.a` и `libSDL2.a` в `LINUX_STATIC_LIBS`.

## [0.5.63] - 2026-11-18

- Изменил README и стили для web
- Изменил Makefile и README.
- Изменил WALKTHROUGH.
- Изменил README.
- Изменил стили для web.
- Изменил README.
- Изменил WALKTHROUGH. Поправил стили для web документации.
- Изменил README.
- Изменил README. Поправил иконки.
- Изменил README.
- Изменил README. Поправил .css для web
- Изменил README
- Чуть изменил readme
- Заменил в assets/ title.png и legend.png
- Point README play links to captain-comic.ru
- Ship local README viewer and documentation with browser build
- Add README link after shield in browser toolbar
- Expand browser version description in both READMEs
- Move and shorten browser introduction in READMEs
- Clean Emscripten warnings and compile miniz separately
- Check Emscripten availability and document SDK installation
- Mention browser version and play link in both READMEs
- Add WebAssembly browser build with SDL2, launcher and persistent saves
- Replace legend with revised label positioning
- Update legend artwork with revised captions
- Widen score digit group gaps to two pixels
- Replace legend with updated item and enemy captions
- Add one-pixel gaps between score digit groups
- Unify teleport item name as Жезл телепортации
- Increase gap above score editor shortcut hint
- Clear default score name with a single Backspace
- Spell out Shift and Ctrl arrow shortcuts in score editor help
- Separate score editor help and vertically center arrow hints
- Space score hint separators and render Shift/Ctrl arrow shortcuts
- Restore score list spacing and condense colored help to two lines
- Fix score editor Ins/Del, add Ctrl x100 step and move colored help below table
- Unify teleport item name as Стек телепортации
- Replace legend screen with updated optimized artwork
- Update title, story, legend and ending screens with optimized PNGs
- Replace legend screen with losslessly optimized artwork
- Add space after the main README heading
- Set README header icon to 64 by 64 pixels
- Replace README with user-provided revision
- Bold all location names in the shield distribution table
- Bold location names in the five Blastola Cola entries
- Move shield effect before distribution and clarify world.json path
- Align shield artwork with description using GitHub-compatible layout
- Align shield illustration with the top of its description
- Remove borders from shield description block
- Remove empty header row from shield description table
- Update READMEs and rename cola and shield illustrations
- Add supplied Linux static libraries and Windows sysroot
- Replace Castle tile set and three panoramas
- Set README icon size to 64x64
- Set README icon size to 48x48
- Add main repository README
- Import Captain Comic 0.5.63 with resources, documentation and Linux checks

## [0.1.0] - 2026-11-17

- Initial commit
