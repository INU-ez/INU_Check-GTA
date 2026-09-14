<div align="center">

<!-- ![INU Check Logo](docs/logo.jpg) -->

# INU_Check (GTA SA)

**Offline checker for a GTA San Andreas install — finds what crashes the game before you launch it.**

<p>
  <img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D6?logo=windows" alt="Windows">
  <img src="https://img.shields.io/badge/Game-GTA%20SA%20%C2%B7%20VC%20%C2%B7%20III-orange" alt="Games">
  <img src="https://img.shields.io/badge/Version-1.0-green" alt="Version">
  <img src="https://img.shields.io/badge/Rules-~450%20codes-blue" alt="Rules">
</p>

**[🇷🇺 Русская версия](docs/README_rus.md)** · **[📖 Rule catalogue](docs/re/GTACHECK_RULES.md)**

</div>

---

## Highlights

<table>
<tr>
<td width="50%" valign="top">

- **Checks like the game does** — walks the folder in the same order as `gta_sa.exe` at startup: gta.dat, IMG, IDE/IPL, DFF/TXD/COL/IFP, handling, weapons, peds, clothes, cutscenes, GXT, main.scm, audio.
- **~450 rule codes** from reverse-engineering the 1.0 US loaders — every code names the function and the unchecked operation that breaks the game.
- **Zero noise on vanilla** — a clean SA 1.0 US gives 0 crashes; everything vanilla also has is hidden by default.
- **Plain-language help** — "what it is / what it breaks / what to do" under every row, plus a code reference.
- **Fixes it can apply** — DFF frame names, TXD re-save (D3D8→D3D9, palettes, mips, pow2), text edits; with backup, **undo**, or into a modloader-ready folder.
- **Built-in viewers** — text editor on the offending line, 3D model preview with collision, TXD viewer (PAL8/D3D8 too), 2D map with heat layers, 3D world view from IPLs.
- **Modloader-aware** — loose files, IDE/IPL by name, lines from mod readmes; fastman92 limit adjuster detected.
- **Build quality rules** (`QLT`) — draw distance vs. radius, heavy TXDs, objects under water, overloaded sectors, unused archive files.
- **GUI + CLI** — the same check from a script with TXT / CSV / JSON / HTML reports and an exit code.
- **EN / RU / ES** interface.

</td>
<td width="50%" valign="top">

<!-- ![Main window](docs/screenshot.png) -->

</td>
</tr>
</table>

→ **[What's new](../../releases/latest)** · [Version history](../../releases)

## What is checked

| Area | Files | Rule families |
|---|---|:---:|
| **Data loading** | `gta.dat` / `default.dat`, IMG directories (VER2, III/VC `.dir`), IDE (all sections), IPL (text + streamed binary), LOD links, `water.dat`, `timecyc.dat`, `plants.dat`, `object.dat`, `effects.fxp`, `nodes*.dat`, `procobj.dat` | `DAT` `IDE` `IPL` `IMG` |
| **Models** | DFF — RW structure, BinMesh, 2DFX, night colours, UV animation, breakable, ped skin, embedded vehicle collision | `DFF` |
| **Textures** | TXD — rasters, formats, mip chains, D3D8 / PAL8, UI TXDs, radar tiles | `TXD` `UI` |
| **Collision** | COL 1–4 — face groups, shadows, bounds, sphere/box counts | `COL` |
| **Animation** | IFP — ANPK / ANP2 / ANP3, timing, quantisation, `animgrp.dat` | `IFP` |
| **Vehicles** | frames / extras / dummies vs. `ms_vehicleDescs`, `handling.cfg`, `carcols.dat`, `carmods.dat`, `cargrp.dat`, `vehicles.ide`, `vehicle.txd`, remap TXDs, audio ids | `VEH` `HND` |
| **Weapons & peds** | `weapon.dat`, `ped.dat`, `pedstats.dat`, `pedgrp.dat`, `popcycle.dat`, `peds.ide`, `decision\*`, `surface*.dat`, `shopping.dat`, `statdisp.dat`, `ar_stats.dat` | `WPN` `PDD` |
| **CJ clothes** | `player.img`, `clothes.dat`, normal / fat / ripped clumps, base TXDs | `CLO` |
| **Cutscenes** | `anim\cuts.img` (.cut / .ifp / .dat), special models in `cutscene.img` | `CUT` |
| **Text & script** | `text\*.gxt`, `main.scm` / `script.img`, `stream.ini`, `fonts.dat` | `GXT` `SCM` |
| **Audio** | `audio\CONFIG\*.dat`, SFX banks, stream packs, `TrakLkup`, vehicle audio | `AUD` |
| **Cross-references** | model ↔ DFF ↔ TXD (+ txdp / vehicle) ↔ COL ↔ IPL, material textures ↔ TXD, pool limits | — |
| **Build quality** | draw distance, TXD weight, under-water objects, sector density, GXT names, duplicate mod files, unused files | `QLT` |

Four severities: **Crash** (crash / hang / LOAD-FAIL) · **Error** (visible garbage, memory corruption) ·
**Warning** (suspicious, silently ignored) · **Info**. Full catalogue with function addresses in the
**[rule catalogue](docs/re/GTACHECK_RULES.md)** (Russian).

## Installation

Download `gtacheck.exe` from the [latest release](../../releases/latest). No installer, no dependencies.

**GUI:** drop `gtacheck.exe` into the game root (next to `gta_sa.exe` / `data\gta.dat`) and run it — the check
starts by itself. Or pass the folder: `gtacheck.exe "D:\Grand Theft Auto San Andreas"`.

**CLI:**

```
gtacheck.exe --cli "D:\Grand Theft Auto San Andreas" --txt report.txt --lang en
```

> The tool never touches game files without your action. It creates `gtacheck.ini` next to the exe and, only
> when asked, `gtacheck_report.*` (export), `gtacheck_backup\` (originals + undo journal) and
> `gtacheck_ignore.txt` (ignore list) in the game folder.

<details>
<summary>All command-line options</summary>

```
gtacheck.exe --cli <game folder> [options]

  --txt f | --csv f | --json f | --html f   write a report (HTML includes rule explanations)
  --lang ru|en|es                           message language
  --info                                    include the Info level
  --show-vanilla                            also show what a clean SA 1.0 US has
  --no-modloader                            ignore modloader\
  --limit-adjuster                          treat pool overflows as notes
  --mod-only                                skip the contents of vanilla IMGs
  --no-dff --no-txd --no-col --no-ifp --no-data   disable check families
  --fix-all [--out <folder>]                apply automatic fixes (in place with backup, or into a folder)
  --undo                                    revert the last fix from the journal

gtacheck.exe --fix-dff <in.dff> <out.dff>            fix frame names of a single file
gtacheck.exe --txd-fix <in.txd> <out.txd> [--ppm]    re-save a TXD (prints the format of every texture)
```

Exit code: `0` — no crashes, `1` — crashes found (hidden vanilla ones excluded), `2` — bad arguments or
unreadable file.

</details>

## Compatibility

| | |
|---|---|
| **Game** | GTA San Andreas 1.0 US (calibrated); Vice City and III — experimental |
| **Mods** | modloader, fastman92 limit adjuster, MTA:SA folders |
| **OS** | Windows x64 (Direct3D 9) |
| **Build** | Visual Studio 2022+, MSVC, C++17 — no other dependencies |

<details>
<summary>Multi-game support (III / VC / SA) — details</summary>

The game is detected from `data\gta.dat` / `gta_vc.dat` / `gta3.dat`; the window palette follows it
(SA — warm black + orange, VC — night purple + neon, III — Liberty City blue + gold), the map reads the
right radar tile grid (12×12 or 8×8).

III and VC go through the same rules: file structures (VER1 `.dir` IMGs, COLL/COL2, ANPK, RW 3.1/3.2 DFFs,
D3D8 / PAL8 TXDs) parse correctly, but many SA thresholds do not apply to them yet — expect false positives
until III/VC calibration is done.

</details>

<details>
<summary>Building from source</summary>

```
build.bat            → bin\win-amd64-d3d9\Release\gtacheck.exe
build.bat debug      → debug build (/Zi /Od)
```

The Visual Studio path is on the first line of `build.bat` (`set VS=…`) — adjust it to your install.
librw, Dear ImGui 1.92.2b and the fonts are in the repository; `rw.lib` is prebuilt.

`librw\` is a fork of [librw](https://github.com/aap/librw) (southland branch) **with local patches** not in
upstream: NULL guard on `CreateTexture`, checked device `Reset()` result, a fixed double free in
`Clump::streamRead` on truncated DFFs, modifier keys in `imgui_impl_rw.cpp`. After changing librw sources
rebuild `rw.lib` from a VS Developer Shell:

```
msbuild librw\build\librw.vcxproj "/p:Configuration=Release win-amd64-d3d9" /p:Platform=x64 /m:1
```

</details>

<details>
<summary>Repository layout & adding a rule</summary>

```
src/              gtacheck sources (C++17)
  main.cpp          window, panels, map, 3D view, CLI, crash reporter
  runner.cpp        check phase order, cross-references
  gamedata.cpp      gta.dat, IMG, IDE/IPL, DAT-* rules
  check_*.cpp       rules by family (txd, dff, veh, col, ifp, handling, weapon, ped,
                    clothes, cuts, text, audio, misc, quality)
  txd_edit.cpp      own TXD codec (parse, decode, repair, DXT encoder, bit-exact write)
  fix.cpp           DFF fixes, writes with backup, output folder, undo journal
  rule_help.cpp     plain-language explanations for rule codes
  lang.cpp/.tsv     RU/EN/ES translations (make_lang.py sync|merge|build)
  vanilla_sa.h      baseline of a clean SA 1.0 US (make_baseline.py)
  tables_*.h        tables lifted from the exe (peds, audio)
librw/            librw fork + skeleton + Dear ImGui (patched)
docs/re/          reverse-engineering notes on the gta_sa.exe 1.0 US loaders and the rule catalogue
build.bat         build script
```

1. Messages are written **in Russian** via `Context::add(...)` in the relevant `check_*.cpp`; the Russian
   text is the key for both the vanilla baseline and the translation lookup.
2. Explanation — a `{code, what it is, what it breaks, what to do}` line in `rule_help.cpp`.
3. Translations: `python make_lang.py sync` → translate `lang_keys.txt` → `python make_lang.py merge <file> en|es`
   → `python make_lang.py build`. Verify: `--cli <folder> --lang en --info --lang-missing f` → `0`.
4. If the rule fires on a clean SA, regenerate the baseline:
   `--cli <vanilla> --no-modloader --info --baseline-dump dump.txt` → `python make_baseline.py dump.txt` → build.

</details>

## Known limitations

- Ped preview is static (skin / hanim not hooked up); parent TXDs (`txdp`) are not loaded in the preview.
- If the tool crashes it writes `gtacheck_crash.txt` with a stack trace next to itself — attach it to your issue.

## Links

- 📖 [Rule catalogue](docs/re/GTACHECK_RULES.md) · [Reverse-engineering notes](docs/re/)
- 🧰 [INU_Tools](https://github.com/INU-ez/INU_Tools-GTA-Blender) — Blender addon for GTA SA modding by the same author

## Credits

- **[librw](https://github.com/aap/librw)** (aap, MIT) — RenderWare re-implementation: DFF/TXD loading, D3D9 rendering, window skeleton.
- **[Dear ImGui](https://github.com/ocornut/imgui)** (MIT) and **[ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)** (MIT) — user interface.
- **[euryopa](https://github.com/aap/euryopa)** (aap) — reference for IPL placement and LOD handling in the 3D view.
- Fonts **Russo One**, **Inter**, **JetBrains Mono** — SIL Open Font License 1.1.

**Author:** INU (Discord `1.n.u` · [server](https://discord.gg/sqtGAVTGdy))

**License:** see [LICENSE](LICENSE)
