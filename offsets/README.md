# CS2 Offsets (cs2-dumper)

Вспомогательная папка для артефактов [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper).

- **`output/`** — положи сюда последний дамп после обновления игры (см. `output/README.md`). Сами сгенерированные файлы в git не попадают.
- **Исполняемый код** использует константы из **`litware-dll/src/core/offsets.h`**.

## После обновления CS2

1. Запусти cs2-dumper и скопируй вывод в `offsets/output/`.
2. Обнови managed-блоки в `litware-dll/src/core/offsets.h` через `python3 offsets/update_offsets.py`.
3. Проверь diff и, если нужно, добей руками оставшиеся netvar/member offsets.
4. Если что-то всё ещё ломается — обнови сигнатуры/паттерны в `litware-dll` (хуки в `render_hook.cpp` и др.), опираясь на новый дамп или сообщество.

## Быстрый запуск

```sh
python3 offsets/update_offsets.py
```

Скрипт читает всё из `offsets/output/`, обновляет только поддерживаемые namespace-блоки в `litware-dll/src/core/offsets.h` и оставляет остальные member offsets как есть.
