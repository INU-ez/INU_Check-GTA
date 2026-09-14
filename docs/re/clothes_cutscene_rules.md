# GTA SA 1.0 US — PLAYER CLOTHES (player.img / clothes.dat / shopping.dat) and CUTSCENES (anim\cuts.img / models\cutscene.img) loading paths

Source of truth: Ghidra decompiles in `E:\RE\asm_data2\<ADDR>.c/.asm` (+ capstone disassembly via
`scratchpad\cdis.py` for the parts Ghidra cut off: tail of `CreateSkinnedClump`, `.CUT` parser
continuation 0x5B0840-0x5B1198, `CDirectory::ReadDirFile`, `CStreaming::RequestSpecialModel`,
`CCamera::LoadPathSplines`, `CPlayerSkin::GetSkinTexture`), raw bytes/strings via `readva.py` /
`scratchpad\strptr.py`. gta-reversed / plugin-sdk used for names and array sizes only; every claim
below cites exe code. Vanilla data spot-checked with the scripts described in §9 (all 152 clothes
DFFs, 390 clothes TXDs, 148 `.cut`/`.dat`/`.ifp` triples, clothes.dat, shopping.dat).

Legend: **CRASH** = guaranteed fault / hang, **CRASH-likely** = garbage pointer or heap corruption
that faults later, **GARBAGE** = wrong result, no fault, **IGNORED** = silently dropped.

---------------------------------------------------------------------------------------------------

## 1. player.img — the private directory of the clothes builder

`CClothesBuilder::LoadCdDirectory` 0x5A4190 and (again, on every rebuild) `CreateSkinnedClump`
0x5A69D0:

```
CDirectory::Init(&DAT_00bc12c0, 0x226 /*550 entries*/, &DAT_00bbcdc8 /*static 550*32 bytes*/)
CDirectory::ReadDirFile(&DAT_00bc12c0, "MODELS\\PLAYER.IMG")
```

`CDirectory::ReadDirFile` 0x532350 (disassembled): `fopen(rb)`, `Read(4)` magic (**never checked**),
`Read(4)` count, then per entry `Read(0x20)` and `if (numEntries < capacity) copy else
printf("Too many objects without modelinfo structures\n")` — entries beyond capacity are **dropped**.
Entry = `{u32 offsetSectors, u16 streamingSizeSectors, u16 sizeInArchive, char name[24]}`.

player.img is registered with the streamer only for reading: `CClothes::Init` 0x5A80D0 →
`CStreaming::AddImageToList("MODELS\\PLAYER.IMG", 0)` — the `0` means it is NOT walked by
`CStreaming::LoadCdDirectory` 0x5B6170, so its entries never reach `ms_pExtraObjectsDir` and never
raise the streaming buffer size (`DAT_008e4ca8` is updated only at 0x5B6170:51 from the streamed IMGs).

Lookup: `CDirectory::FindItem(key,&off,&size)` 0x5324D0 compares `CKeyGen::GetUppercaseKey(entry.name)`
(0x53CF30, CRC32 of uppercased C string, reads until NUL) with the requested key
`AppendStringToKey(hash(name), ".DFF"|".TXD")` (0x5A41C0 / 0x5A4220 / 0x5A55A0). On a miss it returns 0
and **leaves `off`/`size` untouched (uninitialised stack)** — every caller passes them straight to
`CStreaming::RequestFile` 0x40A080 without testing the return value.

Sizes: `FindItem` returns `*(u16*)(entry+4)` (streaming size, sectors); `RequestFile` stores
`offset & 0xffffff` and the size; `ConvertBufferToObject` 0x40C6B0 reads `size << 11` bytes from the
shared streaming buffer whose size is the largest entry of the *streamed* IMGs (vanilla 1263 sectors,
`vgwsthiway1.txd`). A player.img entry bigger than that overflows the buffer.

Vanilla: 542 entries (**8 free slots**), longest name 20, all streaming sizes non-zero, max entry 385
sectors (`player_torso.txd`).

## 2. CPedClothesDesc and the name tables

`CPedClothesDesc` (0x78 bytes, `Initialise` 0x5A78F0): `+0x00 u32 models[10]`, `+0x28 u32 textures[18]`,
`+0x70 float fat`, `+0x74 float muscle`. All names are stored as `GetUppercaseKey` hashes
(`SetModel` 0x5A7920, `SetTextureAndModel` 0x5A8050/0x5A8080).

Model parts (`GetClothesModelFromName` 0x5A7A20, **case-sensitive full-string compare**, unknown →
returns 0 = torso):

| idx | name | default model (0x5A69D0 `SetModel` when slot == 0) | texture slot used for the material (`PTR_DAT_008d0a7c[10]`) |
|---|---|---|---|
| 0 | torso | `torso` | "torso" |
| 1 | head | `head` | "head" |
| 2 | hands | `hands` | "torso" (hands share the torso texture) |
| 3 | legs | `legs` | "legs" |
| 4 | feet | `feet` | "feet" |
| 5 | necklace | – | "necklace" |
| 6 | watch | – | "watch" |
| 7 | glasses | – | "glasses" |
| 8 | hat | – | "hat" |
| 9 | extra1 | – | "extra1" |

Texture parts (`GetTextureDependency` 0x5A7EA0 / `GetDependentTexture` 0x5A7F30): 0 torso, 1 head
(face), 2 legs, 3 feet, **4..12 = nine tattoo layers** (no model), 13 necklace, 14 watch, 15 glasses,
16 hat, 17 extra1. `shopping.dat` `type` column is this index.

Hard-coded base TXD names (`PTR_s_player_torso_008d0a4c`..): `player_torso`, `player_legs`,
`player_face`, `player_feet`; texture names looked up inside them: `torso`, `torso_fat`,
`torso_ripped`, `legs`, `legs_fat`, `legs_ripped`, `face`, `face_fat`.

## 3. clothes.dat — `CClothes::LoadClothesFile` 0x5A7B30

Opened via `CFileMgr::OpenFile("DATA\\CLOTHES.DAT","r")`, read with `CFileLoader::LoadLine` 0x536F80
(≤ 511 chars, control chars and `,` → space, leading blanks skipped). Grammar as executed:

```
state = 0
for each line:
    if line[0]=='#' or line empty: continue
    if state==0: if strncmp(line,"rule",4)==0: state=1 ; continue        ; everything else ignored
    if strncmp(line,"end",3)==0: state=0; continue                        ; CASE-SENSITIVE, prefix only!
    kw = strtok(line," \t,")  ; stricmp against cuts/setc/tex/hide/ignore/endignore/exclusive/endexclusive
    type = 0/1/2/3/5/4/7/6 respectively  ; unknown keyword -> `local_14` keeps its PREVIOUS value (uninitialised on 1st line)
    rules[n++] = type
    a = strtok(NULL); b = strtok(NULL)                                    ; both fetched for every keyword
    CUTS/TEX : rules[n++]=key(a); rules[n++]=key(b)                       ; 3 words
    SETC     : c=strtok; d=strtok; rules[n++]=key(a); rules[n++]=GetClothesModelFromName(b);
               rules[n++]= (c=="-")?0:key(c); rules[n++]=(d=="-")?0:key(d) ; 5 words
    HIDE     : rules[n++]=key(a); rules[n++]=GetClothesModelFromName(b)   ; 3 words
    IGNORE/ENDIGNORE/EXCLUSIVE/ENDEXCLUSIVE : rules[n++]=key(a)           ; 2 words
```

* `GetUppercaseKey(NULL)` (`mov al,[edi]` at 0x53CF36) and the inline `"-"` compare on `c`/`d`
  dereference a NULL token → **CRASH** when a rule has too few arguments.
* `rules` = `ms_clothesRules` at 0xBC1300, **600 u32** (gta-reversed sizing; the next live global is
  `ms_lastClothesDesc` at 0xBC1C78 = word 606, then `g_bCutSceneFinishing` 0xBC1CF8 and the
  `CCutsceneMgr` state from 0xBC1D68). `ms_numRuleTags` 0xBC12FC has no bound check. Vanilla uses
  **587 words** — three more SETC lines overflow.
* The `end` test is `strncmp("end", line, 3)` — **case-sensitive prefix**. A lowercase `endignore x`
  or `endexclusive x` line inside the block ends the block; all following rules are ignored until the
  next `rule` line. Vanilla writes `ENDIGNORE` / `ENDEXCLUSIVE` in upper case for exactly this reason.
* Component names (`GetClothesModelFromName`) must be exactly `torso head hands legs feet necklace
  watch glasses hat extra1` (lowercase; the compare includes the terminator). Anything else → 0 = torso.

Evaluation (`PreprocessClothesDesc` 0x5A44C0): rules are replayed in file order for each rebuild; the
IGNORE/EXCLUSIVE tags live in a local table of **8** entries (`local_20[8]`, the `case 5`/`case 7`
loops give up silently when no free slot). SETC `-` for model/texture means "keep the current one"
(`local_30 == 0 → textures[GetDependentTexture(comp)]`, `local_34 == 0 → models[comp]`), so a SETC
target never becomes a NULL hash through the rule file itself. CUTS rules only apply when
`bCutscenePlayer` (`p1`) is set.

## 4. Building CJ — `CClothes::ConstructPedModel` 0x5A81E0 → `CClothesBuilder::CreateSkinnedClump` 0x5A69D0

Called from `RebuildPlayer` 0x5A82C0 (`modelId = ped->m_nModelIndex` = 0 for CJ) and
`RebuildCutscenePlayer` 0x5A8270 (`modelId = 1`, `bCutscenePlayer = true`). Arguments are the model
info's `m_pRwObject` (+0x1C) and the `RwTexDictionary*` of its TXD slot.

Base models: `CStreaming::RequestSpecialModel(0,"player")` (script) / `RequestModel(1)` (cutscene);
`player.dff` (gta3.img, 32-node skeleton) and `csplay.dff` (gta3.img, 61-node cutscene skeleton,
`default.ide` `hier 1, csplay, player`). `RequestSpecialModel` 0x409D10 sets the model's TXD to
`FindTxdSlot(name)` or **"generic"** (0x409F92, string 0x858A34) when no `player.txd` slot exists.

Sequence in 0x5A69D0:

1. `CDirectory::Init/ReadDirFile` (§1), default parts filled in.
2. Compare with the old desc (`bTexturesChanged`, `bModelsChanged`, `bFatChanged` flags at 0x8D0AA4-A6);
   nothing changed → return NULL (no rebuild).
3. `PreprocessClothesDesc(copy, bCutscene)` (§3), fat/muscle → three blend weights (0x5A42B0).
4. If textures changed and not cutscene: `RwTexDictionaryForAllTextures(txd, 0x5A44A0 /*remove+destroy*/)`
   — **the model's TXD is emptied** — then `ConstructTextures` (§4.2) refills it.
5. Ten materials from the ten names of `PTR_DAT_008d0a7c` (`RwTexDictionaryFindNamedTexture`; a
   missing name gives a NULL material pointer for that part).
6. `ConstructGeometryArray` (§4.1) → ten blended geometries.
7. `ConstructGeometryAndSkinArrays` 0x5A6530 (§4.3) → one geometry + skin arrays;
   `RpSkinCreate(numVerts, RpSkinGetNumBones(baseSkin), weights, indices, RpSkinGetSkinToBoneMatrices(baseSkin))`.
8. Tail (disassembled 0x5A6CE6-0x5A6DA6): `RpHAnimHierarchyCreateFromHierarchy(baseHier)`, new frame,
   `RpAtomicCreate/SetGeometry/SetFrame`, `RpSkinAtomicSetHAnimHierarchy`, `RpSkinAtomicSetType(1)`,
   `RpClumpCreate/AddAtomic`, part geometries and materials destroyed, new clump returned; caller
   (0x5A81E0) calls `modelinfo->DeleteRwObject()` then `SetClump(newClump)`.

### 4.1 Part geometry — `ConstructGeometryArray` 0x5A55A0

For slot i = 0..9 with `models[i] != 0`: model id **384+i** (`ms_modelInfoPtrs[0x180+i]`),
`modelinfo->m_nFlags |= 2` (byte +0x13), `FindItem(key+".DFF")`, `RequestFile(384+i, off, size,
ms_clothesImageId, 0x12)`, `LoadAllRequestedModels(1)`, prefetch of slot i+1, then

```
geo[i] = BlendGeometry(modelinfo->m_pRwObject, "normal", "fat", "ripped", wNormal, wFat, wRipped)   ; 0x5A4940
StoreBoneArray(modelinfo->m_pRwObject, i)                                                          ; 0x5A48B0
CStreaming::RemoveModel(384+i)
```

`m_pRwObject` is **never NULL-checked**; if the DFF was not found (§1) or `RpClumpStreamRead`
failed, `BlendGeometry` calls `RpClumpForAllAtomics(NULL,…)` → **CRASH**.

Flag 2 selects the multi-clump loader in `CFileLoader::LoadClumpFile` 0x5372D0 (asm_map): it creates
an empty clump + root frame and loops `RwStreamFindChunk(CLUMP) → RpClumpStreamRead → clone root frame
under the new root → move atomics (0x537290: RpAtomicClone, RpAtomicSetFrame(frame->root),
RpClumpAddAtomic)` until no more CLUMP chunks. **A clothes entry is therefore a concatenation of three
complete DFF clumps** (vanilla: every one of the 152 entries has exactly 3, each with 1 atomic, frame
names `Normal`/`Fat`/`Ripped` in any case). Any failed `RpClumpStreamRead` returns 0 → model not loaded
→ crash above.

`CClumpModelInfo::SetClump` 0x4C4F70 with flag 2: if the **first** atomic's geometry has an RpSkin,
`RpClumpForAllAtomics(SetHierarchyForSkinAtomic, NULL)` 0x4C4EF0 → each atomic gets
`GetAnimHierarchyFromFrame(atomic->frame)` 0x734AB0 (the frame's own HAnim plugin, else the first
child carrying one). No skin on the first atomic → no hierarchies set at all.

`BlendGeometry` 0x5A4940: `GetAtomicWithName` 0x5A4810 (`stricmp(GetFrameNodeName(atomic->frame),
name)`, returns 0 when absent) for "normal", "fat", "ripped"; then **without any NULL test** reads
`atomic+0x18` (geometry), `geometry+0x5C` (morph target), `+0x14` vertices, `+0x18` normals,
`+0x34` texcoords[0], `RpSkinGeometryGetSkin` → `RpSkinGetVertexBoneIndices/Weights`. Loop bound is
the **"normal" geometry's vertex count**; fat/ripped arrays are read in lock-step, the result is
written back into the "normal" geometry (positions, normals normalised, UV0, up to 4 bone weights merged
by `AddWeightToBoneVertex` 0x5A4840 and renormalised). Consequences: atomic missing → NULL deref
**CRASH**; skin missing → `RpSkinGetVertexBoneIndices(NULL)` **CRASH**; normals or UV0 missing →
NULL array deref **CRASH**; fat/ripped with fewer vertices → heap over-read **GARBAGE / CRASH-likely**.

`StoreBoneArray` 0x5A48B0: hierarchy of the "normal" atomic (`RpSkinAtomicGetHAnimHierarchy`, NULL →
`*(NULL+4)` **CRASH**), copies `numNodes` node ids into `gBoneIndices[i][64]` (0xBBC8C8 + i*0x80,
int16) and terminates with -1 only if `numNodes < 64`.

### 4.2 Textures — `ConstructTextures` 0x5A6040 (asm read; the Ghidra output is scrambled)

`RequestTexture` 0x5A4220 loads `<hash>.TXD` from player.img into one of the four rotating dummy TXD
slots (`CTxdStore::defaultTxds[4]` at 0xC88004, `RequestFile(20000+slot,…)`); the helper
`0x5A5F70` ("FillPalette" in the symbol file, really *GetTextureFromTxdAndLoadNextTxd*) takes
`(accumTex, curSlot, nextHash, &nextSlot)`: it loads the current slot, takes `GetFirstTexture(txd)`
(0x734940) and either `CopyTexture` (accum == 0) or `PlaceTextureOnTopOfTexture(accum, first)`
(0x5A57B0: for every pixel of the **source** raster, if `alpha != 0` copy the 32-bit pixel into the
destination), removes the slot, and requests the next hash (returns -1 when the hash is 0).

Pipeline (texture slot numbers from §2):

```
tattoo  = overlay chain over textures[4..12] (first present is CopyTexture'd, the rest placed on top)
torso   = CopyTexture(player_torso.txd:"torso") blended with "torso_fat"/"torso_ripped" (0x5A59C0 or,
          with a tattoo layer, 0x5A5BC0) ; then <textures[0]>.txd first texture placed on top ; named "torso"
legs    = CopyTexture(player_legs.txd:"legs") blended with "legs_fat"/"legs_ripped" ; <textures[2]>.txd first
          texture placed on top ; named "legs"
head    = TXD <textures[1]> (or player_face if 0): "face" (blended with "face_fat" if present, 0x5A5820)
          else GetFirstTexture ; CopyTexture ; named "head"
feet    = TXD <textures[3]> (or player_feet): GetFirstTexture, CopyTexture ; named "feet"
necklace/watch/glasses/hat/extra1 = TXD <textures[13..17]>: GetFirstTexture, CopyTexture (only if hash != 0)
```

`CopyTexture` 0x5A5730 (0x5A5747 after the SecuROM jump): `RwRasterCreate(w,h,depth,(fmt&0xFF00)|4)`,
`memcpy(stride*height)`, `RwTextureCreate`, filter 2 — format preserved, **`srcTex` dereferenced
without test** (`mov ebx,[eax]`) → `GetFirstTexture` of an empty TXD or `FindNamedTexture` miss → **CRASH**.
`BlendTextures` 0x5A59C0 / 0x5A5BC0 / 0x5A5820: `RwRasterLock` all rasters (NULL raster → **CRASH**),
loop `width*height` of the **first source** raster, read/write **4 bytes per pixel**. So every raster
that takes part in a blend or overlay must be 32 bpp, uncompressed, and of identical dimensions;
a DXT raster's lock buffer is 4-8× smaller than `w*h*4` → heap overrun **CRASH-likely**; a larger
overlay writes past the destination **CRASH-likely**, a smaller one leaves the rest untouched (GARBAGE).
Vanilla: every player.img raster is 32 bpp, format 0x500/0x600, 1 mip level; torso overlays and tattoos
256×256, legs overlays 128×256, heads 128×128, feet 64×64.

### 4.3 Merge — `ConstructGeometryAndSkinArrays` 0x5A6530

`RpGeometryCreate(sumVerts, sumTris, 0x35)`; per part: copy positions/normals/UV0, triangles with
`RpGeometryTriangleSetVertexIndices(tri, v0+base, …)` where `base` is a **`short`** (`sVar11`) and RW
triangle indices are u16 → the merged model must stay ≤ 65535 vertices (vanilla ≈ 3000);
`RpGeometryTriangleSetMaterial(tri, materials[i])`. Skin arrays: `BuildBoneIndexConversionTable`
0x5A56E0 turns the part's node-id list (§4.1) into base-hierarchy indices via `RpHAnimIDGetIndex`
(**-1 → 0 = root**), table of 64 bytes on the stack; each vertex's four bone indices are looked up in
that table (**index ≥ numNodes reads stack garbage → vertex follows a random bone, GARBAGE**), weights
copied as is. The part's own skin-to-bone matrices are discarded — parts must be modelled on the base
bind pose. Vanilla parts contain ids not present in `player.dff` (303-307 helper bones, 5001+ face
bones of the cutscene skeleton) — those silently map to the root; that is stock behaviour.

## 5. shopping.dat link — `CShopping::LoadPrices` 0x49B8D0

`section prices` → `section Clothes|Haircuts` (ids 4/5): `texturename nametag modelname type stats… price`
(`strtok " \t,"`); id 6 `Tattoos`: `texturename nametag type1 type2 …` (`-` = -1). `nametag` is
`strncpy(…, 8)` (GXT key ≤ 7). `texturename`/`modelname` are hashed with `GetUppercaseKey` and end up
in `CPedClothesDesc` via `SetTextureAndModel(tex, model, type)` — so `<texturename>.txd` and
`<modelname>.dff` must exist in player.img (§1, §4) and `type` must be a valid texture-part index
(Clothes: 0,2,3,13..17; Haircuts: 1; Tattoos: 4..12); any other value indexes `textures[]` out of
range (`SetTextureAndModel` writes `+0x28 + type*4`) → GARBAGE / stack-neighbour corruption.

## 6. Player skin texture — `CPlayerInfo::LoadPlayerSkin` 0x56F7D0 / `SetPlayerSkin` 0x5717F0

Only caller: `CGame::Init2` 0x5BA1FA. `CPlayerSkin::GetSkinTexture` 0x6FFA10: looks the name up in
the "skin" TXD slot (created by `CPlayerSkin::Initialise` 0x6FF8A0), otherwise `RtBMPImageRead` of
`models\generic\player.bmp` (name `$$""` or empty) or `skins\%s.bmp`; a read failure returns NULL and
the caller just stores NULL (`m_pSkinTexture`, +0x184). Vanilla ships no `player.bmp`. Nothing to
validate — VC leftover; a present BMP must merely be a readable BMP.

## 7. Cutscenes — `CCutsceneMgr`

### 7.1 Directories and files

* `Initialise` 0x4D5A20: `ms_pCutsceneDir = new CDirectory(0x200)` → **512 entries** (0x5322A0 allocates
  `n*32`). `LoadCutsceneData_overlay` 0x5B13F0 resets `numEntries = 0` and `ReadDirFile("ANIM\\CUTS.IMG")`
  before every cutscene (same unchecked-magic reader as §1; excess entries dropped). Vanilla: 444
  entries (148 × `.cut/.dat/.ifp`), longest name 11.
* `_overlay` copies the script's cutscene name into `ms_cutsceneName` **[8]** (0xBC3F0C) with an
  unbounded byte loop; 8+ chars overwrite `ms_pCutsceneObjects` (0xBC3F14/18).
* `_preload` 0x5B05A0: `"%s.CUT"` → `FindItem(name)` 0x5324A0 (stricmp; needs a NUL inside the 24-byte
  name field). Found → `new(size<<11)`, `RwStreamOpen("ANIM\\CUTS.IMG")`, skip `offset<<11`, read
  `size<<11` bytes, parse (§7.2). Not found → `LoadCutsceneData_postload` directly (no objects).
* `_postload` 0x5AFBC0: `"%s.IFP"` found → `CStreaming::MakeSpaceFor(size<<10)`,
  `CAnimManager::LoadAnimFile(stream, true, ms_aUncompressedCutsceneAnims)`,
  `CAnimBlendAssocGroup::CreateAssociations(&ms_cutsceneAssociations, ms_cutsceneName, ms_cLoadAnimName,
  ms_cLoadObjectName, 0x20)`; `ms_animLoaded = found`. `"%s.DAT"` found → `CFileMgr::Seek(offset<<11)`,
  `CCamera::LoadPathSplines(file)`; `dataFileLoaded = found`. Missing IFP → objects never animate;
  missing DAT → `HasCutsceneFinished` 0x5B0570 returns `!dataFileLoaded` = true immediately.
* Cutscene models: `models\cutscene.img` is a normal streamed IMG (`gta.dat` line 7). Its DFFs have no
  IDE entry, so `CStreaming::LoadCdDirectory` 0x5B6170:119-122 puts them into `ms_pExtraObjectsDir`
  (`new CDirectory(0x226)` at `CStreaming::Init2` 0x5B8DDF — **550 entries for all IDE-less DFFs of all
  streamed IMGs**, vanilla 392 = 317 cutscene.img + 75 gta3.img); its TXDs get ordinary TXD slots.
* `AddCutsceneHead` 0x5B0380 is `xor eax,eax; ret` — no head system exists in SA.

### 7.2 `.CUT` text grammar — `LoadCutsceneData_preload` 0x5B0800-0x5B0FEB (disassembly)

Line reader (0x5B0830): skip `\r`/`\n`; a `\0` ends the file; copy bytes until `\r`/`\n`/`\0` into a
**1024-byte** stack buffer (`esp+0x150`, no bound → a longer line smashes the stack), terminate. **No
trimming, no comma conversion.** The buffer is the raw sector-padded IMG entry — parsing relies on a NUL
somewhere in the padding (vanilla pads with spaces then zeros).

Section switch: the line is compared **whole** (`repe cmpsb` with length incl. NUL):
`end` → section 0; in section 0 the line selects `info`(1) `model`(2) `text`(3) `uncompress`(4)
`attach`(5) `remove`(6) `peffect`(7) `extracol`(8); any other line in section 0 is ignored (this is
how the vanilla `motion … end` blocks are skipped). A header with trailing blanks or a comma is not
recognised → the whole block is silently ignored.

| section | parse (exe) | storage / limit | on malformed line |
|---|---|---|---|
| info | `strncmp(line,"offset",6)==0` → `sscanf(line+6,"%f %f %f")` → `ms_cutsceneOffset`; ped removed from car; `CIplStore::AddIplsNeededAtPosn`, `CStreaming::LoadScene(offset)` | 1 vector | sscanf failure leaves the three locals uninitialised → LoadScene at garbage → **CRASH-likely**; other keywords ignored |
| model | `strtok(line," ,")` (number, `sscanf "%d"`, unused); `strtok` → model name copied (unbounded) to a 64-byte local, `_strlwr`; then for each further `strtok` token: anim name → 32-byte local, `_strlwr`; `idx = ms_numLoadObjectNames++`; `strcpy(ms_cLoadObjectName[idx], model)`, `strcpy(ms_cLoadAnimName[idx], anim)`; first token: `ms_iModelIndex[idx]=2 (LOAD_THIS)`, `ms_bRepeatObject=0`; others: `3 (USE_PREV)`, `1` | `[50]` each: 0xBC38C8 / 0xBC3288 (32 B), 0xBC31C0 (u32), 0xBC2A34 (u8); **no bound** — entry 51 overwrites `ms_cutsceneTimer` 0xBC3F08 and `ms_cutsceneName` | only a number (or blank) → `strcpy` from NULL at 0x5B0B40 → **CRASH**; no anim token → line dropped silently; names ≥ 32 spill into the next slot |
| text | `sscanf(line,"%d,%d,%s", &start,&dur, local[20])`; `_strupr`; `strcpy(ms_cTextOutput[n], label)`; `ms_iTextStartTime[n]=start; ms_iTextDuration[n]=dur; n++` | `[64]`: 0xBC2FC0 (8 B each), 0xBC2EC0, 0xBC2DC0; **no bound** — line 65 overwrites `ms_iModelIndex` | label ≥ 8 chars overwrites following labels; ≥ 20 chars smashes the stack; sscanf failure stores garbage |
| uncompress | `LoadAnimationUncompressed(strtok(line," ,"))` 0x4D5AB0: unbounded copy into `ms_aUncompressedCutsceneAnims[n++]`, then writes a NUL at slot n | `[8][32]` at 0xBC2CC0; slot 9 = `ms_iTextDuration` | blank/space-only line → `strtok` NULL → byte read at 0 → **CRASH**; unknown name simply never matches |
| attach | `sscanf(line,"%d,%d,%d")` → `ms_iAttachObjectToBone[n++] = {objA, objB, boneId}` | `[50]` × 12 B at 0xBC2A68 | garbage ints on failure |
| remove | `sscanf(line,"%f,%f,%f,%s")` → `ms_crToHideItems[n++] = {pos, name[32]}` | `[50]` × 0x2C at 0xBC20D0 (counter shared with `ms_pHiddenEntities[50]` in `HideRequestedObjects` 0x5AFAD0) | name ≥ 32 overwrites the line buffer; unknown model → skipped at runtime |
| peffect | `strtok(line,",")` ×11: `strncpy(name,31)`, `atoi` start, `atoi` end, `atoi` objId, `strncpy(part,31)`, `atof` ×6 (pos, dir) | `[8]` × 0x6C at 0xBC1D70; slot 9 = `ms_crToHideItems` | fewer than 11 fields → `strncpy`/`atoi`/`atof` on NULL → **CRASH** |
| extracol | first such line only (`byte esp+0x13`): `sscanf("%d")`; `v != 0` → `CTimeCycle::StartExtraColour(v-1, false)` 0x55FEC0: weather `= (v-1)/8 + 21`, slot `= (v-1)%8` | – | `v > 16` → weather row ≥ 23, outside the 23 timecyc rows → GARBAGE colours |

After the file: script-appended objects (`ms_cAppendObjectName/AnimName[50]`, `AppendToNextCutscene`
0x4D5DB0) are added to the same 50-entry arrays; `ms_numLoadObjectNames == 0` → postload. Otherwise
(0x5B10AA-0x5B1198) for every entry: `stricmp(name,"csplay")==0` → model 1; `LOAD_THIS` →
`CModelInfo::GetModelInfo(name,&id)` → `RequestModel(id,0x1C)`, else `RequestSpecialModel(300+k, name,
0x1C)` and `while (IsModelLoaded(300+k)) k++` — **no upper bound**: the 21st distinct special model is
loaded over the IDE model 320 and beyond; `USE_PREV` copies the previous id. Then
`LoadAllRequestedModels(1)`, `ms_cutsceneLoadStatus = 1`.

`CStreaming::RequestSpecialModel` 0x409D10 (disassembled): if the model already carries this name and a
stream entry → plain request. Else removes peds/objects using the id, sets `modelinfo->m_nKey =
GetUppercaseKey(name)`, borrows the TXD index from any other model info with the same key, then
`ms_pExtraObjectsDir->FindItem(name, &off, &size)` (0x409F76) — **return value ignored**, `off/size`
uninitialised on a miss — `CClumpModelInfo::Init`, `SetTexDictionary(FindTxdSlot(name) != -1 ? name :
"generic")`, stores `off/size` into the stream entry, `RequestModel`.

### 7.3 `_loading` 0x5B11C0 → `_postload` → objects

Waits until every id in 300..319 listed in `ms_iModelIndex` is loaded (state byte == 1), then: for each
non-repeat entry `CreateCutsceneObject(id)` 0x5B02A0 (for 300..319: `SetColModel(&ms_colModelCutObj[id-300])`,
`UpdateCutsceneObjectBoundingBox(modelinfo->m_pRwObject)` — NULL clump → **CRASH**; `new CCutsceneObject`,
`SetModelIndex`, `ms_pCutsceneObjects[ms_numCutsceneObjs++]`), then `SetCutsceneAnim(anim, obj)` 0x5B0390:
`ms_cutsceneAssociations.GetAnimation(name)` 0x4CE040 → NULL → nothing (object stays static); otherwise
`CopyAnimation` and link to the clump. Particle effects: `objId` is **1-based** (`0 < id ≤ numObjs`),
skinned object → `part` = `atol` bone id → `RpHAnimIDGetIndex` (-1 → matrix at index -1, GARBAGE),
rigid object → `GetFrameFromName(part)`; `FxManager_c::CreateFxSystem(name,…)` NULL for an unknown effect
(skipped in `Update_overlay` 0x5B17FD/0x5B181B). Attachments: `ms_pCutsceneObjects[objA]`,
`[objB]` **0-based, unchecked** → out-of-range index → garbage pointer → **CRASH**.

`CAnimBlendAssocGroup::CreateAssociations(block, animNames, objNames, 32)` 0x4CE3B0:
`CAnimManager::GetAnimationBlock(blockName)` 0x4D3940 (stricmp over `ms_aAnimBlocks`) → **NULL when the
IFP's internal block name ≠ cutscene name → `mov edi,[eax+0x18]` CRASH at 0x4CE40D**. For each animation
of the block: the .cut anim list is scanned (`GetUppercaseKey(animNames[k]) == anim.hash`, k < block
anim count), then `CModelInfo::GetModelInfo(objNames[k])` → `CreateInstance` (vtable+0x2C, clones the
clump: NULL clump → **CRASH**) → `CAnimBlendStaticAssociation::Init`. Animations without a matching .cut
entry, or .cut anims absent from the IFP, are skipped silently.

`SetupCutsceneToStart` 0x5B14D0: for every object with an association, reads
`hierarchy->m_pSequences[0].m_Frames[0]` translation (`+0x14` uncompressed, `+0xA` × 1/1024
compressed) — root sequence without keyframes → NULL deref **CRASH** (IFP-rules), root without
translation → reads past the 16/10-byte keyframe → GARBAGE position; objects without animation are
placed at `ms_cutsceneOffset`.

### 7.4 `.DAT` camera splines — `CCamera::LoadPathSplines` 0x5B24D0 (disassembled)

Frees `m_pPathSplines[4]` (CCamera+0x960), then reads lines with `CFileLoader::LoadLine` from the open
cuts.img handle (**not bounded by the entry size** — it continues into the following IMG entries until
four blocks are done or EOF):

```
expectCount = 1; remaining = 0; block = -1
line[0]=='#' or '\0' -> skip
remaining == 0:
    expectCount: block++ ; block > 3 -> return ; sscanf(line,"%d",&n) ; size = (block<2 ? n*16 : n*40) + 4
                 spline[block] = malloc(size); spline[block][0] = (float)n ; remaining = n ; expectCount = 0
    else line[0]==';' -> expectCount = 1 ; anything else ignored
remaining  > 0: remaining-- ; for every strtok(", \t") token: *p++ = atof(token)      ; NO bound on tokens
```

Blocks 0 and 1 take **4** floats per line (time + 3 values), blocks 2 and 3 take **10** (time + 9);
the `f` suffix on the time is stopped by `atof`. More tokens than the entry size → heap overrun
(**CRASH-likely**); fewer → the following entries shift (GARBAGE); a missing `;` swallows the next
count line and its data; fewer than four blocks → the reader parses the next IMG entry (binary) as text.
Vanilla: all 148 files have exactly 4 blocks with 4/4/10/10 tokens per line.

---------------------------------------------------------------------------------------------------

## 8. Check list (ids for the validator)

Severity: **Crash**, **Error** (visible garbage / heap corruption without immediate fault),
**Warning** (silently ignored / suspicious), **Info**.

### CLO — player.img, clothes.dat, shopping.dat

| id | file | condition | consequence | where (decompile) | how to check |
|---|---|---|---|---|---|
| CLO-01 | player.img | not `VER2`, or > 550 entries | Crash — entries beyond 550 dropped by `ReadDirFile` (printf only); the item is later requested → CLO-03/04 | `CDirectory::Init(0x226)` 0x5A419x/0x5A69ED; `ReadDirFile` 0x5323BA `cmp edi,eax; jge printf` | parse header + count |
| CLO-02 | player.img | entry name ≥ 24 chars (no NUL in field); streaming-size field (bytes 4-5) == 0; streaming size > largest entry of gta3/gta_int/cutscene/script/carrec/modloader IMGs | Crash — key never matches (reads into the next entry) / 0-sector read → clump read fails / streaming buffer overrun | `GetUppercaseKey` 0x53CF30 (reads to NUL); `FindItem` 0x5324D0 returns `*(u16*)(entry+4)`; buffer size 0x5B6170:51 only from streamed IMGs | directory scan; compare with the max computed for `DAT-*` |
| CLO-03 | player.img ↔ clothes.dat/shopping.dat | any model name used as a part is not `<name>.dff` in player.img: defaults `torso head hands legs feet`; clothes.dat `SETC` arg 3 (unless `-`), `CUTS` arg 2; shopping.dat Clothes/Haircuts `modelname` | Crash — `FindItem` miss leaves offset/size uninitialised, model never loads, `BlendGeometry(NULL clump)` | 0x5A55A0 (no return check, `m_pRwObject` unchecked) → 0x5A4940 `RpClumpForAllAtomics` | build the name set from both .dat files + defaults, check keys (case-insensitive) |
| CLO-04 | player.img ↔ clothes.dat/shopping.dat | any texture name is not `<name>.txd`: hard-coded `player_torso player_legs player_face player_feet`; clothes.dat `SETC` arg 4 (unless `-`), `TEX` arg 2; shopping.dat `texturename` (Clothes/Haircuts/Tattoos) | Crash — dummy slot stays empty, `RwTexDictionaryFindNamedTexture(NULL)` / `GetFirstTexture(NULL)` → `CopyTexture` `mov ebx,[eax]` | `RequestTexture` 0x5A4220 (no return check); 0x5A6040; `CopyTexture` 0x5A5735 | same as CLO-03 with `.TXD` |
| CLO-05 | clothes DFF | entry is not exactly three concatenated `CLUMP` (0x10) chunks, or a clump lacks an atomic whose frame name is `normal` / `fat` / `ripped` (stricmp), or has a second atomic | Crash — `GetAtomicWithName` returns 0, `mov eax,[0+0x18]`; extra atomics are merged but ignored (Info) | `LoadClumpFile` 0x5372D0 flag-2 loop; `BlendGeometry` 0x5A4940; `GetAtomicWithName` 0x5A4810 | split the entry on top-level CLUMP headers, read each clump's atomic frame name |
| CLO-06 | clothes DFF | the three geometries differ in vertex count; any of them lacks normals, UV set 0, or a Skin PLG; triangle indices ≥ vertex count | Crash (NULL array) / heap over-read garbage when fat/ripped are smaller | 0x5A4940 loop over `normal->numVertices` reading `+0x14/+0x18/+0x34` and skin arrays of all three | compare counts and flags (`0x10 NORMALS`, `0x04 TEXTURED`) per clump |
| CLO-07 | clothes DFF | no HAnim node table on the atomic's frame or below it; more than 64 nodes; a vertex bone index ≥ node count; first atomic of the *first* clump unskinned | Crash (`StoreBoneArray` `*(hier+4)`) / Error (index into a 64-entry stack table, random bone) / Crash (no hierarchies set → StoreBoneArray NULL) | `SetClump` 0x4C4F70 → 0x4C4EF0 → `GetAnimHierarchyFromFrame` 0x734AB0; 0x5A48B0; 0x5A56E0 `local_40[64]` | per clump: HAnim PLG with `numNodes > 0` on the atomic frame or its subtree; max skin index < numNodes ≤ 64 |
| CLO-08 | clothes DFF set | sum of `normal` vertex counts over the ten worn parts > 65535 | Error — u16 triangle indices / `short` base offset wrap → garbage triangles | 0x5A6530 `sVar11 = (short)iVar12`, `RpGeometryTriangleSetVertexIndices` | sum over any combination is impractical; flag a single part > 6000 vertices (vanilla max 734) |
| CLO-09 | player_torso/legs/face/feet.txd | `player_torso.txd` lacks `torso`, `torso_fat`, `torso_ripped`; `player_legs.txd` lacks `legs`, `legs_fat`, `legs_ripped`; `player_face.txd` / `player_feet.txd` empty; fat/ripped dimensions ≠ base | Crash (NULL texture into `CopyTexture`/`RwRasterLock`) / Error (blend loop over the first source's `w*h` writes 4 B/pixel into a smaller destination) | 0x5A6111-0x5A6178, 0x5A6212-0x5A6258, 0x5A631B-0x5A6372; `BlendTextures` 0x5A59C0/0x5A5BC0/0x5A5820 | TXD name list + dimensions; `face_fat` optional but must match `face` |
| CLO-10 | every player.img TXD | raster not 32 bpp, or DXT/PAL compressed, or (for overlays) first texture dimensions ≠ base: torso-slot TXDs (type 0 / SETC torso / TEX) ≠ 256×256, legs-slot ≠ 128×256, tattoos (types 4..12) ≠ 256×256 (all nine equal) | Error → Crash-likely (heap overrun: `PlaceTextureOnTopOfTexture` loops over the **source** `w*h`, `BlendTextures` over the first source, both 4 B/pixel) | 0x5A57B0, 0x5A59C0, 0x5A5BC0, 0x5A5820; `RwRasterLock` returns the raw (compressed) buffer | per TXD: `depth == 32`, no fourcc/DXT, no palette; role from shopping.dat `type` / clothes.dat comp; head/feet/necklace/watch/glasses/hat/extra1 rasters may be any size |
| CLO-11 | every player.img TXD | zero textures | Crash — `GetFirstTexture` NULL → `CopyTexture` | 0x5A5F70 / 0x5A63xx / 0x5A64E3 | texture count ≥ 1 (only the first is used — extra textures are Info) |
| CLO-12 | clothes.dat | rules outside a `rule` … `end` block; a line inside the block that starts with lowercase `end` (e.g. `endignore`, `endexclusive`) | Warning — the block is closed, every following rule up to the next `rule` line is ignored | 0x5A7B30 `strncmp("rule",line,4)`, `strncmp("end",line,3)` | keyword check: `endignore`/`endexclusive` must be written in a case that does not start with `end` (vanilla: `ENDIGNORE`) |
| CLO-13 | clothes.dat | too few tokens: SETC < 4 args, CUTS/TEX/HIDE < 2, IGNORE/ENDIGNORE/EXCLUSIVE/ENDEXCLUSIVE < 1; unknown keyword | Crash (`GetUppercaseKey(NULL)` / `"-"` compare on NULL) ; unknown keyword → previous rule type reused with these tokens (Error) | 0x5A7B30 `strtok` chain, 0x53CF36 `mov al,[edi]` | token count per keyword |
| CLO-14 | clothes.dat | SETC/HIDE component not exactly one of `torso head hands legs feet necklace watch glasses hat extra1` (lowercase, exact) | Error — treated as `torso` | `GetClothesModelFromName` 0x5A7A20 returns `0` on mismatch | exact string compare |
| CLO-15 | clothes.dat | total rule words (SETC 5, CUTS/TEX/HIDE 3, others 2) > 600 (vanilla 587; hard end of the array at word 606) | Error → Crash-likely (overwrites `ms_lastClothesDesc` 0xBC1C78, then cutscene globals) | `ms_clothesRules[600]` 0xBC1300, `DAT_00bc12fc++` unbounded | count words |
| CLO-16 | clothes.dat | more than 8 IGNORE/EXCLUSIVE tags active at the same point of the file | Warning — the 9th tag is silently not registered | `PreprocessClothesDesc` 0x5A44C0 `local_20[8]` | simulate the tag stack |
| CLO-17 | shopping.dat | Clothes/Haircuts `type` ∉ {0,1,2,3,13,14,15,16,17}; Tattoos type ∉ 4..12 (or `-`); `nametag` > 7 chars; name lengths > 19 (`+".DFF"` must stay ≤ 23 for CLO-02) | Error — `SetTextureAndModel` writes `textures[type]` out of range; GXT key truncated | `LoadPrices` 0x49B8D0 (`strncpy 8`, `atol`), 0x5A8050 | section parse (`section prices` → `Clothes`/`Haircuts`/`Tattoos`) |
| CLO-18 | gta3.img | `player.dff` (skinned ped clump, PED-C rules) or `csplay.dff` (skinned, cutscene skeleton) missing / unskinned; `player.txd` missing | Crash (`GetFirstAtomic`/`RpSkinGeometryGetSkin` NULL at 0x5A6C6A-0x5A6C7D) ; missing player.txd → model 0 gets "generic" and every rebuild **destroys all textures of generic.txd** (Error, whole map loses generic textures) | `RequestSpecialModel` 0x409F83-0x409F9A; 0x5A69D0 `RwTexDictionaryForAllTextures(txd, destroyCB)` | presence + skin check of the two DFFs; presence of `player.txd` |
| CLO-19 | anim.img | IFP blocks `fat` and `muscular` missing | Warning — `GetAnimationBlockIndex` = -1, request of a bogus id; fat/muscular motion groups never load (animgrp.dat rules cover the crash) | `RequestMotionGroupAnims` 0x5A8120 | cross-check with the IFP list |
| CLO-20 | models\generic\player.bmp, skins\*.bmp | file present but not a readable BMP | Info — `RtBMPImageRead` NULL tolerated | `GetSkinTexture` 0x6FFA86 | optional BMP check |

### CUT — anim\cuts.img, models\cutscene.img

| id | file | condition | consequence | where (decompile) | how to check |
|---|---|---|---|---|---|
| CUT-01 | anim\cuts.img | not `VER2`; > 512 entries; entry name ≥ 24 chars; streaming-size field 0 | Warning → the dropped/unfindable `.cut`/`.ifp`/`.dat` is treated as absent (see CUT-03); size 0 → empty buffer parsed as empty file | `Initialise` 0x4D5A20 `CDirectory(0x200)`; `ReadDirFile` 0x532350; `FindItem` 0x5324A0 | directory scan |
| CUT-02 | anim\cuts.img | cutscene base name > 7 chars | Error → Crash-likely — `ms_cutsceneName[8]` overflow into `ms_pCutsceneObjects` when the script loads it | `_overlay` 0x5B13F0 byte-copy loop, `ms_cutsceneName` 0xBC3F0C | name length of every `.cut` |
| CUT-03 | anim\cuts.img | for a `NAME.cut` the matching `NAME.ifp` / `NAME.dat` is missing (or vice versa) | Warning — no `.cut`: no objects, cutscene ends at once; no `.ifp`: objects static; no `.dat`: `HasCutsceneFinished` true immediately | `_preload` 0x5B05A0 FindItem paths; `_postload` 0x5AFBC0; 0x5B0570 | group by base name |
| CUT-04 | .cut | line ≥ 1024 chars; no NUL byte inside the sector-padded entry (text length == size×2048); section header not exactly `info`/`model`/`text`/`uncompress`/`attach`/`remove`/`peffect`/`extracol`/`end` (trailing blanks, commas, tabs) | Crash (stack) / Crash-likely (read past buffer) / Warning (block silently ignored) | line reader 0x5B0830-0x5B087A, `repe cmpsb` chain 0x5B0867-0x5B098B | emulate the reader; flag unknown non-blank lines in section 0 other than the vanilla `motion` block content |
| CUT-05 | .cut info | no `offset X Y Z` line (space-separated, three floats parse) | Crash-likely — uninitialised offset → `LoadScene` at garbage, objects placed at garbage | 0x5B09E4-0x5B0AFC `sscanf "%f %f %f"` result unchecked | require exactly one well-formed offset line |
| CUT-06 | .cut model | line with < 2 tokens (only a number / blank); line with no anim token; total object slots (all anim tokens + script appends) > 50; model or anim name ≥ 32 chars | Crash (`strcpy` from NULL 0x5B0B40) / Warning (line dropped) / Crash (slot 51 overwrites `ms_cutsceneTimer`/`ms_cutsceneName` 0xBC3F08) / Error (spills into next slot) | 0x5B0B04-0x5B0C41; arrays 0xBC38C8/0xBC3288/0xBC31C0 `[50]` | count tokens; vanilla max 18 slots |
| CUT-07 | .cut model ↔ IMGs | model name (not `csplay`, not an IDE name) has no `<name>.dff` in any streamed IMG (gta3/gta_int/cutscene/modloader); more than 20 distinct such special models in one cutscene; total IDE-less DFFs across streamed IMGs > 550 | Crash — `RequestSpecialModel` uses uninitialised offset/size (0x409F76 result ignored), clump NULL → `UpdateCutsceneObjectBoundingBox` / `CreateInstance`; 21st special overwrites IDE model 320+ (Error/Crash); 551st IDE-less DFF dropped from `ms_pExtraObjectsDir` | 0x5B10DE-0x5B114E (`while IsModelLoaded k++` unbounded), 0x409D10, `CreateCutsceneObject` 0x5B02D0, 0x4CE500; `Init2` 0x5B8DDF `CDirectory(0x226)`; 0x5B6170:122 | name resolution against IDE + IMG directories; vanilla max 16 specials, 392 IDE-less DFFs |
| CUT-08 | cutscene.img / .cut model | a special model has no `.txd` of the same base name in any IMG | Warning — TXD slot "generic" → textures missing (white/black) | 0x409F83 `FindTxdSlot(name)` → 0x409F92 `"generic"` | DFF/TXD pairing (vanilla 317/317) |
| CUT-09 | .cut text | format not `start,duration,LABEL` (commas required; `sscanf "%d,%d,%s"`); > 64 lines; label > 7 chars; label ≥ 20 chars | Error (garbage times) / Crash (line 65 overwrites `ms_iModelIndex`) / Error (label overwrites the next 8-byte slot) / Crash (stack) ; label uppercased, must exist in the GXT (Info) | 0x5B0C46-0x5B0CF7; `ms_cTextOutput[64][8]` 0xBC2FC0 | regex `^\s*-?\d+,\s*-?\d+,\S{1,7}$`; vanilla max 58 lines |
| CUT-10 | .cut uncompress | blank / space-only line inside the block; > 8 names; name ≥ 32 chars; name not in the IFP | Crash (`strtok` NULL → 0x4D5AB0 reads address 0) / Error (9th slot = `ms_iTextDuration`) / Error / Warning (no effect) | 0x5B0CFC-0x5B0D17; `ms_aUncompressedCutsceneAnims[8][32]` 0xBC2CC0 | count + IFP cross-check |
| CUT-11 | .cut attach | format not `objA,objB,boneId`; > 50 lines; objA/objB ≥ number of object slots (0-based); boneId not a node of objA's hierarchy | Error / Error / **Crash** (`ms_pCutsceneObjects[idx]` garbage pointer `+0x18` deref) / Error (index -1) | `_loading` 0x5B11C0 attach loop (no range check); `ms_iAttachObjectToBone[50]` 0xBC2A68 | index range vs. CUT-06 slot count; bone id vs. the object's DFF HAnim ids |
| CUT-12 | .cut remove | format not `x,y,z,modelname`; > 50 lines; name ≥ 32 chars; model not in IDE | Error / Error (overflow of `ms_crToHideItems`/`ms_pHiddenEntities`) / Error / Warning (skipped) | 0x5B0D7F-0x5B0E03; `HideRequestedObjects` 0x5AFAD0 | regex + IDE name check |
| CUT-13 | .cut peffect | fewer than 11 comma-separated fields `name,start,end,objId,part,x,y,z,dx,dy,dz`; > 8 lines; effect name not in effects.fxp; objId not in 1..slots; part not a bone id (skinned) / frame name (rigid) of that object | **Crash** (`strncpy`/`atoi`/`atof` on NULL) / Error (9th slot = `ms_crToHideItems`) / Warning (NULL system, effect absent) / Warning (world-space) / Error (matrix -1) | 0x5B0E08-0x5B0FE6; `ms_pParticleEffects[8]` 0xBC1D70; 0x5B11C0 fx loop; 0x5B17FD | field count; name vs `NAME:` entries of effects.fxp (DAT-47/48 parser) |
| CUT-14 | .cut extracol | value < 0 or > 16; more than one line (only the first counts) | Error (timecyc row ≥ 23 read out of bounds → garbage colours) / Info | 0x5B0998-0x5B09DF; `StartExtraColour` 0x55FEC0 `(v-1)/8+21`, `(v-1)&7` | integer range |
| CUT-15 | .ifp in cuts.img | internal block name ≠ file base name (stricmp); an anim referenced in the `model` section absent from the IFP; an IFP anim not referenced; anim name > 23; root (first) sequence without translation keyframes or with 0 frames | **Crash** (`GetAnimationBlock` NULL → `mov edi,[eax+0x18]` 0x4CE40D) / Warning (object static) / Info / Error (per IFP rules) / Error (GARBAGE position, 0x5B14D0 reads `+0x14` / `+0xA` of keyframe 0) or Crash (0 frames) | 0x4CE3B0-0x4CE500; 0x5B0390; 0x5B14D0 | IFP parse (`check_ifp`) + cross-check with CUT-06 anim tokens |
| CUT-16 | .dat in cuts.img | not exactly 4 blocks of `N` / N data lines / `;`; block 0-1 lines with ≠ 4 numbers, block 2-3 lines with ≠ 10; non-numeric count line; missing `;` | Crash-likely (extra tokens overrun the `n*16+4` / `n*40+4` heap block) / Error (shifted values, missing block → the parser continues into the next IMG entry) / Error (uninitialised count) | `LoadPathSplines` 0x5B24D0-0x5B25EB | emulate §7.4 on the entry bytes |
| CUT-17 | models\cutscene.img | any DFF fails the ped-skin (PED-C*) / map-DFF (DFF-*) rules; any TXD fails TXD-*; name ≥ 24 | as the referenced rules; the models load through the ordinary `LoadClumpFile`/`SetClump` path (`RequestSpecialModel` → id 300..319) | 0x40C6B0 → 0x5372D0 → 0x4C4F70 | run the existing DFF/TXD checkers over the IMG |
| CUT-18 | – | script `ADD_CUTSCENE_HEAD` / any head model convention | Info — `AddCutsceneHead` 0x5B0380 is `xor eax,eax; ret` | – | nothing to check |
| CUT-19 | default.ide / gta3.img / player.img | `hier 1, csplay, player` line missing, `csplay.dff` missing or unskinned, `player.txd` missing; a `CUTS` target (`cs_*.dff`) missing in player.img | Crash — `LoadCutsceneData` 0x4D5E80 `RequestModel(1)` then `RebuildCutscenePlayer` → CLO-03/CLO-18 | 0x4D5E80, 0x5A8270, 0x5A69D0 | presence checks |

---------------------------------------------------------------------------------------------------

## 9. Vanilla spot-check (SA 1.0 US, `D:\Grand Theft Auto San Andreas`)

Scripts run in this session (Python, `INU_tools\core\{img,dff,txd,ifp}` readers + `rwtree.py`):

* player.img: VER2, 542 entries (8 free), longest name 20, no zero streaming sizes, no non-zero
  archive sizes, max entry 385 sectors (< 1263 = largest streamed entry). 152 DFF entries: every one
  = 3 CLUMP chunks, one atomic each, frame names {normal,fat,ripped} (case varies: `Ripped`, `rIPPED`,
  `ripped`), identical vertex counts (max 734), normals + UV + Skin PLG present, HAnim table on the
  atomic's frame (32 / 37 / 61 nodes, never > 64), all skin indices < numNodes. 390 TXDs: 388 with one
  texture, 2 base TXDs with three; every raster 32 bpp, format 0x500/0x600, no fourcc, 1 level;
  torso-role 256×256 (73/73), legs-role 128×256 (44/44), tattoos 256×256 (48/48), heads 128×128, feet
  64×64, base TXDs `player_torso` 256×256 ×3, `player_legs` 128×256 ×3, `player_face` 128×128,
  `player_feet` 64×64.
* clothes.dat: 87 SETC, 12 HIDE, 11 IGNORE/ENDIGNORE, 3 EXCLUSIVE/ENDEXCLUSIVE, 20 CUTS = **587 words**
  (limit 600); all model/texture targets exist in player.img; max 5 simultaneous IGNORE tags.
* shopping.dat: 339 Clothes/Haircuts/Tattoos entries, all `.txd`/`.dff` present in player.img, types
  {0,1,2,3,13..17} and {4..12}.
* gta3.img: `player.dff` (skinned, 32 nodes), `player.txd`, `csplay.dff` present; extra-objects
  directory usage 392/550.
* anim\cuts.img: VER2, 444 entries = 148 complete `.cut/.dat/.ifp` triples, longest name 11 (base ≤ 7).
  `.cut`: longest line 63; sections used: info 148, model 148, text 147, uncompress 121, extracol 55,
  remove 36, peffect 22, attach 0; max 18 object slots, 58 text lines, 5 uncompress names, 3 remove,
  3 peffect; labels ≤ 7; extracol ∈ {1,2,3,4,9,11,12,15,16}; max 16 special models per cutscene; every
  special model exists in cutscene.img/gta3.img; every peffect name (`cigarette_smoke`, `explosion_door`,
  `jetpack`, `explosion_tiny`, `vent2`) exists in effects.fxp. `.ifp`: block name == base name for all
  148; every `.cut` anim exists in its IFP (22 IFP anims are unreferenced — Info); all 974 root
  sequences carry translation. `.dat`: all 148 = 4 blocks, 4/4/10/10 tokens per line.
* models\cutscene.img: 634 entries = 317 DFF/TXD pairs, no orphan, longest name 20.

All CLO/CUT rules pass on vanilla (CUT-15 "unreferenced IFP anim" and CLO-05 "extra atomic" are Info).

## 10. Hook points vs. offline checks

In-game (inuhook-style register-preserving stubs): `CClothesBuilder::ConstructGeometryArray`
0x5A55A0 (after `LoadAllRequestedModels`, test `ms_modelInfoPtrs[384+i]->m_pRwObject`),
`BlendGeometry` 0x5A4940 (three `GetAtomicWithName` results, vertex counts, skin/normals/UV pointers),
`ConstructTextures` 0x5A6040 (texture pointers before each `CopyTexture`/`BlendTextures`, raster
`w/h/depth/format`), `CClothes::LoadClothesFile` 0x5A7B30 (`ms_numRuleTags` ≤ 600 at return),
`CCutsceneMgr::LoadCutsceneData_preload` 0x5B05A0 (after parsing: `ms_numLoadObjectNames ≤ 50`,
`ms_numTextOutput ≤ 64`, `ms_numUncompressedCutsceneAnims ≤ 8`, `ms_iNumParticleEffects ≤ 8`,
`ms_iNumHiddenEntities ≤ 50`, special ids < 320), `CAnimBlendAssocGroup::CreateAssociations` 0x4CE3B0
(`GetAnimationBlock` result), `CCamera::LoadPathSplines` 0x5B24D0 (four non-NULL splines at return).

Everything in §8 is decidable from the files: IMG directories (`VER2` header, 32-byte entries), the
concatenated CLUMP chunks of player.img DFFs, TXD raster headers, the two `.dat` text files, the
`.cut` text (reader emulation of §7.2), the `.dat` splines (emulation of §7.4), IFP block/anim names
(existing `check_ifp`), effects.fxp `NAME:` list (existing DAT-47 parser), IDE model names (existing
DAT parser).
