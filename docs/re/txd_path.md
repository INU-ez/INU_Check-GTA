# GTA SA 1.0 US — TEXTURE DICTIONARY (TXD) loading path

Source of truth: Ghidra decompiles in `E:\RE\asm_map\<ADDR>.c` / `E:\RE\asm\<ADDR>.c`, capstone
disassembly (`E:\RE\xdis_tmp.py`) of the RW functions that were not pre-dumped
(`RwTexDictionaryGtaStreamRead` 0x730FC0, `RwTextureGtaStreamRead` 0x730E60,
`_rwD3D9NativeTextureRead` 0x4CD820, `rwD3D9RasterLock` 0x4C9F90, `D3DResourceSystem::CreateTexture`
0x730510, `RwTextureSetName` 0x7F38A0, `RwTexDictionaryAddTexture` 0x7F3980,
`RwTexDictionaryFindNamedTexture` 0x7F39F0, `RwStreamRead` 0x7EC9D0, `RwStreamFindChunk` 0x7ED2D0,
`CStreaming::LoadCdDirectory` 0x5B6170), raw tables via `readva.py`. gta-reversed / plugin-sdk were
used only for names. Vanilla data survey: all 3983 TXDs / 32166 textures of gta3.img, gta_int.img,
player.img, cutscene.img (script `scratchpad/txd/survey.py`).

Legend: **CRASH** = guaranteed fault, **CRASH-likely** = heap/VRAM-sysmem overrun that faults later,
**GARBAGE** = wrong rendering, **LOAD-FAIL** = the texture read returns NULL → the whole TXD is
destroyed → `CTxdStore::LoadTxd` returns false → `CStreaming::ConvertBufferToObject` 0x40C6B0 does
`RemoveModel + RequestModel` (re-requested forever; every model that needs the TXD never appears),
**IGNORED** = accepted silently.

---------------------------------------------------------------------------------------------------

## 0. Call graph (what actually runs)

```
CStreaming::ConvertBufferToObject 0x40C6B0   (id 20000..24999 = txd slot 0..4999)
  ├─ parent slot set (TxdDef+6 != -1) and CTxdStore::GetTxd(parent)==NULL  → RemoveModel+RequestModel (retry)
  ├─ !(flags & 0xE) && !AreTexturesUsedByRequestedModels(slot)             → RemoveModel (dropped)
  ├─ ms_bLoadingBigModel(0x8E4A58)==0 : CTxdStore::LoadTxd(slot, memstream) 0x731DD0
  └─ else                              : CTxdStore::StartLoadTxd 0x731930 (first half of the textures,
                                         status=4) … later CTxdStore::FinishLoadTxd 0x731E40 (rest)
CTxdStore::LoadTxd(slot, stream) 0x731DD0
  ├─ RwStreamFindChunk(stream, 0x16)          (version stamp must decode to 0x34000..0x36003)
  ├─ RwTexDictionaryGtaStreamRead 0x730FC0    (GAME's own reader – RwTexDictionaryStreamRead 0x804C30
  │     │                                       has 0 callers)
  │     ├─ RwStreamFindChunk(1) ; RwStreamRead(&local4, structLen)   ← !!! 4-byte stack slot
  │     ├─ RwTexDictionaryCreate
  │     └─ numTextures × RwTextureGtaStreamRead 0x730E60
  │            ├─ RwStreamFindChunk(0x15, &len, &ver)  (len is passed on but NEVER used)
  │            ├─ RwGlobals.stdFunc[26] = _rwD3D9NativeTextureRead 0x4CD820 (stream, &tex, len)
  │            ├─ filter 1→2, 3→4 ; anisotropy if FxQuality>=2
  │            └─ RwTexDictionaryAddTexture (inserts at list HEAD)
  └─ CTxdStore::SetupTxdParent 0x731D50 (copies parent RwTexDictionary* into the child's plugin slot)
CFileLoader::LoadTexDictionary 0x5B3860 (loose file): RwStreamOpen(FILENAME) → FindChunk(0x16) →
  RwTexDictionaryGtaStreamRead; on failure returns an EMPTY dictionary (never NULL).
CTxdStore::LoadTxd(slot, filename) 0x7320B0 → thunk 0x402161: loops `RwStreamOpen(2,1,path)` until it
  succeeds (a missing file = infinite loop / HANG), then LoadTxd(slot, stream).
```

`RwGlobals` (`RwEngineInstance` = `*(void**)0xC97B24`): `stdFunc[]` starts at +0x48; the D3D9 driver
fills it in 0x7F7380 (`rwDEVICESYSTEMSTANDARDS`): [4]=0x4CCE60 rasterCreate, [15]=0x4C9F90 rasterLock,
[16]=0x4CA290 rasterUnlock, [23]=0x4CA4E0 lockPalette, [24]=0x4CA540 unlockPalette,
[25]=0x4CD360 nativeTextureGetSize, [26]=0x4CD820 **nativeTextureRead**, [27]=0x4CD4D0 nativeTextureWrite,
[28]=0x4CBCB0 rasterGetMipLevels. `stdFunc[26]` lives at `RwEngineInstance+0xB0` (0x730ED0
`call [ecx+0xb0]`) – that pointer is the cleanest hook point (see §5).

---------------------------------------------------------------------------------------------------

## 1. Byte layouts accepted, and what is validated

### 1.1 TXD file (chunk 0x16)

| bytes | field | validated? |
|---|---|---|
| 12 | chunk hdr `0x16, len, libID` | type must be found by FindChunk; **libID must decode to version 0x34000..0x36003** (0x7ED337-0x7ED374); `len` unused |
| 12 | STRUCT hdr `1, len, libID` | version same rule; **`len` is read into a 4-byte stack slot** (0x730FE1-0x730FEC, buffer at frame+4, `len` at +8, version at +0xC, return address at +0x10) → see TXD-STRUCTLEN |
| 2 | `numTextures` u16 | none. 0 → empty dict. Larger than the real count → FindChunk hits EOF → LOAD-FAIL. Smaller → extra textures silently IGNORED |
| 2 | `deviceId` u16 | **ignored by the game's reader** (stock reader 0x804C30 checks it; vanilla files carry 2 or 6) |
| … | `numTextures` × Texture Native chunks (0x15) | each found with FindChunk(0x15) → any other chunk type in between is skipped by its length |
| … | trailing chunks (EXTENSION of the dictionary) | never read |

The reader knows the 0x15 chunk lengths only via `RwStreamFindChunk`; **the native reader never uses
them**. After it returns the stream sits wherever the last mip level ended; the texture's own
EXTENSION chunk (if present) is skipped by the next FindChunk. Consequently *every byte* of the
STRUCT payload must be consumed exactly (no padding/trailing bytes), otherwise the next FindChunk
parses pixel bytes as chunk headers (LOAD-FAIL, or a "skip" of a garbage length).

### 1.2 Texture Native (0x15) → STRUCT payload, D3D9 (`_rwD3D9NativeTextureRead` 0x4CD820)

Stack frame (base = esp after 4 pushes): version @+0x1C, length @+0x24, raster hdr @+0x28..0x37,
platform hdr @+0x38..0x7F.

| off | size | field | what the reader does |
|---|---|---|---|
| 0x00 | 4 | `platformId` | **must be 9** (0x4CD87B) else return 0 |
| 0x04 | 4 | `filterAddressing` | byte0 = filter mode, bits 8-11 addrU, 12-15 addrV, upper 16 bits dropped. Copied verbatim into `RwTexture+0x50` (0x4CDD3D-0x4CDD7C). NOT range-checked. Then 0x730F58: filter 1(NEAREST)→2(LINEAR), 3(MIPNEAREST)→4(MIPLINEAR) |
| 0x08 | 32 | `name[32]` | `RwTextureSetName` = strncpy 32 + strlen; strlen ≥ 32 → `name[31]=0` + RwErrorSet (non-fatal). No NUL inside 32 bytes → strlen runs into `mask` on the stack (harmless) → truncated to 31 |
| 0x28 | 32 | `mask[32]` | same via `RwTextureSetMaskName`; never used on the D3D9 TXD path |
| 0x48 | 4 | `rasterFormat` | bits 0x0F00 pixel format, 0x1000 AUTOMIPMAP, 0x2000 PAL8, 0x4000 PAL4, 0x8000 MIPMAP. See §1.4 for the exact rules |
| 0x4C | 4 | `d3dFormat` | D3DFORMAT / FOURCC. Compressed/cube: passed straight to D3D after `CheckDeviceFormat` (0x4CBDE0). Uncompressed: must EQUAL the table value for the rasterFormat nibble (0x4CDBA3 `cmp edx,[edi+0x18]`) |
| 0x50 | 2 | `width` u16 | must equal raster->width after creation (0x4CDBB0) → clamped/rounded sizes fail cleanly |
| 0x52 | 2 | `height` u16 | same (0x4CDBBE) |
| 0x54 | 1 | `depth` | **IGNORED**: `RwRasterCreate` stores it, `rwD3D9SetRasterFormat` 0x4CC5C0 overwrites `raster+0x14` from the format table |
| 0x55 | 1 | `numLevels` | loop count for the level reads (0x4CDC86) when the MIPMAP bit is set AND automipmapgen is off; NOT clamped to the D3D chain. If MIPMAP bit clear the D3D texture has 1 level but the loop STILL runs `numLevels` times |
| 0x56 | 1 | `rasterType` | OR-ed into the RwRasterCreate flags: `cType = &7`, `cFlags = &0xF8`. Only 0/4 lockable (jump table 0x4CA278: 1,3 → lock fails; 2 = camera back-buffer; 5 = camera-texture) |
| 0x57 | 1 | `flags` | bit0 hasAlpha, bit1 cubeMap, bit2 autoMipmap, bit3 compressed. bit2 set while the device could not enable autogen → fail (0x4CDB81-0x4CDB8C) |
| 0x58 | 0x80 / 0x400 | palette | read iff PAL4 (0x4000: 0x80 bytes, checked first) / PAL8 (0x2000: 0x400 bytes) via `RwRasterLockPalette` |
| … | 4 + n | per face (1, or 6 if cube) × per level (`numLevels`, or 1 if automipmapgen): `u32 size` + `size` bytes | `RwRasterLock(level, WRITE)`; `RwStreamRead(stream, pBits, size)` must return `size`. **`size` is never compared with the surface size** |

STRUCT chunk version must be 0x34000..0x36003 (0x4CD848-0x4CD85C; `0x1803FFFF` → 0x36003 OK,
RW 3.7 stamps → LOAD-FAIL). The STRUCT `len` is never used. 0x48+0x10 header bytes are always read.

### 1.3 Creation paths (decides which checks apply)

```
flags&8 (compressed):
   cube:   CheckDeviceFormat(d3dFormat); RwRasterCreate(w,h,depth, rasterFormat|type|0x80 DONTALLOC);
           levels = (cFormat&0x80) ? numLevels : 1 ; needs caps 0x30000 for levels>1;
           IDirect3DDevice9::CreateCubeTexture(w, levels, autogen?0x400:0, d3dFormat, MANAGED)
   normal: same RwRasterCreate; D3DResourceSystem::CreateTexture(w,h,levels,d3dFormat,&ext->texture)
           0x730510  →  **levels>1 is turned into 0 = full chain** (0x730518-0x73051D); recycles a
           destroyed texture of equal (w==h only) size/levels/format from D3DTextureBuffer::Pop 0x72FF60
           → old pixel content!
   ext->alpha = flags&1 ; ext->compressed=1 ; ext->d3dFormat = stream d3dFormat
flags&2 (cube, uncompressed): RwRasterCreate(DONTALLOC) + CreateCubeTexture(... stream d3dFormat ...)
           but ext->d3dFormat is never written → later check `d3dFormat == ext->d3dFormat(0)` → LOAD-FAIL
           (uncompressed cube maps can never load in this build)
else, rasterFormat & 0xFFFF6FFF != 0 (a real RW pixel format):
           RwRasterCreate(w,h,depth, rasterFormat|type) → rwD3D9RasterCreate 0x4CCE60 →
           rwD3D9SetRasterFormat 0x4CC5C0 (table 0x85C670: nibble→D3DFMT, depth, alpha;
           CheckDeviceFormat; PAL4 → return 0; PAL8 → needs P8) → w/h==0 → no texture, else
           rwD3D9CreateTexture 0x4CB7C0 (clamp dims to caps 0x7FED70; PAL8 → alloc 0x404 palette +
           D3DFMT_P8; D3DResourceSystem::CreateTexture(w,h, mip?0:1, fmt))
           ext->alpha = table alpha (stream bit0 IGNORED), ext->compressed = 0
else ("format 0" = only 0x9000 bits): RwD3D9RasterCreate 0x4CD050 (w,h,d3dFormat,(fmt&0x9000)|type):
           creates from the explicit D3DFORMAT; DXTn sets cFormat |= 2/3 → the later
           `rasterFormat == cFormat<<8` compare fails → only formats absent from the RW table
           (A8L8, V8U8, D3DFMT_A8…) survive this path
common: flags&4 && !autogen → fail ; rasterFormat == cFormat<<8 ; d3dFormat == ext->d3dFormat ;
        width/height equal ; palette ; levels ; RwTextureCreate ; filter/addressing ; names
```

### 1.4 rasterFormat nibble → D3DFORMAT table (0x85C670, 8 bytes/entry, indexed `(fmt>>8)&0xF`)

| nibble | RW name | D3DFMT | depth | alpha |
|---|---|---|---|---|
| 0x100 | 1555 | 25 A1R5G5B5 | 16 | 1 |
| 0x200 | 565 | 23 R5G6B5 | 16 | 0 |
| 0x300 | 4444 | 26 A4R4G4B4 | 16 | 1 |
| 0x400 | LUM8 | 50 L8 | 8 | 0 |
| 0x500 | 8888 | 21 A8R8G8B8 | 32 | 1 |
| 0x600 | 888 | 22 X8R8G8B8 | 32 | 0 |
| 0x700 | 16 (Z) | 80 D16 | 16 | 0 |
| 0x800 | 24 (Z) | 77 D24X8 | 32 | 0 |
| 0x900 | 32 (Z) | 71 D32 | 32 | 0 |
| 0xA00 | 555 | 24 X1R5G5B5 | 16 | 0 |
| 0xB00..0xF00 | — | **reads past the table** (0x3BA3D70A, 0x4CDF50 …) | | |

Nibbles 0xB..0xF index beyond the 11-entry table (`(uVar7>>8&0xF)*8` in 0x4CC5C0) → garbage
D3DFORMAT → `CheckDeviceFormat` fails → LOAD-FAIL (clean, no fault: the read stays inside .rdata).

Reverse table (D3DFORMAT → alpha/depth/rwFormat) at 0xB4E7E0 (128×4, built in 0x4C9ADA): only
21→0x500, 22→0x600, 23→0x200, 24→0xA00, 25→0x100, 26→0x300, 41(P8)→0x2000, 50(L8)→0x400 map back;
all others have rwFormat 0.

### 1.5 Vanilla usage (survey of 32166 textures)

* platform 9 everywhere; all chunk stamps `0x1803FFFF`; TXD STRUCT length always 4; deviceId 2 or 6.
* raster combos: `0x0200/DXT1/depth16/flags 8` 21491, `0x8200/DXT1/16/8` 7008, `0x0300/DXT3/16/9` 2098,
  `0x0600/X8R8G8B8/32/0` 1024, `0x0100/DXT1/16/9` 308, `0x0500/A8R8G8B8/32/1` 237. rasterType always 4.
  No PAL4/PAL8, no 0x1000, no cube maps, no non-pow2, sizes 8..1024, 5437 non-square.
* filter dword: 0x1106 (25159), 0x1102 (4409), 0x1101 (2589), 0x1206 (8), 0x2106 (1).
* mipmapped textures always carry the full chain (`floor(log2(max(w,h)))+1`); **levels smaller than
  4×4 are stored with `size = 0`** (10065 such levels, e.g. a51.txd/dam_gencon levels 7,8) — the engine
  simply skips them (they stay uninitialised, see TXD-LEVEL-SHORT).
* names ≤ 31 chars, charset `space # % & ' ( ) - 0-9 @ A-Z ] _ a-z`; 2171 textures have a mask
  name; no duplicate names inside one TXD; max 165 textures per TXD; biggest TXD 2.59 MB.

---------------------------------------------------------------------------------------------------

## 2. In-memory layouts

### 2.1 RwRaster (freelist, 0x34 bytes + plugins; `RwRasterCreate` 0x7FB230)

| off | field | evidence |
|---|---|---|
| 0x00 | `parent` (RwRaster*, self) | 0x7FB28A `mov [esi],esi` |
| 0x04 | `cpPixels` | 0x7FB28C; lock sets it from lockedRect.pBits (0x4CA21F) |
| 0x08 | `palette` | 0x7FB28F; `rwD3D9RasterLockPalette` 0x4CA50F |
| 0x0C | `width` | 0x7FB264; lock overwrites with `width>>level` (0x4CA231) |
| 0x10 | `height` | 0x7FB26E / 0x4CA234 |
| 0x14 | `depth` | 0x7FB275, then overwritten by 0x4CC5C0 from the table |
| 0x18 | `stride` | 0x4CA252 (lockedRect.Pitch) |
| 0x1C/0x1E | `nOffsetX/Y` s16 | 0x7FB282/0x7FB286, used by sub-raster lock 0x4CA04C |
| 0x20 | `cType` (0 NORMAL,1 ZBUFFER,2 CAMERA,4 TEXTURE,5 CAMERATEXTURE) | 0x4CCE60 `param_3 & 7` |
| 0x21 | `cFlags` (0x80 DONTALLOCATE) | 0x4CCE60 `param_3 & 0xF8` |
| 0x22 | `privateFlags` (lock bits 2/4/8/0x18) | 0x4CA08F-0x4CA0A1, 0x4CA514 |
| 0x23 | `cFormat` = `rasterFormat >> 8` (0x80 MIPMAP, 0x10 AUTOMIPMAP, 0x20 PAL8, 0x40 PAL4, 0x0F fmt) | 0x4CC5C0 end; 0x4CDB98 `mov ch,[esi+0x23]; cmp eax,ecx` |
| 0x24 | `originalPixels` | — |
| 0x28 / 0x2C | `originalWidth/Height` | 0x4CA225 / 0x4CA22A |
| 0x30 | `originalStride` | — |

D3D9 raster extension (`raster + *(int*)0xB4E9E0`, plugin registered at 0x4C9AC5):

| off | field | evidence |
|---|---|---|
| 0x00 | `IDirect3DTexture9* texture` (or cube/surface) | 0x4CCE60 `*puVar1=0`; 0x4C9FFE-0x4CA022 `GetSurfaceLevel` |
| 0x04 | `palette` block (0x404 bytes: 256 PALETTEENTRY + u32 index at +0x400) | 0x4CB7C0 |
| 0x08 | `alpha` u8 | 0x4CD9A9 (stream bit0) / 0x4CB7C0 `table[fmt].alpha`; consumed by `_rwD3D9RWSetRasterStage` 0x7FDCFC → ALPHABLEND/ALPHATEST |
| 0x09 | `cube:4 \| face:4` | 0x4CD932-0x4CD939, 0x4CDD03-0x4CDD15 |
| 0x0A | `automipmapgen:4 \| compressed:4` | 0x4CD95B, 0x4CD9AF-0x4CD9B5 |
| 0x0B | `lockedMipLevel` (0xFF = none) | 0x4CCE60 `byte +0xb = 0xff`, 0x4CA25A |
| 0x0C | `lockedSurface` | 0x4CA008 |
| 0x10 | `D3DLOCKED_RECT lockedRect` {Pitch +0x10, pBits +0x14} | 0x4CA042, 0x4CA214 |
| 0x18 | `d3dFormat` | 0x4CD9B8, 0x4CDBA7 |
| 0x1C | `swapChain` | 0x4CA173 |
| 0x20 | `window` | 0x4CC978 |

### 2.2 RwTexture (freelist entry 0x58 + plugins; `RwTextureCreate` 0x7F37C0)

| off | field | evidence |
|---|---|---|
| 0x00 | `raster` | 0x7F37EE |
| 0x04 | `dict` | 0x7F37FF; `RwTexDictionaryAddTexture` 0x7F39A1 |
| 0x08 | `lInDictionary` LLLink {next, prev} | 0x7F39A7-0x7F39B6 |
| 0x10 | `name[32]` | `RwTextureSetName` strncpy to +0x10, `[edi+0x2f]=0` |
| 0x30 | `mask[32]` | `RwTextureSetMaskName` +0x30, `[edi+0x4f]=0` |
| 0x50 | `filterAddressing` u32: byte0 filter, bits 8-11 addrU, 12-15 addrV (default 0x1101) | 0x7F37F6-0x7F380B, 0x4CDD3D-0x4CDD7C, `RwD3D9SetTexture` 0x7FDE84/0x7FDECF/0x7FDF12 |
| 0x54 | `refCount` (1 at create; `RwTextureRead` ++) | 0x7F3808, 0x7F3AE0 |

Filter/addressing are applied through fixed tables: address table 0x88493C (5 entries: NA→0, WRAP 1,
MIRROR 2, CLAMP 3, BORDER 4), filter table 0x884950 (8 pairs: NA{0,0}, NEAREST{1,0}, LINEAR{2,0},
MIPNEAREST{1,1}, LINEARMIPNEAREST{2,1}, MIPLINEAR{1,2}, LINEARMIPLINEAR{2,2}, ANISO{3,2}). Filter byte
0..255 indexes `[ebp*8+0x884950]` and address nibble 0..15 indexes `[eax*4+0x88493C]` without bounds
→ out-of-range values read neighbouring .rdata and feed garbage to `SetSamplerState` (GARBAGE, no
fault).

### 2.3 RwTexDictionary (0x18 + plugins; `RwTexDictionaryCreate` 0x7F3600)

+0x00 RwObject{type 6, subType, flags, privateFlags, parent}; +0x08 `texturesInDict` sentinel
{next +8, prev +0xC}; +0x10 `lInInstance`. Game plugin "TxdParent" (`CTxdStore::PluginAttach`
0x731650: `RwTexDictionaryRegisterPlugin(4, 0x253F2F5, …)`, offset in `0xC88018`): one
`RwTexDictionary* parent`, written by `SetupTxdParent` 0x731D50, walked by `TxdStoreFindCB` 0x731720.

`RwTexDictionaryAddTexture` inserts at the **head** (`sentinel.next = tex.link`, 0x7F39B6);
`RwTexDictionaryFindNamedTexture` walks from `sentinel.next` with a case-insensitive strcmp
(0x7F3A17-0x7F3A48) → **the last texture added (last in the file) wins** for duplicate names.

### 2.4 CTxdStore (`ms_pTxdPool` 0xC8800C, CPool<TxdDef>, **5000** entries: `Initialise` 0x731F5F
`push 0x1388`), TxdDef = 0xC bytes

| off | field | evidence |
|---|---|---|
| 0x00 | `RwTexDictionary* txd` | `LoadTxd` `*piVar2 = iVar1` |
| 0x04 | `u16 refCount` | `SetupTxdParent` `*psVar1 += 1`; `RemoveTxd` |
| 0x06 | `s16 parentIndex` (-1 none) | `AddTxdSlot` `=0xFFFF`; `SetupTxdParent` |
| 0x08 | `u32 nameHash` (`CKeyGen::GetUppercaseKey`) | `AddTxdSlot`, `FindTxdSlot` 0x731850 |

Streaming id = 20000 + slot (0x40C6B0 `param_2 - 20000`, `RemoveTxd` `RemoveModel(iVar1 + 20000)`).

### 2.5 RwStream (memory type 3, `_rwStreamInitialize(&0x8E48AC, 0, 3, 1, &{ptr,size})` in 0x40C6B0)

+0x00 type (1 FILE, 2 FILENAME, 3 MEMORY, 4 CUSTOM; jump table 0x7ECB1C), +0x04 accessType,
+0x0C `position`, +0x10 `nSize`, +0x14 `memBlock` (0x7ECA76-0x7ECAC4). `RwStreamRead` on a memory
stream clamps to the remaining bytes (returns fewer, sets error) and copies with `rep movsd/movsb`
into the destination — a NULL destination faults.

---------------------------------------------------------------------------------------------------

## 3. Crash / garbage / silent conditions

Every item cites the unchecked operation. IDs use the `TXD-` prefix.

### TXD-STRUCTLEN — TXD STRUCT chunk length ≠ 4 (CRASH for len ≥ 13, LOAD-FAIL for 5..12)
`RwTexDictionaryGtaStreamRead` 0x730FC0: `RwStreamRead(stream, frame+4, structLen)` into a 4-byte
local. Frame: +4 buffer, +8 `structLen`, +0xC version, **+0x10 return address**. len 5..8 corrupts
`structLen` → the `cmp eax,ecx` (0x730FF8, `ecx` reloaded from +8 AFTER the read) fails → returns 0
(unless the overwriting bytes equal len). 9..12 also smashes version (unused). **≥ 13 overwrites the
return address with file bytes → CRASH / control-flow hijack** at the `ret` 0x731002/0x73104B.
(Same bug in the unused stock reader 0x804C30 and in `StartLoadTxd`'s 0x731070.) Vanilla: always 4.

### TXD-VERSION — chunk stamps outside RW 3.4.0..3.6.0.3 (LOAD-FAIL)
`RwStreamFindChunk` 0x7ED337-0x7ED374 rejects the found 0x16 / 0x15 / STRUCT chunk when the decoded
version is < 0x34000 or > 0x36003 (RwErrorSet, returns 0). The native STRUCT is re-checked at
0x4CD848-0x4CD85C. libID `0x1803FFFF` (= 3.6.0.3) is the safe value; old-style `0x00000310` stamps and
3.7 stamps (`0x1C02xxxx`) fail.

### TXD-PLATFORM — platformId ≠ 9 (LOAD-FAIL)
0x4CD87B `cmp dword [esp+0x38], 9` → return 0. (PS2 `'PS2\0'`, Xbox 5, D3D8 8 all rejected.)

### TXD-LEVEL-OVERSIZE — a mip level's `size` larger than the D3D surface (CRASH-likely)
Loop 0x4CDCA0-0x4CDCFD: `RwRasterLock(raster, level, WRITE)` → `RwStreamRead(stream, pBits, size)`.
`size` is only compared with the number of bytes the stream returned (0x4CDCE5). Nothing compares it
with `stride*rows` of the locked surface. Bytes beyond the level are written past the D3D managed
texture's system-memory copy → heap corruption → fault at a later `UnlockRect`/`Release`/allocation.
Expected bytes: DXT1 `max(1,⌈w/4⌉)·max(1,⌈h/4⌉)·8`, DXT2-5 `·16`; uncompressed `stride·h` with
stride = `w·bpp/8` from the table (§1.4) for the level dims `max(1,w>>l)`.

### TXD-LEVEL-SHORT — a mip level's `size` smaller than the surface (GARBAGE)
Same loop; a short `size` leaves the tail of the level uninitialised. The D3D texture is either
fresh (undefined content) or recycled from `D3DTextureBuffer::Pop` 0x72FF60 (previous texture's
pixels: the well-known "wrong mip" look). Vanilla itself stores `size=0` for levels < 4×4 (2×2, 1×1),
so a validator must accept `size == 0` and only flag `0 < size < expected` as suspicious and
`size > expected` as fatal. Same effect when the sum of level bytes ≠ STRUCT payload (see
TXD-STRUCT-TRAILING).

### TXD-NUMLEVELS-OVER — `numLevels` > levels the D3D texture actually has (CRASH when the extra level's size ≠ 0)
`D3DResourceSystem::CreateTexture` 0x730518 turns any `levels > 1` into 0 (= full chain
`floor(log2(max(w,h)))+1`), so D3D never refuses; but the read loop runs `numLevels` times
(0x4CDC86). For a level ≥ chain length `rwD3D9RasterLock` 0x4C9F90's `GetSurfaceLevel` fails →
returns 0 → `RwRasterLock` returns NULL (0x7FB2FF-0x7FB303) → `RwStreamRead(stream, NULL, size)` →
`rep movsd` to address 0 (0x7ECABB) → **CRASH**; with `size == 0` nothing is copied and the
subsequent `RwRasterUnlock` on an unlocked raster returns 0 harmlessly (0x4CA299).
Without the MIPMAP bit the texture has exactly 1 level, so **`numLevels > 1` crashes the same way**.

### TXD-NUMLEVELS-UNDER — `numLevels` < full chain with MIPMAP set, or `numLevels == 0` (GARBAGE)
Full chain is always created (0x730518); unread levels stay undefined / recycled (see
TXD-LEVEL-SHORT). Visible as random colours/other textures at distance with trilinear filtering.
`numLevels == 0` with MIPMAP → `levels = 0` → full chain, no level read → whole texture garbage;
without MIPMAP → 1-level garbage texture.

### TXD-STRUCT-TRAILING — bytes left in the STRUCT after the last level / extra levels (LOAD-FAIL)
The reader never seeks to the STRUCT end (no use of the length). The next `RwStreamFindChunk(0x15)`
(0x730E75) interprets the leftover bytes as `{type,len,libID}` headers and skips `len` bytes each
time → normally EOF → texture read NULL → dictionary destroyed → LOAD-FAIL. Also triggered by
AUTOMIPMAP (TXD-AUTOMIP) and by writing 6 faces without the cube flag.

### TXD-ZERO-DIM — width or height == 0 (CRASH for uncompressed + d3dFormat 0 + numLevels ≥ 1)
`rwD3D9RasterCreate` 0x4CCE60: `width==0 || height==0` → marks DONTALLOCATE, no D3D texture, returns
1. Checks pass only if the stream's `d3dFormat == 0` (ext->d3dFormat stays 0). Then
`rwD3D9RasterLock` 0x4C9FFE-0x4CA013: `eax = ext->texture (NULL)`, `mov edx,[eax]` → **CRASH**.
Compressed path: `CreateTexture(0,…)` fails → clean LOAD-FAIL. With a real d3dFormat: clean
LOAD-FAIL (d3dFormat mismatch).

### TXD-RASTERTYPE — rasterType byte ≠ 4 (or 0)
Type bits OR-ed into the raster flags (0x4CD8D6 / 0x4CDB17). 1 (ZBUFFER): depth surface created
(0x4CCD70), lock jump table entry → fail → NULL → **CRASH** in `RwStreamRead(NULL)`. 3: same lock
failure. 2 (CAMERA): the game's back buffer is locked and file bytes are copied into it
(CRASH-likely on oversize). 5 (CAMERATEXTURE): render-target texture in DEFAULT pool (works by
accident, not MANAGED). Extra bits (≥ 8) pollute `cFlags` (0x80 = DONTALLOCATE → uncompressed:
no texture → lock NULL-deref as in TXD-ZERO-DIM when d3dFormat = 0).

### TXD-FORMAT-MISMATCH — rasterFormat nibble vs d3dFormat vs compressed flag (LOAD-FAIL, clean)
Enforced by 0x4CDB92-0x4CDBC6: `rasterFormat == cFormat<<8` and `d3dFormat == ext->d3dFormat`.
Uncompressed: d3dFormat must be the table value (§1.4) for the nibble; DXT fourcc without bit3 →
mismatch. DXT with bit3: any nibble 1..6/0xA accepted (0x8500+DXT3 loads fine per the code); nibble
0 fails (default format substituted at 0x4CC5C0 by *display* depth, then cFormat ≠ stream);
nibbles 0xB..0xF fail (past the table). Bits in `rasterFormat & 0xFF` or ≥ 0x10000 → mismatch.

### TXD-PALETTE — PAL4 / PAL8 (LOAD-FAIL on today's GPUs; CRASH in one combination)
`rwD3D9SetRasterFormat` 0x4CC5C0: PAL4 (0x4000) with a format nibble → `return 0`; PAL8 (0x2000) →
`CheckDeviceFormat(D3DFMT_P8=0x29)` → no modern D3D9 driver supports P8 → `RwRasterCreate` NULL →
LOAD-FAIL (whole TXD). `_rwD3D9RasterConvertToNonPalettized` 0x4CD250 is NOT on this path (its
callers 0x812EA3/0x813AE1/0x816398/0x816FA5 are the image→raster converters). On a P8-capable
device: PAL8 uncompressed works (palette block 0x404 from 0x4CB7C0); **PAL8 + compressed flag →
palette pointer NULL → `RwRasterLockPalette` returns NULL → `RwStreamRead(stream, NULL, 0x400)`
0x4CDC1C → CRASH**. Both PAL4 and PAL8 set → PAL4 wins (0x4CDBCC first) but creation fails earlier.

### TXD-AUTOMIP — rasterFormat bit 0x1000 (AUTOMIPMAP) and/or flags bit 2 (LOAD-FAIL, device-dependent)
With 0x9000 set and `CheckDeviceFormat(usage AUTOGENMIPMAP)` OK (0x4CBF8A / 0x4CC020), `ext->
automipmapgen=1` → **only ONE level is read** (0x4CDC76-0x4CDC84) → remaining level data stays in
the stream → TXD-STRUCT-TRAILING → LOAD-FAIL. If the device cannot autogen: flags bit 2 → fail
(0x4CDB88); bit 2 clear → loads normally. DXT formats generally return D3DOK_NOAUTOGEN → normal.

### TXD-CUBE — flags bit 1 (cube map)
Compressed cube: 6 faces × levels read (face nibble `ext+9` bumped at 0x4CDD03); requires caps
`0xC9BF44 & 0x30000` when levels > 1 else fail. Uncompressed cube: **never loads** (ext->d3dFormat not
written → 0x4CDBA3 mismatch). No vanilla cube maps; the exporter must never set the bit.

### TXD-DIMS — non power-of-two / above device caps (LOAD-FAIL, clean; device-dependent)
Uncompressed: 0x7FED70 clamps to `MaxTextureWidth/Height` (0xC9BF58/5C) and rounds down to pow2 when
`TextureCaps & POW2` (0x2) is set (always when mipmapped; without mipmaps only if
`NONPOW2CONDITIONAL` 0x100 is absent), squares when `SQUAREONLY` 0x20 → raster dims ≠ stream dims →
fail. Compressed path skips 0x7FED70; the D3D runtime refuses sizes above caps → fail. Modern GPUs
have no POW2 caps, so non-pow2 loads there and fails on old ones. DXT below 4×4 (top level 2×2 or
1×1, or mip tails) is legal for D3D9 (one block; `LockRect` pitch = 8/16); the surface holds one
full block, so `size` may be 8/16 (our exporter) or 0 (vanilla).

### TXD-NAME-LEN — texture name ≥ 32 chars / no NUL (IGNORED → texture unreachable)
`RwTextureSetName` 0x7F38A0: strncpy 32, `strlen(src) >= 0x20` → `name[31] = 0`, RwErrorSet
(0x8000001E). The DFF material asks for the full (≤127-char) name via `RwTextureRead` 0x7F3AC0 →
`TxdStoreFindCB` → case-insensitive compare → a 32+-char name never matches → material renders
untextured. Rule: 1..31 chars, identical bytes (case-insensitive) in DFF and TXD.

### TXD-NAME-DUP — duplicate names in one TXD (IGNORED, last wins)
Head insertion 0x7F39B6 + head-first search 0x7F39FB → the LAST occurrence in the file is used;
both stay resident. Names differing only in case are duplicates (0x7F3A23-0x7F3A3D).

### TXD-MISSING — a model's texture is absent from the TXD chain (IGNORED → untextured)
`RwTextureRead` 0x7F3AC0: find CB NULL → read CB `TxdStoreLoadCB` 0x731710 returns 0 → RwErrorSet →
`RwTextureStreamRead` 0x8046E0 skips the plugin chunks and returns NULL → `RpMaterialStreamRead`
0x74DD30 stores `material->texture = NULL` and continues → rendered with material colour × vertex
colour, `RwD3D9SetTexture(NULL)` (white-looking surface). No crash.

### TXD-ALPHA-FLAG — hasAlpha flag vs data (GARBAGE-visual)
Compressed: `ext->alpha = flags&1` (0x4CD9A9/0x4CDA16) → `_rwD3D9RWSetRasterStage` 0x7FDCFC enables
ALPHABLEND (0x1B) + ALPHATEST (0x0F) via the deferred render-state cache and marks the texture as
"has alpha". DXT3/DXT5 with flag 0 → drawn opaque; DXT1 punch-through with flag 0 → holes drawn black;
DXT1 with flag 1 → alpha test (1-bit) works and the mesh is treated as alpha (sorting cost).
Uncompressed: flag IGNORED, alpha comes from the table (1555/4444/8888 = alpha, 565/888/LUM8 = none).

### TXD-FILTER — filter byte ∉ 1..6 / addressing nibbles ∉ 1..4 (GARBAGE, mild)
Not checked at load (0x4CDD3D-0x4CDD7C). `RwD3D9SetTexture` indexes the 8-entry filter table and the
5-entry address table without bounds (0x7FDEAC, 0x7FDEEF, 0x7FDF6A/0x7FDF83) → arbitrary
`SetSamplerState` values (D3D retail ignores invalid ones → stale state). 0 (NA) maps to invalid
D3D values too. Vanilla: 0x1106 / 0x1102 / 0x1101.

### TXD-COUNT — `numTextures` vs real chunk count
Larger → FindChunk EOF → LOAD-FAIL. Smaller → later textures never loaded (IGNORED). No upper bound
besides u16 (vanilla max 165).

### TXD-POOL — more than 5000 TXD slots (CRASH)
`CTxdStore::AddTxdSlot` 0x731C80: `CPool::New` 0x731B80 returns NULL when full (0x731BE3) →
`*puVar1 = 0` (0x731C8C) → write to address 0 → **CRASH**. Slots are consumed by every distinct txd
name in IDE `objs/tobj/anim/peds/cars/weap/hier` lines, every `.txd` IMG entry
(`LoadCdDirectory` 0x5B62AE), every `txdp` parent, vehicle remap TXDs and ~20 engine-created ones.
Vanilla ≈ 3990 → roughly 1000 free.

### TXD-IMGNAME — IMG entry base name > 20 chars (IGNORED)
`CStreaming::LoadCdDirectory` 0x5B6222 `cmp edx,0x14; jg skip`: an entry whose `strchr('.')` offset
exceeds 20 is silently skipped → the slot is never registered → models referencing it never load
(no crash). Entry name field is 24 bytes total (20 + `.txd`).

### TXD-PARENT-LOOP — `txdp` cycle or self-parent (LOAD-FAIL forever)
`ConvertBufferToObject` 0x40C6B0 refuses to load a child while `GetTxd(parent) == NULL` (re-request);
a cycle (or `txdp a,a`) means neither ever loads. `TxdStoreFindCB` 0x731720 would spin forever on a
cyclic chain only if such a chain existed in memory, which the streamer prevents. A parent that is
never loaded (missing file) → children never load.

### TXD-BROKEN-TXD-RETRY — any LOAD-FAIL (behavioural note)
0x40C948: `RemoveModel(id); RequestModel(id, flags)` → the TXD is re-read on every streaming pass,
the dependent DFFs stay in "requested" state forever (invisible objects, no crash, CPU churn).
`CFileLoader::LoadTexDictionary` (loose files, e.g. `generic`) instead returns an empty dictionary.

### TXD-DEPTH — depth byte (IGNORED)
Overwritten from the format table in `rwD3D9SetRasterFormat` before anything reads it. Any value
accepted.

### TXD-MASK — mask name (IGNORED on D3D9)
Stored only; the native reader never opens mask files. Length ≥ 32 → truncated + RwErrorSet.

### TXD-DEVICEID — dictionary deviceId (IGNORED)
Game reader never looks at it (only the unused stock reader compares it with
`rwDEVICESYSTEMGETID`).

---------------------------------------------------------------------------------------------------

## 4. Limits

| what | value | evidence |
|---|---|---|
| TXD slots (pool) | 5000, streaming ids 20000..24999 | 0x731F5F `push 0x1388`; 0x40C6B0 |
| IMG entry base name | ≤ 20 chars (+`.txd`) | 0x5B6222 |
| texture name / mask | ≤ 31 chars effective (32-byte field, NUL forced at [31]) | 0x7F38D0-0x7F3902 |
| textures per TXD | u16 (65535); vanilla max 165; no engine cap | 0x730FC0 loop |
| rasterFormat nibble | 1..6, 0xA (0xB..0xF read past the table) | 0x85C670 |
| numLevels | u8; must be 1 (no MIPMAP) or `floor(log2(max(w,h)))+1` (MIPMAP); D3D chain for 65535 = 16 | 0x730518, 0x4CDC86 |
| width/height | u16; ≤ D3DCAPS MaxTextureWidth/Height; pow2 required only on POW2-cap devices | 0x7FED70 |
| chunk versions | 0x34000..0x36003 (libID 0x1803FFFF) | 0x7ED337, 0x4CD848 |
| STRUCT of 0x16 | exactly 4 bytes | 0x730FE1 |
| filter / addressing | 1..6 (7 = aniso set at runtime) / 1..4 | tables 0x884950 / 0x88493C |
| palette | PAL4 0x80 B, PAL8 0x400 B; PAL4 never creatable, PAL8 needs D3DFMT_P8 | 0x4CDBCC, 0x4CC5C0 |
| TXD file size | must fit `CStreaming::ms_streamingBufferSize` (sized from the largest IMG entry, u16 sectors ⇒ < 128 MB); files > half buffer use the two-stage StartLoadTxd/FinishLoadTxd | 0x5B61E6, 0x40C6B0 |
| per-level bytes | DXT `⌈w/4⌉·⌈h/4⌉·{8,16}` (≥ 1 block); uncompressed `w·bpp/8·h` | D3D9 LockRect semantics, 0x4CD360 (`pitch·h`, compressed `pitch/4·h`) |

---------------------------------------------------------------------------------------------------

## 5. Validator hook design and self-test

### 5.1 Hook points

1. **Per texture, before the engine touches the raster** — replace `RwGlobals.stdFunc[26]`
   (`*(void**)(*(uint8_t**)0xC97B24 + 0xB0)`) after `RwEngineStart`, or trampoline 0x4CD820 (its only
   caller is `call [ecx+0xb0]` at 0x730ED0; no register liveness beyond the cdecl contract — the
   caller reloads everything from `[esp+4]`). Signature: `RwBool cdecl (RwStream*, RwTexture**,
   RwInt32 length)`. `length` is the exact byte count of the 0x15 payload (STRUCT header + payload +
   optional EXTENSION). Procedure:
   * `RwStreamRead(stream, buf, length)` (memory stream: or read directly from `memBlock+position`,
     `stream+0x14/+0x0C`, then `RwStreamSkip`); parse §1.2 in `buf`; compute the expected level sizes;
     check platform, version, rasterType==4, nibble∈{1,2,3,5,6}, bit3⇔DXT fourcc, no 0x1000/0x2000/0x4000,
     flags∈{8,9} (bit1/bit2 clear), numLevels rule, every `size ≤ expected` (fatal if greater; warn if
     `0 < size < expected`), byte sum == STRUCT length, name 1..31 chars + NUL, mask ≤ 31,
     filter 1..6, addr 1..4, no duplicate (keep a per-TXD set: `RwTexDictionaryFindNamedTexture` on the
     dictionary is not yet available inside the hook, so compare names collected so far).
   * If OK: open `RwStreamOpen(rwSTREAMMEMORY=3, READ=1, &{buf,length})` and call the original
     0x4CD820 on it (it consumes exactly what it needs; the outer stream is already past the chunk,
     which also neutralises TXD-STRUCT-TRAILING). If not OK: log and return 0 (the dictionary is
     dropped exactly as the engine would for a corrupt file) or hand back a 4×4 placeholder texture
     (`RwRasterCreate(4,4,32,0x504)` + `RwTextureCreate` + `RwTextureSetName`) to keep the TXD usable.
   * After the original returns, `(*pTexture)->raster` is complete: read back `ext->alpha`,
     `raster+0x23`, `RwRasterGetNumLevels` 0x7FB160 to log the effective state; `RwRasterLock(raster,
     level, rwRASTERLOCKREAD=2)` gives `pBits/stride` if pixel checks are wanted (managed pool, safe).
2. **Per TXD** — hook `CTxdStore::LoadTxd` 0x731DD0 / `StartLoadTxd` 0x731930 / `FinishLoadTxd`
   0x731E40 (all cdecl `(int slot, RwStream*)`; callers 0x40C6B0, 0x7320F0). At entry: peek the
   0x16 + STRUCT headers at `memBlock+position` and refuse when STRUCT `len != 4` (TXD-STRUCTLEN is
   the one fault that happens before any texture hook runs) or the versions are out of range. At
   exit: `ms_pTxdPool[slot].txd` (0xC8800C → `*pool + slot*0xC`) holds the dictionary →
   `RwTexDictionaryForAllTextures` 0x7F3730 to count, detect duplicates (`FindNamedTexture` returning a
   different pointer than the visited one), and report per-texture summaries. The inuhook convention
   (naked pushad/popad registrator stubs) should be kept for these game-side entry points.
3. Optional render-side check (TXD-MISSING): hook `RwTextureRead` 0x7F3AC0 and log every name that
   comes back NULL together with `RwTexDictionaryGetCurrent` 0x7F3A90 (the TXD slot being used).

### 5.2 Self-test: loading many TXDs

* **Streaming path** (exercises exactly the production code): for each slot `s` in 0..4999 with a
  CD entry, `CStreaming::RequestModel(20000+s, 2 /*GAME_REQUIRED, so 0x40C6B0's
  AreTexturesUsedByRequestedModels drop does not apply*/)`, `CStreaming::LoadAllRequestedModels(false)`
  0x40EA10, then `CStreaming::RemoveModel(20000+s)` 0x4089A0 (→ `CTxdStore::RemoveTxd` 0x731E90).
  `RequestModel` requests the `txdp` parent automatically and 0x40C6B0 defers the child until the
  parent's `GetTxd` is non-NULL, so parents are handled. Files larger than half the streaming buffer
  take the `StartLoadTxd/FinishLoadTxd` route — the stdFunc hook sees every texture either way.
  Load one slot at a time (a few MB each; `ms_memoryAvailable` and the D3DTextureBuffer recycling are
  untouched by this loop). Do not touch the pinned-model pool.
* **Loose-file path** (for the exporter's output without an IMG): `slot = CTxdStore::AddTxdSlot("inu_chk")`
  (only once; 5000-slot pool!), `CTxdStore::LoadTxd(slot, "F:\\path\\file.txd")` 0x7320B0 (check the
  file exists first — the thunk 0x7320D0 loops forever on open failure), inspect, then
  `CTxdStore::RemoveTxd(slot)` and keep the slot for the next file (`RemoveTxdSlot` 0x731CD0 frees it).
  Here the stream is a FILE stream, so the texture hook must use `RwStreamRead` rather than
  `memBlock`.
* Both routes run `SetupTxdParent`; the loose-file route leaves `parentIndex = -1`.

### 5.3 Exporter-side checks (things the engine never verifies)

1. STRUCT of the dictionary = exactly 4 bytes; every chunk stamped `0x1803FFFF`.
2. platform 9; rasterType 4; flags 8 (DXT1) / 9 (DXT with alpha); no bit1/bit2; rasterFormat
   nibble 1..6 (0x200/0x100 for DXT1, 0x300 for DXT3, 0x500 for A8R8G8B8 …) with `0x8000` iff a full
   mip chain follows; never 0x1000/0x2000/0x4000.
3. `numLevels == 1` without 0x8000, `== floor(log2(max(w,h)))+1` with it; each level's `size`
   exactly `max(1,⌈w_l/4⌉)·max(1,⌈h_l/4⌉)·{8,16}` (DXT) or `w_l·bpp/8·h_l`; level bytes sum to the
   STRUCT payload; no trailing bytes; EXTENSION chunk length 0.
4. width/height power of two, ≥ 4 for DXT top level (below 4 is legal but pointless), ≤ 2048
   to stay under the caps of old cards (engine cap is the device's MaxTextureWidth).
5. Names 1..31 ASCII chars, NUL-padded to 32, unique per TXD case-insensitively, identical to the
   DFF material texture names (≤ 31 chars there too); mask empty.
6. filter 2 (LINEAR, no mips) or 6 (LINEARMIPLINEAR); addressing 0x1100 (wrap/wrap).
7. alpha flag == "the DXT format carries alpha and the image needs it" (DXT1 opaque → 8).
8. Total distinct TXD names across the mod's IDE/IMG < ~1000 additional (5000 pool); IMG base names ≤ 20 chars.
9. No `txdp` cycles; every parent present in an IMG.
