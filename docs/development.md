# Сборка и проверки

Первый запуск и установка Python-зависимостей — в [README](../README.md).
Устройство программы описано в [описании компонентов](algorithms.md#структура).

<a id="build"></a>

## Сборка

Требуются CMake 3.20+ и C++20. nlohmann/json 3.12.0 включён в `third_party/`;
CMake проверяет SHA-256. [Сведения о зависимости](../third_party/README.md).

| Скрипт из корня проекта | Действие |
|---|---|
| `./build.sh [config.json] [output_directory]` | Release и расчёт; по умолчанию конфигурация и выход в корне проекта |
| `./build.sh --debug` | Только сборка Debug с ASan/UBSan |
| `./build.sh --test` | Debug с ASan/UBSan, предупреждения как ошибки, CTest |
| `.\build.ps1 -Mode release` | Release и расчёт через MSVC |
| `.\build.ps1 -Mode debug` / `test` | Только сборка Debug / Debug и CTest; без санитайзеров |

PowerShell принимает `-ConfigPath`, `-OutputDirectory`, `-Python`;
по умолчанию конфигурация и выход находятся в корне проекта, Python — `python`.
Форматирование задаёт `.clang-format`, параметры сборки — `CMakeLists.txt`.
Скрипты сборки не форматируют исходники.

Для VS Code C/C++ задайте `C_Cpp.default.cppStandard` равным `c++20`, а
`C_Cpp.default.compileCommands` — `${workspaceFolder}/build/release/compile_commands.json`.
Локальная папка `.vscode/` исключена из публикации. Файл `.clangd` указывает
clangd на ту же базу команд CMake со стандартом, путями заголовков и определениями.

Сборка без обёртки с полной матрицей тестов (Python из установленного окружения):

```bash
cmake -S . -B build/ci -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCOLLATZ_VISUAL_TESTS=ON -DCOLLATZ_WARNINGS_AS_ERRORS=ON \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/ci --config Release --parallel
ctest --test-dir build/ci -C Release --output-on-failure
```

На Windows передайте путь к `.venv\Scripts\python.exe`; у генератора Visual Studio
приложение находится в `build/ci/Release/collatz_run.exe`. В PowerShell для
переноса команд используется обратная кавычка вместо `\`.

| Опция CMake | По умолчанию / назначение |
|---|---|
| `BUILD_TESTING` | ON; без Python остаются C++-тесты |
| `COLLATZ_VISUAL_TESTS` | OFF; ON требует Python и `requirements.txt`, добавляет графики с Agg |
| `COLLATZ_WARNINGS_AS_ERRORS` | OFF; ON включает `-Werror` или `/WX` |
| `COLLATZ_LTO` | ON; Release использует LTO, если компилятор поддерживает |
| `COLLATZ_SANITIZERS` | OFF; ASan/UBSan для Clang/GCC |
| `COLLATZ_TSAN` | OFF; отдельная сборка TSan для Clang/GCC |

Санитайзеры не совмещаются; автоматическое LTO для них отключено.
Для сборки без тестов и требования Python задайте `-DBUILD_TESTING=OFF`.
Прямые Python-зависимости закреплены в `requirements.txt`, транзитивные — нет.

## Проверки

```bash
./build.sh --test
cmake -S . -B build/tsan -DCMAKE_BUILD_TYPE=Debug -DCOLLATZ_TSAN=ON -DCOLLATZ_WARNINGS_AS_ERRORS=ON
cmake --build build/tsan --parallel
ctest --test-dir build/tsan --output-on-failure
```

Тесты сверяют CSV и сводку с независимыми полными траекториями на Python,
проверяют основания 2…36, границы целых, предел шагов, кэш, семейства стартов,
потоки и восстановление после ошибок. Отдельно проверяются публикация наборов,
паспорт, параллельные процессы и выбор файлов. Графики проверяются без GUI,
в том числе без разрешения читать CSV. Публичные C++-заголовки компилируются отдельно.

## CI и фактическое состояние

[Workflow](../.github/workflows/ci.yml) настроен для Ubuntu 24.04 (GCC/Clang),
macOS 15 (Apple Clang), Windows 2022 (MSVC), ASan/UBSan на Linux и TSan на macOS.
Release-задания запускают графики и экспорт пяти PNG с Python 3.14.
Права workflow — `contents: read`; шагов публикации нет.
`.gitattributes` сохраняет байты vendored JSON без преобразования переводов строк.

Выполненные локальные проверки:

| Среда | Результат CTest | Дата, 2026 |
|---|---|---|
| macOS arm64, Apple Clang 21, Release с LTO, Python 3.14.5 | 12/12, включая графики | 10 ноября |
| macOS arm64, Apple Clang 21, ASan/UBSan | 11/11 | 10 ноября |
| macOS arm64, GCC 16.2, Release с LTO | 11/11 | 8 ноября |
| macOS arm64, TSan | 11/11 | 8 ноября |

10 ноября Release собран из чистой копии 119 файлов для публикации;
ASan/UBSan проверен повторно в рабочем каталоге. Быстрый пример из README
проверен в чистой копии: созданы паспорт, данные и пять PNG.
Версии установленных Python-зависимостей совпадают с `requirements.txt`.
Ранее проверен и запуск на миллионе стартов.
Linux arm64 Debian 12 с GCC 12.2 и Clang 14 проверялся ранее, до последующих
оптимизаций и наборов результатов. Текущая версия на Linux и Windows локально
не проверялась; MSVC и PowerShell также не запускались. Конфигурация CI
не подтверждает прохождение GitHub Actions: результат проверяется после размещения.

После выделения модулей экспорта, загрузки данных и элементов управления
Release и ASan/UBSan проверены повторно. CSV, сводка и траектории побайтово
совпали с прежней версией в пяти сценариях; изображения 11 вариантов графиков
совпали по пикселям.

Восемь проверок визуализации включают строгое неравенство в выборочной функции
распределения, повторяющиеся значения и нормировку гистограммы, JSON- и CSV-пути,
кэширование чтений, возврат `Figure` без показа окна и работу элементов управления.
