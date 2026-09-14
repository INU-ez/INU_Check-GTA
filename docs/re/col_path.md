# COL path — gta_sa.exe 1.0 US: what the collision loaders read, check, and trust

Scope: `.col` archives streamed from IMG (ids 25000+), the `COLFILE` route in gta.dat/default.dat, and the
per-model parsers `CFileLoader::LoadCollisionModel` (COL1) / `Ver2` / `Ver3` / `Ver4`, plus everything downstream
that consumes the parsed `CColModel` without re-checking it (`CColStore`, `CCollision`, `CEntity`, `CStencilShadows`).
Evidence: Ghidra dumps in `E:\RE\asm_map\<addr>.c/.asm`, capstone disassembly of the functions Ghidra mis-bounded
(`LoadCollisionModel`, `LoadCollisionModelVer2`, `CColStore::LoadCollision`, `AddColSlot` which is relocated to the
SecuROM region 0x1563290), and a scan of all 251 vanilla `.col` files (`E:\RE\addon_check\col\scan_cols.py`).
gta-reversed names are used for readability only; every claim points at the exe.

Check ids use the prefix `COL-`.

---

## 0. Pipeline (who calls what)

```
boot: CFileLoader::LoadLevel 0x5B9030
  IDE files ................................ CModelInfo entries (name hash only, no col yet)
  first "IPL" line:
    CObjectData::Initialise 0x5B5360 ......... object.dat
    CColStore::LoadAllBoundingBoxes 0x4113D0 -> CColStore::LoadAllCollision 0x410E60
        for slot 1..254: RequestModel(25000+slot), LoadAllRequestedModels, RemoveModel
          -> CStreaming::ConvertBufferToObject 0x40C6B0 (ids 25000..25254)
             -> CColStore::LoadCol(slot, buf, sectors<<11) 0x4106D0
                -> slot range still inverted (end<start)  => CFileLoader::LoadCollisionFileFirstTime 0x5B5000
                -> otherwise                              => CFileLoader::LoadCollisionFile 0x538440
                   -> per model: LoadCollisionModel 0x537580 | Ver2 0x537EE0 | Ver3 0x537CE0 | Ver4 0x537AE0 (0x538440 only)
          -> RemoveModel -> CColStore::RemoveCol 0x410730 -> CColModel::RemoveCollisionVolumes 0x40F9E0
             (frees CCollisionData, keeps the CColModel header: bbox, sphere, flags, colSlot)
  IPL files: CFileLoader::LoadObjectInstance 0x538090 grows ColDef.m_Area from each entity's bound rect
  CIplStore::LoadAllRemainingIpls, CColStore::BoundingBoxesPostProcess 0x410EC0 (area += 120 m, quadtree insert)
  CColStore::RemoveAllCollision 0x410E00
gta.dat/default.dat "COLFILE 0 x.col": CFileLoader::LoadCollisionFile(filename, 0) 0x5B4E60 -> slot 0 "generic", never streamed out
runtime: CColStore::LoadCollision 0x410865 (per frame) -> quadtree ForAllMatching(player pos) -> request/remove slots
DFF collision plugin (vehicles): CCollisionPlugin -> LoadCollisionModelVer3 with the same parser (not covered here)
```

`CColAccel` (0x5B2C20 `addCacheCol`, 0x5B2CC0 `cacheLoadCol`, 0x5B31A0 `startCache`, MODELS\CINFO.BIN) is **dead in 1.0 US**:
the only store to its state word `0xBC40A0` is `mov [0xBC40A0], esi(=0)` at 0x5B2C0B inside `endCache`; `addCacheCol`
and `cacheLoadCol` test for state 1/2 and are no-ops. The FirstTime pass therefore runs on every boot.

---

## 1. On-disk layout as the parsers read it

### 1.1 Per-model file header (all versions) — 32 bytes

| off | size | field | parser use |
|----:|-----:|-------|------------|
| 0x00 | 4 | fourcc `COLL` / `COL2` / `COL3` / `COL4` | 0x538440: all four (0x4C4C4F43, 0x324C4F43, 0x334C4F43, 0x344C4F43). **0x5B5000 (FirstTime) and 0x5B4E60 (COLFILE) accept only COLL/COL2/COL3; any other fourcc (incl. COL4, zero padding, junk) ends the whole file** (`return 1`). |
| 0x04 | 4 | size = bytes following this field (24 + payload) | next model = this + 8 + size. Never checked against the buffer (see COL-05). `size-0x18` is handed to Ver2/3/4 as data size. |
| 0x08 | 22 | model name, expected NUL-terminated | copied 22 bytes into a 24-byte stack local (5 dwords + 1 word at 0x5384C0/0x5B5045), hashed with `CKeyGen::GetUppercaseKey` 0x53CF30 which scans **until NUL** → 22 non-NUL chars read into uninitialised stack bytes. Vanilla: bytes after the NUL are garbage (e.g. `Lae2_roads59\0\0\0Ae\0…`), so only the NUL matters. |
| 0x1E | 2 | model id (uint16) | hint only: if `< 20000` and `ms_modelInfoPtrs[id]` non-NULL and its `m_nKey == hash(name)` it is used; otherwise name search (0x538440: `GetModelInfo(name, slot.start, slot.end)` 0x4C5A20 — range-limited; 0x5B5000: `GetModelInfo(name,&id)` 0x4C5940 — whole table, starting from the last search position, forward then backward). |

Match requirement (both loaders): model info exists **and** `CBaseModelInfo+0x12 bit 0x80` ("owns col model", set by `Init` 0x4C4B10 (`mov word [ecx+0x12],0xC0`) and by `SetColModel(x,true)`; cleared by `SetColModel(x,false)` — weapons 0x5B3FB0, peds 0x5B7420, and the *other* time-object of a pair, 0x4C4BC0). Otherwise the entry is skipped silently (FirstTime still calls `IncludeModelIndex` when the model exists).

### 1.2 COL1 (`COLL`) payload — `LoadCollisionModel` 0x537580 (byte-packed, sequential)

| order | bytes | field | what the parser does |
|------|------:|-------|---------------------|
| 1 | 4+12 | bounding sphere: radius, center | → CColModel+0x24 (radius), +0x18 (center) |
| 2 | 24 | bbox min, max | → CColModel+0x00/+0x0C |
| 3 | 4 | numSpheres (int32) | read as **int16** (`mov ax,[esi]`, `cmp ax,bx / jle`): ≤0 → no spheres and **data not skipped**; stored in `CCollisionData+0`. Each sphere 20 B: radius f32, center 3×f32, surface{material, flag, unk, light} → `CColSphere::Set(r, c, [+0x10], [+0x11], [+0x13])` 0x40FD10 (material→+0x10, flag→+0x11, light→+0x12; +0x13 left uninitialised). |
| 4 | 4 | numLines | only the **low byte, signed** is used (`mov dl,[esi]; test al,al; jle`): 1..127 → skips n×24 bytes; ≥128 or 0 → nothing skipped. Lines are always discarded (`numLines=0`, `m_pLines=NULL`). |
| 5 | 4 | numBoxes | int16 signed, same pattern; 28 B each: min, max, surface → `CColBox::Set` (material [+0x18], flag [+0x19], light [+0x1B]). |
| 6 | 4 | numVertices (int32) | `Malloc(n*6)`; each f32×3 → `*128 → _ftol → int16` (truncation, wraps beyond ±255.99, see COL-13). **The count is not stored anywhere** (CCollisionData has no vertex count). |
| 7 | 4 | numFaces | int16 signed; 16 B each: a,b,c int32 (only the low 16 bits are stored: `mov bp,word [esi]`), material [+0xC], flag [+0xD] ignored, unk [+0xE] ignored, light [+0xF]. |

After the faces: `[data+7] &= ~4` (bHasShadow off; bHasFaceGroups/bUsesDisks are already 0 from the `CCollisionData` ctor 0x40F030 `and dl,0xF8`), shadow ptrs/counts = 0, `m_pTrianglePlanes = 0`, `m_bHasCollisionVolumes |= (numSpheres|numBoxes|numTriangles) != 0` (0x537968…0x537979).
`CCollisionData` is `operator new(0x30)` (CRT) + per-array `CMemoryMgr::Malloc` → "multiple alloc" (CColModel flag bit 1 stays 0).
Nothing is validated: no size check, no index check, no bounds check.

### 1.3 COL2 payload — `LoadCollisionModelVer2` 0x537EE0 (header 0x4C, then raw blob)

Header copied with `rep movsd` (0x13 dwords):

| off | size | field | parser use |
|----:|-----:|-------|-----------|
| 0x00 | 12 | bbox min | → CColModel+0x00 |
| 0x0C | 12 | bbox max | → CColModel+0x0C |
| 0x18 | 12 | sphere center | → CColModel+0x18 |
| 0x24 | 4 | sphere radius | → CColModel+0x24 |
| 0x28 | 2 | numSpheres | → CCollisionData+0 (uint16, no check) |
| 0x2A | 2 | numBoxes | → +2 |
| 0x2C | 2 | numFaces | → +4 (consumers treat it as **signed int16**, see COL-11) |
| 0x2E | 1 | numLines | → +6 (kept but dead: `bUsesDisks` is forced 0, `and byte [eax+7],0xFE`) |
| 0x2F | 1 | pad | ignored |
| 0x30 | 4 | flags | bit1 (value 2) → CColModel+0x29 bit0 `m_bHasCollisionVolumes`; bit3 (value 8) → CCollisionData+7 bit1 `bHasFaceGroups` (`shr al,2; and 2`); bit0 cleared; bit4 (16) **ignored**; other bits ignored |
| 0x34 | 4 | offSpheres | ptr = block + off − 0x38, or NULL if 0 |
| 0x38 | 4 | offBoxes | same |
| 0x3C | 4 | offLines | same (→ +0x10, dead) |
| 0x40 | 4 | offVertices | same → +0x14 |
| 0x44 | 4 | offFaces | same → +0x18 |
| 0x48 | 4 | offTrianglePlanes | **ignored**, +0x1C forced NULL |

Offsets are relative to `fourcc+4` (the `size` field): the first data byte after the COL2 header is file offset 0x64
from the fourcc, and the engine maps `off → block+0x30+(off−0x68)`; verified on 500 vanilla COL2/COL3 models
(`oS_ok_base+4 500`, no mismatch). Zero offset means NULL pointer regardless of the count.

Allocation: if `dataSize−0x4C == 0` the function returns after the bounds copy (no CCollisionData, `m_pColData`
untouched). Otherwise `block = CMemoryMgr::Malloc(dataSize − 0x4C + 0x30)`; the whole payload after the header is
`memcpy`'d to `block+0x30`; block[0..0x30) becomes the `CCollisionData`. `m_pTrianglePlanes = NULL`,
shadow fields = 0, `CColModel+0x29 |= 2` (single alloc → freed with one `CMemoryMgr::Free`).
No previous `m_pColData` is freed (leak on duplicate entries, COL-24).

Face groups: not touched by the loader. Consumers (`CCollision::ProcessColModels` 0x418B23,
`SphereCastVsEntity` 0x41A1AA) read `uint32 nGroups = *(pTriangles − 4)` and `TFaceGroup groups[] = pTriangles − 4 − 28*n`
(each: bbox min/max 24 B, `int16 first, last` inclusive) **only if flag 8 is set** — the layout `[verts][groups][count][faces]`
is implicit and unchecked.

### 1.4 COL3 payload — `LoadCollisionModelVer3` 0x537CE0 (header 0x58)

COL2 header + 
| 0x4C | 4 | numShadowFaces | → CCollisionData+0x20 (uint32, unconditionally) |
| 0x50 | 4 | offShadowVerts | ptr = block + off − 0x44 (block copy starts at file 0x58), NULL if 0 |
| 0x54 | 4 | offShadowFaces | same → +0x2C |

`bHasShadow` (CCollisionData+7 bit2, value 4) is set **iff** `offShadowFaces && offShadowVerts && numShadowFaces > 0`.
File flag 16 is never read. `numShadowVertices` (+0x24) is not in the file: it is computed by `FUN_00537510` as
`max(index over all shadow faces) + 1` — reading every shadow face unchecked. Data block = `Malloc(dataSize − 0x58 + 0x30)`
(Ghidra: `param_2 − 0x28` from the 0x18-adjusted size), payload copied from file offset 0x58.

### 1.5 COL4 — `LoadCollisionModelVer4` 0x537AE0: COL3 + one ignored u32 (header 0x5C, block = size−0x5C+0x30, ptr rebase −0x48).
Only reachable via 0x538440; the boot pass rejects it (COL-04).

### 1.6 What each parser validates vs assumes (summary)

Validated: nothing beyond "buffer has ≥ 9 bytes left" and "fourcc known". Assumed: `size` correct; counts consistent
with data; offsets inside the block and non-zero when the count is non-zero; vertex indices < number of vertices;
face groups contiguous, in-range and preceded by their count; coordinates within ±255.99; surface ids < 179;
bounding box/sphere enclosing the geometry; name ≤ 21 chars + NUL; one entry per model; model exists in an IDE.

---

## 2. In-memory layouts (offsets verified in the loaders/consumers)

### CColModel — 0x30 bytes, pool `CPools::ms_pColModelPool` @0xB744A4, **10150 entries** ("ColModel", `push 0x27A6` @0x551106)

| off | type | field | notes |
|----:|------|-------|-------|
| 0x00 | CVector | m_boundBox.min | used by `CEntity::GetBoundRect` 0x534120 (sector insertion, ColDef area) |
| 0x0C | CVector | m_boundBox.max | |
| 0x18 | CVector | m_boundSphere.center | `CEntity::GetIsOnScreen` 0x534540 (render culling), `ProcessColModels` broad phase |
| 0x24 | float | m_boundSphere.radius | |
| 0x28 | u8 | m_nColSlot | written by the loaders after parsing (`p1[0x28] = colId`) |
| 0x29 | u8 | flags: bit0 `m_bHasCollisionVolumes`, bit1 `m_bIsSingleColDataAlloc`, bit2 `m_bIsActive` | ctor 0x156C690 (via 0x40FB60): `(flags & ~3) | 4`, slot 0, data NULL |
| 0x2C | CCollisionData* | m_pColData | NULL-checked by every `CCollision::*` entry (0x41864A, 0x1564C49, 0x41798A, 0x417BF8) |

`operator new` 0x40FC30 → `CPool::New` 0x40FB80: **returns NULL when the pool is full**; none of the three loaders checks it.
`RemoveCollisionVolumes` 0x40F9E0: bit1 set → `RemoveTrianglePlanes` + `CMemoryMgr::Free(block)`; else
`CCollisionData::RemoveCollisionVolumes` + CRT `free`; then `m_pColData = NULL`. The CColModel object itself is never
returned to the pool by the streaming path.

### CCollisionData — 0x30 bytes (first 0x30 of the single block for COL2/3/4)

| off | type | field |
|----:|------|-------|
| 0x00 | u16 | m_nNumSpheres |
| 0x02 | u16 | m_nNumBoxes |
| 0x04 | u16 | m_nNumTriangles (read with `movsx` in 0x40F593, 0x418C3B, 0x418C74) |
| 0x06 | u8 | m_nNumLines |
| 0x07 | u8 | bit0 bUsesDisks (always 0 from files), bit1 bHasFaceGroups, bit2 bHasShadow |
| 0x08 | CColSphere* | m_pSpheres (20 B each: center 12, radius 4, material, flags, lighting, light) |
| 0x0C | CColBox* | m_pBoxes (28 B: min 12, max 12, material, flags, lighting, light) |
| 0x10 | CColLine*/CColDisk* | m_pLines (dead for files) |
| 0x14 | CompressedVector* | m_pVertices (3×int16, /128 → 0x858B88 = 0.0078125) — **no count field** |
| 0x18 | CColTriangle* | m_pTriangles (8 B: a,b,c u16, material u8, light u8) |
| 0x1C | CColTrianglePlane* | m_pTrianglePlanes (10 B: normal 3×int16 (/4096, 0x858BFC), distance int16 (×128), orientation u8) — lazily built by `CCollision::CalculateTrianglePlanes` 0x416330 through a 50-entry LRU (`CCollision::Init` 0x416260: 600/12 links) |
| 0x20 | u32 | m_nNumShadowTriangles |
| 0x24 | u32 | m_nNumShadowVertices (computed) |
| 0x28 | CompressedVector* | m_pShadowVertices |
| 0x2C | CColTriangle* | m_pShadowTriangles |

Hidden face-group block (COL2/3 only): `uint32 count` at `m_pTriangles−4`, `TFaceGroup[count]` (28 B: bbox min, max, `int16 first`, `int16 last`) growing downwards from `m_pTriangles−4`.

### ColDef — 0x2C bytes, pool `CColStore::ms_pColPool` @0x965560, **255 entries** ("CollisionFiles", `Initialise` 0x4113F0), slot 0 = "generic"

| off | field | notes |
|----:|-------|-------|
| 0x00 | CRect m_Area (left, bottom, right, top) | init `+1e6, −1e6, −1e6, +1e6` (inverted), grown by `CRect::Restrict` 0x404200 (= union) in `LoadObjectInstance` 0x5383F7 for entities whose col has volumes and slot ≠ 0; `BoundingBoxesPostProcess` 0x410EC0 expands by 120 m then `CQuadTreeNode::AddItem` 0x552CD0 |
| 0x10 | char name[18] | **never written in vanilla** (AddColSlot 0x1563290 does no strcpy) |
| 0x22 | i16 m_nModelIdStart | init 0x7FFF; `IncludeModelIndex` 0x410820 |
| 0x24 | i16 m_nModelIdEnd | init −0x8000 |
| 0x26 | u16 m_nRefCount | `CObject` AddRef/RemoveRef |
| 0x28 | bool m_bActive | set by `LoadCol` on success |
| 0x29 | bool m_bCollisionIsRequired | per-frame |
| 0x2A | bool m_bProcedural | name ∈ {`procobj`, `proc_int`, one obfuscated third string — gta-reversed: `proc_int2`} (stricmp) → never unloaded |
| 0x2B | bool m_bInterior | name starts with (strnicmp) `int_la`, `int_sf`, `int_veg`(7), `int_cont`, `gen_int1`, `gen_int2`, `gen_int3`, `gen_int4`, `gen_int5`, `gen_intb`, `savehous`, `levelmap`, exact `props`, `props2`, plus one obfuscated 7-char prefix (string table has `stadint` next to them) → loaded only when the player's area code ≠ 0 (`SetIfCollisionIsRequired` 0x4103D0) |

Quadtree: root rect (−3000, +3000, +3000, −3000), depth 3 (8×8 cells of 750 m). `AddItem` recurses only into
children whose quadrant overlaps the rect (`InSector` 0x5526A0); a rect entirely outside ±3000 (or still inverted)
is **never inserted**. `ForAllMatching(pos)` 0x5529F0 uses `FindSector(pos)` 0x552640 = −1 outside the root → nothing.

---

## 3. Store / streaming behaviour that matters for a file

* Slot creation: `CStreaming::LoadCdDirectory` 0x5B6170 → every IMG entry with extension `.COL` → `AddColSlot(name)`;
  no duplicate check by name (gta-reversed marks the vanilla `FindColSlot()` as a stub) → two same-named `.col` files
  = two slots. `AddColSlot` does not check the pool: 255 − 1 (generic) − **251 vanilla files = 3 free slots**.
* FirstTime pass (boot, once per slot): finds the model, `IncludeModelIndex(slot, id)` (range grows even when the model
  does not own its col), allocates a **new** CColModel per matched entry (never checks for an existing one), parses,
  `SetColModel(mi, cm, true)`.
* Runtime loads (0x538440): reuse `mi->m_pColModel`, or allocate one if NULL (then the range search
  `GetModelInfo(name, start, end)` must find the model). Existing data is not freed before parsing.
* `RemoveCol(slot)`: for ids in `[start,end]`, frees volumes only if `mi owns col && cm->m_nColSlot == slot`.
* `LoadCollision` 0x410865 per frame: clears required flags, quadtree walk with the player position (and 20 m × vehicle
  velocity look-ahead), interior rule, plus the "collision needed" entity list (0xA90850, 75 entries) and
  `ms_vecCollisionNeeded`; slots with `required || procedural || refcount>0` are requested (`RequestModel(25000+i, 0x18)`),
  everything else active is `RemoveModel`'d. Slot 0 is never touched.
* Entity side (`LinkLods` 0x5B51E0 / `LoadObjectInstance` 0x538090): a LOD instance with exactly one child gets the
  child's `CColModel*` (`SetColModel(cm,false)` at 0x5B52DD); an entity whose model has no col model is skipped
  there but crashes in `CWorld::Add` → `CEntity::Add` 0x533020 → `GetBoundRect` 0x534131 (`mov eax,[ecx+0x14]; mov edx,[eax]`).
  Vanilla: 4106 text-IPL-placed models have no COL entry and all are single-child LODs.
* The COL bounding box drives `CEntity::GetBoundRect` (world sectors, col-slot area); the COL bounding sphere drives
  `CEntity::GetIsOnScreen` 0x534540 (visual culling of the DFF!) and the `ProcessColModels` broad phase
  (`TestSphereSphere` on the two bounding spheres before any primitive is looked at).

---

## 4. Conditions that crash or produce garbage

Consequence codes: **crash** / **garbage** / **silently-ignored**. "Where" = the unchecked instruction(s).

| id | condition | consequence | where / evidence |
|----|-----------|-------------|------------------|
| COL-01 | CColModel pool full (10150; ~9958 used on a stock install with the map loaded — every matched entry of every `.col` costs one for the life of the process) | crash | `CColModel::operator new` 0x40FC30 → 0x40FB80 returns NULL; 0x5B5000: `p1 = NULL` then `LoadCollisionModelVer2(…, NULL, …)` writes `[ebx+0x18]` at 0x537F0D; 0x538440: `SetColModel(mi, NULL, true)` then parser writes `[ebp+0x24]` at 0x5375A6. No NULL check in any loader. |
| COL-02 | More than 254 `.col` files across all IMGs (255-entry ColDef pool incl. "generic"; vanilla uses 251) | crash at start-up | `AddColSlot` 0x1563290 (thunk 0x411140): `call 0x411030` (CPool::New, NULL when full) then `mov byte [esi+0x28], al` with esi = NULL. |
| COL-03 | Model name field has no NUL within 22 bytes (name ≥ 22 chars) | silently-ignored (hash of garbage; nondeterministic) | 0x5384C0 copies 22 bytes into a 24-byte local; `CKeyGen::GetUppercaseKey` 0x53CF30 loops `test al,al / jne` past the copy. |
| COL-04 | `COL4` entry (or any unknown fourcc / junk / non-zero padding) anywhere in a `.col` file | silently-ignored: that entry **and every entry after it** are skipped in the boot pass (never in the slot range → later runtime loads can't find them either) → their IPL instances then hit COL-27 | 0x5B5000 accepts only 0x324C4F43/0x334C4F43/0x4C4C4F43 and `return 1` otherwise; same in 0x5B4E60 (which additionally keeps the stale `local_30` version). |
| COL-05 | `size` field wrong: larger than the remaining buffer, or smaller than 24+payload | garbage (parser walks into the next model's middle / past the buffer; stops at the first unknown fourcc), possible crash reading the next 8-byte header past the streaming buffer | 0x538440 loop: only `if (param_2 < 9) return`, then `param_2 += -8 - size` (unsigned wrap), `param_1 = data + size - 0x18`. |
| COL-06 | COL2/3/4 `size` < 24+header (0x4C/0x58/0x5C) | crash | Ver2: `uVar5 = dataSize − 0x4C` wraps, `Malloc(huge)` → NULL, `rep movsd` to NULL+0x30 at 0x537F77 and reads past the buffer. Ver3/Ver4 identical (`param_2 − 0x58` / `− 0x5C`). |
| COL-07 | Count > 0 with offset 0 (e.g. numFaces>0, offFaces=0) | crash on first collision test | Loader stores NULL for a zero offset (0x537FEC…0x53805A); `CCollisionData::CalculateTrianglePlanes` 0x40F5B7 does `lea edx,[ecx+edi*8]` with ecx = m_pTriangles = NULL → `CColTrianglePlane::Set` 0x411660 reads it. Spheres/boxes: `ProcessColModels` indexes `[ebx+8]`/`[ebx+0xC]` unchecked. |
| COL-08 | Offset points outside the model's payload (`off < 0x68` or `off + count*stride > size+8`) | garbage/crash | pointers are `block + off − 0x38` with no range check (0x537FE3); consumers dereference them. |
| COL-09 | Face vertex index ≥ number of vertices actually stored (any version; COL1 truncates 32-bit indices to 16 bits, so index ≥ 65536 wraps) | garbage (planes/normals from whatever follows the vertex array), crash if the read leaves the heap | `CColTrianglePlane::Set` 0x411660: `p1 + idx*6` for a,b,c with no bound; `TestLineTriangle` 0x413AC0 / `ProcessLineTriangle` 0x4140F0 / `TestSphereTriangle` 0x4165B0 same. There is no vertex count in `CCollisionData` to check against. |
| COL-10 | More than 65535 vertices (COL2/3/4) | garbage (indices wrap, geometry unreachable) | `CColTriangle` indices are u16 (0x537903 `mov bp, word [esi]` for COL1; raw copy for COL2+). |
| COL-11 | numFaces ≥ 32768 (any version) | garbage (all triangles silently ignored by every line/sphere test) → crash when face groups are present or the allocator rejects the size | count treated as **signed int16** everywhere: `CCollisionData::CalculateTrianglePlanes` 0x40F593 `movsx eax,[esi+4]; inc; *10 → CMemoryMgr::Malloc(negative)`, plane loop skipped (`cmp word [esi+4],di; jle`); `ProcessColModels` 0x418C3B `cmp ax,si; jle`, `TestLineOfSight` 0x1564DFE/0x1564E68, `ProcessLineOfSight` 0x417AEE/0x417B6E, `ProcessVerticalLine` 0x417D9E/0x417DF2 all `movsx`/signed; the face-group path 0x418BD5 then dereferences the NULL plane array. COL1: `cmp ax,bx; jle` stores the raw count (0x5378DE `mov word [edi+4],dx`) with `m_pTriangles = NULL` and still sets `m_bHasCollisionVolumes`. |
| COL-12 | COL1: numSpheres or numBoxes ≥ 32768, or numLines outside 1..127 with line data present | garbage | 0x537621/0x5376B4 signed 16-bit compares, 0x53768F signed 8-bit compare; when the count is judged ≤ 0 the parser does not advance `esi` → every following section is read from the wrong place. |
| COL-13 | Vertex coordinate outside ±255.99 (COL1 floats get `*128 → _ftol → int16`; COL2/3 store int16 directly) | garbage (coordinate wraps by 512 m; whole faces land elsewhere) | 0x53774A…0x53775C (`fmul 128.0 @0x858BF4; call _ftol; mov word [ebp],ax`). Vanilla max |v| = 255.02 (`sbedsfn1_SFN`). |
| COL-14 | Per-face plane distance |n·v0| ≥ 256 (a face far from the model origin along its normal; possible even with all coords inside ±255.99, e.g. a diagonal face near a corner, |v| up to 443) | garbage (plane distance wraps → line/sphere tests miss or hit at the wrong depth) | `CColTrianglePlane::Set` 0x411852: `dot * 128.0 → _ftol → mov word [esi+6]`; read back with `movsx` (0x413ADE). |
| COL-15 | Face group flag (8) set but no `[groups][count]` block precedes the faces, or `count` garbage | garbage (reads groups from the vertex bytes; huge count walks before the heap block → crash) | 0x418B2D `mov edx,[ebx+0x18]; mov eax,[edx-4]`; groups at `pTriangles − 0x20 − 0x1C*i`. Loader only copies the flag (0x537FC1). |
| COL-16 | Face group `last ≥ numFaces`, `first > 32767`, or `first/last` not covering the faces | `last≥numFaces`: reads triangles **and planes** past their arrays (garbage/crash, planes array is exactly numFaces×10 B). `first>last`: group skipped. Faces not covered by any group: never collide with moving entities (silently-ignored) but still block lines of sight (line tests iterate all faces). | 0x418BC4 `movsx esi,[edi+0x18]; movsx eax,[edi+0x1A]; cmp esi,eax; jg`, then `[edx+esi*8]`, `[ecx+esi*10]` unchecked. Same shape at 0x41A1AA (SphereCastVsEntity). |
| COL-17 | Face group bbox not enclosing its faces | silently-ignored (faces outside the group bbox never collide with vehicles/peds/objects; line-of-sight still works) | 0x418B41…0x418BC2: sphere-vs-group-bbox reject before any triangle of the group is tested. Vanilla: 0 violations in 10 155 models. |
| COL-18 | Surface/material id > 178 (table has 179 entries, `SurfaceInfos_c` 0x8F4 bytes = 0x90 + 179×12) | garbage (friction/flags/audio read from the bytes after the table: `IsSeeThrough`, `IsWater`, tyre grip…) | `SurfaceInfos_c::GetAdhesionGroup` 0x55E5C0, `GetTyreGrip` 0x55E5E0, `IsSeeThrough` 0x55E6B0 …: `lea eax,[eax+eax*2]; mov …[ecx+eax*4+0x94]` — no bound. Vanilla max = 178 (`des_trainline09`). |
| COL-19 | Flag 2 ("not empty") clear while spheres/boxes/faces are present (COL2/3/4) | silently-ignored: every IPL instance of the model gets `SetUsesCollision(false)` at load, and slot area is not grown | Ver2 0x537F34…0x537F4A copies bit1 into CColModel+0x29 bit0; `LoadObjectInstance` 0x5383C9 `test byte [eax+0x29],1 → and dword [esi+0x1C],~1`. Vanilla: 0 such models. |
| COL-20 | Flag 2 set but the entry is header-only (size = 0x18+header) or all counts 0 | silently-ignored for collision (`m_pColData` stays NULL / empty), but the entity keeps `m_bUsesCollision` and the slot area grows → cheap but useless. Not a crash: all `CCollision::*` entries NULL-check `m_pColData` (0x41864A/0x418662, 0x1564C49, 0x41798A, 0x417BF8). | Ver2 early `return` at 0x537F4D when `dataSize − 0x4C == 0`. |
| COL-21 | Shadow mesh with `numShadowFaces > 0` but `offShadowVerts`/`offShadowFaces` = 0 | silently-ignored (`bHasShadow` cleared, count kept) — harmless; **file flag 16 is irrelevant either way** | Ver3 0x537E5C…0x537EAE; consumers (`CStencilShadows::RegisterStencilShadows` 0x7118B8) test bit 4 of +7 first. |
| COL-22 | Shadow face index ≥ shadow vertices actually stored | garbage (numShadowVertices = max index + 1 is derived from the faces, so the vertex loop in `CStencilShadows::RenderForObject` 0x710310 / `RenderForVehicle` 0x70FAE0 (`GetShadTrianglePoint` 0x40F640 at 0x70FCDE/0x7104BE) reads past the array) | `FUN_00537510` scans faces with no vertex bound; `GetShadTrianglePoint` `[this+0x28] + id*6`. |
| COL-23 | Bounding box / sphere not enclosing the geometry (or radius ≤ 0 / NaN) | garbage: (a) DFF **visually culled** while on screen (`GetIsOnScreen` 0x534540 uses CColModel+0x18/+0x24); (b) entity registered in too few world sectors (`GetBoundRect` 0x534120 uses +0x00/+0x0C) → no collision / line-of-sight in the uncovered part; (c) `ProcessColModels` sphere-sphere broad phase rejects contacts outside the sphere; (d) col-slot area too small → slot not streamed near the object. | see cited functions; nothing recomputes the bounds. Vanilla: sphere sometimes smaller than the bbox corners (4488 models; it encloses the geometry, not the box). |
| COL-24 | Same model name twice in one `.col` file, or in two `.col` files | garbage/leak: FirstTime allocates a second CColModel (pool leak, COL-01 sooner); runtime reloads overwrite `m_pColData` without freeing (heap leak on every stream-in); `RemoveCol` frees only when `m_nColSlot` matches the unloading slot, so the volumes of the "other" file stay resident or go missing depending on load order. Vanilla: 0 duplicates. | 0x5B5000 always `operator new`; 0x537F53 mallocs unconditionally; 0x410730 `cmp byte [this+0x28], slot`. |
| COL-25 | Entry name matches no IDE model, or matches a model that does not own its col (weapon/ped, the second model of a time pair, any model that got `SetColModel(x,false)`) | silently-ignored (entry skipped; for time pairs the *other* model shares the day model's col) | 0x538490…0x5384F8; `test byte [this+0x12],0x80`. IDE names are searched by uppercase CRC → case-insensitive; duplicate IDE names resolve to whichever `GetModelInfo` 0x4C5940 hits first from its last position. |
| COL-26 | Entry matched only through the `modelId` field with a **different** name (id hint) | not possible: the hint is validated against the name hash (0x5384D6 `cmp key`), name always wins. Wrong id = harmless. | |
| COL-27 | An IPL-placed model has no col model at all (no matching COL entry, and not a LOD with exactly one child) | crash at scene load | `CEntity::GetBoundRect` 0x534131 dereferences `mi->m_pColModel` (NULL) from `CEntity::Add` 0x53302F ← `CWorld::Add` ← `LinkLods` 0x5B5348. Single-child LODs inherit the child's pointer at 0x5B52DD. |
| COL-28 | `.col` file name starts with an interior prefix (`int_la`, `int_sf`, `int_veg`, `int_cont`, `gen_int1..5`, `gen_intb`, `savehous`, `levelmap`, exact `props`/`props2`, probably `stadint`) while its objects are outdoors (area 0) | silently-ignored: never streamed while the player is in area 0 | `SetIfCollisionIsRequired` 0x4103D0: `ms_nRequiredCollisionArea == 0 && m_bInterior → return`. |
| COL-29 | Col-slot area outside ±3000 (all instances outside), or no IPL instance references the slot (area stays inverted: script-only objects), or the player is outside ±3000 | silently-ignored by position streaming: never inserted in / never matched by the quadtree; only `m_bProcedural`, `m_nRefCount` (CObject creation) or the "collision needed" list load it | `AddItem` 0x552D20 `InSector` loop; `ForAllMatching` 0x552A1A `FindSector == -1`. `CEntity::Add` 0x5347D0 clamps entity rects to [−3000, 2999]. |
| COL-30 | `COLFILE` entry (gta.dat/default.dat, slot 0) whose model payload exceeds 32 KB (0x8000 static buffer @0xBC40D8, followed by `gCurrIplInstancesCount` @0xBCC0D8) | crash / garbage IPL state | 0x5B4E60: `CFileMgr::Read(f, 0xBC40D8, size − 0x18)` with no limit. |
| COL-31 | `.col` file larger than the streaming buffer | not an issue: `ms_streamingBufferSize` is raised to the largest IMG entry at directory load (LoadCdDirectory); vanilla max 215 040 B (`sfs_7.col`). Files are still read in 2048-byte sectors, so the buffer beyond `size` is padding; the parser stops at the first zero fourcc (COL-04 semantics). | |
| COL-32 | COL1 entry re-loaded into a CColModel whose previous data was single-alloc (e.g. same model in a COL2 file in another slot) | crash/heap corruption | COL1 loader never clears CColModel+0x29 bit1; `RemoveCollisionVolumes` then `CMemoryMgr::Free`s a CRT-`new`ed CCollisionData (0x40F9FA) and leaks the arrays. |
| COL-33 | Box with min > max, or zero-size box | silently-ignored (never intersects) / fine (vanilla has 21 zero-thickness boxes) | box tests compare against min/max; no normalisation. |
| COL-34 | Dense col meshes: more than 599 triangles (or 0x2F boxes / spheres caps in the same function) intersect an entity's bounding sphere in one test | silently-ignored contacts (extra triangles dropped) | `ProcessColModels` 0x418C03/0x418C6C `cmp eax,0x257 / jge` on the collected-triangle counter. |
| COL-35 | object.dat: more than 400 *distinct* parameter lines (5 defaults + 395 unique) | garbage/crash (table `ms_aObjectInfo` overflows, 0x50 B per entry) | `CObjectData::Initialise` 0x5B5360: `uStack_224` only compared for de-duplication, never against 400. Names that match no IDE model are ignored. |

Not checked by the engine but harmless: sphere/box `flags`/`unk` bytes (vanilla has 255s), the `light` byte
(day/night nibbles, any value), `numLines`/`offLines`/`offPlanes` (dead), COL2 pad byte, file flag 16.

---

## 5. Limits (hard numbers)

| what | limit | source |
|------|------:|--------|
| `.col` files (slots) | 254 usable (255 − generic); **vanilla uses 251 → 3 free** | ColDef pool 0xFF @0x4113F0; streaming ids 25000..25254 (0x40C6B0 `< 0x62A7`) |
| CColModel objects | 10150 total, ~192 free on stock (9958 used) | pool @0xB744A4 |
| model entry name | ≤ 21 chars + NUL (22-byte field) | COL-03 |
| model id hint | u16, < 20000 to be used | 0x5384AE |
| spheres / boxes per model | u16 in file; COL1 parser treats counts as int16 (≤ 32767) | COL-12 |
| faces per model | **≤ 32767** (signed int16 everywhere that matters) — the addon currently allows 65535 | COL-11 |
| vertices per model | ≤ 65536 addressable (u16 indices); COL1 count int32 but indices truncated | COL-09/10 |
| coordinates | int16/128 → [−256, +255.9921875]; plane distance |n·v| < 256 | COL-13/14 |
| surface id | 0..178 | COL-18 |
| face group `first/last` | int16, `last < numFaces`; count u32 read as signed | COL-15/16 |
| shadow faces / verts | u32 count; indices u16; verts derived = max index + 1 | COL-22 |
| `COLFILE` (slot 0) per-model payload | ≤ 0x8000 B | COL-30 |
| world collision extent | ±3000 m (quadtree root, sector clamp) | COL-29 |
| triangle-plane cache | 50 col datas at a time (LRU, `CCollision::Init`) — performance only | 0x416260 |
| triangles collected per `ProcessColModels` | 599 | COL-34 |
| vanilla envelope (for warnings) | max 5191 faces / 3408 verts (`kickbus04`), 160 face groups, group span ≤ 108 faces, 37 spheres, 154 boxes, 2022 shadow faces (`wires_05_SFS`), file 215 040 B | scan_cols.py |

---

## 6. Validator hook and self-test

**Primary hook: `CColStore::LoadCol(int slot, uint8* buf, int size)` 0x4106D0** (cdecl, 3 stack args). It is the single
entry for every streamed `.col` (both the boot FirstTime pass and runtime reloads go through it), it knows the slot,
and `buf` is the raw file image (2048-byte padded). Two-phase use:

1. *Pre* (before calling the original): walk the buffer with an independent parser and report COL-03…COL-22, COL-30-style
   structural faults **before** the engine dereferences anything (this is the only way to catch COL-06/07/08/15/16
   without a crash). Also resolve each name with `CModelInfo::GetModelInfo(const char*, int*)` 0x4C5940 and check
   `CBaseModelInfo+0x12 & 0x80` (COL-25) and the free count of the CColModel pool (COL-01: `CPool` @0xB744A4:
   +0 objects, +4 flag bytes (bit7 = free), +8 size, +0xC last index).
2. *Post* (after the original returns): for every entry, `mi->m_pColModel` (+0x14) → verify `m_nColSlot == slot`,
   the CCollisionData counts, that all pointers lie inside `[block, block + size − 0x4C + 0x30)` and that
   `m_bHasCollisionVolumes` agrees with the counts (COL-19/20). This confirms the engine's own view, which is what
   the addon must match.

Per-model alternatives (both have the raw model bytes, size, `CColModel*`, name):
`LoadCollisionModelVer3` 0x537CE0 / `Ver2` 0x537EE0 / `Ver4` 0x537AE0 — cdecl `(uint8* data, int dataSize, CColModel* cm, const char* name)`,
`data` points just past the 32-byte header (so header = `data − 0x20`, fourcc at `data − 0x20`, size at `data − 0x1C`,
id at `data − 2`). Hooking `Ver3` also catches vehicle DFF-embedded collision (CCollisionPlugin). `LoadCollisionModel`
0x537580 is `(uint8* data, CColModel* cm, const char* name)` and its entry is an obfuscated SEH prologue
(`push -1; jmp 0x401065 … jmp 0x537587`) — hook at 0x537587 or at the call sites 0x538440/0x5B5000/0x5B4E60 instead.
Register hazard (from the inuhook notes): the callers keep live values in registers across these cdecl calls, so use naked
pushad/popad stubs.

Entity-level checks (COL-27, COL-23) need an IPL-side hook: `CFileLoader::LoadObjectInstance` 0x538090 return
(entity → `mi->m_pColModel` NULL and `m_nLodIndex == −1` → will crash in `CWorld::Add`) or `LinkLods` 0x5B51E0 before
the `CWorld::Add` loop at 0x5B5340.

**Self-test**: no extra machinery is needed for vanilla coverage — the boot pass (`CColStore::LoadAllBoundingBoxes`
0x4113D0 → `LoadAllCollision` 0x410E60) already streams every slot 1..254 through `LoadCol` once, so a hook on
0x4106D0 sees all 10 155 vanilla entries during loading. To re-run at any time call `CColStore::LoadAllCollision`
0x410E60 (cdecl, no args) from the game thread: it does `RequestModel(25000+i, 0)`, `LoadAllRequestedModels(false)`,
`RemoveModel(25000+i)` per slot — the normal (non-FirstTime) path, no CColModel allocation, no pool growth, and it
leaves the store as it was (`RemoveCol`). Do not call it while `CStreaming` is mid-load. Individual slots: request
id 25000+slot with flags 0, load, then `RemoveModel`. Slot 0 (COLFILE) cannot be re-loaded this way; hook 0x5B4E60 for it.

---

## 7. Exporter-side checks the engine will not do (INU_Tools, `core/col.py`, `ops/col_export.py`)

* Name: ≤ 21 chars (`write_str` currently truncates to 22 with no NUL → COL-03); must equal an IDE model name
  (case-insensitive) that is an `objs`/`tobj`/`anim` model; warn if the name is a weapon/ped/vehicle.
* Faces ≤ 32767 (not 65535), vertices ≤ 65536, every index < vertex count, no degenerate faces (vanilla has none).
* Every coordinate in [−256, 255.99]; every face plane distance |n·v0| < 256 (COL-14) — practically: keep the whole
  collision inside a 255.99 m sphere around the model origin.
* Surface ids ≤ 178 for SA (already clamped by `_clamp_surfaces_for_target`).
* Flags: set 2 iff any primitive exists (already), never set 8 (the addon writes no face groups; the engine does not need
  them — 2017 vanilla models with faces have no groups), never set 1; if face groups are ever added they must be
  contiguous, cover `0..numFaces−1`, `last < numFaces`, each bbox enclosing its faces, count written right before the faces.
* Shadow mesh: `numShadowFaces > 0` requires both offsets non-zero and indices < shadow vertex count (already fine:
  when absent the addon writes count 0 and end-of-data offsets → `bHasShadow` cleared).
* Bounds: bbox and sphere must enclose all spheres/boxes/vertices (already) **and should enclose the DFF's render
  geometry** — the col sphere is what culls the visual mesh (COL-23a); warn when the DFF bound sphere is larger.
* One entry per model name per file; no name shared with another `.col` in the target IMG (COL-24).
* Every IPL-placed model exported by the addon must have a COL entry unless it is a LOD with exactly one child (COL-27).
* Target IMG: count existing `.col` entries — refuse if the total would exceed 254 slots (COL-02), and warn when the
  number of new model entries approaches the free CColModel budget (~190 on stock) (COL-01).
* File name must not start with an interior prefix unless the objects are interior (COL-28); instances must lie within
  ±3000 (COL-29).
* `COLFILE` route: per-model payload ≤ 32 KB (COL-30); prefer IMG streaming.
* Write only `COLL`/`COL2`/`COL3` (never `COL4`), no junk between models, correct `size` (COL-04/05).

---

## 8. Vanilla reference numbers (scan of gta3.img + gta_int.img, `scan_cols.py`)

251 `.col` files, 10 155 model entries (885 COL2, 9270 COL3, no COL4), 0 duplicate names, max name length 19,
max model id 15 063, every name NUL-terminated; flags seen: 0 (2205), 2 (3959), 10 (3413), 16 (190, shadow-only wires),
18 (189), 26 (199); `offLines`/`offPlanes` always 0, `numLines` always 0; face groups never violate range/coverage/bbox;
vertex region padded by ≤ 12 B; sphere `r ≤ 0`: none; surface max 178; light byte up to 255.
Loose `models\coll\peds.col` is a leftover VC-layout file and is not referenced by default.dat (only `WEAPONS.COL`
is loaded via `COLFILE`; `VEHICLES.COL` is commented out).

---

## 9. Open questions

* The third `m_bProcedural` name and the last `m_bInterior` prefix are behind SecuROM-obfuscated pointer arithmetic
  (0x15632FD, 0x1563456, 0x156349D); gta-reversed says `proc_int2`, and the string table next to the others contains
  `stadint` (7 chars, matching the obfuscated strnicmp length pattern) — not executed to confirm.
* Whether `CStencilShadows` buffers (0x800 verts / 0x1000 indices per flush at 0x710C20) ever receive a shadow mesh
  larger than that in one object; the flush logic looks safe but was not traced fully.
* `numLines` from a COL2 header is stored and `m_pLines` rebased; no consumer was found that reads them when
  `bUsesDisks == 0`, but only `ProcessColModels` was inspected for it.
* Exact upper bound of the CColModel pool free count on the user's modded install (task states 9958/10150 for stock).
