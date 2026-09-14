# Vehicle data + vehicle DFF setup — gta_sa.exe 1.0 US (rules VEH-nn / HND-nn)

Sources: Ghidra dumps `E:\RE\asm_data2\<ADDR>.c/.asm` (LoadHandlingData 0x5BD830 + continuation 0x5BE179,
Initialise 0x5BF3D0, LoadVehicleObject 0x5B6F30, LoadVehicleColours 0x5B6890, LoadVehicleUpgrades 0x5B65A0,
AddUpgradeLink 0x4C74B0, AddWheelUpgrade 0x4C8700, SetClump 0x4C95C0, PreprocessHierarchy 0x4C8E60,
SetupCommonData 0x5B8F00, LoadEnvironmentMaps 0x4C8780, SetupLightFlags 0x4C8C90, SetFrameIds 0x4C5460,
GetFrameFromId 0x4C53C0, GetWheelPosn 0x4C7D20, LoadCarGroups 0x5BD1A0, AddRemap 0x4C86D0), `asm\`
(FindExactWord 0x6F4F30, GetHandlingId 0x6F4FD0, GetFlyingPointer 0x6F52D0, GetBoatPointer 0x6F5300,
AssignRemapTxd 0x4C9360, SetCarCustomPlate 0x4C9450, ReduceMaterialsInVehicle 0x4C8BD0, frame callbacks
0x4C52F0/0x4C5350, GetAnimationBlockIndex 0x4D3990), capstone disassembly of undumped code
(ClumpCollisionStreamRead 0x41B1D0, RpClumpRemoveAtomic 0x74A4C0, SetupVehicleUpgradeFlags 0x4C4570,
SetAnimFile 0x4C7670 / ConvertAnimFileIndex 0x4C76D0, ConvertDataToGameUnits 0x6F5080,
CBike::SetupSuspensionLines 0x6B89B0, CVehicleModelInfo::CreateInstance 0x4C9680, ChooseVehicleColour 0x4C8500,
CCustomCarPlateMgr::Initialise 0x6FD500, SetAtomicRendererCB 0x4C77E0, LoadInitialVehicles 0x407F20,
LoadZoneVehicle 0x40B4B0), raw bytes via `readva.py` (name table 0x8D3978, delimiters, format strings,
upgrade prefixes). gta-reversed / plugin-sdk only for struct layouts, which were then re-derived from the
address arithmetic in the dumps (stated per item). The 12 per-type frame tables are `E:\RE\vehicle_descs.txt`
(dumped from 0x8A7740). Vanilla data read from `D:\Grand Theft Auto San Andreas\data` and the 212 vehicle DFFs
in `models\gta3.img` (scanner: scratchpad `veh\scan.py`; note this install's `admiral.dff` is the user's
re-export, not vanilla — it still passes every rule).

Every crash / garbage / silently-ignored claim cites the function and the unchecked operation.
Legend: **crash** = access violation / use-after-free; **garbage** = engine continues with wrong data;
**ignored** = dropped silently. Severity mapping for gtacheck: Crash / Error (garbage) / Warning / Info.

---------------------------------------------------------------------------------------------------

## 0. When each loader runs

| phase | loader |
|---|---|
| `CGame::Initialise` → `Init1` | `cHandlingDataMgr::Initialise` 0x5BF3D0 → `LoadHandlingData` 0x5BD830 (`DATA\HANDLING.CFG`), then constants 0.1/0.9/1.0/0.8/0.98 |
| first `IPL` line of gta.dat (`CFileLoader::LoadLevel`) | `CVehicleModelInfo::SetupCommonData` 0x5B8F00: `LoadVehicleColours` (carcols.dat) → `LoadVehicleUpgrades` (carmods.dat) → `LoadEnvironmentMaps` → load `MODELS\GENERIC\VEHICLE.TXD` into slot `vehicle`, `ms_pLightsTexture`/`ms_pLightsOnTexture`, `ms_pVehicleStructurePool = new CPool(50, "VehicleStruct")` (0xB4E680) |
| `CGame::Initialise` | `CCustomCarPlateMgr::Initialise` 0x6FD500 (plate textures from the `vehicle` TXD) |
| `CPopulation::Initialise` | `LoadPedGroups` then `LoadCarGroups` 0x5BD1A0 (`CARGRP.DAT`) |
| IDE `cars` line | `CFileLoader::LoadVehicleObject` 0x5B6F30 |
| IMG directory scan (`CStreaming::LoadCdDirectory` 0x5B6170) | every `.txd` whose slot is new → `CVehicleModelInfo::AssignRemapTxd` 0x4C9360 (name ends in digits + matches a vehicle 400..630 → `AddRemap`) |
| vehicle DFF streamed in | RW clump read → plugin `ClumpCollisionStreamRead` 0x41B1D0 (extension 0x0253F2FA) → `CVehicleModelInfo::SetClump` 0x4C95C0 → `CClumpModelInfo::SetFrameIds` 0x4C5460 → `PreprocessHierarchy` 0x4C8E60 → `ReduceMaterialsInVehicle` → `SetCarCustomPlate` |
| vehicle spawned | `CAutomobile`/`CBike`/... ctor → `SetupModelNodes` (`FillFrameArray`) → `SetupSuspensionLines` (→ `GetWheelPosn` 0x4C7D20) → `CreateInstance` 0x4C9680 (extras via comp rules) |
| `CStreaming::LoadInitialVehicles` 0x407F20 | **empty (`ret`)** in 1.0 US. `LoadZoneVehicle` 0x40B4B0 = zone → `CCarCtrl::ChooseCarModelToLoad` → `RequestModel` — nothing file-decidable beyond cargrp/popcycle |

`SetupLightFlags` 0x4C8C90 only copies four bits of a *CVehicle instance* (`+0x584`) into statics at
0xB4E3E9..0xB4E3EB — runtime only, no data rule.

---------------------------------------------------------------------------------------------------

## 1. handling.cfg — `cHandlingDataMgr::LoadHandlingData` 0x5BD830

### 1.1 Line grammar (verified in the asm 0x5BD890..0x5BE470)

* File opened with `CFileMgr::OpenFile("HANDLING.CFG")` after `SetDir("DATA")`; **the handle is never checked**
  → missing file = `LoadLine(NULL)` → CRT crash (**HND-01**). Lines via `CFileLoader::LoadLine` 0x536F80
  (512-byte buffer, control chars and `,` → space, leading blanks trimmed; see textdata_path.md §1.1).
* Line equal to `;the end` (9 bytes compared incl. NUL, 0x5BD890) → stop. Any other line whose first char is
  `;` → comment. **`#` is NOT a comment** — such a line is parsed as a car line named `#...` (→ HND-03).
* First char selects the parser; tokens by `strtok(line, " \t")` (delimiter string at 0x85A510 = `20 09 00`):

| prefix | array (index = position of the name in the 210-name table) | fields after the name | stride / base (this = `gHandlingDataMgr` 0xC2B9C8) |
|---|---|---|---|
| none | `m_aVehicleHandling[210]` (tHandlingData 0xE0) | 35 | `0x14 + id*0xE0` (0x5BE1E9) |
| `!` | `m_aBikeHandling[13]` | 15 | `0x8F54 + id*0x40` (= `0xB7D4 + (id-162)*0x40`) |
| `$` | **flying** `m_aFlyingHandling[24]` | 21 | `0x7B24 + id*0x58` (= `0xBB14 + (id-186)*0x58`) |
| `%` | **boat** `m_aBoatHandling[12]` | 14 | `GetBoatPointer(id)` 0x6F5300: `0x9A50 + id*0x3C` only for 175 ≤ id ≤ 186, else `this+0xC354` (= boat[0]) |
| `^` | vehicle anim groups `CVehicleAnimGroupData::m_vehicleAnimGroups[30]` at 0xC1CDC0 (0x94 each) | 35 (3 ints, 18 bools, 13 floats, 1 int) | `0xC1CDC0 + field1*0x94` (0x5BE182, **no bound**) |

(The task text had `$`/`%` swapped: in the exe `$` = flying (stride 0x58 = tFlyingHandlingData), `%` = boat
(stride 0x3C = tBoatHandlingData); vanilla: `$ RUSTLER`, `% PREDATOR`.)

* **Empty line = crash (HND-02).** A blank/whitespace-only line comes back as `""`; `*line == 0` is not `;`,
  so it takes the car branch: `strtok("")` returns NULL and the code jumps straight into the switch
  (0x5BE1B8 `JMP 0x5BE1C0`, no NULL test) → case 0 → `FindExactWord(this, NULL, table, 0xE, 0xD2)` 0x6F4F30 →
  `_strncmp(NULL, ...)` → access violation. Vanilla handling.cfg has **zero** blank lines (516 lines, all
  data or `;`). A `!` line without a name ends its loop immediately and calls
  `ConvertBikeDataToGameUnits(previous bike ptr)` 0x6F5290 — NULL on the first bike line → crash, otherwise the
  previous bike's angles are converted twice (garbage). `$`/`%` without a name: nothing written.
* **Name lookup (HND-03).** `FindExactWord` walks the fixed table `s_LANDSTAL` 0x8D3978 (210 entries × 14
  bytes, dumped below) and compares `strncmp(token, tableName, strlen(tableName))` — **prefix match on the
  table name, case-sensitive**; first hit wins; **returns 210 when nothing matches**. Consequences of 210:
  car line → `this + 0x14 + 210*0xE0 = 0xB814` = `m_aBikeHandling[1]` (MOPED) — 0xE0 bytes of bike data
  (MOPED..FCR900) overwritten, then `ConvertDataToGameUnits` on that block (garbage);
  `!` → `0x8F54 + 210*0x40 = 0xC3D4` = inside `m_aBoatHandling[2..3]` (REEFER/RIO) — garbage;
  `$` → `0x7B24 + 210*0x58 = 0xC354` = `m_aBoatHandling[0..1]` (PREDATOR, SPEEDER) — garbage;
  `%` → `GetBoatPointer(0xD2)` → boat[0] PREDATOR overwritten — garbage.
  Because the table is compiled in, **custom handling names are impossible**; a mod must reuse one of the 210.
* **Prefix type vs. name range (HND-04).** `$ <car name>` writes 0x58 bytes at `0x7B24 + id*0x58`, which for
  id < 186 lands inside `m_aVehicleHandling` (e.g. `$ LANDSTAL` → offset 0x7B24 = entry 140 EMPEROR + 0x10)
  → that car's handling is destroyed. `! <car name>` likewise (`0x8F54 + id*0x40` < 0xB7D4 for id < 162).
  `% <non-boat>` → PREDATOR's boat data replaced. So `!` needs ids 162..174, `$` 186..209, `%` 175..186.
* **Field counts (HND-05)** — the `do { switch(fieldIndex) ... } while (strtok() != NULL)` loops store field
  *k* into a fixed slot; fewer tokens → the remaining slots keep whatever was there (static zero, or the value
  from an earlier line with the same name); more tokens → `switch` default, ignored. Vanilla is exact:
  car 36 tokens (name + 35), `!` 17, `$` 23, `%` 16, `^` 36 (RCRAIDER's `0.1s` is `atof`'d to 0.1).
  Numbers via `atof`/`atol` (non-numeric → 0); modelFlags / handlingFlags via `sscanf("%x")`;
  driveType / engineType take the first character of the token (cases 0x0F/0x10).

Car line slots (index → tHandlingData offset): 1 mass +4 · 2 turnMass +C · 3 dragMult +10 · 4..6 CoM +14..1C ·
7 percentSubmerged `atol`→byte +20 · 8 tractionMult +28 · 9 tractionLoss +A4 · 10 tractionBias +A8 ·
11 numGears `atol`→byte +76 · 12 maxVel +84 · 13 engineAccel×const +7C · 14 engineInertia +80 ·
15 driveType char +74 · 16 engineType char +75 · 17 brakeDecel +94 · 18 brakeBias +98 · 19 ABS (≠0) +9C ·
20 steeringLock +A0 · 21..27 suspension +AC..C4 · 28 seatOffset +D4 · 29 collDamageMult +C8 ·
30 monetaryValue `atol` +D8 · 31 modelFlags `%x` +CC · 32 handlingFlags `%x` +D0 (also copied to +78) ·
33 frontLights byte +DC · 34 rearLights byte +DD · 35 animGroup byte +DE.

### 1.2 Value ranges that produce garbage (HND-06)

`ConvertDataToGameUnits` 0x6F5080 (disassembled): `1/mass` (`fdiv [ecx+4]` 0x6F50C7) → mass 0 = INF
reciprocal (collision damage, physics explode); `mass*0.8 / percentSubmerged` (`fidiv` 0x6F50D6) →
percentSubmerged 0 = INF buoyancy; the max-velocity search loop 0x6F50F0 always terminates (v decreases to 0).
`cTransmission::InitGearRatios` 0x6D0460 writes `m_aGears[1..numGears]` into a 6-entry array (0x2C..0x74)
— **numGears ≥ 6 overwrites driveType/engineType/numGears/handlingFlags/engineAccel** (+0x74..+0x80) and,
because numGears itself is clobbered, keeps writing into the following handling entries (garbage);
numGears 0 → division by zero → NaN gear table (vehicle cannot move). Vanilla max gears = 5.
animGroup (field 35, byte) indexes `m_vehicleAnimGroups[30]` → ≥ 30 reads past the array (garbage ped
enter/exit anims, crash likely when a ped uses the group). frontLights/rearLights are `eVehicleLightsSize`
0..3 (used as table index). Ranges to enforce: mass > 0, percentSubmerged 1..255 (byte), numGears 1..5,
driveType ∈ {F,R,4}, engineType ∈ {P,D,E}, animGroup ≤ max `^` id (< 30), lights 0..3, maxVel > 0,
steeringLock > 0, tractionBias/brakeBias/suspBias in 0..1 (advisory).

### 1.3 `^` anim-group lines (HND-07)

`^ idx idx2 kind 18×bool 13×float int`: after parsing, `CVehicleAnimGroup::InitAnimGroup` (0x5B9EB0, group
ids = `kind + 0x58`), then `CopyAnimGroup(&m_vehicleAnimGroups[idx])` with `idx*0x94 + 0xC1CDC0` (0x5BE182)
— **no bound**; idx ≥ 30 overwrites the statics after the array (garbage / crash). Vanilla: 30 lines, idx 0..29.

### 1.4 The 210 built-in handling names (0x8D3978, 14-byte cells, index = position)

0 LANDSTAL 1 BRAVURA 2 BUFFALO 3 LINERUN 4 PEREN 5 SENTINEL 6 DUMPER 7 FIRETRUK 8 TRASH 9 STRETCH 10 MANANA
11 INFERNUS 12 VOODOO 13 PONY 14 MULE 15 CHEETAH 16 AMBULAN 17 MOONBEAM 18 ESPERANT 19 TAXI 20 WASHING
21 BOBCAT 22 MRWHOOP 23 BFINJECT 24 PREMIER 25 ENFORCER 26 SECURICA 27 BANSHEE 28 BUS 29 RHINO 30 BARRACKS
31 HOTKNIFE 32 ARTICT1 33 PREVION 34 COACH 35 CABBIE 36 STALLION 37 RUMPO 38 RCBANDIT 39 ROMERO 40 PACKER
41 MONSTER 42 ADMIRAL 43 TRAM 44 AIRTRAIN 45 ARTICT2 46 TURISMO 47 FLATBED 48 YANKEE 49 GOLFCART 50 SOLAIR
51 TOPFUN 52 GLENDALE 53 OCEANIC 54 PATRIOT 55 HERMES 56 SABRE 57 ZR350 58 WALTON 59 REGINA 60 COMET
61 BURRITO 62 CAMPER 63 BAGGAGE 64 DOZER 65 RANCHER 66 FBIRANCH 67 VIRGO 68 GREENWOO 69 HOTRING 70 SANDKING
71 BLISTAC 72 BOXVILLE 73 BENSON 74 MESA 75 BLOODRA 76 BLOODRB 77 SUPERGT 78 ELEGANT 79 JOURNEY 80 PETROL
81 RDTRAIN 82 NEBULA 83 MAJESTIC 84 BUCCANEE 85 CEMENT 86 TOWTRUCK 87 FORTUNE 88 CADRONA 89 FBITRUCK
90 WILLARD 91 FORKLIFT 92 TRACTOR 93 COMBINE 94 FELTZER 95 REMINGTN 96 SLAMVAN 97 BLADE 98 FREIGHT 99 STREAK
100 VINCENT 101 BULLET 102 CLOVER 103 SADLER 104 RANGER 105 HUSTLER 106 INTRUDER 107 PRIMO 108 TAMPA
109 SUNRISE 110 MERIT 111 UTILITY 112 YOSEMITE 113 WINDSOR 114 MTRUCK_A 115 MTRUCK_B 116 URANUS 117 JESTER
118 SULTAN 119 STRATUM 120 ELEGY 121 RCTIGER 122 FLASH 123 TAHOMA 124 SAVANNA 125 BANDITO 126 FREIFLAT
127 CSTREAK 128 KART 129 MOWER 130 DUNE 131 SWEEPER 132 BROADWAY 133 TORNADO 134 DFT30 135 HUNTLEY
136 STAFFORD 137 NEWSVAN 138 TUG 139 PETROTR 140 EMPEROR 141 FLOAT 142 EUROS 143 HOTDOG 144 CLUB 145 ARTICT3
146 RCCAM 147 POLICE_LA 148 POLICE_SF 149 POLICE_VG 150 POLRANGER 151 PICADOR 152 SWATVAN 153 ALPHA
154 PHOENIX 155 BAGBOXA 156 BAGBOXB 157 STAIRS 158 BOXBURG 159 FARM_TR1 160 UTIL_TR1 161 ROLLER |
bikes 162 BIKE 163 MOPED 164 DIRTBIKE 165 FCR900 166 NRG500 167 HPV1000 168 BF400 169 WAYFARER 170 QUADBIKE
171 BMX 172 CHOPPERB 173 MTB 174 FREEWAY | boats 175 PREDATOR 176 SPEEDER 177 REEFER 178 RIO 179 SQUALO
180 TROPIC 181 COASTGRD 182 DINGHY 183 MARQUIS 184 CUPBOAT 185 LAUNCH 186 SEAPLANE | flying 186 SEAPLANE
187 VORTEX 188 RUSTLER 189 BEAGLE 190 CROPDUST 191 STUNT 192 SHAMAL 193 HYDRA 194 NEVADA 195 AT400 196 ANDROM
197 DODO 198 SPARROW 199 SEASPAR 200 MAVERICK 201 COASTMAV 202 POLMAV 203 HUNTER 204 LEVIATHN 205 CARGOBOB
206 RAINDANC 207 RCBARON 208 RCGOBLIN 209 RCRAIDER.

(186 SEAPLANE is both the last boat slot and the first flying slot — matches `GetBoatPointer` 175..186 and
`GetFlyingPointer` 186..209.) Prefix-match hazards: a name that *starts with* an earlier table name is
silently mapped to it (e.g. `BUSX`→BUS, `BIKE2`→BIKE); lowercase never matches (→ 210).

---------------------------------------------------------------------------------------------------

## 2. vehicles.ide `cars` — `CFileLoader::LoadVehicleObject` 0x5B6F30

`sscanf(line, "%d %s %s %s %s %s %s %s %d %d %x %d %f %f %d")` = id, model, txd, type, handling, gameName,
anims, class, frequency, flags, compRules(hex), wheelModelId, wheelScaleFront, wheelScaleRear,
wheelUpgradeClass. Stack layout re-derived from the 15 pushes at 0x5B6F66..0x5B6FC1 (S = frame base):
id S+0x20 (default −1) · model S+0x84 [24] · txd S+0x9C [24, last local → ≥ 24 chars = return address] ·
**type S+0x18 [8]** (textdata_path.md said 12; the buffer is followed by `id` at S+0x20, so a type ≥ 8 chars
overwrites the already-parsed id — moot because only ≤ 7-char types are valid) · handling S+0x44 [16] ·
gameName S+0x54 [32] · anims S+0x74 [16] · class S+0x24 [16] · frequency S+0x38 · flags S+0x40 ·
compRules S+0x3C · wheelModelId S+0xC · scaleFront S+0x14 · scaleRear S+0x10 · upgradeClass S+0x34 (default −1).
General line rules (DAT-09/11/13) already exist; the vehicle-specific semantics:

| field | stored | rule |
|---|---|---|
| type | `+0x3C m_nVehicleType`; exact `CMPSB` incl. NUL: car 0, mtruck 1, quad 2, heli 3, plane 4, boat 5, train 6, f_heli 3(!), f_plane 8, bike 9, bmx 10, trailer 11 | selects the frame table `ms_vehicleDescs[type]` 0x8A7740 (§3); `f_heli` shares the HELI table index 3 (and `+0x3C = 3`), i.e. the FHELI table (index 7) is never used by an IDE type; `f_plane` forces wheel scales 1.0 |
| handling | `GetHandlingId` 0x6F4FD0: `strncmp(name, cell, 14)` over the 210 cells → **exact, case-sensitive**; not found → **210** → `+0x4A m_nHandlingId = 210` | **VEH-09**: every `CVehicle` ctor does `&m_aVehicleHandling[210]` = `gHandlingDataMgr+0xB814` = `m_aBikeHandling[1]` → mass = MOPED lean-COM 0.35, turnMass 0.25, … numGears/driveType from bike bytes → physics garbage (vehicle bounces away on contact, may NaN). `PreprocessHierarchy` also reads flags through `(byte)handlingId*0xE0 + 0xC2B9DC` (0x4C8E7A) — same memory |
| type ↔ handling class | `CBike` uses `m_aBikeHandling[id−162]` (negative index for non-bike ids → reads car data as bike data); `GetBoatPointer`/`GetFlyingPointer` fall back to slot 0 (PREDATOR / SEAPLANE) | **VEH-10** (Warning): bike/bmx → 162..174; boat → 175..186; heli/plane/f_heli/f_plane → 186..209 |
| anims | `SetAnimFile` 0x4C7670: `strcmp(name,"null")` (exact lowercase) → −1, else strdup; `ConvertAnimFileIndex` 0x4C76D0 → `GetAnimationBlockIndex` 0x4D3990 → −1 when no block of that name | **VEH-12** (Warning): must be `null` or the name of an `.ifp` in an IMG (block names come from the IMG directory; case-insensitive — vanilla `KART`, `BF_injection` vs `kart.ifp`, `bf_injection.ifp`); unknown → no anim block streamed with the vehicle → vehicle-specific ped anims missing |
| class | exact: normal 0 … bicycle 11, `ignore` 0xFF (and `ignore` returns **before** frequency is stored) | DAT-13 |
| frequency | `+0x52 m_nFrq` int16 | traffic weighting (`CLoadedCarGroup`); 0 = never random. Info |
| flags | `+0x4E` **byte** | > 255 truncated. Info |
| compRules | `+0x54` (see §3.5) | **VEH-11** |
| wheelModelId | car family: `+0x48` int16 — **unused** by the engine; bike/bmx: `+0x58 m_fBikeSteerAngle = (float)value` (degrees → `tan` in CBike ctor) | bikes: 0 < angle < 90 (vanilla 16/23), else steering NaN/∞ (garbage) |
| wheelScaleFront/Rear | `+0x40/+0x44` | **VEH-14**: > 0 for car family (wheel collision sphere radius = scale/2 in `SetupSuspensionLines`); boats/trains lines have 11 fields → fields stay 0 and are unused |
| wheelUpgradeClass | `+0x4F` byte | **VEH-15**: −1 or 0..3 (`ms_upgradeWheels[4][15]`, counters `ms_numWheelUpgrades[4]` 0xB4E470); ≥ 4 reads past the wheel table in the mod shop (garbage) |

The txd slot of every vehicle gets parent = slot `vehicle` (0x5B701C), so materials may reference textures of
`models\generic\vehicle.txd` (shared lookup) — a texture missing from both is just white (RW NULL texture).
gameName: `_`→space, `strncpy(+0x32, 8)` (GXT key; unresolved key shows the raw name — not a crash).

---------------------------------------------------------------------------------------------------

## 3. Vehicle DFF — what the engine does with the frame hierarchy

### 3.1 Load: `SetClump` 0x4C95C0

1. `m_pVehicleStruct = ms_pVehicleStructurePool->New()` (0x4C94C0) — **pool of 50** (`SetupCommonData`
   0x5B8F00: `CPool(0x32, "VehicleStruct")`). Full pool → NULL stored (0x4C95F5), no other handling.
2. `CClumpModelInfo::SetClump`, `SetAtomicRenderCallbacks` (0x4C7B10 → `SetAtomicRendererCB` 0x4C77E0 per
   atomic: frame name containing `_vlo` → LOD renderer 0x7331E0; else alpha material or name starting with
   `windscreen` → alpha renderer 0x733F80; else 0x733240; then `HideDamagedAtomicCB` hides atomics whose
   frame name contains `_dam`).
3. `SetFrameIds(ms_vehicleDescs[type])` 0x4C5460: for every table entry **without** flag 0x1 (VEH_STRUCT_PART)
   → `RwFrameForAllChildren(clumpFrame, FindFrameFromNameWithoutIdCB)` (0x4C52F0: `_stricmp`, only frames
   whose hierarchy id is still 0, recursive, **root frame excluded**) → `SetFrameHierarchyId(frame, id)`.
   Names are therefore case-insensitive; a second frame with the same name never gets an id (vanilla has such
   duplicates: dumper `headlights`×2, enforcer `taillights`×2, … — harmless, **VEH-17** Info).
4. `PreprocessHierarchy` 0x4C8E60 — two passes over the table:
   * pass 1, entries with flags & 0x20208 (DUMMY 0x8 / EXTRA 0x200 / UPGRADE 0x20000), found **by name**:
     - DUMMY (0x8): copies the frame position into `m_pVehicleStruct->m_avDummyPos[id]` (`[EDI+0x5C] + id*0xC`,
       write at **0x4C8F24 — no NULL check on m_pVehicleStruct**), transforms it through the parent chain
       (0x4C8F57), then **`RwFrameDestroy(frame)` 0x4C8F6E**. An atomic attached to a dummy frame keeps a
       pointer to the freed frame → use-after-free at the next render/LTM update (**VEH-04**, crash); child
       frames of a dummy lose their parent (garbage transform). The frame must be a childless empty.
     - UPGRADE (0x20000, `ug_*`): position + quaternion + parent hierarchy id stored in
       `m_aUpgrades[id]` (`+0xB4 + id*0x20`); frame kept.
     - EXTRA (0x200, `extra1..6`): `GetFirstObject(frame)` 0x4C8FF9 → `RpClumpRemoveAtomic(clump, obj)`
       0x4C9007 → 0x74A4C0 reads `[obj+0x44]` **without NULL check** → an `extraN` frame with no atomic
       crashes at 0x74A4C4 (**VEH-03**). Otherwise the atomic is detached and stored in
       `m_apExtras[m_nNumExtras++]` (slot order = table order extra1..extra6 among *present* extras, ≤ 6).
   * pass 1 also, entries with MAIN_WHEEL 0x10000 / TRAIN_FRONT_BOGIE 0x100000 (found by id): walks the frame
     and its first-child chain until a frame that owns an object → that atomic becomes the wheel/bogie
     template (vanilla: atomic on child frame `wheel` under `wheel_rf_dummy`; helis/RC/vortex have none —
     legal, wheels simply invisible).
   * pass 2, all other entries, found **by id** (`GetFrameFromId`): DOOR 0x10 → `m_nNumDoors++`;
     DAMAGEABLE 0x2 → child frames collapsed (`CollapseFramesCB`), `_ok`/`_dam` atomic pair detected
     (`GetOkAndDamagedAtomicCB`) and the damage mask bit set; WHEEL 0x4 → clone of the template wheel atomic
     attached (plus a second, offset clone on rear wheels when handling flag bit 29 of `handlingFlags` is set);
     TRAIN_REAR_BOGIE → clone of the front bogie. Every lookup here is NULL-checked — **missing components
     never crash at load**.
5. `ReduceMaterialsInVehicle` (dynamic list, no limit), `SetCarCustomPlate` 0x4C9450 → `CCustomCarPlateMgr::
   SetupClump` (materials whose texture is named `carplate` / `carpback` get the generated plate; absent → no
   plate, no crash).

### 3.2 Spawn — frames dereferenced without NULL checks

| type (`+0x3C`) | ctor → | unchecked frames |
|---|---|---|
| car 0, heli 3 (also `f_heli`), plane 4 | `CAutomobile` 0x6B0A90 → `CAutomobile::SetupSuspensionLines` 0x6A65D0 (4 wheels); `CPlane` 0x6C8E20 additionally calls `GetWheelPosn` for 4 wheels twice | `GetWheelPosn` 0x4C7D20: `GetFrameFromId(clump, ms_wheelFrameIDs[i])` (0x8A7770 = {5,7,2,4}) then `mov eax,[frame+0x40]` at **0x4C7DAD** (plane & !local: `[frame+0x10]` copy at **0x4C7D57**) — **no NULL test** → missing `wheel_lf_dummy`(5) / `wheel_lb_dummy`(7) / `wheel_rf_dummy`(2) / `wheel_rb_dummy`(4) = crash (**VEH-01**; confirmed in the live game, see memory `gta-vehicle-dummy-mechanism`). `wheel_lm/rm_dummy` (6/3) are never returned by GetWheelPosn (needed only by model 432 rhino: `GetFirstObject(m_aCarNodes[LM/RM])` unchecked in the ctor). |
| mtruck 1, quad 2, trailer 11 | `CMonsterTruck::SetupSuspensionLines` 0x6C7FB0, `CQuadBike::` 0x6CDCA0, `CTrailer::` 0x6CF1A0 — same `GetWheelPosn` calls (gta-reversed: 2/2/1 uses) | same four dummies (**VEH-01**) |
| bike 9, bmx 10 | `CBike::SetupSuspensionLines` 0x6B89B0: `ebp = [this+0x5B0]`/`[this+0x5B4]` (= `m_aBikeNodes[4]` wheel_front / `[5]` wheel_rear), `lea esi,[ebp+0x10]; rep movsd` at **0x6B8AC9** without testing ebp | `wheel_front`, `wheel_rear` required (**VEH-02**); `forks_rear` (`[this+0x5AC]`) is tested at 0x6B8B44 → optional |
| boat 5, train 6, f_plane 8 | `m_BoatNodes[...]` uses are NULL-checked in the code read; train/f_plane ctors not disassembled | no crash proven — Warning-level presence checks only |

All other table names are optional at load and spawn in the code read; missing DUMMY names leave
`m_avDummyPos[id] = (0,0,0)` (peds enter/sit at the model origin, exhaust/lights at origin — garbage, not a
crash; **VEH-22** Warning). `chassis` is NULL-checked where read (CPed); every vanilla model has it.

### 3.3 Per-type name tables (from `E:\RE\vehicle_descs.txt`; id in parentheses)

| IDE type | table | crash-required at spawn | DUMMY names (must be empty leaves, VEH-04) | extras / upgrades |
|---|---|---|---|---|
| car | CAR 0x8A6468 | wheel_rf_dummy(2) wheel_rb_dummy(4) wheel_lf_dummy(5) wheel_lb_dummy(7) | ped_frontseat ped_backseat headlights taillights headlights2 taillights2 exhaust engine petrolcap hookup ped_arm miscpos_a..d | extra1..6; ug_bonnet ug_bonnet_left ug_bonnet_right ug_bonnet_dam ug_bonnet_left_dam ug_bonnet_right_dam ug_spoiler ug_spoiler_dam ug_wing_left ug_wing_right ug_frontbullbar ug_backbullbar ug_lights ug_lights_dam ug_roof ug_nitro |
| mtruck | MTRUCK 0x8A6B80 | same four wheels | same 15 as car | extra1..6 (no ug_) |
| quad | QUAD 0x8A6D90 | same four wheels | same 15 as car | extra1..6 |
| heli, f_heli | HELI 0x8A6978 | same four wheels | same 15 as car | extra1..6 |
| plane | PLANE 0x8A6750 | same four wheels (parent-chain variant 0x4C7D57) | ped_frontseat ped_backseat headlights taillights headlights2 taillights2 exhaust engine petrolcap aileron_pos elevator_pos rudder_pos wingtip_pos miscpos_a miscpos_b | extra1..6 |
| trailer | TRAILER 0x8A7530 | same four wheels | same 15 as car | extra1..6 |
| boat | BOAT 0x8A6F80 | none proven | ped_frontseat | extra1..6 |
| train | TRAIN 0x8A7068 | none proven (bogie_front/rear, wheel_*1..3_dummy used by id) | ped_frontseat ped_backseat headlights taillights headlights2 taillights2 exhaust engine ped_left_entry ped_mid_entry ped_right_entry | — |
| f_plane | FPLANE 0x8A7218 | none proven | light_tailplane light_left light_right | — |
| bike | BIKE 0x8A7270 | wheel_front(4) wheel_rear(5) | ped_frontseat ped_backseat headlights taillights headlights2 taillights2 exhaust engine petrolcap hookup bargrip miscpos_a miscpos_b | extra1..6 |
| bmx | BMX 0x8A73D0 | wheel_front(4) wheel_rear(5) | same 13 as bike | extra1..6 |

Other (optional, id-matched) component names per table: car/trailer `chassis door_rf/rr/lf/lr_dummy
bump_front/rear_dummy wing_rf/lf_dummy bonnet_dummy boot_dummy windscreen_dummy exhaust_ok misc_a..e`
(trailer: misc_a..c); mtruck `… transmission_f transmission_r loadbay misc_a`; quad `… body_front/rear_dummy
suspension_rf/lf rear_axle handlebars misc_a misc_b`; heli `… static_rotor moving_rotor static_rotor2
moving_rotor2 rudder elevators misc_a..d`; plane `… static_prop moving_prop static_prop2 moving_prop2 rudder
elevator_l elevator_r aileron_l aileron_r gear_l gear_r misc_a misc_b`; boat `boat_moving_hi boat_rudder_hi
boat_flap_left/right boat_rearflap_left/right static_prop moving_prop static_prop2 moving_prop2
windscreen_hi_ok`; train `door_lf/rf_dummy wheel_rf1..3_dummy wheel_rb1..3_dummy wheel_lf1..3_dummy
wheel_lb1..3_dummy bogie_front bogie_rear`; f_heli `chassis_dummy toprotor backrotor tail topknot skid_left
skid_right`; f_plane `wheel_front_dummy wheel_rear_dummy propeller`; bike `chassis_dummy forks_front forks_rear
wheel_front wheel_rear mudguard handlebars misc_a misc_b`; bmx `chassis_dummy forks_front forks_rear
wheel_front wheel_rear handlebars chainset pedal_r pedal_l`.
Names present in **every** vanilla model of a type (scan of 212 DFFs): car — chassis chassis_dummy headlights
ped_frontseat + 4 wheels; bike — chassis chassis_dummy chassis_vlo engine exhaust forks_front forks_rear
handlebars headlights mudguard ped_frontseat taillights wheel_front wheel_rear; heli — chassis chassis_dummy
chassis_vlo moving_rotor static_rotor ped_frontseat + 4 wheels; plane — chassis ped_frontseat rudder + 4
wheels; boat — ped_frontseat; train — bogie_front bogie_rear chassis headlights taillights + 8 wheel dummies.
Vanilla: 0 dummy frames with atomics/children, 0 extras without atomic, 0 car-family models without the 4
wheel dummies.

### 3.4 Embedded collision — `ClumpCollisionStreamRead` 0x41B1D0 (clump extension 0x0253F2FA)

```
RwStreamRead(stream, PC_Scratch 0xC8E0C8, chunkLength)      ; 16 KB global, length unchecked  -> VEH-07
colModel = new CColModel (0x30 bytes, ctor 0x40FB60)
fourcc = *(u32*)scratch:  'COL2' -> LoadCollisionModelVer2(scratch+0x20, size-0x18, col, 0)   (0x537EE0)
                          'COL3' -> LoadCollisionModelVer3(scratch+0x20, size-0x18, col, 0)   (0x537CE0)
                          'COLL' -> LoadCollisionModel   (scratch+0x20, col, 0)               (0x537580)
                          other  -> LoadCollisionModel   (scratch+0x00, col, 0)   ; fourcc bytes parsed as COL1 data
CColModel::MakeMultipleAlloc; CCollisionPlugin::ms_currentModelInfo->SetColModel(col, true); flags |= 8
```
* Body layout = one complete COL entry: fourcc(4) size(4) name(22) modelId(2) payload (`size − 0x18` bytes).
* **VEH-05** (crash): a vehicle model with neither this plugin nor a same-named entry in a loaded `.col`
  archive has `m_pColModel == NULL`; `SetupSuspensionLines` reads `GetColModel()->m_pColData` on spawn
  (gta-reversed 0x6A65D0 / CBike 0x6B89B0: `mov eax,[esi+0x14]; mov edi,[eax+0x2C]` at 0x6B89C3 with no test)
  → crash. All 212 vanilla vehicle DFFs carry the plugin.
* **VEH-06** (warning): fourcc ∉ {COLL, COL2, COL3} (incl. COL4) → the whole body is parsed as a COL1 blob
  starting at the fourcc → garbage bounds/spheres. Vanilla `rccam.dff` has a 160-byte III/VC-style blob
  (`f2 d7 a8 3e…`) and does exactly this (RC vehicle; tolerated) — keep as Warning/Info (gtacheck COL-04).
* **VEH-07** (crash): body > 16384 bytes → `RwStreamRead` overruns `PC_Scratch` into the following globals.
  Also the COL3 payload goes through the same parser as .col files (COL-nn rules apply: face-group, shadow,
  bounds).

### 3.5 Extras and comp rules — `CreateInstance` 0x4C9680 (VEH-11)

Only when `m_nNumExtras != 0` (0x4C9697). `compRules` (IDE field 11, `+0x54`): bits 0-11 = up to three
4-bit component indices for rule A, bits 12-15 = rule A, bits 16-27 = components for rule B, bits 28-31 =
rule B. Rules: 1 ALLOW_ALWAYS, 2 ONLY_WHEN_RAINING, 3 MAYBE_HIDE, 4 FULL_RANDOM (random 0..5, ignores the
nibbles); nibble 0xF = unused. `ChooseComponent` 0x4C7FB0 returns `(comps >> 4*rand) & 0xF` where `rand <
CountCompsInRule(comps)` (number of nibbles ≠ F); with rule 1/2/3 and **all three nibbles F** the count is 0,
`GetRandomNumberInRange(0,0)` → 0 → the function returns **0xF**; `CreateInstance` then reads
`m_apExtras[0xF]` (`[edx+ebx*4+0x2F4]` at 0x4C96E2 = offset 0x330, past the 0x314-byte CVehicleStructure) and,
if non-NULL, `RpAtomicClone` on a garbage pointer → crash. Same for any nibble 6..14 (`m_apExtras` has 6
entries; 6..14 read m_nNumExtras/mask/next pool slot). Component indices refer to the **slot order** of the
extras actually present (extra1..extra6 in table order), not to the digit in the name. Vanilla rules: `0`,
`4fff`, `1f10`, `2ff0`, `3f10`, `3210`, `1012`, `3f01`, `3012`, `3f341210`, `30123345`, `1f341210` — all pass.

### 3.6 Shared textures, plates, env maps (VEH-19)

* `SetupCommonData` 0x5B8F00: `CTxdStore::LoadTxd(slot,"MODELS\\GENERIC\\VEHICLE.TXD")` — on failure the slot
  stays empty and `ms_pVehicleTxd` = NULL → `RwTexDictionaryFindNamedTexture(NULL,"vehiclelights128")` → crash
  (missing file). Missing `vehiclelights128` / `vehiclelightson128` textures → NULL light textures → crash
  when a vehicle's lights are rendered (Error).
* `CCustomCarPlateMgr::Initialise` 0x6FD500: `RwTextureRead("platecharset")` from the `vehicle` TXD →
  `mov byte [eax+0x50],1` at **0x6FD525 with no NULL check** (same for `plateback1/2/3` at 0x6FD53F/57/6F)
  → missing texture = crash at startup. Vanilla vehicle.txd: xvehicleenv128 vehicletyres128
  vehiclesteering128 vehiclespecdot64 vehicleshatter128 vehiclescratch64 vehiclepoldecals128 vehiclelightson128
  vehiclelights128 vehiclegrunge256 vehiclegeneric256 vehicleenvmap128 vehicledash32 platecharset plateback3
  plateback2 plateback1 carplate carpback.
* `LoadEnvironmentMaps` 0x4C8780: `RwTextureRead("white")` in the `particle` TXD, then `*(tex+0x10) = 0x40`
  **without NULL check** → `models\particle.txd` must contain `white` (vanilla: yes).
* Remap TXDs (**VEH-20**, Info): `AssignRemapTxd` 0x4C9360 strips trailing digits from a new TXD slot name
  and, if the rest names a vehicle model with id 400..630 (`GetModelInfo(name,400,0x276)`), calls `AddRemap`
  0x4C86D0 → `m_anRemapTxds[4]` (+0x2FA); the 5th and later remaps are written to the padding at +0x302
  (`for (i=0; slot[i]!=-1 && i<4; i++)` stops at 4) — silently dropped, no crash.
* Editable (recolourable) materials are picked by material colour (`SetEditableMaterialsCB` 0x4C8220), plate
  materials by texture name `carplate`/`carpback` — Info only.

---------------------------------------------------------------------------------------------------

## 4. carcols.dat — `CVehicleModelInfo::LoadVehicleColours` 0x5B6890 (asm-level; Ghidra dropped the body)

Reader: `CFileMgr::ReadLine(f, buf[1024], 0x400)` (**handle unchecked → HND-01**); leading bytes ≤ 0x20
skipped; `,` and `\r` → space up to `\n`/NUL; empty or `#`-first lines skipped. Section 0: `col` (3-char
prefix) → 1, `car4` (4-char) → 3, `car` → 2, anything else ignored; inside a section `end` (prefix) → 0.

* **col** (0x5B6A22): `sscanf("%d %d %d")` → stored as bytes at `ms_vehicleColourTable` 0xB4E480 + 4·n
  (order r,g,b,255 with the pointer pre-incremented), **no count limit**. Table = 128 entries (0xB4E480..0xB4E680,
  next symbol `ms_pVehicleStructurePool` 0xB4E680). **HND-09**: entry 129 clobbers the pool pointer (reassigned
  later in SetupCommonData), 130 → 0xB4E684, 131..133 → `ms_pVehicleTxd`/`ms_pLightsTexture`/
  `ms_pLightsOnTexture` (reassigned later), 134+ → unnamed statics up to CAnimManager 0xB4EA28 → garbage.
  After the sscanf the code scans from `line+1` for a `#` byte (0x5B6A68..0x5B6A77, result unused, read-only)
  — a col line without a `#` comment reads past the 1 KB buffer until some 0x23 byte (Info). Vanilla: 127
  colours, every line commented.
* **car** (0x5B6A8E): `sscanf("%s %d×16")` → n; `mi = CModelInfo::GetModelInfo(name, NULL)` 0x4C5940 (NULL when
  no model has that name); `numVariations = (n−1)/2` written to `[mi+0x2D0]` at **0x5B6B2F with no NULL test**
  → unknown name = write to address 0x2D0 = **crash (HND-08)**. Then pairs copied into `+0x2B0`/`+0x2B8`
  (tertiary/quaternary `+0x2C0`/`+0x2C8` zeroed) — max 8 (sscanf limit), no overflow. A name that is a
  **non-vehicle** model writes 0x2B0..0x2D0 past a 0x20-byte CAtomicModelInfo → corrupts neighbouring store
  entries (garbage).
* **car4** (0x5B6B80): `sscanf("%s %d×32")`, `numVariations = (n−1)/4`, four colours per variation, same NULL
  hazard at 0x5B6CBF.
* Colour indices are bytes; `ChooseVehicleColour` 0x4C8500 indexes `ms_vehicleColourTable[idx]` without a
  bound → idx ≥ colourCount reads other statics (garbage colour). **Vanilla moonbeam uses 227 with 127
  colours** → Warning level (HND-09b). Vehicles absent from carcols get `m_nNumColorVariations = 0` → colour
  0/0/0/0 (0x4C850E), Info (**HND-10**).

---------------------------------------------------------------------------------------------------

## 5. carmods.dat — `CVehicleModelInfo::LoadVehicleUpgrades` 0x5B65A0

`LoadLine` reader (handle unchecked → HND-01); `#`/empty skipped; section headers by prefix `link`/`mods`/
`wheel`, `end` closes; tokens by `strtok(" \t,")` (0x2C0920). **Every model name goes through
`CModelInfo::GetModelInfo(name,&id)` 0x4C5940 followed by `CAtomicModelInfo::SetupVehicleUpgradeFlags`
0x4C4570 called thiscall with ECX = the returned pointer** (0x5B66EF/0x5B6773/0x5B6825/0x5B6848/0x5B67B6/
0x5B67E1); its first real instruction is `mov ax,[ecx+0x12]` (0x4C4576) → **unknown model name = crash
(HND-11)**. Details:

* **link**: two names → ids (−1 when unknown, but the crash above comes first) → `AddUpgradeLink` 0x4C74B0 on
  `ms_linkedUpgrades` 0xB4E6D8: `m_anUpgrade1[30]`, `m_anUpgrade2[30]` (+0x3C), `count` (+0x78) — **no bound**:
  the 31st link writes `upgrade2[30]` = the count itself (loop corruption, garbage). Vanilla: 23 links.
* **mods**: first token = vehicle → `EBP = GetModelInfo(vehicle)` (0x5B6742, **not NULL-checked**: unknown
  vehicle → `mov [ebx],cx` at 0x5B6784 with EBX = 0x2D6 → crash); each following token → id stored at
  `EBP + 0x2D6 + 2·i` (`m_anUpgrades[18]`), then `hydralics` at slot i and `stereo` at slot i+1 (both must
  exist as models — vanilla has them). **> 16 upgrades per line** overflow into `m_anRemapTxds` (+0x2FA) and
  `m_nAnimBlockIndex` (+0x304 — a garbage anim block index is used by streaming → crash) (**HND-12**). The
  vehicle must be a `cars` model: for a smaller model info the writes land in the neighbouring store entries.
  An upgrade name that is unknown would crash (above); a *duplicate* id is harmless.
* **wheel**: `sscanf("%d",&group)` then names → `AddWheelUpgrade(group,id)` 0x4C8700:
  `ms_upgradeWheels[group*15 + ms_numWheelUpgrades[group]++]` at 0xB4E3F8 (int16, **[4][15]**), counters at
  0xB4E470 (int16 ×4) — **no bound on group or count**: group ≥ 4 or a 16th wheel in a group writes the
  counters / `ms_compsUsed` 0xB4E478 / `ms_pRemapTexture` 0xB4E47C / the colour table → garbage (**HND-12**).
  Vanilla: groups 0,1,2 with 10/10/9 models.
* `SetupVehicleUpgradeFlags` assigns the mod-shop component slot by **name prefix** (strings at 0x85BB00..):
  `chss_ wheel_ exh_ fbmp_ rbmp_` (dummy-positioned) and `bnt_ bntl_ bntr_ spl_ wg_l_ wg_r_ fbb_ bbb_ lgt_ rf_
  nto_ hydralics stereo` (chassis-positioned); a name with none of these gets no slot → the part cannot be
  fitted / garbage in the shop (**HND-13**, Warning). Vanilla: all names match.

---------------------------------------------------------------------------------------------------

## 6. cargrp.dat — `CPopulation::LoadCarGroups` 0x5BD1A0

`CFileMgr::ReadLine(f, buf[1024], 0x400)` (handle unchecked → HND-01). Per line: scan forward replacing `,`
and `\r` with spaces **until a 0x0A byte** — the loop tests only `!= '\n'`, never NUL, so the **last line
without a trailing newline scans (and writes) past the 1 KB stack buffer** until some 0x0A byte turns up →
stack corruption (**HND-14**, crash; vanilla ends with CRLF). Then up to **23** tokens (`iVar9 < 0x17`,
counting failed lookups too; the rest of the line is ignored — vanilla has lines with 29 tokens → Info);
a token starting with `#` ends the line. Each token → `GetModelInfo(name,&id)`; **NULL → skipped silently**
(Warning), found → `m_aCarGroups[group*23 + n] = id` at 0xC0ED38 **without checking that it is a vehicle
model** → an object/ped name is later handed to `CCarCtrl` as a car model → crash when traffic picks it
(**HND-15**). A line with ≥ 1 accepted name becomes the next group: `m_nNumCarsInGroup[group]` at 0xC0EC78
(int16 × 34 = 0xC0EC78..0xC0ECBC) and unused slots filled with 2000; **> 34 groups** write the count into
`m_nNumPedsInGroup[0]` 0xC0ECC0 and the models into `m_PedGroups` 0xC0F358 (loaded *before* car groups by
`CPopulation::Initialise`) → ped groups garbage → crash on ped spawn (**HND-16**). Vanilla: 34 groups.

---------------------------------------------------------------------------------------------------

## 7. Rule table

Severity: Crash / Error (visible garbage) / Warning / Info. "check" = how an offline checker decides it.

### VEH — vehicles.ide + vehicle DFF

| id | sev | condition | evidence | check |
|---|---|---|---|---|
| VEH-01 | Crash | type car/mtruck/quad/heli/f_heli/plane/trailer and the DFF lacks any of `wheel_lf_dummy`, `wheel_lb_dummy`, `wheel_rf_dummy`, `wheel_rb_dummy` (non-root frames, case-insensitive) | GetWheelPosn 0x4C7D20: deref at 0x4C7DAD / 0x4C7D57 without NULL test; callers SetupSuspensionLines 0x6A65D0/0x6C7FB0/0x6CDCA0/0x6CF1A0, CPlane ctor 0x6C8E20; live-game confirmed | parse FRAME_LIST names (FRAME_NAME 0x253F2FE ext., NUL-trimmed), exclude frame 0 (root), stricmp against the four names |
| VEH-02 | Crash | type bike/bmx and the DFF lacks `wheel_front` or `wheel_rear` | CBike::SetupSuspensionLines 0x6B89B0: `lea esi,[ebp+0x10]; rep movsd` at 0x6B8AC9, ebp = m_aBikeNodes[4]/[5], no NULL test | as VEH-01 |
| VEH-03 | Crash | a frame named `extra1`..`extra6` (any type) owns no atomic | PreprocessHierarchy: GetFirstObject 0x4C8FF9 → RpClumpRemoveAtomic 0x4C9007 → 0x74A4C4 reads `[atomic+0x44]` with atomic = NULL | for each ATOMIC chunk collect frameIndex; every `extraN` frame must appear |
| VEH-04 | Crash | a DUMMY-flag frame (per-type list §3.3: ped_frontseat, ped_backseat, headlights, taillights, headlights2, taillights2, exhaust, engine, petrolcap, hookup, ped_arm, miscpos_a..d, aileron_pos, elevator_pos, rudder_pos, wingtip_pos, bargrip, ped_left/mid/right_entry, light_tailplane/left/right) owns an atomic | PreprocessHierarchy copies the position then RwFrameDestroy(frame) 0x4C8F6E; the atomic's frame pointer dangles → use-after-free at render | atomic.frameIndex must not point at a dummy-named frame |
| VEH-04b | Warning | a DUMMY-flag frame has child frames | same destroy; children orphaned → transforms garbage | any frame whose parent index is a dummy frame |
| VEH-05 | Crash | vehicle DFF has no clump extension 0x0253F2FA and no `.col` entry with the model name | m_pColModel NULL → SetupSuspensionLines reads GetColModel()->m_pColData (CBike 0x6B89C3 `mov edi,[eax+0x2C]` unchecked) | `d.hasCollision` or COL cross-ref |
| VEH-06 | Warning | collision plugin body fourcc ∉ {COLL, COL2, COL3} (COL4 or junk) | ClumpCollisionStreamRead 0x41B268: anything else → LoadCollisionModel(scratch+0) parses the fourcc as COL1 data | first 4 bytes of the extension body; vanilla rccam.dff trips it (gtacheck COL-04) |
| VEH-07 | Crash | collision plugin body > 16384 bytes, or < 0x20 bytes / `size` field ≠ bodyLen − 8 | RwStreamRead into PC_Scratch 0xC8E0C8 (16 KB) with the chunk length, no bound (0x41B1FB); header fields read from the scratch buffer | extension chunk length; `size − 0x18` must equal payload length |
| VEH-08 | Info | more than 50 vehicle models resident at once (mission scripts / IPL `cars` generators with many distinct models, limit-adjuster builds) | SetClump 0x4C95C0: pool `CPool(50,"VehicleStruct")` New → NULL, then PreprocessHierarchy writes `[NULL + id*0xC]` at 0x4C8F24 | not file-decidable; report as a limit note (count distinct vehicle models per IPL cars cluster) |
| VEH-09 | Error | vehicles.ide handling name not exactly one of the 210 names (§1.4, uppercase, ≤ 14 chars) | GetHandlingId 0x6F4FD0 returns 210 → m_nHandlingId 210 → `&m_aVehicleHandling[210]` = 0xC2B9C8+0xB814 = m_aBikeHandling[1] used as tHandlingData (mass 0.35 …) | exact string compare against the list |
| VEH-10 | Warning | type bike/bmx with handling id ∉ 162..174; boat ∉ 175..186; heli/plane/f_heli/f_plane ∉ 186..209 | CBike reads m_aBikeHandling[id−162] (negative index → car data as bike data); GetBoatPointer/GetFlyingPointer 0x6F5300/0x6F52D0 fall back to slot 0 (PREDATOR / SEAPLANE) | index of the name in §1.4 vs type |
| VEH-11 | Crash | compRules with rule nibble ∈ {1,2,3} whose three comp nibbles are all F, or any comp nibble in 6..14, **and** the DFF has ≥ 1 extra | ChooseComponent 0x4C7FB0 returns 0xF (CountCompsInRule = 0 → rand 0 → nibble 0) or 6..14; CreateInstance 0x4C96E2 `[edx+ebx*4+0x2F4]` reads past m_apExtras[6] (struct 0x314) → RpAtomicClone(garbage) | decode hex: A = bits0-11/12-15, B = bits16-27/28-31 |
| VEH-12 | Warning | anims field ≠ `null` and no `<anims>.ifp` in any IMG (case-insensitive) | SetAnimFile 0x4C7670 / ConvertAnimFileIndex 0x4C76D0 → GetAnimationBlockIndex 0x4D3990 = −1 → block never streamed, vehicle-specific ped anims missing | compare with IMG `.ifp` names |
| VEH-13 | Error | handling.cfg car line field 35 (animGroup) ≥ 30 or > highest `^` index defined | byte +0xDE indexes m_vehicleAnimGroups[30] 0xC1CDC0 (0x94 each) | numeric range |
| VEH-14 | Error | car-family line with wheelScaleFront/Rear ≤ 0; bike/bmx with steer angle (field 12) ≤ 0 or ≥ 90 | wheel collision sphere radius = scale/2 (SetupSuspensionLines); CBike: `tan(deg2rad(angle))` | numeric range |
| VEH-15 | Warning | wheelUpgradeClass ∉ {−1,0,1,2,3} | ms_upgradeWheels[4][15] / ms_numWheelUpgrades[4] indexed by class in the mod shop | numeric range |
| VEH-16 | Info | (documentation) LoadVehicleObject buffers: type 8, class 16, handling 16, gameName 32, anims 16, model 24, txd 24 (last local) | stack layout 0x5B6F66..0x5B6FC1 | already DAT-09/13 |
| VEH-17 | Info | duplicate frame names in a vehicle DFF | SetFrameIds: only the first frame with id 0 gets the id (FindFrameFromNameWithoutIdCB 0x4C52F0); vanilla has duplicates | name histogram |
| VEH-18 | Info | naming conventions: `_dam` atomics hidden until damage (HideDamagedAtomicCB), `_vlo` = LOD renderer, `windscreen*` = alpha renderer; wheel template atomic must be on `wheel_rf_dummy` or its first-child chain (else all wheels invisible — vanilla helis/RC) | SetAtomicRendererCB 0x4C77E0, PreprocessHierarchy MAIN_WHEEL walk 0x4C9083.. | frame/atomic names |
| VEH-19 | Crash | `models\generic\vehicle.txd` missing, or lacks `platecharset`/`plateback1`/`plateback2`/`plateback3`; `models\particle.txd` lacks `white` | SetupCommonData 0x5B8F00 (`RwTexDictionaryFindNamedTexture(NULL,…)`); CCustomCarPlateMgr::Initialise 0x6FD525/0x6FD53F/0x6FD557/0x6FD56F write `[tex+0x50]` unchecked; LoadEnvironmentMaps 0x4C8780 writes `[tex+0x10]` unchecked | TXD name lists |
| VEH-19b | Error | vehicle.txd lacks `vehiclelights128` / `vehiclelightson128` | ms_pLightsTexture/ms_pLightsOnTexture NULL (0x5B8F9E..) → used unchecked by vehicle light rendering | TXD name list |
| VEH-20 | Info | more than 4 IMG TXDs named `<vehicle><digits>.txd` for one vehicle (id 400..630) | AssignRemapTxd 0x4C9360 → AddRemap 0x4C86D0 stores at most 4, the rest go to padding +0x302 | IMG directory scan |
| VEH-21 | Info | f_heli in vehicles.ide uses the HELI table (type 3), never the FHELI table (index 7); train / f_plane / boat spawn paths not disassembled | LoadVehicleObject: `f_heli` → `+0x3C = 3` | — |
| VEH-22 | Warning | DUMMY names present in every vanilla model of the type are missing (car: ped_frontseat, headlights; bike: ped_frontseat, headlights, taillights, engine, exhaust; boat: ped_frontseat; train: headlights, taillights) or `chassis`/`chassis_dummy` missing | m_avDummyPos stays (0,0,0) → peds enter/sit at the origin, lights/exhaust at origin (garbage); chassis NULL-checked but universal in vanilla | name presence |

### HND — handling.cfg / carcols.dat / carmods.dat / cargrp.dat

| id | sev | condition | evidence | check |
|---|---|---|---|---|
| HND-01 | Crash | `data\handling.cfg`, `data\carcols.dat`, `data\carmods.dat`, `data\cargrp.dat` missing (also `models\generic\vehicle.txd` → VEH-19) | OpenFile results unchecked: 0x5BD855 → LoadLine(NULL); 0x5B68B0 → ReadLine(NULL); 0x5B65C0.. ; 0x5BD1C0.. | file existence (modloader-aware) |
| HND-02 | Crash | handling.cfg contains a blank / whitespace-only line, or a data line with no token (also `!` with no name as the first bike line) | car branch: strtok → NULL, no test (0x5BE1B8) → FindExactWord 0x6F4F30 strncmp(NULL); `!`: ConvertBikeDataToGameUnits(NULL) 0x6F5290 | after LoadLine normalisation (ctrl/`,` → space, trim) the line is empty and not `;`-prefixed |
| HND-03 | Error | data line whose name is not one of the 210 names (§1.4) — includes `#`-comment lines and lowercase names | FindExactWord 0x6F4F30 returns 210 → car: 0xE0 bytes over m_aBikeHandling[1..] (0xB814); `!`: 0xC3D4 (boats 2-3); `$`: 0xC354 (boats 0-1); `%`: PREDATOR slot; then Convert*ToGameUnits on the garbage | table lookup; **Warning** when the token only prefix-matches (e.g. `BUSX`) — engine silently maps it |
| HND-04 | Error | `!` line naming a non-bike id (∉162..174), `$` line naming id ∉ 186..209, `%` line naming id ∉ 175..186 | `!`/`$` bases 0x8F54/0x7B24 + id·stride land inside m_aVehicleHandling for small ids (e.g. `$ LANDSTAL` overwrites EMPEROR at 0x7B24); `%` → boat[0] overwritten (GetBoatPointer 0x6F5300) | index range per prefix |
| HND-05 | Warning | token count ≠ car 36 / `!` 17 / `$` 23 / `%` 16 / `^` 36; non-numeric numeric field; driveType ∉ {F,R,4}; engineType ∉ {P,D,E} | fixed-slot switch loops 0x5BE1C0.. (missing → stale/zero, extra → ignored); atof/atol → 0; chars taken verbatim (+0x74/+0x75) | count tokens after LoadLine normalisation |
| HND-06 | Error | car line: mass ≤ 0, percentSubmerged ≤ 0 or > 255, numGears ∉ 1..5, maxVel ≤ 0, steeringLock ≤ 0, frontLights/rearLights ∉ 0..3, animGroup ≥ 30 (VEH-13) | ConvertDataToGameUnits 0x6F50C7 (`1/mass`), 0x6F50D6 (`fidiv percentSubmerged`); InitGearRatios 0x6D0460 writes m_aGears[1..n] into a 6-slot array (n ≥ 6 clobbers +0x74.. incl. numGears itself); byte stores | numeric ranges |
| HND-07 | Error | `^` line with first field ∉ 0..29, or ≠ 36 tokens, or duplicated/missing indices | CopyAnimGroup into 0xC1CDC0 + idx·0x94 (0x5BE182) unbounded | numeric range; vanilla 0..29 |
| HND-08 | Crash | carcols.dat `car`/`car4` line whose name is not a defined model; **Error** if the model is not a `cars` entry | GetModelInfo 0x4C5940 NULL → `mov [esi+0x2D0],al` at 0x5B6B2F / 0x5B6CBF; non-vehicle → writes +0x2B0..+0x2D0 past the model-info object | name ∈ IDE cars |
| HND-09 | Error | more than 128 `col` entries | ms_vehicleColourTable 0xB4E480 = 128×4; entries 129+ overwrite ms_pVehicleStructurePool 0xB4E680, 0xB4E684, ms_pVehicleTxd 0xB4E688, ms_pLightsTexture, ms_pLightsOnTexture, then unnamed statics (colours < 134 are harmless because SetupCommonData re-assigns those five later) | count col lines |
| HND-09b | Warning | colour index in a car/car4 line ≥ number of col entries (or > 255) | ChooseVehicleColour indexes the table unbounded (stored as byte) → garbage colour; **vanilla moonbeam uses 227 of 127** | numeric |
| HND-09c | Warning | col line RGB outside 0..255 | stored as bytes (0x5B6A4F..0x5B6A5F) | numeric |
| HND-10 | Info | vehicle absent from carcols (colours 0,0,0,0); car line with > 16 / car4 with > 32 numbers (extra ignored, odd tails dropped: variations = (n−1)/2 or /4); col line without `#` (dead scan past the buffer) | ChooseVehicleColour 0x4C850E; sscanf argument counts; 0x5B6A68 scan loop | — |
| HND-11 | Crash | carmods.dat: any model name (mods vehicle, mods parts, link pairs, wheel lists) not defined in an IDE; `hydralics` or `stereo` model missing | SetupVehicleUpgradeFlags 0x4C4570 called with ECX = GetModelInfo() result, `mov ax,[ecx+0x12]` at 0x4C4576; mods vehicle: `mov [ebx],cx` at 0x5B6784 with EBX = NULL+0x2D6 | name ∈ IDE (any section); vehicle name ∈ `cars` (else Error: writes past a smaller model info) |
| HND-12 | Error | > 16 parts on a `mods` line; > 30 `link` lines; wheel group ∉ 0..3; > 15 models in a wheel group | m_anUpgrades[18] (+0x2D6) minus hydralics/stereo → 17th writes m_anRemapTxds/+0x304 anim block index (streaming crash); CLinkedUpgradeList 0xB4E6D8 [30]/[30]/count (31st overwrites count); ms_upgradeWheels 0xB4E3F8 [4][15] + counters 0xB4E470 (AddWheelUpgrade 0x4C8700 unbounded) | counts |
| HND-13 | Warning | carmods part name without a known prefix (`chss_ wheel_ exh_ fbmp_ rbmp_ bnt_ bntl_ bntr_ spl_ wg_l_ wg_r_ fbb_ bbb_ lgt_ rf_ nto_`, or exactly `hydralics`/`stereo`) | SetupVehicleUpgradeFlags prefix table 0x85BB00.. → no component slot | prefix test |
| HND-14 | Crash | cargrp.dat last line has no trailing `\n`; any line ≥ 1024 bytes | LoadCarGroups line scan 0x5BD1F0.. stops only at 0x0A and rewrites `,`/`\r` bytes → runs off the 1 KB stack buffer; ReadLine splits long lines | file tail byte / line length |
| HND-15 | Crash | cargrp.dat name that is a defined model but not a `cars` entry; **Warning** for names that are no model at all (silently skipped) | GetModelInfo result stored as a car model without a type check (0x5BD2B0..); NULL → skipped | name ∈ cars / ∈ any IDE |
| HND-16 | Error | more than 34 groups (lines with ≥ 1 accepted name) | m_nNumCarsInGroup[34] 0xC0EC78 / m_aCarGroups[34][23] 0xC0ED38 → 35th writes m_nNumPedsInGroup 0xC0ECC0 and m_PedGroups 0xC0F358 (ped groups already loaded → garbage ped models) | count groups |
| HND-16b | Info | more than 23 names on a cargrp line (rest ignored; vanilla up to 29) | `iVar9 < 0x17` token cap | — |
| HND-17 | Info | handling.cfg without the `;the end` terminator (parsing just continues to EOF); line > 511 chars (split, DAT-01) | 0x5BD890 compare; LoadLine 512-byte buffer | — |
| HND-18 | Info | CStreaming::LoadInitialVehicles 0x407F20 is an empty `ret` in 1.0 US; LoadZoneVehicle 0x40B4B0 only requests what cargrp/popcycle select | disassembly | — |

---------------------------------------------------------------------------------------------------

## 8. Vanilla spot-check (D:\Grand Theft Auto San Andreas)

* handling.cfg: 516 lines, 0 blank, 210 car / 13 `!` / 24 `$` / 12 `%` / 30 `^` lines, token counts exactly
  36/17/23/16/36, all names exact table matches, max gears 5, mass and percentSubmerged > 0, `^` ids 0..29,
  terminator `;the end` present. HND-02..07 pass.
* vehicles.ide: 212 lines (11 boat lines with 11 fields — trailing fields default, already DAT-11 Info); all
  handling names exact; types/classes valid; compRules all valid (`4fff` is rule 4 → ignores nibbles);
  wheel classes −1/0/1/2; anims ∈ {null + 21 IFP names present in gta3.img}. VEH-09..15 pass.
* carcols.dat: 127 colours (all commented), 199 vehicles, max 16/32 numbers per line, all names are `cars`
  models; **moonbeam colour 227** → HND-09b Warning (the only vanilla hit).
* carmods.dat: 23 links, ≤ 16 parts per mods line, wheel groups 0..2 (10/10/9), every name an IDE model with
  a known prefix; hydralics/stereo defined. HND-11..13 pass.
* cargrp.dat: 34 groups, CRLF-terminated, all names are `cars` models, longest line 29 tokens (HND-16b Info).
* DFFs (212 vehicles in gta3.img): 211 × COL3 plugin + rccam (VEH-06 Warning); no extras without atomics; no
  dummy frames with atomics or children; all car-family models have the 4 wheel dummies (helis/RC/vortex have
  the dummies but no wheel geometry — legal); bikes/bmx have wheel_front/wheel_rear; duplicate names exist
  (VEH-17 Info). vehicle.txd and particle.txd contain every texture named in VEH-19.

## 9. Not verified / open

* Train (`CTrain` 0x6F6030), boat (`CBoat` 0x6F2940 beyond the gta-reversed reading), `f_plane` and
  `CHeli`-specific rotor code were not disassembled — their optional/required frame sets are Warning-level only.
* `RwFrameDestroy` semantics for children of a dummy frame (orphaned vs. destroyed) taken from RW behaviour,
  not re-verified in the exe (VEH-04b kept at Warning).
* The exact runtime effect of handling id 210 (VEH-09) is "bike memory read as car handling"; whether a given
  model crashes or merely misbehaves depends on the values — classified Error.
* `CAnimManager::GetAnimationBlock` 0x4D3940 is obfuscated (0x4D3945 trampoline); case-insensitivity inferred
  from vanilla `KART`/`BF_injection` matching `kart.ifp`/`bf_injection.ifp`.
