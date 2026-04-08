# 🧊 VoxelCube Engine
 
> Профессиональный движок для создания воксельных игр нового поколения с собственным редактором, компилятором и мультиплеером
> 
> Текущий детальный прогресс разработки и фактические статусы этапов ведутся в `Что_обьяснять_ChatGPT.md`.
 
![Version](https://img.shields.io/badge/version-0.1.0--alpha-1A6FB5)
![License](https://img.shields.io/badge/license-MIT-22A7F0)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D4)
![Language](https://img.shields.io/badge/language-C%2B%2B20-005A9E)
![Renderer](https://img.shields.io/badge/renderer-DirectX%2011-0066CC)
![Physics](https://img.shields.io/badge/physics-NVIDIA%20PhysX%205-76B900)
 
---
 
## 📋 Содержание
 
- [О проекте](#-о-проекте)
- [Ключевые технологии](#-ключевые-технологии)
- [Система вокселей MGS](#-система-вокселей-voxelcube-merged-geometry)
- [Редактор VoxelCube Editor](#-редактор-voxelcube-editor)
- [Компилятор и сборка игр](#-компилятор-и-сборка-игр)
- [Мультиплеер и сервер](#-мультиплеер-и-сервер)
- [API для разработчиков](#-api-для-разработчиков)
- [Архитектура](#-архитектура)
- [Требования](#-требования)
- [Установка и сборка движка](#-установка-и-сборка-движка)
- [Структура проекта](#-структура-проекта)
- [Дорожная карта](#-дорожная-карта)
- [Вклад в проект](#-вклад-в-проект)
- [Лицензия](#-лицензия)
 
---
 
## 🎮 О проекте
 
**VoxelCube Engine** — это полноценная платформа разработки воксельных игр с собственным редактором в стиле Unreal Engine, встроенным компилятором проектов, поддержкой мультиплеерных серверов и API на **C++**.
 
В основе движка лежит принципиально новая система вокселей — **Merged Geometry System (MGS)** — которая объединяет тысячи мелких воксельных объектов в единый оптимизированный меш, избавляясь от миллионов draw call'ов, характерных для Minecraft-подобных движков.
 
### Чем VoxelCube отличается от аналогов?
 
| Критерий | Minecraft-подход | Teardown-подход | **VoxelCube MGS** |
|---|---|---|---|
| Единица сцены | Блок 1×1×1 | Воксель из точек | Группа вокселей → 1 mesh |
| Draw Calls | Миллионы | Сотни тысяч | **Единицы–сотни** |
| Деструктивность | Поблочная | Полная | **Configurable** (по зонам) |
| Физика | AABB простая | Custom | **NVIDIA PhysX 5** |
| Редактор | Нет | Нет | **Встроенный (UE-стиль)** |
| Скриптинг | Java/Lua | Нет | **C++  |
 
---
 
## ⚙️ Ключевые технологии
 
### Рендеринг — DirectX 11
- Текущий bootstrap уже поднимает `ID3D11Device`, `ID3D11DeviceContext`, `IDXGISwapChain1`, back buffer `RTV`, depth `DSV/SRV`, DX11 context sync через `D3D11_QUERY_EVENT`, общий `DX11Buffer`-слой с `Write`, `CopyFrom`, `Map/Unmap`, `DX11ShaderCompiler` для HLSL bytecode compilation через `D3DCompile`, `DX11PipelineState` для shader/input-layout/rasterizer/blend/viewport binding, `DX11GeometryBuffer` для vertex/index upload, `DX11Texture2D` для `.dds`-загрузки, `SRV` и pixel-shader binding, а также `DX11TextureAtlas` для grid/custom regions поверх atlas texture.
- Следующие шаги рендера: material binding, camera/view-projection data и переход от textured atlas-bootstrap triangle к реальному voxel draw path.
 
### Физика — NVIDIA PhysX 5
- Полная поддержка Rigid Body Dynamics
- Разрушаемые воксельные структуры через Voronoi Fracture
- Cloth Simulation для флагов и тканей
- Particle System — пыль, осколки, взрывы
- Vehicle Dynamics для транспортных средств
- Scene Query (Raycast, Overlap, Sweep) для игровой логики
 
### Звук — OpenAL Soft
- 3D позиционный звук с поддержкой HRTF
- EFX эффекты: реверберация в пещерах, эхо в ущельях
- Динамические звуковые зоны через AuxiliaryEffectSlot
- Поддержка форматов: WAV, OGG Vorbis, FLAC
- Аудио-стриминг для длинных треков без загрузки в RAM
 
---
 
## 🧱 Система вокселей VoxelCube (Merged Geometry)
 
Ключевое ноу-хау движка. Вместо того чтобы хранить и рендерить каждый воксель как отдельный объект, VoxelCube группирует пространственно близкие воксели одного типа в **VoxelCluster** и строит из них единый динамический меш.
 
### Принцип работы
 
```
Воксельные данные          Группировка               Финальный меш
┌─┬─┬─┬─┐                ╔═══════════╗
│G│G│G│G│  ──────────►   ║  Cluster  ║  ──────────►  1 Draw Call
│G│G│G│G│                ║  [Grass]  ║               (64 вокселей
│G│G│G│G│                ╚═══════════╝                → 1 меш)
└─┴─┴─┴─┘
 
Без MGS: 64 объекта = 64 draw calls
С MGS:   64 объекта = 1 draw call
```
 
### VoxelCluster — основная единица
 
```
VoxelCluster
├── AABB bounds             — ограничивающий объём для culling
├── VoxelType               — тип вокселей в кластере
├── DirtyFlag               — флаг перестройки меша
├── VoxelData[N]            — воксельные данные (RLE-сжатие)
├── VertexBuffer (DX11)     — готовый меш на GPU
└── PhysicsShape (PhysX)    — коллизия кластера
```
 
### Режимы деструктивности (Destructibility Mode)
 
Для каждой зоны уровня можно задать свой режим:
 
| Режим | Описание | Применение |
|---|---|---|
| `STATIC` | Нет деструкции, самый быстрый | Фон, архитектура |
| `CHUNK_BREAK` | Разрушение по кластерам | Стены, платформы |
| `VOXEL_BREAK` | Разрушение поблочно | Интерактивные объекты |
| `FRACTURE` | PhysX Voronoi fracture | Взрывы, крупные объекты |
 
### Операции в реальном времени
 
- Добавление и удаление вокселей → автоматический merge соседних кластеров
- Покраска вокселей (смена типа без перестройки топологии)
- Разрушение через PhysX с разлётом осколков
- LOD-система: удалённые кластеры заменяются imposter-billboard
 
---
 
## 🖥️ Редактор VoxelCube Editor
 
Полноценная IDE для создания воксельных игр. Интерфейс выполнен в **бело-синей цветовой гамме** в стилистике Unreal Engine.
 
### Компоновка панелей
 
```
┌─────────────────────────────────────────────────────────────────┐
│  [File]  [Edit]  [Build]  [Server]  [Tools]  [Help]  VoxelCube  │
├─────────────────┬──────────────────────────┬────────────────────┤
│                 │                          │                    │
│  World          │   VIEWPORT  (DX11)       │   Properties       │
│  Outliner       │                          │   Inspector        │
│                 │                          │                    │
│  ── World       │                          │   Transform        │
│     ├ Cluster0  │                          │   Voxel Type       │
│     ├ Cluster1  │                          │   Physics Body     │
│     ├ Light_Sun │                          │   Audio Zone       │
│     └ Player    │                          │   Script           │
│                 │                          │                    │
├─────────────────┴──────────────────────────┴────────────────────┤
│  Content Browser / File System                                  │
│  [ Assets ] [ Scripts ] [ Prefabs ] [ Shaders ] [ Audio ]       │
├─────────────────────────────────────────────────────────────────┤
│  Output / Console / Log                             [Clear]     │
└─────────────────────────────────────────────────────────────────┘
```
 
### Встроенный File System Browser
 
- Иерархическое дерево проекта (как в UE Content Browser)
- Drag & Drop ресурсов прямо в сцену
- Превью текстур, мешей и аудиофайлов прямо в браузере
- Импорт форматов:
  - Текстуры: `.png`, `.jpg`, `.dds`, `.tga`
  - Аудио: `.wav`, `.ogg`, `.flac`
  - Сцены и префабы: `.vcscene`, `.vcprefab` (нативный формат)
  - Скрипты: `.cpp`, `.h`
- Hot Reload шейдеров и C#-скриптов без перезапуска редактора
- Встроенная история изменений и Git-статус файлов
 
### Инструменты редактирования вокселей
 
- **Voxel Painter** — рисование вокселей кистью с настраиваемым радиусом
- **Voxel Sculptor** — выдавливание, вдавливание, сглаживание поверхностей
- **Terrain Tool** — процедурная генерация рельефа с параметрами Noise
- **Structure Placer** — вставка готовых префабов (здания, деревья и т.д.)
- **Cluster Inspector** — просмотр и ручное управление кластерами MGS
 
### Редактор скриптов
 
- Подсветка синтаксиса C++ 
- Автодополнение через Language Server Protocol (LSP)
- Интеграция с VS Code, CLion, Rider через генерацию `.vscode` / `.idea`
- Live-дебаггинг: точки останова, просмотр переменных прямо в редакторе
- Hot Reload C++-скриптов в Play Mode
 
---
 
## 🔨 Компилятор и сборка игр
 
VoxelCube содержит встроенный **Build System** для компиляции и упаковки готовых игр.
 
### Настройки компиляции (Build Settings)
 
```
Project Settings → Build
├── Target Platform:  [Windows x64]  [Linux x64]  [macOS ARM]
├── Build Config:     [Debug]  [Development]  [Shipping]
├── Optimization:     [O0]  [O2]  [O3]
├── Pak Assets:       [✓] Упаковать ресурсы в .vcpak
├── Strip Symbols:    [✓] Убрать отладочные символы (Shipping)
├── Obfuscate C++:     [✓] Скомпилировать .cpp → IL + обфускация
└── Output Dir:       ./Build/Windows_x64/
```
 
### Процесс сборки
 
```bash
# Через редактор: Build → Build Game  (F7)
# Через CLI:
voxelcube-build --project MyGame.vcproject --config Shipping --platform Win64
 
# Результат:
Build/
└── Windows_x64/
    ├── MyGame.exe           — исполняемый файл игры
    ├── VoxelCubeRuntime.dll — рантайм движка
    ├── PhysX5.dll           — физика
    ├── OpenAL32.dll         — звук
    ├── Assets.vcpak         — упакованные ресурсы
    └── Scripts/             — скомпилированные C#-скрипты
```
 
### Поддержка платформ
 
| Платформа | C++ |  Графика | Статус |
|---|---|---|---|---|
| Windows 10/11 x64 | ✅ | DirectX 11 | Основная |
| Linux x64 | ✅ | Vulkan | WIP |
| macOS ARM | ✅ | Metal | Planned |
 
---
 
## 🌐 Мультиплеер и сервер
 
VoxelCube поддерживает создание мультиплеерных игр с автоматической генерацией серверной части.
 
### Создание сервера
 
При включённом `Multiplayer: Enabled` в настройках проекта Build System автоматически создаёт отдельную серверную сборку:
 
```
Build → Build Server  (Ctrl+Shift+F7)
 
Build/
├── Windows_x64/
│   └── MyGame.exe            — клиентская часть
└── Server_x64/
    ├── MyGameServer.exe      — выделенный сервер (без DX11 и звука)
    ├── VoxelCubeServer.dll   — серверный рантайм
    └── server.cfg            — конфиг сервера
```
 
### Конфигурация сервера (`server.cfg`)
 
```ini
[Server]
name        = My VoxelCube Server
port        = 7777
max_players = 64
tickrate    = 64
 
[World]
seed        = 12345
auto_save   = 300
 
[Security]
password    = ""
whitelist   = false
anti_cheat  = true
 
[Network]
compression = true
encryption  = false
```
 
### Сетевое API — Authoritative Server
 
```cpp
// C++ — репликация переменных и RPC
class Player : public vc::Entity {
    VCNET_REPLICATED(float,    health,   100.0f)
    VCNET_REPLICATED(vc::Vec3, position)
 
    VCNET_SERVER_RPC(void, TakeDamage, float damage) {
        health -= damage;
    }
 
    VCNET_MULTICAST_RPC(void, PlayHitEffect) {
        GetComponent<ParticleSystem>().Emit("hit_sparks");
    }
};
```
---
 
## 🏗️ Архитектура
 
```
┌───────────────────────────────────────────────────────────────┐
│                     VoxelCube Editor                          │
│    Viewport │ File System │ Properties │ Build System         │
├───────────────────────────────────────────────────────────────┤
│                    Game / Script Layer                        │
│               C++ Game Code  │  C++ Scripts                    │
├──────────────┬───────────────┬──────────────┬─────────────────┤
│  World (MGS) │  Renderer     │  Physics     │  Audio          │
│              │  (DX11)       │  (PhysX 5)   │  (OpenAL)       │
├──────────────┴───────────────┴──────────────┴─────────────────┤
│               ECS Core (EnTT / fallback registry)             │
├───────────────────────────────────────────────────────────────┤
│                  Platform / OS Abstraction                    │
│         Window (Win32) │ Input │ FileSystem │ Threading       │
└───────────────────────────────────────────────────────────────┘
```
 
### Поток кадра (Frame Pipeline)
 
```
1.  Input Collection
2.  C++ Scripts Update
3.  C++ Game Logic Update
4.  Physics Simulation      (PhysX substeps)
5.  VoxelCluster Mesh Rebuild (async worker threads)
6.  Audio 3D Update         (OpenAL)
7.  DirectX 11 Render:
    a. Depth Pre-Pass
    b. Shadow Maps
    c. G-Buffer  (Deferred Shading)
    d. Lighting Pass
    e. Transparent Voxels
    f. Post-Processing  (SSAO, Bloom, TAA)
    g. UI / HUD  (ImGui)
8.  Present (DXGI SwapChain)
```
 
---
 
## 📦 Требования
 
### Для разработки (сборка движка)
 
| Компонент | Требование |
|---|---|
| ОС | Windows 10 версия 2004+ (сборка 19041) |
| Компилятор | MSVC 2022 (v143), C++20 |
| CMake | ≥ 3.25 |
| GPU | DirectX 11 |
| VRAM | ≥ 4 GB (≥ 8 GB рекомендуется) |
| RAM | ≥ 16 GB |
 
---
 
## 🔧 Установка и сборка движка
 
### 1. Клонирование
 
```bash
git clone --recurse-submodules https://github.com/your-username/VoxelCube.git
cd VoxelCube
```
 
### 2. Текущее состояние bootstrap

На текущем этапе репозиторий собирается как минимальный engine bootstrap без внешних runtime-зависимостей. `spdlog` остаётся опциональным: если пакет доступен через CMake/vcpkg, движок подключит его автоматически; иначе используется встроенный console/file backend.

### 3. Сборка

```bash
cmake --preset default
cmake --build --preset default
```

### 4. Запуск sandbox

```bash
./build/Debug/VoxelCubeSandbox.exe
```

Для sandbox логирование сейчас настроено в `sandbox/sandbox.vcconfig` и пишет в `logs/sandbox/voxelcube_sandbox.log`; там же задаются параметры окна, тайминга и `threading.workerCount` для `JobSystem`.
 
---
 
## 📁 Структура проекта
 
```
VoxelCube/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
│
├── engine/                     # Ядро движка (C++)
│   ├── include/VoxelCube/      # Публичные заголовки
│   └── src/
│       ├── core/               # Engine, Loop, Config, FileSystem, Events
│       ├── world/              # MGS, Cluster, VoxelRegistry, WorldGen
│       ├── renderer/           # DX11 Device/SwapChain/RTV/Sync/Buffer/Shader/Pipeline/Geometry bootstrap, Shaders, Materials
│       ├── physics/            # PhysX 5 интеграция
│       ├── audio/              # OpenAL, AudioSource, EFX
│       ├── ecs/                # Registry, TransformComponent, CameraComponent, SystemScheduler
│       ├── network/            # Server, Client, Replication
│       └── scripting/          # c++ scripting
│
├── editor/                     # VoxelCube Editor (C++ / ImGui)
│   ├── src/
│   │   ├── panels/             # Viewport, Outliner, Properties, FS Browser
│   │   ├── tools/              # VoxelPainter, TerrainTool и др.
│   │   └── build_system/       # Компилятор, сборщик игр и серверов
│   └── resources/              # Иконки, темы, шейдеры UI
│
├── runtime/                    # Минимальный рантайм для готовых игр
│
├── shaders/                    # HLSL шейдеры (DX11)
│   ├── voxel_gbuffer.hlsl
│   ├── lighting.hlsl
│   ├── raytracing.hlsl
│   └── postprocess/
│
├── scripts/                    # Утилиты сборки (PowerShell / C++)
│
├── sandbox/                    # Тестовый проект на движке
│   ├── sandbox.vcconfig        # Конфиг запуска sandbox-приложения
│   ├── game/                   # C++ логика
│   ├── scripts/                # C++ скрипты
│   └── assets/
│
├── tests/                      # Unit / Integration тесты
│
└── docs/
    ├── getting-started.md
    ├── api-cpp.md
    ├── api-csharp.md
    ├── editor-guide.md
    ├── mgs-system.md
    ├── multiplayer.md
    └── build-system.md
```
 
---
 
## 🗺️ Дорожная карта
 
### v0.1.0 — Core Engine *(в разработке)*
- [x] CMake bootstrap, `Application`, `Window`, `Input`, `Config`, `FileSystem`, `EventBus`, `JobSystem`, `Memory`/arena/pool allocators, `Math/Vec3`, `ECS Registry/SystemScheduler`, `TransformComponent`, `CameraComponent` с auto-detect `EnTT` и fallback backend, настраиваемый логгер с уровнями и file sink
- [ ] DirectX 11 рендерер (device + immediate context + swapchain + render targets + context sync + buffer map/unmap + shader compile + pipeline state + geometry upload/indexed draw + DDS texture loading + texture atlas готовы, material path в работе)
- [ ] Merged Geometry System (MGS) — прототип
- [ ] PhysX 5 интеграция (Rigid Body)
- [ ] OpenAL 3D звук
- [ ] ECS gameplay-layer: higher-level gameplay systems, scene hierarchy
- [ ] Базовый редактор (Viewport + Outliner)
- [ ] File System Browser в редакторе
 
### v0.2.0 — Editor & Build
- [ ] Полный редактор в UE-стиле (все панели)
- [ ] VoxelPainter и TerrainTool
- [ ] Build System для Win64
- [ ] Упаковка ресурсов в `.vcpak`
- [ ] Hot Reload шейдеров и C++-скриптов
 
### v0.3.0 — Multiplayer
- [ ] Authoritative Server архитектура
- [ ] Автогенерация серверной сборки
- [ ] Replication System (VCNET макросы)
- [ ] Lobby и Matchmaking API
 
### v0.4.0 — Advanced Rendering
- [ ] DXR Ray Tracing (тени, GI)
- [ ] Mesh Shaders для вокселей
- [ ] Volumetric Fog и облака
- [ ] LOD для кластеров
 
### v1.0.0 — Release
- [ ] Stable Public API
- [ ] Полная документация
- [ ] Примеры проектов (Survival, Sandbox, Shooter)
- [ ] Steam Workshop интеграция
 
---

 
## 📄 Лицензия
 
Движок распространяется под лицензией **MIT**. Подробности в [LICENSE](LICENSE).
 
**Зависимости с отдельными лицензиями:**
- NVIDIA PhysX 5 — BSD 3-Clause
- OpenAL Soft — LGPL v2
- Dear ImGui — MIT
 
---
 
<div align="center">
 
**VoxelCube Engine** — Построй свой мир, кластер за кластером.
 
[⭐ Star](https://github.com/your-username/VoxelCube) · [🐛 Issues](https://github.com/your-username/VoxelCube/issues) · [📖 Docs](https://voxelcube.dev/docs) · [💬 Discord](https://discord.gg/voxelcube)
 
</div>
