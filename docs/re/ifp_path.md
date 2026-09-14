# IFP animation loading path — gta_sa.exe 1.0 US (base 0x400000)

Sources: Ghidra dumps in `E:\RE\asm_skin\<ADDR>.c/.asm` and `E:\RE\asm\`, raw bytes via
`readva.py`, capstone disassembly of the exe for the few functions that had no dump
(CAnimBlendNode::Update 0x4D06C0, UpdateCompressed 0x4D08D0, GetCurrentTranslation 0x4CFC50,
CAnimBlendAssociation::Init 0x4CED50, CAnimBlendStaticAssociation::Init 0x4CEC20,
CAnimBlendAssocGroup::CreateAssociations 0x4CE220/0x4CE6E0, CreateAnimAssocGroups 0x4D3CC0,
RpAnimBlendClumpFindBone 0x4D6400, RpAnimBlendClumpFindFrameFromHashKey 0x4D6370,
CQuaternion::Normalise 0x4D1610, CQuaternion::Slerp 0x59C300, UncompressAnimation 0x4D41C0,
CStreaming::ConvertBufferToObject 0x40C6B0). gta-reversed/plugin-sdk used only for names.

Every crash/garbage claim below cites the function and the exact unchecked dereference.

---------------------------------------------------------------------------------------------------

## 0. Callers of CAnimManager::LoadAnimFile (0x4D47F0, cdecl(RwStream*, bool loadCompressed, const char (*uncompressedList)[32]))

| caller | args | what follows |
|---|---|---|
| CAnimManager::LoadAnimFiles 0x4D5620 (startup, `ANIM\PED.IFP`) | `(stream, true, NULL)` | `new CAnimBlendAssocGroup[ms_numAnimAssocDefinitions]`, CreateAnimAssocGroups 0x4D3CC0 |
| CStreaming::ConvertBufferToObject 0x40C6B0 @ 0x40CA7C (model id 0x63E7..0x649A = 25575..25754 = IFP slots, ESI = model id) | `(stream, true, NULL)` | CreateAnimAssocGroups 0x4D3CC0 immediately after (0x40CA84) |
| CCutsceneMgr::LoadCutsceneData_postload (cuts.img) | `(stream, true, ms_aUncompressedCutsceneAnims)` | CAnimBlendAssocGroup::CreateAssociations; unloaded later by RemoveLastAnimFile 0x4D3ED0 |

`loadCompressed` is `true` for every caller. It only matters for the ANPK path (ANP3 carries its own
per-sequence compression); `uncompressedList` is a NUL-terminated list of 32-byte names exempted
from compression (cutscenes only).

---------------------------------------------------------------------------------------------------

## 1. On-disk layout the parser accepts

### 1.1 Dispatch (0x4D47F0..0x4D4856)

```
RwStreamRead(stream, hdr, 8)              // hdr = {char tag[4]; uint32 size}
if strncmp(tag,"ANP2",4)==0 || strncmp(tag,"ANP3",4)==0  -> ANP2/ANP3 path (0x4D5172)
else                                                     -> ANPK path (0x4D485C)  (tag is NOT checked!)
```

Anything that is not `ANP2`/`ANP3` is parsed as ANPK — the `ANPK` tag itself is never compared.
The `size` field of the top-level header is rounded up to 4 and then ignored in both paths.

### 1.2 ANP2 / ANP3 (0x4D5172 – 0x4D558B). All fields little-endian, no padding.

```
struct Anp3File {
    char   tag[4];          // "ANP2" or "ANP3"
    uint32 fileSize;        // ignored
    char   blockName[24];   // -> strncpy(block->name, 16) ; matched with _stricmp against ms_aAnimBlocks[].name
    uint32 numAnims;        // loop count (ANP3/ANP2 path uses THIS count, see 3.9)
    Anp3Anim anims[numAnims];
};
struct Anp3Anim {
    char   name[24];        // hashed with CKeyGen::GetUppercaseKey up to the first NUL
    uint32 numSequences;    // stored as int16 into hier->m_nSeqCount, loop uses int32
    // ANP3 only:
    uint32 frameDataSize;   // CMemoryMgr::Malloc(frameDataSize) -> one block for ALL keyframes of this anim
    uint32 flags;           // bit0 -> hier->m_bRunningCompressed, BUT overridden by first sequence type (see below)
    Anp3Seq seqs[numSequences];
};
struct Anp3Seq {
    char   name[24];        // hashed (GetUppercaseKey) -> seq->m_hash
    uint32 frameType;       // 1 = KeyFrame(20B, uncompressed, rot only)   2 = KeyFrameTrans(32B, uncompressed, rot+trans)
                            // 3 = KeyFrameCompressed(10B)                 4 = KeyFrameTransCompressed(16B)
                            // anything else: NO SetNumFrames, NO read -> stream desync (see 3.4)
    uint32 numFrames;       // stored as int16 in seq->m_nFrameCount; RwStreamRead uses the full uint32
    int32  boneId;          // -1 = "use name hash"; otherwise SetBoneTag: seq->flags |= 0x10, *(int16*)seq = boneId
    uint8  frames[numFrames * sizeof(type)];   // read RAW into memory; on-disk layout == runtime layout (1.4)
};
```

Evidence: reads of 0x18/4 (0x4D5194,0x4D51A4), per anim 0x18/4 (+4/4 if ANP3, 0x4D5271..0x4D52BF),
per sequence 0x18/4/4/4 (0x4D5382..0x4D53B4), switch on frameType at 0x4D5411 (`DEC EAX; CMP EAX,3; JA default`),
RwStreamRead(seq->m_Frames, n*0x14 / n<<5 / n*10 / n<<4) at 0x4D544B/0x4D5491/0x4D54D9/0x4D5518.

ANP3 specifics:
* `frameDataSize` bytes are allocated once (`CMemoryMgr::Malloc`, 0x4D52D5); every sequence's `m_Frames`
  is a cursor into that block (`SetNumFrames(..., p4=cursor)`, cursor += bytes read). **Nothing checks that
  the sum of `numFrames*kfSize` fits in `frameDataSize`** (3.3).
* Every sequence gets `flags |= 8` (external memory, 0x4D5539) — also in the invalid-frameType default case.
* `flags & 1` is written to `hier->m_bRunningCompressed` (0x4D52D2) but at the **first** sequence the loader
  overwrites it: frameType 1/2 -> 0, otherwise -> 1 (0x4D53E4–0x4D540D, `cStack_231` one-shot). So the header
  flag only matters when `numSequences == 0`.
* ANP2: identical minus `frameDataSize/flags`; frames are malloc'ed per sequence (SetNumFrames with p4=NULL).

After all sequences of an anim: if `hier->m_bRunningCompressed == 0` -> `RemoveQuaternionFlips()` then
`CalcTotalTime()` (0x4D5570/0x4D5577). Compressed anims are left untouched until first use.

### 1.3 ANPK (GTA III/VC style, 0x4D485C – 0x4D516D). Sequential, size-prefixed sections.

Each section header = `{char tag[4]; uint32 size}`; `size` is rounded up to a multiple of 4 before
reading the body. **No tag except the KFRM tags is ever checked**, and no `size` is ever used to skip:
the parser reads the body of exactly the sections listed below, in this order.

```
[ANPK hdr 8]                                      // tag unchecked (any non-ANP2/ANP3 tag lands here)
[INFO hdr 8] [body: uint32 numAnims; char blockName[...]]   // body read into a 260-byte stack buffer (3.13)
for each anim (loop count = block->numAnims, see 3.9):
    [NAME hdr 8] [body: char animName[...]]       // hashed up to NUL -> hier->m_hashKey
    [DGAN hdr 8]                                  // body NOT read (size only rounded, 0x4D4A20)
    [INFO hdr 8] [body: uint32 numSequences; char name[...]]
    for each sequence:
        [CPAN hdr 8]                              // body NOT read
        [ANIM hdr 8] [body: char objName[28]; uint32 numFrames; uint32 next; uint32 prev; (int32 boneId if size==44)]
        if numFrames != 0:
            [KFRM hdr 8]   tag must be "KRTS" | "KRT0" | "KR00"   (0x4D4BF0/0x4D4C1F/0x4D4C4A)
            numFrames * keyframe:
                KR00: float q[4] (x,y,z,w); float time                          (20 B)
                KRT0: float q[4]; float pos[3]; float time                      (32 B)
                KRTS: float q[4]; float pos[3]; float scale[3]; float time      (44 B)  scale is READ AND DISCARDED
```

Evidence: body offsets `iStack_fc` = +0x1C (numFrames), `iStack_f0` = +0x28 (boneId), `CMP [ESP+0x24],0x2c`
before SetBoneTag (0x4D4BA8); frame reads 0x2C/0x20/0x14 at 0x4D4C9F/0x4D4E1F/0x4D4FCD; time taken from
buffer+0x28 / +0x1C / +0x10 respectively (`FLD [ESP+0x10c]`, `[ESP+0xb4]`, `[ESP+0xe0]`).

ANPK conversion on load:
* quaternion is **conjugated**: x,y,z negated, w kept (`FCHS` x3 at 0x4D4CE5..0x4D4CF7 etc.). ANP3 data is
  taken raw. => an ANPK file must store the conjugate of what an ANP3 file stores.
* if the anim is compressed (`loadCompressed && name not in uncompressedList` — always true for
  streamed/ped IFPs) each frame is quantised right away:
  `q*4096 -> int16 (_ftol, truncation)`, `time*60 + 0.5 -> int16`, `pos*1024 -> int16`
  (constants 0x858BF8=4096, 0x858B34=60, 0x858B8C=0.5, 0x85C6D8=1024; stores are `MOV word ptr`).
  Otherwise stored as floats in runtime layout (time at +0x10, pos at +0x14).
* the sequence-name hash is set first, then `SetBoneTag(boneId)` if ANIM size == 44 (0x4D4BB9).
* KRTS scale keys: consumed (44-byte stride) and dropped. Nothing else in the engine reads scale.
* After all sequences: if not compressed -> RemoveQuaternionFlips + CalcTotalTime (0x4D5149/0x4D5150).

### 1.4 Keyframe formats in memory (and on disk for ANP2/ANP3)

| type | size | layout |
|---|---|---|
| KeyFrame (type 1) | 0x14 | `float q[4] @0 (x,y,z,w); float time @0x10` |
| KeyFrameTrans (type 2) | 0x20 | `float q[4] @0; float time @0x10; float pos[3] @0x14` |
| KeyFrameCompressed (type 3) | 0x0A | `int16 q[4] @0 (x4096); int16 time @8 (x60)` |
| KeyFrameTransCompressed (type 4) | 0x10 | `int16 q[4] @0; int16 time @8; int16 pos[3] @0xA (x1024)` |

Evidence: CAnimBlendSequence::Uncompress 0x4D0D40 (`psVar3[-2..1]` quat *1/4096 (0x858BFC), `[2]` time *1/60
(0x859044 = 0.0166667), trans-variant `[-3]` time, `[-2..0]` trans *1/1024 (0x85C6D4)); CAnimBlendNode::Update
0x4D06C0 reads time at `[ecx+0x10]`, pos at `[ecx+0x14..0x1c]`; UpdateCompressed 0x4D08D0 reads time at `[ebp+8]`.

**Time semantics:** on disk the `time` field is the ABSOLUTE time of the keyframe (seconds; 1/60 s ticks
when compressed). `CalcTotalTime` (0x4CF2F0) / `CalcTotalTimeCompressed` (0x4CF3E0) convert in place:
`totalTime = max over sequences of frames[n-1].time`, then for i = n-1 .. 1: `frames[i].time -= frames[i-1].time`.
Frame 0 is never converted — its stored value stays as its absolute time and is only consumed on loop
wrap-around in NextKeyFrame (0x4D04A0: `rem += frames[0].time` after `idx = 0`). So frame 0 must be at t = 0.
Uncompressed anims are converted at load; compressed ones on first `CAnimBlendHierarchy::Uncompress`
(0x4CF5F0, guarded by `totalTime == 0.0f`) or `CalcTotalTimeCompressed` from UncompressAnimation 0x4D41C0
for `m_bKeepCompressed` anims.

### 1.5 How a sequence is bound to a bone

Binding happens when an association is built for a clump: CAnimBlendStaticAssociation::Init 0x4CEC20
(static copies made by CreateAssociations for animgrp groups) and CAnimBlendAssociation::Init 0x4CED50
(script/cutscene AddAnimation(clump, hier)). Both loops are identical:

```
numNodes = clumpData->m_NumFrames          // one node per bone/frame of the CLUMP, not per sequence
for each seq in hier:
    if (seq->flags & 0x10)  fd = RpAnimBlendClumpFindBone(clump, (int16)seq->boneId)      // 0x4CEDC3: movsx from *(int16*)seq
    else                    fd = RpAnimBlendClumpFindFrameFromHashKey(clump, seq->hash)
    if (fd && seq->numFrames > 0)  nodes[(fd - clumpData->m_pFrames)/0x18].sequence = seq   // 0x4CEE07
```

* Bone-id path (0x4D6400 + callback 0x4D63E0): compares against `AnimBlendFrameData.boneTag` (+0x14), which
  RpAnimBlendClumpInitSkinned 0x4D6510 fills from `RpHAnimHierarchy->pNodeInfo[i].nodeID` (stride 0x10,
  `*(local_318 + hier+0x10)`). I.e. **the IFP bone id must equal the HAnim node ID stored in the DFF**, not
  the bone index. Non-skinned clumps have boneTag = -1, so bone-id sequences never bind to them.
* Name path on a skinned clump (0x4D6370 -> 0x4D6310): for every bone, `ConvertBoneTag2BoneName(nodeID)`
  (0x4D56F0, fixed table: 0 Root,1 Pelvis,2 Spine,3 Spine1,4 Neck,5 Head, 6/7 brows, 8 Jaw, 21..26 L arm,
  31..36 R arm, 41..44 L leg, 51..54 R leg, 201 Belly, 301 L Breast, 302 R Breast) then
  `GetUppercaseKey(name) == seq->hash`. Bones whose nodeID is not in that table have no name and can only be
  reached by bone id. The DFF's own frame names are NOT used for skinned clumps.
* Name path on a non-skinned clump (0x4D6340): `GetUppercaseKey(GetFrameNodeName(frame)) == seq->hash`.
* A sequence whose bone/frame is not found is **silently dropped** (node stays NULL; every update loop
  tests `node->sequence != 0`, e.g. FrameUpdateCallBackSkinned 0x4D2B90 `*(int *)(this + 0x10) != 0`).
* Two sequences resolving to the same bone: the later one overwrites the node slot (no check).
* `SetBoneTag` (0x4D0C70) writes `*(int16*)this = boneId` — it **overwrites the low 16 bits of the name
  hash**; `CAnimBlendHierarchy::FindSequence` 0x4CF290 compares the full dword, so name lookups of a
  bone-tagged sequence fail. Harmless for peds, but a sequence must be either name- or id-addressed.

---------------------------------------------------------------------------------------------------

## 2. Runtime structures

```c
// CAnimManager statics
CAnimBlendHierarchy ms_aAnimations[2500]  @ 0xB4EA40, stride 0x18   (0xB4EA40 + 2500*0x18 == 0xB5D4A0)
int   ms_numAnimations                     @ 0xB4EA2C
CAnimBlock ms_aAnimBlocks[180]             @ 0xB5D4A0, stride 0x20   (0xB5D4A0 + 180*0x20 == 0xB5EB20)
int   ms_numAnimBlocks                     @ 0xB4EA30
CLinkList<CAnimBlendHierarchy*> ms_animCache @ 0xB5EB20  (50 links: `operator_new(600)` / 0xC, Initialise 0x5BF6B0)
AnimAssocDefinition ms_aAnimAssocDefinitions[145] @ 0x8AA5A8, stride 0x30 (cap 0x91 from AddAnimAssocDefinition 0x4D3BA0)
int   ms_numAnimAssocDefinitions           @ 0xB4EA28  (118 built-in + animgrp.dat)
CAnimBlendAssocGroup* ms_aAnimAssocGroups  @ 0xB4EA34  (new[] of ms_numAnimAssocDefinitions, stride 0x14)

struct CAnimBlock {                 // 0x20
    char   name[16];                // +0x00  strncpy(…,16): NOT NUL-terminated if >= 16 chars
    uint8  bIsLoaded;               // +0x10
    uint8  pad;                     // +0x11
    int16  usageCount;              // +0x12
    int32  firstAnimIdx;            // +0x14  index into ms_aAnimations
    int32  numAnims;                // +0x18
    int32  groupId;                 // +0x1C  GetFirstAssocGroup(name) (118 = none)
};

struct CAnimBlendHierarchy {        // 0x18   (ctor 0x4CF270)
    uint32 hashKey;                 // +0x00  CKeyGen::GetUppercaseKey(name)
    CAnimBlendSequence* sequences;  // +0x04  new CAnimBlendSequence[n]  (count word at -4)
    int16  numSequences;            // +0x08
    uint8  bRunningCompressed;      // +0x0A
    uint8  bKeepCompressed;         // +0x0B  always 0 from the loader
    int32  animBlockId;             // +0x0C  index into ms_aAnimBlocks
    float  totalTime;               // +0x10  0 until CalcTotalTime
    CLink* cacheLink;               // +0x14  ms_animCache link (NULL = not uncompressed-cached)
};

struct CAnimBlendSequence {         // 0x0C   (ctor 0x4D0C10: hash=0xFFFF, flags=0, n=0, frames=NULL)
    union { uint32 hashKey; int16 boneId; };   // +0x00
    uint16 flags;                   // +0x04  1 rotation(always) | 2 hasTranslation | 4 compressed
                                    //        | 8 externalMemory(ANP3 block / hierarchy block) | 0x10 usesBoneId
    int16  numFrames;               // +0x06
    void*  frames;                  // +0x08  see 1.4
};

struct CAnimBlendNode {             // 0x18   (Init 0x4CFB70)
    float theta, invSinTheta;       // +0x00,+0x04   CalcTheta 0x4D00E0
    int16 currentKF, prevKF;        // +0x08,+0x0A
    float remainingTime;            // +0x0C
    CAnimBlendSequence* seq;        // +0x10  NULL = bone not animated by this anim
    CAnimBlendAssociation* assoc;   // +0x14
};

struct AnimBlendFrameData {         // 0x18
    uint8 flags;                    // +0 (bit1 ignoreRot, bit2 ignoreTrans, bit3 hasVelocity, bit4 3dVel, bit6 compressed)
    CVector pos;                    // +4
    RpHAnimBlendInterpFrame*/RwFrame* frame; // +0x10
    uint32 boneTag;                 // +0x14  HAnim nodeID, -1 for non-skinned
};

struct CAnimBlendStaticAssociation { // 0x14
    void* vtbl; int16 numBlendNodes(+4); int16 animId(+6); uint16 flags(+8);
    CAnimBlendSequence** sequences(+0xC); CAnimBlendHierarchy* hier(+0x10);
};
struct AnimAssocDefinition {        // 0x30
    char groupName[16]; char blockName[16]; int32 modelIndex(+0x20, 7 = MALE01 for animgrp.dat);
    uint32 numAnims(+0x24); char** animNames(+0x28, each 24 bytes); AnimDescriptor* descs(+0x2C, 8 bytes each);
};
```

Hash: `CKeyGen::GetUppercaseKey` 0x53CF30 — case-insensitive one-at-a-time hash (names are matched
case-insensitively; different names may collide).

---------------------------------------------------------------------------------------------------

## 3. Input conditions that crash or produce garbage (engine does NOT check)

Legend: **CRASH** = access violation / heap corruption / hang, **GARBAGE** = wrong data silently used.

### 3.1 Sequence with 0 frames — CRASH (ANPK) / GARBAGE (ANP3)
`CAnimBlendHierarchy::CalcTotalTime` 0x4CF2F0: `iVar5 = numFrames - 1; kf = frames + iVar5*0x14 (or 0x20);
if (totalTime < *(float*)(kf+0x10)) totalTime = …` — index -1 is not checked.
* ANPK, `numFrames == 0`: no KFRM read (`if (iStack_fc != 0)`), `SetNumFrames` never called, `frames == NULL`
  (ctor). Uncompressed load -> read at `0xFFFFFFFC` -> **CRASH at load**. Compressed load (normal case) ->
  same read happens on first use inside `CAnimBlendHierarchy::Uncompress` 0x4CF5F0 (`totalTime == 0` ->
  RemoveQuaternionFlips + CalcTotalTime) -> **CRASH on first play**.
* ANP3, `numFrames == 0`: `frames` = cursor into the block (non-NULL), the read hits 4 bytes *before* the
  sequence data (previous sequence's last field or the heap header) -> **GARBAGE totalTime** (may be huge).
* Binding side is safe: `Init` skips `numFrames <= 0` (0x4CEDE7). FindKeyFrame/SetupKeyFrameCompressed
  return false for `numFrames < 1`.
=> Exporter: never emit a sequence with 0 frames (drop it).

### 3.2 Animation with 0 sequences — CRASH on unload / on use
`m_pSequences = new CAnimBlendSequence[0]` = a 4-byte allocation + 4 (non-NULL).
* `CAnimBlendHierarchy::RemoveAnimSequences` 0x4CF8E0: `if (sequences) { flags = *(sequences+4); if (flags&8)
  Free(*(sequences+8)) }` — reads sequence[0] of an empty array (heap bytes past the allocation); if bit 3
  happens to be set -> `CMemoryMgr::Free(garbage)` -> **CRASH when the IFP is unloaded** (RemoveAnimBlock
  0x4D3F40 / RemoveLastAnimFile 0x4D3ED0 -> Shutdown 0x4CF980).
* `CAnimBlendHierarchy::Uncompress` 0x4CF5F0 / `CompressKeyframes` 0x4CF6C0 / `RemoveUncompressedData` 0x4CF760:
  same unconditional `*(sequences+4)` read, then `Free(sequences[0].frames)` -> **CRASH on first use** if the
  ANP3 header `flags&1` was 1 (the first-sequence override cannot run with 0 sequences).
=> Exporter: never emit an animation with 0 sequences.

### 3.3 ANP3 `frameDataSize` smaller than the keyframe data — CRASH (heap overflow)
0x4D52D5 `Malloc(frameDataSize)`; each `RwStreamRead(seq->frames, numFrames*kfSize)` (0x4D544B etc.) writes
at the running cursor with no bound. Sum of `numFrames*kfSize` > `frameDataSize` -> heap corruption ->
**CRASH later** (random). Larger size is only a leak of the slack. `frameDataSize == 0` -> `Malloc(0)` -> NULL
-> every `SetNumFrames` mallocs privately but the sequences still carry flag 8 -> leak + Uncompress frees
seq[0]'s private buffer as if it were the block (no crash, memory leak).
Retail files: `frameDataSize == sum(numFrames*kfSize)` exactly (keyframe bytes only, excluding the 36-byte
sequence headers).

### 3.4 ANP3 `frameType` not in 1..4 — GARBAGE + CRASH
Default of the switch at 0x4D5411: no `SetNumFrames`, no read, flag 8 set. The keyframe bytes remain in the
stream -> every following header is misparsed (GARBAGE names/counts -> huge `numSequences` -> `new` of
gigabytes / reads past EOF). The sequence itself has `frames == NULL, numFrames == 0` -> 3.1 crash in
CalcTotalTime if the anim is uncompressed.

### 3.5 ANPK KFRM tag not KRTS/KRT0/KR00 while `numFrames != 0` — GARBAGE
0x4D4C4A..0x4D4C6E: none matched -> `SetNumFrames` skipped -> frames not consumed -> stream desync as in 3.4.

### 3.6 Mixed compressed/uncompressed sequences inside one animation — GARBAGE / heap over-read
The hierarchy's `bRunningCompressed` comes from the FIRST sequence only (0x4D53E4). CalcTotalTime (uncompressed
walker, stride 0x14/0x20) applied to 10/16-byte compressed frames, or `CAnimBlendSequence::Uncompress` applied to
float frames, reads/writes with the wrong stride: reads past the sequence (past the block for the last sequence)
and writes deltas into the wrong words. Playback then feeds int16 pairs to the float path -> NaN / exploding
bones. ANP3 header `flags` is irrelevant except for 3.2.
=> Exporter: all sequences of an anim must be the same class (1/2 or 3/4).

### 3.7 Non-monotonic / negative / all-equal keyframe times — HANG or GARBAGE
Times on disk are absolute; `CalcTotalTime` makes deltas without any sign check.
* Negative deltas: `FindKeyFrame` 0x4D0240 loop `while (t > kf.dt) { t -= kf.dt; idx++ ... }` and
  `NextKeyFrame` 0x4D04A0 `do { idx++ (wrap if looping); rem += kf.dt } while (rem <= 0)` — with a looping
  association (assoc flags +0x2E bit1) and a period whose delta sum is <= 0 the loop never exits ->
  **HANG (100% CPU)**. Same in NextKeyFrameCompressed 0x4D0570.
* All frames at the same time (all deltas 0, e.g. every key at t=0): same hang for looping anims
  (NextKeyFrame: `rem += 0` forever). Non-looping anims just finish immediately.
* First frame at t != 0: frames[0].time stays absolute (never converted) and is added at every loop
  wrap-around -> GARBAGE hold of that length at the loop seam; FindKeyFrame starts at index 1 so the frame-0
  pose is skipped.
* `totalTime` (max of last absolute times) is what CAnimBlendAssociation uses for looping/finishing;
  sequences shorter than the longest simply hold their last key (non-looping) or wrap (looping).
* Zero delta between two frames is SAFE: every division by `kf.dt` is guarded (`Update` 0x4D078E
  `fcomp 0.0 ... t = 0`, `UpdateCompressed` 0x4D09C8 `test ax,ax`, `GetCurrentTranslation` 0x4CFCD9).
=> Exporter: times strictly increasing, first key at 0, last key > 0 for looping anims.

### 3.8 int16 quantisation limits (apply to ANP3 types 3/4 AND to every ANPK loaded by the game, since
`loadCompressed == true` everywhere)
* time: `int16(time*60 + 0.5)` -> absolute time must be < 546.1 s (32767/60); `_ftol` returns int32 and the
  store is `MOV word ptr` (0x4D4F57) -> wraps silently -> GARBAGE order / negative deltas (3.7 hang).
  Resolution 1/60 s: keys closer than that collapse into the same tick (delta 0 = jump).
* translation: `int16(pos*1024)` -> |pos| < 32.0 units or wrap -> GARBAGE positions (0x4D4D79/0x4D4F0F).
* quaternion: `int16(q*4096)` -> |component| < 8 (fine for unit quats; a non-unit quat > 8 wraps).
* Re-compression on cache eviction (`RemoveUncompressedData` -> `CompressKeyframes` 0x4D0F40) quantises the
  *delta* times the same way; deltas > 546 s wrap.

### 3.9 Counts stored as int16
* `numFrames` -> `*(int16*)(seq+6)` (SetNumFrames 0x4D0D2E). > 32767: ANPK loop `0 < (short)n` skips the
  frame reads -> desync (3.5); ANP3 reads `n*kfSize` bytes with the full uint32 into the block (3.3 applies)
  then every walker sees a negative count -> sequence ignored / CalcTotalTime index `n-1` negative -> reads
  far before the buffer -> GARBAGE/CRASH.
* `numSequences` -> `*(int16*)(hier+8)` while the allocation and read loop use the int32 (0x4D52FF/0x4D5305).
  > 32767 -> negative count -> anim silently empty; `new(n*0xC+4)` with a huge n -> bad_alloc/NULL -> the
  loop then writes through `sequences == NULL + offset` -> **CRASH**.
* ANP3 `numAnims` is the loop count; ANPK uses `block->numAnims` (0x4D4925/0x4D5230) which equals the file's
  value unless the block already existed with a different count (3.10).

### 3.10 Block name collisions / mismatch with the IMG entry — CRASH
Streaming registers a block per `*.ifp` IMG entry (`RegisterAnimBlock(entryName)` 0x4D3E50, strncpy 16,
`numAnims = 0`). At load `GetAnimationBlock(internalName)` (0x4D3945, `_stricmp` 0x8229B6):
* internal name != IMG stem (case-insensitive): a NEW block is appended (`ms_numAnimBlocks++`, 0x4D51C7).
  The registered slot keeps `numAnims = 0, bIsLoaded = 0`. `CreateAnimAssocGroups` 0x4D3CC0 finds the
  registered slot first (`bIsLoaded == 0` -> skipped) so the animgrp group is never created; a later
  `CAnimManager::AddAnimation(clump, group, id)` 0x4D3AA0 -> `CopyAnimation` 0x4CE130 with
  `m_pAssociations == NULL` -> `UncompressAnimation(*(NULL + id*0x14 + 0x10))` -> **CRASH**. Also every extra
  block eats one of the 180 slots (3.11). Note the internal name is truncated to 16 by strncpy and compared
  against a 16-byte field that is not NUL-terminated when the name is >= 16 chars.
* internal name equal to an already loaded block with `numAnims != 0` (e.g. a cutscene IFP named "ped", or a
  second IMG entry with the same internal name): the loader reuses `firstAnimIdx` of the old block and
  **overwrites** its hierarchies (old `sequences` pointers dropped without delete -> leak; ANP3 path loops
  over the file's `numAnims` while the block keeps the old `numAnims`, so extra anims spill into the next
  block's range). For cutscenes `RemoveLastAnimFile` then pops the wrong (last) block and rewinds
  `ms_numAnimations` to its `firstAnimIdx` -> subsequent loads overwrite live anims -> GARBAGE/CRASH.
=> Exporter/validator: internal block name == file stem, <= 15 chars, unique.

### 3.11 Static array overflow — CRASH
* `ms_aAnimations[2500]`: no check in either path (`ms_numAnimations` only updated at 0x4D559D). Anim #2500
  overwrites `ms_aAnimBlocks[0]` ("ped" block header) at 0xB5D4A0 -> **CRASH**.
* `ms_aAnimBlocks[180]`: `RegisterAnimBlock` 0x4D3E50 and both LoadAnimFile paths increment without a check;
  block #180 overwrites `ms_animCache` at 0xB5EB20 -> **CRASH** on the next UncompressAnimation. Streaming
  also has exactly 180 IFP model ids (25575..25754), so > 180 `.ifp` entries in all IMGs is fatal anyway.
* `ms_aAnimAssocDefinitions[145]`: `AddAnimAssocDefinition` 0x4D3BA0 writes entry `ms_num…` unconditionally
  (only the trailing NUL is guarded by `< 0x91`); 118 are built in -> **at most 27 groups in animgrp.dat**.
* ms_animCache: 50 uncompressed anims at once; the 51st evicts the oldest (RemoveUncompressedData). Not a
  crash, but a hard cap on simultaneously playing distinct compressed anims.

### 3.12 Missing animation named in animgrp.dat — CRASH
`CAnimBlendAssocGroup::CreateAssociations(name, clump, names[], n)` 0x4CE6E0: for each non-empty name
`hier = GetAnimation(name, block)` (0x4D42F0 -> 0x4D39F0 linear hash scan, NULL if absent) then
`CAnimBlendStaticAssociation::Init(clump, hier)` 0x4CEC20 -> `cmp word ptr [ebx+8], si` with `ebx == NULL`
(0x4CEC60) -> **CRASH** right after the IFP is streamed in (CreateAnimAssocGroups runs at 0x40CA84).
Also `AddAnimToAssocDefinition` 0x4D3C80 scans for the first empty name slot with no bound: more names in a
group than the declared count -> writes past the `char*[]` -> CRASH; names > 23 chars overflow the next
24-byte name slot.

### 3.13 ANPK section body larger than 260 bytes — CRASH
`RwStreamRead(stream, &local_118, roundedSize)` for INFO/NAME/ANIM bodies. `local_118` is at frame offset
-0x118, the SEH record (`pvStack_14`) at -0x14: 260 bytes of room. Bigger -> overwrites the exception chain
pointer, saved EBP and the return address -> **CRASH on return**. NAME bodies without a NUL: the hash reads
stale stack bytes -> GARBAGE hash (never matches).

### 3.14 Unnormalised / degenerate quaternions — GARBAGE (no crash)
* RemoveQuaternionFlips 0x4D1190 only flips sign on `dot < 0`, no normalisation.
* CalcTheta 0x4D00E0: `dot = a.b; if (dot > 1) dot = 1; theta = acos(dot)` — `dot < -1` is NOT clamped but
  cannot occur after flips; `theta == 0 -> invSin = 0` and Slerp 0x59C300 copies `from`. Non-unit
  quaternions therefore interpolate with wrong weights; the final `CQuaternion::Normalise` 0x4D1610
  (`len2 == 0 -> w = 1`, otherwise `1/sqrt`) hides magnitude but not the wrong blend.
* NaN/Inf components: `fcomp` unordered -> no clamp -> `acos(NaN)` -> NaN bone matrix -> mesh vanishes /
  explodes (no exception; FPU exceptions are masked).
* (0,0,0,0) quaternion -> Normalise turns it into identity -> pose snaps.
=> Exporter: normalise every quaternion, reject NaN.

### 3.15 Translation keys on non-root bones — GARBAGE if not intended
`FrameUpdateCallBackSkinned` 0x4D2B90: for any node whose sequence has flag 2 the blended translation
replaces the bone's bind position (`pos = blend*trans + (1-blend)*bonePos`, `*(pbVar5 & 4) == 0`). A KRT0 /
type-2/4 sequence on a non-root bone with (0,0,0) translation collapses that bone onto its parent. There is
no "root/translation flag without data" state — the presence of translation IS the flag (bit 2 set by
SetNumFrames from the keyframe type). Root translation with `hasVelocity` (frame flag 8) is extracted as
ped velocity.

### 3.16 Bone ids that do not exist in the skeleton / wrong skeleton — silently ignored
See 1.5: unmatched sequences are dropped, no error. An IFP made for skeleton A played on skeleton B animates
only the ids both share; custom bone ids (not in ConvertBoneTag2BoneName) MUST use the bone-id field
(ANP3 boneId != -1 or ANPK 44-byte ANIM) because the name path only knows the 30 standard ped bone names.
Bone ids are compared as `movsx int16` (0x4CEDC3) vs `uint32 nodeID` -> ids >= 0x8000 never match.

### 3.17 Duplicate names / hashes — silently ignored
* Two anims with the same hash in one block: `GetAnimation(hash, block)` 0x4D39F0 returns the first;
  the second is unreachable (AnimId-based lookups in a group still index by position, so animgrp order
  binds the first match).
* Two sequences for the same bone: last one wins (3.16 / 1.5).
* Names are hashed case-insensitively; `Name` vs `NAME` collide.

### 3.18 Names without NUL inside the 24-byte ANP3 fields — GARBAGE
`GetUppercaseKey` runs to the first NUL; a 24-char name continues into the adjacent stack dwords
(`numSequences` etc.) -> hash depends on stack garbage. Retail files happen to contain uninitialised bytes
after the NUL (harmless). Keep names <= 23 chars, NUL-padded. Block name additionally <= 15 (strncpy 16).

### 3.19 Truncated file / short reads
`RwStreamRead` return values are ignored everywhere (the ANPK path sums them into `uStack_238` but never
uses it). A truncated IFP leaves keyframes uninitialised -> GARBAGE (ANP3: heap garbage; ANPK: stale stack).

---------------------------------------------------------------------------------------------------

## 4. Exact limits

| item | limit | source |
|---|---|---|
| animations total (all loaded blocks) | 2500 | `ms_aAnimations` 0xB4EA40..0xB5D4A0, stride 0x18 |
| anim blocks (IFP files incl. "ped") | 180 | `ms_aAnimBlocks` 0xB5D4A0..0xB5EB20, stride 0x20; streaming ids 25575..25754 |
| assoc group definitions | 145 total, 118 built in -> 27 from animgrp.dat | 0x4D3BA0 `< 0x91` |
| uncompressed anims cached at once | 50 | Initialise 0x5BF6B0 `new(600)/0xC` |
| block name | <= 15 chars (strncpy 16; compared with _stricmp; must equal IMG file stem) | 0x4D51D1 / 0x4D3E50 |
| ANP3 anim / sequence name | <= 23 chars + NUL (24-byte field, hashed to NUL) | 0x4D5271 / 0x4D5382 |
| ANPK section body (INFO/NAME/ANIM) | <= 260 bytes incl. NUL, size rounded to 4 | stack frame of 0x4D47F0 |
| frames per sequence | 1 .. 32767 (int16), >= 2 for a looping anim with duration | SetNumFrames 0x4D0D2E |
| sequences per anim | 1 .. 32767 (int16); effectively <= bones in the clump | 0x4D52FF |
| keyframe absolute time | 0 .. 546.1 s (int16 ticks of 1/60 s) when compressed; first key at 0 | 0x4D4F46 (`*60 + 0.5`) |
| translation component | -32.0 .. +31.999 (int16/1024) when compressed | 0x85C6D8 |
| quaternion component | -8 .. +8 (int16/4096); should be unit | 0x858BF8 |
| ANP3 frameDataSize | == sum(numFrames * {20,32,10,16}[type]) (bigger only wastes) | 0x4D52D5 |
| frameType | 1,2,3,4 only; all sequences of one anim in the same class | 0x4D5411 |
| ANPK KFRM tag | KRTS / KRT0 / KR00; scale ignored | 0x4D4BF0.. |
| ANPK ANIM section size | 40 (no bone id) or 44 (bone id honoured) | 0x4D4BA8 |
| bone id | int16 range, must equal DFF HAnim nodeID; -1 = by name | 0x4D0C70 / 0x4CEDC3 |
| anims per animgrp group | == declared count; names <= 23 chars | 0x4D3C80 |

---------------------------------------------------------------------------------------------------

## 5. Recommended validator hook

**Hook `CAnimManager::LoadAnimFile` 0x4D47F0 itself** (cdecl, 3 stack args, no register args; callers do
`push 0; push 1; push edi; call; add esp,0xC`). Wrap it (naked pushad/popad stub per the inuhook rules, then
call a C validator after the original returns):

1. Before calling the original: snapshot `ms_numAnimBlocks` (0xB4EA30), `ms_numAnimations` (0xB4EA2C) and
   `bIsLoaded` (+0x10) of every `ms_aAnimBlocks[i]`.
2. After it returns: the loaded block is the one whose `bIsLoaded` went 0 -> 1 (exactly one block gets
   `pcVar5[0x10] = 1`). If `ms_numAnimBlocks` grew, the file's internal block name did not match any
   registered block (3.10) — report the mismatch (the streaming caller has the IFP slot in ESI - 0x63E7 at
   0x40CA7C if you prefer a call-site hook; the entry hook does not need it).
3. Enumerate `for (i = block->firstAnimIdx; i < block->firstAnimIdx + block->numAnims; ++i) hier = &ms_aAnimations[i]`.
   All data is final at this point: hash, `numSequences`, each sequence's `flags/numFrames/frames/boneId`.
   Note the time representation: uncompressed anims are already converted to deltas (CalcTotalTime ran) and
   flip-fixed; compressed anims (ANP3 type 3/4) still hold raw absolute int16 ticks. Validate accordingly:
   uncompressed -> `frames[0].time == 0`, every delta >= 0, sum > 0 for anims that will loop; compressed ->
   ticks strictly increasing from 0.
4. Since the wrapper returns before `CreateAnimAssocGroups` (0x40CA84 / 0x4D5661), also check every
   `ms_aAnimAssocDefinitions[g]` (g < ms_numAnimAssocDefinitions, `_stricmp(def.blockName, block->name) == 0`)
   that each non-empty `def.animNames[k]` hashes to an existing hierarchy in the block — otherwise the game
   will crash in `CAnimBlendStaticAssociation::Init` (3.12). Abort/patch before returning to the caller.
5. Global limits at the same point: `ms_numAnimations <= 2500`, `ms_numAnimBlocks <= 180`,
   `ms_numAnimAssocDefinitions <= 145`.
6. To walk every loaded animation at any time: `for b in 0..ms_numAnimBlocks-1: if ms_aAnimBlocks[b].bIsLoaded:
   for a in firstAnimIdx..+numAnims: &ms_aAnimations[a]`. Bone binding can only be verified against a clump
   (`RpAnimBlendClumpInitSkinned` fills `boneTag` from the DFF); do it at `CAnimBlendAssociation::Init`
   0x4CED50 / `CAnimBlendStaticAssociation::Init` 0x4CEC20 if per-model coverage is wanted (count sequences
   that got no node).

Why not a mid-function hook: the block pointer/index lives in different stack slots per path
(`[ESP+0x38]` / `[ESP+0x54]` on ANPK vs `[ESP+0x4c]` on ANP3 at 0x4D5591) — fragile. The entry wrapper +
`bIsLoaded` diff works for all three callers (startup ped.ifp, streaming, cutscenes).

---------------------------------------------------------------------------------------------------

## 6. Exporter-side checklist (INU_Tools)

1. ANP3 preferred: quaternions raw (ANPK must be conjugated: negate x,y,z).
2. `frameDataSize` == exact keyframe byte count (current writer includes the 36-byte sequence headers ->
   over-allocates, harmless; never under-allocate). Header `flags` is ignored (first sequence decides); the
   writer emitting 0 where retail has 1 is harmless as long as `numSequences > 0`.
3. Per animation: >= 1 sequence, all sequences same class (all 1/2 or all 3/4).
4. Per sequence: 1..32767 frames; times absolute, first == 0, strictly increasing, last < 546 s; <= 23-char
   NUL-terminated name; boneId == DFF HAnim nodeID (or -1 with a standard bone name for stock peds).
5. Quaternions unit length, no NaN; translation |x,y,z| < 32 (compressed) and only on bones meant to move.
6. Block name == output file stem, <= 15 chars, unique; anim names unique per block (case-insensitive).
7. Respect 2500 anims / 180 blocks / 27 animgrp groups; animgrp names <= 23 chars and exactly the declared
   count; every animgrp name must exist in the IFP.
8. ANPK: NAME/INFO/ANIM sections <= 260 bytes, NUL-terminated strings, ANIM size 44 to carry bone ids, KFRM
   tag only KRTS/KRT0/KR00, omit KFRM when numFrames == 0 (but rather drop the sequence).

## 7. Open questions

* `CAnimBlendHierarchy::Uncompress` keeps re-running CalcTotalTime for anims whose `totalTime` is genuinely 0
  (single-pose anims) — harmless as far as seen (deltas of zeros), not traced through every playback path.
* Behaviour of `CMemoryMgr::Malloc(0)` (NULL vs minimal block) not read; affects only 3.3's `frameDataSize == 0`.
* `CKeyGen::GetUppercaseKey` 0x53CF30 body not re-verified here (standard SA one-at-a-time hash assumed).
* The cutscene caller and `ms_aUncompressedCutsceneAnims` handling were taken from gta-reversed, not from the
  obfuscated CCutsceneMgr code.
