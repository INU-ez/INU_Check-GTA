# Text data files (gta.dat / IDE / IPL / bnry / zon / nodes / timecyc / water / plants / fxp) — gta_sa.exe 1.0 US

Sources: Ghidra dumps in `E:\RE\asm_map\<ADDR>.c/.asm` (plus `asm\`, `asm_skin\`), raw bytes via `readva.py`,
capstone disassembly (scratchpad `csdis.py`) for the handful of functions with no dump
(LinkLods 0x5B51E0, CTheZones::CreateZone 0x5728A0, AssignZoneInfoForThisZone 0x572180,
LoadTimeCyclesModifier 0x5B81D0, AddWaterLevelVertex 0x6E5A40, water block scan 0x6E7B30,
AddPolyToBlock 0x6E5750, CEntryExitManager::AddOne 0x43FA00, CGarages::AddOne 0x4471E0,
binary LoadCarGenerator 0x537990, CTheCarGenerators::CreateCarGenerator 0x6F31A0,
SetAtomicModelInfoFlags 0x5B3B20, CTxdStore::Initialise 0x731F20, CPathFind::LoadPathFindData body at
0x156F750). gta-reversed / plugin-sdk used only for names and for array sizes that I then verified by
address arithmetic (stated per item). Vanilla data read from `D:\Grand Theft Auto San Andreas\data`.

Every crash / garbage / silently-ignored claim cites the function and the unchecked operation.
Check ids: `DAT-nn`.

Legend: **crash** = access violation / stack smash; **garbage** = engine proceeds with wrong data;
**ignored** = engine drops the line/section with no message.

---------------------------------------------------------------------------------------------------

## 0. When each parser runs (relative to an .asi DllMain)

ASI loaders (dinput8/vorbisfile proxies) run the plugin's DllMain during process initialisation,
i.e. before WinMain. Everything below happens later, so a hook installed at DllMain sees all of it.

| phase (gta-reversed names, exe addresses verified by dumps) | parser(s) |
|---|---|
| RW init → `CGame::InitialiseCoreDataAfterRW` 0x5BFA90 | `CTimeCycle::Initialise` 0x5BBAC0 (`DATA\TIMECYC.DAT`) |
| `CGame::Initialise` 0x53BC80 → `Init1` 0x5BF840 | `CPlantMgr::Initialise` 0x5DD910 → `CPlantSurfPropMgr::LoadPlantsDat` 0x5DD3B0 (`DATA\PLANTS.DAT`); `Fx_c::Init` → `FxManager_c::LoadFxProject` 0x5C2420 (`models\effects.fxp`); `CIplStore::Initialise` 0x405EC0; `CStreaming::InitImageList` (IMG directories → `AddIplSlot` for every `*.ipl` entry) |
| `CFileLoader::LoadLevel` 0x5B9030 (`DATA\DEFAULT.DAT`, then `DATA\GTA.DAT`) | IDE (`LoadObjectTypes` 0x5B8400), text IPL/ZON (`LoadScene` 0x5B8700: inst/zone/cull/occl/grge/enex/pick/cars/jump/tcyc/auzo), COLFILE/TEXDICTION/MODELFILE/HIERFILE/IMG |
| end of `LoadLevel` (first IPL line triggers `CStreaming::Init2`; after the loop `CIplStore::LoadAllRemainingIpls` 0x405780) | streamed IPLs from IMG (`CIplStore::LoadIpl` 0x406080 / `LoadIplBoundingBox` 0x405C00, `bnry` or text) |
| `Init2` 0x5BA1A0 | `CWaterLevel::WaterLevelInitialise` 0x6EAE80 (`DATA\WATER.DAT` / `WATER1.DAT`) |
| runtime streaming (`CStreaming::ConvertBufferToObject` 0x40C6B0) | streamed IPL (ids 25255..25510), `nodesNN.dat` (ids 25511..25574 → `CPathFind::LoadPathFindData` 0x4529F0) |

Resource id ranges verified in 0x40C6B0 (`cmp esi, 0x4E20/0x61A8/0x62A7/0x63A7/0x63E7/0x649B`):
TXD 20000..24999, COL 25000..25254, IPL 25255..25510 (256 slots), DAT 25511..25574 (64 path areas),
IFP 25575..25754, RRR 25755+. (The task text's "IPL 25755+" is the RRR range; streamed IPL slot ids
start at 25255 = 0x62A7.)

---------------------------------------------------------------------------------------------------

## 1. Common line reader — `CFileLoader::LoadLine`

### 1.1 File variant 0x536F80 (gta.dat, IDE, IPL from disk, water.dat, timecyc.dat, plants.dat, ...)

```
CFileMgr::ReadLine(file, ms_line /*0xB71848*/, 0x200)   ; = fgets(buf, 512, FILE*)  (0x5389B0 → 0x823798)
for each byte until NUL:  if (c < 0x20 || c == ',') c = ' '
skip leading bytes <= 0x20
return pointer (NULL only when fgets fails = EOF)
```

* `ms_line` is 512 bytes. A physical line longer than 511 characters is returned as **two lines**
  (fgets stops at 511) — the tail is parsed as a separate record (**DAT-01**, garbage).
* Every byte `< 0x20` (TAB, CR, LF, ...) and every `,` becomes a space. So comma or whitespace
  separation is equivalent; CRLF/LF do not matter. **`#` is only a comment when it is the first
  non-blank character** (each caller tests `line[0] == '#'`). `//` and `;` are **not** comments for
  IDE/IPL/gta.dat (`//` only in timecyc, `;` only in water.dat/plants.dat) (**DAT-02**).
* A blank line comes back as a pointer to `'\0'`; callers test `line[0] == 0`.
* `CFileMgr::OpenFile` result is never checked by `LoadObjectTypes`/`LoadScene`/`LoadLevel`; on a
  missing file `fgets(NULL)` is executed → CRT crash (**DAT-03**).

### 1.2 Memory variant 0x536FE0 (streamed IPL text, from the streaming buffer)

Copies until `\n`/NUL/`bytesLeft==0` into `PC_Scratch` 0xC8E0C8 (16 KB, gta-reversed name), same
character replacement and trimming. A line ≥ 16 KB would overflow the global — practically irrelevant
(**DAT-40**, informational).

---------------------------------------------------------------------------------------------------

## 2. gta.dat — `CFileLoader::LoadLevel` 0x5B9030

Grammar (per line, after LoadLine): first-non-blank `#` → comment. Otherwise keyword matched with
`strncmp(keyword, line, len(keyword))` **prefix-only, case-sensitive**, and the argument taken at a
**fixed offset** (exactly one separator character assumed):

| keyword | arg offset | action |
|---|---|---|
| `EXIT` | – | stop reading |
| `TEXDICTION` | +11 | copy to `local_40[64]`, `LoadTexDictionary`, textures merged into the generic TXD |
| `IMG` | +4 | `CStreaming::AddImageToList(path,1)` unless path == `MODELS\GTA_INT.IMG` (already added) |
| `COLFILE` | +10 (`COLFILE 0 path`) | `LoadCollisionFile(path, 0)` |
| `MODELFILE` | +10 | `LoadAtomicFile` |
| `HIERFILE` | +9 | `LoadClumpFile` |
| `IDE` | +4 | `LoadObjectTypes(path)` |
| `IPL` | +4 | copy to `local_40[64]`; **first** IPL line runs `MatchAllModelStrings`, `CObjectData::Initialise(object.dat)`, `CVehicleModelInfo::SetupCommonData`, `CStreaming::Init2`, `CColStore::LoadAllBoundingBoxes` and vtable+0x34 (`ConvertAnimFileIndex`) for all 20000 modelinfo slots; then `LoadScene(path)` |
| `SPLASH` | – | no-op (strncmp result discarded) |
| anything else / blank | – | ignored |

Checks:

* **DAT-03** — `IDE`/`IPL` path that does not open (missing file, extra separator, wrong case on a
  case-sensitive FS) → `OpenFile` returns 0 → `LoadLine(0)` → `fgets(NULL)` → **crash** in the CRT
  (`LoadObjectTypes` 0x5B8400 and `LoadScene` 0x5B8700 never test the handle).
* **DAT-04** — every `IDE` line must precede the first `IPL` line: `LoadLevel` runs
  `CStreaming::Init2` (IMG directory scan that resolves DFF names to model ids) only once, on the first
  IPL (`bVar2` latch). Models defined by an IDE listed after an IPL get a modelinfo but never a CD
  position → they never stream, instances of them are invisible (**garbage/ignored**, no message).
* **DAT-05** — `TEXDICTION`/`IPL` argument ≥ 64 chars overflows `local_40[64]` (0x5B9030, unbounded
  byte copy loop) → **crash**; `IDE` argument ≥ 256 chars overflows `local_100[256]` in
  `LoadObjectTypes` (unbounded copy) → **crash**.

---------------------------------------------------------------------------------------------------

## 3. IDE — `CFileLoader::LoadObjectTypes` 0x5B8400

Section state machine: with no section open, the first 4 chars of the line are compared against
`objs`, `tobj`, `weap`, `hier`, `anim`, `cars`, `peds`, `path`, `2dfx`, `txdp` (exact, lower-case).
`end` (3 chars) closes a section. An unknown header is simply ignored (the following lines are then
each re-tested as headers until a known one appears) — silent (**DAT-18** analogue).
Every per-line loader receives the LoadLine buffer.

### 3.1 `objs` — `LoadObject` 0x5B3C60

```
n = sscanf(line, "%d %s %s %f %d", &id, name[24], txd[24], &dd, &flags)
if (n != 5 || dd < 4.0f /*0x858B90*/) {
    if (sscanf(line, "%d %s %s %d", &id, name, txd, &count) != 4) return -1;   // line dropped
    count==1: sscanf("%d %s %s %d %f %d",       ... &dd, &flags)
    count==2: sscanf("%d %s %s %d %f %f %d",    ... &dd, &dd2, &flags)
    count==3: sscanf("%d %s %s %d %f %f %f %d", ... &dd, &dd2, &dd3, &flags)
    (other count: id/name/txd from the 4-field parse, dd/flags keep whatever the first sscanf left)
}
mi = (flags & 0x1000) ? AddDamageAtomicModel(id) : AddAtomicModel(id)
mi->m_fDrawDistance = dd; m_nKey = GetUppercaseKey(name); SetTexDictionary(txd); SetAtomicModelInfoFlags(flags)
```

Only the first draw distance is stored (dd2/dd3 discarded — the "multi-mesh" columns are legacy).

* **DAT-08** — draw distance `< 4.0` makes the engine re-parse the SA 5-field line as the legacy
  count form: `"%d %s %s %d"` reads the integer part of the draw distance as *count* (e.g. `3.5` →
  count 3 → third format → dd = 0.5, flags = 0 (the `%f` eats "0"), dd3 uninitialised). Silent
  **garbage**. Exporter rule: draw distance ≥ 4.0 in SA objs/tobj lines (legacy form only with an
  integer count 1..3 in column 4).
* **DAT-08b** — objs line with < 4 tokens (or non-numeric id) → both sscanf fail → line
  **silently dropped** (`return -1`).
* **DAT-09** — `%s` targets are 24-byte stack buffers with no width: model name ≥ 24 chars
  overruns into the txd buffer: the name's NUL lands inside `txd[]` and is then overwritten by the
  txd parse, so `GetUppercaseKey(name)` hashes `name+txd` → model never matches its DFF → never
  streams (**garbage/ignored**). txd ≥ 24 chars overruns the return address (`local_18` is the last
  local) → **crash on return**. Same layout in `LoadTimeObject`, `LoadClumpObject`,
  `LoadAnimatedClumpObject`, `LoadVehicleObject`, `LoadPedObject` (table §3.11).
* **DAT-06** — id is never range-checked. `CModelInfo::Add*Model` (0x4C6620..0x4C67A0) do
  `ms_modelInfoPtrs[id] = slot` on the 20000-entry array at 0xA9B0C8 (end 0xAAE948, the bound used by
  LoadLevel's post-loop). id < 0 or ≥ 20000 writes outside → memory corruption (**garbage/crash**).
  Ids 20000+ collide with the streaming resource ranges (§0).
* **DAT-07** — duplicate id: a second `Add*Model` overwrites the pointer and consumes a new store slot
  (first modelinfo leaked, its `ms_modelInfoPtrs` entry gone) — silent. Stores have **no capacity
  check** (`Add*Model` just increments the count): atomic 14000 (count 0xAAE950, stride 0x20;
  0xAAE954 + 14000·0x20 = 0xB1BF54 ✓), damage-atomic 70 (0xB1BF58, 0x24), time 169 (0xB1C960, 0x24 →
  0xB1E128 ✓), clump (hier + anim) 92 (0xB1E958, 0x24 → 0xB1F64C ✓), vehicle 212 (0xB1F650, 0x308 →
  0xB478F4 ✓), ped 278 (0xB478F8, 0x44), weapon 51 (gta-reversed, not verified). Exceeding a store
  overwrites the next store's counter/objects (**garbage**).
* **DAT-14** — flags accepted (from `SetBaseModelInfoFlags` 0x5B3AD0 + `SetAtomicModelInfoFlags`
  0x5B3B20; other bits ignored):

| IDE flag | effect |
|---|---|
| 0x1 | `bIsRoad` (+0x13 bit0, wet-road reflection) |
| 0x4 or 0x8 (`&0xC`) | `bDrawLast` (alpha, drawn after opaque) |
| 0x8 | `bAdditive` |
| 0x40 | `bDontWriteZBuffer` |
| 0x80 | `bDontCastShadowsOn` |
| 0x200 | special type 4 = GLASS_TYPE_1 |
| 0x400 | special type 5 = GLASS_TYPE_2 |
| 0x800 | special type 7 = GARAGE_DOOR |
| 0x1000 | modelinfo goes to the **damage-atomic** store (breakable/damaged variant) |
| 0x2000 | special type 1 = TREE |
| 0x4000 | special type 2 = PALM |
| 0x8000 | `bDontCollideWithFlyer` |
| 0x80000 | special type 10 |
| 0x100000 | special type 6 = TAG |
| 0x200000 | **clears** `bIsBackfaceCulled` (flag = "disable backface culling") |
| 0x400000 | special type 11 = BREAKABLE_STATUE |

### 3.2 `tobj` — `LoadTimeObject` 0x5B3DE0

`"%d %s %s %f %d %d %d"` (id, name[24], txd[24], dd, flags, timeOn, timeOff); same `< 4.0` legacy
fallback with `"%d %s %s %d"` and `+ %f{1,2,3} %d %d %d`. `AddTimeModel` (169 slots), times stored as
bytes at `CTimeModelInfo+0x20/+0x21`; `FindOtherTimeModel(name)` links the `_nt`/`_dy` partner.
Dropped if the fallback yields ≠ 4. Same DAT-06/07/08/09 conditions. timeOn/timeOff are `uint8` hours.

### 3.3 `hier` — `LoadClumpObject` 0x5B4040

`"%d %s %s"` exactly 3 else dropped. `AddClumpModel` (clump store, 92). Sets the generic
`CColModel` at 0x968A00.

### 3.4 `anim` — `LoadAnimatedClumpObject` 0x5B40C0

`"%d %s %s %s %f %d"` (id, name[24], txd[24], animName[16], dd (default 2000.0), flags) — exactly 6
else dropped (returns -1). `AddClumpModel`; `SetBaseModelInfoFlags(flags)`; flag 0x20 → +0x13 bit2;
animName ≠ `"null"` (5-byte cmpsb at 0x5B41A2) → `+0x13 |= 1` (animated → `CAnimatedBuilding` for
its instances).
* **DAT-10** — animName is a **16-byte** buffer at ESP+0xC, directly below the model-name buffer
  (ESP+0x1C, from the pushes at 0x5B40E5..0x5B410A): an anim name ≥ 16 chars overwrites the
  already-parsed model name → wrong hash, model never streams (**garbage**). Also exactly 6 fields or
  the line is dropped.

### 3.5 `weap` — `LoadWeaponObject` 0x5B3FB0

`"%d %s %s %s %d %f"` (id, name[24], txd[24], anim[16], flags, dd). **No return check**; `id` is
uninitialised stack → **DAT-11**: a malformed weap line calls `AddWeaponModel(garbage)` → OOB write
→ **crash/garbage**.

### 3.6 `cars` — `LoadVehicleObject` 0x5B6F30

`"%d %s %s %s %s %s %s %s %d %d %x %d %f %f %d"` = id, model[24], txd[24], type[12], handling[16],
gameName[32], anims[16], class[16], frequency, flags, compRules(hex), wheelId, wheelScaleFront,
wheelScaleRear, upgradeClass (15 fields; buffer sizes from the stack layout at 0x5B6F66..0x5B6FC1).
No return check; `id` defaults to -1 → `AddVehicleModel(-1)` writes `ms_modelInfoPtrs[-1]` (0xA9B0C4)
on a malformed line (**DAT-11**, garbage). `type` must be one of `car mtruck quad heli plane boat
train f_heli f_plane bike bmx trailer` (exact), `class` one of `normal poorfamily richfamily
executive worker big taxi moped motorbike leisureboat workerboat bicycle ignore`; unknown strings leave
the constructor defaults (**DAT-13**, silent). `gameName`: `_` → space, then `strncpy(+0x32, 8)`.
Vehicle txd parent set to the `vehicle` TXD slot.

### 3.7 `peds` — `LoadPedObject` 0x5B7420

`"%d %s %s %s %s %s %x %x %s %d %d %s %s %s"` = id, model[24], txd[24], pedType[24], stats[24],
animGroup[24], carsCanDrive(hex), flags(hex), animFile[16], radio1, radio2, audioPedType[20],
voice1[60], voice2[60] (sizes from the pushes at 0x5B742A..0x5B748E; frame 0x128).
No return check, `id` default -1 (**DAT-11**). `pedType`/`stats` resolved by name
(`CPedType::FindPedType`, `CPedStats::GetPedStatType`).
* **DAT-12** — `animGroup` is looked up linearly against `CAnimManager::GetAnimGroupName(i)`; if not
  found `m_nAnimType = ms_numAnimAssocDefinitions` (one past the end, 0x5B7568) → indexing the
  assoc-group array at spawn → **crash** when such a ped is created. Must be a group from `animgrp.dat`.

### 3.8 `txdp` — `LoadTXDParent` 0x5B75E0

`"%s %s"` (child[32], parent[32]); both slots created if absent; child slot `+6 = parent index`.
* **DAT-15** — `CTxdStore::AddTxdSlot` 0x731C80 does `p = pool->New(); *p = 0` with **no NULL
  check**; the pool is 5000 entries (`CTxdStore::Initialise` 0x731F20: `push 0x1388, "TexDictionary"`).
  More than 5000 distinct TXD names across IDEs + IMG entries → **crash** (NULL write).

### 3.9 `2dfx` (IDE section) — `Load2dEffect` 0x5B7670

`"%d %f %f %f %d"` = modelId, x, y, z, type; then per type:
0 light `"... %d %d %d %d"` + two `"quoted"` names scanned for `"` with **no bound** + 13 more
numbers; 1 particle `"%s"` copied straight into the effect (24 B); 3 ped attractor 18 fields;
5 sun-glare 38 fields; 6 enter/exit 16; 7 street sign 15 (four `%s` of 32); 8 trigger point 6;
9 cover point 8; 10 escalator 15.
* **DAT-16** — `mi = ms_modelInfoPtrs[modelId]` unchecked, then `CBaseModelInfo::Add2dEffect(mi, fx)`
  → modelId undefined (NULL) or out of range → **crash**. The 2dfx store is 100 entries (0xB4C2D8,
  gta-reversed) with **no check** (`count++` at 0x5B76B8). Effects for one model must be
  **consecutive** lines (Add2dEffect stores first-index + count). For type 0 a missing closing `"`
  makes the scan run past the line buffer (**garbage/crash**). (The addon writes 2dfx into the DFF
  plugin — keep the IDE section unused.)

### 3.10 `path` (IDE) — `LoadPathHeader` 0x5B41C0 / `LoadPedPathNode` 0x5B41F0 / `LoadCarPathNode` 0x5B4380

Header `"%d %d %s"` (type, id, name[84]); then 12 node lines
(`"%d %d %d %f %f %f %f %d %d %d %d %f %d"` for cars). The node sinks `CPathFind::StoreNodeInfoCar`
0x44D2C0 / `StoreNodeInfoPed` 0x44D2F0 are `ret 0x44` stubs (bytes `C2 44 00`) → **DAT-17**: the IDE
`path` section is parsed and **discarded** in SA (paths come only from `nodes*.dat`).

### 3.11 Buffer-size table for `%s` conversions (all unbounded)

| loader | buffers |
|---|---|
| objs/tobj | name 24, txd 24 (txd is the last local → ≥ 24 chars = return address) |
| hier | name 24, txd 24 |
| anim | name 24, txd 24 (last local), anim 16 (below name) |
| weap | name 24, txd 24, anim 16 |
| cars | model 24, txd 24 (last local), type 12, handling 16, gameName 32, anims 16, class 16 |
| peds | model 24, txd 24, pedType 24, stats 24, animGroup 24, animFile 16, audioPedType 20, voice1 60, voice2 60 |
| txdp | 32, 32 |
| inst (IPL) | name 24 |
| zone | name 24, infoName 12 |
| grge | name 8 |
| enex | name 32 |
| auzo | name 16 |

---------------------------------------------------------------------------------------------------

## 4. Text IPL (gta.dat `IPL` lines, also `.zon`) — `CFileLoader::LoadScene` 0x5B8700

Section headers (first 4 chars): `inst`(2) `mult`(3) `zone`(4) `cull`(5) `path`(1) `occl`(6)
`grge`(8) `enex`(9) `pick`(10) `cars`(11) `jump`(12) `tcyc`(13) `auzo`(14); `end` closes.
Section 7 (the old IPL path nodes) is unreachable.

* **DAT-18** — unknown section header → no state change; its lines are re-tested as headers and
  silently ignored until a known header appears.
* **DAT-19** — `mult` is accepted but has no case → its lines are **ignored**. `path`: sets state 1,
  and the very next data line hits `if (state == 1) break;` (0x5B8A0A) → the **rest of the file is
  not parsed** (silent). Never emit a `path` section into an SA IPL.
* After the loop: `gCurrIplInstancesCount` entities are copied into a fresh
  `CIplStore::GetNewIplEntityIndexArray` array (used by streamed IPLs of the same basename for LOD
  lookups), `SetupRelatedIpls`, then `LinkLods` 0x5B51E0, `RemoveRelatedIpls`.

### 4.1 `inst` — `LoadObjectInstance` (text) 0x538690 → (struct) 0x538090

Text: `sscanf("%d %s %d %f %f %f %f %f %f %f %d", &id, name[24], &interior, &x,&y,&z, &rx,&ry,&rz,&rw, &lod)`
— **no return check**; the struct is `CFileObjectInstance {float x,y,z, rx,ry,rz,rw; int id; int interior; int lod}` (40 B),
exactly the `bnry` record. `name` is **not used** (SA resolves by id only; a name/id mismatch is silent).

Struct loader 0x538090:

```
mi = ms_modelInfoPtrs[id]                       ; NO range check (DAT-21)
if (!mi) return NULL                            ; undefined id → NULL (callers do not check, DAT-21)
if (mi->m_nObjectInfoIndex == -1)               ; not in object.dat → building
    e = (mi is clump && +0x13&1) ? new CAnimatedBuilding : new CBuilding
    if (+0x12 & 0x10 /*bDontCastShadowsOn*/) e->flags |= 0x10000
    if (mi->m_fDrawDistance < 2.0f /*0x858CA0*/) e->bIsVisible = 0        ; (DAT-25b)
else
    e = new CDummyObject; if (CGlass::IsObjectGlass(e) && specialType != GLASS_TYPE_2) e->bIsVisible = 0
if (|rx| > 0.05 || |ry| > 0.05 || ((interior & 0x200) && rx != 0 && ry != 0))      ; 0x858C28 = 0.05
    quat = (-rx,-ry,-rz,rw); AllocateStaticMatrix; SetRotate(quat)          ; full rotation (conjugate)
else
    heading = (rz >= 0) ? -2*acos(rw) : 2*acos(rw); SetRotateZOnly(heading)  ; Z-only (DAT-23), 0x53823C/0x538269
position = (x,y,z)
interior & 0x100 → bUnimportantStream; 0x400 → bUnderwater; 0x800 → bTunnel; 0x1000 → bTunnelTransition
e->m_nAreaCode = (uint8)interior; e->m_pLod = (CEntity*)lod   ; raw index, resolved later
if (id == 0xFFFF /*DAT_008CD6B4*/) SetMatrixForTrainCrossing
if (mi->m_pColModel && !(colmodel +0x29 bit0)) e->bUsesCollision = 0
```

* **DAT-20** — fewer than 11 tokens → the unfilled locals are stack garbage (lod in particular) →
  `LinkLods` dereferences `gCurrIplInstances[garbage]` → **crash** (see DAT-22). No field-count check.
* **DAT-21** — model id: (a) not defined by any IDE → `ms_modelInfoPtrs[id] == NULL` → returns NULL;
  `LoadScene` stores NULL in `gCurrIplInstances[]` and `LinkLods` does `mov ecx,[eax+0x30]` with
  eax = 0 (0x5B51F7) → **crash**. In streamed IPLs (`CIplStore::LoadIpl` 0x4061FC / 0x406302 and
  `LoadIplBoundingBox`) the result is written to immediately (`mov [esi+0x2e], dl`) → **crash**.
  (b) id ≥ 20000 or < 0 → reads outside `ms_modelInfoPtrs` → garbage pointer → vtable call → **crash**.
* **DAT-22** — `lod`: text IPL → `LinkLods` 0x5B51E0 does `e->m_pLod = gCurrIplInstances[lod];
  m_pLod->m_nNumLodChildren++` (byte at +0x34) with **no range check** (0x5B51FF..0x5B5209): lod must
  be -1 or a valid index into *this file's* inst list; an index ≥ count reads past the array →
  garbage pointer → **crash**; pointing at an instance whose own model was undefined (NULL) →
  **crash**. Streamed IPL → the index addresses `IplEntityIndexArrays[relatedIpl]` = the inst list of
  the **gta.dat text IPL with the same basename** (0x406201: `mov eax,[ecx+eax*4]`); if that text IPL
  does not exist `local_4 == 0` and the engine reads address `lod*4` → **crash**; index beyond that
  array → garbage → **crash**. `m_nNumLodChildren` is a `uint8` → more than 255 children of one LOD
  wrap to 0 (garbage big-building logic).
* **DAT-23** — rotation: quaternion expected normalised. Z-only path uses `acos(rw)` (0x822380) →
  `|rw| > 1` → NaN heading → NaN matrix (invisible / broken collision, silent). Full path uses
  `CMatrix::SetRotate` on the **conjugate** (x,y,z negated) — the file stores the inverse rotation.
  Selection rule: |rx| ≤ 0.05 and |ry| ≤ 0.05 (and not forced by interior-flag 0x200) → the Z-only
  path **ignores rz's magnitude** and uses only sign(rz) with rw.
* **DAT-24** — interior field: bits 0..7 area code (stored as byte, > 255 truncated); 0x100
  unimportant-stream, 0x200 force full rotation, 0x400 underwater, 0x800 tunnel, 0x1000 tunnel
  transition; other bits ignored.
* **DAT-25** — position is never range-checked. `CEntity::Add` 0x5347D0 clamps the bounding rect to
  ±3000 (`0x859A90/0x859A94`) and sector indices to 0..119 (`0x76 <` clamp), so out-of-world instances
  do not crash — they land in the edge sectors (streaming/culling wrong, silent). **DAT-25b** —
  instances of a model with draw distance < 2.0 are created invisible.
* **DAT-26** — `gCurrIplInstances` 0xBCC0E0 is a fixed array (4096 per gta-reversed, size not
  verified in the exe); `LoadScene` increments with no bound → > 4096 inst lines in one text IPL
  overflow (**garbage**).
* Instances whose model draw distance × `0xB6F118` (LOD multiplier) > 300 (`0x858FD8`) and with no LOD
  children are turned into "big buildings" by `LinkLods` (0x5B5266..0x5B5289) — informational.

### 4.2 `zone` — `LoadZone` 0x5B4AB0 → `CTheZones::CreateZone` 0x5728A0

`sscanf("%s %d %f %f %f %f %f %f %d %s", name[24], &type, &x1,&y1,&z1, &x2,&y2,&z2, &level, info[12])`
— **only when the result is exactly 10** is `CreateZone` called (**DAT-27**).

`CreateZone`: min/max swapped per axis if needed; both names upper-cased **in place** and copied with
`strncpy(dst, src, 7); dst[7] = 0` (0x572935..0x57294B) → **7 characters max, silently truncated**.
type < 0 → return; type 0 or 1 → `NavigationZoneArray[TotalNumberOfNavigationZones++]` (380 entries
at 0xBA3798, 32 B; 0xBA3798 + 380·32 = 0xBA6718 = `m_CurrLevel` ✓) then `AssignZoneInfoForThisZone`
0x572180 (ZoneInfoArray 380 × 17 B at 0xBA1DF0 → 0xBA372C ✓); type 3 → `MapZoneArray[TotalNumberOfMapZones++]`
(39 entries at 0xBA1908 → 0xBA1DE8 = `TotalNumberOfZoneInfos` ✓); type 2 or > 3 → **silently dropped**.
Coordinates are `_ftol`'d into int16 (values beyond ±32767 wrap). Level stored as byte. **No count
checks** → the 381st navigation zone overwrites `m_CurrLevel`, the 40th map zone overwrites the
zone-info counter (**garbage**).

* **Exporter finding**: `INU_tools/core/ipl.py::_format_zone_line` (and `IplZone`) emit **9 fields**
  (no 10th GXT/info column) → SA drops every such zone (`iVar1 == 10` test). `core/zon.py` is correct
  (10 fields). Fix ipl.py or route zones through zon.py.
* **DAT-27b** — `info[12]`: an info name ≥ 12 chars overruns the already-parsed x2/y2/z2 (garbage
  bounds), ≥ 36 chars corrupts the zone name, ≥ 60 the return address (**crash**).

### 4.3 `cull` — `LoadCullZone` 0x5B4B40 → `CCullZones::AddCullZone` 0x72DF70 / `AddMirrorAttributeZone` 0x72DC10

`sscanf("%f %f %f %f %f %f %f %f %f %d %f %f %f %f")` — if exactly 14 → mirror zone (72 entries at
0xC815C0, 24 B, **no check**); else `sscanf("%f %f %f %f %f %f %f %f %f %d %d")` (no return check;
11th value ignored) → `AddCullZone(center, f4..f9, (uint16)flags)`: flags with 0x80 or 0x800 → also a
tunnel zone (40 entries at 0xC81C80, `AddTunnelAttributeZone` 0x72DB50, **no check**), then
`flags &= 0xF77F`; remaining flags ≠ 0 → attribute zone (1300 entries at 0xC81F50 × 18 B → 0xC87AB8 ✓,
**no check**); remaining flags == 0 → **dropped silently** (**DAT-28**). Extents are `_ftol`'d to int16.

### 4.4 `occl` — `LoadOcclusionVolume` 0x5B4C80 → `COcclusion::AddOne` 0x71DCD0

`sscanf("%f %f %f %f %f %f %f %f %f %d ", &midX,&midY,&bottomZ, &widthX,&widthY,&height, &rotX,&rotY,&rotZ, &flags)`
(rotX/rotY/rotZ/flags default 0, no return check); centreZ = bottomZ + height·0.5. `isInterior` = the
IPL **file name** has `int` at `[len-7..len-5]` (i.e. ends with `int.ipl`) (**DAT-29**). `AddOne`: if
two of (widthX, widthY, height) truncate to 0 → dropped; angles wrapped into [0,360) by ±360 loops
(`0x859E2C`); interior occluders limited to 40 (`DAT_00C73CC4 < 0x28`), map occluders to 1000
(`DAT_00C73F98 < 1000`) — when full the volume is **silently dropped** (this one is checked).
Values stored as int16/int8.

### 4.5 `grge` — `LoadGarage` 0x5B4530 → `CGarages::AddOne` 0x4471E0

`sscanf("%f %f %f %f %f %f %f %f %d %d %s", x1,y1,z1, frontX,frontY, x2,y2,z2, &flags,&type, name[8])`
— exactly 11 or dropped. **DAT-31** — `name` is an **8-byte** buffer directly below the return address:
a name of 8+ characters (NUL at index 8) overwrites the return address → **crash**; ≤ 7 chars only.
`AddOne` indexes `aGarages[NumGarages]` (0x96C048, 0xD8 each, 50 max) with **no check** (0x447288).

### 4.6 `enex` — `LoadEntryExit` 0x5B8030 → `CEntryExitManager::AddOne` 0x43FA00

`sscanf("%f %f %f %f %f %f %f %f %f %f %f %d %d %s %d %d %d %d", x,y,z, enterAngle, sizeX,sizeY,sizeZ, exitX,exitY,exitZ, exitAngle, &interior, &flags, name[32], &sky, &numPeds(=2), &timeOn(=0), &timeOff(=24))`
— no return check. The name: `strrchr(name,'"')` → cut there and pass `name+1` (so `"NAME"` is
expected; a name **containing spaces** is split by `%s` and the numbers after it are not parsed →
defaults used, silent). Pool of 400 (`new CEntryExitsPool(400)`): `AddOne` returns 0 when `New()`
fails (0x43FA2C..0x43FA33) and the loader then ORs the flags into entry **0** (**DAT-30**, garbage).
Name ≥ 32 chars → stack overflow (**crash**).

### 4.7 `pick` — `LoadPickup` 0x5B47B0

`"%d %f %f %f"` exactly 4; the pickup id is mapped through the switch at 0x5B480A (ids 4..6, 9..0x37
with holes 7, 8, 0x1E, 0x2A); unmapped ids → **dropped silently** (**DAT-33**).

### 4.8 `cars` — `LoadCarGenerator` 0x5B4740 → struct loader 0x537990 → `CTheCarGenerators::CreateCarGenerator` 0x6F31A0

`"%f %f %f %f %d %d %d %d %d %d %d %d"` exactly 12 (x,y,z, angle(deg), model, col1, col2, flags,
alarm, doorLock, minDelay, maxDelay) into 12 dwords = the 48-byte `bnry` record; the struct loader
reads `+0x10 model (int32)`, `+0x14 col1 (int16)`, `+0x18 col2 (int16)`, `+0x1C flags (byte: bit0
forceSpawn, bit1 ignorePopulationLimit)`, `+0x20 alarm (byte)`, `+0x24 doorLock (byte)`, `+0x28
minDelay (u16)`, `+0x2C maxDelay (u16)`, angle / 57.2957764 (`0x859878`) → radians.
`CreateCarGenerator`: model must be -1 or in [400..630] (`cmp ecx,0x276`) else **dropped** (returns
-1); 500 generators (0xC27AED stride 0x20 → 0xC2B96D ✓) — when full, **dropped** (**DAT-34**, both
checked).

### 4.9 `jump` — `LoadStuntJump` 0x5B45D0 → `CStuntJumpManager::AddOne` 0x49CB40

`"%f"×15 + "%d"` exactly 16 or dropped; pool `STUNT_JUMP_COUNT` — `AddOne` returns without adding
when `New()` is NULL (**dropped silently**, **DAT-32**).

### 4.10 `tcyc` — `LoadTimeCyclesModifier` 0x5B81D0 → `CTimeCycle::AddOne` 0x55FF40

`sscanf("%f %f %f %f %f %f %d %d %f %f %f %f", x1,y1,z1, x2,y2,z2, &farClip, &extraColor, &strength, &falloff(=100), &unused(=1), &lodDistMult(=1))`;
if fewer than 12 → `lodDistMult = unused` (0x5B823C). `AddOne` writes `m_aBoxes[m_NumBoxes++]`
(0xB7C550, 0x28 each, **20 max** per gta-reversed; **no check** → the 21st box runs into the following
globals, **garbage**) (**DAT-35**). strength × 0.01 (`0x858C58`), farClip int16, lodDistMult `_ftol` → byte.

### 4.11 `auzo` — `LoadAudioZone` 0x5B4D70 → `CAudioZones::RegisterAudioBox` 0x508240 / `RegisterAudioSphere` 0x5081C0

`"%s %d %d %f %f %f %f %f %f"` (name[16], id, switch, box) if exactly 9 → box; else
`"%s %d %d %f %f %f %f"` → sphere (no return check). Box table 158 × 24 B at 0xB6DCD0 (→ 0xB6EBA8 ✓ =
sphere table), sphere table 3 × 28 B; **no count checks**. The name is copied unbounded into an
**8-byte** field before the numeric fields are written → names of 8..15 chars lose their NUL
(garbage name), ≥ 16 chars overflow the loader's stack buffer (**DAT-36**). Coordinates → int16.

---------------------------------------------------------------------------------------------------

## 5. Streamed IPL — `CIplStore`

### 5.1 Slots — `Initialise` 0x405EC0 / `AddIplSlot` 0x405AC0

Pool `"IPL Files"` = 256 slots (`push 0x100`), slot 0 = `generic`; streamed ids 25255 + slot.
`IplDef` (0x34 B): `CRect bb (0x00)`, `char name[18] (0x10)`, `int16 firstBuilding (0x22, 0x7FFF)`,
`lastBuilding (0x24, 0x8000)`, `firstDummy (0x26)`, `lastDummy (0x28)`, `staticIdx (0x2A, -1)`,
`isInterior 0x2C`, `loaded 0x2D`, `loadRequested 0x2E`, `disableDynamicStreaming 0x2F`,
`ignoreWhenDeleted 0x30`, `isLarge 0x31`.
* **DAT-39** — `AddIplSlot` copies the name with an unbounded byte loop into `name[18]` **after**
  the int16 fields were initialised (0x405AC0: the fields at 0x22..0x31 are written, then the copy).
  IMG entry names are up to 23 chars + `.ipl` → a streamed IPL basename ≥ 18 chars overwrites
  firstBuilding..staticIdx (0x2A = the related-IPL entity-array index used for LOD lookups) →
  **garbage/crash**. Keep streamed IPL basenames ≤ 17 characters.
* `AddIplSlot` uses `CPool::New` with no NULL check → more than 255 streamed IPL files → **crash**.

### 5.2 `LoadIpl` 0x406080 (second pass) and `LoadIplBoundingBox` 0x405C00 (first pass)

`LoadIpl` is used when the slot bounding box is already valid (`bb.left <= bb.right && bb.bottom <=
bb.top`), otherwise `LoadIplBoundingBox` runs (same parsing, additionally accumulates the bounding box,
sets `isLarge` when any model's draw distance > 150 (`0x858A28`), then enlarges the box by 200 / 350
(`0x858A48/0x858A4C`) and inserts it in the quadtree).

`bnry` layout (`tBinaryIplFile`, 76 B; offsets 0x04, 0x14, 0x1C, 0x3C verified in 0x406080):

```
+0x00 "bnry"
+0x04 numInst  +0x08 numCull  +0x0C numGarage  +0x10 numEnex  +0x14 numCarGen  +0x18 numPickup   (u32 each)
+0x1C offInst,+0x20 sizeInst  +0x24/+0x28 cull  +0x2C/+0x30 garage  +0x34/+0x38 enex  +0x3C offCarGen,+0x40 size  +0x44/+0x48 pickup
records: CFileObjectInstance 40 B (7 floats, int id, int interior, int lod) @ offInst; CFileCarGenerator 48 B @ offCarGen
```

* **DAT-37** — only `inst` and `cars` are consumed; the cull/garage/enex/pickup counts are ignored.
  `numInst` and `numCarGen` are read as **int16** (`movsx`, 0x4061C3 / 0x40623F), so a count ≥ 32768
  is negative → the section is skipped. Offsets are relative to the file start, added in place
  (`p+0x1C += p`) with **no bounds check** against `dataSize` → wrong offsets read the streaming
  buffer beyond the file (**garbage/crash**). The record count must fit the file (no check).
* **DAT-38** — text streamed IPL: only the `inst` section is parsed (`LoadIpl` 0x4062A6..: looks for
  `inst` / `end`); any other section is **ignored**.
* DAT-21/DAT-22 apply (NULL result written to at 0x4061FC / 0x406302 → **crash**; lod index resolved
  through the related text IPL's entity array).
* After each instance: `entity->m_nIplIndex = slot` (byte at +0x2E), `Add()` to the world,
  `IncludeEntity(slot, e)`; `LoadIplBoundingBox` also appends to `ppCurrIplInstance` when non-NULL.

---------------------------------------------------------------------------------------------------

## 6. `nodesNN.dat` — `CPathFind::LoadPathFindData` 0x4529F0 / 0x452F40, `Init` 0x44D080

The area index is `modelId - 25511`, and the model id comes from `sscanf(name+5, "%d")` on the IMG
entry `nodesNN.dat` (gta-reversed `LoadDirectory`; the exe range check is in 0x40C6B0: `id < 0x63E7`).
0x452F40 opens `data\paths\nodes%d.dat` directly (debug/unused path).

The reader body sits in the SecuROM-relocated region (0x4529F0 is `jmp 0x156F750`); the Ghidra dump
shows the header and the first two sections, the capstone listing of 0x156F750 confirms the record
sizes (`imul ...,0x1c`, `imul ...,0xe`, `add ecx,0xC0`) but the link-section sequence is partly
obfuscated. Layout (header verified, tail per gta-reversed + the 192-slot stride seen in the code):

```
u32 numNodes, numVehicleNodes, numPedNodes, numNaviNodes(CarPathLinks), numLinks
CPathNode[numNodes]            28 B each   (RwStreamRead numNodes*0x1c)
CCarPathLink[numNaviNodes]     14 B each   (RwStreamRead numNavi*0xe)
CNodeAddress[numLinks + 192]    4 B each   (192 = 16 dynamic links × 12, read from the file)
CCarPathLinkAddress[numLinks]   2 B each
uint8 linkLengths[numLinks + 192]
CPathIntersectionInfo[numLinks + 192] 1 B each
```

* **DAT-41** — area number must be 0..63 (`m_anNumNodes` etc. are 64/72-entry arrays at
  `this+0xFA4..`; `Init` clears 0x48 = 72 entries); `nodes64.dat` and above map into the IFP id range
  (25575+) and are treated as animation blocks (**garbage**). Node/navi/link counts are trusted: a
  file shorter than the declared sizes leaves the malloc'd arrays partly uninitialised (**garbage**);
  no checks that link targets `< numNodes` of the target area, that `areaId` fields equal the file's
  area, or that a node's `linkId + numLinks` stays inside the link table → OOB reads during
  path-finding (**crash**, at runtime not at load). Positions are int16 (×8 fixed point → ±4095.875).
  Best checked **offline** (the addon's `core/paths.py` already writes the +192 tails).

---------------------------------------------------------------------------------------------------

## 7. `timecyc.dat` — `CTimeCycle::Initialise` 0x5BBAC0 (+ `AddOne` for tcyc boxes, §4.10)

```
for weather in 0..22:            ; 23 weathers (outer loop counter 0x17)
  for hour in 0..7:              ; 8 samples
    do line = LoadLine(f) while (line && (line[0]=='/' || line[0]==0))
    sscanf(line, "%d"×21 " %f %f %f" " %d %d %d" " %f %f %f" " %d"×6 " %f"×4 " %f"×4 " %f"×5 " %d %d %f", ...)   ; 52 conversions
    store into Colors<T>[hour][weather]  (index = hour*23 + weather)
```

Columns (52): ambient RGB, ambientObj RGB, directional RGB, skyTop RGB, skyBot RGB, sunCore RGB,
sunCorona RGB, sunSize, spriteSize, spriteBrightness, shadowStrength, lightShadow, poleShadow,
farClip, fogStart, lightsOnGround, lowClouds RGB, bottomClouds RGB, water RGBA, postFx1 A R G B,
postFx2 A R G B, cloudAlpha, highLightMinIntensity, waterFogAlpha, directionalMult.
Vanilla `data\timecyc.dat` has **51** columns on 183 lines and 49 on one; the 52nd conversion
(`m_nDirectionalMult` 0xB79FD8) therefore always reads an unfilled local (harmless in practice).

* **DAT-42a** — exactly 184 data lines are consumed; a **short file** makes `LoadLine` return NULL
  and `sscanf(NULL, ...)` is called (0x5BBB4B, no check) → **crash** in the CRT. Extra lines are
  ignored.
* **DAT-42b** — only `/`-prefixed and empty lines are skipped; a `#` line or any label line is
  fed to sscanf, which returns early → that weather/hour keeps stale/uninitialised values **and the
  line count shifts** so every following sample lands in the wrong slot (**garbage**).
* **DAT-42c** — no return check on sscanf: a short line leaves the tail columns as whatever the
  previous line left on the stack.
* **DAT-42d** — storage conversions (0x5BBCE7.., constants 0x85862C = 10, 0x858628 = 100,
  0x858B8C = 0.5): colour ints → `uint8` (values > 255 wrap); sunSize/spriteSize/spriteBrightness →
  `int8(v*10+0.5)` (v ≤ 12.7); farClip/fogStart → `int16`; lightsOnGround → `uint8(v*10+0.5)`;
  postFx alphas → `uint8(a*2)` (a ≤ 127); cloudAlpha/highlight/waterFogAlpha → `uint8`;
  directionalMult → `uint8(uint8(int(v))*100)` (only 0, 1, 2 are meaningful).
* `#`/labels between blocks are not allowed; the addon writer (`core/timecyc.py::format_slot`) uses
  TAB-separated groups, which LoadLine converts to spaces — fine.

---------------------------------------------------------------------------------------------------

## 8. `water.dat` — `CWaterLevel::WaterLevelInitialise` 0x6EAE80

Grammar: file `DATA\WATER.DAT` (or `WATER1.DAT` when `m_nWaterConfiguration == 1`), opened in a
retry loop until it opens. Lines whose first non-blank char is `;`, `*`, `p` (the `processed`
keyword — **any** line starting with `p`) or that are empty are skipped. Otherwise:

```
flags = 1
if sscanf(line, "%f"×28 " %d") == 29  → quad (4 vertices × 7 floats + flags)
elif sscanf(line, "%f"×28) == 28      → quad, flags = 1
else sscanf(line, "%f"×21 " %d")      → triangle (3 vertices + flags), NO return check
per vertex: x y z flowX flowY bigWaves smallWaves
CRenPar { float z; float bigWaves; float smallWaves; int8 flowX = ftol(flowX*64); int8 flowY = ftol(flowY*64) }   ; 0x859A44 = 64
X = ftol(x), Y = ftol(y)  (int32, later int16)
AddWaterLevelQuad(X1,Y1,P1, ..., X4,Y4,P4, flags) 0x6E7EF0 / AddWaterLevelTriangle(...) 0x6E7D40
```

Post-pass `0x6E7B30`: for each of the 12×12 500-unit blocks (`x = -3000 + 500·i`) every quad/tri
whose (sorted) extent strictly overlaps the block is registered in the block (`AddPolyToBlock`
0x6E5750: single quad / single tri / combo list at 0xC215F8, 700 entries, **no bound**).

* **DAT-43** — a line with 22..27 or fewer than 21 numeric tokens is parsed as a triangle with
  uninitialised/stale fields (**garbage**); the addon's writer emits 7 fields per vertex + flag, OK.
* **DAT-44** — arrays (verified by symbol/address): `m_aVertices` 1021 × 20 B at 0xC22910 (`AddWaterLevelVertex`
  0x6E5A40: linear dedup on (x, y, z), then `m_aVertices[NumWaterVertices++]` with **no bound**);
  `WaterQuads` 301 × 10 B at 0xC21C90 (→ 0xC22854); `WaterTriangles` **6** × 8 B at 0xC22854 (→
  0xC22884 = `m_nNumOfWaterTriangles`); poly-combo list 700 × 2 B at 0xC215F8 (→ 0xC21B70 =
  block table). None checked: the 7th triangle overwrites its own counter, the 302nd quad overwrites the
  triangle table, the 1022nd vertex overwrites the following globals (**garbage/crash**).
* **DAT-45a** — `AddWaterLevelVertex` clamps X/Y to [-3000, 3000] (`0x859A90/0x859A94`) and when it
  clamps it **resets the CRenPar** (z = 0, waves 0, flow 0; 0x6E5A56..0x6E5B25) → an out-of-world
  vertex becomes a z = 0 vertex (**garbage**). Coordinates are truncated to integers (`_ftol`) and
  stored as int16 — keep them integral. Flow × 64 must fit int8 → |flow| < 2.0.
* **DAT-45b** — degenerate polygons (all X equal or all Y equal) are dropped silently (0x6E7EFB..0x6E7F33).
  Quads are assumed to be **axis-aligned rectangles**: vertices are sorted by (y, x) and used as
  (minx,miny),(maxx,miny),(minx,maxy),(maxx,maxy); a non-rectangular quad renders as garbage.
  Triangles must have two vertices on one Y (or X) line (same sorting assumption).
* **DAT-45c** — flags: bit0 clear → `bInvisible` (flags = 0 → invisible water), bit1 → `bLimitedDepth`;
  default (no 29th field) = 1 = visible.
* Block rule: a polygon larger than one 500×500 block is registered in every block it overlaps (loop
  at 0x6E7B90..0x6E7D15); the earlier in-game observation ("bigger than 500 → no texture") stands as an
  observed rendering limit, not a parser check — keep quads ≤ 500 per side and aligned to the grid.

---------------------------------------------------------------------------------------------------

## 9. `plants.dat` — `CPlantSurfPropMgr::LoadPlantsDat` 0x5DD3B0 (called from `Initialise` 0x5DD6C0)

Grammar: LoadLine; a line equal to `;the end` (first 9 bytes) terminates; lines starting `;` are
comments; other lines are tokenised with `strtok(line, " \t")` (LoadLine already turned commas into
spaces) and **exactly 18** tokens are consumed in order:

```
0 Name (surface name → SurfaceInfos_c::GetSurfaceIdFromName; 0 = unknown)
1 PCDid (0..2; >2 → 0)          2 SlotID (u16, whole surface)    3 ModelID (int16)   4 UVoff (int16)
5 R 6 G 7 B 8 I 9 VarI 10 A (bytes)   11 SclXY 12 SclZ 13 SclVarXY 14 SclVarZ 15 WBendScl 16 WBendVar 17 Density (floats)
```

* **DAT-46a** — unknown surface name → `sprintf("Unknown surface name '%s' in 'Plants.dat' (line %d)!
  See Andrzej to fix this.")` and **return false** → `CPlantMgr::Initialise` 0x5DD910 returns false →
  `CGame::Init1` returns false, which `CGame::Initialise` ignores → the game runs **without any
  grass** (silent). Same for a line with < 18 tokens (`uVar6 < 0x12 → return`), and for more than
  **57** distinct surfaces (`0x38 < count → return`; table 0xC38338, 57 × 124 B → 0xC39ED4 ✓).
  Everything after the offending line is not read.
* **DAT-46b** — value ranges are not checked: `SlotID` indexes `PC_PlantModelsTab[4][4]` (0..3, only
  0/1 loaded from `models\grass`), `ModelID` 0..3, `UVoff` 0..3 (four textures per slot,
  `txgrass{slot}_{uv}`); out-of-range values index garbage atomics at render → **garbage/crash**.
  Colour/intensity/alpha are truncated to bytes.
* `PCDid > 2` is silently coerced to 0 (overwrites entry 0).

---------------------------------------------------------------------------------------------------

## 10. `effects.fxp` — `FxManager_c::LoadFxProject` 0x5C2420 and the blueprint loaders

The TXD is derived from the project file name: the last 4 characters are replaced by `PC.txd`
(`effects.fxp` → `effectsPC.txd`, loaded into a new `CTxdStore` slot and made current) (**DAT-48**).
The parser is **line-positional**: `CFileMgr::ReadLine(file, buf, 256)` + `sscanf(buf, "%s ...")`
in a fixed order, keyword text is *not* verified except where noted. Version (`p3`, the number after
`FX_SYSTEM_DATA:`; vanilla 109) gates optional lines.

```
LoadFxProject:  line1 "%s" (FX_PROJECT_DATA:)  line2 (blank)  line3 "%s" == "FX_SYSTEM_DATA:" ?
   loop: LoadFxSystemBP → FxSystemBP_c::Load;  then 2 lines (blank + "%s") until != "FX_SYSTEM_DATA:"
LoadFxSystemBP 0x5C1F50:  line "%d" version
FxSystemBP_c::Load 0x5C05F0:
   blank; [v>100: "%s %s" FILENAME]; "%s %s" NAME (→ hash); "%s %f" LENGTH;
   [v>=106: "%s %f" LOOPINTERVALMIN; "%s %f" LENGTH(loop)] ; "%s %d" PLAYMODE (byte); "%s %f" CULLDIST (→int16);
   [v>103: "%s %f %f %f %f" BOUNDINGSPHERE]; "%s %d" NUM_PRIMS (int8) ; GetMem(NUM_PRIMS*4)
   for each prim: "%s" must be "FX_PRIM_EMITTER_DATA:" (else the slot stays NULL → later vtable call on NULL → crash);
        blank; FxEmitterBP_c::Load 0x5C25F0 → FxPrimBP_c::Load 0x5C2010:
            "%s" (FX_PRIM_BASE_DATA:) ; "%s %s" NAME ; "%s %f"×12 MATRIX (identity → no matrix stored, else 12×int16);
            "%s %s" TEXTURE → names[prim][0..31]; [v>101: TEXTURE2/3/4 → +0x20/+0x40/+0x60]; "%s %d" ALPHAON; SRCBLENDID; DSTBLENDID;
            FxInfoManager_c::Load 0x5C0B70: blank; "%s %d" NUM_INFOS; GetMem(n*4);
                per info: "%s" keyword → AddFxInfo(type); [non-EM infos: "%s %d" TIMEMODEPRT]; FxInfo*_c::Load → FxInterpInfo*_c::Load; blank
        "%s %f" LODSTART (→int16), "%s %f" LODEND (→int16)
   [v>107: "%s %d"] [v>108: "%s %s"]
   for each prim: LoadTextures 0x5C0A30 (RwTextureRead(name, "%sm"); "NULL" = none)
FxInterpInfo{Float,U255,32,255}_c::Load 0x5C16F0/0x5C18F0/0x5C1B10/0x5C1D30:
   for each curve (count fixed per info type): "%s" (curve name) ; "%s" (FX_INTERP_DATA:) ; "%s %d" LOOPED (byte) ; "%s %d" NUM_KEYS (int8)
        first curve only: times = GetMem(NUM_KEYS*2)  ; values = GetMem(NUM_KEYS*sizeof)
        per key: "%s" (FX_KEYFLOAT_DATA:) ; "%s %f" TIME → int16(t*256) (0x858FB4) ; "%s %f" VAL (float / int16(v*256) / int32 ...)
```

Recognised `FX_INFO_*_DATA:` keywords → curve counts (constructor `Allocate(n)`, gta-reversed):
EMRATE 1, EMSIZE 7, EMSPEED 2, EMDIR 3, EMANGLE 2, EMLIFE 2, EMPOS 3, EMWEATHER 4, EMROTATION 2 (EM
family, type 0x1xxx, no TIMEMODEPRT line); NOISE 1, FORCE 3, FRICTION 1, ATTRACTPT 4, ATTRACTLINE 7,
GROUNDCOLLIDE 3, WIND 1, JITTER 1, ROTSPEED 4, FLOAT 0, UNDERWATER 0, COLOUR 4, SIZE 4, SPRITERECT 4,
HEATHAZE 0, TRAIL 2, FLAT 9, DIR 3, ANIMTEX 1, COLOURRANGE 7, SELFLIT 0, COLOURBRIGHT 5, SMOKE 8
(these read a `"%s %d"` line before their curves because `version >= 0.7` (0x86B1D0) is always true).

* **DAT-47a** — `NUM_PRIMS` > 8: the texture-name scratch is `local_40c[1024]` = 8 × 128 B on the
  stack of `FxSystemBP_c::Load`, indexed `prim*0x80` with **no bound** → stack smash → **crash**.
  Vanilla max is 6.
* **DAT-47b** — unknown `FX_INFO_*_DATA:` keyword: no comparison matches, the code falls to
  `*(byte*)(infoPtr[i] + 6) = timeMode` with `infoPtr[i]` still the zero-filled pool value →
  **NULL write → crash** (0x5C0B70 tail). `NUM_INFOS` must equal the number of info blocks.
* **DAT-47c** — all curves of one info share the **time array allocated from the first curve's
  NUM_KEYS** (0x5C16F0: `if (iVar4 == 0) GetMem(keys*2)`); a later curve with more keys writes past
  it into the next pool allocation (**garbage**). `NUM_KEYS` is `int8` (≤ 127); times are
  `int16(t*256)`; U255/255 values `int16(v*256)`.
* **DAT-47d** — the 1 MB `FxMemoryPool_c` (0x4A9C30) has no bounds check in `GetMem` 0x4A9CA0 → an
  fxp whose blueprints exceed 1 MB overruns the heap (**crash**).
* **DAT-47e** — any extra/missing line (blank lines are counted!) shifts every following `sscanf`
  — values silently become 0/garbage; a truncated file makes `ReadLine` fail and the stale buffer is
  re-parsed (garbage). Textures are resolved in `effectsPC.txd` (then the RW fallback search); a
  missing texture leaves the texture pointer NULL (untextured particles, silent). Use the literal
  `NULL` for unused texture slots.

---------------------------------------------------------------------------------------------------

## 11. Check list (ids for the validator / exporter)

| id | file | condition | consequence | where (decompile) |
|---|---|---|---|---|
| DAT-01 | any text | line > 511 chars | garbage (split into 2 lines) | LoadLine 0x536F80 `ReadLine(...,0x200)` |
| DAT-02 | any text | `#` not first non-blank; `//`/`;` used as comment in IDE/IPL | garbage (parsed as data) | callers test `line[0]=='#'` only |
| DAT-03 | gta.dat | IDE/IPL path unopenable (missing, 2 separators, wrong case) | crash (`fgets(NULL)`) | LoadObjectTypes 0x5B8400 / LoadScene 0x5B8700: OpenFile result unchecked |
| DAT-04 | gta.dat | IDE line after the first IPL line | ignored (models never stream) | LoadLevel 0x5B9030 `bVar2` one-shot Init2 |
| DAT-05 | gta.dat | TEXDICTION/IPL path ≥ 64, IDE path ≥ 256 chars | crash | LoadLevel `local_40[64]`, LoadObjectTypes `local_100[256]` unbounded copies |
| DAT-06 | IDE | model id < 0 or ≥ 20000 | crash/garbage (`ms_modelInfoPtrs` OOB write) | CModelInfo::Add*Model 0x4C6620.. |
| DAT-07 | IDE | duplicate id; store overflow (14000/70/169/92/212/278/51) | garbage (silent replace / next store overwritten) | Add*Model: `count++` no bound |
| DAT-08 | IDE objs/tobj | draw distance < 4.0 (SA form) | garbage (legacy re-parse) | LoadObject 0x5B3C60 `local_44 < 4.0` |
| DAT-08b | IDE objs/tobj/hier/anim | wrong token count | ignored (line dropped) | `return -1` paths |
| DAT-09 | IDE all | `%s` ≥ buffer (24/16/12/32/60, table §3.11) | garbage (name) / crash (txd = last local) | stack layouts §3.11 |
| DAT-10 | IDE anim | anim name ≥ 16 chars | garbage (model name corrupted) | LoadAnimatedClumpObject buffer at ESP+0xC |
| DAT-11 | IDE cars/peds/weap | malformed line (no return check; id default -1 / uninit) | crash/garbage | 0x5B6F30 / 0x5B7420 / 0x5B3FB0 |
| DAT-12 | IDE peds | anim group not in animgrp.dat | crash at ped spawn | LoadPedObject: index = ms_numAnimAssocDefinitions |
| DAT-13 | IDE cars | unknown type/class string | garbage (ctor default) | 0x5B6F30 string chain |
| DAT-14 | IDE objs | flags bits outside the table §3.1 | ignored | SetAtomicModelInfoFlags 0x5B3B20 |
| DAT-15 | IDE/IMG | > 5000 distinct TXD names | crash | AddTxdSlot 0x731C80 `*New()=0` unchecked |
| DAT-16 | IDE 2dfx | model undefined; > 100 effects; non-consecutive per model; unterminated `"` | crash / garbage | Load2dEffect 0x5B7670 |
| DAT-17 | IDE path | section present | ignored (stubs 0x44D2C0/0x44D2F0 `ret 0x44`) | LoadCarPathNode 0x5B4380 |
| DAT-18 | IPL | unknown section header | ignored | LoadScene header chain |
| DAT-19 | IPL | `mult` (ignored) / `path` (aborts rest of file) | ignored | LoadScene `if (iVar6==1) break` |
| DAT-20 | IPL inst | < 11 tokens | crash (garbage lod) / garbage | 0x538690 no return check |
| DAT-21 | IPL inst | model id undefined or ≥ 20000 | crash | 0x538090 `ms_modelInfoPtrs[id]`; LinkLods 0x5B51F7; LoadIpl 0x4061FC/0x406302 |
| DAT-22 | IPL inst | lod not -1 and not a valid index (text: same file; streamed: same-name text IPL); > 255 children | crash / garbage | LinkLods 0x5B51FF..0x5B5209; LoadIpl 0x406201..0x40620B |
| DAT-23 | IPL inst | quaternion not normalised (abs(rw) > 1) | garbage (NaN matrix) | 0x538090 acos path 0x53823C |
| DAT-24 | IPL inst | interior > 255 or unknown high bits | garbage / ignored | 0x538090 flag mapping |
| DAT-25 | IPL inst | position outside ±3000; model dd < 2 | garbage (edge sector) / invisible | CEntity::Add 0x5347D0 clamps; 0x538090 `dd < 2.0` |
| DAT-26 | IPL inst | > 4096 inst in one text IPL (size unverified) | garbage | LoadScene `gCurrIplInstances[count++]` |
| DAT-27 | IPL zone | ≠ 10 fields (**addon ipl.py writes 9**); name/info > 7 chars; type ∉ {0,1,3}; > 380 navi / > 39 map | ignored / truncated / garbage | LoadZone `iVar1==10`; CreateZone strncpy 7, no count checks |
| DAT-28 | IPL cull | flags == 0 after masking; > 1300 attr / 40 tunnel / 72 mirror | ignored / garbage | AddCullZone 0x72DF70 |
| DAT-29 | IPL occl | > 1000 map / 40 interior; two zero dims; interior-ness from file name `…int.ipl` | ignored (checked) | COcclusion::AddOne 0x71DCD0 |
| DAT-30 | IPL enex | name with spaces / no quotes; ≥ 32 chars; > 400 enex | garbage / crash / garbage (entry 0) | LoadEntryExit 0x5B8030, AddOne 0x43FA00 |
| DAT-31 | IPL grge | name ≥ 8 chars; ≠ 11 fields; > 50 garages | crash / ignored / garbage | LoadGarage `local_8[8]`; AddOne 0x4471E0 |
| DAT-32 | IPL jump | ≠ 16 fields; pool full | ignored | LoadStuntJump 0x5B45D0, AddOne 0x49CB40 |
| DAT-33 | IPL pick | id not in the switch table; ≠ 4 fields | ignored | LoadPickup 0x5B47B0 |
| DAT-34 | IPL cars | ≠ 12 fields; model ∉ {-1, 400..630}; > 500 | ignored | 0x5B4740, CreateCarGenerator 0x6F31A0 |
| DAT-35 | IPL tcyc | > 20 boxes; < 12 fields (lodDistMult = field 11) | garbage | CTimeCycle::AddOne 0x55FF40 |
| DAT-36 | IPL auzo | name ≥ 8 (garbage) / ≥ 16 (crash); > 158 boxes / 3 spheres | garbage / crash | RegisterAudioBox 0x508240 |
| DAT-37 | bnry | header counts ≥ 32768; offsets outside the file; other sections | ignored / garbage | LoadIpl 0x4061C3 `movsx` |
| DAT-38 | streamed text IPL | sections other than inst | ignored | LoadIpl 0x4062A6 |
| DAT-39 | IMG | streamed IPL basename ≥ 18 chars; > 255 IPL files | garbage (IplDef fields) / crash | AddIplSlot 0x405AC0 |
| DAT-40 | streamed text IPL | line ≥ 16 KB | garbage | LoadLine 0x536FE0 |
| DAT-41 | nodes | area ∉ 0..63; declared counts larger than file; link/node indices OOB | garbage / crash at runtime | LoadPathFindData 0x4529F0 (SecuROM body) |
| DAT-42 | timecyc | < 184 data lines (crash); non-`/` comment lines; short lines; value ranges | crash / garbage | CTimeCycle::Initialise 0x5BBAC0 |
| DAT-43 | water | token count ∉ {22, 28, 29} | garbage | 0x6EAE80 sscanf chain |
| DAT-44 | water | > 1021 vertices / 301 quads / 6 triangles / 700 combos | garbage/crash | 0x6E5A40 / 0x6E7EF0 / 0x6E7D40 / 0x6E5750 |
| DAT-45 | water | vertex outside ±3000 (z reset); non-integer coords; abs(flow) ≥ 2; non-axis-aligned quad; degenerate | garbage / ignored | AddWaterLevelVertex 0x6E5A40 clamp; sort assumptions |
| DAT-46 | plants | unknown surface / < 18 tokens / > 57 surfaces (grass disabled); SlotID/ModelID/UVoff > 3 | ignored (whole file) / crash | LoadPlantsDat 0x5DD3B0 |
| DAT-47 | fxp | NUM_PRIMS > 8; unknown FX_INFO keyword; NUM_INFOS mismatch; curve key counts differ; NUM_KEYS > 127; line-count drift; > 1 MB | crash / garbage | FxSystemBP_c::Load 0x5C05F0, FxInfoManager_c::Load 0x5C0B70, FxInterpInfo*::Load |
| DAT-48 | fxp | TXD must be `<project>PC.txd` (`effectsPC.txd`) with all referenced textures | garbage (NULL texture) | LoadFxProject 0x5C2420, LoadTextures 0x5C0A30 |

---------------------------------------------------------------------------------------------------

## 12. Hook points vs. offline checks

### In-game (inuhook style: register-preserving stubs; all targets below take the line pointer or
### the parsed struct, so a pre-hook can validate and a post-hook can inspect)

| where | what to check | why in-game |
|---|---|---|
| `CFileLoader::LoadObject` 0x5B3C60 / `LoadTimeObject` 0x5B3DE0 / `LoadClumpObject` 0x5B4040 / `LoadAnimatedClumpObject` 0x5B40C0 / `LoadVehicleObject` 0x5B6F30 / `LoadPedObject` 0x5B7420 / `LoadWeaponObject` 0x5B3FB0 (cdecl, `const char* line`) | re-scan the line: token count, id range, name lengths, dd ≥ 4, duplicate id (`ms_modelInfoPtrs[id] != NULL` before the call) | sees the real file the engine opened |
| `CFileLoader::LoadObjectInstance` 0x538090 (cdecl, `CFileObjectInstance*, const char*`) | `id < 20000 && ms_modelInfoPtrs[id]`, quaternion norm, interior bits, lod == -1 or in range (text: `gCurrIplInstancesCount` 0xBCC0D8; streamed: size of the related entity array) | single choke point for text **and** bnry |
| `LinkLods` 0x5B51E0 (pre) | walk `gCurrIplInstances[0..count)` for NULLs and lod indices ≥ count | last chance before the NULL deref |
| `CIplStore::LoadIpl` 0x406080 / `LoadIplBoundingBox` 0x405C00 (cdecl `slot, data, size`) | `bnry`: counts vs `size`, offsets in range, record size multiples; text: unsupported sections | streamed IPLs load at runtime |
| `CTheZones::CreateZone` 0x5728A0, `CCullZones::AddCullZone` 0x72DF70, `COcclusion::AddOne` 0x71DCD0, `CTimeCycle::AddOne` 0x55FF40, `CGarages::AddOne` 0x4471E0, `CEntryExitManager::AddOne` 0x43FA00, `CAudioZones::RegisterAudioBox/Sphere` 0x508240/0x5081C0, `CTheCarGenerators::CreateCarGenerator` 0x6F31A0 | count vs. the fixed array (380/39, 1300/40/72, 1000/40, 20, 50, 400, 158/3, 500), name lengths | the engine has no counters of its own except occl/cargen |
| `CWaterLevel::AddWaterLevelQuad` 0x6E7EF0 / `AddWaterLevelTriangle` 0x6E7D40 / `AddWaterLevelVertex` 0x6E5A40 | counts 301/6/1021, ±3000, axis alignment | runs once in Init2 |
| `CPlantSurfPropMgr::LoadPlantsDat` 0x5DD3B0 (post, return value) | report `false` (the engine swallows it) | otherwise "no grass" is silent |
| `FxManager_c::LoadFxProject` 0x5C2420 (pre) | pre-scan the file: NUM_PRIMS ≤ 8, keyword set, NUM_INFOS/blocks, per-info key counts | the loader itself cannot be validated mid-way |
| `CTimeCycle::Initialise` 0x5BBAC0 (pre) | count data lines (== 184), column counts | runs during RW init — still after DllMain, but before CGame::Initialise |

Timing note: everything above executes after an ASI's DllMain (§0); `CTimeCycle::Initialise` is
the earliest (from `InitialiseCoreDataAfterRW` 0x5BFA90, before `CGame::Initialise`). A `LoadLine`
0x536F80 hook sees every text line of every file but has no file context; prefer the per-loader hooks.

### Offline (python, exporter side) — the right tool for

* gta.dat: keyword/separator format, IDE-before-IPL ordering, path lengths, file existence (DAT-03/04/05).
* IDE: everything in DAT-06..DAT-15 (ids, duplicates across all IDEs, store capacities, name lengths,
  dd ≥ 4, flag bits, anim-group/ped-type names against `animgrp.dat`/`ped.dat`, TXD count).
* IPL/ZON: DAT-18..DAT-36 — model ids resolvable in the IDE set, lod indices within the same file
  (or the same-basename text IPL for streamed files), quaternion norm, zone field count/name length
  (fix `core/ipl.py::_format_zone_line`), garage name ≤ 7, enex quoting, per-section capacities
  summed across **all** IPLs (zones 380/39, cull 1300/40/72, occl 1000/40, tcyc 20, grge 50, enex
  400, auzo 158/3, cargen 500), no `path`/`mult` sections.
* bnry: header sanity, basename ≤ 17, ≤ 255 streamed IPLs.
* nodes: area 0..63, record sizes, link/node index ranges (DAT-41).
* timecyc: 184 lines, 51/52 columns, value ranges (DAT-42).
* water: token counts, counts 1021/301/6, integral coords in ±3000, flow range, rectangle check,
  500-grid alignment (DAT-43..45).
* plants: 18 tokens, surface names from `surface.dat`/`SurfaceInfos`, ≤ 57 surfaces, slot/model/uv ≤ 3.
* fxp: structural line count, keyword set, NUM_PRIMS ≤ 8, NUM_INFOS, per-info curve counts (§10
  table), equal NUM_KEYS across the curves of one info, NUM_KEYS ≤ 127, textures present in
  `effectsPC.txd`.

---------------------------------------------------------------------------------------------------

## 13. Structures and limits (verified unless marked)

| struct / array | layout / size | evidence |
|---|---|---|
| `CFileObjectInstance` | 40 B: float x,y,z, rx,ry,rz,rw; int32 id; int32 interior(+flags); int32 lod | 0x538690 locals → 0x538090 `param_1[7..9]`; bnry stride 0x28 at 0x406230 |
| `tBinaryIplFile` | 76 B header (§5.2) | offsets 0x04/0x14/0x1C/0x3C used in 0x406080 |
| `CFileCarGenerator` | 48 B (§4.8) | 0x537990 field reads, stride 0x30 at 0x406274 |
| `IplDef` | 0x34 B, name[18] at +0x10 | AddIplSlot 0x405AC0, pool stride 0x34 |
| IPL pool | 256 slots | 0x405EC0 `push 0x100` |
| `ms_modelInfoPtrs` | 20000 × ptr at 0xA9B0C8..0xAAE948 | LoadLevel loop bound 0xAAE948 |
| model stores | atomic 14000 / damage 70 / time 169 / clump 92 / vehicle 212 / ped 278 (weapon 51 unverified) | address arithmetic §3.1 |
| TXD pool | 5000 | 0x731F20 `push 0x1388` |
| 2dfx store | 100 (gta-reversed) | 0xB4C2D8 |
| `CZone` | 32 B; navi 380 @0xBA3798, map 39 @0xBA1908, `CZoneInfo` 17 B × 380 @0xBA1DF0 | 0x5728A0 strides, neighbouring globals |
| `CAttributeZone` | 18 B; 1300 @0xC81F50, tunnel 40 @0xC81C80, mirror 24 B × 72 @0xC815C0 | 0x72DF70 stride 0x12, address arithmetic |
| `COccluder` | 18 B; 1000 @0xC73FA0, interior 40 @0xC73CC8 | 0x71DCD0 |
| `CTimeCycleBox` | 0x28 B; 20 @0xB7C550 (count 0xB7C480) | 0x55FF40 (20 from gta-reversed) |
| timecyc tables | `uint8/int8/int16 [8 hours][23 weathers]`, index `hour*23+weather` | 0x5BBAC0 loop |
| `CWaterVertex` | 20 B: int16 x, y; float z, bigWaves, smallWaves; int8 flowX, flowY (+pad) — 1021 @0xC22910 | 0x6E5A40 stride 0x14, symbol |
| `CWaterQuad` | 10 B (4×uint16 verts + flags) — 301 @0xC21C90 | 0x6E7B30 stride 0xA, 0xC22854 follows |
| `CWaterTriangle` | 8 B — 6 @0xC22854 | 0x6E7C40 stride 8, counter at 0xC22884 |
| water blocks | 12 × 12 PolyInfo (uint16) @0xC21B70, combos 700 × uint16 @0xC215F8 | 0x6E7B30 / 0x6E5750 |
| `CPlantSurfProp` | 124 B (u16 slot + 3 × 40 B PCD); 57 @0xC38338; ptr table 178 @0xC38070 | 0x5DD3B0 strides 0x3E shorts / 0x28, 0xC39ED4 counter |
| path areas | 64 (+8 interior slots in the arrays) | 0x44D080 clears 0x48 entries; ids 25511..25574 |
| Fx | pool 1 MB; NUM_PRIMS scratch 8 × 128 B; NUM_KEYS int8; times int16(t·256) | 0x4A9C30, 0x5C05F0 `local_40c[1024]`, 0x5C16F0 |
| audio zones | box 24 B × 158 @0xB6DCD0, sphere 28 B × 3 @0xB6EBA8 | 0x508240 / 0x5081C0 strides, address arithmetic |
| garages | 0xD8 B × 50 @0x96C048 | 0x4471E0 |
| enex pool | 400 (gta-reversed `new CEntryExitsPool(400)`) | 0x43FA00 uses pool New |
| car generators | 0x20 B × 500 @0xC27AED | 0x6F31A0 loop bound 0xC2B96D |

## 14. Open questions

* `gCurrIplInstances` capacity (4096 per gta-reversed) not verified from the exe (no neighbouring symbol).
* `CTimeCycle::m_aBoxes` capacity 20 and the weapon store 51 are gta-reversed values; the exe never
  compares against them (no bound check exists), so the number only matters for the offline checker.
* `nodes*.dat` tail sections: sequence and the +192 spare-link stride are from gta-reversed plus the
  `add ...,0xC0` / `imul 0x1c/0xe` seen in the SecuROM body; not byte-verified.
* The water "quad > 500 units renders untextured" rule is an earlier in-game observation; the parser
  registers such a quad in every overlapped block, so the failure is in the renderer, not the loader.
