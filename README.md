<div align="center">

![INU Check Logo](docs/logo.jpg)

# INU_Check (GTA SA)

**🔎 Offline checker for a GTA SA/VC/III folder — finds what will crash the game before you launch it.**

<p>
  <img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D6?logo=windows" alt="Windows">
  <img src="https://img.shields.io/badge/Game-GTA%20SA%20%C2%B7%20VC%20%C2%B7%20III-orange" alt="Games">
  <img src="https://img.shields.io/badge/Version-1.0-green" alt="Version">
  <img src="https://img.shields.io/badge/Rules-~500%20codes-blue" alt="Rules">
  <img src="https://img.shields.io/badge/License-GPL--3.0-blue" alt="License">
</p>

**[🇷🇺 Русская версия](docs/README_rus.md)** · **[📖 Rule catalogue](docs/re/GTACHECK_RULES.md)**

</div>

---

## ✨ Highlights

<table>
<tr>
<td width="50%" valign="top">

- 🎮 **Checks like the game does** — walks the folder in the same order as `gta_sa.exe` at startup: gta.dat, IMG, IDE/IPL, DFF/TXD/COL/IFP, handling, weapons, peds, clothes, cutscenes, GXT, main.scm, audio.
- 🧠 **~500 rule codes** from reverse-engineering the 1.0 US loaders — every code names the function and the unchecked operation that breaks the game.
- 🤫 **Zero noise on vanilla** — a clean SA 1.0 US gives 0 crashes; everything vanilla also has is hidden by default.
- 💬 **Plain-language help** — "what it is / what it breaks / what to do" under every row, plus a reference of codes and shortcuts.
- 🛠️ **Fixes it can apply** — DFF frame names, TXD re-save (D3D8→D3D9, palettes, mips, power of two, DXT without alpha, dead textures), draw distance and alpha flag in IDE, text edits; with backup, **undo**, or into a modloader-ready folder.
- 👀 **Built-in viewers** — text editor on the offending line, 3D model preview with collision and polygon picking, TXD editor (PAL8/D3D8 too), 2D map with heat layers, 3D world view from IPLs with 14 colouring modes.
- 🧩 **Modloader-aware** — loose files, IDE/IPL by name, lines from mod readmes; fastman92 limit adjuster detected.
- 📦 **Build quality** (`QLT`) — draw distance vs. radius, heavy TXDs, objects under water, overloaded sectors, duplicate placements, unused archive files, **TXD diet** in one click.
- 🗺️ **Map splitting into districts** — a big mod is cut into IPL/IDE/COL/LOD-TXD by a grid with object limits.
- 🖥️ **GUI + CLI** — the same check from a script with TXT / CSV / JSON / HTML reports and an exit code.
- 🌍 **EN / RU / ES** interface, 9 colour schemes, UI scale.

</td>
<td width="50%" valign="top">

<!-- ![Main window](docs/screenshot.png) -->

</td>
</tr>
</table>

→ **[What's new](../../releases/latest)** · [Version history](../../releases)

## 🔬 What is checked

| Area | Files | Rule families |
|---|---|:---:|
| 📂 **Data loading** | `gta.dat` / `default.dat`, IMG directories (VER2, III/VC `.dir`), IDE (all sections), IPL (text + streamed binary), LOD links, `water.dat`, `timecyc.dat`, `plants.dat`, `object.dat`, `effects.fxp`, `nodes*.dat`, `procobj.dat` | `DAT` `IDE` `IPL` `IMG` |
| 🧊 **Models** | DFF — RW structure, BinMesh, 2DFX, night colours, UV animation, breakable, ped skin, embedded vehicle collision | `DFF` |
| 🖼️ **Textures** | TXD — rasters, formats, mip chains, D3D8 / PAL8, UI TXDs, radar tiles | `TXD` `UI` |
| 🧱 **Collision** | COL 1–4 — face groups, shadows, bounds, sphere/box counts | `COL` |
| 🏃 **Animation** | IFP — ANPK / ANP2 / ANP3, timing, quantisation, `animgrp.dat` | `IFP` |
| 🚗 **Vehicles** | frames / extras / dummies vs. `ms_vehicleDescs`, `handling.cfg`, `carcols.dat`, `carmods.dat`, `cargrp.dat`, `vehicles.ide`, `vehicle.txd`, remap TXDs, audio ids | `VEH` `HND` |
| 🔫 **Weapons & peds** | `weapon.dat`, `ped.dat`, `pedstats.dat`, `pedgrp.dat`, `popcycle.dat`, `peds.ide`, `decision\*`, `surface*.dat`, `shopping.dat`, `statdisp.dat`, `ar_stats.dat` | `WPN` `PDD` `PED` |
| 👕 **CJ clothes** | `player.img`, `clothes.dat`, normal / fat / ripped clumps, base TXDs | `CLO` |
| 🎬 **Cutscenes** | `anim\cuts.img` (.cut / .ifp / .dat), special models in `cutscene.img` | `CUT` |
| 📝 **Text & script** | `text\*.gxt`, `main.scm` / `script.img`, `stream.ini`, `fonts.dat` | `GXT` `SCM` |
| 🔊 **Audio** | `audio\CONFIG\*.dat`, SFX banks, stream packs, `TrakLkup`, vehicle audio | `AUD` |
| 🔗 **Cross-references** | model ↔ DFF ↔ TXD (+ txdp / vehicle) ↔ COL ↔ IPL, material textures ↔ TXD, pool limits | — |
| ⚖️ **Build quality** | draw distance, TXD weight, under-water objects, sector density, GXT names, duplicate mod files and placements, alpha flags, dead textures, unused files | `QLT` |

Four severities: 💥 **Crash** (crash / hang / LOAD-FAIL) · ❌ **Error** (visible garbage, memory corruption) ·
⚠️ **Warning** (suspicious, silently ignored) · ℹ️ **Info**. Full catalogue with function addresses in the
**[rule catalogue](docs/re/GTACHECK_RULES.md)** (Russian).

## 🧰 What is in the window

- **Issue table** — grouped by code, filters by severity / category / file / IMG / mod, search (Ctrl+F), sorting, "fixable / new / mod", ignore list (`gta_check_ignore.txt`).
- **Row details** — severity, code (click → reference), "Open file | Copy | Open folder", "Fix: auto | manual…", rule explanation.
- **Text editor** — IDE/IPL/DAT/CFG in place: line numbers with severity dots, F3 through problems, Ctrl+S with backup, right-click → show on map / in 3D / where used.
- **Model preview** — DFF from IMG with textures and collision (spheres/boxes/faces), polygon picking by click, orbit/pan/zoom.
- **TXD editor** — texture list with formats, preview, add / remove / drag, "Re-save…", "To folder…".
- **Map** — radar tiles from IMG, issue markers, heat layers, "Screenshot" to PNG, district splitting with cell preview.
- **3D world view** — the whole world from IPLs around the camera, LOD as in the game, click on a model = info card (id, IPL:line, DFF/TXD KB, COL, LOD, flags), layers **DFF | COL | LOD | TXD**, day/night, streaming load at the camera point, 14 colouring modes (problems, weight, draw distance, polygons, IPL, archive, vanilla/mod, LOD, collision, density, IPL flags, alpha, TXD in streaming, districts).
- **Unused** — archive files nothing references, with sizes and "Copy list".
- **Where used** — search a model/texture name across all IDE/IPL/DAT/script.
- **Folder watch** — a game file changed → offer to re-check; the re-check shows "fixed / new".
- **Settings** — language, colour scheme (studio, game, calm, Blender, panel, neon CRT, graphite, indigo, ocean), font (10 embedded), scale 100/125/150 %, output folder, TXD diet, tooltips.
- **Reference** — "Rule codes" and "Keys & mouse" tabs.

## 📥 Installation

Download `gta_check.exe` from the [latest release](../../releases/latest). No installer, no dependencies.

**GUI:** drop `gta_check.exe` into the game root (next to `gta_sa.exe` / `data\gta.dat`) and run it — the check
starts by itself. Or pass the folder: `gta_check.exe "D:\Grand Theft Auto San Andreas"`.

**CLI:**

```
gta_check.exe --cli "D:\Grand Theft Auto San Andreas" --txt report.txt --lang en
```

> 🔒 The tool never touches game files without your action. It creates `gta_check.ini` next to the exe and, only
> when asked, in the game folder: `gta_check_report.*` (export), `gta_check_backup\` (originals + undo journal
> `undo.txt`), `gta_check_ignore.txt` (ignore list), `gta_check_out\` / `modloader\gta_check_fix\` (fixed files),
> `gta_check_txd_diet\` (TXD diet), `gta_check_shot_N.png` (screenshots).

<details>
<summary>⌨️ All command-line options</summary>

```
gta_check.exe --cli <game folder> [options]

  --txt f | --csv f | --json f | --html f   write a report (HTML includes rule explanations)
  --lang ru|en|es                           message language
  --info                                    include the Info level
  --show-vanilla                            also show what a clean game has
  --no-modloader                            ignore modloader\
  --limit-adjuster                          treat pool overflows as notes
  --mod-only                                skip the contents of vanilla IMGs
  --no-dff --no-txd --no-col --no-ifp --no-data   disable check families
  --fix-all [--out <folder>]                apply automatic fixes (in place with backup, or into a folder)
  --undo                                    revert the last fix from the journal
  --district N [--district-min M]           split the map into N×N districts (IPL/IDE/COL/LOD-TXD)
      [--district-what ipl,ide,col,txd,one] [--district-log f]
  --baseline-dump f                         dump for the vanilla baseline (make_baseline.py)
  --lang-missing f                          untranslated strings (development)

gta_check.exe --fix-dff <in.dff> <out.dff>            fix frame names of a single file
gta_check.exe --txd-fix <in.txd> <out.txd> [--ppm]    re-save a TXD (prints the format of every texture)
```

Exit code: `0` — no crashes, `1` — crashes found (hidden vanilla ones excluded), `2` — bad arguments or
unreadable file.

</details>

## 🧪 Compatibility

| | |
|---|---|
| 🎮 **Game** | GTA San Andreas 1.0 US (calibrated); **Vice City and III — experimental** |
| 🧩 **Mods** | modloader, fastman92 limit adjuster, MTA:SA folders |
| 🖥️ **OS** | Windows x64 (Direct3D 9) |
| 🔧 **Build** | Visual Studio 2022+, MSVC, C++17 — no other dependencies |

<details>
<summary>III / VC support — details (experimental)</summary>

The game is detected from `data\gta.dat` / `gta_vc.dat` / `gta3.dat`; the window palette follows it
(SA — warm black + orange, VC — night purple + neon, III — Liberty City blue + gold), the map reads the
right radar tile grid (12×12 or 8×8).

Already done, based on the re3 / reVC sources:

- own vanilla baselines — clean III and VC give **0 crashes**;
- formats: VER1 `.dir` IMGs, COLL/COL2, ANPK, RW 3.1/3.2 DFFs (clump extension past the chunk), D3D8 / PAL8 TXDs, `txd.img`;
- III/VC engine limits (pools, zones, 2dfx, IFP, timecyc), `handling.cfg` with names baked into the exe, peds without skin, `waterpro.dat`;
- 3D view: LOD visibility by the game's rules (`SetupBigBuilding`, `FindRelatedModel`), island LODs, lighting and decals as in the game, `generic.txd`.

Not calibrated yet: `weapon.dat`, GXT (III has no TABL), `carcols`, `object.dat`, cull zones, `particle.cfg` —
these families are skipped for III/VC. False positives are possible; please report them in an issue with the rule code.

</details>

<details>
<summary>🏗️ Building from source</summary>

```
build.bat            → bin\win-amd64-d3d9\Release\gta_check.exe
build.bat debug      → debug build (/Zi /Od)
```

The Visual Studio path is on the first line of `build.bat` (`set VS=…`) — adjust it to your install.
librw, Dear ImGui 1.92.2b, nanosvg and the fonts are in the repository; `rw.lib` is prebuilt. The exe icon and
version live in `src/gtacheck.rc` (`src/gen_icon.py` builds the icon from `src/icon.png`).

`librw\` is a fork of [librw](https://github.com/aap/librw) (southland branch) **with local patches** not in
upstream: NULL guard on `CreateTexture`, checked device `Reset()` result, a fixed double free in
`Clump::streamRead` on truncated DFFs, modifier keys in `imgui_impl_rw.cpp`, a window without the system caption
and frames during drag in `skeleton/win.cpp`. After changing librw sources rebuild `rw.lib` from a VS Developer Shell:

```
msbuild librw\build\librw.vcxproj "/p:Configuration=Release win-amd64-d3d9" /p:Platform=x64 /m:1
```

</details>

<details>
<summary>📁 Repository layout & adding a rule</summary>

```
src/              sources (C++17)
  main.cpp          window, panels, map, 3D view, CLI, crash reporter
  runner.cpp        check phase order, cross-references
  gamedata.cpp      gta.dat, IMG, IDE/IPL, DAT-* rules
  check_*.cpp       rules by family (txd, dff, veh, col, ifp, handling, weapon, ped,
                    clothes, cuts, text, audio, misc, quality)
  txd_edit.cpp      own TXD codec (parse, decode, repair, DXT encoder, bit-exact write)
  fix.cpp           DFF/TXD/IDE fixes, writes with backup, output folder, undo journal
  district.cpp      map splitting into districts
  rule_help.cpp     plain-language explanations for rule codes
  lang.cpp/.tsv     RU/EN/ES translations (make_lang.py sync|merge|build)
  vanilla_*.h       baselines of clean SA / III / VC (make_baseline.py)
  tables_*.h        tables lifted from the exe (peds, audio)
  icons_blender.h   Blender icons (gen_icons.py), nanosvg/ — SVG rasterisation
librw/            librw fork + skeleton + Dear ImGui (patched)
docs/re/          reverse-engineering notes on the gta_sa.exe 1.0 US loaders and the rule catalogue
build.bat         build script
```

1. Messages are written **in Russian** via `Context::add(...)` in the relevant `check_*.cpp`; the Russian
   text is the key for both the vanilla baseline and the translation lookup.
2. Explanation — a `{code, what it is, what it breaks, what to do}` line in `rule_help.cpp`.
3. Translations: `python make_lang.py sync` → translate `lang_keys.txt` → `python make_lang.py merge <file> en|es`
   → `python make_lang.py build`. Verify: `--cli <folder> --lang en --info --lang-missing f` → `0`.
4. If the rule fires on a clean game, regenerate the baseline:
   `--cli <vanilla> --no-modloader --info --baseline-dump dump.txt` → `python make_baseline.py dump.txt [iii|vc]` → build.

</details>

## ⚠️ Known limitations

- Ped preview is static (skin / hanim not hooked up); parent TXDs (`txdp`) are not loaded in the preview.
- The ignore list is not applied in CLI mode; the TXT export has no rule explanations (HTML has them).
- District splitting for III/VC covers IPL and IDE only (COL and LOD-TXD are skipped); text IPLs are not converted to binary streamed IPLs.
- If the tool crashes it writes `gta_check_crash.txt` with a stack trace next to itself — attach it to your issue.

## 🔗 Links

- 📖 [Rule catalogue](docs/re/GTACHECK_RULES.md) · [Reverse-engineering notes](docs/re/)
- 🧰 [INU_Tools](https://github.com/INU-ez/INU_Tools-GTA-Blender) — Blender addon for GTA SA modding by the same author

## 🙏 Credits

- **[librw](https://github.com/aap/librw)** (aap, MIT) — RenderWare re-implementation: DFF/TXD loading, D3D9 rendering, window skeleton.
- **[Dear ImGui](https://github.com/ocornut/imgui)** (MIT) — user interface.
- **[euryopa](https://github.com/aap/euryopa)** (aap) and **[re3 / reVC](https://github.com/Jai-JAP/re-GTA)** — reference for IPL placement, LOD and visibility in the 3D view.
- **[Blender](https://www.blender.org/)** — UI icons (GPL-2.0-or-later); **[nanosvg](https://github.com/memononen/nanosvg)** (zlib) — their rasterisation.
- Fonts **Russo One**, **Inter**, **Roboto**, **IBM Plex Sans**, **Manrope**, **Rubik**, **Exo 2**, **Play**, **JetBrains Mono** — SIL Open Font License 1.1.

**Author:** INU (Discord `1.n.u` · [server](https://discord.gg/sqtGAVTGdy))

**License:** [GPL-3.0](LICENSE)
