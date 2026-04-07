# VoxelCube Engine — ChatGPT

> Этот файл предназначен для ChatGPT. Читай его перед любой задачей по проекту.
> Здесь описано: что уже сделано, что в процессе, что не начато — и как всё устроено.

---

## 🗂️ Быстрый контекст

| Параметр | Значение |
|---|---|
| Проект | VoxelCube Engine |
| Тип | Воксельный игровой движок |
| Язык | C++20|
| Рендерер | DirectX 11 |
| Физика | NVIDIA PhysX 5 |
| Звук | OpenAL Soft |
| Сборка | CMake 3.25+, MSVC 2022, Windows 10/11 |
| Редактор | Dear ImGui (встроенный) |
| ECS | entt |
| Сеть | ENET (authoritative server, WIP) |

---

## 📊 Таблица прогресса — главная точка отсчёта

> Обновляй статус после каждого завершённого этапа. Формат: `✅ Готово` / `🔄 В процессе` / `⏳ Не начато` / `🚫 Заблокировано`

### 🔵 CORE — Ядро движка

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 1.1 | Engine Loop | Инициализация, game loop, shutdown | ✅ Готово | `Application::Run()` интегрирован с жизненным циклом Win32-окна и корректным shutdown |
| 1.2 | Engine Loop | Delta time, fixed update, фреймлимит | ✅ Готово | Есть `Timestep`, fixed-step accumulator, target FPS и frame budget для smoke-тестов |
| 1.3 | Window | Win32 окно, resize, fullscreen | ✅ Готово | Реализован `Window` wrapper, resize tracking, message pump и fullscreen по `F11` / `Alt+Enter` |
| 1.4 | Input | Клавиатура, мышь, raw input | ✅ Готово | Есть polling API `Input`, состояния клавиш/кнопок мыши, wheel и raw mouse delta через `WM_INPUT` |
| 1.5 | Config | Парсинг `.vcconfig` (JSON) | ✅ Готово | Реализован JSON DOM, загрузка из файла и применение `.vcconfig` к `ApplicationSpecification` |
| 1.6 | Logging | spdlog интеграция, уровни, файл | ✅ Готово | Есть уровни логирования, console/file sinks, настройка через `.vcconfig` и auto-detect `spdlog` через CMake; без зависимости работает встроенный backend |
| 1.7 | FileSystem | Абстракция FS, пути, hot-watch | ✅ Готово | Есть `FileSystem` для path/io и polling-based `FileSystemWatcher`; sandbox hot-watch для `.vcconfig` проверен |
| 1.8 | Events | EventBus, Subscribe/Emit | ✅ Готово | Есть `EventBus` с `Subscribe/Emit`, generic `OnEvent`, события окна и ввода, sandbox использует подписки на resize/wheel/close |
| 1.9 | Threading | JobSystem, thread pool | ⏳ | |
| 1.10 | Memory | Custom allocator, arena, pool | ⏳ | |

### 🟣 ECS — Entity Component System

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 2.1 | ECS | entt интеграция | ⏳ | |
| 2.2 | ECS | Entity spawn / destroy | ⏳ | |
| 2.3 | ECS | Component add / get / remove | ⏳ | |
| 2.4 | ECS | System регистрация и порядок | ⏳ | |
| 2.5 | ECS | Transform компонент | ⏳ | |
| 2.6 | ECS | Camera компонент | ⏳ | |

### 🔴 RENDERER — DirectX 11

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 3.1 | DX11 Init | Device, adapter, debug layer | ⏳ | |
| 3.2 | DX11 Init | DeviceContext, immediate context | ⏳ | |
| 3.3 | DX11 Init | SwapChain (DXGI), Present | ⏳ | |
| 3.4 | DX11 Init | RenderTargetView, DepthStencilView, SRV | ⏳ | |
| 3.5 | DX11 Sync | Flush, синхронизация DeviceContext | ⏳ | |
| 3.6 | Memory | Управление буферами (Map/Unmap) | ⏳ | |
| 3.7 | Shaders | FXC/D3DCompile компиляция HLSL шейдеров | ⏳ | |
| 3.8 | Pipeline | Input Layout, Rasterizer, Blend State | ⏳ | |
| 3.9 | Geometry | VertexBuffer, IndexBuffer upload | ⏳ | |
| 3.10 | Textures | Texture2D загрузка (.dds), SRV | ⏳ | |
| 3.11 | Textures | Texture Atlas система | ⏳ | |
| 3.12 | Rendering | Depth Pre-Pass | ⏳ | |
| 3.13 | Rendering | G-Buffer (Deferred Shading) | ⏳ | |
| 3.14 | Rendering | Lighting Pass (Directional + Point) | ⏳ | |
| 3.15 | Rendering | Shadow Maps (CSM) | ⏳ | |
| 3.16 | Rendering | Transparent вокселей проход | ⏳ | |
| 3.17 | Post-FX | SSAO | ⏳ | |
| 3.18 | Post-FX | TAA (Temporal Anti-Aliasing) | ⏳ | |
| 3.19 | Post-FX | Bloom | ⏳ | |
| 3.20 | Post-FX | Tone Mapping | ⏳ | |
| 3.21 | Advanced | Dynamic Resolution Scaling | ⏳ | |
| 3.22 | Advanced | Geometry Shader для вокселей (опционально) | ⏳ | |
| 3.23 | Advanced | Soft Shadows (PCF) | ⏳ | |
| 3.24 | Advanced | Screen-Space Reflections (SSR) | ⏳ | |
| 3.25 | Advanced | Async texture streaming (WIC/DDSLoader) | ⏳ | |

### 🟠 WORLD & MGS — Мир и воксельная система

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 4.1 | VoxelRegistry | Регистрация типов вокселей | ⏳ | |
| 4.2 | VoxelRegistry | Параметры: текстура, звук, hardness | ⏳ | |
| 4.3 | VoxelData | RLE-сжатие воксельных данных | ⏳ | |
| 4.4 | VoxelCluster | Структура кластера (AABB, DirtyFlag) | ⏳ | |
| 4.5 | MGS | Алгоритм merge: соседние вокселей → 1 кластер | ⏳ | КЛЮЧЕВАЯ задача |
| 4.6 | MGS | Greedy Meshing внутри кластера | ⏳ | |
| 4.7 | MGS | Async rebuild меша (worker threads) | ⏳ | |
| 4.8 | MGS | Авто-split/merge при изменении | ⏳ | |
| 4.9 | Destructibility | Режимы: STATIC / CHUNK_BREAK / VOXEL_BREAK / FRACTURE | ⏳ | |
| 4.10 | World | Chunk система (32×32×256) | ⏳ | |
| 4.11 | World | Динамическая подгрузка чанков | ⏳ | |
| 4.12 | WorldGen | Perlin/Simplex Noise генератор | ⏳ | |
| 4.13 | WorldGen | Биомы | ⏳ | |
| 4.14 | WorldGen | Структуры (деревья, пещеры) | ⏳ | |
| 4.15 | LOD | Imposter/billboard для дальних кластеров | ⏳ | |
| 4.16 | Persistence | Сохранение мира (бинарный формат) | ⏳ | |
| 4.17 | Persistence | Загрузка мира | ⏳ | |
| 4.18 | Raycast | Raycast по вокселям (block picking) | ⏳ | |

### 🟡 PHYSICS — NVIDIA PhysX 5

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 5.1 | PhysX Init | PxFoundation, PxPhysics, PxScene | ⏳ | |
| 5.2 | PhysX Init | PxCooking, PVD debug коннект | ⏳ | |
| 5.3 | Rigid Body | Static / Dynamic actors | ⏳ | |
| 5.4 | Rigid Body | Box, Capsule, Convex shapes | ⏳ | |
| 5.5 | Rigid Body | Материалы (трение, упругость) | ⏳ | |
| 5.6 | Integration | PhysX ↔ MGS коллизии для кластеров | ⏳ | КЛЮЧЕВАЯ задача |
| 5.7 | Integration | Substep синхронизация с game loop | ⏳ | |
| 5.8 | Character | CharacterController (CCT) | ⏳ | |
| 5.9 | Queries | Raycast, Overlap, Sweep | ⏳ | |
| 5.10 | Destruction | Voronoi Fracture для кластеров | ⏳ | |
| 5.11 | Advanced | Cloth Simulation | ⏳ | |
| 5.12 | Advanced | Vehicle Dynamics | ⏳ | |

### 🟢 AUDIO — OpenAL Soft

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 6.1 | Init | Устройство, контекст, инициализация | ⏳ | |
| 6.2 | Playback | AudioSource компонент (позиция, pitch, gain) | ⏳ | |
| 6.3 | Playback | Listener (камера → 3D позиция) | ⏳ | |
| 6.4 | Formats | Загрузка WAV | ⏳ | |
| 6.5 | Formats | Загрузка OGG Vorbis | ⏳ | |
| 6.6 | Formats | Загрузка FLAC | ⏳ | |
| 6.7 | Streaming | Аудио-стриминг для длинных треков | ⏳ | |
| 6.8 | EFX | AuxiliaryEffectSlot (реверберация) | ⏳ | |
| 6.9 | EFX | HRTF 3D звук | ⏳ | |
| 6.10 | System | AudioBank (группы звуков) | ⏳ | |
| 6.11 | System | Ambient звуковые зоны | ⏳ | |

### 🔷 GAMEPLAY — Игровая логика (C++)

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 7.1 | Script | Script компонент (OnInit / OnUpdate / OnDestroy) | ⏳ | |
| 7.2 | Script | Hot Reload C++ через DLL (опционально) | ⏳ | |
| 7.3 | Gameplay | CharacterController (C++) | ⏳ | |
| 7.4 | Gameplay | Inventory система (C++) | ⏳ | |
| 7.5 | Gameplay | Interaction система (raycast → interact) | ⏳ | |
| 7.6 | Gameplay | Game State Machine | ⏳ | |
| 7.7 | Gameplay | Player HUD (ImGui overlay) | ⏳ | |

### 🖥️ EDITOR — VoxelCube Editor

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 8.1 | ImGui | Интеграция Dear ImGui с DX11 | ⏳ | |
| 8.2 | ImGui | Docking layout (панели) | ⏳ | |
| 8.3 | ImGui | Тема: бело-синяя палитра | ⏳ | |
| 8.4 | Viewport | 3D viewport в ImGui (рендер в текстуру) | ⏳ | |
| 8.5 | Viewport | Камера в редакторе (orbit, fly) | ⏳ | |
| 8.6 | Outliner | World Outliner (дерево сущностей) | ⏳ | |
| 8.7 | Inspector | Properties Inspector (компоненты) | ⏳ | |
| 8.8 | FS Browser | File System Browser (дерево проекта) | ⏳ | |
| 8.9 | FS Browser | Превью ресурсов (текстуры, аудио) | ⏳ | |
| 8.10 | FS Browser | Drag & Drop ресурсов в сцену | ⏳ | |
| 8.11 | Tools | VoxelPainter (кисть вокселей) | ⏳ | |
| 8.12 | Tools | TerrainTool (noise-генерация) | ⏳ | |
| 8.13 | Tools | Structure Placer (префабы) | ⏳ | |
| 8.14 | Scene | Сохранение / загрузка `.vcscene` | ⏳ | |
| 8.15 | Scene | Undo / Redo система | ⏳ | |
| 8.16 | Play Mode | Запуск игры из редактора | ⏳ | |
| 8.17 | Code Editor | Встроенный редактор кода (C++) | ⏳ | |
| 8.18 | Code Editor | LSP автодополнение | ⏳ | |

### 🔨 BUILD SYSTEM — Компилятор игр

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 9.1 | Build | CMake-сборка игрового проекта из редактора | ⏳ | |
| 9.2 | Build | Упаковка ресурсов в `.vcpak` (LZ4) | ⏳ | |
| 9.3 | Build | Strip debug символов (Shipping) | ⏳ | |
| 9.4 | Build | Strip symbols, PDB для Shipping | ⏳ | |
| 9.5 | Build | Итоговый `.exe` + DLL bundle | ⏳ | |
| 9.6 | Build | Таргет: Linux x64 (Vulkan fallback) | ⏳ | |
| 9.7 | Build | Таргет: macOS ARM (Metal fallback) | ⏳ | |
| 9.8 | CLI | `voxelcube-build` утилита командной строки | ⏳ | |

### 🌐 NETWORK — Мультиплеер

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 10.1 | Transport | TCP/UDP транспортный слой | ⏳ | |
| 10.2 | Transport | Сериализация пакетов (LZ4 сжатие) | ⏳ | |
| 10.3 | Server | Выделенный сервер (headless, без DX11) | ⏳ | |
| 10.4 | Server | Конфиг сервера `server.cfg` | ⏳ | |
| 10.5 | Replication | VCNET_REPLICATED — авто-синхронизация переменных | ⏳ | |
| 10.6 | Replication | VCNET_SERVER_RPC | ⏳ | |
| 10.7 | Replication | VCNET_MULTICAST_RPC | ⏳ | |
| 10.8 | World sync | Синхронизация вокселей между клиентами | ⏳ | |
| 10.9 | Prediction | Client-side prediction | ⏳ | |
| 10.10 | Build | Автогенерация серверной сборки из редактора | ⏳ | |

---

## 🏗️ Структура директорий (актуальная)

```
VoxelCube/
├── CMakeLists.txt
├── CMakePresets.json
├── Что_обьяснять_ChatGPT.md   ← этот файл
├── README.md
│
├── engine/
│   ├── include/VoxelCube/
│   └── src/
│       ├── core/              — Engine, Loop, Config, Events, FileSystem
│       ├── world/             — MGS, Cluster, VoxelRegistry, WorldGen
│       ├── renderer/          — DX11 Renderer, Shaders, Materials, PostFX
│       ├── physics/           — PhysX интеграция
│       ├── audio/             — OpenAL, AudioSource, EFX
│       ├── ecs/               — Entity, Component, System (entt)
│       ├── network/           — Server, Client, Replication
│       └── gameplay/          — Script компонент, GameState, CharacterController
│
├── editor/
│   └── src/
│       ├── panels/            — Viewport, Outliner, Inspector, FSBrowser
│       ├── tools/             — VoxelPainter, TerrainTool
│       └── build_system/      — Build, Pack, Deploy
│
├── shaders/                   — HLSL (DX11, FXC)
├── sandbox/                   — тестовая игра
├── tests/
└── docs/
```

---

## 📌 Правила для ChatGPT

### Перед началом любой задачи

1. **Прочитай таблицу прогресса** выше и определи, к какому блоку относится задача. Работаем на чистом C++, без скриптовых языков.
2. **Не реализуй то, что помечено `✅ Готово`** — только если не просят переписать.
3. **Учитывай зависимости**: DX11 (3.x) нужен до MGS меша (4.6), ECS (2.x) нужен до компонентов физики (5.8).
4. **Спрашивай статус**, если он влияет на задачу: *"4.5 MGS merge алгоритм сделан? Покажи текущую реализацию"*.

### Архитектурные договорённости

- Все публичные типы живут в `engine/include/VoxelCube/` — никогда не в `src/`
- Умные указатели: `vc::Ref<T>` = `std::shared_ptr<T>`, `vc::Scope<T>` = `std::unique_ptr<T>`
- `VC_ASSERT(cond, msg)` вместо голых `assert`
- Логирование только через `VC_LOG_TRACE / VC_LOG_INFO / VC_LOG_WARN / VC_LOG_ERROR` (встроенный console/file backend уже работает; при наличии зависимости CMake автоматически подключает `spdlog`)
- Все строки путей через `vc::Path` (обёртка над `std::filesystem::path`)
- HLSL шейдеры — `shaders/` в корне, компилируются через FXC/D3DCompile в `.cso` при сборке

### Соглашения кода

```cpp
// Типы: PascalCase
class VoxelCluster { };

// Методы: PascalCase
void RebuildMesh();

// Переменные: camelCase
float mergeThreshold = 64.0f;

// Члены класса: m_ prefix
float m_hardness;

// Константы: UPPER_SNAKE
constexpr int MAX_CLUSTER_SIZE = 128;

// Пространства имён: lowercase
namespace vc { namespace world { } }
```


### Приоритет реализации (текущий порядок)

```
1. Core (1.x)          — без этого ничего не работает
2. ECS (2.x)           — нужен всем подсистемам
3. DX11 базовый (3.1–3.9)  — до рендера MGS
4. MGS core (4.1–4.8)  — ключевая фича движка
5. PhysX базовый (5.1–5.9)
6. OpenAL (6.1–6.7)
7. Gameplay C++ (7.1–7.7)
8. Редактор (8.x)      — только после стабильного core
9. Build System (9.x)
10. Network (10.x)      — в последнюю очередь
```

---

## 🐛 Известные проблемы и решения

> Заполняй по мере разработки.

| # | Проблема | Статус | Решение / Заметка |
|---|---|---|---|
| 1 | Окно пока без DX11-рендера | 🔄 В процессе | Win32 lifecycle уже поднят, но `Device/SwapChain/RTV` начнутся на этапе 3.1–3.4 |
| 2 | `spdlog` не закреплён как обязательная зависимость репозитория | 🔄 В процессе | CMake уже умеет автоматически подключать `spdlog`, но текущий bootstrap полностью работает и без него на встроенном backend с файловым логом |
| 3 | В build-логе мелькает `pwsh.exe` warning от vcpkg | 🔄 В процессе | Сборка не падает: vcpkg автоматически откатывается на `powershell.exe`; позже можно добавить PowerShell 7 в `PATH` |
| 4 | Hot-watch пока polling-based | 🔄 В процессе | `FileSystemWatcher` уже рабочий и подходит для dev/hot-reload; при необходимости позже можно заменить на OS-level notifications |

---

## 📝 Журнал изменений (последние)

> Добавляй запись при каждом значимом изменении.

| Дата | Что сделано | Блок |
|---|---|---|
| — | Проект создан, README и CLAUDE.md написаны | — |
| 2026-04-07 | Поднят стартовый CMake-каркас, реализованы `Base`, `Timestep`, `Log`, `Application`, добавлен headless sandbox и проверена сборка через VS2022 | 1.1 / 1.2 / 1.6 |
| 2026-04-07 | Реализовано Win32-окно (`Window`), resize/fullscreen, message pump внутри `Application`, sandbox переведён на оконный режим и проверен запуск | 1.3 |
| 2026-04-07 | Реализована подсистема ввода: клавиатура, мышь, wheel и raw input; `WindowProc` интегрирован с `Input`, sandbox проверен на новом API | 1.4 |
| 2026-04-07 | Реализован парсинг `.vcconfig`: JSON DOM (`ConfigDocument`/`ConfigValue`), загрузка `ApplicationSpecification` из файла и запуск sandbox из `sandbox/sandbox.vcconfig` | 1.5 |
| 2026-04-07 | Логгер доведён до рабочего состояния: уровни, console/file output, конфигурирование через `.vcconfig`, автоматическое подключение `spdlog` через CMake при наличии зависимости и проверка записи в `logs/sandbox/voxelcube_sandbox.log` | 1.6 |
| 2026-04-07 | Реализован `FileSystem`: path utilities, чтение/запись текста, перечисление файлов и polling-based hot-watch; `Config` переведён на новый слой, sandbox watcher проверен на изменении `.vcconfig` | 1.7 |
| 2026-04-07 | Реализован header-only `EventBus`: `Subscribe/SubscribeAny/Emit`, базовый `Event`, `OnEvent` в `Application`, события окна и ввода (`resize`, `fullscreen`, `close`, `keyboard`, `mouse`, `raw input`); sandbox переведён на подписки и runtime-проверен | 1.8 |

---

## 🔗 Ключевые источники и документация

| Тема | Ссылка |
|---|---|
| DirectX 11 | https://learn.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11 |
| DX11 примеры Microsoft | https://github.com/microsoft/DirectX-SDK-Samples |
| NVIDIA PhysX 5 | https://nvidia-omniverse.github.io/PhysX/physx/5.3.1/ |
| OpenAL Soft | https://github.com/kcat/openal-soft/wiki |
| HLSL Reference | https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-reference |
| entt ECS | https://github.com/skypjack/entt/wiki |
| D3D11 Buffer Guide | https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-buffers |
| Dear ImGui DX11 | https://github.com/ocornut/imgui/tree/master/examples/example_win32_directx11 |
| FXC / D3DCompile | https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-part1 |
