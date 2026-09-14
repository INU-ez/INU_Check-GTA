# GXT text / main.scm + script.img / IMG streaming order / UI files — gta_sa.exe 1.0 US

Sources: Ghidra dumps `E:\RE\asm_data2\<ADDR>.c/.asm` (plus `asm\`, `asm_map\`) for every function named in
the task; capstone listings (scratchpad `calls.py`/`csd2.py`) for the bodies that Ghidra could not follow
(SecuROM trampolines `jmp 0x156xxxx` / `jmp 0x40xxxx`): `CText::Load` main 0x6A01A0, `CText::Get` 0x6A0050,
`CText::Search` 0x6A0000, `CKeyGen::GetUppercaseKey` 0x53CF30, `CTheScripts::Init` body 0x468D5D..,
`ReadObjectNamesFromScript` body 0x156F5A0, `ReadMultiScriptFileOffsetsFromScript` body 0x1565E20,
`UpdateObjectIndices` body 0x1562660, `LoadAndLaunchMissionInternal` 0x489980, `CStreamedScripts::LoadStreamedScript`
body 0x1565EC0, `CStreamingInfo::GetCdPosnAndSize` body 0x1560E50, `CDirectory::AddItem` 0x532310,
`CGame::Init1` 0x5BF840 / `Init2` 0x5BA1A0 / `InitialiseCoreDataAfterRW` 0x5BFA90 / `InitialiseRenderWare` 0x5BD600,
`CFont::LoadFontValues` 0x7187C0, `CHud::Initialise` 0x5BA850, `CTxdStore::LoadTxd` 0x7320B0 / 0x731DD0,
`CLoadingScreen::LoadSplashes` 0x5900B0, `CMenuManager::LoadAllTextures` 0x572EC0, `CRadar::DrawRadarSection`
0x586110, `CStreaming::RequestModel` 0x4087E0, CRT `fread` 0x823521 / `_lock_str` 0x823FA2.
Raw bytes via `readva.py`; name/pointer tables read straight from the exe (`ptrtab.py`).
gta-reversed / plugin-sdk were used only for names and array sizes, each size re-derived from exe address
arithmetic (stated per item). Vanilla data: `D:\Grand Theft Auto San Andreas` (text\*.gxt, data\script\main.scm,
models\*.img, models\*.txd, data\fonts.dat, stream.ini) — every rule below was run against it (§6).

Check ids: `GXT-nn`, `SCM-nn`, `IMG-nn`, `UI-nn`. Severity legend as in gtacheck: **Crash** = access violation /
infinite loop / memory corruption; **Error** = engine proceeds with visibly wrong data; **Warning** = silently
ignored / suspicious; **Info**.

---------------------------------------------------------------------------------------------------

## 0. Boot order — which file is read when (and what a mod can *not* override)

All addresses verified by call/jmp xrefs in the exe (`xref.py`), not taken from gta-reversed.

| step | function | files read | notes |
|---|---|---|---|
| 0 | ASI/DllMain (dinput8 proxies) | – | before everything below |
| 1 | `CGame::InitialiseOnceBeforeRW` 0x53BB50 | – | `CMemoryMgr::Init`, `CLocalisation::Initialise`, `CFileMgr::Initialise`, `CdStreamInit(5)`, `CPad::Initialise` |
| 2 | RW init → `CGame::InitialiseRenderWare` 0x5BD600 | **MODELS\FONTS.TXD + DATA\FONTS.DAT** (`CFont::Initialise` 0x5BA690 @0x5BD76A), **MODELS\PCBTNS.TXD** (same), **MODELS\HUD.TXD** (`CHud::Initialise` 0x5BA850 @0x5BD76F) | also `CTxdStore::Initialise`, `CPlayerSkin::Initialise`, `CPostEffects::Initialise` |
| 3 | WinMain state machine 0x748C23.. | **MODELS\TXD\LOADSCS.TXD** (`CLoadingScreen::Init` 0x5902B0 → `LoadSplashes` 0x5900B0) | after the title movies |
| 4 | `CGame::InitialiseEssentialsAfterRW` 0x5BA160 @0x748C30 | **TEXT\<LANG>.GXT** (`CText::Load` 0x6A01A0) | then `CCarFXRenderer::Initialise`, `CGrassRenderer::Initialise`, `CCustomBuildingRenderer::Initialise`, `CTimer::Initialise` |
| 5 | `CGame::InitialiseCoreDataAfterRW` 0x5BFA90 @0x748C3F | handling.cfg (`cHandlingDataMgr::LoadHandlingData` 0x5BD830), surface*.dat (`SurfaceInfos_c::Init` 0x55F420), pedstats.dat (`CPedStats::Initialise` 0x5BF9D0 → `LoadPedStats` 0x5BB890) + decision makers (`LoadDefaultDecisionMaker` 0x5BF400), **timecyc.dat** (`CTimeCycle::Initialise` 0x5BBAC0), popcycle.dat (`CPopCycle::Initialise` 0x5BC090), audio config (`CAudioEngine::Initialise` 0x5B9C60), interior tables | happens **before the main menu** — none of these are re-read on "new game" (`CTimeCycle::Initialise` is re-run only from 0x53BD6B = `CGame::ReInitGameObjectVariables`) |
| 6 | main menu (`CMenuManager::LoadAllTextures` 0x572EC0) | **MODELS\FRONTEN1.TXD, FRONTEN2.TXD, FRONTEN3.TXD, FRONTEN_pc.TXD** | on menu open |
| 7 | "new game" → `InitialiseGame` 0x53E580 → `CGame::Initialise("DATA\GTA.DAT")` 0x53BC80 | | |
| 7a | `CGame::Init1` 0x5BF840 | gta3.ini (`CIniFile::LoadIniFile` 0x56D070), **MODELS\PARTICLE.TXD**, plants.dat + **models\grass\plant1.txd** (`CPlantMgr::Initialise` 0x5DD910), animgrp.dat (`CAnimManager::Initialise` 0x5BF6B0 → `ReadAnimAssociationDefinitions` 0x5BC910), `CCutsceneMgr::Initialise` 0x4D5A20, `CModelInfo::Initialise`, **gta3.img + gta_int.img opened** (`CStreaming::InitImageList` 0x4083C0 @0x5BF991), **stream.ini** (`CStreaming::ReadIniFile` 0x5BCCD0 @0x5BF996), melee.dat (`LoadMeleeData` 0x5BEDC0), effects.fxp + effectsPC.txd (`Fx_c::Init` 0x49EA90 → `LoadFxProject` 0x5C2420) | |
| 7b | `CColAccel::startCache` 0x5B31A0; `CFileLoader::LoadLevel("DATA\DEFAULT.DAT")` then `("DATA\GTA.DAT")` 0x5B9030 | IDE / COLFILE / TEXDICTION / MODELFILE / **IMG lines → `CStreaming::AddImageToList` 0x407610 @0x5B915B** / SPLASH / IPL | **first IPL line** (0x5B924E..0x5B9294): `MatchAllModelStrings` 0x5B57C0, object.dat (`CObjectData::Initialise` 0x5B5360), carcols.dat + carmods.dat + **MODELS\GENERIC\VEHICLE.TXD** (`CVehicleModelInfo::SetupCommonData` 0x5B8F00), **`CStreaming::Init2` 0x5B8AD0 (body dumped as `CStreaming::Init` 0x5B9020): `CStreamedScripts::Initialise`, `LoadCdDirectory()` 0x5B82C0 = directory scan of every IMG registered so far, streaming buffer allocation, `ms_memoryAvailable=0x3200000`, `desiredNumberOfVehiclesLoaded=22`**, `CColStore::LoadAllBoundingBoxes` 0x4113D0 |
| 7c | `CGame::Init2` 0x5BA1A0 | player skin (`CPlayerInfo::LoadPlayerSkin`), **MODELS\PLAYER.IMG registered as the last IMG** + clothes (`CClothes::Init` 0x5A80D0 @0x5BA215), water.dat (`WaterLevelInitialise` 0x6EAE80), initial vehicles/peds/weapons streamed, ped.ifp etc. (`CAnimManager::LoadAnimFiles` 0x4D5620), ped.dat (`CPed::Initialise` 0x5DEBB0 → `CPedType::Initialise` 0x608E40 → `LoadPedData` 0x608B30), **radar tiles bound** (`CRadar::Initialise` 0x587FB0 @0x5BA2C5, `CRadar::LoadTextures` 0x5827D0 @0x5BA2CA), weapon.dat (`CWeapon::InitialiseWeapons` 0x73A300 → `CWeaponInfo::Initialise` 0x5BF750), pedgrp.dat + cargrp.dat (`CPopulation::Initialise` 0x610E10), **data\script\main.scm + script.img header cross-ref** (`CTheScripts::Init` 0x468D50 @0x5BA340) | |
| 7d | `CStencilShadows::Init`, `CTheScripts::StartTestScript` 0x464D40, `CTheScripts::Process`, `CCamera::Process`, `CGame::Init3` 0x5BA400 | main script starts executing at ScriptSpace+0 | |

Consequences for a checker / for "what a mod cannot override":

* Anything in steps 2–6 is loaded **once, before the menu**: fonts, hud, loading screens, GXT, handling,
  surface, pedstats, timecyc, popcycle, audio config, frontend TXDs. (GXT is re-loaded on language change
  and by `CTheScripts::Init` for mission packs — 3 callers of 0x6A01A0.)
* IMG directories are scanned **exactly once**, at the first `IPL` line of gta.dat (`Init2` is a one-shot,
  see DAT-04 in `textdata_path.md`). IMG lines after that line, and `player.img`, are never scanned by
  `LoadCdDirectory` (IMG-02). `player.img` is only ever read through `CClothesBuilder`'s own `CDirectory`.
* `stream.ini` is read in `Init1` and its `memory`/`vehicles` values are **overwritten** in `Init2`
  (0x5B9020: `DAT_008a5a80 = 0x3200000; DAT_008a5a84 = 0x16`, executed after `LoadCdDirectory`) — IMG-12.
* main.scm is read in `Init2` after every IDE has been parsed, so the object-name table can be resolved
  against the final model list (SCM-04); `script.img`'s directory has already been scanned at that point,
  so `CStreamedScripts::m_nCountOfScripts` is final when `ReadStreamedScriptData` runs (SCM-06).

---------------------------------------------------------------------------------------------------

## 1. GXT — `CText`

### 1.1 Object layout (from the dumps; sizes by address arithmetic)

`TheText` @0xC1B340, size 0xA90 (0xC1B340+0xA90 = 0xC1BDD0 = the 256-byte conversion buffer used by 0x69F7E0).

| offset | field | evidence |
|---|---|---|
| +0x00 | `CKeyArray m_MainKeyArray` {entries*, count} | `CText::Load` 0x6A030A passes `ecx=esi` to 0x69F490 |
| +0x08 | `CData m_MainText` {data*, size} | 0x6A0341 `lea ecx,[esi+8]` → 0x69F5D0 |
| +0x10 / +0x18 | mission key array / mission data | `LoadMissionText` 0x69FBF0 frees +0x10/+0x18 |
| +0x20 | language char `'e','f','g','i','s'` | `GetUpperCase` 0x69F750 `switch(*(this+0x20))` |
| +0x21 | TABL loaded | set at 0x6A02DB after a TABL chunk |
| +0x22 | CDERROR loaded | 0x6A03DF |
| +0x23 | mission text loaded | 0x69FBF0 / 0x69F9A0 |
| +0x24 | mission table name [8] | 0x69FBD0 |
| +0x2C | CDERROR text [256] | 0x6A03C7 copy loop |
| +0x12C | mission table offsets, **200 × 12 bytes** {name[8], u32 offset} | 0x6A02CC `lea ecx,[esi+0x12C]`; count stored at +0x12C+0x960 (0x69F670 `param_1+0x960`) → 0x960/12 = **200** |
| +0xA8C | int16 number of tables | `LoadMissionText` `*(short*)(this+0xa8c)` |

### 1.2 Main file load — `CText::Load` 0x6A01A0

```
SetDir("TEXT"); name = {AMERICAN,FRENCH,GERMAN,ITALIAN,SPANISH}.GXT by FrontEndMenuManager.m_nLanguage (0xBA67CC)
f = OpenFile(name,"rb")                 ; result NOT checked (0x6A0228 → edi used directly)
Read(f,2); Read(f,2)                    ; 4-byte version header (04 00 08 00) consumed, never compared
bytesRead = 4
loop until (gotTKEY && gotTDAT):        ; 0x6A0255
    read 8 header bytes one at a time  ; a failed Read just leaves the previous header bytes in place (0x6A0280 → 0x6A0298)
    size = header[4..8]; if size == 0 → continue
    "TABL" → CMissionTextOffsets::Load(this+0x12C, size, f)   (0x69F670), TABL flag
    "TKEY" → CKeyArray::Load(size, f)   (0x69F490: count = size>>3, new(count*8), read count*8 bytes), gotTKEY
    "TDAT" → CData::Load(size, f)       (0x69F5D0: new(size), read size bytes), gotTDAT
    else   → skip `size` bytes one Read at a time, Read results ignored
CKeyArray::Update(mainData)             ; 0x6A0387: every entry.offset += data base pointer, NO range check
CloseFile; m_szCdErrorText = convert(Get("CDERROR")); SetDir("")
```

* **Missing file → crash** (GXT-01): `CFileMgr::OpenFile` 0x538900 is `fopen` (→0x8232D8); `CFileMgr::Read`
  0x538950 is `fread(buf,1,n,FILE*)` (→0x823521) which calls `_lock_str` 0x823FA2: for a pointer outside the
  `_iob` table it does `EnterCriticalSection((char*)stream+0x20)` (0x823FC6) → NULL+0x20 → access violation.
* **EOF is not an exit condition** (GXT-03): the only way out of the chunk loop is having seen both TKEY and
  TDAT. A file with no MAIN TKEY/TDAT, a TKEY/TDAT with size 0, or one truncated inside TKEY/TDAT spins
  forever (with a stale "TKEY"/"TDAT" header it re-allocates `size` bytes per iteration → memory exhaustion).
* Header bytes 0..3 are consumed blindly (GXT-02): a III/VC-style file that starts with `TABL`/`TKEY` has
  its first four bytes eaten and the chunk walk starts mis-aligned → the loop skips garbage sizes → hang.
* Chunk alignment (GXT-04): `CKeyArray::Load` consumes exactly `(size>>3)*8` bytes and
  `CMissionTextOffsets::Load` exactly `(size/12)*12`; a size that is not a multiple leaves the file
  position inside the chunk → the next 8-byte header is garbage → hang or garbage tables.
* TABL is **optional** for MAIN; MAIN's offset may point directly at `TKEY` (vanilla does: TABL ends at
  1536 and MAIN's offset is 1536) or at an 8-byte `MAIN\0\0\0\0` block — the latter is tolerated only
  because bytes 4..7 of that block are 0 and `size==0` makes the loop re-read.
* TABL capacity (GXT-07): 0x69F670 writes `this+0x12C + i*12` for **every** `size/12` entry with no bound;
  entry 200 lands on the count field (+0xA8C) and entries ≥ 201 on 0xC1BDD0.. (conversion scratch buffers,
  then unidentified globals). The count is stored as `int16` from a 32-bit quotient.

### 1.3 Lookup — `CText::Get` 0x6A0050 / `Search` 0x6A0000 / `BinarySearch` 0x69F570 / hash 0x53CF30

* `Get(key)`: if `key[0]==0 || key[0]==' '` → returns `""`. Else `Search(main)`; if not found and
  (mission-pack game ? missionLoaded : TABLloaded && missionLoaded) → `Search(mission)`; if still not
  found → `sprintf(buf, "")` at 0x6A00B1 (0x858B54 is the empty string) and returns the 25-byte static
  buffer 0xC1AEB8 → **a missing key renders as an empty string** (GXT-11). MAIN is searched first, so a
  hash present in both MAIN and the loaded mission table resolves to MAIN.
* `Search`: `hash = CKeyGen::GetUppercaseKey(key)`; `BinarySearch(hash, entries, 0, (int16)count-1)`.
  `GetUppercaseKey` 0x53CF30 = table CRC-32 (`CKeyGen::keyTable` 0x8CD068, init 0xFFFFFFFF, `toupper`
  from the CRT = ASCII a–z only, **no final XOR**) — i.e. JAMCRC of the upper-cased key. Verified: JAMCRC
  ("CDERROR") = 0xEF128EC3 is present in the MAIN TKEY of all five vanilla files (§6).
* `BinarySearch` 0x69F570: `lo/hi` are `int16`, comparison `param_1 < uVar1` on `uint` → entries must be
  sorted **ascending by unsigned hash** (GXT-05) and there can be at most 32767 entries per table (`dec cx`
  at 0x6A0018 on `count`; `count ≥ 32768` gives a negative `hi` and no search at all). Duplicate hashes: the
  search returns whichever entry the bisection hits first.
* `CKeyArray::Update` 0x69F540 adds the TDAT base to every offset without comparing against the TDAT size
  (GXT-06): an offset ≥ TDAT size, or a string that is not NUL-terminated inside TDAT, makes `Get` return a
  pointer past the heap block → text garbage or access violation when the font code walks the string.

### 1.4 Mission tables — `LoadMissionText` 0x69FBF0 (script opcode 054C LOAD_MISSION_TEXT)

```
if (bMissionPackGame) return;  free old mission arrays;  if (numTables == 0) return;
for i in 0..numTables-1:  match if strlen(tabl[i].name) == strlen(arg) && strncmp(tabl[i].name, arg, len)==0   ; case-sensitive
if not found → return (mission arrays stay empty → every key of that table renders "")
SetDir("TEXT"); open <LANG>.GXT; Seek(tabl[i].offset); read 8 bytes (table name), strncmp(name,arg,8) — RESULT DISCARDED (0x69FD6F)
loop until (TKEY && TDAT): read 8-byte header; TKEY → CKeyArray::Load; TDAT → CData::Load; else skip `size` bytes  (Read results unchecked)
Update(); CloseFile; SetDir(""); copy arg into m_szMissionName; missionLoaded = 1
```

* `strlen(tabl[i].name)` at 0x69FC3A runs across the 8-byte name field into the offset dword when the name
  has no NUL → the length never equals the label's → **table names must be ≤ 7 characters** (GXT-08).
  Vanilla max = 7 (`AMBULAE`, `BCESAR2` …).
* A mission table entry must point at `name[8]` + `TKEY` + `TDAT` (GXT-09); the name at the offset is not
  verified; missing TKEY/TDAT → infinite loop (same structure as §1.2, plus a `Seek` past EOF is not
  detected).
* Mission text is loaded only when the script asks for a table that exists in TABL; the name comparison
  is case-sensitive and exact-length (GXT-10).

### 1.5 Keys as seen from the script, encoding

* Script text labels are **8 raw bytes** (`CRunningScript::ReadTextLabelFromScript` 0x463D50, param type
  9: copies 8 bytes, no terminator added) → a label of 8 characters has no NUL → keys used by the script
  must be **≤ 7 characters** (GXT-12). The GXT format itself stores only hashes, so key length is otherwise
  unlimited.
* Encoding (GXT-14, Info): TDAT strings are NUL-terminated 8-bit byte strings; bytes < 0x80 are ASCII,
  bytes ≥ 0x80 are glyph indices of `fonts.txd` (the European accented set). `~x~` sequences are colour/
  button/newline tags handled by `CFont`/`CMessages`. `GetUpperCase` 0x69F750 upper-cases only a–z for
  'e', and via tables 0x8D3038 ('f') / 0x8D2FB8 ('g','i','s') for bytes ≥ 0x80. The only place the engine
  re-encodes text is 0x69F7E0 (used for the Windows message box / mission-pack names): it maps 0x80–0x83 →
  0xC0–0xC3, 0x84–0x8D → 0xC6–0xCF, 0x8E–0x91 → 0xD2–0xD5, 0x92–0x95 → 0xD9–0xDC, 0x96–0x9A → 0xDF–0xE3,
  0x9B–0xA4 → 0xE6–0xEF, 0xA5–0xA8 → 0xF2–0xF5, 0xA9–0xCC → +0x50, 0xCD → 0xD1, 0xCE → 0xF1, 0xCF → 0xBF,
  anything else ≥ 0xD0 → `#`, and truncates at 255 characters (`cmp ecx,0xff` at 0x4012BB).
* `Get("CDERROR")` is copied into `m_szCdErrorText` at load (0x6A03AD..0x6A03D8); a MAIN without
  `CDERROR` gives an empty disc-error box (GXT-13).

---------------------------------------------------------------------------------------------------

## 2. main.scm / script.img — `CTheScripts`

### 2.1 `CTheScripts::Init` 0x468D50 (body at 0x468D5D after the SecuROM hop)

```
memset(ScriptSpace 0xA49960, 0, 0x106B2*4 = 269000)      ; 200000 main + 69000 mission block (0xA7A6A0)
memset(LocalVariablesForCurrentMission 0xA48960, 0, 0x400*4)
... pools/arrays reset ...
if (bMissionPackGame) { ... MPACK//MPACK%d//SCR.SCM, CText::Load(0) ... }
else { SetDir("data\script"); f = OpenFile("main.scm","rb"); Read(f, ScriptSpace, 0x30D40 = 200000); CloseFile }   ; OpenFile result unchecked (0x468EC9..0x468EDB)
SetDir("")
ReadObjectNamesFromScript()  0x486720 → 0x156F5A0
UpdateObjectIndices()        0x486780 → 0x1562660
MultiScriptArray[0..199] = 0 (rep stosd ecx=0xC8 at 0xA444C8)
ReadMultiScriptFileOffsetsFromScript() 0x4867C0 → 0x1565E20
if (!bMissionPackGame) CStreamedScripts::ReadStreamedScriptData(&StreamedScripts 0xA47B60) 0x470750
```

* Missing main.scm → `fread(NULL)` → crash (SCM-01), same CRT path as GXT-01.
* Only the first **200000 bytes** are copied into `ScriptSpace`; header tables beyond that are zero
  (SCM-02/05).
* `StartTestScript` 0x464D40 = `StartNewScript(ScriptSpace)` → execution begins at byte 0, so the file
  must begin with the `02 00 01 <int32>` GOTO like every SA header segment (SCM-02).

### 2.2 Header segments (offsets as the engine reads them)

| segment | how found | fields read |
|---|---|---|
| seg0 | offset 0 | `02 00 01` + int32 → start of seg1 (= size of the global variable space, see SCM-09) |
| seg1 objects | `*(int32*)(ScriptSpace+3)` | at seg1+8: **uint16** count (0x156F5A6 `mov ax,word ptr`), then count × 24-byte names copied raw (`0x18` byte loop at 0x156F5EB) into `UsedObjectArray[i].name` (0xA44B70, stride 0x1C, index field at +0x18 = 0xA44B88) |
| seg2 missions | `*(int32*)(seg1+3)` | at seg2+8: u32 MainScriptSize → 0xA444C4, u32 LargestMissionScriptSize → 0xA444C0, **int16** NumberOfMissionScripts (movsx) → 0xA444BC, u16 NumberOfExclusiveMissionScripts → 0xA444B8, u32 LargestNumberOfMissionScriptLocalVariables → 0xA444B4, then N × u32 offsets → `MultiScriptArray` 0xA444C8 |
| seg3 streamed | `*(int32*)(seg2+3)` | at seg3+8: u32 largest streamed size → `StreamedScripts+0xA40`; seg3+12 u32 count is **ignored**; entries of 28 bytes {name[20], u32 unused, u32 size} at seg3+16 |
| seg4 | `*(int32*)(seg3+3)` | code start (the GOTO at seg3 lands here) |

Capacities, all derived from the exe:

* `UsedObjectArray`: cleared in `Init` from 0xA44B88 to 0xA476BC step 0x1C → (0xA476BC-0xA44B88)/0x1C =
  **395** entries (0xA44B70 + 395*0x1C = 0xA476BC = start of `EntitiesWaitingForScriptBrain`). No bound in
  0x156F5A0 → SCM-03. `UpdateObjectIndices` (0x1562660) runs `i = 1..count-1`: `index = -1;
  CModelInfo::GetModelInfo(name, &index)` (0x4C5940: uppercase-CRC compare over all 20000 model slots, any
  model type). Names that match nothing keep **-1**; when the script later uses that object slot the model
  id -1 reaches `CStreaming::RequestModel` 0x4087E0 which has no bounds check
  (`lea edi,[ebp*5]; shl edi,2; ... [edi+0x8E4CC6]` → 0x8E4CB2, inside `ms_pStreamingBuffer`/buffer size
  globals) → SCM-04. A name without a NUL inside its 24 bytes hashes on into the -1 index bytes → never
  matches.
* `MultiScriptArray`: **200** (rep stosd 0xC8 dwords at 0xA444C8; the next global 0xA447E8 is
  `bAlreadyRunningAMissionScript`). No bound in 0x1565E20 → SCM-05.
* Mission launch (`LOAD_AND_LAUNCH_MISSION_INTERNAL`, 0x489980..0x489A7A): `Seek(f, MultiScriptArray[id])`,
  `Read(f, 0xA7A6A0, 0x10D88 = 69000)` — **exactly 69000 bytes, regardless of the mission's size** → a mission
  longer than 69000 bytes is truncated and runs into whatever follows 0xA8BAC8 (SCM-05). Main code beyond
  200000 bytes is never loaded (SCM-02). The 200000/69000 split is fixed by `memset(…, 269000)` in `Init`.
* `LocalVariablesForCurrentMission` 0xA48960 has 0x400 dwords (`WipeLocalVariableMemoryForMissionScript`
  0x1569F10 `rep stosd ecx=0x400`) and is immediately followed by `ScriptSpace` 0xA49960 → a mission local
  index ≥ 1024 aliases the global variable space (SCM-08).

### 2.3 script.img — `CStreamedScripts` @0xA47B60

* Layout from the dumps: entries at +0 stride 0x20 {mem*, u8 users, int16 indexUsedByScript (+6),
  char name[20] (+8), int32 size (+0x1C)}, `m_nLargestExternalSize` at +0xA40, `int16 m_nCountOfScripts` at
  +0xA44 → **82** entries (82*0x20 = 0xA40).
* `RegisterScript` 0x4706C0 (called from `LoadCdDirectory` for every `*.scm` IMG entry): unbounded `strcpy`
  into `this + 8 + count*0x20`, `count++`. Entry 82 overwrites `m_nLargestExternalSize`/`m_nCountOfScripts`
  → SCM-07. A base name of ≥ 20 characters overflows `name[20]` into `size` (harmless until…) and, since the
  seg3 name field is 20 bytes without a terminator, a 20-character name can never match → SCM-06.
* `ReadStreamedScriptData` 0x470750: `for i in 0..m_nCountOfScripts-1` (the **IMG** count!) copy the i-th
  28-byte seg3 entry, `stricmp` its name against every registered name; not found → `j = -1` and the size
  is written to `this + (-1)*0x20 + 0x1C` = 0xA47B5C and the index to 0xA47B46 — i.e. into
  `EntitiesWaitingForScriptBrain[149]/[146]` (0xA476B0, stride 8, 150 entries) → SCM-06. If script.img
  holds more `.scm` files than seg3 has entries, the loop reads seg3 rows past the table (code bytes) as
  names.
* `LoadStreamedScript` 0x470840 → 0x1565EC0: `new(size_from_seg3)`, `RwStreamRead(stream, buf, size_from_seg3)`
  → a seg3 size smaller than the file truncates the script (SCM-06); larger just reads trailing bytes of
  the streaming buffer (RW memory streams clamp), no overflow.
* Resource ids: SCM = 26230..26311 (`0x6676 + index`, `LoadCdDirectory` 0x5B6427).

### 2.4 Savegames (Info)

`CTheScripts::Load` 0x5D4FD0 runs `Init()` (re-reads main.scm) and then overwrites `ScriptSpace[0..N]`
with the saved global block in 0xC800-byte pieces; N is the saved size, not re-derived from the new file.
A main.scm whose seg0 size or global layout differs from the one that produced the save gets its globals
replaced by foreign data (SCM-09, Info — checker cannot know the save).

---------------------------------------------------------------------------------------------------

## 3. IMG archives and streaming — `CStreaming`

### 3.1 The image list (8 slots)

`ms_files` @0x8E48D8, stride 0x30 {char name[0x28], u8 bNotPlayerImg (+0x28), i32 handle (+0x2C)}, end
0x8E4A58 → (0x8E4A58-0x8E48D8)/0x30 = **8**.

* `InitImageList` 0x4083C0 (Init1): clears the 8 slots, puts `MODELS\GTA3.IMG` in the first free slot (0)
  and `MODELS\GTA_INT.IMG` in the next (1), both with flag 1, `CdStreamOpen` each.
* gta.dat `IMG <path>` (LoadLevel 0x5B914E..0x5B9160): skipped if the path equals `MODELS\GTA_INT.IMG`,
  otherwise `AddImageToList(path, 1)`.
* `AddImageToList` 0x407610: first slot with `name[0]==0`; unbounded string copy into `name[0x28]`
  (IMG-03); `CdStreamOpen`; **returns 0 when all 8 slots are used** — the caller cannot tell (IMG-01).
* `CClothes::Init` 0x5A80D0 (Init2, after LoadLevel): `ms_clothesImageId (0xBC12F8) = AddImageToList("MODELS\\PLAYER.IMG", 0)`.
  With 2 built-ins + N gta.dat lines + player.img, N ≥ 6 makes player.img get **id 0 = gta3.img**; every
  clothes read then uses player.img offsets inside gta3.img → garbage RW streams for the player model
  (IMG-01, Crash). Vanilla: N = 3 (`DATA\PATHS\CARREC.IMG`, `DATA\SCRIPT\SCRIPT.IMG`, `MODELS\CUTSCENE.IMG`).
* `LoadCdDirectory()` 0x5B82C0 walks slots 0..7 while `name[0] != 0` and calls `LoadCdDirectory(name, slot)`
  only for slots with flag ≠ 0 → player.img (flag 0) is never scanned. It runs once, from `Init2` 0x5B8AD0
  (body 0x5B9020 @0x5B8E1B) at the first IPL line; IMG lines after that are registered but never scanned
  (IMG-02).

### 3.2 Directory scan — `LoadCdDirectory(file, imgId)` 0x5B6170 (+ tail 0x5B6449)

```
f = OpenFile(path,"rb"); if (f <= 0) return                  ; unreadable IMG silently skipped
Read(f, magic, 4)   ; never compared ("VER2" not checked)
Read(f, count, 4)   ; not validated
for each of count 32-byte entries (Read result not checked):
    {u32 offset; u16 streamingSize; u16 sizeInArchive; char name[24]}   ; name[23] forced to 0
    if (streamingSize > ms_streamingBufferSize) ms_streamingBufferSize = streamingSize      ; uses bytes 4..5 ONLY
    dot = strchr(name,'.'); if (!dot || dot-name >= 0x15) continue         ; base name > 20 chars → entry ignored
    *dot = 0; ext = dot+1; strnicmp(ext, "DFF"/"TXD"/"COL"/"IPL"/"DAT"/"IFP"/"RRR"/"SCM", 3)
      DFF: GetModelInfo(name,&id) (uppercase-CRC, any model type); not a model → CDirectory::AddItem(ms_pExtraObjectsDir, entry) and continue
      TXD: FindTxdSlot/AddTxdSlot + CVehicleModelInfo::AssignRemapTxd → id = slot + 20000
      COL: FindColSlot/AddColSlot → +25000      IPL: FindIplSlot/AddIplSlot → +25255
      DAT: sscanf(name+5, "%d", &id) → +25511   ("nodes%d" assumed; anything else → id uninitialised)
      IFP: RegisterAnimBlock → +25575           RRR: RegisterRecordingFile → +25755        SCM: RegisterScript → +26230
      other extension: *dot = '.'; continue                                   ; entry ignored
    info = &ms_aInfoForModel[id]
    if (info.GetCdPosnAndSize())  → skip (0x5B6450)            ; already registered by an earlier entry/IMG → FIRST WINS
    info.imgId = imgId; size = sizeInArchive ? sizeInArchive : streamingSize; info.SetCdPosnAndSize(offset, size); info.flags = 0
    previous.nextOnCd = id  (chain in directory order)
```

`CStreamingInfo::GetCdPosnAndSize` (0x4075A0 → 0x1560E50) returns false iff `m_nCdSize == 0`;
`SetCdPosnAndSize` (0x4075E0 → 0x1564070) stores offset/size unconditionally.

Rules that follow:

* IMG-04 format: no magic/count validation; entries beyond EOF are parsed from stale stack bytes.
* IMG-05 names: 24-byte field, byte 23 forced NUL (so 23 usable characters), first `.` must be at index
  ≤ 20, extension matched on its **first three characters, case-insensitive** (`dffx`, `ipl2` match), first
  dot wins (`a.b.dff` → ext `b.d` → ignored). Unknown extensions and over-long base names are dropped
  silently. Base names compare case-insensitively (CRC of upper-case) for DFF/TXD/COL/IPL, `strncpy 16`
  for IFP (see `ifp_path.md`), `stricmp` for SCM, `"carrec%d"`/`"CARREC%d"` for RRR (anything else → number
  850 → id 26605 = outside the 26312 `ms_aInfoForModel` entries).
* IMG-06 duplicates: the first entry that registers a non-zero size owns the slot; later entries with the
  same base name and type are ignored, whatever IMG they are in. Order = slot order (gta3, gta_int, gta.dat
  lines in file order). Within one IMG: directory order. Vanilla ships 4 such shadowed entries (§6).
* IMG-07 an entry with `streamingSize == 0` (and `sizeInArchive == 0`) never claims the slot → the model is
  "not on CD"; a later duplicate can still claim it.
* IMG-08 `sizeInArchive` (bytes 6..7): used for the read size when non-zero, **but not for the buffer
  maximum** — if it exceeds the largest `streamingSize` of all scanned entries, the read overruns the
  streaming buffer. Vanilla: 0 in every entry of every IMG.
* IMG-10 `ms_pExtraObjectsDir = new CDirectory(0x226 = 550)` (0x5B9020); `CDirectory::AddItem` 0x532310 does
  `if (count >= capacity) return` → the 551st non-model DFF is silently unreachable by name (special
  characters, cutscene models). Vanilla: 392 (75 in gta3.img + 317 in cutscene.img).
* IMG-11 per-type registries, limits verified elsewhere or here: TXD 5000 (DAT-15), COL 255 (`col_path.md`),
  IPL 256 (DAT-39), IFP 180 (`ifp_path.md`), nodes 64 (DAT-41; a `.dat` entry that is not `nodesNN.dat`
  leaves `sscanf`'s target uninitialised → random id), RRR **475** (`StreamingArray` 0x97D880 stride 16 up to
  `NumPlayBackFiles` 0x97F630 → 475; 0x156F157 stores without a bound), SCM 82 (SCM-07).

### 3.3 Streaming buffer sizing (`Init2` body 0x5B9020, after `LoadCdDirectory`)

```
if (ms_streamingBufferSize & 1) ms_streamingBufferSize++          ; round up to even sectors
ms_pStreamingBuffer[0] = MallocAlign(ms_streamingBufferSize << 11, 0x800)
ms_streamingBufferSize /= 2                                        ; per-channel size
ms_pStreamingBuffer[1] = buffer[0] + ms_streamingBufferSize*0x800
ms_memoryAvailable = 0x3200000 (50 MB);  desiredNumberOfVehiclesLoaded = 22
```

So the buffer is exactly twice the largest `streamingSize` seen (rounded), split into two channels; any
entry larger than one channel occupies both. Vanilla max = 1263 sectors (2.47 MB, `vgwsthiway1.txd`).
player.img is not scanned and therefore does not influence the size (its data is read by
`CClothesBuilder`, not through `ms_pStreamingBuffer` — not traced here, see §7).

### 3.4 stream.ini — `CStreaming::ReadIniFile` 0x5BCCD0

`f = OpenFile("stream.ini","r")` (unchecked) → `CFileLoader::LoadLine(f)` → `fgets(NULL)` → crash when
missing (IMG-12). Per line: `#`/blank skipped; `key = strtok(line, " ,\t")`, `value = strtok(NULL, …)`;
keys (`stricmp`): `memory` (KB, ignored if `devkit_memory` was seen), `devkit_memory` (KB), `vehicles`,
`dontbuildpaths` (no value), `pe_lightchangerate`, `pe_lightingbasecap`, `pe_lightingbasemult` (atof),
`pe_leftx`, `pe_rightx`, `pe_topy`, `pe_bottomy`, `pe_bRadiosity`, `def_brightness_pal` (atol). A value key
without a value passes `NULL` to `atol`/`atof` → crash. `memory`/`devkit_memory` and `vehicles` are then
overwritten in `Init2` (§3.3) — they have **no effect** in 1.0 US (vanilla file says 13500 KB / 12, the game
runs with 50 MB / 22).

---------------------------------------------------------------------------------------------------

## 4. UI files

### 4.1 `CTxdStore::LoadTxd(slot, filename)` 0x7320B0 — the hang

```
do { s = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, filename); } while (!s);   ; 0x7320D0..0x7320E5
ok = LoadTxd(slot, s) 0x731DD0;  RwStreamClose(s);  return ok
```

A missing file is an **infinite loop** (UI-01). Call sites (all `xref` 0x7320B0):

| file | caller | when |
|---|---|---|
| `MODELS\FONTS.TXD` | `CFont::Initialise` 0x5BA690 | RW init |
| `MODELS\PCBTNS.TXD` | `CFont::Initialise` 0x5BA7D4 | RW init |
| `MODELS\HUD.TXD` | `CHud::Initialise` 0x5BA850 | RW init |
| `MODELS\TXD\loadscs.txd` | `CLoadingScreen::LoadSplashes` 0x5900B0 | before menu |
| `MODELS\FRONTEN1.TXD`, `MODELS/FRONTEN_pc.TXD`, `MODELS\FRONTEN2.TXD`, `MODELS/FRONTEN3.TXD` | `CMenuManager::LoadAllTextures` 0x572EC0 | menu |
| `MODELS\PARTICLE.TXD` | `CGame::Init1` 0x5BF8B7 | new game |
| `models\grass\plant1.txd` | `CPlantMgr::Initialise` 0x5DD95F | new game |
| `models\effectsPC.txd` | `FxManager_c::LoadFxProject` 0x5C248F | new game (DAT-48) |
| `MODELS\GENERIC\VEHICLE.TXD` | `CVehicleModelInfo::SetupCommonData` 0x5B8F5E | first IPL line |
| `models\txd\<name>.txd` | script `LOAD_TXD_DICTIONARY` 0x0390 → 0x48418A | at runtime (SCM-10) |

`LoadTxd(slot, stream)` 0x731DD0: `RwStreamFindChunk(0x16)` fails or `RwTexDictionaryGtaStreamRead`
returns NULL → returns false; **no caller checks it** → the slot's dictionary is NULL and every subsequent
`CSprite2d::SetTexture` (0x727270: `RwTextureRead(name, NULL)`) yields NULL. `CSprite2d::SetRenderState`
0x727B30 then sets `rwRENDERSTATETEXTURERASTER = NULL` → untextured (flat-coloured) quads (UI-02).
Texture lookup inside a TXD is case-insensitive (vanilla hud.txd stores `skipicon`, `radar_hostpital`,
`radar_Flag` for the engine names `SkipIcon`, `radar_hostpitaL`, `radar_flag`); the mask names passed as the
second argument are not looked up in the TXD.

### 4.2 Required textures (names read from the exe tables)

| TXD | names | table |
|---|---|---|
| fonts.txd | `font2`, `font1` | `CFont::Initialise` |
| pcbtns.txd | `up`, `down`, `left`, `right` | `CFont::Initialise` |
| hud.txd | `fist`, `siteM16`, `siterocket`, `radardisc`, `radarRingPlane`, `SkipIcon` | `CHud::Initialise`, table 0x8D128C |
| hud.txd | `radar_centre`, `arrow`, `radar_north`, `radar_airYard`, `radar_ammugun`, `radar_barbers`, `radar_BIGSMOKE`, `radar_boatyard`, `radar_burgerShot`, `radar_bulldozer`, `radar_CATALINAPINK`, `radar_CESARVIAPANDO`, `radar_chicken`, `radar_CJ`, `radar_CRASH1`, `radar_diner`, `radar_emmetGun`, `radar_enemyAttack`, `radar_fire`, `radar_girlfriend`, `radar_hostpitaL`, `radar_LocoSyndicate`, `radar_MADDOG`, `radar_mafiaCasino`, `radar_MCSTRAP`, `radar_modGarage`, `radar_OGLOC`, `radar_pizza`, `radar_police`, `radar_propertyG`, `radar_propertyR`, `radar_race`, `radar_RYDER`, `radar_saveGame`, `radar_school`, `radar_qmark`, `radar_SWEET`, `radar_tattoo`, `radar_THETRUTH`, `radar_waypoint`, `radar_TorenoRanch`, `radar_triads`, `radar_triadsCasino`, `radar_tshirt`, `radar_WOOZIE`, `radar_ZERO`, `radar_dateDisco`, `radar_dateDrink`, `radar_dateFood`, `radar_truck`, `radar_cash`, `radar_flag`, `radar_gym`, `radar_impound`, `radar_light`, `radar_runway`, `radar_gangB`, `radar_gangP`, `radar_gangY`, `radar_gangN`, `radar_gangG`, `radar_spray` (blip ids 2..63) | `CRadar::LoadTextures` 0x5827D0 (txd "hud"), table 0x8D0720..0x8D0920 |
| loadscs.txd | `nvidia`, `eax`, `title_pc_US`, `loadsc0`..`loadsc14` (7 sprites: title, nvidia/eax, 5 random of the 15) | `LoadSplashes` 0x590180.. |
| fronten1.txd | `arrow`, `radio_playback`, `radio_krose`, `radio_KDST`, `radio_bounce`, `radio_SFUR`, `radio_RLS`, `radio_RADIOX`, `radio_csr`, `radio_kjah`, `radio_mastersounds`, `radio_WCTR`, `radio_TPLAYER` | 0x8CDF28 |
| fronten2.txd | `back2`..`back8`, `map` | 0x8CDF90 |
| fronten3.txd | `back8_top`, `back8_right` | 0x8CDFD0 |
| fronten_pc.txd | `mouse`, `crosshair` | 0x8CDFE0 |
| vehicle.txd | `vehiclelights128`, `vehiclelightson128` looked up with `RwTexDictionaryFindNamedTexture` (0x5B8F9F/0x5B8FB4) | `SetupCommonData` (use of a NULL result not traced) |

### 4.3 fonts.dat — `CFont::LoadFontValues` 0x7187C0

`SetDir(""); f = OpenFile("DATA\FONTS.DAT","rb")` (unchecked → `fgets(NULL)` crash if missing).
Lines via `LoadLine` (§1.1 of `textdata_path.md`: 511-char limit, `#` comment only at column 0). The first
token of each line (`sscanf "%s"`) is compared **exactly** (`repe cmpsb` including the NUL) against:

| token | action |
|---|---|
| `[TOTAL_FONTS]` | next line `%d` → local, unused |
| `[FONT_ID]` | next line `%d` → `bl` (**byte, unchecked**) selects `gFontData[bl]` @0xC718B0, stride 0xD2 |
| `[REPLACEMENT_SPACE_CHAR]` | next line `%d` → `gFontData[bl] + 0xD0` |
| `[PROP]` | next **26** lines, each `sscanf("%d %d %d %d %d %d %d %d")` → 8 bytes each → 208 width bytes (`gFontData[bl] + 0..0xCF`); missing numbers leave the previous line's stack values → wrong widths |
| `[UNPROP]` | next line `%d` → `gFontData[bl] + 0xD1` |

`gFontData` has **2** entries: 0xC718B0 + 2*0xD2 = 0xC71A54 = `CFont::m_nExtraFontSymbolId`, followed by
`m_Color` 0xC71A60, `m_Scale`, …, `CFont::Sprite` 0xC71AD0, `ButtonSprite` 0xC71AD8. A `[FONT_ID]` of 2 or
more writes its 210 bytes over those globals (id 2 covers 0xC71A54..0xC71B26, i.e. the font sprite
pointers) → crash at the first text draw (UI-04).

### 4.4 Radar tiles — `CRadar::Initialise` 0x587FB0 / `LoadTextures` 0x5827D0

`for i in 0..143: gRadarTxdIds[i] (0xBA8478) = CTxdStore::FindTxdSlot(sprintf("radar%02d", i))` — the slot
exists only if some scanned IMG contains `radarNN.txd` (`LoadCdDirectory` TXD branch). `-1` is checked at
every use (`DrawRadarSection` 0x586180 `cmp eax,-1 → skip`; `RequestMapSection` etc. in gta-reversed) → a
missing tile is simply blank (UI-05, Warning). Drawing uses the **first texture of the dictionary**
(`GetFirstTexture` 0x734940 at 0x5861AF) — the texture name inside `radarNN.txd` is irrelevant; an empty
TXD draws nothing. Vanilla: 144 tiles, each one 128×128 texture named `radarNN`. Name must be exactly
`radar%02d` (`radar0.txd` is not found; comparison is case-insensitive).

---------------------------------------------------------------------------------------------------

## 5. Rule table

Severity: **Crash** = crash/hang/memory corruption, **Error** = visible garbage, **Warning** = silently
ignored, **Info**. "how" = what the offline checker computes.

| id | file | condition | severity | where | evidence | how to check |
|---|---|---|---|---|---|---|
| GXT-01 | text\american.gxt | file missing/unreadable (the language selected in the settings; american by default) | Crash | `CText::Load` 0x6A01A0 | `OpenFile` result unchecked (0x6A0228), `Read` 0x538950 → `fread` 0x823521 → `_lock_str` 0x823FA2 `EnterCriticalSection(NULL+0x20)` | file exists `TEXT\AMERICAN.GXT`; french/german/italian/spanish missing → Warning (only opened when that language is selected; same crash then) |
| GXT-02 | *.gxt | first 4 bytes ≠ `04 00 08 00` (III/VC layout without version header) | Crash (hang) | 0x6A022D/0x6A023C | two `Read(2)` consume 4 bytes blindly; chunk walk then mis-aligned, loop has no EOF exit | bytes 0..3 == 04 00 08 00 |
| GXT-03 | *.gxt | no top-level `TKEY` **and** `TDAT` with size > 0 reachable by walking chunks from offset 4 (MAIN table), or file truncated inside them | Crash (hang / unbounded `new`) | 0x6A0255..0x6A0382 | loop exits only when both flags set; failed `Read` at 0x6A0280 → 0x6A0298 keeps stale header | walk: [4]="TABL"? skip size; then expect (optional 8-byte name with zero size) `TKEY`,`TDAT`; both sizes > 0 and inside the file |
| GXT-04 | *.gxt | `TKEY` size % 8 ≠ 0; `TABL` size % 12 ≠ 0; entries per table > 32767 | Crash (hang) / Error (no key found) | `CKeyArray::Load` 0x69F490 `size>>3`, 0x69F670 `size/0xC`, `Search` 0x6A0000 `dec cx` (int16) | consumes floor(size/8)*8 bytes → next header garbage; int16 indices in `BinarySearch` 0x69F570 | size arithmetic; count ≤ 32767 |
| GXT-05 | *.gxt | TKEY entries not sorted ascending by **unsigned** hash / duplicate hash | Error (keys not found → blank) / Warning | `BinarySearch` 0x69F570 `param_1 < uVar1` (uint) | bisection on unsorted data misses | hash[i] < hash[i+1] for every table |
| GXT-06 | *.gxt | TKEY entry offset ≥ TDAT size, or string not NUL-terminated before TDAT end | Crash / Error | `CKeyArray::Update` 0x69F540 (`*p += base`, no range check), `Get` returns the pointer | reads past the `new(size)` block | offset < dsize and `memchr(TDAT+offset, 0, dsize-offset)` |
| GXT-07 | *.gxt | TABL has > 200 entries | Crash (memory corruption) | `CMissionTextOffsets::Load` 0x69F670 | writes `this+0x12C+i*12` for all `size/12` entries; array is 0x960 bytes = 200; entries ≥ 200 overwrite the count (+0xA8C) and 0xC1BDD0.. | size/12 ≤ 200 |
| GXT-08 | *.gxt | TABL name uses all 8 bytes (no NUL) | Error (mission table can never be selected → its keys render "") | `LoadMissionText` 0x69FBF0 strlen loop 0x69FC3A | strlen runs into the offset dword → length mismatch | every TABL name has a NUL within 8 bytes (≤ 7 chars); vanilla max 7 |
| GXT-09 | *.gxt | mission table offset does not point at `name[8]` + `TKEY` + `TDAT` (both size > 0, inside the file) | Crash (hang when the script loads that table) | 0x69FD9A.. | header/`Read` results unchecked, loop exits only on TKEY&&TDAT; name compare result at 0x69FD6F discarded | parse every non-MAIN TABL entry as name+TKEY+TDAT and apply GXT-04..06 |
| GXT-10 | *.gxt + main.scm | script `LOAD_MISSION_TEXT` (0x054C) label not present in TABL (exact, case-sensitive, same length) | Warning (no mission text; keys → "") | 0x69FC0F..0x69FC5B | `return` when no match | scan SCM for opcode 0x054C, compare its 8-byte label with TABL names |
| GXT-11 | *.gxt + main.scm | key referenced by a script print opcode not in MAIN (or in the mission table the script loads) | Warning (renders as empty string) | `CText::Get` 0x6A0050 → 0x6A00B1 `sprintf(buf,"")` (0x858B54 = "") | – | JAMCRC(toupper(label)) ∈ MAIN hashes ∪ hashes of the table selected by the nearest 0x054C |
| GXT-12 | main.scm | text label longer than 7 characters | Warning (8-byte field has no NUL → key garbage) | `ReadTextLabelFromScript` 0x463D50 copies 8 bytes without terminator | – | label byte 7 must be 0 |
| GXT-13 | *.gxt | MAIN lacks key `CDERROR` (hash 0xEF128EC3) | Warning (empty disc-error box) | 0x6A03AD..0x6A03D8 | copies `Get("CDERROR")` = "" | hash present |
| GXT-14 | *.gxt | strings: NUL-terminated 8-bit; ≥ 0x80 = font glyph index; `~` tags; 0x69F7E0 caps Windows-side conversion at 255 chars | Info | 0x69F7E0, `GetUpperCase` 0x69F750 | – | (informational; hash = CRC-32 table 0x8CD068, init 0xFFFFFFFF, no final XOR, key upper-cased with C-locale toupper) |
| SCM-01 | data\script\main.scm | file missing/unreadable | Crash | `CTheScripts::Init` 0x468D50 (0x468EC9 `OpenFile`, 0x468EDB `Read`) | `fread(NULL)` → `_lock_str` NULL deref | file exists |
| SCM-02 | main.scm | byte 0..2 ≠ `02 00 01`; segment GOTO targets (int32 at +3 of seg0/1/2/3) not strictly ascending or ≥ 200000; object/mission/streamed tables not entirely inside the first 200000 bytes | Crash | `StartTestScript` 0x464D40 = `StartNewScript(ScriptSpace)`; `Init` reads only 0x30D40 bytes (0x468ECE); header readers at 0x156F5A0/0x1565E20/0x470750 index `ScriptSpace + target` unchecked | execution starts at byte 0; tables past 200000 are read as zeros / out of the global | parse 4 headers; targets < 200000 and tables end < 200000 |
| SCM-03 | main.scm | object-name count (uint16 at seg1+8) > 395; a name without NUL in its 24 bytes | Crash / Error | `ReadObjectNamesFromScript` 0x486720 → 0x156F5A0 | no bound: `UsedObjectArray` 0xA44B70..0xA476BC (395 × 0x1C), overflow hits `EntitiesWaitingForScriptBrain` 0xA476B0 and `CStreamedScripts` 0xA47B60 | count ≤ 395; each name has a NUL within 24 bytes (vanilla: 389, max 18 chars) |
| SCM-04 | main.scm + IDE | object-table name (index ≥ 1) is not a model defined in any loaded IDE (any section, case-insensitive) | Crash (when the script uses that object: model id -1 → `RequestModel` 0x4087E0 indexes `ms_aInfoForModel[-1]` at 0x8E4CAC.., no bounds check) | `UpdateObjectIndices` 0x486780 → 0x1562660 (`index=-1; GetModelInfo(name,&index)` 0x4C5940) | – | every name ∈ model-name set built from the IDEs |
| SCM-05 | main.scm | seg2: number of missions (int16 at seg2+16) > 200; MainScriptSize > 200000; LargestMissionScriptSize > 69000 or any mission (offset[i+1]-offset[i], last: EOF-offset) > 69000; mission offset ≥ file size | Crash / Error (truncated script executes garbage) | `ReadMultiScriptFileOffsetsFromScript` 0x4867C0 → 0x1565E20 (no bound, `MultiScriptArray` 0xA444C8[200]); mission launch 0x4899D1/0x489A5A `Read(f, 0xA7A6A0, 69000)` | fixed 200000+69000 split (`memset` 0x106B2 dwords at 0x468D5D) | header fields + offsets |
| SCM-06 | main.scm + script.img | seg3 entry count < number of `*.scm` entries in script.img; a seg3 name not found in script.img (stricmp); a streamed script base name ≥ 20 chars; seg3 size < actual entry size | Error (memory corruption in `EntitiesWaitingForScriptBrain[149]/[146]`, truncated scripts) | `ReadStreamedScriptData` 0x470750 (loop over IMG count, `j=-1` → write at `this-4`), `LoadStreamedScript` 0x1565EC0 (`new(seg3size)`, `RwStreamRead(seg3size)`), `RegisterScript` 0x4706C0 (`name[20]`) | – | compare seg3 rows with the `.scm` directory of script.img (name, count, size ≤ seg3 size) |
| SCM-07 | script.img | more than 82 `*.scm` entries in all scanned IMGs | Crash | `CStreamedScripts::RegisterScript` 0x4706C0 | entry 82 (`this+8+82*0x20` = +0xA48) overwrites `m_nLargestExternalSize`/`m_nCountOfScripts` (+0xA40/+0xA44); ids 26230..26311 | count ≤ 82 (vanilla 79) |
| SCM-08 | main.scm | seg2 "largest number of mission locals" (u32 at seg2+20) > 1024 | Warning (locals ≥ 1024 alias `ScriptSpace`) | `LocalVariablesForCurrentMission` 0xA48960 = 0x400 dwords (`Wipe` 0x1569F10) directly before `ScriptSpace` 0xA49960 | – | field ≤ 1024 (vanilla 964) |
| SCM-09 | main.scm | global-variable space (seg0 target) differs from the one a save was made with | Info | `CTheScripts::Load` 0x5D4FD0 (`Init()` then overwrite `ScriptSpace[0..savedSize]`) | – | report seg0 size; cannot be decided without the save |
| SCM-10 | main.scm + models\txd | script `LOAD_TXD_DICTIONARY` (0x0390) name without `models\txd\<name>.txd` | Crash (hang) | 0x484100..0x48418A → `CTxdStore::LoadTxd` 0x7320B0 (`RwStreamOpen` retry loop) | see UI-01 | scan SCM for opcode 0x0390 string params, check file |
| IMG-01 | gta.dat / default.dat | more than 5 `IMG` lines in total (2 built-ins + lines + player.img > 8 slots) | Crash (player.img gets id 0 = gta3.img → clothes/player model built from wrong data); the 9th+ IMG itself is silently ignored | `AddImageToList` 0x407610 (`ms_files` 0x8E48D8, 8 × 0x30, returns 0 when full), `CClothes::Init` 0x5A80D0 (`ms_clothesImageId = AddImageToList("MODELS\\PLAYER.IMG",0)` after LoadLevel) | – | count IMG lines (excluding `MODELS\GTA_INT.IMG`) ≤ 5 (vanilla 3) |
| IMG-02 | gta.dat | `IMG` line after the first `IPL` line | Warning (archive opened but its directory never scanned → every entry missing) | `LoadCdDirectory()` 0x5B82C0 runs once from `Init2` 0x5B8AD0 (body 0x5B9020 @0x5B8E1B) triggered at the first IPL line (LoadLevel 0x5B924E..0x5B927D) | – | line order in default.dat+gta.dat |
| IMG-03 | gta.dat | `IMG` path ≥ 40 characters | Error (overflows `name[0x28]` into the next slot's name) | `AddImageToList` 0x407610 unbounded copy | – | strlen(path) < 40 |
| IMG-04 | *.img | not `VER2`; `8 + count*32` > file size; count ≤ 0 | Crash / Error (garbage entries from stale stack) | `LoadCdDirectory` 0x5B6170: magic and count not validated, `Read` results unchecked | – | header check |
| IMG-05 | *.img | entry name: no `.` or first `.` at index > 20; extension (3 chars after the first dot, case-insensitive) not one of DFF/TXD/COL/IPL/DAT/IFP/RRR/SCM; no NUL in 24 bytes (23 usable) | Warning (entry silently ignored) | 0x5B6170 `strchr`/`< 0x15`, `strnicmp(...,3)` chain, `local_1 = 0` | – | per entry |
| IMG-06 | *.img | same base name + type registered twice (within an IMG or across IMGs) | Info (first registered wins: slot order gta3, gta_int, gta.dat lines in order; directory order within an IMG) | 0x5B6449 `GetCdPosnAndSize` (0x1560E50 `m_nCdSize != 0`) → skip at 0x5B6450 | – | report duplicates with the winner; vanilla has 4 (`barrier`, `kbmiscfrn1`, `lawest1`, `changeme` .txd in gta_int.img shadowed by gta3.img) — must not be an Error |
| IMG-07 | *.img | entry with streaming size 0 (bytes 4..5) and size-in-archive 0 | Warning (never registered → model "not on CD") | `SetCdPosnAndSize` stores size 0; `GetCdPosnAndSize` then false | – | size ≠ 0 |
| IMG-08 | *.img | bytes 6..7 (size in archive) ≠ 0 | Warning; Crash if it exceeds the maximum bytes 4..5 of all scanned entries (read overruns `ms_pStreamingBuffer`) | 0x5B61EC (`max` uses bytes 4..5 only), 0x5B6465..0x5B6486 (bytes 6..7 override the stored size) | – | bytes 6..7 == 0 (vanilla: always 0) |
| IMG-09 | *.img | streaming buffer = 2 × max(bytes 4..5) rounded to even sectors, split into 2 channels; entries > half the buffer block the second channel | Info | `Init2` body 0x5B9020 (`MallocAlign(size<<11)`, `/2`) | – | report max entry (vanilla 1263 sectors) |
| IMG-10 | *.img + IDE | more than 550 `.dff` entries whose base name is not an IDE model (special peds, cutscene models) | Warning (entries ≥ 551 unreachable by name) | `ms_pExtraObjectsDir = new CDirectory(0x226)` (0x5B9020), `CDirectory::AddItem` 0x532310 `count >= capacity → drop` | – | count DFFs ∉ model set ≤ 550 (vanilla 392) |
| IMG-11 | *.img | `.rrr` entries > 475 or not named `carrecN`; `.dat` entry not named `nodesNN` (NN 0..63); type limits TXD 5000 / COL 255 / IPL 256 / IFP 180 / SCM 82 | Crash | `RegisterRecordingFile` 0x156F110 (`StreamingArray` 0x97D880 stride 16, no bound; default number 850), `sscanf(name+5,"%d")` at 0x5B6390 with uninitialised target | ids 25755+475 = 26230 = SCM range; `ms_aInfoForModel` has 26312 entries | per-type counts; cross-ref DAT-15, DAT-39, DAT-41, IFP-, COL- rules |
| IMG-12 | stream.ini | file missing; a value key (`memory`, `devkit_memory`, `vehicles`, `pe_*`, `def_brightness_pal`) without a value token | Crash (`fgets(NULL)` / `atol(NULL)`) | `ReadIniFile` 0x5BCCD0 (`OpenFile` unchecked → `LoadLine`; `strtok` second token unchecked) | – | exists; each such line has 2 tokens. Info: `memory`/`devkit_memory`/`vehicles` are overwritten by `Init2` (0x3200000 / 22) and have no effect |
| IMG-13 | *.img | `(offset + size) * 2048` > file size | Error (read returns short → streaming error path; not traced past `CdStreamRead`) | `SetCdPosnAndSize` stores raw values | – | per entry |
| UI-01 | models\*.txd etc. | any of `MODELS\FONTS.TXD`, `MODELS\PCBTNS.TXD`, `MODELS\HUD.TXD`, `MODELS\TXD\LOADSCS.TXD`, `MODELS\FRONTEN1.TXD`, `MODELS\FRONTEN2.TXD`, `MODELS\FRONTEN3.TXD`, `MODELS\FRONTEN_PC.TXD`, `MODELS\PARTICLE.TXD`, `models\grass\plant1.txd`, `MODELS\GENERIC\VEHICLE.TXD` (and `models\effectsPC.txd`, DAT-48) missing | Crash (infinite `RwStreamOpen` loop) | `CTxdStore::LoadTxd` 0x7320B0 0x7320D0..0x7320E5; callers listed in §4.1 | – | files exist |
| UI-02 | same TXDs | file is not a TXD (no `rwID_TEXDICTIONARY` 0x16 chunk) or fails to parse; a required texture name missing | Error (NULL dictionary / NULL sprite → flat quads) | `LoadTxd` 0x731DD0 returns false, callers ignore; `CSprite2d::SetTexture` 0x727270 → `RwTextureRead` NULL; `SetRenderState` 0x727B30 sets NULL raster | – | parse TXD; names of §4.2 present (case-insensitive) |
| UI-03 | hud.txd | any of the 6 HUD sprites or the 62 `radar_*` + `arrow` blip textures missing | Error (blip/HUD element drawn as a coloured square) | `CHud::Initialise` 0x5BA850 (table 0x8D128C), `CRadar::LoadTextures` 0x5827D0 (table 0x8D0720) | – | list in §4.2 |
| UI-04 | data\fonts.dat | file missing (Crash); `[FONT_ID]` ∉ {0,1} (Crash: writes 210 bytes over `CFont::m_Color..Sprite` 0xC71A54..); `[PROP]` block with < 26 lines or a line with < 8 integers (Error: stale values); `[UNPROP]`/`[REPLACEMENT_SPACE_CHAR]` value not an int | Crash / Error | `CFont::LoadFontValues` 0x7187C0 (`bl` unchecked, `gFontData` 0xC718B0 × 2, stride 0xD2) | – | parse sections; both font ids present |
| UI-05 | gta3.img (or any scanned IMG) | `radar00.txd`..`radar143.txd` missing; a tile TXD with no texture | Warning (tile blank) | `CRadar::Initialise` 0x587FB0 (`FindTxdSlot("radar%02d")` → -1), `DrawRadarSection` 0x586180 (-1 skipped), `GetFirstTexture` 0x5861AF | – | 144 entries; first texture exists (vanilla 128×128 `radarNN`) |
| UI-06 | loadscs.txd / fronten*.txd | required names (§4.2) missing | Error (flat quad) | `LoadSplashes` 0x5900B0, `LoadAllTextures` 0x572EC0 | as UI-02 | names present |

---------------------------------------------------------------------------------------------------

## 6. Vanilla spot-check (D:\Grand Theft Auto San Andreas, scripts in the scratchpad: `gxtchk.py`, `scmchk.py`, `imgchk.py`, `txdnames.py`)

* `text\{american,french,german,italian,spanish}.gxt`: header `04 00 08 00`; TABL 127 entries (≤ 200),
  names ≤ 7 chars, MAIN first, MAIN offset == end of TABL (no name block); every table: TKEY size % 8 == 0,
  hashes strictly ascending, unique, every offset < TDAT size and NUL-terminated; MAIN 5427–5431 keys;
  `CDERROR` (0xEF128EC3) present in all five → GXT-01..09, 13 pass. (This install's american.gxt carries a
  Russian re-lettering; structure unchanged.)
* `data\script\main.scm` (3 079 599 bytes): seg targets 43808 / 53156 / 53720 / 55948, all ascending and
  < 200000; 389 object names (≤ 395, max 18 chars, entry 0 empty); 135 missions (≤ 200), MainScriptSize
  194146 == first mission offset (≤ 200000), largest mission 68439 (≤ 69000, matches offset differences),
  highest locals 964 (≤ 1024); seg3: 79 streamed scripts, names ≤ 16 chars, all 79 present in script.img and
  vice versa → SCM-02..08 pass.
* IMGs (`VER2`): gta3 16316 entries, gta_int 2484, carrec 426 (`.rrr` ≤ 475), script 79 (`.scm` ≤ 82),
  cutscene 634, player 542. No entry with bytes 6..7 ≠ 0, none with a late/missing dot, all names
  NUL-terminated, 64 `nodesNN.dat`. Duplicates: 4 `gta_int.img` TXDs shadowed by `gta3.img` (IMG-06 must be
  Info). Non-model DFFs: 392 (≤ 550). Largest streaming size 1263 sectors. gta.dat has 3 IMG lines, all
  before the first IPL → IMG-01..11 pass.
* `stream.ini`: every value key has a value; `dontbuildpaths` alone (no value needed) → IMG-12 passes.
* hud.txd: all 69 names incl. the 6 HUD sprites and 63 blips (case differences only); fonts.txd `font1`,
  `font2`; pcbtns.txd `up/down/left/right`; loadscs.txd `nvidia`, `eax`, `title_pc_US`, `loadsc0..14`;
  fronten1/2/3/_pc complete; radar00..radar143 present, each with one 128×128 texture → UI-01..06 pass.
* `data\fonts.dat`: `[TOTAL_FONTS] 2`, font ids 0 and 1, two 26×8 `[PROP]` blocks → UI-04 passes.

---------------------------------------------------------------------------------------------------

## 7. Open questions / not verified in this pass

* What the streaming thread does when `CdStreamRead` returns fewer bytes than requested (IMG-13) —
  `CStreaming::RetryLoadFile` / CD-error path not read; severity kept at Error.
* How `CClothesBuilder` reads `player.img` (its own `CDirectory` via `ReadDirFile`, `CdStreamRead` with
  `ms_clothesImageId << 24`) — the exact failure mode when the id is 0 (IMG-01) is inferred from the wrong
  archive being read, not traced to a specific crash site.
* Whether any consumer of a GXT string has a fixed buffer smaller than the longest vanilla string (521
  chars in MAIN) — `CMessages` buffers not read; no rule issued.
* `vehiclelights128`/`vehiclelightson128` NULL handling after `SetupCommonData` (UI-02 table, marked
  "not traced").
* Language codes other than 0..4 in the settings file leave the GXT filename buffer uninitialised
  (0x6A01D5 `ja 0x6A021B`) — settings file, not game data; no rule.
* The three trailing seg3 fields (`largest streamed size` at seg3+8 → `StreamedScripts+0xA40`) are stored
  but their consumer was not located; no rule.
