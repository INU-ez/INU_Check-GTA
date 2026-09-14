# INU_Tools core writers — round-trip check against vanilla GTA SA assets

Date: 2026-09-11. Everything here was done WITHOUT Blender, using the pure-Python
format core of the addon (`F:\GitHub\INU_Tools-GTA-sa-\INU_tools\core\{dff,ifp,img,rwbinary}.py`,
repo untouched, working copy state = git status of the session). Game files were
read from `D:\Grand Theft Auto San Andreas` (read-only). All artefacts live in
`E:\RE\addon_check\`.

Scripts (reproducible):

| file | purpose |
|---|---|
| `extract.py` | pulls the test assets out of `gta3.img` / `cutscene.img` / `anim.img` with `core.img.ImgReader`, trims the 2048-byte sector padding (DFF: walks root RW chunks; IFP: 8 + header size) → `ped\*.dff`, `ifp\*.ifp` |
| `rwtree.py` | independent RenderWare chunk-tree parser + decoders (frame list, HAnim, geometry struct, skin, bin-mesh, material, atomic) — used so that the comparison does not depend on the addon's own reader |
| `check_dff.py` | read → write (two modes, see below) → byte compare → chunk-tree diff with per-field decoding → re-read with `core.dff` and compare structures; results in `dff_results.json`, written files in `out_dff\` |
| `check_ifp.py` | read → write (same format) → byte compare → independent ANP3 header/keyframe compare → re-read compare; plus a cross-format ANPK write/re-read; results in `ifp_results.json`, written files in `out_ifp\` |
| `tables.md` | the two tables below, generated from the json |

Two DFF write modes were exercised because `core.dff.read_dff` keeps raw
copies of the Geometry List and Atomic chunks (`clump.raw_geometry_list`,
`clump.raw_atomics`) and `DffClump.to_bytes` re-emits them verbatim when
present:

* **Mode A — as read.** `write_dff(read_dff(x))`. Geometry list + atomics are
  raw passthrough; only the clump struct, frame list and clump extension go
  through the writer. (The frame list is always re-serialised: the reader
  stores `_raw_frame_list` on the frames but never sets `clump.raw_frame_list`.)
* **Mode B — full re-serialisation.** Same, after clearing
  `raw_geometry_list` / `raw_atomics`, so every geometry, material, texture,
  skin, bin-mesh and atomic goes through `DffGeometry.to_bytes`,
  `DffMaterial.to_bytes`, `SkinData.to_bytes`, the atomic writer, etc. This is
  the path that matters for a real Blender export (which builds the
  structures from scratch).

---

## 1. Ped skins — 29 DFFs

Selection (all skinned, 1 geometry, 1 atomic, HAnim + Skin PLG):

* cops / military: `lapd1`, `sfpd1`, `swat`, `army`
* gang: `ballas1`
* female: `wfyst`, `bfyri`, `wfycrk` (thin, crack addict), `vwfyst1`, `wfyri`
* male street: `wmyst`, `bmyri`, `hmycm`, `male01`
* fat / old: `wmost`, `bfost`
* player-related: `player` (6-vertex CJ placeholder), `csplay` (cutscene CJ, 61 bones)
* story characters: `smoke`, `ryder`, `sweet`, `tenpen`, `cat`, `csher`
* cutscene skins from `cutscene.img` (no `csbin*` exists in this install; `csb*` are
  these): `csbigbear` (fat, 61 bones), `csbigbearthin` (thin), `csbfyst`,
  `csbmybu`, `csbettina` (56 bones)

### 1.1 Per-asset results

`A` = mode A, `B` = mode B. "re-read structural diff" = `core.dff.read_dff`
of the written file compared field-by-field with the original read (frames:
name/rot/pos/parent/flags/HAnim ids+nodes; geometry: flags, vertices, normals,
UVs, prelit, triangles, bounding sphere, materials (colour, surface props,
texture name/mask/filter, specular/reflection/env/bump/dual/uv-anim), skin
(num_bones, num_used, max_weights, bones_used, indices, weights, matrices),
extra colours, pipeline; atomics; version).

| asset | orig bytes | A bytes | A exact | B bytes | B exact | first differing byte | first differing leaf chunk | B leaf diffs besides FRAME_NAME | re-read structural diff |
|---|---|---|---|---|---|---|---|---|---|
| army.dff | 73925 | 73995 | no | 73995 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| ballas1.dff | 86549 | 86619 | no | 86619 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| bfost.dff | 66213 | 66283 | no | 66283 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| bfyri.dff | 94948 | 95018 | no | 95018 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| bmyri.dff | 80271 | 80341 | no | 80341 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| cat.dff | 87461 | 87531 | no | 87531 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| csbettina.dff | 134773 | 134905 | no | 134905 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| csbfyst.dff | 76644 | 76719 | no | 76735 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG (x2), SKIN_PLG, STRUCT | none |
| csbigbear.dff | 107779 | 107925 | no | 107925 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| csbigbearthin.dff | 111299 | 111445 | no | 111445 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| csbmybu.dff | 64713 | 64791 | no | 64791 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| csher.dff | 86517 | 86587 | no | 86587 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| csplay.dff | 12287 | 12431 | no | 12431 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| hmycm.dff | 83751 | 83821 | no | 83837 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG (x2), SKIN_PLG, STRUCT | none |
| lapd1.dff | 73477 | 73547 | no | 73547 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| male01.dff | 91430 | 91500 | no | 91500 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| player.dff | 6869 | 6937 | no | 6937 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| ryder.dff | 93318 | 93388 | no | 93388 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| sfpd1.dff | 75885 | 75955 | no | 75955 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| smoke.dff | 90507 | 90577 | no | 90577 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| swat.dff | 80606 | 80676 | no | 80676 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| sweet.dff | 88443 | 88511 | no | 88511 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| tenpen.dff | 102349 | 102423 | no | 102423 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| vwfyst1.dff | 133360 | 133430 | no | 133430 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| wfycrk.dff | 88899 | 88969 | no | 88985 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG (x2), SKIN_PLG, STRUCT | none |
| wfyri.dff | 92321 | 92391 | no | 92391 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| wfyst.dff | 85714 | 85784 | no | 85784 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| wmost.dff | 71952 | 72022 | no | 72022 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG, SKIN_PLG, STRUCT | none |
| wmyst.dff | 91822 | 91890 | no | 91906 | no | 4 (CLUMP header size field, grows with name padding) | FRAME_LIST[0]/EXTENSION[1]/FRAME_NAME[0] | BIN_MESH_PLG, BREAKABLE, MATFX_PLG (x2), SKIN_PLG, STRUCT | none |

`STRUCT` in the "B leaf diffs" column is always the MATERIAL struct (`unused` word), never the geometry, frame-list, clump or atomic struct — those were byte-identical in every file.

### 1.2 What differs, chunk by chunk, and whether it matters

Everything that changed is listed here; nothing else in the 29 files differed (the
chunk-tree diff pairs every leaf chunk by path and decodes the payload, so a
silent change in vertex data, weights, HAnim node tables, bounding spheres,
material colours etc. would have shown up).

| # | chunk | mode | what the writer does vs vanilla | classification |
|---|---|---|---|---|
| 1 | `FRAME_LIST/EXTENSION[i]/FRAME_NAME (0x253F2FE)` — every named frame (1039 chunks across the 29 files) | A and B | vanilla writes exactly `strlen(name)` bytes (no NUL, no padding: `Root` = 4 bytes, ` Pelvis` = 7); writer uses `_pad_string` → NUL-terminated and padded to a multiple of 4 (`Root\0\0\0\0` = 8, ` Pelvis\0` = 8). This is the ONLY difference in mode A and the reason every file grows (+70 bytes for a 33-frame ped) and the first differing byte is the CLUMP size word at offset 4. | **Neutral for these files.** The engine reads the chunk with `RwStreamRead(name, length); name[length] = 0` into a 24-byte plugin slot (`NodeNameStreamRead` @ 0x72FA50 — see gta-reversed `NodeName.cpp`, `NAME_LENGTH 23`, plugin size 24). NULs inside the 24 bytes are harmless (vanilla `admiral.dff` itself has NUL-padded 20-byte names). It becomes a real problem only for names of 20–23 characters — see finding W1 in section 3. Longest name in this set is 16 chars (`Bip01 R Clavicle`). |
| 2 | `GEOMETRY/MATERIAL_LIST/MATERIAL/STRUCT` — `unused` u32 at +12 | B | vanilla peds carry `4`, writer emits `0` (`pack('<II', 0, has_tex)`) | neutral: `RpMaterialStreamRead` ignores the word (RW "unused" field, `RpMaterialChunkInfo.unused`). Colour, textured flag and surface props (ambient/specular/diffuse) are byte-identical. |
| 3 | `GEOMETRY/EXTENSION/BIN_MESH_PLG (0x50E)` | B | flags (0 = tri list), split count, index count, per-split material index and index count are identical; the ORDER of the triangles inside the single split differs (writer emits them in Geometry-struct order; vanilla's bin-mesh order is an independent, cache-optimised permutation). Verified: same triangle set, same winding for every triangle, different sequence. | neutral (rendering order inside one material only; vertex-cache efficiency may differ slightly). The Geometry-struct triangle list itself (`b,a,mat,c` u16) is byte-identical. |
| 4 | `GEOMETRY/EXTENSION/SKIN_PLG (0x116)` | B | header (num_bones / num_used / max_weights / pad), used-bone table, per-vertex bone indices, per-vertex weights, the 12-byte SA trailer (boneLimit / numMeshes / numRLE = 0) and matrix elements [0..14] are byte-identical. Only element **[15]** of every inverse-bind matrix differs: vanilla stores `0.0`, writer stores `1.0` (64 bytes differ per file = 2 bytes × 32 bones). Cause: `_read_skin_plugin` forces `raw[15] = 1.0` (and `raw[3]=raw[7]=raw[11]=0.0`, which vanilla already has). | neutral: the 64-byte record is an `RwMatrix`; [3] is `flags`, [7]/[11]/[15] are `pad1/pad2/pad3`. `RpSkin` only uses the 3×4 part; pad3 is never read. (DragonFF/Kam's write 1.0 there too.) |
| 5 | `GEOMETRY/EXTENSION/BREAKABLE (0x253F2FD)` — 4-byte chunk containing `0` | B | dropped: the reader only decodes the chunk when `ecs >= 28`, so the vanilla 4-byte "not breakable" marker is lost and not re-emitted | neutral: `CBreakable` stream read returns immediately when the leading u32 is 0; absence of the chunk means the same thing. |
| 6 | `ATOMIC/EXTENSION/MATFX_PLG (0x120)` = `0` | B | ADDED on every skinned atomic (`DffClump.to_bytes`: `if geom.skin: … _chunk(0x0120, pack('<I', 0))`, mis-commented as "Node Name PLG (required for SA skinned)"). Vanilla ped atomics carry only `RIGHT_TO_RENDER (0x1F) = {0x116, 1}` — which the writer also emits, byte-identical. | neutral: `MatFXAtomicStreamRead` only calls `RpMatFXAtomicEnableEffects` for a non-zero value. Harmless 16 bytes, but it is not what vanilla has and the comment is wrong (0x120 is Material Effects PLG, not Node Name). |
| 7 | second `MATFX_PLG` = `1` on the atomic — `csbfyst`, `hmycm`, `wfycrk`, `wmyst` only | B | these 4 skins have a Reflection Material extension (`0x253F2FC`, all fields 1.0 — a Max exporter leftover) on their material; `_matfx = any(... m.reflection ...)` is therefore true and the writer emits a SECOND 0x120 chunk with value 1 right after the value-0 one. Vanilla has no MatFX flag on these atomics. | **not byte-neutral, almost certainly render-neutral, but a deviation**: the engine will call `RpMatFXAtomicEnableEffects` on a skinned ped atomic. No material carries a MatFX effect block, so the effect type stays `rpMATFXEFFECTNULL` and the skin-matfx pipeline draws the same picture as the plain skin pipeline. Still, the file now says something vanilla never says; see W3. |
| 8 | Chunk sizes of every container on the path to the changed leaves (`CLUMP`, `FRAME_LIST`, each frame `EXTENSION`, `GEOMETRY_LIST`, `GEOMETRY`, its `EXTENSION`, `ATOMIC`, its `EXTENSION`) | A and B | size words recomputed | neutral (consequence of 1–7). |

Byte-identical in every file (both modes): clump struct (atomics/lights/cameras), frame-list struct (all 9+3 floats, parent index, flags 0x20003 root / 0x3 bones), every HAnim PLG (version 0x100, node id, node count, flags 0, keyframe size 36, the whole (id, index, type) node table — e.g. army: 32 nodes, push/pop types preserved), geometry struct (flags 0x10036 / 0x10076, triangle/vertex counts, prelit, UVs, triangles, bounding sphere, has_pos/has_norm, vertices, normals), material list struct, material colour / surface props / textured flag, texture struct (filter word 0x1106 etc.), texture name and mask strings, texture extension, material extension (Reflection Material payload where present), atomic struct (frame 2, geometry 0, flags 5), Right-To-Render {0x116,1}, library IDs (0x1803FFFF everywhere), clump extension (empty).

Re-read of every written file (both modes) with `core.dff.read_dff` produced structures equal to the original read in every compared field — i.e. the writer does not lose anything the reader can see; what it loses is only what the reader already discards (bin-mesh order, material `unused`, the 4-byte breakable marker, matrix pad words).

**Verdict for all 29 skins: not byte-exact, semantically equivalent** (with the reservation on item 7 for the four Reflection-Material skins).

---

## 2. IFP — `anim\ped.ifp` + 7 IFPs from `anim.img`

All files are ANP3 (SA compressed). `write_ifp` was called with the reader's
`source_format`, so the writer used `write_anp3`. Independent ANP3 parser
(`check_ifp.parse_anp3`) compared every animation header (name bytes,
num_bones, data_size, flag), every bone header (name bytes, type, keyframe
count, bone id) and the raw int16 keyframe bytes. Second pass: re-read the
written file with `core.ifp.read_ifp` and compare animation names, bone
name/id/key_type, keyframe counts, quaternions, translations, times. Third
pass: write the same data as ANPK (float32) and re-read.

| asset | fmt | anims/bones/kf | orig bytes | written | exact | first differing byte (section) | header-field diffs | keyframe bytes | re-read structural diff (max dq / dt / dtime) | ANPK cross-write re-read (max dq/dt/dtime) |
|---|---|---|---|---|---|---|---|---|---|---|
| bikes.ifp | ANP3 | 20/543/4663 | 67744 | 67744 | no | 47 (anim 'BIKEs_Back' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 1.91e-07) |
| car.ifp | ANP3 | 11/286/7683 | 89670 | 89670 | no | 50 (anim 'Fixn_Car_Loop' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 4.45e-07) |
| colt45.ifp | ANP3 | 7/130/1966 | 25066 | 25066 | no | 53 (anim '2guns_crouchfire' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 5.56e-08) |
| dancing.ifp | ANP3 | 13/331/9319 | 122650 | 122650 | no | 44 (anim 'bd_clap' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 2.23e-07) |
| fat.ifp | ANP3 | 18/570/12915 | 153504 | 153504 | no | 44 (anim 'FatIdle' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 1.11e-07) |
| muscular.ifp | ANP3 | 17/544/9565 | 118036 | 118036 | no | 54 (anim 'MscleWalkst_armed' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 1.11e-07) |
| ped.ifp | ANP3 | 294/7607/112150 | 1433248 | 1433248 | no | 44 (anim 'abseil' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 8.9e-07) |
| swim.ifp | ANP3 | 7/212/2463 | 32958 | 32958 | no | 48 (anim 'Swim_Breast' header) | data_size, flag, name pad | identical | none (0 / 0 / 0) | none (0 / 0 / 1.11e-07) |

### 2.1 What differs

| # | field | writer vs vanilla | classification |
|---|---|---|---|
| I1 | animation `name[24]` and bone `name[24]` | vanilla has garbage after the terminating NUL (3ds Max path fragments, stack leftovers: `BIKEs_Back\0=C:\3dsmax5\0=`); writer zero-fills | neutral — `CAnimBlendHierarchy::SetName` / `CAnimBlendSequence::SetName` hash the C string up to the NUL. This garbage is the FIRST differing byte in every file (offset 44–54 = inside the first animation's name field). |
| I2 | animation header `data_size` (+28) | vanilla = bytes of KEYFRAME data only (sum over bones of kf_size × count); writer = that PLUS 36 bytes per bone (it measures from the first bone header). E.g. `BIKEs_Back`: 532 → 1468 (26 bones). | neutral for retail `gta_sa.exe`: `CAnimManager::LoadAnimFile` (0x4D47F0) does `CMemoryMgr::Malloc(data_size)` and then copies only `kf_size × count` per sequence into it, so the writer merely over-allocates 36 B per bone per animation (~270 KB for ped.ifp). Any tool that seeks by `data_size` would break, but the game does not. |
| I3 | animation header `flag` (+32) | vanilla = `1` on every animation of every file; writer = `0` (`_build_anp3_anim`: `out.extend(struct.pack('<I', 0))  # flag`) | **neutral for retail `gta_sa.exe` 1.0, NOT neutral for reimplementations** — see W6. The loader stores `flags & 1` into `hierarchy.m_bIsCompressed`, but the retail code then overrides it from the FIRST sequence's frame type (`frameType 1/2 → 0, 3/4 → 1`; decompile lines 168–176 of `exports\004d47f0.json`). Only afterwards does it run `RemoveQuaternionFlips()` + `CalcTotalTime()` when the flag is 0 — and those treat the keyframes as float structs (stride 0x14/0x20 in `CAnimBlendSequence::RemoveQuaternionFlips` @ 0x4D1190, no compressed check). gta-reversed's `LoadAnimFile_ANP23` does NOT have the override and trusts the header flag. |
| I4 | keyframe payload (int16 quaternion ×4096, u16 frame time, int16 translation ×1024) | byte-identical in all 8 files (112 150 keyframes in ped.ifp) | reader divides by 4096/1024/30, writer rounds back — exact for every representable value. **The core writer performs no quaternion normalisation**; vanilla quaternions are read and written with their native non-unit magnitudes (0.99952–1.0 in these files; `CAR_LjackedLHS` bone `fam3` id -1 even has magnitude 0.707 and survives). |
| I5 | times | ANP3: u16 frame index, exact. Cross-write to ANPK stores `frame/30` as float32 → max 8.9e-7 s error on re-read (float32 rounding of n/30), no drift on the ANP3 path. | neutral |

Structural re-read: identical (0 quaternion delta, 0 translation delta, 0 time delta, all names/ids/types/counts equal) for all 8 files.

**Verdict for all 8 IFPs: not byte-exact, semantically equivalent for the retail exe**; the header `flag=0` is a latent writer bug for other engines.

Quaternion normalisation note: normalisation exists only in the Blender-side
exporter (`ops/ifp_export.py` ~line 485): `bl_quat.normalize()` only when
`|mag-1| > 5e-3`, plus a byte-exact snap to the imported source int16 when the
value is within 4.9e-4, plus hemisphere continuity (negate if dot with previous
< 0). Consequence for a vanilla anim re-exported through Blender: the two
`CAR_LjackedLHS/fam3` keyframes (magnitude 0.707) would be re-normalised and
change bytes; everything else stays byte-exact as long as it is unedited.

---

## 3. Writer hazards found by reading the code (things the engine could reject or mis-render)

Ordered by likelihood of biting a real export. "core" = `INU_tools/core/dff.py` / `ifp.py`; "ops" = Blender-side builders that feed the core.

**W1 — FRAME_NAME padding overflows the engine's 24-byte name slot for 20–23-char names.** `DffFrame.extension_bytes` → `_pad_string(name)` writes `len+1` rounded up to 4. For a name of 20..23 characters the chunk becomes 24 bytes; `NodeNameStreamRead` (0x72FA50) does `RwStreamRead(slot, 24); slot[24] = 0` on a 24-byte `RwFrame` plugin block → one-byte out-of-bounds write into whatever plugin follows NodeName in the frame. Vanilla never emits more than `strlen` bytes (`NodeNameStreamGetSize` = `strlen`), so vanilla 23-char names are safe; the padded ones are not. 10 vanilla frames would hit this on re-export (`parachute.dff` `Para_Harness_SpineTrace`, `bar_barriergate1_dam`, `phils_compnd_gate_l0`, `doublestreetlght1_L0`, `ws_roadwarning_0[1-5]_L0`, `CJ_Monketshopsign_L0`), and any user object name of that length. Names ≥ 24 chars are truncated by nothing in the writer either (vanilla engine would overflow on those too). Fix: write `name.encode()` with no NUL and no padding (as vanilla), or at least cap at 23 and never pad past 23+1.

**W2 — Skin PLG layout switches to the pre-RW-3.5 format whenever `num_used == 0`.** `SkinData.to_bytes`: `oldver = (self.num_used == 0)` → 4 pad bytes before every matrix and NO 12-byte SA trailer (boneLimit/numMeshes/numRLE). Verified: a 2-bone SA skin with `num_used=0` serialises to 160 bytes instead of 164. Vanilla SA never has `num_used == 0` (all 488 skinned geometries in gta3.img+cutscene.img have `num_used > 0`), and the ops builder (`dff_export.py` ~1126) computes `bones_used` EXCLUDING bone 0 — so a mesh weighted 100 % to the root bone (a common test rig) gets `num_used = 0` and a skin chunk laid out for RW 3.4 inside an RW 3.6 file. Whether SA's `RpSkin` reader detects the old layout by the numUsedBones field or by stream version is not verifiable here; DragonFF's reader keys on `num_used == 0`, Kam's does not. Safer: always write the SA layout for version ≥ 0x35000 and put bone 0 into `bones_used` when it is weighted.

**W3 — MatFX flag emitted on skinned atomics / duplicate 0x120 chunks.** `DffClump.to_bytes` writes `0x120 = 0` for every skinned atomic (comment calls it "Node Name PLG", it is Material Effects PLG) and then, if any material has env/bump/dual/specular/reflection/uv-anim, a second `0x120 = 1`. Vanilla skinned atomics carry only `0x1F {0x116, 1}`. Two same-ID plugin chunks in one extension are legal for RW (second overrides) but non-vanilla; `= 1` puts a ped through `RpMatFXAtomicEnableEffects` because of a leftover all-1.0 Reflection Material (4 of the 29 vanilla skins). Render-neutral in practice, but the reflection/specular test should exclude skinned geometry (it already does for the 0x1F {0x120,0} chunk).

**W4 — HAnim node table vs frame hierarchy consistency is entirely on the ops layer.** Core writes the node list verbatim (`HAnimData.to_bytes`: version 0x100, flags 0, keyframe size 36 — all 700 vanilla SA HAnim roots use exactly these, so the constants are right). The ops builder (`_export_armature`) emits nodes in `armature.data.bones` order with `index = stored import index or j` and `bone_type = stored or 0`, and per-bone `frame.parent = stored parent index`. For a rig round-tripped from a DFF the import created bones in HAnim-node order, so it is consistent. For bones the user adds/reparents/reorders: (a) `bone_type` defaults 0 → push/pop flags no longer describe the tree → `RpHAnimHierarchyUpdateMatrices` walks the matrix stack wrong (mangled skeleton); (b) `bone_id` defaults 0 → duplicate node IDs → `RpHAnimIDGetIndex` binds animations/skin to the wrong bone; (c) stored `index` no longer equals the array position; (d) stored `parent` indices become stale. Additionally, when `dff_raw_frame_list` is present the ENTIRE frame list is replayed from import bytes (`clump.frames.clear()`), so a user who added or removed a bone gets a skin with `num_bones = len(bones)` ≠ HAnim node count of the raw frame list → skin matrices indexed past the hierarchy's matrix array. None of this can be produced from the core API alone; the core does no cross-check (`_validate_geometry_writable` only checks u8/u16 ranges — it does not compare `skin.num_bones` to the HAnim node count or to `len(bone_matrices)`, nor `len(bone_indices)` to the vertex count).

**W5 — `max_weights` is always 4 on fresh exports.** Vanilla SA skins use 4 / 3 / 2 / 1 (291/136/16/45 geometries). The D3D9 skin pipeline picks the vertex-shader variant from `maxNumWeights`; 4 is always safe (superset), so this only costs GPU work — neutral. Weights are renormalised to sum 1 and the root bone is moved to slot 3 (Kam convention) — fine. Per-vertex weights that are all zero (vertex not in any deform group) are written as index (0,0,0,0) weight (0,0,0,0) → the vertex is not moved by any bone; RW tolerates it, the vertex stays at bind pose.

**W6 — ANP3 animation header `flag` written as 0 (vanilla 1 = compressed).** As shown in I3, retail `gta_sa.exe` re-derives the flag from the first sequence's frame type, so nothing happens there; gta-reversed (and anything else that honours the header) would set `m_bIsCompressed = false` and immediately run `RemoveQuaternionFlips`/`CalcTotalTime` over int16 data using float strides — corrupting keyframes in place and reading past the malloc'd block. Also `data_size` is over-stated by 36 B/bone (I2). Write `1` and the keyframe-only size to match vanilla.

**W7 — ANPK writer emits a 48-byte `ANIM` chunk; SA only reads the bone tag from a 44-byte one.** `_build_anpk_cpan` builds `ANIM` as name[24] + 4 + num_kf@28 + 8 + bone_id@40 + (-1)@44 = 48 bytes (GTA III layout; III `ped.ifp` = 48, VC `ped.ifp` = 44). In the SA loader the ANPK branch does `if (chunkSize == 0x2C) SetBoneTag(body+40)` (decompile line 346) — with 48 bytes the bone id is ignored and sequences are matched by NAME hash against the DFF frame names. Ped IFP bone names carry the leading space (` Pelvis`) so most bones still match, but the root (`Normal` in IFP vs `Root` in the DFF) does not → root track dropped. Only matters when a user forces ANPK for SA (default is ANP3); for a SA target the chunk should be 44 bytes.

**W8 — ANP3 quantisation limits are not validated.** `int(round(q*4096))` / `int(round(t*1024))` are packed with `<h`; a translation component ≥ 32.0 units or a quaternion component ≥ 8.0 raises `struct.error` (verified) — a crash of the export, not a bad file. Time is clamped silently to 65535 frames (2184.5 s). Rotations are written as-is: a non-unit quaternion (Blender path only normalises when `|mag-1| > 5e-3`) is written non-unit; the engine slerps unnormalised keys, which is what vanilla does anyway.

**W9 — Geometry writer masks vertex indices to u16 instead of refusing.** `tri.a & 0xFFFF` etc. with only a warning for > 65536 vertices → a mesh with more vertices silently wraps indices (garbage triangles) rather than failing. Triangle count > 65536 is allowed (u32 count) — correct.

**W10 — Version-gated structure sizes.** `DffClump.to_bytes` always writes a 12-byte clump struct (numAtomics, numLights, numCameras); RW < 3.3 (GTA III DFFs, 0x31000) expects 4 bytes. `DffGeometry.to_bytes` writes constant surface props (1,1,1) for `rw_version < 0x34000` instead of the material's values. Both are III-only concerns, irrelevant for SA output.

**W11 — Material list never uses RW's material-reuse indices** (`pack('<i', -1)` for every slot). Reused materials in the source (the reader expands them into duplicates) are written as separate MATERIAL chunks. Valid, slightly larger, neutral.

**W12 — Reader-side losses that become writer-side changes** (all neutral for the engine, but they make byte-exactness impossible): bin-mesh triangle order and tri-strip flag (strips are converted to lists), material `unused` word, 4-byte Breakable marker, matrix pad words [3]/[7]/[11]/[15], HAnim hierarchy `flags`/`keyframeSize` (skipped by `_read_hanim_plugin` and re-emitted as constants 0/36 — matches all 700 vanilla SA roots, but a modded file with non-zero HAnim flags would be rewritten with 0), texture extension chunks, user-data of unknown plugins, garbage bytes after NULs in IFP name fields.

Things checked and found correct (no finding): library ID encoding (0x1803FFFF for 3.6.0.3); frame struct floats and flags; HAnim constants; skin header/used table/indices/weights/trailer; geometry flags incl. UV-count byte; prelit/UV/triangle/vertex/normal packing; bounding sphere; material colour and surface props; texture filter word written as the full 32-bit value; Right-To-Render for skinned atomics; atomic flags 5; ANP3 int16/uint16 quantisation (exact inverse of the reader); ANP3 bone header (name/type 3|4/count/id); ANPK INFO/NAME/DGAN/CPAN/KR00/KRT0 chunking and 4-byte rounding.

---

## 4. Artefacts

* `E:\RE\addon_check\ped\*.dff` — originals (sector padding trimmed), `out_dff\<name>.A.dff` / `.B.dff` — written files
* `E:\RE\addon_check\ifp\*.ifp` — originals, `out_ifp\<name>` (ANP3 rewrite) and `out_ifp\<name>.anpk.ifp`
* `dff_results.json`, `ifp_results.json` — full per-chunk diff lines and structural comparison output
* Engine evidence used: `E:\RE\exports\004d47f0.json` (Ghidra decompile of `CAnimManager::LoadAnimFile`), `E:\RE\asm\004D1190.c` (`CAnimBlendSequence::RemoveQuaternionFlips`), `E:\RE\asm\004CF4E0.c`, gta-reversed `NodeName.cpp` / `AnimManager.cpp` (scratchpad clone), plugin-sdk `NodeName.cpp`.
