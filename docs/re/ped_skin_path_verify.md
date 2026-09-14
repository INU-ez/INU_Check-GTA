# Verification of `ped_skin_path.md` claims C01–C28 (gta_sa.exe 1.0 US)

Method: every cited function was re-read from `E:\RE\asm_skin\<ADDR>.c/.asm`, or (for the bodies
Ghidra did not dump: SEH-wrapped `CreateHitColModelSkinned` 0x4c6d97, RW stream callbacks 0x7c4a00 /
0x7c6d20, `RtAnimInterpolator*` 0x7cd520/0x7cd5a0, `SkinGetBonePositionsToTable` 0x735360, skin render
0x7c8060.., `RwMatrixInvert` 0x7f2070/0x7f2160, `CMemoryMgr::MallocAlign` 0x72f4c0) from a capstone
disassembly of the exe bytes (`scratchpad/xdis.py`, copy at `E:\RE\xdis_tmp.py`). Constants via
`readva.py`. A vanilla `army.dff` (`E:\RE\addon_check\ped\army.dff`) was parsed to check what the engine
accepts in practice. gta-reversed was used only to see which *callers* are conditional.

Verdict legend: **CONFIRMED** = the unchecked access exists and the stated consequence follows;
**CONFIRMED (corrected)** = access exists, but offsets/limits/severity in the claim need the fix noted;
**REFUTED** = the condition as written is wrong (would flag vanilla files or the consequence does not follow).

---------------------------------------------------------------------------------------------------

## C01 — no hierarchy anywhere, last atomic skinned → CRASH — **CONFIRMED**

`CClumpModelInfo::SetClump` 0x4c4f70 (decompile):
```
p2 = (undefined4 *)GetAnimHierarchyFromClump(param_2);
RpClumpForAllAtomics(param_2,SetHierarchyForSkinAtomic,(undefined *)p2);
... weight loop ...
*p2 = 0x3000;                       // no NULL test -> write to address 0
```
`GetAnimHierarchyFromClump` 0x734b10 = `GetAnimHierarchyFromFrame(*(clump+4))` 0x734ab0: root frame's
plugin slot (`RpHAnimFrameGetHierarchy` 0x7c5160 = `*(frame + DAT_00c9b8c8 + 4)`), then
`RwFrameForAllChildren(FUN_00734a70)` depth-first (0x734a70: returns 0 = stop on first non-NULL). With no
table anywhere the result is NULL and the `*p2 = 0x3000` faults before `CreateHitColModelSkinned` is
even reached. Guaranteed crash at load.

## C02 — last atomic (first in RW list) has no skin, or 0 atomics → CRASH — **CONFIRMED**

* List order: `RpClumpAddAtomic` 0x74a4a3-0x74a4ad inserts at the list head
  (`[esi+0x44]=&clump->atomics; [esi+0x40]=old head; head=node`), `RpClumpForAllAtomics` 0x749b7b
  walks from `[clump+8]`. `GetFirstAtomicCallback` 0x734810 stores and returns 0. → last atomic in the
  file is "first".
* 0x4c4f70: `puVar5 = GetFirstAtomic(); if (puVar5 && RpSkinGeometryGetSkin(*(puVar5+0x18)))` — the
  whole hierarchy block (including `SetHierarchyForSkinAtomic`) is skipped otherwise. Atomic plugin
  ctor 0x7c6a80 initialises the slot to 0, so every atomic keeps hierarchy = NULL.
* `CPedModelInfo::SetClump` 0x4c7340: `if (*(this+0x34)==0) CreateHitColModelSkinned(clump)`; the hit
  col model is deleted in `CPedModelInfo::DeleteRwObject` 0x4c6c50 so this runs on every load.
* Body 0x4c6d97: `call GetAnimHierarchyFromSkinClump` (0x734a40 → cb 0x734a20 =
  `RpSkinAtomicGetHAnimHierarchy` of the first-iterated atomic, NULL if no atomics) → stored at
  `[esp+0x18]`; loop `0x4c6e69 call RpHAnimIDGetIndex(hier, boneID)` with no test.
  `RpHAnimIDGetIndex` 0x7c51a8 `mov esi,[edx+0x10]` with edx = NULL → read fault at 0x10.

## C03 — HAnim flags bit 0x2 (NOMATRICES) → CRASH — **CONFIRMED**

Frame plugin read 0x7c4a00: `0x7c4b08 mov eax,[esp+0x1c] (flags from file); mov [esi],eax;
0x7c4b18 test al,2; je alloc; else [esi+8]=0,[esi+0xc]=0`. The model clump's hierarchy therefore has
`pMatrixArray == NULL`. SetClump then overwrites `*p2 = 0x3000` (bit 2 gone), but nothing allocates
matrices for the *model* clump (only clones get them: frame-plugin copy 0x7c4857 passes the current
flags 0x3000 to `RpHAnimHierarchyCreate`). `CreateHitColModelSkinned` 0x4c6e74
`RpHAnimHierarchyGetMatrixArray` (= `[hier+8]` = 0) → `esi = idx<<6 + 0` → `RwMatrixTransform(work,
NULL+idx*0x40, 1)` reads page 0 → fault on the first sphere (bone 5). Crash at load.

## C04 — maxInterpKeyFrameSize < 28 → CRASH (heap overflow) — **CONFIRMED**

* `RtAnimInterpolatorCreate` 0x7cd520: `eax = numNodes*size + 0x4c; RwMalloc`; `[eax+0x2c]=numNodes;
  [eax+0x24]=[eax+0x20]=size` (raw from the DFF, passed by 0x7c4afe-0x7c4b00).
* Game scheme 0x4d618d: `interpKeyFrameSize = 0x1c`.
* `CClumpModelInfo::CreateInstance` 0x4c51a9 calls `RtAnimInterpolatorSetCurrentAnim` 0x7cd5a0 on the
  clone: `0x7cd5b8 [esi+0x24] = anim->interpInfo->interpKeyFrameSize (28)`; loop 0x7cd5e0-0x7cd611
  calls the interpolate CB for `numNodes` frames at `esi+0x4c+i*28`; second loop 0x7cd62a writes two
  pointers per frame at the same stride. No comparison against `+0x20`. `FrameUpdateCallBackSkinned`
  0x4d2b90 later writes 28 bytes per frame (`pfVar3[0..6]`). Block size is `0x4c+numNodes*fileSize`,
  so fileSize < 28 overflows by `numNodes*(28-fileSize)` bytes on every spawn.

## C05 — skin numBones > 64 → CRASH (stack smash) — **CONFIRMED (corrected: exact layout)**

`RpAnimBlendClumpInitSkinned` 0x4d6510: `sub esp,0x314; push ebx,esi,edi` → P. At 0x4d655d
`lea edx,[esp+0x24]` is executed with one cdecl argument still pushed (esp = P-4), so the table is at
**P+0x20 = esp_after_sub+0x14** and the 64×12 = 0x300 bytes end exactly at esp_after_sub+0x314 = top of
the frame (zeroing: `[esp+0x28]=0` + `rep stosd 0xbf` covers precisely P+0x20..P+0x320).
`SkinGetBonePositionsToTable` 0x735360 writes `table[i]` for `i = 1 .. numBones-1` (`0x735458 push eax
(table+12*i)`, loop bound `[esp+0x28]` = `RpSkinGetNumBones`) with no bound. Entry 64 lands on the
saved-EBP/padding + **return address** → `ret` at 0x4d660f jumps to a float. Crash on first spawn for
numBones ≥ 65. (The earlier text "table at esp+0x24" is the literal operand; the frame-relative
position is +0x14 of the 0x314 block, 64 entries fit with zero slack.)

## C06 — skin numBones != hierarchy numNodes → **CONFIRMED (corrected: GARBAGE + crash-likely, not a guaranteed crash)**

* numBones > numNodes: 0x4d65a2 `mov edx,[esi+0x10]; mov edi,[edi+edx]` reads `nodeInfo[i]` for
  i < numBones (heap read past the `numNodes*16` block — no fault); keyframe pointer
  `interp+0x4c+i*[interp+0x24]` (0x4d65af-0x4d65b9) only leaves the interpolator block when
  `numBones*28 > numNodes*36` (i.e. numBones > 1.29·numNodes); `SkinGetBonePositionsToTable`
  0x735462 reads `nodeInfo[i].flags` OOB → random push/pop → possible mid-table underflow (see C17).
  VS palette 0x7c78a0 loops `hier->numNodes` only, so vertices with index ≥ numNodes get bone 0 (C10).
* numBones < numNodes: `SetNumberOfBones(numBones)` allocates numBones frames;
  `FillFrameArrayIndicesSkinned` 0x4d647e-0x4d648a stores `&frames[idx]` for idx < numNodes → pointers
  past the block, dereferenced only by conditional gameplay code (see C20). 0x7c78a0 loop
  `while (iVar5 < puVar1[1])` reads `skinToBone[i]` past the matrix array (into the same data block's
  index/weight bytes) → garbage palette rows.
Net: always GARBAGE, heap corruption only under the sub-conditions above → "CRASH-likely", not CRASH.

## C07 — numBones == 0 → **CONFIRMED (crash-likely)**

`SetNumberOfBones` 0x4cf158-0x4cf168: `lea eax,[edi+edi*2]; lea ecx,[eax*8-1]; shr ecx,6; inc; shl 6`
→ for 0: `0xFFFFFFFF>>6 = 0x3FFFFFF; +1; <<6` = 0x100000000 → **0**. `MallocAlign(0,0x40)` 0x72f4c0 =
`malloc(0x40)` rounded up → 0..48 usable bytes. 0x4d6606 `or byte [edi],8` writes at the aligned pointer
(may be exactly the block end); `FillFrameArrayIndicesSkinned` stores `&frames[idx]` (idx up to 31 →
744 bytes past). Later conditional writes (C20) corrupt the heap. Also the skin data block has no
matrices, so 0x7c78a0 reads index/weight bytes as matrices → garbage render. Crash-likely, not
immediate.

## C08 — all-zero weights on a vertex of the last atomic → GARBAGE (NaN) — **CONFIRMED**

0x4c4f70: `fVar2 = _DAT_00858624 (=1.0f, readva) / (w1+w0+w2+w3); w[k] *= fVar2` with no zero test →
`1/0 = +inf`, `inf*0 = NaN`. D3D9 instancer `FUN_007c90d0`: only `if (1.0 < w0) w = (1,0,0,0)`; `1.0 <
NaN` is false so NaN survives. No fault (NaN vertices are clipped).

## C09 — weights not summing to 1 / negative / NaN on other atomics → GARBAGE — **CONFIRMED**

Normalisation loop runs only over `GetFirstAtomic`'s `geometry+0x14` vertices (0x4c4f70). Other skinned
atomics are rendered with raw weights (VS blends `Σ w_i·M_i`). `_rpSkinInitialize` 0x7c8740 orders the
4 (index,weight) slots by descending weight using **signed int compares of the float bits**
(`if (*piVar9 < 0x3f800000)`, `iVar6 < iVar1`), so negative/NaN weights sort wrongly. Garbage only.

## C10 — vertex bone index ≥ numBones/numNodes → GARBAGE — **CONFIRMED**

VS path `FUN_007c90d0`: `acStack_114[272]` cleared (255 bytes), filled `[i] = i*3` for i < numNodes
(or `[boneIds[k]] = k*3` in the compaction path), then `_rpD3D9VertexDeclarationInstIndicesRemap`
maps every index byte through it → unmapped indices become register 0 (bone 0). SW path 0x7cad64-
0x7cad79: `eax = index & 0xff; shl 6; add matrixBuffer` → ≤ 0x3fc0 inside the 0x4000 buffer. No fault.

## C11 — maxWeights == 0 in SA section / numUsed == 0 with maxWeights != 0 → **CONFIRMED (reasoning corrected)**

Reader 0x7c6d20: `0x7c6d73 mov al,[esp+0x1e] (byte2 = maxWeights); 0x7c6d7d test eax,eax; jne
0x7c6e35` → format is keyed on maxWeights only. Old path 0x7c6f62-0x7c6fa7: per bone `RwStreamSkip(4)`
+ `ReadReal(0x40)`, no used-list read, no 12-byte split header → an SA section is parsed out of phase
(LOAD-FAIL or garbage geometry).
numUsed == 0: the earlier text said the reader calls `FUN_007c70c0` "if numUsed==0". Wrong: both
`0x7c6dc2` and `0x7c6e60 test eax,eax` test **maxWeights** again, so in the new-format path that call is
dead code. The conclusion stands: `0x7c6f5c jne 0x7c704a` skips the recompute at 0x7c6fe8, so
`numBoneIds` stays 0. Render 0x7c8443-0x7c8454: `numNodes <= limit` and `numBoneIds (0) < numNodes` →
run-length path 0x7c8456 with `eax = numBoneIds-1 = -1`; the scan at 0x7c846d reads bytes past the
empty list (matrix bytes) and uploads one bogus run → real palette never uploaded → GARBAGE.

## C12 — used-bone entry ≥ numNodes / not sorted-unique → **REFUTED as stated; real condition is the inverse**

* Sorted: `_rpSkinInitialize` 0x7c8740 `FUN_008247e0(boneIds, numBoneIds, 1, cmp)` (qsort) — order is
  never a problem.
* Duplicates: run-length scan 0x7c8464-0x7c8480 breaks the run on a non-consecutive pair; a duplicate
  only produces a redundant upload of the same register. Harmless.
* Entry ≥ numNodes: register `base+id*3` beyond MaxVSConst → `SetVertexShaderConstantF` fails silently;
  palette read `+id*0x30` stays inside the 0x4000 scratch buffer for id ≤ 255. Only vertices that
  reference that id are affected — and those are already garbage under C10. By itself harmless.
* **The unchecked input that does produce garbage:** when `numBoneIds < numNodes` (vanilla army.dff:
  numUsed 27 < numBones 32, so this is the normal path) only the runs listed in `boneIds` are uploaded
  (0x7c8456-0x7c84bf). A bone referenced by a vertex but **absent from the used list** keeps whatever the
  previous skinned atomic left in that register → the ped deforms with another model's bone matrix.
  Exporter must emit a used list that is a superset of all referenced (non-zero-weight) bone indices, or
  simply list all bones (then `numBoneIds >= numNodes` → 0x7c84c4 uploads the whole palette).

## C13 — maxWeights > 4 → GARBAGE — **CONFIRMED (note)**

0x7c704c stores the raw byte in `skin+0x10`; render 0x7c80a7-0x7c80b3 `cl = [ebx+0x10]; add cl,cl;
... and cl,0xe; xor al,cl` packs `maxWeights & 7` into bits 1-3 of the shader-descriptor byte
`[esp+0x2a]`, later handed to the pipeline's get-vertex-shader callback (0x7c82a6). 5-7 select a
non-existent weight count, 8 wraps to 0. The `FUN_007c70c0` "shift wraps at 32" remark only applies to
the old-format path (dead code for SA sections). GARBAGE (or nothing drawn), no fault seen.

## C14 — numUsedBones > `(MaxVSConst-12)/3` → silently SW skinning — **CONFIRMED**

0x7cb2f5-0x7cb30c: `DAT_00c978ec = MaxVertexShaderConst; DAT_00c978e8 = (MaxVSConst-12)/3` (mul by
0xAAAAAAAB, shr 1), only when VS version ≥ 1.1 (0x7cb2e3). `FUN_007c8a00`: `[skin+0x1c] <= DAT_00c978e8`
else returns 0 → instance cb 0x7c9009 `[esi+0x20] = 0` → CPU path. `[skin+0x1c]` is set at
0x7c8ff4-0x7c9001 from `+0x24` (split boneLimit) or `+4` (numBoneIds). Not a crash.

## C15 — numNodes > 256 (SW) / > 341 (VS) → CRASH-likely — **CONFIRMED (moot)**

`RpSkinPluginAttach` 0x7c6951 `RwMalloc(0x400f)`, aligned to 16 → 0x4000 usable at `DAT_00c978ac`.
SW update 0x7ca8f5-0x7ca933 writes 0x40 per node (`add edi,0x40`, bound `[esi+4]` = numNodes); VS
update 0x7c78a0 writes 12 dwords (0x30) per node (`puVar4 += 0xc`), bound `puVar1[1]` = numNodes.
Overflow at 257 / 342 nodes. Only reachable if numBones ≠ numNodes (C06), since C05 fires at 65.

## C16 — node table on the clump root frame → GARBAGE (ped at origin) — **CONFIRMED**

`RpHAnimHierarchyUpdateMatrices` 0x7c51d0, flags 0x3000 branch: `iVar10 = *(p0+0x14)` (parentFrame,
stored by the stream read at 0x7c4b0e); `puVar5 = *(iVar10+4)` (RwObject.parent = parent frame);
NULL → identity in `local_940`, else `RwFrameGetLTM(parent)`. So bone matrices are relative to the
*parent of the frame carrying the table*; on the clump root that parent is NULL → model space.
`_rpD3D9SkinVertexShaderMatrixUpdate` 0x7c78a0: `_rwD3D9VSGetInverseWorldMatrix(); palette[i] =
skinToBone[i] * matrixArray[i] * invWorld` and the render sets the world matrix from the atomic frame's
LTM (0x7c80bc-0x7c80c6) → the world transform cancels → geometry drawn in model space at the origin;
`GetBonePosition` 0x5e4280 returns `matrixArray[idx].pos` = local coords. Vanilla: table on frame
"Root" (child of frame 0).

## C17 — POPs > PUSHes (or > 32 pending pushes) → CRASH — **REFUTED as stated (vanilla files do this); corrected below**

Flag decode (0x7c51d0 matrix branch): `uVar12 = flags & 3`; 1 → pop (`local_a0c--; parent =
*local_a0c`), 2 → push (`*local_a0c++ = parent; parent = &matrix[i]`), 3 → parent unchanged (neither
push nor pop; 0x735360 does push-then-pop, same net effect). The popped value is only used as the
parent of the **next** node.
Vanilla `army.dff` node table: pushes (flags==2) on ids 2,3,4,5,31,41 = 6; pops (flags==1) on
7,36,26,301,201,44,**54** = 7. The last node (54) pops an empty stack: 0x7c51d0 reads `local_880[0]`
(uninitialised) into `local_a14`, 0x735489 reads `[S+0xec]` into `[esp+0x14]`, and both loops then
terminate without using it. So "more POPs than PUSHes" is the *normal* R* layout and must not be
flagged. The dangerous condition is an **underflow at any node other than the last** (cumulative
pops > pushes with a node still to follow): the next node's parent is an uninitialised pointer
(`RwMatrixMultiply(matrix[i], kf, garbage)` → read fault / garbage) or, in 0x735360, an uninitialised
parent *index* (`shl esi,6; add esi,matrices` 0x735435-0x735438 → OOB read). CRASH-likely.
Push depth: 0x735360's stack starts at `S+0xec` (pre-increment), locals end at the return address
`S+0x16c` → **31** pending pushes fit, the 32nd overwrites the return address (not "~32"). The
0x7c51d0 stack (`local_880[1..47]` then the unused `local_7c0[496]` in this branch) is not the limit.

## C18 — duplicate nodeIDs → GARBAGE — **CONFIRMED**

`RpHAnimIDGetIndex` 0x7c51b9-0x7c51c3 returns on the first equal id. `RpAnimBlendClumpFindBone`
0x4d6400 → `ForAllFrames(0x4d63e0)`: `if (id == [frame+0x14]) DAT_00b5f87c = frame` for every frame
→ last match wins. Hit-col/IK/weapons use one copy, IFP sequences drive the other.

## C19 — node order ≠ skinToBone order → GARBAGE — **CONFIRMED**

0x7c78a0: `iVar3 = matrixArray - skinToBone; RwMatrixMultiply(tmp, skinToBone_i, skinToBone_i +
iVar3)` — pairing strictly by array position. `nodeInfo.nodeIndex` (+4) is read nowhere in the game
path (only `+0` in IDGetIndex/InitSkinned, `+8`/`+0xc` in UpdateMatrices/0x735360).

## C20 — missing hard-coded bone IDs → **CONFIRMED (corrected: GARBAGE always, CRASH only on gameplay conditions)**

Unchecked `-1` uses verified:
* `CreateHitColModelSkinned` 0x4c6e69-0x4c6e83: `esi = idx<<6 + matrixArray; RwMatrixTransform(work,
  esi, 1)` — a READ of the 64 bytes before the aligned matrix array (heap header/previous chunk):
  memory-safe, garbage sphere. Table 0x8a630c bones: 5,3,3,2,32,22,33,23,42,52,43,53 (readva).
* `FillFrameArrayIndicesSkinned` 0x4d647e-0x4d648a: `m_apBones[node] = frames + idx*0x18`, no test.
  Nodes 1..18 map to tags 3,5,32,22,... via `ConvertPedNode2BoneTag` 0x4d58a0 jump table.
* `CPed::GetBonePosition` 0x5e4280: `matrixArray + idx*0x40 + 0x30` read; NULL hierarchy handled, -1
  not. `GetTransformedBonePosition` 0x5e01c0: no NULL check either.
* `CPed::PreRenderAfterTest` 0x5e6b50-0x5e6b6a `RwMatrixScale(matrixArray+idx*0x40)` etc. are WRITES,
  but every one of them is inside a gameplay condition (rain on player / open-top vehicle, decapitated
  & talking, drunk wobble, earthquake — gta-reversed lines 2923-3000 mirror the asm). `CPedIK::
  PitchForSlope` (`frames[idx].KeyFrame->q` writes) runs only with |slopePitch| > 0.01; IKChain /
  m_apBones users (RemoveBodyPart, aiming, jump, jetpack, hold-entity) are all conditional.
So a ped missing e.g. bone 24 loads, spawns and renders (garbage hit spheres / weapon position);
the heap-corrupting writes to `matrix[-1]` / `frames[-1]->KeyFrame` happen only when one of those
conditions is hit. Recommendation unchanged (require the 32-bone table), severity corrected.

## C21 — HAnim version ≠ 0x100 → LOAD-FAIL — **CONFIRMED**

0x7c4a37 `cmp dword [esp+0x20],0x100; je ok; ... xor eax,eax; ret` → plugin read returns NULL →
`RpClumpStreamRead` fails.

## C23 — Skin PLG payload size ≠ header-derived size → **CONFIRMED (consequence mostly LOAD-FAIL)**

Reader 0x7c6d20 never reads its `length` argument (`[esp+0x22c]` after the 4 pushes = orig+8 is not
referenced; `FUN_007c8be0` receives only (stream, skin)); byte counts are `4 + numUsed (0x7c6ee7) +
numVerts*4 (0x7c6f13) + numVerts*16 (0x7c6f39) + numBones*64 (0x7c7052) + 12 (three ReadInt32 in
0x7c8be0, always)`. A mismatch leaves the RW chunk cursor out of phase for the next extension chunk →
normally the clump read fails; garbage only if the misread bytes happen to parse.

## C25 — singular / all-zero skinToBone matrix → GARBAGE — **CONFIRMED (corrected: no NaN for exact zero; flags word matters)**

`SkinGetBonePositionsToTable` 0x735426 `RwMatrixInvert(tmp, copy of skinToBone[i])`.
`RwMatrixInvert` 0x7f2070: `0x7f2085 mov eax,[esi+0xc]` — the matrix **flags word at +0xc** (the pad
after `right`) is honoured: `& 0x20000` (identity) → plain copy, `(eax & 3) == 3` (orthonormal) →
transpose path, else generic 0x7f2160. Generic: `0x7f21b3 mov edx,[esp+8] (det); test edx,edx; je` →
**det == +0.0 uses reciprocal 1.0** (no division) → an all-zero matrix yields an all-zero inverse, not
NaN; `-0.0` or a tiny non-zero det gives inf/NaN. Either way the bone offset table (velocity
extraction, `GetBonePosition` fallback) is garbage. Exporter: write 0 in all four pad/flag words
(vanilla army.dff has `(0,0,0,0)` in every bone matrix) and only invertible matrices.

## C26 — two frames with node tables → GARBAGE — **CONFIRMED**

0x734ab0 checks the root frame's slot first, then `FUN_00734a70` DFS (returns 0 to stop at the first
hit; `RwFrameForAllChildren` visits the most recently added child first). One hierarchy is assigned to
every atomic by `SetHierarchyForSkinAtomic`; the other is never updated/used.

## C28 — geometry flag 0x01000000 with a normal Skin PLG → LOAD-FAIL — **CONFIRMED**

0x7c6d31 `test dword [esi+8],0x1000000; jne 0x7c7088` → `FUN_007c88b0`: `RwStreamFindChunk(stream,1)`
(struct chunk), version must be 0x34000..0x36003 else `RwErrorSet`, then `ReadInt32 == 9` (platform),
then a single int32 numBones. A normal SA payload has no struct header → returns 0 → clump read fails.

---------------------------------------------------------------------------------------------------

## Struct / limit corrections summary

| item | earlier text | verified |
|---|---|---|
| RpAnimBlendClumpInitSkinned bone table | `esp+0x24`, 64 CVectors | frame-relative `esp_after_sub+0x14`, exactly 64 entries to the frame top; entry 64 hits saved EBP/return address |
| 0x735360 push stack | "~32" | 31 pending pushes; 32nd overwrites the return address |
| 0x7c51d0 push stack | 32 | 47 pointer slots + unused 0x7c0 area below the return address — not the binding limit |
| Skin reader `FUN_007c70c0` call in new format | "if numUsed==0" | tests maxWeights (0x7c6e60) → dead code for SA sections; numUsed==0 still never recomputed |
| RwMatrixInvert on singular | NaN | det==+0.0 → reciprocal 1.0 (finite garbage); honours flags word at matrix+0xc |
| Node flag 3 (push+pop) | — | treated as "parent unchanged" in 0x7c51d0; equivalent push+pop in 0x735360 |
| Last-node POP underflow | crash | harmless (vanilla army.dff does it) |
| Used-bone list | must be < numNodes, sorted, unique | must contain every referenced bone when numUsed < numNodes; order/dups irrelevant |
| C06 / C07 / C20 severity | CRASH | GARBAGE guaranteed, heap corruption only under stated sub-conditions |
| Hierarchy offsets | as listed | confirmed: +0 flags, +4 numNodes, +8 matrices, +0xc unaligned, +0x10 nodeInfo, +0x14 parentFrame, +0x18 self, +0x20 interpolator (+0x20 maxKF, +0x24 curKF, +0x2c numNodes, +0x4c frames) |
| Skin offsets | as listed | confirmed from 0x7c7190/0x7c704c/0x7c8ff4: +0 numBones, +4 numBoneIds, +8 boneIds, +0xc matrices, +0x10 maxWeights, +0x14 indices, +0x18 weights, +0x1c paletteBones, +0x20 useVS, +0x24 boneLimit, +0x28 numMeshes, +0x2c numRLE, +0x30..+0x38 split ptrs, +0x3c data block |

## Additional exporter/validator checks surfaced by this pass

1. Used-bone list ⊇ {bone index of every (index,weight≠0) pair} (0x7c8456 run-length upload).
2. All 16 pad words of every skinToBone matrix = 0 (RwMatrixInvert reads +0xc as type flags).
3. Push/pop balance is checked per prefix: for every node k < numNodes-1, pushes(0..k) ≥ pops(0..k)
   (flags==3 counts as neither); a trailing pop on the last node is allowed; pending depth ≤ 31.
4. Vanilla reference (army.dff): HAnim flags 0, kfsize 36, 32 nodes, numUsed 27 < numBones 32,
   maxWeights 4, split header (0,0,0).
