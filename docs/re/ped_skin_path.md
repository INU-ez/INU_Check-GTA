# GTA SA 1.0 US — PED SKIN loading path (RpSkin / RpHAnim / CPedModelInfo)

Source of truth: Ghidra decompiles in `E:\RE\asm_skin\<ADDR>.c` (+ capstone disassembly of the
un-dumped RW callbacks, script `scratchpad/xdis.py`), raw bytes via `readva.py`.
gta-reversed / plugin-sdk were used for names only; every claim below cites the exe code.

Legend: **CRASH** = guaranteed fault, **CRASH-likely** = deref of garbage pointer / heap corruption
that faults later, **GARBAGE** = wrong rendering / positions, no fault, **LOAD-FAIL** =
RpClumpStreamRead returns NULL, model never loads.

---------------------------------------------------------------------------------------------------

## 1. Runtime struct layouts (as seen in the decompile)

### 1.1 RpSkin (freelist DAT_00c978b8, entry size 0x40 — `RpSkinPluginAttach` 0x7c6941
`RwFreeListCreateAndPreallocateSpace(0x40,…)`; zeroed with `rep stosd 0x10` in `RpSkinCreate` 0x7c75b0)

| off  | field | evidence |
|------|-------|----------|
| 0x00 | `numBones` (u32) | `RpSkinGetNumBones` 0x7c77e0 returns `*(p0)`; stream read stores byte0 of header here (0x7c6d68 / 0x7c6f62) |
| 0x04 | `numBoneIds` (u32) = "numUsedBones" | `RpSkinCreate` `puVar2[1]=uVar8`; `_rpSkinInitialize` 0x7c8740 qsorts `boneIds` with count `+4` |
| 0x08 | `boneIds` (u8*) = used-bone list, start of the data block | `puVar2[2] = block`; 0x7c6edd `RwStreamRead(stream, [esi+8], numUsed)` |
| 0x0C | `skinToBoneMatrices` (RwMatrix*, 16-aligned, right after boneIds) | `RpSkinGetSkinToBoneMatrices` 0x7c7810 returns `+0xc`; `puVar2[3] = (block+numUsed+15)&~15` |
| 0x10 | `maxNumWeightsForVertex` (u32, 1..4) | 0x7c704c `mov [esi+0x10], eax` (byte2 of header); old format computed at 0x7c6fad-0x7c6fe6 |
| 0x14 | `vertexBoneIndices` (u32* — 4×u8 per vertex) | `RpSkinGetVertexBoneIndices` 0x7c7800 → `+0x14`; = matrices + numBones*0x40 |
| 0x18 | `vertexBoneWeights` (RwMatrixWeights*, 16 B/vertex) | `RpSkinGetVertexBoneWeights` 0x7c77f0 → `+0x18`; = indices + numVerts*4 |
| 0x1C | `numBonesForPalette` (= numBoneIds, or boneLimit if split) | set at 0x7c8fef-0x7c9001 in the D3D9 instance cb: `[esi+0x1c] = [esi+0x34] ? [esi+0x24] : [esi+4]` |
| 0x20 | `useVertexShader` (bool) | 0x7c9009 `= FUN_007c8a00(atomic)`; tested in render cb 0x7c7cd0 / 0x7c8011 |
| 0x24 | split.boneLimit | `FUN_007c8be0` (split stream read) `param_2[9]` |
| 0x28 | split.numMeshes (`RpSkinIsSplit` 0x7c7820 tests `+0x28 != 0`) | `param_2[10]` |
| 0x2C | split.numRLE | `param_2[0xb]` |
| 0x30 | split.meshBoneRemapIndices (u8*, own RwMalloc block) | `param_2[0xc]`, freed in `_rpSkinSplitDataDestroy` 0x7c8b10 |
| 0x34 | split.meshBoneRLECount (u8*) | `param_2[0xd]` |
| 0x38 | split.meshBoneRLE (u8*) | `param_2[0xe]` |
| 0x3C | data block pointer (freed in `RpSkinDestroy` 0x7c77a0 `+0x3c`) | `puVar2[0xf] = RwMalloc(numBones*0x40 + numVerts*0x14 + 0xf + numUsed)` |

Geometry plugin (DAT_00c978a8, 4 bytes): `RpSkin*`. Atomic plugin (DAT_00c978a4, 4 bytes):
`RpHAnimHierarchy*` (`RpSkinAtomicSetHAnimHierarchy` 0x7c7520). Atomic pipeline `+0x6c`:
skin pipeline object with `+0x2c == 0x116`, `+0x30 = RpSkinType` (`RpSkinAtomicGetType` 0x7c7880).

### 1.2 "Skin PLG" (0x116) geometry stream read — LAB_007c6d20 (registered at 0x7c68c5)

Args `(stream, length, geometry, offset, size)`. `length` is **never used** — the reader trusts the
header counts, not the chunk size.

```
if geometry->flags(+8) & 0x01000000 (NATIVE)  -> 0x7c88b0: expects struct chunk ver 0x34000..0x36003,
                                                  int32 platform==9, int32 numBones only (not our case)
hdr = ReadInt32(4)            ; byte0 numBones, byte1 numUsedBones, byte2 maxWeights, byte3 pad
numVerts = geometry+0x14
skin = FreeListAlloc(0x40); zero
if maxWeights == 0:   ; ---- OLD format (pre-3.5 / "0xDEADDEAD" pads) ----
    if numUsed==0: FUN_007c70c0 (no-op: skin+0x10 still 0)
    FUN_007c7190(skin, numBones, numUsed, numVerts, usedList, 0,0,0)      ; allocate block
    ReadInt32(indices, numVerts*4) ; ReadReal(weights, numVerts*16)
    for each bone: RwStreamSkip(4); ReadReal(matrix, 0x40)                 ; 4-byte pad before each matrix
    maxWeights := 1 + count of non-zero weight slots (scan 0x7c6fb4-0x7c6fe6, `cmp dword,0` on raw bits)
    FUN_007c70c0(skin, indices, weights, boneIds, &numUsed, numVerts)     ; recompute used list
else:                 ; ---- NEW format (SA 0x36003) ----
    if numUsed==0: FUN_007c70c0(...) with skin+0x10==0 -> count stays 0   (list NOT recomputed later!)
    FUN_007c7190(skin, numBones, numUsed, numVerts, stackbuf, 0,0,0)
    RwStreamRead(skin->boneIds, numUsed)   ; used-bone id list, numUsed bytes
    ReadInt32(indices, numVerts*4) ; ReadReal(weights, numVerts*16)
    skin->maxNumWeights = maxWeights (raw byte, NOT clamped to 4)
    ReadReal(skinToBone, numBones*0x40)
    FUN_007c8be0: ReadInt32 boneLimit, numMeshes, numRLE (12 bytes, ALWAYS read); if numMeshes>0
                  RwMalloc(numBones + (numMeshes+numRLE)*2) and RwStreamRead that many bytes
RpSkinGeometrySetSkin -> _rpSkinInitialize 0x7c8740 (sorts each vertex's 4 (idx,weight) pairs by
                         weight desc if w0<1.0 using integer compares of the float bits, qsort boneIds)
```

No validation of: numBones==0, vertex index < numBones, weight sums, maxWeights <= 4, used-list
entries < numBones, matrices invertible. Which format is used is keyed **only on the maxWeights byte**.

### 1.3 RpHAnimHierarchy (freelist DAT_00c9b8cc, 0x24 bytes — 0x7c468f) — `RpHAnimHierarchyCreate` 0x7c4c30

| off  | field | evidence |
|------|-------|----------|
| 0x00 | `flags` | `*piVar1 = p3`; SetClump writes 0x3000 here |
| 0x04 | `numNodes` (int32) | `piVar1[1] = p0`; `RpHAnimIDGetIndex` loop bound |
| 0x08 | `pMatrixArray` (RwMatrix*, 16-aligned, separate RwMalloc(numNodes*0x40+15)) — NULL if flags&2 | `piVar1[2]`; `RpHAnimHierarchyGetMatrixArray` 0x7c5120 |
| 0x0C | `pMatrixArrayUnaligned` | `piVar1[3]` |
| 0x10 | `pNodeInfo` (RpHAnimNodeInfo[numNodes], RwMalloc(numNodes<<4)) | `piVar1[4]` |
| 0x14 | `parentFrame` (RwFrame*) — the frame whose HAnim PLG carried the node table | stream read 0x7c4b0e `[esi+0x14]=frame` |
| 0x18 | `parentHierarchy` (self) | `piVar1[6] = piVar1` |
| 0x1C | `rootParentOffset` (sub-hierarchies only) | `RpHAnimHierarchyCreateSubHierarchy` `puVar3[7]` |
| 0x20 | `currentAnim` (RtAnimInterpolator*) | `piVar1[8] = RtAnimInterpolatorCreate(numNodes, maxKeyFrameSize)` |

RpHAnimNodeInfo (16 B): `+0 nodeID, +4 nodeIndex, +8 flags (1=POP, 2=PUSH parent matrix), +C pFrame`
(stream read 0x7c4b7d-0x7c4bbf reads 3 int32 and zeroes pFrame). **nodeIndex is never read by the
engine**; `RpHAnimIDGetIndex` 0x7c51a0 returns the *array position* of the first node whose nodeID
matches, or -1 (`piVar3 += 4` dwords per node, `while (p1 != *piVar3)`).

RtAnimInterpolator (`RtAnimInterpolatorCreate` 0x7cd520, RwMalloc(0x4c + numNodes*maxKeyFrameSize)):
`+0x20 maxInterpKeyFrameSize (from DFF), +0x24 currentInterpKeyFrameSize, +0x2c numNodes,
+0x3c keyFrameApplyCB (0 until SetCurrentAnim), +0x44 keyFrameInterpolateCB, +0x4c interp frames[]`.
The game registers its own scheme 0x253f2fb at 0x4d6180 with **interpKeyFrameSize = 0x1C (28)**,
apply CB 0x4d5fa0. `RtAnimInterpolatorSetCurrentAnim` 0x7cd5a0 copies size 28 into `+0x24` and
interpolates numNodes frames at `+0x4c + i*28`.

### 1.4 HAnim frame plugin (DAT_00c9b8c8, 8 bytes — `RwFrameRegisterPlugin(8, 0x11e)` 0x7c465e)

`+0 nodeID (int32, ctor 0x7c4750 sets -1)`, `+4 RpHAnimHierarchy*` (`RpHAnimFrameGetHierarchy`
0x7c5160, `RpHAnimFrameGetID` 0x7c5190).

"HAnim PLG" (0x11E) frame stream read — LAB_007c4a00 (registered as *read* at 0x7c4665;
0x7c48d0 is the *write*):
```
ReadInt32 version ; if != 0x100 -> return 0  -> RwFrame plugin read fails -> RpClumpStreamRead == NULL
ReadInt32 nodeID  -> plugin+0
ReadInt32 numNodes
if numNodes > 0:
    ReadInt32 flags ; ReadInt32 maxKeyFrameSize
    hier = FreeListAlloc(0x24) zeroed; hier->currentAnim = RtAnimInterpolatorCreate(numNodes, maxKeyFrameSize)
    hier->flags = flags (raw from file) ; parentFrame = frame ; numNodes ; parentHierarchy = self
    if flags & 2 (NOMATRICES): pMatrixArray = 0  else RwMalloc(numNodes*0x40+15) aligned
    nodeInfo = RwMalloc(numNodes*16); per node ReadInt32 x3 (id, index, flags), pFrame = 0
    plugin+4 = hier
```
Chunk length is ignored. Clone (RpClumpClone -> frame plugin copy 0x7c4830) recreates the hierarchy
with the same numNodes/flags/keyframe size and copies node (id,index,flags); pFrame stays 0.
`RpHAnimHierarchyAttach`/`RpHAnimFrameGetID` have **no callers** in the game code (call scan) —
per-bone-frame nodeIDs are never consulted for peds; only the node table matters.

### 1.5 CAnimBlendClumpData (clump plugin DAT_00b5f878, 0x14 bytes) / AnimBlendFrameData (0x18)

`CAnimBlendClumpData::SetNumberOfBones` 0x4cf140: `+8 numFrames = n`, `+0x10 frames =
CMemoryMgr::MallocAlign(((n*0x18-1)>>6+1)*0x40, 0x40)`. `ForAllFrames` 0x4cf190 strides 0x18.
AnimBlendFrameData: `+0 flags(u8; 8 = velocity-extraction root, 0x10 = 3D), +4 bone offset (CVector,
from SkinGetBonePositionsToTable), +0x10 keyframe ptr (game's RpHAnimBlendInterpFrame: quat @0, pos
@0x10 — `FrameUpdateCallBackSkinned` 0x4d2d8a..0x4d2e00 writes `[EDI+0..0xC]`, `[EDI+0x10..0x18]`),
+0x14 nodeID (bone tag)` (`RpAnimBlendClumpInitSkinned` 0x4d65a2-0x4d65c4).

---------------------------------------------------------------------------------------------------

## 2. Load path, in order (ped model)

1. `CFileLoader::LoadClumpFile` 0x5372d0 / `FinishLoadClumpFile` 0x537450 -> `RpClumpStreamRead`
   0x74b420 -> virtual `mi->SetClump` (CPedModelInfo vtable 0x85BDC0, slot 16 / +0x40 = 0x4C7340).
2. `CPedModelInfo::SetClump` 0x4c7340:
   a. `CClumpModelInfo::SetClump` 0x4c4f70 — `GetFirstAtomic(clump)` (**= the LAST atomic in the DFF**:
      `RpClumpAddAtomic` 0x74a4a3-0x74a4ad inserts at the list head, `RpClumpForAllAtomics` 0x749b7b
      walks from the head, `GetFirstAtomicCallback` 0x734810 returns 0 on the first hit). If its
      geometry has a skin and `!bHasComplexHierarchy` (flags byte +0x13 & 2, never set for peds):
      - bounding sphere radius of morph target 0 x1.2 (`*(*(geom+0x5c)+0x10) *= 1.2`)
      - `p2 = GetAnimHierarchyFromClump(clump)` 0x734b10 = `GetAnimHierarchyFromFrame(root)`:
        root frame's own plugin, else depth-first children (0x734a70). First hierarchy found wins.
      - `RpClumpForAllAtomics(SetHierarchyForSkinAtomic, p2)` -> `RpSkinAtomicSetHAnimHierarchy` on
        **every** atomic (skinned or not).
      - weight normalisation loop over `geometry+0x14` vertices of that first atomic only:
        `f = 1.0f / (w0+w1+w2+w3); w*=f` — **no zero check**.
      - `*p2 = 0x3000` (flags := UPDATEMODELLINGMATRICES|UPDATELTMS) — **no NULL check**.
   b. `SetFrameIds(m_pPedIds 0x8a6268)` — III/VC frame names ("Smid","Shead",…,"Slowerlegr") -> no
      match in SA peds, harmless.
   c. if `m_pHitColModel(+0x34)==0` -> `CreateHitColModelSkinned` 0x4c6d90 (SEH-wrapped, body at
      0x4c6d97): `hier = GetAnimHierarchyFromSkinClump(clump)` 0x734a40 (= atomic plugin of the
      first-iterated atomic, 0x734a20), 12 spheres from table 0x8a630c (stride 0x1c:
      `+0 boneID, +4 piece, +8 center, +0x14 radius`), each via `RpHAnimIDGetIndex(hier, boneID)` and
      `RwMatrixTransform(work, matrixArray + idx*0x40, PRECONCAT)` — **no NULL/-1 checks**.
   d. atomics' render CB := `CVisibilityPlugins::RenderPedCB` 0x7335b0.
3. Per ped instance: `CClumpModelInfo::CreateInstance` 0x4c5140 -> `RpClumpClone` (hierarchy cloned,
   flags now 0x3000 so matrices are allocated), `SetHierarchyForSkinAtomic`,
   `RpAnimBlendCreateAnimationForHierarchy` 0x4d60e0 + `RtAnimInterpolatorSetCurrentAnim` (stride := 28).
   `CPed::SetModelIndex` 0x5e4880 -> `RpAnimBlendClumpInit` 0x4d6720 -> (skin on first atomic?) ->
   `RpAnimBlendClumpInitSkinned` 0x4d6510 (clump in EAX) -> `RpAnimBlendClumpFillFrameArray` 0x4d64a0
   (skinned: 0x4d6450) -> `CreateHitColModelSkinned` again if needed -> `UpdateRpHAnim` 0x532b20 ->
   `RpHAnimHierarchyUpdateMatrices` 0x7c51d0.
4. Render: skin D3D9 pipeline (attached automatically by the atomic *always* callback 0x7c6ae0 when
   the geometry has a skin — no "right to render" chunk needed). Instance 0x7c8d60/0x7c90d0, render
   0x7c7c?? / 0x7c8060, palette `_rpD3D9SkinVertexShaderMatrixUpdate` 0x7c78a0.

---------------------------------------------------------------------------------------------------

## 3. Bone IDs the engine hardcodes for peds

Canonical SA skeleton = IK/BoneNodeManager table at **0x8D26D0** (32 entries x 40 B, `GetIdFromBoneTag`
0x617050 scans `[0x8d26d0,0x8d2bd0)`): `tag -> parent`:
`0->-1, 1->0, 2->1, 3->2, 4->3, 5->4, 6->5, 7->5, 8->5, 21->4, 22->21, 23->22, 24->23, 25->24, 26->25,
31->4, 32->31, 33->32, 34->33, 35->34, 36->35, 41->1, 42->41, 43->42, 44->43, 51->1, 52->51, 53->52,
54->53, 302->3, 301->3, 201->2`. Vanilla `army.dff` has exactly these 32 nodes (checked).

What happens when `RpHAnimIDGetIndex` returns -1: `matrixArray + (-1)*0x40` = the 64 bytes *before*
the aligned matrix array (malloc header + previous heap chunk). READs -> garbage but memory-safe;
WRITEs -> heap corruption -> **CRASH-likely** later. For `frameData[-1]` (AnimBlendFrameData, 0x18 B
before the CMemoryMgr block) the same.

| bone IDs | where (call scan of 0x7c51a0 / 0x5e4280 / 0x5e01c0) | -1 effect |
|---|---|---|
| 5,3,3,2,32,22,33,23,42,52,43,53 (table 0x8a630c) + 3 | `CreateHitColModelSkinned` 0x4c6e69 (READ of matrix[-1] into col spheres), `AnimatePedColModelSkinned` 0x4c7018/0x4c70ab, `…World` 0x4c71db/0x4c7248 | GARBAGE hit spheres (memory-safe read) |
| 3,5,32,22,34,24,41,51,43,53,52,42,33,23,31,21,4,8 (`ConvertPedNode2BoneTag` 0x4d58a0, ped nodes 1..18) | `FillFrameArrayIndicesSkinned` 0x4d6450: `m_apBones[i] = &frames[idx]` — no check (0x4d647e-0x4d648a) | m_apBones[i] -> `frames[-1]`; later `->KeyFrame` deref / flag writes (CPed::RemoveBodyPart, CPedIK, TaskSimpleHoldEntity, Jump, JetPack, ThrowProjectile) -> **CRASH-likely** |
| 4,31,21,3,32,22,5,8,6,7 ; 302,301,201 (rain/open-top jiggle, 0x5e6f1d-0x5e6f8d) ; 42,52,33,23,32,22,43,53,34,24 (earthquake) ; 5 | `CPed::PreRenderAfterTest` 0x5e6b50…0x5e71ce — `RwMatrixRotate/Scale(matrixArray + idx*0x40)` with **no -1 check** | WRITE to matrix[-1] -> heap corruption -> **CRASH-likely** (201/301/302 only when raining on player / driving open-top / earthquake) |
| 302,32,31,301,22,21 | `CPed::ShoulderBoneRotation` 0x5df594-0x5df748 (player model only) | RwMatrixCopy into matrix[-1] -> **CRASH-likely** |
| 24 | `CPed::AddWeaponModel` 0x5e5fa1, `CPed::ProcessControl` 0x5e8d67, `CPedIntelligence::ProcessAfterPreRender` 0x601adf, `CTaskSimpleFight` 0x61d13e, `CBike::FixHandsToBars` | weapon attaches to garbage matrix -> GARBAGE |
| 24,34,53,43 | `CTaskSimpleSwim::ProcessEffects` 0x68ae1b.. | GARBAGE fx positions |
| 24,26,301,34,32,302 | `CBike::FixHandsToBars` 0x6b8122.. | GARBAGE / matrix writes -> CRASH-likely |
| 2,51,52,53,41,42,43 | `CPedIK::PitchForSlope` 0x5fe385-0x5fe81b (matrix writes) | **CRASH-likely** |
| chain from effector to pivot via 0x8D26D0 parents (typ. 24->23->22->21->…) | `IKChain_c::SetupBones` 0x617cd1/0x617d0e: `frames[idx].keyframe` read, `BoneNode_c::Init` derefs it | **CRASH-likely** (aiming) |
| 34 ; 23 | `CVisibilityPlugins::RenderWeaponPedsForPC` 0x7330c2 ; `CHandObject::Render` 0x59ef10 | GARBAGE |
| 5 | `CCam::Process_AttachedCam` 0x513077, many `GetBonePosition(5)` | GARBAGE camera |
| `GetBonePosition` 0x5e4280 with 0,1,3,4,5,22,24,34,41,51,53 (43 callers: shadows use 0, camera 3/4/5, tasks…) | returns `matrixArray[idx*0x40+0x30..]` — hierarchy NULL is handled (falls back to entity pos) but -1 is not | GARBAGE position |
| `GetTransformedBonePosition` 0x5e01c0 (5 in `CPed::ProcessBuoyancy` 0x5e230a, 34/24 in `FireGun`) | **no NULL-hierarchy check** (`RpHAnimIDGetIndex(NULL)` reads `[0+0x10]`) | NULL -> **CRASH**; -1 -> GARBAGE |
| any (script/fx) | `FxSystem_c::AttachToBone` 0x4aa41a, `CCutsceneMgr::AttachObjectToBone` | GARBAGE |

Animation sequences (IFP) are matched by `RpAnimBlendClumpFindBone` 0x4d6400 (last frame whose
nodeID matches) in `CAnimBlendAssociation::Init` 0x4ced50 — a sequence whose bone is missing is simply
skipped (0x4cede3 `test eax,eax; je`). Bones with no sequence get identity from
`RpAnimBlendCreateAnimationForHierarchy`. -> missing bones never crash the *animation* code, only the
hard-coded lookups above.

Minimum set that is dereferenced on *every* ped (load + spawn + each frame, no gameplay condition):
**0,1,2,3,4,5,6,7,8,21,22,23,24,31,32,33,34,41,42,43,51,52,53** (hit-col, FillFrameArray,
PreRenderAfterTest, shadows). 201/301/302, 25/26/35/36, 44/54 are conditional (rain/open-top/
earthquake/bike/finger IK). Recommendation: require the full 32-bone table.

---------------------------------------------------------------------------------------------------

## 4. Conditions on the DFF that crash or produce garbage (each cites the unchecked access)

| id | condition | consequence | where / evidence |
|---|---|---|---|
| C01 | no RpHAnimHierarchy anywhere (no HAnim PLG with numNodes>0 on root or any descendant frame) while last atomic is skinned | **CRASH at load** | `CClumpModelInfo::SetClump` 0x4c4f70: `p2 = GetAnimHierarchyFromClump(); … *p2 = 0x3000` — null write. Also `SetHierarchyForSkinAtomic(…,0)` path then `CreateHitColModelSkinned` -> `RpHAnimIDGetIndex(NULL,…)` reads `[0x10]` |
| C02 | ped clump whose last atomic (file order) has **no skin** (non-skinned ped, or an unskinned extra atomic placed last), or 0 atomics | **CRASH at load** | `CClumpModelInfo::SetClump` skips the skin block (0x4c4f70 `GetFirstAtomic`+`RpSkinGeometryGetSkin` test) -> atomics never get a hierarchy -> `CPedModelInfo::SetClump` 0x4c7364 -> `CreateHitColModelSkinned` 0x4c6d97: `GetAnimHierarchyFromSkinClump` = NULL -> `RpHAnimIDGetIndex(NULL,5)` at 0x4c6e69 |
| C03 | HAnim flags in file contain bit 0x2 (NOMATRICES; e.g. 0x36) | **CRASH at load** | stream read 0x7c4b18 `test al,2 -> [esi+8]=0`; SetClump then forces flags 0x3000; `CreateHitColModelSkinned` -> `RwMatrixTransform(work, NULL + idx*0x40)`; clones would get matrices but the model clump does not |
| C04 | HAnim maxInterpKeyFrameSize (2nd dword after numNodes) < 28 (vanilla 36) | **CRASH-likely** (heap overflow on every ped spawn) | `RtAnimInterpolatorCreate` 0x7cd535 allocates `0x4c + numNodes*size`; `RtAnimInterpolatorSetCurrentAnim` 0x7cd5b8 sets stride := 28 (game scheme, 0x4d618d) and writes numNodes frames at 0x7cd5fd; `FrameUpdateCallBackSkinned` writes 28 B per frame |
| C05 | skin.numBones > 64 (also hierarchy numNodes > 64 when equal) | **CRASH** (stack smash on spawn) | `RpAnimBlendClumpInitSkinned` 0x4d6516 `sub esp,0x314`, table at `esp+0x24` = 64xCVector; `SkinGetBonePositionsToTable` 0x735360 writes `table[i]` for i<numBones (0x735458), no bound |
| C06 | skin.numBones != hierarchy.numNodes (any skinned atomic) | numBones > numNodes: **CRASH-likely** (`RpAnimBlendClumpInitSkinned` 0x4d65a2 reads `nodeInfo[i]` and computes keyframe ptr `interp+0x4c+i*size` for i<numBones -> keyframe writes past the interpolator block; `SkinGetBonePositionsToTable` 0x735462 reads `nodeInfo[i].flags` OOB). numBones < numNodes: GARBAGE + CRASH-likely (`FillFrameArrayIndicesSkinned` indexes `frames[idx]` with idx<numNodes >= numFrames; `_rpD3D9SkinVertexShaderMatrixUpdate` 0x7c78a0 loops `hier->numNodes` reading `skinToBone[i]` past the array; software path 0x7ca8e9 same) | see cites |
| C07 | skin.numBones == 0 | GARBAGE -> CRASH-likely | `SetNumberOfBones(0)` 0x4cf140: size `((0*0x18-1)>>6+1)*0x40` wraps to 0; `FillFrameArray` still stores pointers into the 0-byte block; later keyframe writes corrupt heap |
| C08 | vertex weights sum to 0 for a vertex of the last atomic (all four 0) | GARBAGE (NaN vertex: `1.0/0 = inf`, `inf*0 = NaN`) | `CClumpModelInfo::SetClump` 0x4c4f70 normalisation `fVar2 = 1.0 / (w0+w1+w2+w3)` no check; the VS instancer 0x7c90d0 only fixes `w0 > 1.0` |
| C09 | weights not summing to 1 on atomics other than the last one; negative weights; NaN | GARBAGE (only the first-iterated atomic is normalised; `_rpSkinInitialize` 0x7c8740 sorts by raw int compare of the float bits) | 0x4c4f70 loop uses `GetFirstAtomic` only |
| C10 | vertex bone index >= numBones (>= numNodes) | GARBAGE (VS: remap table `acStack_114[272]` 0x7c90d0 is indexed by the byte -> entries beyond numNodes are 0 -> bone 0 / stale constants; SW path indexes the 16 KB matrix buffer by byte*64 -> inside buffer) — no crash | 0x7c90d0, 0x7cad00 |
| C11 | maxWeights byte == 0 in an SA (new-format) section, or numUsed==0 with maxWeights!=0 | maxWeights==0 -> parsed as OLD format: 4-byte pad expected before every matrix and no 12-byte split header -> stream desync -> **LOAD-FAIL/GARBAGE**. numUsed==0 && maxWeights!=0 -> new format, used list NOT recomputed (0x7c6f5c `jne 0x7c704a` skips 0x7c6fe8) -> `numBoneIds=0` -> VS constant upload 0x7c8456 runs with count `numBoneIds-1 = -1`, reads bone ids from the matrix bytes -> palette not uploaded -> **GARBAGE** (ped deformed by stale palette) | 0x7c6d20, 0x7c8418-0x7c84bf |
| C12 | used-bone list entry >= numNodes / not sorted-unique | GARBAGE (run-length constant upload 0x7c8464/0x7c84f2 assumes sorted; register index `id*3+base` beyond 256 -> SetVertexShaderConstantF fails silently); `_rpSkinInitialize` qsorts, so order itself is fixed up | |
| C13 | maxWeights > 4 | GARBAGE (packed into the shader descriptor bits 1..3 at 0x7c80a7; old-format path 0x7c70c0 loops `k < maxWeights` shifting the index dword by 8k -> wraps) | |
| C14 | numUsedBones (or split boneLimit) > D3D9 palette limit `DAT_00c978e8 = (MaxVertexShaderConst-12)/3` (=81 on 256-const hardware, 0x7cb2fb-0x7cb30c) | not a crash: `FUN_007c8a00` returns 0 -> `skin+0x20=0` -> software skinning (slow). hierarchy numNodes > limit but numUsed <= limit -> palette compaction path (0x7c84e0, remap 0x7c90d0) — works | |
| C15 | hierarchy numNodes > 256 (software path) / > 341 (VS path) | **CRASH-likely** (heap overflow of the 0x4000-byte matrix buffer DAT_00c978ac: SW update 0x7ca8f5 writes 0x40 per node, VS update 0x7c78a0 writes 0x30 per node) — moot because C05 fires first | |
| C16 | HAnim node table placed on the **clump root frame** (frame with parent -1) instead of a child bone frame | GARBAGE: bone matrices computed relative to `parentFrame->parent` LTM (`RpHAnimHierarchyUpdateMatrices` 0x7c51d0: `iVar10=[p0+0x14]; puVar5=[iVar10+4]; NULL -> identity`) -> matrices in model space, VS palette multiplies by inverse world (0x7c78a0 `_rwD3D9VSGetInverseWorldMatrix`) -> ped rendered at world origin, `GetBonePosition` returns local coords | vanilla: root frame 0 (0x20003) -> frame 1 "Root" carries the table |
| C17 | node push/pop flags unbalanced: more POPs than PUSHes | **CRASH-likely** (`RpHAnimHierarchyUpdateMatrices` pointer stack `local_a0c = local_880+1`, pop reads `local_880[0]` uninitialised and uses it as a matrix pointer; `SkinGetBonePositionsToTable` 0x735489 pops `[esp+0xec]` uninitialised -> parent index -> OOB matrix read) ; PUSHes without POPs only mis-parent bones = GARBAGE; pending push depth > 32 overflows the 0x735360 stack (frame 0x15c) | |
| C18 | duplicate nodeIDs | GARBAGE (`RpHAnimIDGetIndex` returns the first, `RpAnimBlendClumpFindBone` the last -> col/IK use one copy, animation drives the other) | 0x7c51a0, 0x4d63e0 |
| C19 | node table order != skinToBone matrix order | GARBAGE (palette pairs `skinToBone[i]` with `matrixArray[i]` by position, 0x7c78a0 `iVar3 = matrixArray - skinToBone`) | |
| C20 | missing any bone from the table in section 3 | see section 3 (CRASH-likely for the always-used set via writes to `matrix[-1]` / `frames[-1]`) | |
| C21 | HAnim PLG version dword != 0x100 on any frame | **LOAD-FAIL** (0x7c4a37 `cmp [esp+0x20],0x100; jne -> ret 0`) | |
| C22 | HAnim numNodes <= 0 on the intended root bone frame | same as C01 (no hierarchy created, 0x7c4a83 `jle`) | |
| C23 | Skin PLG payload size != `4 + numUsed + numVerts*4 + numVerts*16 + numBones*64 + 12` (new format) | **LOAD-FAIL/GARBAGE** (reader ignores chunk length; subsequent chunks parsed out of phase) | 0x7c6d20 |
| C24 | split header numMeshes > 0 with wrong sizes | reader RwMallocs `numBones+(numMeshes+numRLE)*2` and reads that many bytes -> desync -> LOAD-FAIL | 0x7c8be0 |
| C25 | singular / all-zero skinToBone matrix | GARBAGE (`RwMatrixInvert` in `SkinGetBonePositionsToTable` 0x735426 -> NaN bone offsets -> velocity extraction / bone positions NaN) | |
| C26 | more than one HAnim node table (two frames with numNodes>0) | GARBAGE: only the first found by DFS from the clump root is used (0x734ab0/0x734a70); the other hierarchy leaks | |
| C27 | second skinned atomic's skin with different numBones | see C06 (each atomic's palette uses hier numNodes vs its own skinToBone array) | |
| C28 | geometry flags bit 0x01000000 (native) set | reader takes the native branch 0x7c88b0 -> needs platform-9 struct -> **LOAD-FAIL** for a normal section | |

---------------------------------------------------------------------------------------------------

## 5. Engine limits

| limit | value | evidence |
|---|---|---|
| bones per ped clump (skin numBones) | **<= 64** | 64-entry CVector table on the stack of `RpAnimBlendClumpInitSkinned` 0x4d6510 (frame 0x314); gta-reversed `MAX_NUM_BONES = 64` agrees |
| numBones / numUsedBones / maxWeights in the file | u8 (<=255) | header byte layout 0x7c6d64-0x7c6d73 |
| bone index per vertex | u8 (4 packed in a u32) | |
| VS palette bones | `(MaxVertexShaderConst-12)/3` -> 81 on 256-const GPUs; each bone = 3 float4 registers; above -> CPU skinning | 0x7cb2fb-0x7cb30c, 0x7c8208 (lights are dropped when `used*3 + lightConsts > MaxVSConst`) |
| matrix scratch buffer DAT_00c978ac | 0x4000 B (256 matrices) | `RpSkinPluginAttach` 0x7c6951 `RwMalloc(0x400f)` |
| interp keyframe stride the game needs | 28 (file value must be >= 28; vanilla 36) | 0x4d618d, 0x7cd5b8 |
| push-stack depth in `SkinGetBonePositionsToTable` | ~32 pending pushes | frame 0x15c, stack at esp+0xec |
| hit-col spheres | 12, table 0x8a630c | `AnimatePedColModelSkinned` loop end `0x8a6468` |
| ped node array `m_apBones` | 19 (nodes 1..18) | 0x4d648f `cmp esi,0x13` |
| IK bone table | 32 entries at 0x8D26D0 (40 B each) | `GetIdFromBoneTag` 0x617050 |
| RpHAnimHierarchy numNodes | int32 in file, only bounded by C05/C15 | |

---------------------------------------------------------------------------------------------------

## 6. Hook points for an in-game validator

**Primary: `CPedModelInfo::SetClump` 0x4C7340** (vtable 0x85BDC0 slot 16 / +0x40; thiscall,
`ECX = CPedModelInfo*`, `[esp+4] = RpClump*`). Called exactly once per ped model load from
`CFileLoader::LoadClumpFile` 0x5372d0 / `FinishLoadClumpFile` 0x537450 with the fully streamed
clump, *before* any engine dereference (the first unchecked accesses are inside
`CClumpModelInfo::SetClump` 0x4c4f70 and `CreateHitColModelSkinned`). Hooking the entry lets the
validator refuse/patch the clump (e.g. return without calling the original and let streaming fail
the model) instead of crashing. Ped-only (vehicles/weapons have their own SetClump).
Register hazard reminder (memory `gta-hook-register-hazard`): use a naked pushad/popad stub.

What is inspectable there:
- `RpClumpForAllAtomics(clump, cb)` (0x749b70): for each atomic `geom = atomic+0x18`,
  `skin = *(geom + DAT_00c978a8)`, `numVerts = geom+0x14`, `geomFlags = geom+8`; skin fields per 1.1
  (numBones, numBoneIds, boneIds, maxNumWeights, indices, weights, skinToBone). The list head is the
  last atomic in the file -> check that one has a skin (C02).
- Frames: `root = clump+4`; `RpHAnimFrameGetHierarchy(f) = *(f + DAT_00c9b8c8 + 4)`, id at `+0`;
  walk `RwFrameForAllChildren` 0x7f0dc0. Find hierarchies: count them (C26), get numNodes, flags
  (C03), `currentAnim+0x20` = file keyframe size (C04), nodeInfo IDs/flags (C17/C18/C20/section 3),
  parentFrame (+0x14) and its parent (+4 of the RwFrame) (C16).
- Cross-checks: numBones == numNodes for every skinned atomic (C06), <=64 (C05), used-list sanity
  (C11/C12/C14), per-vertex indices < numBones (C10), weights: no all-zero, sum~1, no NaN/negative
  (C08/C09), maxWeights 1..4 (C13), matrices finite & invertible (C25), the 32 canonical IDs present.

Secondary options: `CClumpModelInfo::SetClump` 0x4c4f70 (all clumps, but SetClump has already been
entered for vehicles too); `RpClumpStreamRead` 0x74b420 return (generic, model type unknown there);
`RpAnimBlendClumpInitSkinned` 0x4d6510 (clump in EAX, per spawn — too late for C01–C03 but the place
where C05/C06 blow up; skip it).

Exporter-side checks that mirror the engine (INU_tools/core/dff.py `SkinData.to_bytes`):
write `oldver` keyed on `max_weights == 0` (engine keys on that byte, not on num_used); never emit
num_used == 0 for SA; always compute the used list; assert len(bone_indices)==len(bone_weights)==
len(vertices); numBones == len(hanim.bones) == len(bone_matrices); <= 64 bones; HAnim flags 0,
keyframe size 36; node table on the first armature bone frame (child of the clump root), node order ==
matrix order; push/pop flags balanced (depth-first); all 32 canonical IDs present, unique.

---------------------------------------------------------------------------------------------------

## 7. Open questions / not verified

- Player (CJ) path `CClothesBuilder::CreateSkinnedClump` 0x5a69d0 builds its own hierarchy/skin —
  not analysed (different loader, `RpHAnimFrameSetHierarchy` 0x7c5130 is used there).
- Whether `RwMalloc` (RwEngineInstance+0x134) is the game's zeroing allocator — irrelevant for the
  crash cases above (they are NULL / OOB, not uninitialised reads) but affects `CreateHitColModelSkinned`
  reading the never-updated model-clump matrix array (garbage spheres until first `AnimatePedColModelSkinned`).
- Exact D3D9 caps of the user's GPU decide the 81-bone VS limit; the SW fallback path was not
  traced beyond the matrix update (0x7ca880) and index usage.
- `RpAnimBlendClumpFindBone` returns the *last* match, `RpHAnimIDGetIndex` the *first* — noted, but
  the practical impact of duplicate IDs was not tested in-game.
