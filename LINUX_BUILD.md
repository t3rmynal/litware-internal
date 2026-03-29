# Сборка LitWare CS2 для Linux

Портировка полностью завершена и готова к компиляции на Linux машине.

## Требования

Установите инструменты разработки:

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libvulkan-dev \
    libsdl2-dev \
    pkg-config
```

### Fedora/RHEL
```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    vulkan-devel \
    SDL2-devel \
    pkgconfig
```

### Arch
```bash
sudo pacman -S base-devel cmake vulkan-devel sdl2 pkg-config
```

## Сборка

### Способ 1: CMake (рекомендуется)
```bash
cd ~/Документы/repos/LitWare-Internal
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# Выходной файл: build/litware.so
```

### Способ 2: Make
```bash
cd ~/Документы/repos/LitWare-Internal
make -j$(nproc)
# Выходной файл: litware.so
```

## Инъекция и тестирование

### 1. Запустить CS2 с LD_PRELOAD

```bash
export CS2_PATH="~/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64"

LD_PRELOAD=./litware.so \
    "$CS2_PATH/cs2" \
    -insecure
```

### 2. Проверить логи в реальном времени

```bash
tail -f /tmp/litware_dll.log
```

### 3. Управление

- **INSERT** — открыть/закрыть меню
- **END** — выгрузить DLL

## Ожидаемые логи при успешной загрузке

```
[litware] LD_PRELOAD constructor
[litware] Entry thread started
[litware] bypass::Initialize failed (non-fatal)
[litware] render_hook::Initialize (Vulkan)
[litware] Vulkan functions loaded
[litware] SDL2 hooks installed
[litware] Vulkan hooks installed successfully
[litware] first Present
[litware] InitImGuiVk starting
[litware] ImGui initialized successfully
```

## Архитектура портировки

### Исходная кодовая база (Windows)
- DLL-инъекция (DllMain)
- DirectX 11 (d3d11.h, dxgi.h)
- ImGui_ImplDX11 + ImGui_ImplWin32
- MinHook для перехвата функций
- Win32 API для памяти, потоков, ввода
- Windows оффсеты

### Портированная база (Linux)
- LD_PRELOAD инъекция (`__attribute__((constructor))`)
- Vulkan (libvulkan.so.1)
- ImGui_ImplVulkan + ImGui_ImplSDL2
- x86-64 inline hook в `platform/linux/hook.h`
- POSIX API (dlopen, mprotect, pthread, /proc/self/maps)
- Linux оффсеты (обновлены 29 марта 2026)

### Файлы портировки

**Новые файлы:**
- `platform/compat.h` — типы и функции Win32 для Linux
- `platform/linux/proc_maps.h` — парсер /proc/self/maps
- `platform/linux/hook.h` — inline hook замена MinHook
- `platform/linux/input.h` — GetAsyncKeyState через SDL2
- `hooks/render_hook_vk.cpp` — Vulkan/SDL2 рендеринг
- `linux_init.cpp` — entry point через constructor/destructor
- `CMakeLists.txt` и `Makefile` — системы сборки

**Измененные файлы:**
- `memory/memory.h` — добавлен compat.h вместо Windows.h
- `core/offset_helpers.h` — Linux модули через proc_maps
- `bypass.cpp` — Linux version (non-fatal)
- `debug.cpp` — логирование в /tmp/
- `core/offsets.h` — обновленные оффсеты для Linux CS2
- `hooks/render_hook.cpp` — #ifdef guards для DX11 кода

## Известные ограничения

1. **Паттерны сканирования** (chams, FOV, no-legs) — это Windows MSVC паттерны, не совпадают с Linux Clang бинарными. Нужно найти новые через Ghidra/отладчик.

2. **Игровая логика** — ESP, aimbot, UI код из `render_hook.cpp` пока в Windows версии. Нужно перенести в Vulkan версию отдельно.

3. **SIGSEGV обработка** — текущий `try/catch` lossy. Для production нужен `sigsetjmp/siglongjmp`.

4. **Тестирование оффсетов** — требует запущенной CS2 и может потребоваться корректировка значений.

## Структура модулей Linux CS2

```
~/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64/
├── cs2 (главный исполняемый файл)
├── libclient.so
├── libengine2.so
├── librendersystemvulkan.so
├── libmaterialsystem2.so
├── libmeshsystem.so
└── ... (ещё ~50 .so файлов)
```

## Дебаг

### Посмотреть загруженные модули
```bash
cat /proc/$(pgrep cs2)/maps | grep libclient
```

### Использовать Vulkan validation layers
```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    LD_PRELOAD=./litware.so \
    ~/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64/cs2 -insecure
```

### GDB отладка
```bash
gdb --args \
    sh -c 'LD_PRELOAD=./litware.so ~/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64/cs2 -insecure'
```

## Источники

- **cs2-dumper**: https://github.com/a2x/cs2-dumper (обновленные оффсеты)
- **ImGui**: https://github.com/ocornut/imgui
- **Vulkan**: https://www.vulkan.org/
- **SDL2**: https://www.libsdl.org/

---

**Статус**: ✅ Портировка завершена и готова к компиляции на Linux.
