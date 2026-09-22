<div align="center">

![INU Check Logo](logo.jpg)

# INU_Check (GTA SA)

**🔎 Оффлайн-проверка папки GTA SA/VC/III — находит то, что уронит игру, ещё до запуска.**

<p>
  <img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D6?logo=windows" alt="Windows">
  <img src="https://img.shields.io/badge/Game-GTA%20SA%20%C2%B7%20VC%20%C2%B7%20III-orange" alt="Games">
  <img src="https://img.shields.io/badge/Version-1.0-green" alt="Version">
  <img src="https://img.shields.io/badge/Rules-~500%20codes-blue" alt="Rules">
  <img src="https://img.shields.io/badge/License-GPL--3.0-blue" alt="License">
</p>

**[🇬🇧 English version](../README.md)** · **[📘 Руководство](MANUAL_rus.md)** · **[📖 Каталог правил](re/GTACHECK_RULES.md)**

</div>

---

## ✨ Главное

<table>
<tr>
<td width="50%" valign="top">

- 🎮 **Проверяет как игра** — проходит папку в том же порядке, что `gta_sa.exe` при старте: gta.dat, IMG, IDE/IPL, DFF/TXD/COL/IFP, handling, оружие, педы, одежда, катсцены, GXT, main.scm, аудио.
- 🧠 **~500 кодов правил** из реверса загрузчиков 1.0 US — у каждого кода есть функция и конкретная непроверенная операция, которая ломает игру.
- 🤫 **Ноль шума на ванили** — чистая SA 1.0 US даёт 0 крашей; всё, что есть и у ванили, по умолчанию скрыто.
- 💬 **Пояснения простым языком** — «что это / чем грозит / что делать» под каждой строкой, плюс справочник кодов и клавиш.
- 🛠️ **Умеет чинить** — имена фреймов DFF, пересохранение TXD (D3D8→D3D9, палитры, мипы, степень двойки, DXT без альфы, мёртвые текстуры), дальность и альфа-флаг в IDE, правка текста; с бэкапом, **откатом** или в отдельную папку для modloader.
- 👀 **Встроенные просмотрщики** — текстовый редактор на нужной строке, 3D-превью модели с коллизией и выбором полигона, редактор TXD (в том числе PAL8/D3D8), 2D-карта с тепловыми слоями, 3D-карта мира из IPL с 14 режимами раскраски.
- 🧩 **Понимает modloader** — loose-файлы, IDE/IPL по имени, строки из readme модов; fastman92 limit adjuster распознаётся.
- 📦 **Качество сборки** (`QLT`) — дальность против радиуса, тяжёлые TXD, объекты под водой, перегруженные сектора, дубли расстановки, лишние файлы в архивах, **диета TXD** одной кнопкой.
- 🗺️ **Деление карты по районам** — большой мод режется на IPL/IDE/COL/LOD-TXD по сетке с лимитами объектов.
- 🖥️ **Окно + консоль** — та же проверка из скрипта с отчётом TXT / CSV / JSON / HTML и кодом возврата.
- 🌍 Интерфейс **RU / EN / ES**, 9 цветовых схем, масштаб UI.

</td>
<td width="50%" valign="top">

<!-- ![Главное окно](screenshot.png) -->

</td>
</tr>
</table>

→ **[Что нового](../../../releases/latest)** · [История версий](../../../releases)

## 🔬 Что проверяется

| Область | Файлы | Семейства |
|---|---|:---:|
| 📂 **Загрузка данных** | `gta.dat` / `default.dat`, каталоги IMG (VER2, III/VC `.dir`), IDE (все секции), IPL (текст + streamed bnry), LOD-связи, `water.dat`, `timecyc.dat`, `plants.dat`, `object.dat`, `effects.fxp`, `nodes*.dat`, `procobj.dat` | `DAT` `IDE` `IPL` `IMG` |
| 🧊 **Модели** | DFF — структура RW, BinMesh, 2DFX, night colours, UV-анимация, breakable, скин педов, встроенная коллизия машин | `DFF` |
| 🖼️ **Текстуры** | TXD — растры, форматы, цепочки мипов, D3D8 / PAL8, UI-TXD, тайлы радара | `TXD` `UI` |
| 🧱 **Коллизия** | COL 1–4 — face-группы, тени, границы, число сфер/боксов | `COL` |
| 🏃 **Анимация** | IFP — ANPK / ANP2 / ANP3, времена, квантование, `animgrp.dat` | `IFP` |
| 🚗 **Машины** | фреймы / extras / dummy против `ms_vehicleDescs`, `handling.cfg`, `carcols.dat`, `carmods.dat`, `cargrp.dat`, `vehicles.ide`, `vehicle.txd`, remap-TXD, audio-id | `VEH` `HND` |
| 🔫 **Оружие и педы** | `weapon.dat`, `ped.dat`, `pedstats.dat`, `pedgrp.dat`, `popcycle.dat`, `peds.ide`, `decision\*`, `surface*.dat`, `shopping.dat`, `statdisp.dat`, `ar_stats.dat` | `WPN` `PDD` `PED` |
| 👕 **Одежда CJ** | `player.img`, `clothes.dat`, клампы normal / fat / ripped, базовые TXD | `CLO` |
| 🎬 **Катсцены** | `anim\cuts.img` (.cut / .ifp / .dat), special-модели `cutscene.img` | `CUT` |
| 📝 **Текст и скрипт** | `text\*.gxt`, `main.scm` / `script.img`, `stream.ini`, `fonts.dat` | `GXT` `SCM` |
| 🔊 **Аудио** | `audio\CONFIG\*.dat`, SFX-банки, стрим-паки, `TrakLkup`, аудио машин | `AUD` |
| 🔗 **Перекрёстные ссылки** | модель ↔ DFF ↔ TXD (+ txdp / vehicle) ↔ COL ↔ IPL, текстуры материалов ↔ TXD, лимиты пулов | — |
| ⚖️ **Качество сборки** | дальность прорисовки, вес TXD, объекты под водой, плотность секторов, gxt-имена, дубли в модах и расстановке, флаги прозрачности, мёртвые текстуры, лишние файлы | `QLT` |

Четыре уровня: 💥 **Краш** (падение / зависание / LOAD-FAIL) · ❌ **Ошибка** (видимый мусор, порча памяти) ·
⚠️ **Предупреждение** (подозрительно, игра молча игнорирует) · ℹ️ **Инфо**. Полный каталог с адресами функций —
в **[каталоге правил](re/GTACHECK_RULES.md)**.

## 🧰 Что есть в окне

- **Таблица проблем** — группировка по коду, фильтры по уровню / категории / файлу / IMG / моду, поиск (Ctrl+F), сортировка, «исправимые / новые / мод», список игнора (`gta_check_ignore.txt`).
- **Описание строки** — уровень, код (клик → справочник), «Открыть файл | Копировать | Открыть папку», «Исправить: авто | вручную…», пояснение правила.
- **Редактор текста** — IDE/IPL/DAT/CFG на месте: номера строк с точками уровней, F3 по проблемам, Ctrl+S с бэкапом, ПКМ → показать на карте / в 3D / где используется.
- **Превью модели** — DFF из IMG с текстурами и коллизией (сферы/боксы/грани), выбор полигона кликом, орбита/сдвиг/зум.
- **Редактор TXD** — список текстур с форматами, превью, добавить / удалить / перетащить, «Пересохранить…», «В папку…».
- **Карта** — тайлы радара из IMG, маркеры проблем, тепловые слои, «Снимок» в PNG, деление по районам с превью клеток.
- **3D-карта мира** — весь мир из IPL вокруг камеры, LOD как в игре, клик по модели = табличка (id, IPL:строка, DFF/TXD КБ, COL, LOD, флаги), слои **DFF | COL | LOD | TXD**, день/ночь, нагрузка стриминга в точке камеры, 14 режимов раскраски (проблемы, вес, дальность, полигоны, IPL, архив, ваниль/мод, LOD, коллизия, плотность, флаги IPL, прозрачность, TXD в стриминге, районы).
- **Лишнее** — файлы архивов, на которые никто не ссылается, с размером и «Копировать список».
- **Где используется** — поиск имени модели/текстуры по всем IDE/IPL/DAT/скрипту.
- **Слежение за папкой** — изменился файл игры → предложение перепроверить; повторная проверка показывает «исправлено / новых».
- **Настройки** — язык, схема цветов (студия, игра, спокойная, Blender, панель, неон-CRT, графит, индиго, океан), шрифт (10 вшитых), масштаб 100/125/150 %, папка вывода, диета TXD, подсказки.
- **Справочник** — вкладки «Коды правил» и «Клавиши и мышь».

## 📥 Установка

Скачай `gta_check.exe` из [последнего релиза](../../../releases/latest). Установка и зависимости не нужны.

**Окно:** положи `gta_check.exe` в корень игры (рядом с `gta_sa.exe` / `data\gta.dat`) и запусти —
проверка начнётся сама. Или передай папку: `gta_check.exe "D:\Grand Theft Auto San Andreas"`.

**Консоль:**

```
gta_check.exe --cli "D:\Grand Theft Auto San Andreas" --txt report.txt
```

> 🔒 Программа не трогает файлы игры без твоего действия. Рядом с exe создаётся `gta_check.ini`, а в папке
> игры — только по запросу: `gta_check_report.*` (экспорт), `gta_check_backup\` (оригиналы + журнал отката
> `undo.txt`), `gta_check_ignore.txt` (список «игнорировать»), `gta_check_out\` / `modloader\gta_check_fix\`
> (исправленные файлы), `gta_check_txd_diet\` (диета TXD), `gta_check_shot_N.png` (снимки).

<details>
<summary>⌨️ Все ключи командной строки</summary>

```
gta_check.exe --cli <папка игры> [ключи]

  --txt f | --csv f | --json f | --html f   отчёт в файл (HTML — с пояснениями правил)
  --lang ru|en|es                           язык сообщений
  --info                                    включить уровень «Инфо»
  --show-vanilla                            показывать и то, что есть у чистой игры
  --no-modloader                            не учитывать modloader\
  --limit-adjuster                          превышения пулов считать заметками
  --mod-only                                не проверять содержимое ванильных IMG
  --no-dff --no-txd --no-col --no-ifp --no-data   отключить семейства проверок
  --fix-all [--out <папка>]                 применить авто-исправления (в игру с бэкапом или в папку)
  --undo                                    откатить последнее исправление из журнала
  --district N [--district-min M]           разделить карту на N×N районов (IPL/IDE/COL/LOD-TXD)
      [--district-what ipl,ide,col,txd,one] [--district-log f]
  --windows-to-lod <модель> [--dry-run]     перенести светящиеся окна модели (яркие ночные цвета) на её LOD
      [--w2l-opt brightness=170,grow=1,growmin=2,growdot=0.999,island=0.999,rotate=1,gain=0.5,skip=1,skipdist=0.5,flatten=1,snap=1,offset=0.1,maxsnap=5]
  --baseline-dump f                         дамп для базиса ванили (make_baseline.py)
  --lang-missing f                          непереведённые строки (для разработки)

gta_check.exe --fix-dff <in.dff> <out.dff>            починить имена фреймов одного файла
gta_check.exe --txd-fix <in.txd> <out.txd> [--ppm]    пересохранить TXD (печатает формат каждой текстуры)
```

Код возврата: `0` — крашей нет, `1` — есть краши (без скрытых ванильных), `2` — неверные аргументы или
файл не читается.

</details>

## 🧪 Совместимость

| | |
|---|---|
| 🎮 **Игра** | GTA San Andreas 1.0 US (откалибровано); **Vice City и III — экспериментально** |
| 🧩 **Моды** | modloader, fastman92 limit adjuster, папки MTA:SA |
| 🖥️ **ОС** | Windows x64 (Direct3D 9) |
| 🔧 **Сборка** | Visual Studio 2022+, MSVC, C++17 — других зависимостей нет |

<details>
<summary>Поддержка III / VC — подробности (экспериментально)</summary>

Игра определяется по `data\gta.dat` / `gta_vc.dat` / `gta3.dat`; палитра окна следует ей (SA — тёплый
чёрный + оранжевый, VC — ночной фиолетовый + неон, III — синий Либерти-Сити + золото), карта берёт нужную
сетку тайлов радара (12×12 или 8×8).

Что уже сделано по исходникам re3 / reVC:

- свои базисы ванили — чистые III и VC дают **0 крашей**;
- форматы: VER1 `.dir` IMG, COLL/COL2, ANPK, DFF RW 3.1/3.2 (расширение клампа за чанком), TXD D3D8 / PAL8, `txd.img`;
- лимиты движков III/VC (пулы, зоны, 2dfx, IFP, timecyc), `handling.cfg` со вшитыми именами, педы без скина, `waterpro.dat`;
- 3D-карта: видимость LOD по правилам игры (`SetupBigBuilding`, `FindRelatedModel`), островные LOD, свет и декали как в игре, `generic.txd`.

Что ещё не откалибровано: `weapon.dat`, GXT (III без TABL), `carcols`, `object.dat`, cull-зоны, `particle.cfg` —
эти семейства для III/VC пропускаются. Возможны ложные срабатывания; сообщайте о них в issue с кодом правила.

</details>

<details>
<summary>🏗️ Сборка из исходников</summary>

```
build.bat            → bin\win-amd64-d3d9\Release\gta_check.exe
build.bat debug      → отладочная сборка (/Zi /Od)
```

Путь к Visual Studio задан в первой строке `build.bat` (`set VS=…`) — поправь под свою установку.
librw, Dear ImGui 1.92.2b, nanosvg и шрифты лежат в репозитории, `rw.lib` уже собран. Иконка и версия exe —
`src/gtacheck.rc` (иконку делает `src/gen_icon.py` из `src/icon.png`).

`librw\` — форк [librw](https://github.com/aap/librw) (ветка southland) **с локальными правками**, которых
нет в апстриме: защита от NULL из `CreateTexture`, проверка результата `Reset()` устройства, исправленный
двойной free в `Clump::streamRead` при обрезанном DFF, модификаторы клавиш в `imgui_impl_rw.cpp`, окно без
системного заголовка и кадры во время перетаскивания в `skeleton/win.cpp`. После изменения исходников librw
пересобери `rw.lib` из VS Developer Shell:

```
msbuild librw\build\librw.vcxproj "/p:Configuration=Release win-amd64-d3d9" /p:Platform=x64 /m:1
```

</details>

<details>
<summary>📁 Структура репозитория и добавление правила</summary>

```
src/              исходники (C++17)
  main.cpp          окно, панели, карта, 3D, CLI, crash-reporter
  runner.cpp        порядок фаз проверки, перекрёстные ссылки
  gamedata.cpp      gta.dat, IMG, IDE/IPL, правила DAT-*
  check_*.cpp       правила по семействам (txd, dff, veh, col, ifp, handling, weapon, ped,
                    clothes, cuts, text, audio, misc, quality)
  txd_edit.cpp      собственный кодек TXD (разбор, декод, ремонт, DXT-энкодер, запись бит-в-бит)
  fix.cpp           исправления DFF/TXD/IDE, запись с бэкапом, папка вывода, журнал отката
  district.cpp      деление карты по районам
  win2lod.cpp       окна → LOD: перенос светящихся окон на LOD-модель
  rule_help.cpp     пояснения к кодам правил простым языком
  lang.cpp/.tsv     переводы RU/EN/ES (make_lang.py sync|merge|build)
  vanilla_*.h       базисы чистых SA / III / VC (make_baseline.py)
  tables_*.h        таблицы, снятые с exe (педы, аудио)
  icons_blender.h   иконки Blender (gen_icons.py), nanosvg/ — растеризация SVG
librw/            форк librw + skeleton + Dear ImGui (с правками)
docs/re/          результаты реверса загрузчиков gta_sa.exe 1.0 US и каталог правил
build.bat         сборка
```

1. Сообщение пишется **по-русски** через `Context::add(...)` в нужном `check_*.cpp`; по русскому тексту
   считается ключ ванильного базиса и ищется перевод.
2. Пояснение — строка `{код, что это, чем грозит, что делать}` в `rule_help.cpp`.
3. Переводы: `python make_lang.py sync` → перевести `lang_keys.txt` → `python make_lang.py merge <файл> en|es`
   → `python make_lang.py build`. Проверка: `--cli <папка> --lang en --info --lang-missing f` → `0`.
4. Если правило срабатывает на чистой игре, перегенерируй базис:
   `--cli <ваниль> --no-modloader --info --baseline-dump dump.txt` → `python make_baseline.py dump.txt [iii|vc]` → сборка.

</details>

## ⚠️ Известные ограничения

- 3D-превью педов статично (skin / hanim не подключены); родительские TXD (`txdp`) в превью не подгружаются.
- Список игнора в консольном режиме не применяется; экспорт TXT без пояснений правил (в HTML они есть).
- Деление по районам для III/VC — только IPL и IDE (COL и LOD-TXD пропускаются); текстовые IPL в бинарный стрим-IPL не переводятся.
- При падении программы рядом появляется `gta_check_crash.txt` со стеком — приложи его к issue.

## 🔗 Ссылки

- 📖 [Каталог правил](re/GTACHECK_RULES.md) · [Заметки реверса](re/)
- 🧰 [INU_Tools](https://github.com/INU-ez/INU_Tools-GTA-Blender) — аддон Blender для моддинга GTA SA от того же автора

## 🙏 Благодарности

- **[librw](https://github.com/aap/librw)** (aap, MIT) — реимплементация RenderWare: чтение DFF/TXD, рендер D3D9, каркас окна.
- **[Dear ImGui](https://github.com/ocornut/imgui)** (MIT) — интерфейс.
- **[euryopa](https://github.com/aap/euryopa)** (aap) и **[re3 / reVC](https://github.com/Jai-JAP/re-GTA)** — ориентир для расстановки IPL, LOD и видимости в 3D-виде.
- **[Blender](https://www.blender.org/)** — иконки интерфейса (GPL-2.0-or-later); **[nanosvg](https://github.com/memononen/nanosvg)** (zlib) — их растеризация.
- Шрифты **Russo One**, **Inter**, **Roboto**, **IBM Plex Sans**, **Manrope**, **Rubik**, **Exo 2**, **Play**, **JetBrains Mono** — SIL Open Font License 1.1.

**Автор:** INU (Discord `1.n.u` · [сервер](https://discord.gg/sqtGAVTGdy))

**Лицензия:** [GPL-3.0](../LICENSE)
