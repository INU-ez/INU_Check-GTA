# WEAPONS and PED data files — gta_sa.exe 1.0 US (base 0x400000)

Rules `WPN-nn` (weapon.dat and everything keyed off weapon types) and `PDD-nn` (ped.dat, pedstats.dat,
pedgrp.dat, popcycle.dat, animgrp.dat grammar, peds.ide cross-references, decision makers, surface*.dat,
shopping.dat, statdisp.dat, ar_stats.dat).

Sources: Ghidra dumps `E:\RE\asm_data2\<ADDR>.c/.asm` (LoadWeaponData 0x5BE670, CWeaponInfo::Initialise
0x5BF750, LoadInitialWeapons 0x40A120, CPedType::LoadPedData 0x608B30 / Initialise 0x608E40,
CPedStats::LoadPedStats 0x5BB890, CPopulation::LoadPedGroups 0x5BCFE0, CPopCycle::Initialise 0x5BC090,
ReadAnimAssociationDefinitions 0x5BC910, CDecisionMakerTypes::LoadEventIndices 0x600840, CGangs::Initialise
0x5DE680, SurfaceInfos_c::LoadAdhesiveLimits 0x55D0E0 / LoadSurfaceInfos 0x55EB90 / LoadSurfaceAudioInfos
0x55F2B0, CShopping::LoadStats 0x49B6A0 / LoadPrices 0x49B8D0 / LoadShop 0x49BBE0 / Init 0x49C290,
FindPedRaceFromName 0x5B6D40, CGame::Initialise 0x53BC80), `asm_map\005B7420` (CFileLoader::LoadPedObject),
`asm\005BCF30` (FindWeaponFireType), `asm\004D3BA0` (AddAnimAssocDefinition), `asm_skin\005BF6B0`
(CAnimManager::Initialise), `asm*\004E3C60/004E3CD0` (GetAudioPedType / GetVoice). Functions with no dump were
disassembled with capstone from the exe (scratchpad `csdis.py`): FindWeaponType 0x743D10, GetWeaponInfo
0x743C60, melee base-combo lookup 0x61DB30, CPedType::FindPedType 0x608790, CPedStats::GetPedStatType
0x6088D0, CPedStats::Initialise 0x5BF9D0, GetSurfaceIdFromName 0x55D220, CStreaming::RequestModel 0x4087E0,
CPed::GiveWeapon 0x5E6080, PedEvent.txt reader 0x5BB9F0, decision-maker file reader 0x6076B0,
CShopping::FindSection 0x49AE70 (body at 0x15659E0), GetNextSection 0x49AF10 (0x156B920),
GetPriceSectionFromName 0x49AAD0, GetKey 0x49AB30 (0x1561A10), stat-name lookup 0x1565360,
CStats::LoadStatUpdateConditions 0x559860 / LoadActionReactionStats 0x5599B0, CGame::Init1/Init2
0x5BF840/0x5BA1A0, InitialiseCoreDataAfterRW 0x5BFA90. Constants read with `readva.py`. Name tables dumped
from the exe's .data. gta-reversed / plugin-sdk used only for names and for struct sizes that I then verified
by address arithmetic (stated per item). Vanilla data read from `D:\Grand Theft Auto San Andreas\data`
(read-only) and passed through every rule below (spot-check scripts in the scratchpad; results quoted per
section).

CRT helpers identified from the exe: 0x8220AD = `sscanf`, 0x8214D0 = `strncmp` (case-sensitive), 0x8229B6 =
`_stricmp` (locale tolower per char), 0x82244B = `strtok`, 0x536F80 = `CFileLoader::LoadLine` (fgets 512,
bytes < 0x20 and `,` -> space, ltrim; NEVER checks the FILE* — a missing file crashes in fgets, see
textdata_path.md DAT-03), 0x5389B0 = `CFileMgr::ReadLine` (raw fgets), 0x538900 = `CFileMgr::OpenFile`
(fopen, NULL when missing).

Legend: **crash** = access violation / heap or stack corruption; **garbage** = engine proceeds with wrong
data; **ignored** = silently dropped; **info**. Severity mapping for gtacheck: crash -> Краш, garbage ->
Ошибка, ignored -> Предупреждение, info -> Инфо (vanilla gives 0 Краш on every rule here; the only vanilla
hits are the three Info items noted in 6.4 and 5.2).

---------------------------------------------------------------------------------------------------

## 0. When each parser runs (startup order, from CGame::Initialise 0x53BC80 and its callees)

| phase | file | parser |
|---|---|---|
| `CGame::InitialiseCoreDataAfterRW` 0x5BFA90 (before Init1) | data\surface.dat, surfinfo.dat, surfaud.dat | `SurfaceInfos_c::Init` 0x55F420 -> 0x55D0E0 / 0x55EB90 / 0x55F2B0 |
| same | DATA\PEDSTATS.DAT | `CPedStats::Initialise` 0x5BF9D0 -> `LoadPedStats` 0x5BB890 (call at 0x5BFA7C) |
| same (tail-jump 0x5BFA84 -> 0x5BF400) | data\decision\PedEvent.txt, data\decision\allowed\*.ped/*.grp | `LoadDefaultDecisionMaker` 0x5BF400 -> `LoadEventIndices` 0x600840 -> 0x5BB9F0; `LoadDecisionMaker` 0x6076B0 |
| same | POPCYCLE.DAT | `CPopCycle::Initialise` 0x5BC090 |
| `CGame::Init1` 0x5BF840 | DATA\ANIMGRP.DAT | `CAnimManager::Initialise` 0x5BF6B0 -> `ReadAnimAssociationDefinitions` 0x5BC910 |
| `CFileLoader::LoadLevel` (IDE `peds` section) | *.ide | `CFileLoader::LoadPedObject` 0x5B7420 (called from 0x5B8654) |
| `CGame::Init2` 0x5BA1A0 | – | `CStreaming::LoadInitialWeapons` 0x40A120 (models 334, 345, 346) |
| Init2 | DATA\PED.DAT | `CPed::Initialise` 0x5DEBB0 -> `CPedType::Initialise` 0x608E40 -> `LoadPedData` 0x608B30 |
| Init2 | DATA\WEAPON.DAT | `CWeapon::InitialiseWeapons` 0x73A300 -> `CWeaponInfo::Initialise` 0x5BF750 -> `LoadWeaponData` 0x5BE670 (all IDEs are loaded by now) |
| Init2 | \DATA\PEDGRP.DAT | `CPopulation::Initialise` 0x610E10 -> `LoadPedGroups` 0x5BCFE0 |
| Init2 | – | `CGangs::Initialise` 0x5DE680 (hard-coded gang weapons) |
| Init2 | DATA\AR_STATS.DAT, DATA\STATDISP.DAT | `CStats::Init` 0x55C0C0 -> 0x5599B0 (call 0x55C131), 0x559860 (call 0x55C136) |
| Init2 | data\shopping.dat (`prices`) | `CShopping::Init` 0x49C290 -> `LoadStats` 0x49B6A0 |
| runtime (script LOAD_SHOP) | data\shopping.dat (`shops`, then `prices/<type>`) | `CShopping::LoadShop` 0x49BBE0 -> `LoadPrices` 0x49B8D0 |

Every loader above opens with `CFileMgr::OpenFile` and immediately calls `LoadLine`/`ReadLine` on the result
without a NULL test (0x5BE68A..0x5BE699, 0x608B4A.., 0x5BB8A6.., 0x5BD006.., 0x5BC0AC.., 0x5BC934..,
0x55D0FB.., 0x55EBA4.., 0x55F2C6.., 0x49B6B8.., 0x55988F.., 0x5599D3.., 0x5BBA2C..). A missing file is
therefore a **crash** in fgets at startup — rule **WPN-01 / PDD-01** (one id per file, see the tables).
The only NULL-checked open is the decision-maker file 0x6076E4 (`jg`): a missing `.ped`/`.grp` returns false.

---------------------------------------------------------------------------------------------------

## 1. weapon.dat — `CWeaponInfo::LoadWeaponData` 0x5BE670

### 1.1 Tables (verified by address arithmetic in `CWeaponInfo::Initialise` 0x5BF750)

* `aWeaponInfo` 0xC8AAB8, stride 0x70, cleared up to 0xC8CDBC -> **80 entries**. Defaults per entry:
  fireType 0 (MELEE), ranges 0, modelId1/2 = -1, slot -1(0xffffffff at +0x14), flags 0, animGroup 0,
  ammo/damage 0, skill 1, accuracy/moveSpeed 1.0, all loop times 0, aimOffsetIndex 0, baseCombo 4, numCombos 1.
* Entry layout (stores in LoadWeaponData): +0 fireType, +4 targetRange, +8 weaponRange, +0xC modelId1,
  +0x10 modelId2, +0x14 slot, +0x18 flags, +0x1C animGroup, +0x20 ammoClip (int16), +0x22 damage (int16),
  +0x24..0x2C fireOffset xyz, +0x30 skillLevel, +0x34 reqStatLevel, +0x38 accuracy, +0x3C moveSpeed,
  +0x40/44/48 animLoop start/end/fire, +0x4C/50/54 animLoop2, +0x58 breakoutTime, +0x5C speed, +0x60 radius,
  +0x64 lifespan, +0x68 spread, +0x6C aimOffsetIndex (int16), +0x6E baseCombo (u8), +0x6F numCombos (u8).
* Aim offsets `aGunAimingOffsets` 0xC8A8A8, stride 0x18, cleared up to 0xC8AAA4 -> **21 entries**
  (fields: aimX, aimZ, duckX, duckZ (floats), rloadA, rloadB, crouchRloadA, crouchRloadB (int16)).
* Weapon-type names (`FindWeaponType` 0x743D10, table 0x8D6150, 49 pointers, compared with `_stricmp`
  0x8229B6 -> **case-insensitive**, unknown -> **0 = UNARMED**):
  0 UNARMED, 1 BRASSKNUCKLE, 2 GOLFCLUB, 3 NIGHTSTICK, 4 KNIFE, 5 BASEBALLBAT, 6 SHOVEL, 7 POOLCUE, 8 KATANA,
  9 CHAINSAW, 10 DILDO1, 11 DILDO2, 12 VIBE1, 13 VIBE2, 14 FLOWERS, 15 CANE, 16 GRENADE, 17 TEARGAS,
  18 MOLOTOV, 19 ROCKET, 20 ROCKET_HS, 21 FREEFALL_BOMB, 22 PISTOL, 23 PISTOL_SILENCED, 24 DESERT_EAGLE,
  25 SHOTGUN, 26 SAWNOFF, 27 SPAS12, 28 MICRO_UZI, 29 MP5, 30 AK47, 31 M4, 32 TEC9, 33 COUNTRYRIFLE,
  34 SNIPERRIFLE, 35 RLAUNCHER, 36 RLAUNCHER_HS, 37 FTHROWER, 38 MINIGUN, 39 SATCHEL_CHARGE, 40 DETONATOR,
  41 SPRAYCAN, 42 EXTINGUISHER, 43 CAMERA, 44 NIGHTVISION, 45 INFRARED, 46 PARACHUTE, 47 "" (empty string),
  48 ARMOUR.
* Fire types (`FindWeaponFireType` 0x5BCF30, full-string memcmp incl. NUL -> **case-sensitive**):
  MELEE 0, INSTANT_HIT 1, PROJECTILE 2, AREA_EFFECT 3, CAMERA 4, USE 5; **anything else -> 1 INSTANT_HIT**
  (last compare returns `bVar4*4+1`).
* Skill-row index (0x5BE968..0x5BE9B0 and `GetWeaponInfo` 0x743C60): for types 22..32 (PISTOL..TEC9)
  row = type+25 (skill 0 POOR), type (1 STD), type+36 (2 PRO), type+47 (3 COP); max 32+47 = 79 -> fits 80.
  For every other type the skill field is **overwritten with 1** and row = type.
  `GetWeaponInfo(type, skill)` applies the same +25/+0/+36/+47 without a type range check.
* Anim-group lookup (inline strcmp loop 0x5BE7D9.., **case-sensitive, exact**) over
  `CAnimManager::GetAnimGroupName(i)` for i < `ms_numAnimAssocDefinitions` (0xB4EA28). The group table
  `ms_aAnimAssocDefinitions` 0x8AA5A8 (stride 0x30, name at +0, 16 bytes) holds **118 built-in groups**
  (indices 0..117, `CAnimManager::Initialise` 0x5BF6B0 sets `ms_numAnimAssocDefinitions = 0x76`) followed by
  the animgrp.dat groups (118..144). Built-in names (dumped from 0x8AA5A8): 0 default, 1 door, 2 bikes,
  3 bikev, 4 bikeh, 5 biked, 6 wayfarer, 7 bmx, 8 mtb, 9 choppa, 10 quad, **11 python, 12 pythonbad,
  13 colt45, 14 colt_cop, 15 colt45pro, 16 sawnoff, 17 sawnoffpro, 18 silenced, 19 shotgun, 20 shotgunbad,
  21 buddy, 22 buddybad, 23 uzi, 24 uzibad, 25 rifle, 26 riflebad, 27 sniper, 28 grenade, 29 flame,
  30 rocket, 31 spraycan**, 32 goggles, 33 melee_1, 34 melee_2, 35 melee_3, 36 melee_4, 37 bbbat_1,
  38 gclub_1, 39 knife_1, 40 sword_1, 41 dildo_1, 42 flowers_1, 43 csaw_1, 44 kick_std, 45 pistlwhp,
  46 medic, 47 beach, 48 sunbathe, 49 playidles, 50 riot, 51 strip, 52 gangs, 53 attractors, 54 player,
  55 fat, 56 muscular, 57 playerrocket, 58 playerrocketf, 59 playerrocketm, 60 player2armed,
  61 player2armedf, 62 player2armedm, 63 playerBBBat, 64 playerBBBatf, 65 playerBBBatm, 66 playercsaw,
  67 playercsawf, 68 playercsawm, 69 playersneak, 70 playerjetpack, 71 swim, 72 drivebys, 73 bike_dbz,
  74 cop_dbz, 75 quad_dbz, 76 fat_tired, 77 handsignal, 78 handsignalL, 79 lhand, 80 rhand, 81 carry,
  82 carry05, 83 carry105, 84 int_house, 85 int_office, 86 int_shop, 87 stealth_kn, 88 stdcaramims,
  89 lowcaramims, 90 trkcaranims, 91 stdbikeanims, 92 sportbikeanims, 93 vespabikeanims, 94 harleybikeanims,
  95 dirtbikeanims, 96 wayfbikeanims, 97 bmxbikeanims, 98 mtbbikeanims, 99 choppabikeanims,
  100 quadbikeanims, 101 vancaranims, 102 rustplaneanims, 103 coachcaranims, 104 buscaranims,
  105 dozercaranims, 106 kartcaranims, 107 convcaranims, 108 mtrkcaranims, 109 traincarranims,
  110 stdtallcaramims, 111 hovercaranims, 112 tankcaranims, 113 bfinjcaramims, 114 learplaneanims,
  115 harrplaneanims, 116 stdcarupright, 117 nvadaplaneanims.
* Melee base-combo lookup 0x61DB30 (memcmp incl. NUL, case-sensitive): UNARMED 4, BBALLBAT 8, KNIFE 9,
  GOLFCLUB 10, SWORD 11, CHAINSAW 12, DILDO 13, FLOWERS 14; **unknown -> 4 (UNARMED)**.

### 1.2 Grammar (after LoadLine: commas = whitespace; first byte decides)

| first byte | sscanf format (0x5BE6EE / 0x5BE860 / 0x5BECBD) | fields |
|---|---|---|
| `#` or empty | skipped | |
| `$` | `%s %s %s %f %f %d %d %d %s %d %d %f %f %f %d %d %f %f %d %d %d %d %d %d %d %x %f %f %f %f` (30 pointers pushed, verified in the .asm) | id, name, fireType, targetRange, weaponRange, modelId1, modelId2, slot, animGroup, ammoClip, damage, fireOffX/Y/Z, skillLevel, reqStatLevel, accuracy, moveSpeed, loopStart, loopEnd, loopFire, loop2Start, loop2End, loop2Fire, breakoutTime, flags(hex), speed, radius, lifespan, spread |
| byte 0xA3 (`£` in cp1252 — the file is 8-bit, the checker must compare the raw byte) | `%s %s %s %f %f %d %d %d %s %d %x %s` (12) | id, name, fireType, targetRange, weaponRange, modelId1, modelId2, slot, baseCombo, numCombos, flags(hex), stealthAnimGroup |
| `%` | `%s %s %f %f %f %f %d %d %d %d` (10) | id, animGroup, aimX, aimZ, duckX, duckZ, rloadA, rloadB, crouchRloadA, crouchRloadB |
| anything else | `%s` into name buffer; `strncmp(name,"ENDWEAPONDATA",13)==0` -> stop reading; otherwise the line is **ignored** | |

* The 4 trailing floats of a `$` line are pre-zeroed (0x5BE92F..0x5BE950) -> **optional** (vanilla: 43 lines
  with 26 fields, 10 with 30). Every other field is an uninitialised stack slot: a short line leaves
  **stack garbage** in that field (WPN-02).
* Loop times: frames/30 (`* 0x858F10 = 0.0333`), then `loopEnd = floor((end-start)*50 + 0.1)*0.02 - 0.006 +
  start` (0x5BEBDE..0x5BEC2F), i.e. quantised to 1/50 s. Nothing to check.
* After the `$` or `£` row is stored: `if (modelId1 > 0) CModelInfo::ms_modelInfoPtrs[modelId1]->+0x24 =
  weaponType` (0x5BEC3E..; reached for every `£` line and for every `$` line whose (effective) skill is 1 and
  type != 40 DETONATOR, 0x5BEC33). The pointer is **not NULL-checked** and +0x24 is
  `CWeaponModelInfo::m_weaponInfo` (plugin-sdk, size 0x28; CBaseModelInfo/CAtomicModelInfo are 0x20,
  CClumpModelInfo 0x24).
* `%` lines: the group index is searched over all definitions; `index - 11` selects the aim-offset entry
  (0x5BED14 `add eax,-0xb`). Not found -> index = `ms_numAnimAssocDefinitions` (loop exit) -> entry
  `N-11` (>= 107). No bounds check.
* `$` lines: `aimOffsetIndex = animGroup - 11` only when `10 < animGroup < 32` (0x5BEB8C..); otherwise the
  entry keeps 0 (= python offsets).

### 1.3 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **WPN-01** | `data\weapon.dat` missing | crash | `OpenFile` -> `LoadLine(NULL)` at 0x5BE699 -> fgets(NULL) | file exists (modloader-aware) |
| **WPN-02** | `$` line with < 26 fields, `£` with < 12, `%` with < 10; or a numeric field that is not a number (sscanf stops there) | garbage | remaining fields keep stack garbage; e.g. slot/skill/modelId garbage -> feeds WPN-06/07/09 | tokenise after LoadLine (commas -> spaces); count; check `%d/%f/%x` fields parse (`%x` accepts hex digits; `%d` decimal) |
| **WPN-03** | any `%s` token >= 32 chars (name, fireType, animGroup, baseCombo, stealth group) or the identifier token >= 8 chars (`$`/`£`/`%` token buffer is 8 bytes) | crash | `sscanf %s` without width into 32-byte stack buffers (`local_68/48/88/28`, 0x5BE6C3..) -> return address smashed | token length |
| **WPN-04** | weapon name not one of the 49 names of 1.1 (case-insensitive; name 47 is the empty string and can never match) | garbage | `FindWeaponType` -> 0 -> the UNARMED entry is overwritten with the row (a `$` row also forces skill=1 for type 0) | lookup |
| **WPN-05** | fireType not exactly one of MELEE / INSTANT_HIT / PROJECTILE / AREA_EFFECT / CAMERA / USE | garbage (silent) | 0x5BCF30 returns INSTANT_HIT for anything else — a misspelt PROJECTILE fires bullets | exact compare |
| **WPN-06** | `$` line for types 22..32 with skillLevel not in 0..3 | garbage | 0x5BE968..0x5BE9B0: `iVar5 = local_110` = the row index of the **previous** line (uninitialised for the first line) -> the previous weapon's row is overwritten | int in 0..3 |
| **WPN-07** | modelId1 (or modelId2) != -1 is not a model id defined in any IDE | crash | load-time: `ms_modelInfoPtrs[modelId1]` NULL -> write to 0x24 (0x5BEC49) for `£` rows and `$` rows with skill 1 (non-skill types always) except DETONATOR; runtime: `CPed::AddWeaponModel` -> `CStreaming::RequestModel` 0x4087E0 derefs `ms_modelInfoPtrs[id]+0xA` at 0x40890A with no NULL test for both ids | model id defined (any IDE section); modelId must be in 0..19999 (id >= 20000 selects TXD/COL/IPL slots in RequestModel) |
| **WPN-08** | modelId1 defined but not by a `weap` IDE line (objs/tobj/anim/cars/peds) | garbage | +0x24 write lands outside a 0x20/0x24-byte model info: for `objs` (CAtomicModelInfo pool, 0x20 stride) it overwrites the **next pool item's +4 = name hash** -> that model can no longer be found by name; vehicles get a field clobbered | section of the defining IDE line == weap |
| **WPN-09** | slot not in 0..12 | garbage (memory corruption inside CPed) | `CPed::GiveWeapon` 0x5E6080: `slot = info->+0x14; weapon = ped + 0x5A0 + slot*0x1C` (0x5E6093..0x5E60A4), no bounds check; `m_aWeapons[13]` (plugin-sdk CPed.h) | int in 0..12 |
| **WPN-10** | `$` animGroup (or `£` stealthAnimGroup) is neither `null` nor an exact (case-sensitive) name from the 118 built-ins + animgrp.dat groups | garbage | group stays 0 = `default` (ped walk-cycle group): no fire/reload anims, aimOffsetIndex 0 | exact lookup over built-ins (list in 1.1) + animgrp.dat headers |
| **WPN-11** | `%` line whose group is not one of the 21 gun groups python..spraycan (indices 11..31) | garbage (can crash) | index-11 is outside `aGunAimingOffsets[21]`: names 0..10 -> negative -> overwrites `gCrossHair` 0xC8A838.. (crosshair texture pointers -> crash when drawn); goggles/melee/unknown (>= 32) -> writes into `aWeaponInfo` entries (`0xC8A8A8 + (i-11)*0x18`, e.g. i=118..144 hits entries 11..15 DILDO2..CANE) | group ∈ {python, pythonbad, colt45, colt_cop, colt45pro, sawnoff, sawnoffpro, silenced, shotgun, shotgunbad, buddy, buddybad, uzi, uzibad, rifle, riflebad, sniper, grenade, flame, rocket, spraycan} |
| **WPN-12** | `£` baseCombo not one of UNARMED / BBALLBAT / KNIFE / GOLFCLUB / SWORD / CHAINSAW / DILDO / FLOWERS (exact) | ignored | 0x61DB30 returns 4 (UNARMED combo) | exact lookup |
| **WPN-13** | `$` animGroup valid but not in 11..31 (e.g. goggles, a melee or animgrp.dat group) | info | no aim offsets: `aimOffsetIndex` stays 0 (python). Vanilla does this for goggles (NIGHTVISION/INFRARED) | index check |
| **WPN-14** | models **334, 345, 346** not defined in any IDE (vanilla: nitestick, missile, colt45 in default.ide `weap`) | crash | `CStreaming::LoadInitialWeapons` 0x40A120 requests exactly these three ids (`RequestModel(0x15A,2); (0x14E,2); (0x159,2)`); `RequestModel` 0x4087E0 derefs `ms_modelInfoPtrs[id]` (0x408903..0x40890A) without NULL test | ids defined |
| **WPN-15** | weapon types 22 PISTOL, 24 DESERT_EAGLE, 28 MICRO_UZI, 30 AK47 have no `$` row with skill 1 | ignored | `CGangs::Initialise` 0x5DE680 hard-codes these type ids as gang weapons (0x16/0x18/0x1C/0x1E); a missing row leaves the Initialise defaults (fireType MELEE, model -1): gang members "shoot" with an invisible melee weapon | rows present |
| **WPN-16** | for types 22..32 a skill row 0 or 2 is missing (row 3 COP is optional — vanilla lacks it for PISTOL_SILENCED, DESERT_EAGLE, SHOTGUN, SAWNOFF, SPAS12, MICRO_UZI, TEC9, MP5, AK47, M4) | info | `GetWeaponInfo(type, skill)` 0x743C60 returns the default entry (fireType 0, models -1) for that skill: peds/player with that skill level hold nothing | rows present per type |
| **WPN-17** | ammoClip or damage outside int16 (-32768..32767), numCombos outside 0..255 | garbage | stored as int16 (0xC8AAD8/0xC8AADA) / u8 (0xC8AB27): truncated | range |
| **WPN-18** | flags field not parseable as hex; any of the `%d` int fields negative where a count is expected (ammoClip, loop frames) | garbage / info | `%x` stops at the first non-hex char -> flags 0 or partial | regex `[0-9A-Fa-f]+` |
| **WPN-19** | duplicate `$` rows for the same type+skill or `£` rows for the same type; `£` row naming a gun type or `$` row naming a melee type (0..15, 46) | ignored (info) | last row wins; a `$` row for a melee type forces skill 1 and stores gun fields, its combo bytes keep defaults | bookkeeping |
| **WPN-20** | `%` lines before the `$` lines of the same group, or lines after `ENDWEAPONDATA`, or a line with an unknown first byte | info | `%` order does not matter (indexed by group); `ENDWEAPONDATA` stops the loop; other prefixes are silently ignored | – |
| **WPN-21** | weaponRange / targetRange <= 0 for INSTANT_HIT/PROJECTILE/AREA_EFFECT; accuracy or moveSpeed <= 0 | ignored | not checked by the loader; runtime divides/uses as multipliers (design comment in weapon.dat: accuracy 0.5..2.0, move speed 0.5..1.5) | range warning only |

Vanilla spot-check (script over `data\weapon.dat`, `default.ide`, built-in group table): 43+10 `$`, 17 `£`,
21 `%`, all names/fire types/groups/combos valid, all model ids in `weap`, slots 0..12, skills 0..3,
tokens <= 31 -> **0 findings**.

---------------------------------------------------------------------------------------------------

## 2. ped.dat — `CPedType::LoadPedData` 0x608B30 (called from `CPedType::Initialise` 0x608E40)

### 2.1 Facts

* `Initialise` allocates `new(0x284)`: 4-byte count (0x20) + **32 × CAcquaintance (0x14 bytes)** =
  exactly 0x284, pointer to the array in `DAT_00C0BBE8`. Each entry: +0 respect, +4 like, +0xC dislike,
  +0x10 hate bitmasks (bit = ped type index).
* Ped-type names (`FindPedType` 0x608790, table 0x8D23B8, 32 entries, inline **case-sensitive** strcmp):
  0 PLAYER1, 1 PLAYER2, 2 PLAYER_NETWORK, 3 PLAYER_UNUSED, 4 CIVMALE, 5 CIVFEMALE, 6 COP, 7..16 GANG1..GANG10,
  17 DEALER, 18 MEDIC, 19 FIREMAN, 20 CRIMINAL, 21 BUM, 22 PROSTITUTE, 23 SPECIAL, 24..31 MISSION1..MISSION8.
  Not found -> the tail re-tests PLAYER_NETWORK (2) / PLAYER_UNUSED (3) and otherwise returns **32**.
* Line grammar (after LoadLine, `#` = comment): `sscanf("%s", local_20[32])` takes the first token.
  If it equals (memcmp incl. NUL, case-sensitive) `Hate` / `Dislike` / `Like` / `Respect`: `strtok(line, " ,\t")`
  (0x86A8C8) skips the keyword, then every further token -> `FindPedType`; index < 32 -> bit OR-ed, index 32
  -> bit 0 (`1 << 32` branch yields 0) = **silently ignored**; the mask is stored at
  `array + iVar3*0x14 + {0x10,0xC,4,0}`. Any other first token -> `iVar3 = FindPedType(token)` = current
  type for the following acquaintance lines. **`iVar3` starts at 0x20 (32)** (0x608B3C).
* Ped-type index of a peds.ide line (`LoadPedObject`) uses the same FindPedType.

### 2.2 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-01** | `data\ped.dat` missing | crash | `LoadLine(NULL)` 0x608B4A | exists |
| **PDD-02** | a Hate/Dislike/Like/Respect line before the first type-name line, or after a type-name line whose name is not one of the 32 (typo, lowercase, `Hates`, …) | crash (heap corruption) | current index = 32 -> store at `array + 32*0x14 + off` = 4..20 bytes past the 0x284-byte heap block (allocation is exact) -> CRT heap header of the next block corrupted -> crash at a later malloc/free | state machine over the file: index valid before each acquaintance line |
| **PDD-03** | a name inside a Hate/Dislike/Like/Respect list is not one of the 32 names (case-sensitive) | ignored | FindPedType -> 32 -> contributes 0 to the mask | lookup |
| **PDD-04** | any token >= 32 chars (first token of any line) | crash | `sscanf %s` into `local_20[32]` 0x608B5A | token length |
| **PDD-05** | type-name line names PLAYER1/PLAYER2/PLAYER_NETWORK/PLAYER_UNUSED or the same type twice | info | allowed; later lines overwrite the mask (no accumulation across blocks — each keyword line replaces the whole mask) | – |

Vanilla: 18 type blocks, all names in the table, no leading acquaintance line -> 0 findings.

---------------------------------------------------------------------------------------------------

## 3. pedstats.dat — `CPedStats::LoadPedStats` 0x5BB890

### 3.1 Facts

* `CPedStats::Initialise` 0x5BF9D0: `new(0x8BC)` = **43 entries × 0x34** at `DAT_00C0BBEC`; each preset to
  id i, name "", fleeDist 20.0, headingChangeRate 15.0, fear/temper/law/sexiness 50, attack 1.0, defend 1.0,
  flags 0, defaultDecisionMaker 0 (0x5BFA06..0x5BFA72).
* Each non-`#`, non-empty line: `sscanf("%s %f %f %d %d %d %d %f %f %d %d", name[32], fleeDist,
  headingChangeRate, fear, temper, lawfulness, sexiness, attackStrength, defendWeakness, shootingRate,
  defaultDecisionMaker)`; stored at entry `n` (n = running count, +0 = n, +4 strcpy name (24 bytes to +0x1C),
  +0x1C..+0x20 floats, +0x24..+0x27 u8 ×4, +0x28/+0x2C floats, +0x30 int16, +0x32 u8). **No check of n < 43**
  (0x5BB8C6..0x5BB959: `iVar4 += 0x34` unconditionally).
* Lookup by name (`GetPedStatType` 0x6088D0, inline case-sensitive strcmp over the 43 slots' names):
  not found -> **16** (vanilla index 16 = STAT_SENSIBLE_GUY). Engine code also addresses stats by index
  (STAT_PLAYER 0, STAT_COP 1, …), hence the file comment "MUST be in correct order".
* defaultDecisionMaker (`CPed::SetPedDefaultDecisionMaker` 0x5E06E0 -> `SetPedDecisionMakerType(stats->+0x32)`)
  is an index into `CDecisionMakerTypes::m_DecisionMakers[20]` (0x99C stride); the game registers 8 ped
  DMs in order GangMbr 0, Cop 1, R_Norm 2, R_Tough 3, R_Weak 4, Fireman 5, m_empty 6, Indoors 7
  (`LoadDefaultDecisionMaker` 0x5BF400), 8/9 are group DMs, 10..19 mission DMs.

### 3.2 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-06** | `data\pedstats.dat` missing | crash | `LoadLine(NULL)` 0x5BB8A6 | exists |
| **PDD-07** | more than **43** data lines | crash (heap corruption) | 44th line writes 0x34 bytes past the 0x8BC block (0x5BB8E4..) | count non-comment lines |
| **PDD-08** | name token >= 32 chars | crash | `sscanf %s` into `local_20[32]` | length |
| **PDD-09** | name 24..31 chars | garbage | strcpy into the 24-byte field overruns into fleeDist (then overwritten) -> name not NUL-terminated -> `GetPedStatType` never matches it | length <= 23 |
| **PDD-10** | fewer than 43 lines, or line order / names differ from the vanilla list (STAT_PLAYER, STAT_COP, STAT_MEDIC, STAT_FIREMAN, STAT_GANG1..10, STAT_STREET_GUY, STAT_SUIT_GUY, STAT_SENSIBLE_GUY, STAT_GEEK_GUY, STAT_OLD_GUY, STAT_TOUGH_GUY, STAT_STREET_GIRL, STAT_SUIT_GIRL, STAT_SENSIBLE_GIRL, STAT_GEEK_GIRL, STAT_OLD_GIRL, STAT_TOUGH_GIRL, STAT_TRAMP_MALE, STAT_TRAMP_FEMALE, STAT_TOURIST, STAT_PROSTITUTE, STAT_CRIMINAL, STAT_BUSKER, STAT_TAXIDRIVER, STAT_PSYCHO, STAT_STEWARD, STAT_SPORTSFAN, STAT_SHOPPER, STAT_OLDSHOPPER, STAT_BEACH_GUY, STAT_BEACH_GIRL, STAT_SKATER, STAT_STD_MISSION, STAT_COWARD) | ignored (warning) | engine indexes by position (e.g. player = 0, cop = 1); missing rows keep Initialise defaults | compare with list |
| **PDD-11** | fear/temper/lawfulness/sexiness outside 0..255 (design 0..100); shootingRate outside int16; defaultDecisionMaker outside 0..7 | garbage | u8/int16 truncation; DM index selects another DM slot (8..19 = group/mission DMs, >= 20 = past `m_DecisionMakers`) | ranges |
| **PDD-12** | fewer than 11 fields | garbage | uninitialised stack in the missing fields | count |

Vanilla: 43 lines, vanilla order, names <= 18 chars, DM 1..5 -> 0 findings.

---------------------------------------------------------------------------------------------------

## 4. pedgrp.dat — `CPopulation::LoadPedGroups` 0x5BCFE0

### 4.1 Facts (own reader, not LoadLine)

* `ChangeDir("\DATA\")`, `OpenFile("PEDGRP.DAT")`, then `CFileMgr::ReadLine(file, buf[1024], 0x400)`
  (raw fgets). Per line: scan **until byte 0x0A** replacing `,` and `\r` by space, put NUL there
  (0x5BD02A..0x5BD04B — the loop tests only for `\n`, not for NUL). Then up to **21 tokens** (`iVar10 < 0x15`);
  a token starting with `#` ends the line; each token is `strncpy`-ed into `cStack_500[256]` and passed to
  `CModelInfo::GetModelInfo(name, &id)` (hash lookup, case-insensitive, **any model type**). Found -> id stored
  at `m_PedGroups[group][n++]` (0xC0F358, int16, 21 per group); not found -> **silently skipped**. Remaining
  slots filled with 2000 (0x7D0). `m_nNumPedsInGroup[group]` (0xC0ECC0, int16) = n. A line with at least one
  token (even if none resolved) advances the group counter; blank/comment-only lines do not.
* Tables (address arithmetic): `m_nNumPedsInGroup` 0xC0ECC0..0xC0ED32 (57 int16, followed by
  `m_CarGroups` at 0xC0ED38), `m_PedGroups` 0xC0F358..0xC0FCB2 (57 × 21 × 2 = 0x95A, followed by
  `m_bDontCreateRandomGangMembers` 0xC0FCB2, `m_bOnlyCreateRandomGangMembers`, `m_bDontCreateRandomCops`,
  `m_bMoreCarsAndFewerPeds`, `bInPoliceStation`, `NumMiamiViceCops` 0xC0FCB8, `CurrentWorldZone` 0xC0FCBC).
  -> **exactly 57 groups**, in the fixed order of `ePopcyclePedGroup`: 0..2 WORKERS LA/SF/VG, 3..5 BUSINESS,
  6..8 CLUBBERS, 9 FARMERS, 10 BEACHFOLK, 11..13 PARKFOLK, 14..16 CASUAL_RICH, 17..19 CASUAL_AVERAGE,
  20..22 CASUAL_POOR, 23..25 PROSTITUTES, 26..28 CRIMINALS, 29 GOLFERS, 30..32 SERVANTS, 33..35 AIRCREW,
  36..38 ENTERTAINERS, 39 OOT_FACTORY, 40 DESERT_FOLK, 41 AIRCREW_RUNWAY, 42 BALLAS, 43 GROVE, 44 VAGOS,
  45 SF_RIFA, 46 DA_NANG, 47 MAFIA, 48 TRIADS, 49 VLA, 50/51 unused, 52 DEALERS, 53 SHOPKEEPERS,
  54 OFFICE_WORKERS, 55 HUSBANDS, 56 WIVES (vanilla file matches: line 43 = BALLAS1.., 50 = VLA1..,
  53 = BMYDRUG..). **No upper bound on the group counter** (`iStack_510 += 0x15` unconditionally).
* Gang members: `CGangs::Initialise` 0x5DE680 stores only weapon ids; gang models come from groups 42..49.

### 4.2 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-13** | `data\pedgrp.dat` missing | crash | `ReadLine(NULL)` 0x5BD01B | exists |
| **PDD-14** | more than 57 groups (non-comment lines with >= 1 token) | garbage -> crash | group 57 writes `m_PedGroups[57]` over `m_bDontCreateRandomGangMembers`..`CurrentWorldZone` (0xC0FCB2..) and beyond; `m_nNumPedsInGroup[60]` lands on `m_CarGroups[0]` 0xC0ED38 | count |
| **PDD-15** | fewer than 57 groups | ignored (warning) | later groups stay empty (count 0) — the popcycle group maps to no models | count |
| **PDD-16** | a token is not the name of any model (case-insensitive over all IDEs) | ignored (warning) | `GetModelInfo` NULL -> skipped; a group can end up with 0 models | lookup |
| **PDD-17** | a token names a model that is **not** a `peds` IDE entry (vehicle/object/weapon) | crash | id stored; `CPopulation::AddPed` treats `ms_modelInfoPtrs[id]` as `CPedModelInfo` (m_pHitColModel, m_nPedType…) -> wrong casts on spawn | lookup in peds section |
| **PDD-18** | more than 21 tokens before `#`/end of line | ignored | tokens 22+ are not read (`iVar10 < 0x15`) | count |
| **PDD-19** | a token >= 256 chars, or a line >= 1024 chars | crash / garbage | `strncpy` into `cStack_500[256]` with the token length (no cap); fgets splits a >1023-char line into two records | lengths |
| **PDD-20** | the **last line has no trailing `\n`** (and is the longest line so far / first line) | crash risk | the `while (buf[i] != '\n')` scan (0x5BD02A) runs past the NUL into stale/uninitialised stack until it meets a 0x0A byte, rewriting `,`/`\r` bytes and planting a NUL there (buffer is 1024 bytes above the return address) | file ends with 0x0A (vanilla ends with CRLF) |
| **PDD-21** | a group in 42..49 (gangs) with 0 resolved models | garbage | gang spawn picks `m_PedGroups[g][rand % 0]` | per-group count > 0 |

Vanilla: 57 groups, max 21 tokens, every token is a `peds` model, trailing CRLF -> 0 findings.

---------------------------------------------------------------------------------------------------

## 5. popcycle.dat — `CPopCycle::Initialise` 0x5BC090

### 5.1 Facts

* `SetDir(...)`, `OpenFile("POPCYCLE.DAT")`; three nested loops: **20 zone types** (`iStack_6c += 0x12`
  while `< 0x168`) × **2** (weekday, weekend) × **12** time slots (2-hour steps) = **480 data lines**, in
  that order (zone-major, then weekday block of 12 then weekend block of 12).
* For each slot: `LoadLine` until a line that does not start with `/` and is not empty; **no NULL test**:
  `sscanf(_Src, "%d"×24, …)` with `_Src == NULL` when the file ends early -> `strlen(NULL)` inside the CRT
  sscanf -> **crash** (0x5BC1E4).
* 24 ints per line: maxPeds, maxCars, percDealers, percGang, percCops, percOther, then **18** group
  percentages (Workers, Business, Clubbers, Farmers, BeachFolk, Parkfolk, Casual_Rich, Casual_Average,
  Casual_Poor, Prozzies, Criminals, Golfers, Servants, Aircrew, Entertainers, oot_fact, Desertfolk,
  Aircrew_runway). Everything after the 24th number is ignored (vanilla has `// 10pm` comments there).
  All 24 are stored as **bytes** (0x5BC1E9..0x5BC30B: `mov byte ptr`).
* The 18 group bytes are normalised: `sum = Σ`; `perc[i] = ftol(perc[i] * (100/sum))` (0x5BC320..0x5BC3C2,
  constant 0x858628 = 100.0f); then the largest gets `100 - Σ` added (0x5BC3D1..). `sum == 0` -> x87 divide
  by zero (masked -> INF), `0*INF = NaN`, `ftol(NaN)` = 0x80000000 -> byte 0 for all -> the last group
  (index 17, Aircrew_runway) receives +100.
* Tables `m_nMaxNumPeds` 0xC0E798 etc. are `uint8[12][2][20]` (stride 0x28 per time slot = 2×20); the group
  percentages `m_nPercTypeGroup` 0xC0BC78 `[12][2][20][18]`. Indices are bounded by the loop, not by the file.

### 5.2 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-22** | `data\popcycle.dat` missing | crash | `LoadLine(NULL)` at 0x5BC0C4 | exists |
| **PDD-23** | fewer than **480** data lines (lines not starting with `/` and not empty) | crash | `sscanf(NULL, …)` at 0x5BC1E4 | count |
| **PDD-24** | more than 480 data lines | ignored | extra lines never read | count (info) |
| **PDD-25** | a data line with fewer than 24 numeric fields, or a non-numeric token among the first 24 | garbage | remaining fields keep stack values from the previous line/uninitialised | tokenise, check 24 leading ints |
| **PDD-26** | any of the 24 values outside 0..255 | garbage | stored as u8 (truncated); percOther/percGang/... are used as 0..100 percentages | range |
| **PDD-27** | the 18 group percentages all 0 | garbage | NaN normalisation -> 100 % Aircrew_runway for that slot (vanilla has none; it relies on non-zero sums that don't add to 100 — 41 vanilla rows don't sum to 100 and are simply rescaled) | sum > 0 |
| **PDD-28** | first data line does not belong to the "BUSINESS / Weekday / Midnight" slot (file reordered) | info | zone-type order is positional: 0..19 in the order of the vanilla comments (BUSINESS, DESERT, ENTERTAINMENT, COUNTRYBUSINESS, RESIDENTIAL RICH, RESIDENTIAL AVERAGE, RESIDENTIAL POOR, GANGLAND, BEACH, SHOPPING, PARK, INDUSTRY, ENTERTAINMENT BUSY, AIRPORT, GOLF CLUB, OUT OF TOWN FACTORY, OUT OF TOWN, AIRPORT RUNWAY, TRIADS, SLUMS-like) | cannot be verified offline beyond the count |

Vanilla: 480 rows × 26 tokens (24 ints + `//` comment), max value 180, no all-zero group rows -> 0 findings.

---------------------------------------------------------------------------------------------------

## 6. animgrp.dat grammar — `CAnimManager::ReadAnimAssociationDefinitions` 0x5BC910

(Limits 145 definitions / 27 file groups, 2500 anims etc. are in ifp_path.md §3.11–3.12; this section adds
the grammar and the buffer sizes.)

* `SetDir("")`, `OpenFile("DATA\ANIMGRP.DAT")`; `LoadLine`; `#` and empty lines skipped.
* State machine: **outside a group** the line is parsed with `sscanf("%s %s %s %d", name[32], block[32],
  type[32], &count)` and `AddAnimAssocDefinition(name, block, 7, count, 0x8A7788)` is called (the third
  token — `walkcycle` — is **ignored**; 7 is the constant model index). **Inside a group** each line is
  `sscanf("%s")` -> if the token equals `end` (memcmp 4 bytes incl. NUL, case-sensitive) the group closes,
  otherwise `AddAnimToAssocDefinition(def, token)` (only the first token of the line is used).
* `AddAnimAssocDefinition` 0x4D3BA0: finds the next free slot from `ms_numAnimAssocDefinitions` (skips
  non-empty names), **strcpy** group name into 16 bytes at +0, block name into 16 bytes at +0x10,
  `new(count*4)` name-pointer array, `new(count*0x18)` name storage (24 bytes each), increments
  `ms_numAnimAssocDefinitions`, NUL-terminates the next slot only if `< 0x91` (145).
* `AddAnimToAssocDefinition` 0x4D3C80: scans the pointer array for the first empty name (no bound) and
  strcpy-s the token there.

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-29** | `data\animgrp.dat` missing | crash | `LoadLine(NULL)` 0x5BC947 | exists |
| **PDD-30** | header line with fewer than 4 tokens or a non-integer 4th token | crash / garbage | `uStack_68` (count) uninitialised -> `new(count*4)` with stack garbage (huge -> bad_alloc/NULL -> writes through NULL in the init loop 0x4D3C21) | 4 tokens, int |
| **PDD-31** | group name or block name >= 16 chars | garbage / crash | strcpy into 16-byte fields: the group name overruns the block name; the block name overruns modelIndex/count/pointers of the same slot -> anim names written through a corrupted pointer | length <= 15 |
| **PDD-32** | any token >= 32 chars | crash | `sscanf %s` into 32-byte stack buffers | length |
| **PDD-33** | more anim lines in a group than the declared count | crash | write past the `char*[count]` heap array (ifp_path 3.12) | count lines until `end` |
| **PDD-34** | fewer anim lines than declared | ignored | remaining names empty; positional AnimId lookups shift | count |
| **PDD-35** | anim name >= 24 chars | garbage | 24-byte slots (ifp_path 3.12) | length <= 23 |
| **PDD-36** | declared count 0 | crash | `new(0)` then any anim line writes through the 0-byte array | count >= 1 |
| **PDD-37** | missing `end` before EOF / before the next header | garbage | the next header line is consumed as an anim name of the open group | state machine |
| **PDD-38** | more than 27 groups | crash | ifp_path 3.11 (145-slot table) | count |
| **PDD-39** | block name is not the stem of an `.ifp` in any IMG, or an anim name is not present in that IFP | crash | ifp_path 3.10 / 3.12 (`CreateAssociations` NULL hierarchy) — existing DAT-12/IFP checks | cross-ref |

Vanilla: 21 groups, names <= 11 chars, counts match, all `end` present -> 0 findings.

---------------------------------------------------------------------------------------------------

## 7. peds.ide cross-references — `CFileLoader::LoadPedObject` 0x5B7420

(Field count / hex / id / animFile length rules exist as DAT-09/DAT-11; animGroup-not-found is DAT-12.
Below are the remaining lookups. Stack offsets verified from the 14 pushes at 0x5B742A..0x5B748E.)

`sscanf("%d %s %s %s %s %s %x %x %s %d %d %s %s %s", &id, modelName[F+0x34, 44 B], txd[F+0xA8, 24 B],
pedType[F+0x90, 24 B], statName[F+0x60, 24 B], animGroup[F+0x78, 24 B], &carsCanDrive, &flags,
animFile[F+0x24, 16 B], &radio1, &radio2, voiceType[F+0x4C, 20 B], voice1[F+0xC0, 60 B], voice2[F+0xFC, 44 B])`
then: `+0x28 = CPedType::FindPedType(pedType)`, `+0x2C = CPedStats::GetPedStatType(statName)`,
`+0x24 = anim group index (case-sensitive scan; not found -> ms_numAnimAssocDefinitions)`,
`+0x30 = carsCanDrive (u16)`, `+0x32 = flags (u16)`, `+0x38 = radio1+1 (u8)`, `+0x39 = radio2+1 (u8)`,
`+0x3A = CPopulation::FindPedRaceFromName(modelName)`, `+0x3C = GetAudioPedType(voiceType) (int16)`,
`+0x3E/+0x40 = GetVoice(voice1/voice2, audioType) (int16)`, `+0x42 = +0x3E`.

* `FindPedRaceFromName` 0x5B6D40 looks at the **first two characters** of the model name (upper-cased):
  `B` -> 1 (black), `W` -> 2 (white), `I`/`O` -> 3 (other?), `H` -> 4 (hispanic); none -> 0 (default).
  Names not starting with one of these letters in position 0 or 1 get race 0 (info only).
* `GetAudioPedType` 0x4E3C60: table 0x8C8108 = PED_TYPE_GEN 0, PED_TYPE_EMG 1, PED_TYPE_PLAYER 2,
  PED_TYPE_GANG 3, PED_TYPE_GFD 4, PED_TYPE_SPC 5 (case-sensitive); unknown -> -1.
* `GetVoice` 0x4E3CD0: per audio type a 20-byte-stride name table (case-sensitive): GEN 0x8AE6A8..0x8AF6FB
  (209 names VOICE_GEN_*), EMG 0x8BA0D8..0x8BA46F (46 VOICE_EMG_*), PLY 0x8BBD40..0x8BBECF (20 VOICE_PLY_*),
  GNG 0x8BE1A8..0x8BE5B7 (52 VOICE_GNG_*), GFD 0x8C4120..0x8C4287 (18 VOICE_GFD_*); type 5 (SPC) and unknown
  types always -> -1. Unknown name -> -1 (ped is mute). Dump the tables from the exe (script in scratchpad)
  rather than hard-coding them; the checker can embed the 345 names.

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-40** | pedType not one of the 32 names of §2.1 (case-sensitive) | garbage | `FindPedType` -> 32 -> `m_nPedType = 32`; `CPedType::GetPedFlag(32)` 0x608830 returns 0 (no relationship bits) and every `ms_apPedTypes[32]` lookup reads past the 32-entry array (acquaintance masks of heap garbage) | lookup |
| **PDD-41** | statName not found in pedstats.dat (case-sensitive, first 43 rows) | ignored (warning) | `GetPedStatType` -> 16 (STAT_SENSIBLE_GUY in vanilla) | lookup |
| **PDD-42** | voiceType not one of the 6 PED_TYPE_* names | ignored (warning) | audio type -1 -> both voices -1 -> mute ped | lookup |
| **PDD-43** | voice1/voice2 not in the table of that audio type (types 0..4) | info | mute ped. **Vanilla has 3 such peds** (WMYSGRD `VOICE_GEN_WMYSGRAD`, BMYMIB `VOICE_GEN_BMYMIB`, BMYPIMP `VOICE_GEN_BMYPI`) so this must stay Info | lookup |
| **PDD-44** | radio1/radio2 outside -1..254 | garbage | stored as u8 after +1 | range |
| **PDD-45** | pedType token >= 24, statName >= 24, voiceType >= 20, voice1 >= 60, voice2 >= 44, txd >= 24, modelName >= 44 chars | crash | `sscanf %s` into the fixed stack slots listed above (frame 0x128) | lengths (DAT-09 already covers animFile 16 / animGroup 24) |
| **PDD-46** | a `peds` line whose model name does not start (char 0 or 1) with B/W/H/I/O | info | race 0 -> shop/dialogue race checks treat it as "default" | – |

Vanilla peds.ide (276 lines): every pedType/stat/animGroup/audio type valid; 3 unknown voices (Info) ->
0 Краш/Ошибка.

---------------------------------------------------------------------------------------------------

## 8. Decision makers — `LoadEventIndices` 0x600840 -> 0x5BB9F0, `LoadDecisionMaker` 0x6076B0

### 8.1 Facts

* `CDecisionMakerTypes` instance (`GetInstance` 0x4684F0): +0 count, +4 `m_DecisionMakers[20]` × 0x99C
  (= 41 `CDecision` × 0x3C), +0xC034 `m_EventIndices[96]` (int32), then 5 default `CDecisionMaker`s.
  0xC034 = 4 + 20×0x99C verified.
* `LoadEventIndices`: `SetDir("data\decision\")`, `OpenFile("PedEvent.txt")`, `SetDir("")`, zeroes 96 dwords,
  then for each `ReadLine(…, 0x100)` line that is not empty/`\n`: `sscanf("%s %d", name[256], &ev)`;
  `m_EventIndices[ev] = n++` (0x5BBA85, **no bounds check on ev**, no cap on n).
* `LoadDecisionMaker(file, dm)` 0x6076B0: `SetDir("data\decision\allowed\")`, `OpenFile`, **NULL-checked**
  (returns false), first line skipped, then per `ReadLine(…, 0x200)`: one
  `sscanf("%d, %d, %d, %f, %f, %f, %f, %d, %d, %f, %f, %f, %f, %f, %f, " ×6 …)` = **80 comma-separated
  numbers** (event, unused, then 6 × {task, 4 probs, 2 bools, 6 facial probs}); then
  `decision = dm->m_aDecisions[m_EventIndices[event]]` (0x607A1D, **event not bounds-checked**) and
  `CDecision::Set`.
* Default files loaded at startup (0x5BF400): RANDOM.ped, m_norm.ped, m_plyr.ped, RANDOM.grp, MISSION.grp,
  GangMbr.ped, Cop.ped, R_Norm.ped, R_Tough.ped, R_Weak.ped, Fireman.ped, m_empty.ped, Indoors.ped,
  RANDOM.grp, RANDOM2.grp — all in `data\decision\allowed\`. Files in `data\decision\` itself (other than
  PedEvent.txt) are never opened by the engine.

### 8.2 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-47** | `data\decision\PedEvent.txt` missing | crash | `ReadLine(NULL)` 0x5BBA4A | exists |
| **PDD-48** | PedEvent.txt has more than **41** event lines | crash | index 41+ -> `.ped` loader writes `m_aDecisions[41]` = 0x3C bytes past the `CDecisionMaker` (into the next DM / the count field) | count |
| **PDD-49** | an event number outside 0..95 | crash / garbage | `m_EventIndices[ev]` write at 0x5BBA85: negative -> into `m_DecisionMakers`, >= 96 -> into the default DMs; huge -> AV | range |
| **PDD-50** | a PedEvent.txt line without a number, or a name token >= 256 chars | garbage / crash | `%d` unparsed -> uninitialised `ev`; `%s` into 256-byte buffer | parse |
| **PDD-51** | one of the 15 default `.ped/.grp` files missing from `data\decision\allowed\` | ignored (warning) | `LoadDecisionMaker` returns false; that DM stays empty -> peds of that type never react | exists |
| **PDD-52** | a data line (after the first) in an allowed `.ped/.grp` with != 80 comma-separated numbers, or longer than 511 chars | garbage | sscanf leaves the rest uninitialised (stack) -> random task ids / probabilities; fgets splits long lines | count tokens split on `,` |
| **PDD-53** | event id (first number) outside 0..95 | crash | `m_EventIndices[event]` read out of bounds -> garbage decision index × 0x3C -> `CDecision::Set` writes anywhere | range |
| **PDD-54** | event id not listed in PedEvent.txt | info | index 0 -> the decision of the **first** PedEvent.txt entry (EVENT_DRAGGED_OUT_CAR in vanilla) is overwritten. **Vanilla allowed/*.ped contain 181 such lines** (events 0, 16, 45, 74 …), so Info only | lookup |

Vanilla: 41 events (7..79), all data lines 80 fields, max line 356 chars -> 0 Краш/Ошибка.

---------------------------------------------------------------------------------------------------

## 9. surface.dat / surfinfo.dat / surfaud.dat — `SurfaceInfos_c::Init` 0x55F420

### 9.1 Facts

* Surface ids: `GetSurfaceIdFromName` 0x55D220 — chain of memcmp (incl. NUL, **case-sensitive**) over
  **179 names** (DEFAULT 0 … RAILTRACK 178, order = vanilla surfinfo.dat order); unknown -> **0 (DEFAULT)**
  (tail 0x55E5B0: `dec eax; and eax,0xB2`). The 179 names can be dumped with `csdis.py 0x55D220 0x13A0`
  (string refs) — all 179 vanilla surfinfo.dat names match, no duplicates.
* `LoadAdhesiveLimits` 0x55D0E0 (`data\surface.dat`, `;` comments): each data line = name then `row+1`
  numbers (`-` = 0.0); row `r` (0-based, counted over data lines) stores `value[c]` at `this + r*0x18 + c*4`
  and symmetrically at `this + c*0x18 + r*4` — a **6×6 float matrix** (RUBBER, HARD, ROAD, LOOSE, SAND, WET).
  No bound on r or c. Names are ignored (positional).
* `LoadSurfaceInfos` 0x55EB90 (`data\surfinfo.dat`, `#` comments):
  `sscanf("%s %s %f %f %s %s" + 29×"%d" + "%s")` = **36 fields**: name, adhesionGroup, tyreGrip, wetGrip,
  skidmark, frictionEffect, 29 ints (softland, seeThro, shootT, sand, water, sWater, beach, steepSl, glass,
  stairs, skateable, pavement, roughness(2 bits), flame(2 bits), sparks, sprint, footsteps, footdust,
  cardirt, carclean, wGrass, wGravel, wMud, wDust, wSand, wSpray, procPlant, procObj, climbable), bulletFx.
  Stored into `this + id*0xC + 0x90..0x9B`: +0x90 tyreGrip (u8, `ftol`), +0x91 wetGrip (u8), +0x94/+0x98
  bit fields. Adhesion group: RUBBER 0 / HARD 1 / ROAD 2 / LOOSE 3 / SAND 4 / WET 5 (exact); unknown ->
  bits unchanged (0). Skidmark DEFAULT/SANDY/MUDDY, friction NONE/SPARKS, bulletFx NONE/SPARKS/SAND/WOOD/DUST —
  unknown -> unchanged. The trailing 37th vanilla token (name again) is ignored.
* `LoadSurfaceAudioInfos` 0x55F2B0 (`data\surfaud.dat`): `sscanf("%s %d"×9)` -> 9 flag bits at +0x98
  bits 10..18 (CON GRS SND GRV WOD WTR MTL LGS TIL).

### 9.2 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-55** | any of the three files missing | crash | `LoadLine(NULL)` 0x55D111 / 0x55EBB0 / 0x55F2D5 | exist |
| **PDD-56** | surface.dat with more than 6 data lines, or a line with more than `row+1` values | garbage | writes past the 6×6 matrix into the surface-info records (`this+0x90..`) | count |
| **PDD-57** | surface.dat with fewer than 6 lines | ignored | remaining limits stay 0 -> zero friction for those group pairs | count |
| **PDD-58** | surfinfo.dat / surfaud.dat name not one of the 179 (case-sensitive) | garbage (silent) | id 0 -> the **DEFAULT** surface takes that row's properties | lookup |
| **PDD-59** | surfinfo.dat line with fewer than 36 fields or non-numeric int fields | garbage | uninitialised stack for the rest | count |
| **PDD-60** | adhesionGroup / skidmark / frictionEffect / bulletFx not in their word lists | ignored | bits keep the previous/zero value | lookup |
| **PDD-61** | tyreGrip / wetGrip outside 0..255 after `ftol` (wetGrip is negative in vanilla: -0.25 -> stored as u8 0xFF wrap) | info | u8 store; vanilla relies on the wrap for wet grip | note only |
| **PDD-62** | a `%s` token >= 32 chars (name/adhesion/skidmark/friction 32 B; bulletFx 32 B; surfinfo name buffer 64 B) | crash | sscanf without width | length |
| **PDD-63** | a surface name of the 179 has no line in surfinfo.dat or surfaud.dat | info | keeps zeros (no footsteps / no audio class) | coverage |

Vanilla: 6 / 179 / 179 rows, all names known -> 0 findings.

---------------------------------------------------------------------------------------------------

## 10. shopping.dat — `CShopping::LoadStats` 0x49B6A0, `LoadPrices` 0x49B8D0, `LoadShop` 0x49BBE0

### 10.1 Facts

* Section grammar (`FindSection` 0x15659E0 / `GetNextSection` 0x156B920): a line starting with the 7 bytes
  `section` (`strncmp`, lowercase) opens a section; its name is the **second `strtok(" \t")` token**;
  a line starting with `end` closes it. `FindSection(file, name)` scans forward, tracks nesting depth, and
  matches only sections at depth 1 relative to the current position with `_stricmp` (case-insensitive);
  leaving the enclosing section (depth < 0) returns false. `GetNextSection` returns the next section name or
  NULL on `end`/EOF. `#`-lines and empty lines are skipped.
* Price sections (names table 0x8A61D8, 11 entries, matched case-insensitively): 0 None, 1 CarMods,
  2 CarPaintJobs, 3 Furniture, 4 Clothes, 5 Haircuts, 6 Tattoos, 7 Gifts, 8 Food, 9 Weapons, 10 Property.
  Unknown subsection name in `LoadStats` -> index -1 (treated like the model-name sections).
* `LoadStats` (startup, all subsections of `section prices`): per item line, tokens split with
  `strtok(" \t,")`: key = `GetUppercaseKey(name)` for Clothes/Haircuts/Tattoos; `FindWeaponType(name)`
  for Weapons; **stale previous key** for CarPaintJobs (0x49B72B: `local_10` not assigned); otherwise
  `local_10 = -1; CModelInfo::GetModelInfo(name, &local_10)` (model id, -1 when the name is not a model).
  Then skip nametag; skip 2 more tokens for sections 4..6, 1 for section 9; then `stat1 change1 stat2
  change2` — stat name via 0x1565360 (**exact, case-sensitive**: fat 0, respect 1, sexy 2, health 3,
  stamina 4, calories 5, anything else incl. `-` -> -1) and `atol(change)` stored as int8. Writes
  `ms_keys[n]` (0xA97D90), `ms_bHasBought[n]` (0xA972A0), `ms_statModifiers[n]` (0xA974D0) and `n++` —
  **no check against 560** (`CShopping::Init` clears 0x230 = 560 keys).
* `LoadPrices(sectionName)` (when a shop is opened): `GetPriceSectionFromName` 0x49AAD0 (`_stricmp`,
  unknown -> -1) -> `ms_priceSectionLoaded`; re-opens the file, `FindSection("prices")`,
  `FindSection(sectionName)`; per line: key as above (`GetKey` 0x1561A10: 4/5/6 uppercase hash, 9
  FindWeaponType, 2 -> the constant 2, else GetModelInfo/-1), `strncpy(nameTag, token2, 8)`, section-specific
  extras (Clothes/Haircuts: modelName hash + `atol(type)`; Tattoos: `-`/int type1 + hash(type2); Weapons:
  `atol(ammo)`), then **4 tokens skipped** (stats) and the 5th = `atol(price)`; `ms_prices[n]` (0xA986F0,
  0x18 stride) and `ms_numPrices++` — **no check against 300** (0xA986F0 + 300×0x18 = 0xA9A310 =
  `ms_numBuyableItems`). Afterwards `GetAnimationBlockIndex(ms_sectionNames[ms_priceSectionLoaded])`:
  with index **-1** this reads the dword before the table = 0x0000FFFF and `GetAnimationBlock` runs
  `_stricmp` on address 0xFFFF -> **access violation**.
* `LoadShop(shopName)`: `strcpy` shopName into `ms_shopLoaded[24]` 0xA9A7D8 (next field
  `ms_numItemsInShop` at 0xA9A7F0 — exactly 24 bytes); `FindSection("shops")`, `FindSection(shopName)`;
  lines `type <name>` -> `LoadPrices(name)` (strcpy of the token into `local_20[32]`), `item <name>` ->
  `GetKey(name, ms_priceSectionLoaded)` -> `ms_shopContents[n++]` (0xA9A318, **no check against 300**;
  0xA9A318 + 300×4 = 0xA9A7C8 = `ms_priceSectionLoaded`). The `type` line must precede the `item` lines
  (items are keyed with the current price section).

### 10.2 Rules

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-64** | `data\shopping.dat` missing | crash | `LoadStats`: `FindSection(NULL)` -> `LoadLine(NULL)` at 0x15659EB | exists |
| **PDD-65** | no `section prices` at top level | ignored (warning) | `LoadStats` loads nothing; every shop empty | structure |
| **PDD-66** | total item lines under `prices` (all subsections) > **560** | garbage -> crash | `ms_keys`/`ms_bHasBought`/`ms_statModifiers` overflow into `ms_priceModifiers` 0xA98650 / each other; at 2400 items the counter itself is overwritten | count |
| **PDD-67** | one `prices` subsection with > **300** items, or one shop with > 300 `item` lines | garbage -> crash | `ms_prices[300]` = `ms_numBuyableItems`; `ms_shopContents[300]` = `ms_priceSectionLoaded` | count per section |
| **PDD-68** | `type <name>` in a shop with a name not among the 11 price-section names (case-insensitive) | crash | `GetPriceSectionFromName` -> -1 -> `ms_sectionNames[-1]` = 0xFFFF -> `_stricmp((char*)0xFFFF, …)` in `GetAnimationBlock` (called from 0x49BB8A via `GetAnimationBlockIndex` 0x4D3990) | lookup |
| **PDD-69** | `type <name>` naming a price section that does not exist under `prices` (e.g. `type Furniture` with an empty section is fine; `type Gifts` when the subsection is absent) | ignored (warning) | `FindSection` fails -> no prices -> shop shows nothing | cross-ref |
| **PDD-70** | `item` line before any `type` line in a shop | garbage | keyed with the previously loaded section (or None) -> key never matches a price | order |
| **PDD-71** | `item <name>` not present in the corresponding `prices` subsection (key mismatch: same lookup as PDD-73/74) | ignored (warning) | shop lists an item without a price entry; UI skips or shows garbage name | cross-ref by computed key |
| **PDD-72** | nametag longer than 7 chars | garbage | `strncpy(…, 8)` leaves no NUL; the GXT lookup reads into the next field | length |
| **PDD-73** | item name in CarMods / Furniture / Gifts / Food / Property (or an unknown subsection) that is not a model name in any IDE (case-insensitive) | garbage | key -1; all such items collide on key -1 (`LoadShop` "bought" scan and price matching pick the first) | lookup (vanilla: veh_mods.ide for CarMods, props.ide for Food) |
| **PDD-74** | Weapons item name not a weapon-type name (case-insensitive, §1.1) | garbage | `FindWeaponType` -> 0 UNARMED: buying gives nothing, all unknowns collide on key 0 | lookup |
| **PDD-75** | stat name (tokens 1 and 3 of the stats pair) not one of fat/respect/sexy/health/stamina/calories/`-` (exact) | ignored | -1 = no stat change | lookup |
| **PDD-76** | stat change outside -128..127, price/ammo/type not integers | garbage | int8 store; `atol` -> 0 | range / parse |
| **PDD-77** | item line with too few tokens for its section (CarMods/Food/…: 7; Clothes/Haircuts/Tattoos: 9; Weapons: 8) | garbage / crash | `strtok` returns NULL -> `atol(NULL)` / `GetUppercaseKey(NULL)` -> NULL deref in `LoadPrices` (0x49BAB2 `_atol(pcVar5)` with pcVar5 NULL) | count |
| **PDD-78** | a `prices` subsection named `CarPaintJobs` with items | garbage | `LoadStats` stores the previous item's key (uninitialised for the first); `GetKey` returns the constant 2 | presence |
| **PDD-79** | shop section name (under `shops`) or `type` token >= 24 / >= 32 chars | crash | `strcpy` into `ms_shopLoaded[24]` (overruns `ms_numItemsInShop`, then `ms_numPrices`… ) / into `local_20[32]` | length |
| **PDD-80** | Clothes/Haircuts model name (3rd token) not an entry of player.img; Tattoos texture (name) not in the tattoo TXDs | ignored (warning) | key hash never matches a streamed clothes model -> item invisible in the shop | cross-ref with player.img directory (case-insensitive stem) |
| **PDD-81** | `section`/`end` imbalance (an `end` closing `prices` early, or missing `end` for a subsection) | garbage | `FindSection` depth tracking: items after a stray `end` are attributed to the wrong subsection or skipped | bracket check |

Vanilla: 544 items (Clothes 258, CarMods 170, Tattoos 48, Haircuts 33, Weapons 20, Food 15), max shop
89 items, all nametags <= 7, all stat names valid, all model/weapon names resolve -> 0 findings.

---------------------------------------------------------------------------------------------------

## 11. statdisp.dat / ar_stats.dat — `CStats::LoadStatUpdateConditions` 0x559860 / `LoadActionReactionStats` 0x5599B0

* `LoadStatUpdateConditions`: `DATA\STATDISP.DAT`, `#` comments; `sscanf("%d %s %s %f %s", &statId,
  statName[64], condition[12], &value, gxtLabel[8])`; entry `tStatMessage` (0x10 bytes) at
  `CStats::StatMessage` 0xB78200: +0 statId (int16), +2 0, +3 condition (0 = `lessthan`, 1 = `morethan`,
  compared with memcmp incl. NUL; else stays 0 = lessthan), +4 value (float), +8 gxt (strcpy, unbounded);
  count -> `TotalNumStatMessages` 0xB794D0. Table end: `LastMissionPassedName` at 0xB78A00 ->
  **(0xB78A00-0xB78200)/0x10 = 128 entries**. No bound check (0x559977 `add ebp,0x10`).
* `LoadActionReactionStats`: `DATA\AR_STATS.DAT`; `sscanf("%d %s %f", &id, name[76], &value)`;
  `StatReactionValue[id] = value` (0xB78F10, table ends at `StatTypesInt` 0xB79000 -> **60 floats**,
  ids 0..59). No bound check (0x559A1D).
* Stat ids used by statdisp: floats 0..82 (`StatTypesFloat` 0xB79380..0xB794CC), ints 120..342
  (`StatTypesInt` 0xB79000..0xB79380, index = id-120; gta-reversed `FIRST_INT_STAT = 120`).

| id | condition | severity | evidence / effect | how to check |
|---|---|---|---|---|
| **PDD-82** | either file missing | crash | `LoadLine(NULL)` 0x5598A5 / 0x5599E0 | exist |
| **PDD-83** | statdisp.dat with more than **128** data lines | garbage | entry 128 overwrites `LastMissionPassedName` 0xB78A00, then `TimesMissionAttempted`… (save-game stats corrupted) | count |
| **PDD-84** | gxt label (5th token) longer than 7 chars | garbage | `strcpy` into 8 bytes -> next entry's statId overwritten; also the 8-byte sscanf slot overruns the `condition` slot | length |
| **PDD-85** | condition token not exactly `lessthan` / `morethan` | ignored | treated as lessthan | exact |
| **PDD-86** | statId not in 0..82 or 120..342, or fewer than 5 tokens, or value not a float | garbage | `GetStatValue` reads outside the stat tables; uninitialised fields | range / count |
| **PDD-87** | statName (2nd token) >= 64 chars, condition >= 12 chars | crash | stack buffers (frame 0x78) | length |
| **PDD-88** | ar_stats.dat id outside 0..59 | garbage / crash | `StatReactionValue[id]` write: 60..123 land in `StatTypesInt`, larger ids anywhere | range |
| **PDD-89** | ar_stats.dat name token >= 76 chars | crash | `%s` into the 0x58-byte frame slot | length |
| **PDD-90** | ar_stats.dat has fewer than 59 ids (0..58, vanilla) | info | missing reaction values stay 0 -> that action never updates the stat | coverage |

Vanilla: statdisp 122 rows (6 lessthan / 116 morethan, labels <= 7, ids 21..230), ar_stats 59 rows ids 0..58
-> 0 findings.

---------------------------------------------------------------------------------------------------

## 12. Summary of hard limits

| item | limit | evidence |
|---|---|---|
| weapon info entries | 80 (types 0..48; skill rows 47..79) | 0x5BF750 loop to 0xC8CDBC |
| gun aiming offsets | 21 (groups python..spraycan) | 0x5BF750 loop to 0xC8AAA4 |
| weapon slots | 0..12 | CPed::m_aWeapons[13], 0x5E6093 |
| weapon.dat `$` fields | 26 mandatory + 4 optional | 30 pushes at 0x5BE860 + pre-zeroing 0x5BE92F |
| ped types | 32 names | table 0x8D23B8, `new(0x284)` |
| ped stats | 43 rows, name <= 23 | `new(0x8BC)`, 24-byte name field |
| ped groups | 57 × 21 | 0xC0ECC0/0xC0F358 vs 0xC0ED38/0xC0FCB2 |
| popcycle rows | 480 = 20 zones × 2 × 12 | loop bounds 0x168/0x12, 2, 0xC |
| anim assoc groups | 145 total, 118 built in, 27 from animgrp.dat; names <= 15, anim names <= 23 | 0x4D3BA0, 16-byte fields |
| ped events | 41 per DM, event ids 0..95 | 0x99C = 41×0x3C, `m_EventIndices` 96 dwords |
| decision makers | 20 (0..7 ped, 8..9 group, 10..19 mission) | 0xC034 = 4 + 20×0x99C |
| surfaces | 179 (ids 0..178), adhesion matrix 6×6 | 0x55D220 chain, 0x18 row stride |
| shopping | 560 items in `prices`, 300 per subsection, 300 per shop, nametag <= 7, shop name <= 23 | 0x49C290 clears 0x230; 0xA986F0+300×0x18 = 0xA9A310; 0xA9A318+300×4 = 0xA9A7C8; 0xA9A7D8+24 = 0xA9A7F0 |
| stat messages | 128; reaction stats ids 0..59 | 0xB78200..0xB78A00; 0xB78F10..0xB79000 |

## 13. Open questions

* The `%`-line group index for a group named in animgrp.dat (index >= 118) provably writes into
  `aWeaponInfo`; which entry is corrupted depends on the total number of definitions — reported as a range.
* `CPopulation::AddPed` with a non-ped model id (PDD-17) was not traced instruction by instruction; the
  crash claim rests on the `CPedModelInfo` field reads in the spawn path (gta-reversed names) and should be
  confirmed with a hook if the checker wants Краш rather than Ошибка.
* The eZonePopulationType order (PDD-28) comes from the vanilla file comments only; the engine maps
  zone-type ids from `info.zon`/IPL zones — not verified here.
* `FindPedRaceFromName` codes (1 B, 2 W, 3 I/O, 4 H) are taken from the switch; their semantic names are
  gta-reversed guesses.
