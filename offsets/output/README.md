# cs2-dumper output

Сюда клади **свежий вывод** [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) после обновления CS2.

Обычно это файлы вроде:

- `offsets.hpp` / `offsets.json` — глобальные оффсеты модулей
- `client_dll.hpp` / `client_dll.json` — схема `client.dll`
- `engine2_dll.hpp`, `schemasystem_dll.hpp`, и т.д. (зависит от версии дампера)

## Как использовать в LitWare

1. Собери дамп на актуальной версии игры.
2. Сверь значения с `litware-dll/src/core/offsets.h` и **перенеси** нужные константы вручную (или скриптом).
3. Если ломаются только сигнатуры/паттерны — правь хуки в `litware-dll` (например `render_hook.cpp`), не только числа.

Сгенерированные файлы в этом каталоге **не коммитятся** (см. корневой `.gitignore`), остаётся только этот README.
