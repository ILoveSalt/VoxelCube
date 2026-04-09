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
| 1.9 | Threading | JobSystem, thread pool | ✅ Готово | Есть `JobSystem` с worker pool, `JobHandle`, `Wait/WaitIdle`, `ParallelFor`, настройка worker count через `.vcconfig` и sandbox smoke-test на фоновой задаче |
| 1.10 | Memory | Custom allocator, arena, pool | ✅ Готово | Есть `Memory` tracker, `ArenaAllocator`, `PoolAllocator`; sandbox проверяет arena reset, pool allocate/free и возврат active bytes к нулю после teardown |

### 🟣 ECS — Entity Component System

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 2.1 | ECS | entt интеграция | ✅ Готово | Добавлен публичный ECS facade: CMake auto-detect `EnTT` + fallback `BootstrapRegistry`, доступный через umbrella header |
| 2.2 | ECS | Entity spawn / destroy | ✅ Готово | Есть `CreateEntity`, `DestroyEntity`, `Wrap`, `Clear`; sandbox проверяет spawn/destroy и счётчик alive entities |
| 2.3 | ECS | Component add / get / remove | ✅ Готово | Есть `AddComponent`, `HasComponent`, `GetComponent`, `RemoveComponent`, `View`; sandbox проверяет `Name`/`Transform`/`Health` |
| 2.4 | ECS | System регистрация и порядок | ✅ Готово | Есть `SystemScheduler` со stage-based pipeline (`Startup`, `Update`, `FixedUpdate`, `Shutdown`), numeric order и deterministic execution order; sandbox проверяет порядок и выполнение систем |
| 2.5 | ECS | Transform компонент | ✅ Готово | Добавлены `Vec3` и `TransformComponent` с `translation/rotationEulerDegrees/scale`, helpers `Translate/Rotate/SetUniformScale` и direction vectors `Forward/Right/Up`; sandbox использует общий компонент |
| 2.6 | ECS | Camera компонент | ✅ Готово | Добавлен `CameraComponent` с `Perspective/Orthographic`, `primary/active`, `aspect/fov/near/far`, helpers для viewport/projection; sandbox создаёт main camera entity и обновляет её через ECS systems |

### 🔴 RENDERER — DirectX 11

| # | Подсистема | Задача | Статус | Примечания |
|---|---|---|---|---|
| 3.1 | DX11 Init | Device, adapter, debug layer | ✅ Готово | Есть `DX11Device`: выбор hardware adapter через DXGI, fallback на WARP, создание `ID3D11Device`, feature level detection и debug-layer fallback; sandbox проверяет bootstrap в рантайме |
| 3.2 | DX11 Init | DeviceContext, immediate context | ✅ Готово | `DX11Device` теперь хранит и отдаёт `ID3D11DeviceContext`, пишет `context type/flags` в info, предоставляет native getters; sandbox проверяет `Immediate` context и доступность native handles |
| 3.3 | DX11 Init | SwapChain (DXGI), Present | ✅ Готово | Есть `DX11SwapChain` на `IDXGISwapChain1`: `CreateSwapChainForHwnd`, resize, `Present`, flip-discard и present counter; sandbox проверяет DXGI bootstrap и 300 успешных presents |
| 3.4 | DX11 Init | RenderTargetView, DepthStencilView, SRV | ✅ Готово | Есть `DX11RenderTargets`: back buffer `RTV`, typeless depth texture, `DSV` и depth `SRV`; sandbox проверяет bind/clear и считает 300 clears вместе с 300 presents |
| 3.5 | DX11 Sync | Flush, синхронизация DeviceContext | ✅ Готово | Есть `DX11ContextSync`: `Flush` для immediate context и `WaitForGpuIdle` через `D3D11_QUERY_EVENT`; sandbox runtime-подтверждает периодические flush-вызовы и успешное ожидание `GPU idle` на shutdown без таймаута |
| 3.6 | Memory | Управление буферами (Map/Unmap) | ✅ Готово | Есть `DX11Buffer`: generic/vertex/index/constant buffer abstraction, `Write`, `CopyFrom`, явные `Map/Unmap`, dynamic/staging/default usage и CPU read/write flags; sandbox runtime-подтверждает dynamic upload, staging readback и checksum-валидацию на кадрах 120 и 240 |
| 3.7 | Shaders | FXC/D3DCompile компиляция HLSL шейдеров | ✅ Готово | Есть `DX11ShaderCompiler`: `CompileFromFile`, `CompileFromSource`, DX11 shader stage/profile builder, macro defines и diagnostics/warnings; sandbox runtime-подтверждает компиляцию `sandbox_bootstrap.hlsl` и inline HLSL source в валидный bytecode без warning-ов |
| 3.8 | Pipeline | Input Layout, Rasterizer, Blend State | ✅ Готово | Есть `DX11PipelineState`: `CreateVertexShader/CreatePixelShader`, input layout из compiled vertex bytecode, rasterizer/blend state, primitive topology и viewport binding; sandbox runtime-подтверждает pipeline creation и 300 bind-вызовов на кадрах |
| 3.9 | Geometry | VertexBuffer, IndexBuffer upload | ✅ Готово | Есть `DX11GeometryBuffer`: immutable vertex/index upload, `Bind`, `DrawIndexed`, index-format abstraction и runtime-валидация; sandbox поднимает bootstrap triangle и подтверждает 300 bind/draw вызовов на кадрах |
| 3.10 | Textures | Texture2D загрузка (.dds), SRV | ✅ Готово | Есть `DX11Texture2D`: legacy `.dds` parsing, `ID3D11Texture2D`, `SRV`, point sampler и `BindPS`; sandbox загружает `bootstrap_checker.dds`, использует textured triangle shader path и подтверждает 300 texture bind-вызовов |
| 3.11 | Textures | Texture Atlas система | ✅ Готово | Есть `DX11TextureAtlas`: grid/custom regions, safe UV-rects с half-texel inset, region lookup и bind поверх atlas texture; sandbox строит 4 atlas-региона и переключает textured triangle между ними в рантайме |
| 3.12 | Rendering | Depth Pre-Pass | ✅ Готово | Есть `DX11DepthPrePass`: depth-only bind в `DSV`, depth-write/read-only depth-stencil states и двухпроходный кадр; sandbox делает 300 depth pass + 300 color pass поверх atlas-bootstrap triangle без ошибок |
| 3.13 | Rendering | G-Buffer (Deferred Shading) | ✅ Готово | Есть `DX11GBuffer`: MRT albedo/normal/material render targets с `RTV/SRV`, resize, clear/bind и preview-copy albedo в backbuffer; sandbox делает 300 G-buffer bind/clear/copy проходов поверх depth pre-pass без ошибок |
| 3.14 | Rendering | Lighting Pass (Directional + Point) | ✅ Готово | Есть `DX11LightingPass`: fullscreen deferred resolve в backbuffer, bind `albedo/normal/material/depth` как `SRV`, dynamic lighting constant-buffer, linear-clamp sampler и depth-disabled lighting pass; sandbox подтверждает 300 lighting pass/update/fullscreen draw проходов поверх depth pre-pass + G-buffer без ошибок |
| 3.15 | Rendering | Shadow Maps (CSM) | ✅ Готово | Есть `DX11ShadowMap`: two-cascade shadow-map array с `D32_FLOAT` `DSV`, `R32_FLOAT` `SRV`, cascade bind/clear и интеграцией в `DX11LightingPass`; sandbox подтверждает 300 shadow clear и 600 cascade shadow pass/bind/write проходов вместе со стабильным deferred lighting и `Present` |
| 3.16 | Rendering | Transparent вокселей проход | ✅ Готово | Есть `DX11TransparentPass`: forward alpha-blended transparent pass поверх backbuffer с read-only `LessEqual` depth state и отдельным transparent pipeline; sandbox подтверждает 300 transparent pass/bind/draw проходов после deferred lighting без ошибок |
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

### 🔷 GAMEPLAY — Игровая логика (C++) (просто база для создания игры)

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
3. DX11 базовый (3.1–3.16) — до рендера MGS
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
| 1 | DX11 слой пока не дошёл до полноценного material/voxel draw-пайплайна | 🔄 В процессе | `ID3D11Device`, `ID3D11DeviceContext`, `IDXGISwapChain1`, `RTV/DSV/SRV`, `Flush`, `WaitForGpuIdle`, `DX11Buffer` с `Map/Unmap`, HLSL compile через `DX11ShaderCompiler`, `DX11PipelineState`, `DX11GeometryBuffer`, `DX11Texture2D`, `DX11TextureAtlas`, `DX11DepthPrePass`, `DX11GBuffer`, `DX11ShadowMap`, `DX11LightingPass`, `DX11TransparentPass`, bind/clear, deferred resolve и `Present` уже подняты, но material binding, camera matrices и реальный voxel draw path ещё впереди на этапах 3.17+ |
| 2 | `spdlog` не закреплён как обязательная зависимость репозитория | 🔄 В процессе | CMake уже умеет автоматически подключать `spdlog`, но текущий bootstrap полностью работает и без него на встроенном backend с файловым логом |
| 3 | В build-логе мелькает `pwsh.exe` warning от vcpkg | 🔄 В процессе | Сборка не падает: vcpkg автоматически откатывается на `powershell.exe`; позже можно добавить PowerShell 7 в `PATH` |
| 4 | Hot-watch пока polling-based | 🔄 В процессе | `FileSystemWatcher` уже рабочий и подходит для dev/hot-reload; при необходимости позже можно заменить на OS-level notifications |
| 5 | Memory tracker пока не перехватывает весь `new/delete` проекта | 🔄 В процессе | Текущий учёт покрывает аллокации через `vc::Memory`, а также backing storage `ArenaAllocator`/`PoolAllocator`; глобальный hook можно добавить позже при реальной необходимости |
| 6 | `EnTT` пока не закреплён как обязательная зависимость репозитория | 🔄 В процессе | `CMake` уже умеет автоматически подключать `EnTT` и включать `VC_HAS_ENTT`, но на машинах без пакета движок использует встроенный `BootstrapRegistry` |
| 7 | ECS system scheduler пока не поддерживает dependency graph между системами | 🔄 В процессе | Текущая версия уже даёт детерминированный stage/order pipeline, которого достаточно для bootstrap/gameplay-base; dependency-based scheduling можно добавить позже, если реально понадобится |
| 8 | `TransformComponent` пока хранит только local TRS без parent/child hierarchy | 🔄 В процессе | Текущего `translation/rotationEulerDegrees/scale` достаточно для bootstrap, gameplay и будущей камеры; world transforms и scene graph можно добавить следующим слоем |
| 9 | `CameraComponent` пока не строит полноценные view/projection matrices и frustum | 🔄 В процессе | Сейчас компонент уже хранит projection settings, aspect/FOV helpers и годится для gameplay/bootstrap; матрицы и frustum logic логично добавлять вместе с DX11 renderer |
| 10 | DX11 слой пока поднимает только immediate context без deferred contexts | 🔄 В процессе | Для текущего bootstrap и ближайшего swapchain/present pipeline достаточно `ID3D11DeviceContext`; deferred contexts и multithreaded command recording можно добавить позже, если реально понадобятся |
| 11 | DX11 renderer пока ограничен textured atlas-bootstrap deferred lighting path без material system и voxel renderer | 🔄 В процессе | `RTV/DSV/SRV`, `Flush`, `WaitForGpuIdle`, `DX11Buffer`, `DX11ShaderCompiler`, `DX11PipelineState`, `DX11GeometryBuffer`, `DX11Texture2D`, `DX11TextureAtlas`, `DX11DepthPrePass`, `DX11GBuffer`, `DX11ShadowMap`, `DX11LightingPass` и `DX11TransparentPass` уже созданы и проверены, sandbox проходит depth-only pre-pass, пишет albedo/normal/material в MRT, делает two-cascade shadow-map pass, fullscreen lighting resolve и отдельный transparent alpha-blend pass в backbuffer, но material system, camera matrices и настоящий voxel renderer ещё впереди |
| 12 | `DX11Buffer::Write` для dynamic-буферов пока рассчитан на whole-buffer update | 🔄 В процессе | Текущий слой уже покрывает безопасный bootstrap-path через `WriteDiscard` и staging readback; partial updates для dynamic buffers и более тонкая upload-стратегия могут понадобиться позже на этапах geometry/material pipeline |
| 13 | Shader compilation пока работает как runtime bootstrap, без `.cso` cache/pipeline на этапе сборки | 🔄 В процессе | `DX11ShaderCompiler` уже даёт валидный bytecode из файла и source, чего достаточно для текущего renderer bootstrap; отдельный shader asset pipeline с precompiled `.cso` и build-step можно добавить позже, если он реально понадобится |
| 14 | `DX11Texture2D` пока поддерживает только legacy uncompressed 32-bit DDS | 🔄 В процессе | Текущий loader уже покрывает bootstrap `.dds` path для `B8G8R8A8_UNORM/R8G8B8A8_UNORM` и этого достаточно для texture smoke-test; DX10-header DDS, BC-compression, arrays/cubemaps и mip-generation можно добавить следующим слоем при реальной необходимости |
| 15 | `DX11TextureAtlas` пока не делает runtime packing и не связан с material system | 🔄 В процессе | Текущий atlas-слой уже покрывает grid/custom regions, safe UVs и runtime region switching поверх одной atlas texture; packing/import pipeline, material binding и voxel-atlas integration логично добавлять следующим слоем |
| 16 | `DX11LightingPass` пока остаётся bootstrap deferred resolve без material system и полноценной camera reconstruction | 🔄 В процессе | Текущий слой уже использует все `albedo/normal/material/depth` входы, делает directional + point lighting, directional shadowing через two-cascade shadow-map array и пишет результат в backbuffer, но пока работает на упрощённой fullscreen-схеме с packed world position в material target, без полноценных camera matrices, устойчивой world/view-space reconstruction и полноценной material packing schema |
| 17 | `DX11TransparentPass` пока реализует только базовый alpha-blended forward path без сортировки прозрачности | 🔄 В процессе | Текущий слой уже даёт отдельный transparent pass поверх depth buffer с read-only `LessEqual` depth test и стандартным `SrcAlpha/InvSrcAlpha` blending; sorting, weighted/OIT подходы и интеграция с настоящими transparent voxel materials можно добавлять следующим шагом при необходимости |

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
| 2026-04-08 | Реализован `JobSystem`: worker thread pool, `JobHandle`, очереди задач, `Wait/WaitIdle`, `ParallelFor`, интеграция в lifecycle `Application`, настройка `threading.workerCount` через `.vcconfig`; sandbox проверен на warmup-задаче и фоне подсчёта простых чисел | 1.9 |
| 2026-04-08 | Реализован memory-layer: `Memory` со статистикой аллокаций, `ArenaAllocator`, `PoolAllocator`, интеграция в umbrella headers и sandbox smoke-test на arena/pool с проверкой `activeBytes -> 0` после teardown | 1.10 |
| 2026-04-08 | Реализован ECS registry-layer: публичные `Registry`/`Entity`, auto-detect `EnTT` через `CMake` и fallback `BootstrapRegistry`, sandbox smoke-test на spawn/destroy, `Add/Get/Remove` и `View` по компонентам | 2.1-2.3 |
| 2026-04-08 | Реализован ECS system-layer: header-only `SystemScheduler`, `SystemStage` и `SystemContext`, deterministic order по stage/priority, интеграция в umbrella headers и sandbox smoke-test на `Startup/Update/FixedUpdate/Shutdown` pipeline | 2.4 |
| 2026-04-08 | Реализован базовый transform-layer: `Vec3` и `TransformComponent` с local TRS, direction helpers и utility-методами; sandbox переведён с локального `SandboxTransformComponent` на общий ECS-компонент и runtime-проверяет movement через `Translate` | 2.5 |
| 2026-04-08 | Реализован `CameraComponent`: `Perspective/Orthographic`, `primary/active`, aspect/FOV/clip настройки и projection helpers; sandbox создаёт main camera entity, синхронизирует aspect ratio с окном и проверяет follow/telemetry через ECS systems | 2.6 |
| 2026-04-08 | Реализован DX11 bootstrap-layer: `DX11Device` с DXGI factory/adapter selection, hardware/WARP fallback, `ID3D11Device` creation, feature level detection и debug-layer fallback; sandbox runtime-проверяет устройство на реальном адаптере | 3.1 |
| 2026-04-08 | Реализован `immediate context` слой для DX11: `DX11Device` хранит `ID3D11DeviceContext`, публикует native getters и context metadata (`type/flags/availability`); sandbox runtime-подтверждает `Immediate` context и готовность native device/context pair | 3.2 |
| 2026-04-08 | Реализован DXGI swapchain-layer: `DX11SwapChain` с `CreateSwapChainForHwnd`, `Resize`, `Present`, flip-discard и счётчиком presents; sandbox runtime-подтверждает создание swap chain и успешный `Present` на 300 кадрах | 3.3 |
| 2026-04-08 | Реализован слой render targets для DX11: `DX11RenderTargets` создаёт back buffer `RTV`, typeless depth texture, `DSV` и depth `SRV`, умеет `Bind/Clear/Resize`; sandbox runtime-подтверждает создание render targets и 300 clear-операций вместе с 300 presents | 3.4 |
| 2026-04-08 | Реализован DX11 sync-layer: `DX11ContextSync` добавляет `Flush` для immediate context и `WaitForGpuIdle` через `D3D11_QUERY_EVENT`; sandbox runtime-подтверждает flush-счётчик в кадре и успешное ожидание `GPU idle` на shutdown без таймаута | 3.5 |
| 2026-04-08 | Реализован DX11 buffer-layer: `DX11Buffer` добавляет generic/vertex/index/constant abstraction, `Write`, `CopyFrom` и явные `Map/Unmap` поверх `ID3D11Buffer`; sandbox runtime-подтверждает dynamic constant-buffer upload, staging readback и checksum-валидацию содержимого | 3.6 |
| 2026-04-08 | Реализован DX11 shader compile-layer: `DX11ShaderCompiler` добавляет `CompileFromFile`, `CompileFromSource`, shader-stage/profile builder и diagnostics/warnings; sandbox runtime-подтверждает компиляцию vertex/pixel HLSL из `sandbox_bootstrap.hlsl` и inline source в валидный bytecode | 3.7 |
| 2026-04-08 | Реализован DX11 pipeline-state layer: `DX11PipelineState` создаёт `ID3D11VertexShader/ID3D11PixelShader`, input layout, rasterizer state, blend state и bind-ит viewport/topology; sandbox runtime-подтверждает создание pipeline и 300 bind-вызовов на кадрах без ошибок | 3.8 |
| 2026-04-08 | Реализован DX11 geometry-layer: `DX11GeometryBuffer` добавляет vertex/index upload, `Bind` и `DrawIndexed`; sandbox runtime-поднимает bootstrap triangle и подтверждает 300 geometry bind/draw вызовов вместе с `Present` без ошибок | 3.9 |
| 2026-04-08 | Реализован DX11 texture-layer: `DX11Texture2D` добавляет `.dds`-загрузку, `ID3D11Texture2D`, `SRV`, point sampler и `BindPS`; sandbox загружает `sandbox/assets/bootstrap_checker.dds`, использует textured triangle shader path и подтверждает 300 texture bind-вызовов вместе с `Present` без ошибок | 3.10 |
| 2026-04-08 | Реализован DX11 texture-atlas layer: `DX11TextureAtlas` добавляет grid/custom regions и safe UV-rects; sandbox строит 4 atlas-региона поверх `bootstrap_checker.dds`, переключает их на кадрах 1/76/151/226 и подтверждает 300 atlas bind-вызовов вместе с `Present` | 3.11 |
| 2026-04-08 | Реализован DX11 depth pre-pass layer: `DX11DepthPrePass` добавляет depth-only bind в `DSV`, depth-write/read-only states и двухпроходный кадр; sandbox подтверждает 300 depth pass + 300 color pass, 600 pipeline bind-вызовов и 600 geometry draw-вызовов без ошибок | 3.12 |
| 2026-04-08 | Реализован DX11 G-buffer layer: `DX11GBuffer` добавляет albedo/normal/material MRT с `RTV/SRV`, resize, clear/bind и preview-copy albedo в backbuffer; sandbox подтверждает 300 G-buffer bind/clear/copy проходов, корректный resize на `1920x1017` и стабильный `Present` без ошибок | 3.13 |
| 2026-04-08 | Реализован DX11 lighting-pass layer: `DX11LightingPass` добавляет fullscreen deferred resolve с directional + point lighting, bind `albedo/normal/material/depth` как `SRV`, dynamic constant-buffer и lighting output в backbuffer; sandbox подтверждает 300 lighting pass/update/fullscreen draw проходов и стабильный `Present` без ошибок | 3.14 |
| 2026-04-08 | Реализован DX11 shadow-map layer: `DX11ShadowMap` добавляет two-cascade shadow-map array с `D32_FLOAT` `DSV`, `R32_FLOAT` `SRV`, shadow pass bind/clear и интеграцию в `DX11LightingPass`; sandbox подтверждает 300 shadow clear, 600 cascade shadow pass/bind/write вызовов, 300 lighting resolve и стабильный `Present` без ошибок | 3.15 |
| 2026-04-08 | Реализован DX11 transparent-pass layer: `DX11TransparentPass` добавляет alpha-blended forward transparent pass поверх backbuffer с read-only `LessEqual` depth state; sandbox подтверждает 300 transparent pass/bind/draw проходов после deferred lighting, корректный depth-aware overlay и стабильный `Present` без ошибок | 3.16 |

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
