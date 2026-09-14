# GTA SA 1.0 US — MAP / OBJECT DFF loading path (non-skinned atomics + clumps, animated objects)

Source of truth: Ghidra decompiles in `E:\RE\asm_map\<ADDR>.c/.asm` (plus capstone disassembly via
`scratchpad\xdis.py` for the RW internals that were not dumped: `FUN_0074b060` atomic reader,
`_rpMaterialListStreamRead`, `RpGeometryCreate`, `RpGeometryAddMorphTargets`, `_rpMeshRead`,
`_rwFrameListStreamRead`, `_rwStringStreamFindAndRead`, `_rwPluginRegistryReadDataChunks`,
`RwTextureRead`, the UV-anim material read CB, `SetRelatedModelInfoCB`, `GetNameAndDamage`,
`SetAtomicModelInfoFlags`, `CCustomBuildingRenderer::AtomicSetup/IsCBPCPipelineAttached`,
`CEntity::ProcessLightsForEntity`, `CShadows::RenderStaticShadows`). gta-reversed / plugin-sdk
were used for names only. NOTE: `D:\...\gta_sa.exe` is the HOODLUM build — a handful of first
instructions are obfuscated trampolines into 0x401xxx-0x406xxx (e.g. `GetFrameNodeName`,
`IsCBPCPipelineAttached`, `StoreStaticShadow`); each was followed by hand and decoded below.

Legend: **CRASH** = guaranteed fault, **CRASH-likely** = garbage pointer / heap or stack
overwrite that faults later, **GARBAGE** = wrong rendering / behaviour, no fault,
**LOAD-FAIL** = `RpClumpStreamRead` returns NULL → `ConvertBufferToObject` does
`RemoveModel`+`RequestModel` again → the streamer retries the model forever, it never appears,
**IGNORED** = data silently dropped. Check ids are `DFF-nn`.

---------------------------------------------------------------------------------------------------

## 0. Call path (what the engine does per model type)

`CStreaming::ConvertBufferToObject` 0x40C6B0 `(int* buf, int modelId)` — ids < 20000 are models:

1. `mi = ms_modelInfoPtrs[id]` (0xA9B0C8). TXD slot `mi+0xA` must already be loaded
   (`CTxdStore::ms_pTxdPool` entry `+0 != 0`), and the anim block (vfunc +0x38) if any; otherwise
   `RemoveModel(id); RequestModel(id, flags)` and return 0 (retry later). `CTxdStore::SetCurrentTxd`
   → every `RwTextureRead` during the load searches that TXD (and its parent chain).
2. `mi->GetRwModelType()` (vfunc +0x24) == 1 (atomic: `objs`, `tobj`, damage, LOD) →
   - `RwStreamReadChunkHeaderInfo`: if the FIRST chunk of the file is 0x2B (UV-anim dictionary)
     → `RtDictSchemaStreamReadDict(&UVAnim schema 0x8DED50)` + `RtDictSchemaSetCurrentDict`.
     The stream is then closed and re-opened from the buffer.
   - `CFileLoader::LoadAtomicFile` 0x5371F0 `(stream, id)`: `RwStreamFindChunk(0x10 CLUMP)` →
     `RpClumpStreamRead` → `DAT_00B71840 = id` → `RpClumpForAllAtomics(clump, SetRelatedModelInfoCB 0x537150)`
     → `RpClumpDestroy(clump)`; returns 0 if `mi->m_pRwObject (+0x1C)` is still NULL.
   - `RtDictDestroy(dict)` (materials keep refs on the anims they picked).
   - else (type 2 = clump: `anim` section objects, weapons, vehicles, peds) → `CFileLoader::LoadClumpFile`
     0x5372D0 `(stream, id)`: `RpClumpStreamRead` → `mi->SetClump(clump)` (vfunc +0x40 =
     `CClumpModelInfo::SetClump` 0x4C4F70). If model-info flag `+0x13 & 2` (multi-clump cutscene
     object) it reads EVERY CLUMP chunk in the file and merges frames+atomics (0x537290 clones).
     **No UV-anim dictionary is read for clump models** (see DFF-52).
3. after success, for non vehicle/ped: `mi->m_nAlpha (+0xC) = (streamFlags & 0x24) ? 0xFF : 0`
   (fade-in), `CStreamingInfo::AddToList`, load state = 1.

`SetRelatedModelInfoCB` 0x537150 (per atomic, iteration order = **reverse file order**, because
`RpClumpStreamRead` inserts each atomic at the list head — 0x74B4xx `*puVar3 = old first; list.first = new`):
```
mi = ms_modelInfoPtrs[DAT_00B71840]->AsAtomicModelInfoPtr()   ; vfunc +4
GetNameAndDamage(GetFrameNodeName(atomic->frame), outName, &isDamaged)   ; 0x5370A0
CVisibilityPlugins::SetAtomicRenderCallback(atomic, NULL)
if isDamaged: mi->AsDamageAtomicModelInfoPtr()->SetDamagedAtomic(atomic)  ; vfunc +8, then 0x4C48D0
else        : mi->SetAtomic(atomic)                                        ; vfunc +0x3C = 0x4C4360
RpClumpRemoveAtomic(clump, atomic); RpAtomicSetFrame(atomic, RwFrameCreate())   ; NEW identity frame
CVisibilityPlugins::SetModelInfoIndex(atomic, id)
```
`GetNameAndDamage` 0x5370A0: name ends with `_dam` → damaged=1, suffix stripped; name ends with
`_l0`/`_L0` → suffix stripped; else copied verbatim.

`CAtomicModelInfo::SetAtomic` 0x4C4360 (thiscall, RET 4): subtracts the old geometry's 2dfx count
from `m_n2dfxCount (+0xD, BYTE)`, stores the atomic in `+0x1C`, adds the new geometry's 2dfx count
(`*(*(DAT_00C3A1E0 + geometry))`), `AddTexDictionaryRef`, `CAnimManager::AddAnimBlockRef` if the
model has an anim file, then pipeline setup:
`if CCustomBuildingRenderer::IsCBPCPipelineAttached(atomic) → CCustomBuildingRenderer::AtomicSetup(atomic)`
`else if CCarFXRenderer::IsCCPCPipelineAttached → SetCustomFXAtomicRenderPipelinesVMICB`;
if model special type == TAG (`flags & 0x7800 == 0x3000` and `!(flags & 0x8000)`) →
`CTagManager::SetupAtomic`; finally `flags |= 1` (bHasBeenPreRendered).

`CClumpModelInfo::SetClump` 0x4C4F70: stores the clump, adds the 2dfx count of
`Get2DEffectAtomic(clump)` (the FIRST atomic in list order = **last in the file** that has a
non-empty 2dfx list — effects on any other atomic are ignored, DFF-37), `CVisibilityPlugins::
SetClumpModelInfo`, `AddTexDictionaryRef`, anim block ref, `RpClumpForAllAtomics(AtomicSetupLightingCB
0x4C4F30)` (same pipeline choice per atomic), skin handling if `GetFirstAtomic` has a skin.

Pipeline choice (`CCustomBuildingRenderer::IsCBPCPipelineAttached` 0x5D7F40, decoded from the
HOODLUM trampoline at 0x401B27): true if the atomic's pipeline-set plugin value
(`GetPipelineID` 0x72FC40 = atomic + `DAT_008D6080`) is **0x53F2009C** (building) or
**0x53F20098** (building DN), or if the geometry has an extra-vertex-colour block AND `preLitLum != NULL`.
`CCustomBuildingRenderer::AtomicSetup` 0x5D7F00: `if GetExtraVertColourPtr(geom) && geom->preLitLum`
→ `CCustomBuildingDNPipeline::CustomPipeAtomicSetup` 0x5D71C0 (materials setup, `RpD3D9GeometrySetUsageFlags(geom, 8)`
= dynamic VB, `atomic+0x6C = DAT_00C02C1C` (DN pipeline), `SetPipelineID(atomic, 0x53F20098)`)
else → `CCustomBuildingPipeline::CustomPipeAtomicSetup` 0x5D7E50 (`atomic+0x6C = DAT_00C02C68`,
`SetPipelineID(0x53F2009C)`). `CCarFXRenderer::IsCCPCPipelineAttached` 0x5D5B80 = id == 0x53F2009A.
An atomic with none of these ids and no night colours keeps the stock RW pipeline; then
`CEntity::CreateRwObject` 0x533D30 sets entity flag 0x10000000 (`IsCBPCPipelineAttached(first atomic) == 0`)
and `CEntity::SetupLighting` 0x553DC0 uses RW dynamic lighting (`ActivateDirectional` +
`CPointLights::GenerateLightsAffectingObject`) for it instead of the timecycle building shader.

`CEntity::CreateRwObject` 0x533D30: `mi->CreateInstance()` (vfunc +0x2C, clone of the model atomic /
clump; with `CDamageAtomicModelInfo::ms_bCreateDamagedVersion` when entity flag 0x200), matrix
update, `CCustomBuildingDNPipeline::PreRenderUpdate(atomic, true)` for atomics, moving-list/anim
setup for animated clumps (`mi+0x13 & 1`), `CStreaming::AddEntity`, `CreateEffects()`, then
`GetFirstAtomic(clump)` for clump objects → `IsCBPCPipelineAttached` (see DFF-40).

---------------------------------------------------------------------------------------------------

## 1. Layouts as read from the stream

All readers accept chunk versions **0x34000..0x36003** only (RW 3.4.0.0 – 3.6.0.3; library id
0x1003FFFF..0x1803FFFF; SA writes 0x1803FFFF). Checked in `RpClumpStreamRead` 0x74B4xx,
`FUN_0074b060`, `RpGeometryStreamRead` 0x74D1xx, `_rpMaterialListStreamRead` 0x74E62C,
`RpMaterialStreamRead` 0x74DDxx, `RwTextureStreamRead` 0x8046xx, `_rwFrameListStreamRead` 0x8079A0,
`_rwPluginRegistryReadDataChunks` 0x8089AB, `_rwStringStreamFindAndRead` 0x80A547.
GTA III (0x31000/0x32000) and VC (0x0C02FFFF = 0x33002) files are rejected; RW 3.7 (0x37xxx) rejected.

### 1.1 CLUMP (0x10)
```
STRUCT(1)  : int32 numAtomics, int32 numLights, int32 numCameras   (always 12 bytes read: RwStreamRead(...,0xC))
FRAMELIST(0xE): STRUCT: int32 numFrames; numFrames × 0x38 { RwV3d right, up, at, pos; int32 parentIndex; int32 matrixFlags }
               then numFrames × EXTENSION(3) (frame plugins: NodeName 0x253F2FE 24 B ...)
GEOMETRYLIST(0x1A): STRUCT: int32 numGeometries; numGeometries × GEOMETRY(0xF)
numAtomics × ATOMIC(0x14)
numLights  × { STRUCT int32 frameIndex; LIGHT(0x12) }        (not used by map DFFs)
numCameras × { STRUCT int32 frameIndex; CAMERA(5) }
EXTENSION(3) of the clump (collision plugin for vehicles only)
```
Evidence: 0x74B420 decompile — `RwStreamRead(&local_c,0xC)`, `RwStreamFindChunk(0xe)`,
`_rwFrameListStreamRead`, `RwStreamFindChunk(0x1a)`, loop `FUN_0074b060` × `local_c`, lights loop ×
`iStack_8`, cameras × `iStack_4`, `_rwPluginRegistryReadDataChunks(&DAT_008d6264, stream, clump)`.
Frames: `_rwFrameListStreamRead` 0x807970 reads 0x38 per frame, `RwFrameCreate`, copies the 4×3 matrix,
`_rwMatrixNormalError/_rwMatrixOrthogonalError/_rwMatrixDeterminant` decide the ORTHONORMAL bits
(non-orthonormal → treated as a general matrix, no error), `if parentIndex >= 0: RwFrameAddChild(frames[parentIndex], frame)`
**without any bounds check** (DFF-06). Frame extensions are read AFTER all frames.

### 1.2 ATOMIC (0x14) — `FUN_0074b060(stream, frameList*, geomList*)`
```
STRUCT(1): int32 frameIndex, int32 geometryIndex, int32 flags, int32 unused
```
Read with `RwStreamRead(stream, &local16, chunkLength)` into a **16-byte stack buffer** (zeroed; a
12-byte RW 3.4 struct is fine) — chunkLength > 16 overwrites the return address (DFF-04).
`atomic+2 = (uint8)flags` (rpATOMICCOLLISIONTEST 1 / rpATOMICRENDER 4).
If `frameList->numFrames != 0`: `_rwObjectHasFrameSetFrame(atomic, frames[frameIndex])` — no bounds
check (DFF-05). If `geomList->numGeometries != 0`: `geoms[geometryIndex]` — no bounds check (DFF-07);
else an inline GEOMETRY chunk must follow (`RwStreamFindChunk(0xF)` + `RpGeometryStreamRead`).
The atomic bounding sphere `atomic+0x1C..0x2B` is copied from `geometry->morphTarget[0]+4` (sphere).
Then `EXTENSION(3)` via `_rwPluginRegistryReadDataChunks(&DAT_008d624c)`: atomic plugins that matter:
`0x253F2F3` "Pipeline Set" (`PipelineStreamRead` 0x72FB90: `RwStreamRead(stream, atomic+DAT_008D6080, chunkLength)`
— length must be 4, DFF-08), `0x120` MatFX atomic flag (int; `RpMatFXAtomicQueryEffects` — gates
UV-anim updates, DFF-53), `0x116` skin (peds only), `0x1F` right-to-render (only the skin plugin
registers a rights callback, `RpAtomicSetStreamRightsCallBack` caller 0x7C688D — map DFFs carry the
pipeline in 0x253F2F3, not in a right-to-render chunk).

### 1.3 GEOMETRY (0xF) — `RpGeometryStreamRead` 0x74D190
```
STRUCT(1): uint32 flags, int32 numTriangles, int32 numVertices, int32 numMorphTargets   (16 B; +12 B surface props if version < 0x34001)
  flags low byte = format: 1 TRISTRIP, 2 POSITIONS, 4 TEXTURED, 8 PRELIT, 0x10 NORMALS, 0x20 LIGHT, 0x40 MODULATEMATERIALCOLOR, 0x80 TEXTURED2
  flags bits 16..23 = numTexCoordSets (if 0: 2 when TEXTURED2 else 1 when TEXTURED else 0); bit 24 = NATIVE (0x01000000)
if !(flags & 0x01000000) && numVertices != 0:
    if flags & 8:      numVertices × RwRGBA   prelit          (RwStreamRead, must be exact)
    numTexCoordSets ×  numVertices × {float u, v}
    if numTriangles:   numTriangles × {uint16 v1?, v0?, matId, v2} → after in-place word swaps memory = {v0,v1,v2,matId}
                       (file order is (vertex2, vertex1, material, vertex3) as in vanilla)
numMorphTargets (as created, ≥1) × { RwSphere sphere(16 B); int32 hasVertices; int32 hasNormals;
                       [numVertices × RwV3d vertices] [numVertices × RwV3d normals] }
MATERIALLIST(8): STRUCT: int32 numMaterials; numMaterials × int32 matIndex (-1 = new material follows, ≥0 = reuse
                       materials[matIndex] of THIS list, unchecked); then one MATERIAL(7) per -1 entry
EXTENSION(3): BinMesh 0x50E, NativeData 0x510, 2dfx 0x253F2F8, NightColours 0x253F2F9, Breakable 0x253F2FD, Skin 0x116
```
`RpGeometryCreate(numVertices, numTriangles, flags)` 0x74CA90: **fails (NULL → LOAD-FAIL) if numVertices < 0 or ≥ 0x10000 or numTriangles < 0**
(`cmp eax,0x10000; jge`). Allocates one block: prelit (if flags&8), texcoords × numTexCoordSets,
triangles (matId pre-set to 0xFFFF); for NATIVE nothing (pointers stay NULL). Then
`RpGeometryAddMorphTargets(1)` 0x74C310: vertices/normals arrays only when !NATIVE and numVertices != 0
(normals only when flags & 0x10). The morph-target table is `+0x5C`, 0x1C per target
{geometry*, sphere(16), verts*, normals*}. `numTexCoordSets` is written through `esi+0x34+i*4` for
i < count with no upper bound (DFF-11).
Material list (`_rpMaterialListStreamRead` 0x74E600): `numMaterials == 0` → empty list returned OK
(DFF-14). Each `MATERIAL(7)`: `RpMaterialStreamRead` 0x74DD30 reads the STRUCT of `chunkLength` bytes
into a **28-byte stack buffer** (DFF-15): `{int32 flags, RwRGBA color, int32 unused, int32 textured, float ambient, specular, diffuse}`
→ material `+0 texture, +4 color, +8 pipeline, +0xC..0x14 surfaceProps, +0x18 refCount(int16)`; if
`textured != 0` a TEXTURE(6) chunk is read by `RwTextureStreamRead` (result stored WITHOUT a NULL
check, 0x74DED5: `mov [esi],eax`) then `EXTENSION(3)` (MatFX 0x120, UVAnim 0x135, ...).
TEXTURE(6): STRUCT {uint16 filterFlags, uint8 addressUV, uint8 pad} — read with `RwStreamRead(&local, chunkLength)`
into a 4-byte local followed by 0x10C bytes of other locals (DFF-17); then two STRING(2) chunks
(name, mask): `_rwStringStreamFindAndRead` copies the whole chunk (in 64-byte pieces) into a
**128-byte** stack buffer with no length check (DFF-18); `RwTextureRead(name, mask)` 0x7F3AC0 → find
callback (case-insensitive compare `rwstricmp`, 0x7F3A17-0x7F3A3F, against the current TXD then its
parents) → refcount++ ; not found → read callback → NULL → **material->texture = NULL** (DFF-19).

### 1.4 BinMesh plugin 0x50E — `_rpMeshRead` 0x758EC0 (via read CB 0x750DE0)
```
int32 flags (0 = tri list, 1 = tri strip), int32 numMeshes, int32 totalIndices
numMeshes × { int32 numIndices, int32 materialIndex, numIndices × int32 index }   (indices stored as uint16)
```
Allocation = header 0x10 + numMeshes×12 + totalIndices×2 (non-native). `material = _rpMaterialListGetMaterial(list, materialIndex)`
0x74E2B0 = `list->materials[materialIndex]` **unchecked** (DFF-21). Indices are copied 256 at a time
(`RwStreamReadInt32`), truncated to 16 bits, appended sequentially — the sum of `numIndices` is never
compared with `totalIndices` (DFF-22). No index is compared with `numVertices` (DFF-23).
Without a BinMesh chunk `RpGeometryUnlock` 0x74C800 builds the meshes from the triangle array
(`_rpBuildMeshCreate`, per triangle `_rpMaterialListGetMaterial(list, tri.matId)` then `mov ecx,[eax]`
at 0x74C8C0 = **dereference of the looked-up pointer**, DFF-20/DFF-24).

### 1.5 2dfx plugin 0x253F2F8 — `Rwt2dEffectPluginDataChunkReadCallBack` 0x6F9FD0
```
uint32 count
count × { RwV3d pos (12); uint32 type (only the low byte kept); uint32 dataSize; data[dataSize] }
```
`CMemoryMgr::Malloc(count*0x40+4)` → `[int32 count][C2dEffect × count]`, pointer stored at
`geometry + DAT_00C3A1E0`. Per-type acceptance (a wrong size → `RwStreamSkip(dataSize)`, the slot is
dropped and the stored count decremented; the two exceptions below do NOT skip):

| type | name | accepted dataSize | fields (file offset → C2dEffect offset) |
|---|---|---|---|
| 0 | light | **0x50** or **0x4C** | 0 RGBA colour→+0x10; 4 coronaFarClip→+0x14; 8 pointlightRange→+0x18; 0xC coronaSize→+0x1C; 0x10 shadowSize→+0x20; 0x14 coronaShowMode(flash type)→+0x26; 0x15 coronaEnableReflection→+0x27; 0x16 coronaFlareType→+0x28; 0x17 shadowColorMultiplier→+0x29; 0x18 flags1→low byte of uint16 +0x24; 0x19 coronaTexName[24]; 0x31 shadowTexName[24]; 0x49 shadowZDistance→+0x2A; 0x4A flags2→high byte of +0x24; 0x4B/0x4C/0x4D lookDirection x,y,z→+0x2B/+0x2C/+0x2D (0x4C variant: 0,0,100); +0x30 coronaTex*, +0x34 shadowTex* = `RwTextureRead(name, NULL)` in the **"particle"** TXD (`CTxdStore::FindTxdSlot("particle")`, pushed/popped around) — NULL if missing |
| 1 | particle | **0x18** | fx system name[24] → +0x10 (copied with an unbounded strcpy loop until NUL) |
| 3 | ped attractor | **0x38** | 0 type(byte used)→+0x34; 4 queueDir→+0x10; 0x10 useDir→+0x1C; 0x1C forwardDir→+0x28; 0x28 externalScript[8]→+0x38; 0x30 pedExistingProbability→+0x35; 0x34 flags→+0x36; 0x36 →+0x37 |
| 4 | sun glare | **nothing is read and nothing is skipped** | — (DFF-31) |
| 6 | enter/exit | **0x2C** or **0x28** | 0 enterAngle→+0x10; 4 radiusX→+0x14; 8 radiusY→+0x18; 0xC exitPos→+0x1C..; 0x18 exitAngle→+0x28; 0x1C interiorId(int16)→+0x2C; 0x1E flags1→+0x2E; 0x1F skyColor→+0x2F; 0x20 interiorName[8]→+0x34 (strncpy 8); 0x28 timeOn→+0x3C; 0x29 timeOff→+0x3D; 0x2A flags2→+0x3E (0x28 variant: timeOn=0, timeOff=24, **flags2 = uninitialised stack byte**) |
| 7 | roadsign | **0x58** | 0 size x,y→+0x10/+0x14; 8 rotation x,y,z→+0x18..; 0x14 flags(uint16: bits0-1 numLines(0=4,1..3), bits2-3 lettersPerLine(0=16,1=2,2=4,3=8), bits4-5 palette)→+0x24; 0x18 text[64] → `CMemoryMgr::Malloc(0x40)` copy, pointer +0x28; atomic* +0x2C = NULL |
| 8 | trigger point / slot machine | **4** | int32 id→+0x10 |
| 9 | cover point | **0xC** | dirX,dirY→+0x10/+0x14; usage byte→+0x18 |
| 10 | escalator | **0x28** | bottom→+0x10, top→+0x1C, end→+0x28, direction byte→+0x34 |
| 2, 5, ≥11 | unknown | **nothing is read and nothing is skipped** | — (DFF-32) |

C2dEffect in memory = 0x40 bytes: `+0 pos, +0xC type (uint8), +0x10.. union`. The model-info total
`m_n2dfxCount` is a **BYTE** (`+0xD`; `SetAtomic`: `add dl, al`), so > 255 effects wrap (DFF-33).
`CModelInfo::Get2dEffectStore` 0x4C5A60 returns 0xB4C2D8 = `CStore<C2dEffect, 100>` — this is only
the store for IDE `2dfx`-section effects (`CFileLoader::Load2dEffect` 0x5B7670 / `Add2dEffect`
0x4C4D20); DFF effects live in the per-geometry malloc block and are NOT limited by it.
`CBaseModelInfo::Get2dEffect` 0x4C4C70: `i >= m_n2dfxCount - geomCount ? geometryEffects[i - (m_n2dfxCount-geomCount)] : store[m_n2dEffectIndex + i]`.
Scripted 2dfx (`CScripted2dEffects`) = 64 slots, unrelated to DFFs.

### 1.6 Night colours plugin 0x253F2F9 — `pluginExtraVertColourStreamReadCB` 0x5D6DE0
```
uint32 hasColours; if != 0: numVertices × RwRGBA nightColour   (numVertices = geometry->numVertices, NOT the chunk length)
```
Plugin block (12 B at `geometry + DAT_008D12BC`): `+0 nightColours*`, `+4 dayColours*` (malloc'd copy of
`geometry->preLitLum` if it is non-NULL — otherwise left **uninitialised**), `+8 float dnBalance = 1.0`.
`NVCPipelineProcess` 0x5D6850 (from `CCustomBuildingDNPipeline::PreRenderUpdate` → `FUN_005d7211`, only
when `atomic+0x6C == DAT_00C02C1C` i.e. the DN pipeline is attached and the balance changed):
`RpGeometryLock(geom, 8)`, per vertex `prelit[i] = day[i]*(1-b) + night[i]*b` (4 channels incl. alpha,
writes into `geometry->preLitLum`), `RpGeometryUnlock` (re-instances the dynamic VB).

### 1.7 UV animation — dictionary 0x2B + material plugin 0x135
Dictionary (`RtDictSchemaStreamReadDict`, first chunk in the file, atomic models only): each entry is an
`RtAnimAnimation` (interpolator type 0x1C1 = rpUVANIMLINEARKEYFRAMETYPEID, 0x1C0 = param) with custom data
`_rpUVAnimCustomDataStreamRead` 0x7CBF70: `char name[32]` + `int32 nodeToUVChannelMap[8]`, refCount = 1
(struct 0x44 at customData: `+0x20` map, `+0x40` refCount).
Material plugin (0x28 B at `material + DAT_00C9B8D0`): `+0 uvMatrix[0]*`, `+4 uvMatrix[1]*`, `+8 RtAnimInterpolator*[8]`.
Material read CB 0x7CBC20: `STRUCT: uint32 slotMask; for bit i in 0..7 set: char animName[32]` →
`RtDictSchemaGetCurrentDict` → `RtDictFindNamedEntry(dict, name)`: found → refCount++; **not found (or no
dictionary)** → `FUN_007cbe00(name)` creates a 2-keyframe identity animation with that name (DFF-51);
interpolator created and set; `FUN_007cbd80` creates the UV matrices for channels 0/1 referenced by any
slot's `nodeToUVChannelMap`. Slots 8..31 of the mask are ignored (DFF-54).
Runtime: `CEntity::PreRender` 0x535FA0 → only if `mi->AsAtomicModelInfoPtr()` (atomic models) and
`RpMatFXAtomicQueryEffects(atomic) != 0` (0x535FEF) → `RpGeometryForAllMaterials(MaterialUpdateUVAnimCB 0x532D70)`
→ `RpMaterialUVAnimExists` → `RpMaterialUVAnimAddAnimTime(CTimer::ms_fTimeStep * k)` + `RpMaterialUVAnimApplyUpdate`.

### 1.8 Breakable plugin 0x253F2FD — `BreakableStreamRead` 0x59CEC0
```
uint32 magic (0 = no data);  if != 0:
0x34-byte header BreakInfo_t { int32 posRule; uint16 numVertices(+4); pad; 3 ptrs; uint16 numTriangles(+0x14); pad; 2 ptrs; uint16 numMaterials(+0x20); pad; 4 ptrs }
numVertices × RwV3d, numVertices × {u,v}, numVertices × RwRGBA, numTriangles × {u16 v0,v1,v2}, numTriangles × u16 matIndex,
numMaterials × char texName[32], numMaterials × char maskName[32], numMaterials × {float r,g,b}
```
One `operator new` block (header + all arrays + numMaterials×4 texture pointers), pointer at
`geometry + DAT_00BB4238`. Each texture: `RwTextureRead(texName, maskName or NULL)` in the current TXD
→ NULL when missing (no failure). Nothing is validated (DFF-60). `CGlass::IsObjectGlass` 0x46A760 does
NOT look at this plugin: it is `entity is CObject && (atomicModelInfo->flags & 0x7800) in {0x2000, 0x2800}`
(IDE flags 0x200 / 0x400 → glass type 1 / 2, `SetAtomicModelInfoFlags` 0x5B3B42-0x5B3B72).

### 1.9 IDE lines → model-info (what flags do)
`CFileLoader::LoadObject` 0x5B3C60 (`objs`): `%d %s %s %f %d` (or the old `%d %s %s %d(count) ...` forms);
model/txd names `%s` into **24-byte stack buffers** (DFF-70). `flags & 0x1000` → `AddDamageAtomicModel`
else `AddAtomicModel`; `m_fDrawDistance (+0x18)`, `m_nKey = GetUppercaseKey(name)`, `SetTexDictionary`,
`SetAtomicModelInfoFlags(mi, flags)` 0x5B3B20:
```
SetBaseModelInfoFlags 0x5B3AD0: 0xC → +0x12 bit 2 (drawLast), 8 → bit 4 (additive), 0x40 → bit 8 (no Z write),
  0x80 → bit 0x10 (no shadows cast on it), !0x200000 → bit 0x40 (backface culled)
0x1 → +0x13 bit 0 (0x100 road), 0x200 → special type GLASS_1 (0x2000), 0x400 → GLASS_2 (0x2800), 0x800 → GARAGE_DOOR (0x3800),
0x2000 → TREE (0x800), 0x4000 → PALM (0x1000), 0x8000 → +0x13 bit 2 (0x400 don't collide with flyers),
0x100000 → TAG (0x3000), 0x400000 → CRANE (0x5800), 0x80000 → BREAKABLE_STATUE (0x5000)     (type = flags & 0x7800)
```
`LoadTimeObject` 0x5B3DE0 (`tobj`): same + `timeOn/timeOff` bytes at `+0x20/+0x21`, `CTimeModelInfo::FindOtherTimeModel`
0x4C47E0 swaps `_nt`↔`_dy` in the name (24-byte stack copy) and scans all 20000 model infos for the
partner (`+0x22 = partner id`); no partner → stays -1 (fine). `LoadClumpObject` 0x5B4040 / `LoadAnimatedClumpObject`
0x5B40C0 (`anim`: `%d %s %s %s(animfile) %f %d`): `AddClumpModel`, animfile != "null" → `+0x13 |= 1`,
flags 0x20 → `+0x13 |= 4`. `CEntity::SetupBigBuilding` 0x533150: entity flags `&~1 | 0x10100`, model
`+0x12 |= 0x20` (LOD). Model-info stores (`AddXxxModel` 0x4C6620..0x4C67A0 — **no capacity check**):
atomic 14000 × 0x20 @0xAAE954, damage 70 × 0x24 @0xB1BF5C, time 169 × 0x24 @0xB1C964, lod-time 1 × 0x28,
clump 92 × 0x24 @0xB1E95C, vehicle 212 × 0x308 @0xB1F654, ped 400 × 0x44 @0xB478FC; `ms_modelInfoPtrs` 20000.

### 1.10 In-memory structures used above
| struct | offsets (evidence) |
|---|---|
| RpClump | +4 frame, +8 atomic list head, +0x10 lights, +0x18 cameras (0x74B420: `*(p1+4)=frames[0]`, list links at +8/+0x10/+0x18) |
| RpAtomic | +2 flags byte, +4 frame, +0x14 repEntry, +0x18 geometry, +0x1C..0x2B bounding sphere, +0x3C clump, +0x40/+0x44 list link, +0x6C pipeline (0x74B0F2, 0x74B127, 0x74B159, 0x74B3xx; 0x5D71D8 `[atomic+0x6c]=pipe`) |
| RpGeometry | +8 flags, +0xE refCount(int16), +0x10 numTriangles, +0x14 numVertices, +0x18 numMorphTargets, +0x1C numTexCoordSets, +0x20 matList {materials*, space, numMaterials}, +0x2C triangles, +0x30 preLitLum, +0x34..0x50 texCoords[8], +0x54 mesh, +0x58 repEntry, +0x5C morphTargets (0x74CB64-0x74CBC3, 0x74D190) |
| RpMaterial | +0 texture, +4 RGBA color, +8 pipeline, +0xC ambient, +0x10 specular, +0x14 diffuse, +0x18 refCount(int16) (0x74DD30 `p1[0..6]`) |
| RwTexture | +0x50 filter/addressing bits, +0x54 refCount, name at +0x10 (0x8046E0 `puVar4+0x54`, 0x7F3A0C) |
| C2dEffect | 0x40 B, +0 pos, +0xC type, +0x10 data (1.5) |
| t2dEffectPlugin | int32* → [count][C2dEffect×count] at geometry+DAT_00C3A1E0 (0x6FA5xx `*puStack_160=count`) |
| ExtraVertColour plugin | 12 B: night*, day*, float balance at geometry+DAT_008D12BC (0x5D6DE0) |
| Breakable plugin | BreakInfo_t* at geometry+DAT_00BB4238 (0x59CEC0) |
| UVAnim material plugin | 0x28 B at material+DAT_00C9B8D0 (0x7CBC39, 0x7CC3B0) |
| Pipeline-set atomic plugin | int32 pipeline id at atomic+DAT_008D6080 (0x72FC40) |
| NodeName frame plugin | 24 B at frame+DAT_00C87C5C (0x72FA5A) |
| CBaseModelInfo | +0 vtable, +4 nameKey, +8 refCount(u16), +0xA txdIndex, +0xC alpha, +0xD n2dfxCount(u8), +0xE 2dEffectIndex, +0x10 objectInfoIndex, +0x12 flags(u16), +0x14 colModel, +0x18 drawDistance, +0x1C rwObject; CAtomicModelInfo 0x20; CDamageAtomicModelInfo +0x20 damagedAtomic; CTimeModelInfo +0x20 timeOn,+0x21 timeOff,+0x22 otherModel; CClumpModelInfo +0x20 animFileIndex |
| CStreamingInfo | 0x14 B at 0x8E4CC0 + id×0x14: +6 flags, +0xC cdSize (blocks), +0x10 loadState (1 = loaded) (0x40C6B0 `DAT_008e4cc6/ccc/cd0`) |

---------------------------------------------------------------------------------------------------

## 2. Crash / garbage conditions (every claim cites the code)

### Stream / structure level (before any model-info code runs)
- **DFF-01** RW version outside 0x34000..0x36003 (III/VC/RW3.7 files, or a garbage library id) →
  `RwErrorSet(0x80000004)` and NULL at every reader (0x74B4xx, 0x74B091, 0x74D1xx, 0x74DDxx, 0x8079A5,
  0x8089B0, 0x80A54C) → **LOAD-FAIL** (streamer retries forever, model invisible).
- **DFF-02** Any object (clump, frame, atomic, geometry, material, texture) without an EXTENSION(3)
  chunk, or with an extension whose child chunk lengths do not sum to the extension length:
  `_rwPluginRegistryReadDataChunks` 0x808980 requires `RwStreamFindChunk(3)` (skips everything until it
  finds one) and loops `remaining -= childLen + 12` with a `jne` (0x808A32) — negative remainders keep
  reading the following chunks as plugin data → **LOAD-FAIL** (typically) or a following atomic/geometry
  swallowed silently.
- **DFF-03** CLUMP struct: `numAtomics` larger than the number of ATOMIC chunks → `RwStreamFindChunk(0x14)`
  fails → geometries destroyed → **LOAD-FAIL** (0x74B9xx). `numAtomics == 0` with an atomic-type model
  → no `SetAtomic` → `mi+0x1C == NULL` → `LoadAtomicFile` returns 0 → **LOAD-FAIL**. Geometry-list count
  larger than the GEOMETRY chunks present → **LOAD-FAIL**; `numGeometries == 0` forces every atomic
  to carry an inline geometry.
- **DFF-04** ATOMIC struct chunk length > 16 → `RwStreamRead` into the 16-byte local at `[esp+0x20]`
  (0x74B0B4-0x74B0BF) overwrites the return address → **CRASH** on return from `FUN_0074b060`.
- **DFF-05** ATOMIC `frameIndex >= numFrames` (frame list non-empty) → `frames[frameIndex]` read past the
  malloc'd pointer array (0x74B0FA-0x74B100) → `_rwObjectHasFrameSetFrame` links the atomic into a garbage
  frame's object list → **CRASH-likely** (write through the garbage pointer at `RwFrameAddChild`/`SetFrame`).
  Frame list empty (`numFrames == 0`) → the atomic has **no frame** (`atomic+4 == NULL`) →
  `SetRelatedModelInfoCB`: `GetFrameNodeName(NULL)` = `0 + nameOffset` → `GetNameAndDamage` reads
  ~0x9x → **CRASH**.
- **DFF-06** FRAMELIST `parentIndex >= index of the frame` (forward reference) or `>= numFrames`: the
  parent slot is uninitialised malloc memory (`frames[]` is filled in order, 0x807B1A/0x807B32) →
  `RwFrameAddChild(garbage, frame)` → **CRASH-likely**. `parentIndex < 0` = root (fine; several roots
  allowed, only `frames[0]` becomes the clump frame: `*(clump+4) = frames[0]`).
- **DFF-07** ATOMIC `geometryIndex >= numGeometries` → `geoms[geometryIndex]` = garbage (0x74B11E-0x74B124)
  → `RpGeometryAddRef` (0x74B137) increments through it → **CRASH-likely**.
- **DFF-08** Pipeline-set plugin 0x253F2F3 with chunk length != 4: `PipelineStreamRead` 0x72FB90 does
  `RwStreamRead(stream, atomic+offset, length)` → length > 4 overwrites the atomic's following plugin
  bytes / heap → **CRASH-likely**; length < 4 leaves a partial id → wrong pipeline (GARBAGE, stock RW lighting).
  Values: 0x53F2009C building, 0x53F20098 building-DN, 0x53F2009A car (never for map objects);
  anything else → RW default pipeline + dynamic lighting (**GARBAGE**: model ignores the building
  shader / day-night blend).
- **DFF-09** GEOMETRY `numVertices >= 65536` or negative, or `numTriangles < 0` → `RpGeometryCreate`
  returns NULL (0x74CAA5-0x74CAB4) → **LOAD-FAIL**.
- **DFF-10** GEOMETRY `numMorphTargets == 0`: `RpGeometryCreate` always makes one target and the read loop
  runs `numMorphTargets` times **as stored in the geometry (≥1)** (0x74D3xx `while local_3c < geom+0x18`),
  so a file that omits the sphere/hasVertices/hasNormals block desyncs the stream (the material-list header
  is read as the sphere) → **LOAD-FAIL** at best, or `hasVertices` = a garbage non-zero word → a huge
  `RwStreamReadReal` into the vertex array → short read → LOAD-FAIL. `numMorphTargets > 1` is accepted
  (extra targets allocated; only target 0 is rendered) — IGNORED.
- **DFF-11** GEOMETRY flags bits 16..23 (`numTexCoordSets`) > 8: `RpGeometryCreate` stores the set pointers
  through `esi+0x34+i*4` for all `i < count` (0x74CBF1-0x74CBFB) → overwrites `+0x54 mesh, +0x58 repEntry,
  +0x5C morphTargets` → `RpGeometryAddMorphTargets` reallocs the garbage `+0x5C` → **CRASH**. Practical
  maximum used by the pipelines: 2.
- **DFF-12** GEOMETRY NATIVE flag (0x01000000) set: no CPU arrays are allocated (`RpGeometryCreate` 0x74CB0C
  `and ecx,0x1000000; jne 0x74cb32`; `RpGeometryAddMorphTargets` 0x74C40E) but the morph-target block is still
  read: `hasVertices != 0` → `RwStreamReadReal(stream, NULL, numVertices*12)` (0x74D4xx) → **CRASH** (write to 0).
  With `hasVertices == 0` the geometry has no vertices/triangles; rendering then depends on the D3D9
  native-data plugin 0x510 (read CB 0x750E40, not analysed) — never set it for PC exports.
- **DFF-13** Morph target `hasNormals != 0` while geometry flags lack NORMALS (0x10): normals pointer NULL
  (0x74C424 `test dl,0x10`) → `RwStreamReadReal(stream, NULL, n*12)` → **CRASH**. `hasVertices == 0` with
  `numVertices > 0` → vertex array left uninitialised → **GARBAGE** geometry. PRELIT flag (8) set but the
  prelit bytes missing → short read → **LOAD-FAIL**; prelit bytes present but flag clear → they are parsed
  as texcoords/triangles → **LOAD-FAIL/GARBAGE**.
- **DFF-14** `numMaterials == 0` (empty MATERIALLIST): `list->materials == NULL` (0x74E666-0x74E66B). With
  a BinMesh: `_rpMeshRead` → `_rpMaterialListGetMaterial` = `mov ecx,[eax]; mov eax,[ecx+edx*4]` with
  ecx = NULL (0x74E2B8) → **CRASH** for every mesh (numMeshes ≥ 1). Without a BinMesh and numTriangles ≥ 1:
  `RpGeometryUnlock` 0x74C8AE-0x74C8C0 → same → **CRASH**. Only a geometry with 0 meshes / 0 triangles survives.
- **DFF-15** MATERIAL struct chunk length > 28 → `RwStreamRead(local_1c, length)` (0x74DDxx) overruns the
  7-dword local into the return address → **CRASH**. Length < 28: fields zero-filled (colour 0 → black,
  `textured = 0` → texture never read: GARBAGE).
- **DFF-16** MATERIALLIST index entry ≥ 0 (material reuse) pointing at a slot not yet filled (`>= number
  of materials read so far`) → `materials[idx]` = uninitialised malloc → `inc word [edi+0x18]` (0x74E85D)
  → **CRASH-likely**. Always write -1 per material.
- **DFF-17** TEXTURE struct chunk length > 0x110 → `RwStreamRead(&local_110, length)` (0x8046xx) smashes
  the return address → **CRASH**; 4 < length ≤ 0x110 only clobbers locals that are re-written → tolerated.
- **DFF-18** Texture NAME string chunk > 128 bytes → `_rwStringStreamFindAndRead` copies it entirely into
  `local_80[128]` (0x80A63F-0x80A67E, 64-byte pieces, no cap) → return address of `RwTextureStreamRead`
  overwritten → **CRASH**. Mask string > 128 overwrites the name buffer (harmless until > 256 → CRASH).
- **DFF-19** Texture name not present in the model's TXD (or its parent chain), or longer than the TXD's
  31-char names (`RwTextureSetName` keeps 31 chars — a 40-char DFF name can never match), or `textured=1`
  with an empty name → `RwTextureRead` returns NULL (0x7F3B09-0x7F3B5C) and `RpMaterialStreamRead` stores
  it (0x74DED5) → material with **texture = NULL** → rendered untextured (`FUN_005da6a0`:
  `RwD3D9SetTexture(NULL,0)`, vertex colour × material colour, usually white/grey) — **GARBAGE**, no crash.
  Name compare is case-insensitive (`rwstricmp` at 0x7F3A17).
- **DFF-20** Triangle `matId >= numMaterials` in a geometry **without** BinMesh → `RpGeometryUnlock`
  `_rpMaterialListGetMaterial(list, matId)` (0x74C8BB) reads past `materials[]` → `mov ecx,[eax]` (0x74C8C0)
  dereferences the garbage → **CRASH-likely**. (With a BinMesh the triangle matIds are never used.)
- **DFF-21** BinMesh `materialIndex >= numMaterials` → `mesh->material` = pointer read past the array
  (0x758FDE) → dereferenced at render (`CustomPipeRenderCB` `[iVar5+8]+0x10` etc.) → **CRASH-likely**.
- **DFF-22** BinMesh `sum(numIndices) > totalIndices` → indices appended past the `totalIndices*2` allocation
  (0x759053-0x75905F) → heap corruption → **CRASH-likely**. `sum < totalIndices` → trailing garbage
  indices are never drawn (fine). `numMeshes == 0` → nothing is rendered (**IGNORED** — invisible model).
- **DFF-23** Any BinMesh index (or triangle vertex index when no BinMesh) `>= numVertices`: never checked
  by `_rpMeshRead` (values are read as int32 and truncated to uint16, 0x759053) nor by the D3D9 instancer;
  the index buffer references vertices outside the vertex buffer → **GARBAGE** triangles (driver-dependent;
  can fault in the instancer's vertex copy when the range is far beyond the arrays — CRASH-likely).
- **DFF-24** BinMesh header `flags` = 1 (tri-strip) while the index lists are triangle lists (or vice
  versa): the mesh is drawn as `D3DPT_TRIANGLESTRIP` with `numIndices-2` primitives → **GARBAGE** (fans of
  wrong triangles). The geometry flag TRISTRIP (1) only matters when no BinMesh exists (`RpGeometryUnlock`
  builds strips); the two flags should agree.
- **DFF-25** Bounding sphere (morph target 0, copied into `atomic+0x1C`) not enclosing the vertices:
  not used for culling (SA culls entities by the COL model: `CVisibilityPlugins::FrustumSphereCB` 0x732A40
  reads `colModel+0x18`; `IsAtomicVisible` 0x732990 is unused), but `_rwD3D9EnableClippingIfNeeded`
  0x756D90 (called first thing in `CustomPipeRenderCB` 0x5D6480) disables D3D clipping (`RS 0x88`) when
  `RpAtomicGetWorldBoundingSphere` is fully inside the frustum → geometry outside a too-small sphere
  crosses the near plane unclipped → **GARBAGE** (stretched/vanishing triangles near the camera); radius 0
  = always "inside". Must enclose all vertices with margin.
- **DFF-26** Geometry with `numVertices == 0` / `numTriangles == 0`: loads (arrays skipped), renders nothing;
  DN instancing of a 0-vertex geometry not verified → **unknown**, avoid.
- **DFF-27** More than one non-`_dam` atomic in an `objs`/`tobj` DFF: `SetRelatedModelInfoCB` runs per atomic
  in reverse file order and `SetAtomic` just overwrites `+0x1C` (0x4C4388) — the FIRST atomic of the file
  wins, the others are leaked (never rendered, their 2dfx lost) → **IGNORED**. Damage models: exactly one
  plain + one `_dam` atomic.
- **DFF-28** Atomic frame name ending in `_dam` in a model that is not an IDE-damageable (`flags & 0x1000`)
  model: `AsDamageAtomicModelInfoPtr` (vfunc +8) returns NULL for `CAtomicModelInfo` (0x4C4A90 `xor eax,eax`)
  → `CDamageAtomicModelInfo::SetDamagedAtomic(this=NULL)` writes `[0+0x20]` (0x4C48D6) → **CRASH**.
  Reverse (damageable model, no `_dam` atomic): `CDamageAtomicModelInfo::CreateInstance` 0x4C4910 returns
  NULL for the damaged version → entity has no RwObject when damaged → invisible (**IGNORED**).
- **DFF-29** Frame transform of the atomic in an `objs` model is discarded: `SetRelatedModelInfoCB` gives the
  atomic a fresh identity frame (0x5371BD `RwFrameCreate` / `RpAtomicSetFrame`) → any offset/rotation
  encoded in the DFF frame is **IGNORED** (geometry must be baked in object space). Clump (`anim`) models
  keep their hierarchy.
- **DFF-30** NodeName frame plugin longer than 23 characters (chunk length ≥ 24): `NodeNameStreamRead`
  0x72FA50 reads `len` bytes into the 24-byte plugin and writes `name[len] = 0` → overwrites the next
  frame plugin / frame memory → **CRASH-likely** (known W1; both IMG entry and frame name limits are 23).

### 2dfx (geometry plugin 0x253F2F8)
- **DFF-31** Sun-glare record (type 4) with `dataSize != 0`: the reader neither reads nor skips it
  (`else if (cVar1 != 4)` 0x6FA3xx — type 4 falls through with no I/O) → the next record is parsed from
  the middle of the data → garbage pos/type/size → usually a size mismatch → `RwStreamSkip(garbage)` →
  chunk desync → **LOAD-FAIL** (or garbage effects). Vanilla writes size 0.
- **DFF-32** Unknown effect type (2, 5, ≥ 11): same fall-through, nothing consumed → **LOAD-FAIL**/garbage.
  Known types with a wrong `dataSize` (see table 1.5) are skipped cleanly and the effect is dropped
  (**IGNORED**, count decremented).
- **DFF-33** More than 255 effects reachable from one model (`count` is stored in the model-info BYTE `+0xD`,
  `SetAtomic` 0x4C43A4 `add dl,al`): `CreateEffects`/`ProcessLightsForEntity` loop only `count & 0xFF`
  times and `Get2dEffect` mis-indexes → some effects **IGNORED**, others read from the wrong slot (**GARBAGE**).
  Limit: ≤ 255 per model (there is no other cap: the block is `Malloc(count*0x40+4)`).
- **DFF-34** Light: corona texture name not found in `particle.txd` → `+0x30 = NULL` → `CCoronas::Render`
  skips coronas whose texture is NULL (0x6FB119 `cmp [edi-0x28],0; je`) → no corona (**IGNORED**), point
  light/shadow still work. Shadow texture not found **and `shadowSize != 0`** → `ProcessLightsForEntity`
  passes `+0x34 = NULL` to `CShadows::StoreStaticShadow` (0x6FD40D/0x6FD42C, no check) → `CShadows::
  RenderStaticShadows` 0x7083A9-0x7083AC `mov ecx,[edi-6]; mov edx,[ecx]` = `texture->raster` → **CRASH**
  when the shadow is rendered (PLAUSIBLE — `StoreStaticShadow` was not fully traced; vanilla always names
  a texture when shadowSize > 0). Names are compared case-insensitively; the 24-byte name fields must be
  NUL-terminated (the reader treats them as C strings; an unterminated name just fails the lookup).
- **DFF-35** Light flags/colour: colour is RGBA (alpha used as corona alpha scale via `LightFade`), flags1 bits:
  1 checkObstacles, 2/4 fog type, 8 withoutCorona, 0x10 onlyLongDistance, 0x20 atDay, 0x40 atNight,
  0x80 blinking1; flags2: 1 onlyFromBelow, 2 blinking2, 4 updateHeightAboveGround, 8 checkDirection,
  0x10 blinking3. `atDay|atNight` both clear → the light never switches on (**IGNORED**). `coronaShowMode`
  > 13 → `switch` default → light never enabled (**IGNORED**). `pointlightRange == 0` → no point light;
  `coronaFarClip` is the corona draw distance (0 → never drawn). Negative sizes → GARBAGE (no check).
- **DFF-36** Particle (type 1): fx name must be a system in `effects.fxp` — `FxManager_c::FindFxSystemBP`
  0x4A9360 compares uppercase hashes; missing → NULL → `CreateFxSystem` 0x4A96B0 returns NULL → nothing
  (**IGNORED**, debug string "Cannot Find Fx System Blueprint").
- **DFF-37** 2dfx on a clump (`anim`) model: only the effects of the atomic that is first in list order
  (= LAST in the file) with a non-empty list are counted (`Get2DEffectAtomic` 0x734880 stops at the first
  hit) → effects on other atomics **IGNORED**.
- **DFF-38** ENEX record of size 0x28: `flags2 (+0x3E)` is an uninitialised stack byte (0x6FA2xx `uStack_de`
  never set for 0x28) → `CreateEffects` may treat it as a timed ENEX and `AddOne` gets garbage flags →
  **GARBAGE**; always write 0x2C. Roadsign: 64-byte text is copied verbatim; lines/letters/palette from
  the flags bits; `CreateRoadsignAtomic` substitutes " " for NULL lines. Ped attractor type 5 requests a
  script brain by the 8-char name; unknown name → **IGNORED**. Escalators/ENEX beyond their pools
  (`CEscalators::AddOne` / `CEntryExitManager::AddOne` return -1) → **IGNORED**.
- **DFF-39** 2dfx position outside the model bounds: nothing checks it (`CreateEffects` 0x533790 /
  `ProcessLightsForEntity` transform the model-space position and go) → works, but the light is culled with
  its entity (draw distance / visibility of the entity) → **IGNORED** (no engine check to mirror; warn only).

### Clump models (`anim` section) and animated objects
- **DFF-40** Clump model whose DFF has **zero atomics**: `SetClump` survives, but `CEntity::CreateRwObject`
  0x533EAF-0x533EB8 does `GetFirstAtomic(clump)` → NULL → `IsCBPCPipelineAttached(NULL)` → `GetPipelineID`
  0x72FC4A reads `[0 + DAT_008D6080]` → **CRASH** when the first instance is placed.
- **DFF-41** Atomic flags (ATOMIC struct third int) without rpATOMICRENDER (4) in a clump model → skipped by
  `RpClumpRender` → invisible atomic (**IGNORED**, PLAUSIBLE: standard RW behaviour, not traced in the exe).
- **DFF-42** Animated clump object (`anim` line, animfile != "null" → `+0x13 & 1`) — the DFF needs the frames
  named exactly as the IFP bones use them; missing anim block → `CAnimManager::AddAnimBlockRef` on index
  -1 is skipped, object just doesn't animate (**IGNORED**).

### Night colours / prelight (DN pipeline)
- **DFF-43** Night-colour block present (`hasColours != 0`) but geometry flags lack PRELIT (8) / `preLitLum == NULL`:
  the read CB allocates the day buffer and **leaves it uninitialised** (0x5D6E1x: copy only `if geom+0x30 != 0`),
  `IsCBPCPipelineAttached`/`AtomicSetup` then pick the plain building pipeline because `preLitLum == NULL`
  (0x5D7F16-0x5D7F29) → the night colours are never blended, the model renders **without any vertex colour**
  (material colour × lighting only) and the two buffers leak — **IGNORED/GARBAGE**. (No crash: the DN pipeline,
  whose `NVCPipelineProcess` writes into `preLitLum`, is only attached when both exist.)
- **DFF-44** PRELIT present, no night-colour block (or `hasColours == 0`): plain building pipeline, prelit is
  static (never blended) — correct for objects that should not react to day/night (**IGNORED** by design).
  Pipeline id 0x53F20098 in the file does not force DN: `AtomicSetup` rewrites it to 0x53F2009C.
- **DFF-45** Night-colour block shorter than `numVertices*4`: `RwStreamRead(night, numVertices*4)` (0x5D6E0x)
  ignores the chunk length → reads into the following chunks → extension desync → **LOAD-FAIL**; longer →
  the leftover bytes are parsed as the next plugin header → **LOAD-FAIL**.
- **DFF-46** Geometry flag MODULATEMATERIALCOLOR (0x40) with a black material colour → `prelit × 0` = black
  model (known, see memory gamelook-modulate-black-export) — **GARBAGE**; material alpha < 255 turns on
  alpha blending for that mesh (`CustomPipeRenderCB` 0x5D65xx `_rwD3D9RenderStateVertexAlphaEnable(mesh->vertexAlpha || material->color.a != 0xFF)`),
  so a "255,255,255,254" material renders in the alpha pass (sorting artefacts) — **GARBAGE**.

### UV animation
- **DFF-50** UV-anim dictionary chunk (0x2B) must be the **first** chunk of the file: `ConvertBufferToObject`
  0x40C7xx checks `RwStreamReadChunkHeaderInfo` once; a dictionary placed after the CLUMP is never read
  → all anims fall to DFF-51 (**IGNORED**). A malformed dictionary (unknown interpolator type, bad
  keyframes) → `RtDictSchemaStreamReadDict` NULL → current dict NULL → same fallback, the model still loads.
- **DFF-51** Material references an anim name not in the dictionary (or no dictionary): the material read CB
  0x7CBCC8-0x7CBCD4 creates a default 2-keyframe **identity** animation with that name → static UVs, no
  error (**IGNORED**).
- **DFF-52** UV anims in a **clump** model (`anim` section): `ConvertBufferToObject` reads the dictionary only
  for `GetRwModelType() == 1` (0x40C7xx `iVar7 == 1`), and `CEntity::PreRender` only updates UV anims when
  `mi->AsAtomicModelInfoPtr()` is non-NULL (0x535FDF `test ebx,ebx; je`) → **IGNORED** for clumps.
- **DFF-53** Atomic MatFX plugin (0x120 in the atomic extension) missing or 0: `RpMatFXAtomicQueryEffects`
  0x535FEF returns 0 → `MaterialUpdateUVAnimCB` never runs → anims frozen (**IGNORED**). Material MatFX
  effect must be 5 (UV transform) for the matrices to reach the shader; effect 2 (env map) is what the DN
  material setup 0x5D7120 handles.
- **DFF-54** More than 8 anims per material: the slot mask is `1 << i` for i in 0..7 (0x7CBC6B-0x7CBD1D);
  bits ≥ 8 and their names are **IGNORED** (and the names are not consumed → the STRUCT length no longer
  matches what was read → extension desync → **LOAD-FAIL**). Anim names are 32 bytes fixed, must be
  NUL-terminated (`RtDictFindNamedEntry` string compare).

### Breakable plugin
- **DFF-60** `BreakableStreamRead` validates nothing: counts drive `operator new(size)` and a chain of
  `RwStreamRead`s (0x59CFxx); short data → uninitialised arrays (**GARBAGE**); `numMaterials` names must be
  32-byte NUL-terminated; textures missing from the TXD → NULL pointers (used later by `BreakObject_c`
  rendering — **unknown**). Triangle indices ≥ `numVertices` / matIndex ≥ `numMaterials` are never
  checked (**CRASH-likely** when the object breaks). Glass behaviour is decided by IDE flags, not by this
  plugin (`CGlass::IsObjectGlass` 0x46A760).

### IDE / model-info / TXD interplay
- **DFF-70** IDE `objs`/`tobj`/`anim` model or TXD name ≥ 24 characters: `sscanf %s` into 24-byte stack
  buffers (`LoadObject` 0x5B3C60 `local_30[24]`, `local_18[24]`) → stack overwrite → **CRASH-likely** at
  IDE load; IMG directory entries also hold 23+NUL. `FindOtherTimeModel` copies the name into a 24-byte
  buffer too.
- **DFF-71** TXD named in the IDE line does not exist in any IMG: `SetTexDictionary` creates the slot, the
  streamer can never load it → `ConvertBufferToObject` keeps requesting → model never loads (**LOAD-FAIL**).
- **DFF-72** IDE flag 0x1000 (damageable) must match the DFF (DFF-28); flags 0x200/0x400 (glass) make
  `CGlass` shatter the object on impact using the model's first material texture; 0x2000/0x4000
  (tree/palm) route through the vegetation renderer (needs the vanilla tree material layout — not
  analysed here); 0x100000 (tag) → `CTagManager::SetupAtomic` at `SetAtomic` (expects a graffiti-tag
  material set — **unknown**, avoid).
- **DFF-73** LOD/time objects: LOD is purely an IPL link (`SetupBigBuilding` 0x533150 sets flag 0x20 on the
  model; a missing LOD model just leaves `m_pLod` NULL — parent pops instead of fading); time objects pair
  by `_dy`/`_nt` name (DFF-1.9), a missing partner is fine. Nothing DFF-side to check beyond names.
- **DFF-74** Model-info store overflow: more than 14000 `objs`, 169 `tobj`, 92 `anim`, 70 damage models
  → `AddXxxModel` writes past the store (0x4C6620.. no bound check) → **CRASH-likely** at IDE load.

---------------------------------------------------------------------------------------------------

## 3. Limits

| what | limit | evidence |
|---|---|---|
| RW version | 0x34000..0x36003 (write 0x1803FFFF) | every reader |
| vertices per geometry | ≤ 65535 (`< 0x10000`) | `RpGeometryCreate` 0x74CAA5 |
| triangles per geometry | int32 ≥ 0 (indices are uint16 → ≤ 65535 distinct vertices) | 0x74CAB0, `_rpMeshRead` truncation |
| texcoord sets | ≤ 8 slots in RpGeometry, pipelines use ≤ 2 | 0x74CBF1 loop, geometry +0x34..0x50 |
| morph targets | ≥ 1 (only target 0 rendered) | 0x74D2xx / 0x74C310 |
| materials per geometry | int32; 0 → crash (DFF-14); mesh materialIndex < numMaterials | 0x74E600, 0x758FDE |
| meshes per geometry | uint16 (`numMeshes` stored as word at header+4) | 0x758F55 |
| atomics per clump | int32; `objs` uses exactly 1 (+1 `_dam`) | DFF-27 |
| frames | int32; parent index < own index; name ≤ 23 chars | DFF-06/30 |
| texture name / mask | ≤ 31 chars to match a TXD entry; string chunk < 128 bytes | DFF-18/19 |
| 2dfx per model | ≤ 255 (byte count); sizes per type as in 1.5 | DFF-33 |
| IDE-section 2dfx store | 100 (`CStore<C2dEffect,100>` @0xB4C2D8) | 0x4C5A60 |
| scripted 2dfx | 64 | `CScripted2dEffects::Init` 0x6FA6F0 |
| UV anims per material | 8 slots; names 32 bytes; dict must be the first chunk | DFF-54/50 |
| night colours | exactly numVertices×4 bytes, requires PRELIT | DFF-43/45 |
| model-info stores | 14000 atomic / 70 damage / 169 time / 92 clump / 212 vehicle / 400 ped; ids < 20000 | 0x4C6620..0x4C67A0, 0x4C4884 |
| IDE names | ≤ 23 chars | DFF-70 |
| model alpha fade | `+0xC` alpha starts at 0 unless requested with flags 0x24 | 0x40CBxx |

---------------------------------------------------------------------------------------------------

## 4. Validator hook points and the self-test

**Where to hook (after the RW readers, before anything uses the object):**
- `CAtomicModelInfo::SetAtomic` **0x4C4360** — `thiscall(RpAtomic*)`, RET 4, vtable slot +0x3C of every
  atomic-type model info (objs/tobj/LOD; damage models route their plain atomic here too). Prologue
  `56 8B F1 8B 46 1C` = `push esi; mov esi,ecx; mov eax,[esi+0x1c]` — **6 bytes, no rel32**, same naked
  pushad/pushfd stub pattern as the ped validator (`INUHOOK(0x004C4360, ..., 6, stub)`). At entry:
  `ecx = CAtomicModelInfo*` (model id = `DAT_00B71840` 0xB71840, which `LoadAtomicFile` sets just before
  `RpClumpForAllAtomics`; or `(this - 0xAAE954)/0x20` for the atomic store), `[esp+4] = RpAtomic*` with a
  complete geometry: flags/counts, materials (+ NULL textures = missing in TXD), mesh header, 2dfx block
  (`*(RpGeometry* + *(int*)0xC3A1E0)`), night colours (`+ *(int*)0x8D12BC`), breakable (`+ *(int*)0xBB4238`),
  UV-anim interpolators per material (`material + *(int*)0xC9B8D0`), pipeline id (`atomic + *(int*)0x8D6080`,
  still the file value — `AtomicSetup` runs later inside SetAtomic), frame name via `frame + *(int*)0xC87C5C`.
- `CDamageAtomicModelInfo::SetDamagedAtomic` **0x4C48D0** (`56 8B 74 24 08 56 89 71` — 5-byte prologue
  `push esi; mov esi,[esp+8]`) for the `_dam` atomic.
- `CClumpModelInfo::SetClump` **0x4C4F70** — `thiscall(RpClump*)`, RET 4; prologue
  `56 57 8B F9 8B 47 1C` = 7 bytes, no rel32. Covers `anim` objects (and weapons; vehicles/peds have their
  own overrides at 0x4C95C0 / 0x4C7340). Model id: search `ms_modelInfoPtrs` 0xA9B0C8 for `this`
  (`DAT_00B71840` is NOT set on this path), or hook `LoadClumpFile` 0x5372D0 `(RwStream*, int id)` cdecl
  and remember `id` in a global before the clump path runs.
- Alternative single point: `CStreaming::ConvertBufferToObject` 0x40C6B0 `cdecl(int* buf, int id)` — a
  return hook sees `mi->m_pRwObject` and the load result (`ms_aInfoForModel[id].loadState`), but the
  RW-level crashes (DFF-04..07, 12..18) happen *inside* it; those can only be caught exporter-side
  (or by a pre-parse of the raw buffer in an entry hook: `buf` = the CD block, size
  `ms_aInfoForModel[id].cdSize*0x800`).
- Render-time checks (pipeline actually chosen, DN attached): after `SetAtomic` returns, read
  `atomic+0x6C` (== `*(void**)0xC02C1C` DN pipe, `*(void**)0xC02C68` building pipe) and `GetPipelineID`.

**Self-test streaming of every map model** (extend `selftest.cpp` LOAD_MODELS):
```
for id in 0..19999:
    mi = *(void**)(0xA9B0C8 + id*4); if !mi: continue
    type = mi->vfunc[+0x10]()            ; 1 atomic, 3 time, 4 weapon, 5 clump, 6 vehicle, 7 ped, 8 LOD
    if type in {6, 7}: continue           ; done by the existing phases
    info = 0x8E4CC0 + id*0x14; if *(u32*)(info+0xC) == 0: continue     ; not in any IMG
    if *(u8*)(info+0x10) == 1: skip (already resident, probably in use — do not RemoveModel a referenced model: check mi->refCount +0x8)
    Log("[selftest] map model %d", id)   ; before the load so a crash names it
    CStreaming::RequestModel(id, 0) 0x4087E0; CStreaming::LoadAllRequestedModels(0) 0x40EA10   ; bounded loop (max(10, 2*requests) iterations)
    loaded = *(u8*)(info+0x10) == 1 && mi->m_pRwObject (+0x1C) != NULL     ; else LOAD-FAIL → log DFF-01/02/03.. class
    if !loaded: CStreaming::RemoveModel(id) 0x4089A0   ; clears the pending request so the streamer stops retrying
    else if id < 1000 or refCount == 0: RemoveModel(id) (RemoveAllUnusedModels 0x40CF80 only sweeps ids ≥ 1000 with refCount 0)
    every 8 models: RemoveAllUnusedModels()
```
Notes: the TXD of each model is streamed automatically by `RequestModel` (dependency in
`CStreaming::RequestModel`); TXDs stay resident until `RemoveAllUnusedModels`. Requesting map DFFs does
**not** allocate CColModels (those come from the COL sectors, allocated once per referenced model when
the .col streams in — the pool at 0xB744A4 is 9958/10150 on this install), so the col-pool hazard from
the ped phase does not apply; do not request ids 25000..25255 (COL slots) in the loop. Ids 290..299 and
some script-only ids are special; the `cdSize == 0` test skips everything without a file. Because
`RemoveAllUnusedModels` only removes refCount-0 models, a model placed in the loaded IPL sector near the
player is left alone (checked by refCount) — its validator run still happened when the world loaded it.
Expect ~1 model/tick × ~15000 ids ≈ 4 min at 60 fps with `MODELS_PER_TICK = 4`.

**Exporter-side (cannot be caught in-game):** DFF-01..18, 20..24, 30, 45, 54 (stream-level), plus the
policy warnings DFF-25, 27, 29, 33, 43, 50, 52, 53.

---------------------------------------------------------------------------------------------------

## 5. Open questions
- The D3D9 instancer's exact reaction to an index ≥ numVertices (DFF-23) was not traced (the default
  instance callback is unnamed in the dump); treat as garbage-with-possible-fault.
- `CShadows::StoreStaticShadow` (HOODLUM-obfuscated entry) may reject a NULL texture before
  `RenderStaticShadows` dereferences it (DFF-34 shadow half) — verify in-game with a light whose shadow
  texture name is bogus and `shadowSize != 0`.
- DN instancing of a geometry with 0 vertices (DFF-26).
- Vegetation (tree/palm) and tag special types expect specific material layouts that were not analysed.
- Native-data plugin 0x510 path (DFF-12) — irrelevant for PC exports, not traced.
