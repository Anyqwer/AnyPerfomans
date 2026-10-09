# AnyPerfomans ⚡

**AnyPerfomans** — высокоточный, ультра-легковесный системный профилировщик и монитор производительности нового поколения (C++20, DirectX 11, Dear ImGui, ImPlot). 

Разработан как альтернатива тяжеловесным мониторам (MSI Afterburner / RTSS) со специальным фокусом на **замер реального оверхеда и влияния фонового ПО, модов и утилит на игру** (например, Counter-Strike 2).

---

## 🚀 Ключевые возможности

1. **Non-Invasive Architecture (Zero-Hook)**:
   - Не внедряет DLL в процесс игры и не перехватывает функции рендеринга внутри памяти игры.
   - Полная безопасность от систем античита (VAC Trusted Mode, Vanguard, EAC).

2. **Per-Process & Per-Thread CPU Attribution**:
   - Микросекундный замер реальных тактов процессора (`QueryProcessCycleTime` / `QueryThreadCycleTime`).
   - Наглядный расчет чистой «цены» фоновой утилиты относительно самой игры: `Overhead: +X.X% CPU`.

3. **Анализ памяти (Commit Charge / Private Working Set)**:
   - Точный учет приватной памяти процесса через `PSAPI` (без разделяемых системных DLL).

4. **Глубокий Thread Inspector**:
   - Анализ всех активных потоков игры в реальном времени.
   - Определение адреса запуска потока (`NtQueryInformationThread`) и сопоставление с модулями DLL (`client.dll`, `engine2.dll` или `[Unknown/Injected]`).

5. **A/B Impact Benchmarking**:
   - Модуль автоматического замера сессий с расчетом падения 1% Low FPS и средней задержки кадров при включении стороннего софта.

6. **Гибридный интерфейс**:
   - **Full Dashboard**: полноценное окно аналитика с интерактивными графиками `ImPlot`.
   - **Mini HUD (HotKey `F11`)**: безрамочный прозрачный клик-сквозной оверлей (`WS_EX_TRANSPARENT | WS_EX_TOPMOST`) поверх игры.

7. **Сверхнизкое потребление**:
   - Нативный C++20 бинарник размером **~600 КБ**.
   - Потребление памяти: **< 25 МБ RAM**.
   - Нагрузка на CPU: **< 0.1%**.

---

## 🛠 Архитектура проекта

```text
AnyPerfomans/
├── CMakeLists.txt              # Сборка C++20, DirectX 11, WinAPI
├── build.bat                   # Автоматический скрипт компиляции под Visual Studio
├── include/anyperf/
│   ├── types.hpp               # Базовые структуры телеметрии и кадров
│   ├── ring_buffer.hpp         # Lock-free кольцевой буфер для графиков ImPlot
│   ├── process_monitor.hpp     # Монитор процессов: CPU такты, Private RAM
│   ├── thread_profiler.hpp     # Анализ потоков, адресов и модулей
│   ├── dx11_backend.hpp        # Бэкенд DirectX 11 и управление оверлеем
│   └── dashboard_view.hpp      # Интерфейс ImGui/ImPlot, графики и карточки
└── src/
    ├── core/                   # Реализация мониторинга процессов и потоков
    ├── render/                 # Рендерер DX11
    ├── ui/                     # Логика UI дашборда и мини-HUD
    └── main.cpp                # Точка входа
```

---

## 📦 Сборка и запуск

### Требования:
- Windows 10/11 x64
- Visual Studio 2022 / 2026 с поддержкой C++ Desktop Development (MSVC v143+)
- CMake 3.24+ (встроен в Visual Studio)

### Быстрая сборка:
Просто запустите скрипт сборки в корне проекта:
```cmd
build.bat
```

Скомпилированный файл появится в `build\Release\AnyPerfomans.exe`.
