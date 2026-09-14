# AUDIO files of a custom build — gta_sa.exe 1.0 US (base 0x400000)

Sources: Ghidra dumps `E:\RE\asm_data2\<ADDR>.c/.asm` (CAEBankLoader / CAEMP3BankLoader 0x4DFB10-0x4E0670,
CAEAudioHardware::Initialise 0x4D9930, LoadSoundBank 0x4D88A0, LoadSound 0x4D8ED0, CAudioEngine::Initialise
0x5B9C60, CAERadioTrackManager::Initialise 0x5B9390, CAECutsceneTrackManager::Initialise 0x5B9630,
CAEStreamThread::Initialise 0x4F1680, CAEVehicleAudioEntity::Initialise 0x4F7670,
SurfaceInfos_c::LoadSurfaceAudioInfos 0x55F2B0, CAEMP3TrackLoader::IsCurrentAudioStreamAvailable 0x4E0BD0),
capstone disassembly of the exe (scratchpad `csdis.py`) for the functions with no dump:
CAEMP3BankLoader::Initialise 0x4E08F0, ::LoadSound 0x4E07A0, ::Service tail 0x4E00B0,
CAEMP3TrackLoader ctor 0x4E0930 / LoadStreamPackTable 0x4E0970 / LoadTrackLookupTable 0x4E09F0 /
GetTrackInfo 0x4E0A70 / Initialise 0x4E0C50 / GetDataStream 0x4E0D20, CAEDataStream ctor 0x4DC620 /
Initialise 0x4DC2B0 / FillBuffer 0x4DC1C0, CAEVorbisDecoder ctor 0x5026B0 / ReadCallback 0x502580,
CAEAudioHardware::Service 0x4D9870, CAudioEngine::Service 0x507750, CAEStreamThread::GetTrackPlayTime
0x4F1530, SurfaceInfos_c::GetSurfaceIdFromName 0x55D220, CGame::InitialiseCoreDataAfterRW 0x5BFA90.
gta-reversed / plugin-sdk used for names, the eSoundBankSlot enum and the (bank, slot) pairs of the
static LoadSoundBank calls; every crash / garbage claim below cites the exe. Raw exe tables read with
`readva.py` / struct (voice→bank tables 0x8AF700 / 0x8BA470 / 0x8BBED0 / 0x8BE5B8 / 0x8C4288 / 0x8C64AC,
vehicle audio settings 0x860AF0). Vanilla data: `D:\Grand Theft Auto San Andreas\audio\*`, `data\surfaud.dat`.
Vanilla was run through every rule below (python, same arithmetic) — 0 findings above Info.

Check ids: `AUD-nn`. Severity legend as in the other reports: **Crash** = access violation / heap
overrun / init failure that dereferences NULL later, **Error** = engine proceeds with visibly wrong
audio, **Warning** = suspicious / silently ignored, **Info**.

---------------------------------------------------------------------------------------------------

## 0. When the audio files are read

`CGame::InitialiseCoreDataAfterRW` 0x5BFA90 (before `CGame::Initialise`, i.e. before any IMG is opened):

| call at | function | files |
|---|---|---|
| 0x5BFAD6 | `SurfaceInfos_c::Init` 0x55F420 → `LoadSurfaceAudioInfos` 0x55F2B0 | `data\surfaud.dat` |
| 0x5BFAF9 | `CAudioEngine::Initialise` 0x5B9C60 → `CAEAudioHardware::Initialise` 0x4D9930 | see below |

`CAEAudioHardware::Initialise` 0x4D9930:

1. `new CAEMP3BankLoader` (0x6E4 bytes), `new CAEMP3TrackLoader` (0x1C bytes)
2. `CAEMP3BankLoader::Initialise` 0x4E08F0 = `LoadBankSlotFile` 0x4E0590 (`AUDIO\CONFIG\BANKSLOT.DAT`) &&
   `LoadBankLookupFile` 0x4DFBD0 (`AUDIO\CONFIG\BANKLKUP.DAT`) && `LoadSFXPakLookupFile` 0x4DFC70
   (`AUDIO\CONFIG\PAKFILES.DAT`, then `CdStreamOpen("AUDIO\SFX\<name>")` for every pak)
3. `CAEMP3TrackLoader::Initialise` 0x4E0C50 = `LoadStreamPackTable` 0x4E0970 (`AUDIO\CONFIG\STRMPAKS.DAT`) &&
   `LoadTrackLookupTable` 0x4E09F0 (`AUDIO\CONFIG\TRAKLKUP.DAT`) && open `AUDIO\STREAMS\<pak[0]>`
4. DirectSound, `CAEStreamingChannel`, `CAEStreamThread::Initialise` 0x4F1680 (creates a **second**
   CAEMP3TrackLoader and runs step 3 again, result ignored), sets `m_bInitialised` (+0) = 1.

If step 2 or 3 fails, `CAEAudioHardware::Initialise` returns 0 without touching `m_pStreamingChannel`
(+0xE10, and CAEStreamThread +8 stays NULL) and `CAudioEngine::Initialise` returns at once
(`CLoadingScreen::Continue` is never called). The next `CAudioEngine::Service` 0x507750 calls
`CAEAudioHardware::GetTrackPlayTime` 0x4D8F60 (0x507791) → `CAEStreamThread::GetTrackPlayTime` 0x4F1530:
`mov ecx,[ecx+8]; mov eax,[ecx]` with `[ecx+8] == NULL` → **access violation at 0x4F153A**.
`CAEAudioHardware::Service` 0x4D9870 likewise has no initialised-guard (`mov eax,[esi+0xE0C]; mov ecx,[eax]`
at 0x4D987A). So every "init fails" case below is a **Crash** on the first frame after init.

After the hardware: `CAERadioTrackManager::Initialise` 0x5B9390 and `CAECutsceneTrackManager::Initialise`
0x5B9630 read **no files** (pure member init, see the dumps). Then `AUDIO\CONFIG\EVENTVOL.DAT` is read by
`CAudioEngine::Initialise` itself: `OpenFile`, `Read(buf, 0xB159)`, must return exactly 0xB159.

`AudioEventHistory.txt` is never opened by the exe (no string reference) — Info only.

---------------------------------------------------------------------------------------------------

## 1. Record layouts (verified by the load code + vanilla data)

### 1.1 `AUDIO\CONFIG\BankSlot.dat` — `LoadBankSlotFile` 0x4E0590

```
u16  count                         ; CFileMgr::Read(2) into m_nBankSlotCount (+0xC)   (0x4E05D3-0x4E05DA)
count × CAEBankSlot (0x12D4 = 4820 bytes):
   +0x00 u32 offset                ; IGNORED: recomputed by CalculateBankSlotsInfosOffsets 0x4DFBA0
   +0x04 u32 size                  ; bytes of sample data this slot can hold   <-- the only field used
   +0x08 u32, +0x0C u32            ; unused on PC
   +0x10 i16 bankId  +0x12 i16 numSounds  +0x14 400×12 sound table   ; runtime state, file content ignored
```

* `GetFileLength <= 2` → return 0 (0x4E05BE-0x4E05D2). Otherwise `new(count*0x12D4)` (0x4E05E9),
  `Read(len-2)` into it (0x4E05FD); `len-2 != bytesRead` → free, `m_paBankSlots = 0`, return 0
  (0x4E060B-0x4E0629).
* `CalculateBankSlotsInfosOffsets` 0x4DFBA0: `offset[0]=0, offset[i]=offset[i-1]+size[i-1]`;
  `m_nBufferSize` (+0x18) = `offset[last]+size[last]` = Σ size (0x4E0639-0x4E0658); `m_pBuffer` (+0x1C) =
  `CMemoryMgr::Malloc(Σ size)` (vanilla Σ = 8 578 144 bytes, 45 slots).
* Vanilla: 216 902 bytes = 2 + 45×4820. Slot sizes (bytes): 0:724416 1:158464 2:1418752 3:441856
  4:308672 5:1048576 6:304128 7-16:74496 17:246680 18:148224 19:821248 20-24:83456 25:98304
  26-28:148456 29:253630 30:99584 31:48640 32:152064 33:16028 34:12910 35:9114 36:10124 37:30178
  38:54976 39:39680 40:290048 41:105472 42:86272 43:2816 44:39680.

### 1.2 `AUDIO\CONFIG\BankLkup.dat` — `LoadBankLookupFile` 0x4DFBD0

```
n × AEBankLookup (12 bytes), n = fileLength / 12 (int16, 0x4DFC10-0x4DFC24), bank id = record index:
   +0 u8  pakFileIndex             ; index into PakFiles.dat  (copied at 0x4E072E / 0x4E0897; used by Service 0x4E00D6)
   +1 u8[3] pad
   +4 u32 offset                   ; BYTE offset of the bank header inside the pak (any alignment; Service
                                   ;   reads from sector offset>>11 and re-adds offset&0x7FF, 0x4E00CC-0x4E00DF)
   +8 u32 size                     ; bytes of sample data AFTER the 0x12C4 header
```

* `GetFileLength == 0` → return 0. `new(n*12)`, `Read(fileLength)` (0x4DFC28-0x4DFC33) — the read length
  is the **whole file**, the allocation is `n*12`.
* Vanilla: 8520 bytes = 710 records, pak indices 0..8, banks laid out contiguously
  (`offset[i+1] == offset[i] + 0x12C4 + size[i]` inside each pak — true for all 701 in-pak neighbours).
  Bank id ranges per pak (vanilla, fixed by the exe tables): FEET 0-6, GENRL 7-143, PAIN_A 144-146,
  SCRIPT 147-364, SPC_EA 365-410 (EMG voices), SPC_FA 411-428 (GFD voices), SPC_GA 429-637 (GEN voices),
  SPC_NA 638-689 (GNG voices), SPC_PA 690-709 (PLY voices).

### 1.3 `AUDIO\CONFIG\PakFiles.dat` — `LoadSFXPakLookupFile` 0x4DFC70

```
n × 52 bytes, n = fileLength / 52 (int16, 0x4DFCC4-0x4DFCD5):
   char name[]  NUL-terminated (vanilla: name, NUL, 0xCD padding to 12, then 40 zero bytes)
```

* For each record: `strlen(record)` bytes copied after `"AUDIO\SFX\"` into a **stack buffer at
  esp+0x14 (frame 0x84, ~0x6C bytes usable)** (0x4DFD40-0x4DFD94), then `CdStreamOpen(path)` 0x4067B0
  (0x4DFD9D); the returned handle (`slotIndex << 24`, or **0 when CreateFile fails** — per gta-reversed
  CdStreamOpen; the exe body sits in the SecuROM section 0x1564A90 and was not re-verified) is stored in
  `m_paStreamHandles[i]` (+0x20, `new(n*4)`, 0x4DFD20-0x4DFDA5). The record buffer is freed afterwards
  (0x4DFDC3) — names are not kept.
* The audio paks are opened before any IMG (§0), so vanilla paks occupy CdStream file slots 0..8
  (FEET = slot 0). A pak that fails to open still consumes a slot (INVALID_HANDLE_VALUE stored) but its
  banks read through handle 0 = **slot 0's file** (FEET).

### 1.4 Bank data inside `AUDIO\SFX\<pak>` (read by `CAEMP3BankLoader::Service` 0x4DFE30)

```
at BankLkup.offset:
   +0x000 i16 numSounds            ; copied to CAEBankSlot+0x12
   +0x002 i16 pad
   +0x004 400 × { u32 sampleOffset ; bytes from start of sample data (sound 0 = 0)
                  u32 loopOffset   ; 0xFFFFFFFF = no loop, else sample index (×2 → bytes, UpdateVirtualChannels 0x4E0450)
                  u16 sampleRate   ; Hz
                  i16 headroom }   ; GetSoundHeadroom 0x4E01E0 = headroom * const_0x858C58
   +0x12C4 u8 sampleData[size]     ; 16-bit mono PCM
```

Whole-bank load (request.soundId == -1): read `((size+0x12C4)>>11)+2` sectors from `offset>>11`
(state 1), then copy `size` bytes of sample data to `m_pBuffer + slot.offset` (state 2, the
`rep movsd/movsb` from `buffer+0x12C4`) and the 0x12C0-byte sound table to `slot+0x14`, `slot+0x10 = bankId`,
`slot+0x12 = numSounds`. **No comparison of `size` against `slot.size`** anywhere in 0x4DFE30-0x4E01AF.
Single-sound load (`LoadSound` 0x4E07A0, `soundId < 400` enforced at 0x4E07B4): 4 sectors for the header,
`soundSize = (soundId < numSounds-1 ? table[soundId+1].offset : BankLkup.size) - table[soundId].offset`
(state 2, non-(-1) branch), then `(soundSize>>11)+2` sectors (0x4E00B1-0x4E0103) and a copy of `soundSize`
bytes to `m_pBuffer + slot.offset` (state 3). Again no check against `slot.size`.

### 1.5 `AUDIO\CONFIG\StrmPaks.dat` — `LoadStreamPackTable` 0x4E0970

`n × 16 bytes` (n = fileLength/16 at 0x4E09D2-0x4E09D9, stored +8; buffer +0x10): NUL-terminated pak name
(vanilla 17 entries, entry 2 is an empty string, entries repeat "NJ"/"CH"). Read with `CreateFileA`/`ReadFile`
(no CFileMgr); missing file → return 0 (0x4E0997) → **init fails (Crash, §0)**; a 0-byte file gives n = 0
and passes.

### 1.6 `AUDIO\CONFIG\TrakLkup.dat` — `LoadTrackLookupTable` 0x4E09F0

```
n × 12 bytes (n = fileLength/12 at 0x4E0A46-0x4E0A51, stored +4; buffer +0xC), track id = record index:
   +0 u8  streamPakIndex           ; index into StrmPaks.dat
   +1 u8[3] pad
   +4 u32 offset                   ; byte offset of the 0x1F84-byte track header inside AUDIO\STREAMS\<pak>
   +8 u32 length                   ; bytes of Ogg data that follow the header (0 = "to end of file", 0x4DC2F9-0x4DC30C)
```

`GetTrackInfo` 0x4E0A70 opens `<pak>` at `offset` and `FillBuffer`s 0x1F84 bytes (the beat/length header,
0x4E0B3D / 0x4E0B9D); `GetDataStream` 0x4E0D20 builds the Ogg data stream at `offset + 0x1F84` with `length`
(0x4E0E1A). All stream bytes are XOR-encrypted with the 16-byte key
`EA 3A C4 A1 9A A8 14 F3 48 B0 D7 23 5D E8 96 A2` (key index = file offset & 15 — verified: vanilla `BEATS`
decrypts to `OggS` at 8068 = offset 0 + 0x1F84). Header = 1000 × {i32 timeMs, i32 control} +
8 × {i32 length, i32 extra} + 4 bytes = 8068. Vanilla: 23 064 bytes = 1922 tracks, pak indices 0,1,3..16
(never 2).

### 1.7 `AUDIO\CONFIG\EventVol.dat` — `CAudioEngine::Initialise` 0x5B9C60

Opaque 0xB159 (45 401) byte blob (`new(0xB159)`, `Read(0xB159)`, must return 0xB159). Vanilla file is 45 402
bytes (1 trailing byte ignored). Byte +0x4D is read by `CAEVehicleAudioEntity::Initialise`.

### 1.8 `data\surfaud.dat` — `SurfaceInfos_c::LoadSurfaceAudioInfos` 0x55F2B0

Text, `CFileLoader::LoadLine` 0x536F80 (same reader as IDE — see textdata_path.md §1.1; `#` comment only
as first char; `,`/control chars → space). Per line: `sscanf("%s %d %d %d %d %d %d %d %d %d", name[64],
9 ints)`; `GetSurfaceIdFromName(name)` 0x55D220 = 179 exact, case-sensitive `repe cmpsb` compares
(`DEFAULT`=0 … `RAILTRACK`=178); **unknown name returns 0** (`setne al; dec eax; and eax,0xB2` at
0x55E5AB-0x55E5B1), i.e. the line is applied to surface DEFAULT. The 9 ints set bits 10..18 of
`SurfaceInfo[id]` (`this + 0x98 + id*12`). Vanilla: 179 data lines, one per surface.

### 1.9 Where bank / track ids come from (all hard-coded in the exe — the data files only have to cover them)

* Vehicle engine banks: `gVehicleAudioSettings` 0x860AF0, **231 × 36 bytes for model ids 400..630**
  (entries 612..630 are `type 10 / bank -1 / -1` dummies; the bytes from 0x862B6C on are a different
  table). `CAEVehicleAudioEntity::Initialise` 0x4F7670 copies entry `(modelId-400)` unconditionally
  (`&PTR_0x860AF0 + (model-400)*9` dwords). Fields: +0 u8 soundType (10 = none → `if (9 < type) return`),
  +2 i16 playerBank, +4 i16 dummyBank, +6 u8 bass, +8 f32, +0xC f32, +0x10 u8 hornTone, +0x14 f32 hornHigh,
  +0x18 u8 doorSound, +0x1A i8 radioId, +0x1B u8 radioType, +0x1C u8 vehTypeForAudio, +0x20 f32.
  Bank ids used: player ≤ 142, dummy ≤ 142; all in GENRL/FEET.
* Ped voices: `int16` tables voice→bank: GEN 0x8AF700 [209] = 429..637, EMG 0x8BA470 [46] = 365..410,
  PLY 0x8BBED0 [20] = 690..709, GNG 0x8BE5B8 [52] = 638..689, GFD 0x8C4288 [18] = 411..428,
  PAIN 0x8C64AC [3] = 144..146 (all dumped from the exe). **Highest bank id referenced by the exe = 709.**
* Static whole-bank loads (gta-reversed call sites, bank ids from its eSoundBank enum, all verified to fit
  in vanilla): (59→slot 0) (60→1) (39→2) (27→3) (52→4) (143→5) (105→6) (74→17) (13→18) (138→19)
  (51→31) (128→32) (FEET 0→41; FEET 1..6→30) (28,29→42) (SCRIPT 44→40); vehicle dummy banks → slots
  7..16; player engine bank → slot 40; ped speech single sounds → slots 20..24 (NPC) / 25 (player, PLY
  banks); script speech (SCRIPT 147..364) single sounds → slots 26..29; police scanner → 33..37.
* Radio / cutscene / ambience track ids: station tables in the exe reference ids < 1922 (vanilla TrakLkup
  count); not enumerated individually.

---------------------------------------------------------------------------------------------------

## 2. Rules

Each rule: id · severity · condition · what the engine does (with address) · how the checker decides it.
"vanilla" = the stock 1.0 US files pass (checked).

### 2.1 Existence / init-fatal (mechanism of §0)

| id | sev | condition | engine behaviour | how to check |
|---|---|---|---|---|
| AUD-01 | Crash | `audio\CONFIG\BankSlot.dat` missing or length ≤ 2 | `LoadBankSlotFile` 0x4E0590 returns 0 (0x4E05AC / 0x4E05C1) → `CAEMP3BankLoader::Initialise` 0x4E08F0 returns 0 (0x4E0917) → hardware init fails → NULL `m_pStreamingChannel` deref in `CAEStreamThread::GetTrackPlayTime` 0x4F153A on the first `CAudioEngine::Service` | file exists, size > 2 |
| AUD-02 | Crash | `BankSlot.dat` length ≠ 2 + count×4820 (count = first u16) | shorter: `Read` returns less than `len-2` → free + return 0 (0x4E060B-0x4E0629) → AUD-01 path. Longer: `Read(len-2)` into a `count×4820` heap block → **heap overrun** by the excess bytes (0x4E05FD) | `size == 2 + count*4820` |
| AUD-03 | Crash | `audio\CONFIG\BankLkup.dat` missing or empty | `LoadBankLookupFile` 0x4DFBD0 returns 0 (0x4DFBEC / 0x4DFC0C) → AUD-01 path | exists, size > 0 |
| AUD-04 | Crash | `BankLkup.dat` length not a multiple of 12 | allocation `(len/12)*12`, `Read(len)` → heap overrun by `len % 12` bytes (0x4DFC28-0x4DFC33) | `size % 12 == 0` |
| AUD-05 | Crash | `audio\CONFIG\PakFiles.dat` missing or empty | `LoadSFXPakLookupFile` 0x4DFC70 returns 0 (0x4DFC92 / 0x4DFCBA) → AUD-01 path | exists, size > 0 |
| AUD-06 | Crash | `PakFiles.dat` length not a multiple of 52 | `new((len/52)*52)`, `Read(len)` → heap overrun by `len % 52` bytes (0x4DFCD9-0x4DFCE4) | `size % 52 == 0` |
| AUD-07 | Crash | `audio\CONFIG\StrmPaks.dat` missing | `LoadStreamPackTable` 0x4E0970: `CreateFileA` = INVALID → return 0 (0x4E0997) → `CAEMP3TrackLoader::Initialise` 0x4E0C50 returns 0 (0x4E0C67) → hardware init fails → AUD-01 crash | exists (0 bytes is accepted) |
| AUD-08 | Crash | `audio\CONFIG\TrakLkup.dat` missing | `LoadTrackLookupTable` 0x4E09F0 returns 0 (0x4E0A17) → same as AUD-07 | exists |
| AUD-09 | Crash | `audio\streams\<StrmPaks[0]>` missing (vanilla: `NJ`) | `CAEMP3TrackLoader::Initialise` 0x4E0C50 builds `AUDIO\STREAMS\` + first 16-byte name, `CFileMgr::OpenFile` (0x4E0CAB); on failure tries the DVD path (`getDvdGamePath` 0x747300) and returns 0 (0x4E0D1A) → hardware init fails → AUD-01 crash | name = first 16 bytes of StrmPaks.dat up to NUL; file must exist (size may be 0 — vanilla `AA` etc. are 0 bytes) |
| AUD-10 | Error | `audio\CONFIG\EventVol.dat` missing or shorter than 45 401 (0xB159) bytes | `CAudioEngine::Initialise` 0x5B9C60: `OpenFile == 0` or `Read != 0xB159` → returns before `CAEFrontendAudioEntity::Initialise` … `CAECollisionAudioEntity::Initialise` and before `CLoadingScreen::Continue`; the hardware IS initialised so no immediate crash, but every audio entity keeps its zeroed state and `CAEVehicleAudioEntity::Initialise` reads volume byte +0x4D of a never-filled `new(0xB159)` buffer. Downstream behaviour not verified — treat as broken audio, possible later crash | exists, `size >= 45401` |

### 2.2 Bank tables — counts and cross references

| id | sev | condition | engine behaviour | how to check |
|---|---|---|---|---|
| AUD-11 | Crash | `BankSlot.dat` count < 45 | The exe requests slots 0..44 (§1.9). `LoadSoundBank` 0x4E0670 / `LoadSound` 0x4E07A0 accept `slot <= count` (**`cmp di,[esi+0xC]; jg`** at 0x4E06A1 / 0x4E07E0 — off by one) so slot == count is queued with `slotInfo = m_paBankSlots + count*0x12D4` (0x4E071F), and `Service` 0x4DFE30 writes the 0x12C0-byte sound table + bankId there → **heap overrun past the slot array**. Slots > count are silently dropped (0x4E0790) | `count >= 45` (Crash); count in 46..60 Info; count > 60 Info (`m_aBankSlotSound[60]` at +0x66C only matters for slots ≥ 60, which the exe never requests) |
| AUD-12 | Crash | `BankLkup.dat` record count < 710 | Highest bank id referenced by the exe is 709 (ped PLY voices, §1.9); bank ids fill 0..709 without gaps, so for any count < 710 some referenced bank == count. `LoadSoundBank` / `LoadSound` accept `bankId <= count` (**`cmp ebp,eax; jg`** 0x4E068B / 0x4E07CB — off by one) but `GetBankLookup` 0x4E01B0 uses `<` (0x4E01C0) and returns NULL → **`mov cl,[eax]` with eax = 0 at 0x4E072E / 0x4E0897** → access violation the first time that bank is requested (a ped speaks / a car spawns). Banks > count are silently never loaded (no sound) | `size/12 >= 710` |
| AUD-13 | Error | `BankLkup.dat` record with `pakFileIndex >= PakFiles.dat count` | `Service` 0x4DFE30 reads `m_paStreamHandles[pak]` (`[this+0x20] + pak*4`, 0x4E00E2-0x4E00E5) beyond the `count*4` heap block → garbage CdStream handle (`handle>>24` indexes `gStreamFileHandles[32]`) → read fails / reads a random open file → the bank never becomes "loaded", its request stays REQUESTED, silence for that bank | `rec.pak < pakCount` for every record |
| AUD-14 | Error | `PakFiles.dat` entry names a file that does not exist as `audio\SFX\<name>` | `CdStreamOpen` returns 0 → banks of this pak are read through stream slot 0 (= the first pak, vanilla FEET) at their own offsets → wrong samples / noise, or a failed read → silent bank. Not a crash | for each of the `count` records: name = bytes up to first NUL; `audio\SFX\<name>` must exist. Also report the record index so the user sees which bank range dies (§1.2 ranges) |
| AUD-15 | Warning | `PakFiles.dat` record without a NUL inside its 52 bytes | `strlen` runs into the following records (0x4DFD71) → the joined string is opened as one name → not found → AUD-14 behaviour; if the joined run exceeds ~0x6C bytes the `rep movsb` at 0x4DFD94 **smashes the stack** (Crash) | NUL present within 52 bytes; escalate to Crash if the NUL-free run from this record's start is ≥ 100 bytes |
| AUD-16 | Error | `BankLkup` record with `offset + 0x12C4 + size > size of audio\SFX\<pak>` | `CdStreamRead` past EOF → short/failed read → the sample buffer keeps stale data (garbage sound) or the request never completes (silence) | compare against the pak file length |
| AUD-17 | Error | bank header `numSounds > 400` | Only 400 entries are copied (0x4B0 dwords); `GetSoundBuffer` 0x4E0280 accepts `soundId < numSounds` and reads `slot + 0x14 + soundId*12` beyond the slot record → offset/length taken from the **next slot's** memory → bogus sample pointer handed to DirectSound (garbage / possible AV). `numSounds < 0` is not produced by any vanilla bank and is treated as "single sound" by `Service` (`== -1`) | read i16 at bank offset; `0 <= numSounds <= 400` (numSounds == 0 is legal — vanilla SPC_GA banks 435, 504, 507, 512, 513, 523, 555, 580, 585, 588, 593) |
| AUD-18 | Error | bank header sound table not monotonic or `table[0].offset != 0` or `table[numSounds-1].offset > size` | `soundSize = next.offset - this.offset` (Service state 2; whole bank: `GetSoundBuffer` 0x4E0300-0x4E0330) goes negative/huge → `Malloc(huge)` / copy beyond the slot → heap overrun (Crash) or wrong sample extents | for the first `numSounds` entries: offsets non-decreasing, first == 0, last ≤ size |
| AUD-19 | Warning | bank replaced with **fewer sounds than vanilla** (any bank whose vanilla numSounds is listed in Appendix A) | Sound ids are hard-coded per bank in the exe; `LoadSound` with `soundId >= numSounds` (allowed, only `< 400` is checked, 0x4E07B4) makes `Service` compute `soundSize = BankLkup.size - table[soundId].offset` from an **uninitialised table entry** (state 2) → oversized read + copy into the slot (heap overrun, Crash) or garbage. Whole-bank plays just skip (`GetSoundBuffer` returns 0 via `cmp eax,ebp; jle` at 0x4E02C2) | `numSounds(bank) >= vanillaNumSounds[bank]` (Appendix A); Warning because only banks played by id trigger it |

### 2.3 Bank sizes vs slot sizes (the copy in `Service` never checks the slot size)

| id | sev | condition | engine behaviour | how to check |
|---|---|---|---|---|
| AUD-20 | Crash | a whole-bank-loaded bank bigger than its slot: `BankLkup.size > slot.size` for the static pairs of §1.9 (bank→slot): 59→0, 60→1, 39→2, 27→3, 52→4, 143→5, 105→6, 74→17, 13→18, 138→19, 51→31, 128→32, FEET 0→41, FEET 1..6→30, 28/29→42, 44→40; every **vehicle dummy bank** (+4 of each 0x860AF0 entry, §1.9) → slots 7..16 (all 74 496 in vanilla, use `min(size[7..16])`); every **vehicle player bank** (+2) → slot 40 | `memcpy(m_pBuffer + slot.offset, data, BankLkup.size)` in Service state 2 overruns into the following slots (audible corruption of other banks) and, for the last slots, past `m_pBuffer` (**heap overrun → crash**) | compare `size` against the slot's `size` field from BankSlot.dat (vanilla: all fit — e.g. 39: 1 214 242 ≤ 1 418 752; dummy banks ≤ 63 952 ≤ 74 496; player banks ≤ 253 518 ≤ 290 048) |
| AUD-21 | Crash | a single sound bigger than its speech slot: for banks 144..146 and 365..689 any `soundSize > min(size[20..24])` (vanilla 83 456); for PLY banks 690..709 any `soundSize > size[25]` (98 304) | single-sound copy at state 3 of `soundSize` bytes into the slot → same overrun as AUD-20 | `soundSize = table[i+1].offset − table[i].offset` (last: `size − offset`) per bank header; vanilla max = 70 938 (bank 421) / 85 468 (bank 697, ≤ 98 304) |
| AUD-22 | Error | script speech sound (banks 147..364) bigger than the mission slots: `soundSize > size[29]` (253 630) → Crash-class overrun; `size[26]` (148 456) < soundSize ≤ `size[29]` → Warning (only fits in slot 29) | as AUD-21 with slots 26..29 (slot choice comes from the script, undecidable offline; vanilla max 210 200) | per bank header |
| AUD-23 | Info | Σ slot sizes (= `m_nBufferSize`) differs from vanilla 8 578 144, or any `BankSlot` size is 0 | `CMemoryMgr::Malloc(Σ)`; a 0-size slot makes every load into it an overrun of the next slot | report Σ; flag `size == 0` as Error |

### 2.4 Stream (music / speech-stream) tables

| id | sev | condition | engine behaviour | how to check |
|---|---|---|---|---|
| AUD-24 | Error | `TrakLkup.dat` record count < 1922 | radio/cutscene/ambience track ids are exe constants < 1922; `GetTrackInfo` 0x4E0A70 / `GetDataStream` 0x4E0D20 return NULL for `id > count` (0x4E0A91 `ja` / 0x4E0D41 `jae`) → `CAEVorbisDecoder::ReadCallback` 0x502580 returns 0 on a NULL data source (0x502584) → `ov_open` fails → decoder deleted (CAEStreamThread::Service 0x4F13A6-0x4F13BB) → **that track is silent**. `id == count` in `GetTrackInfo` (off by one, `ja`) reads 12 bytes past the table — garbage pak index → NULL again | `size/12 >= 1922`; size % 12 ≠ 0 → Warning (tail ignored) |
| AUD-25 | Error | `TrakLkup` record with `streamPakIndex >= StrmPaks count` | `GetTrackInfo` 0x4E0AA3 `jbe` (also off by one: index == count passes and reads the 16 bytes after the table) → wrong/garbage name → `CAEDataStream::Initialise` 0x4DC2B0 `CreateFileA` fails (0x4DC2DA) → silent track | `rec.pak < pakCount` |
| AUD-26 | Error | `audio\streams\<name>` missing for a StrmPaks entry that is referenced by ≥ 1 track (entry 0 → AUD-09) | `CAEDataStream::Initialise` fails → decoder discarded → silent track | only referenced entries; vanilla entry 2 is `""` and unreferenced |
| AUD-27 | Error | track `offset + 0x1F84 + length > size of audio\streams\<pak>` | `SetFilePointer` beyond EOF succeeds, `ReadFile` returns 0 bytes → `FillBuffer` 0x4DC1C0 short → Ogg sync fails → silent track (or truncated playback) | compare; `length == 0` means "rest of file" (0x4DC2F9-0x4DC30C) and is legal |
| AUD-28 | Error | decrypted bytes at `offset + 0x1F84` are not `OggS` | `ov_open_callbacks` (via `CAEVorbisDecoder::Initialise` 0x5024D0) fails → silent track | XOR the 4 bytes at `p = offset+0x1F84` with `key[(p+i) & 15]`, key = EA 3A C4 A1 9A A8 14 F3 48 B0 D7 23 5D E8 96 A2; expect `OggS` (vanilla: all 1922 tracks) |
| AUD-29 | Warning | `StrmPaks` entry without a NUL inside 16 bytes | `lstrlen` runs into the next entry (0x4E0C76) — the buffer is sized from that length so no overflow, but the joined name is opened → not found → AUD-26 | NUL within 16 bytes |

### 2.5 `data\surfaud.dat`

| id | sev | condition | engine behaviour | how to check |
|---|---|---|---|---|
| AUD-30 | Crash | `data\surfaud.dat` missing | `OpenFile` result unchecked, `CFileLoader::LoadLine(0)` → `fgets(NULL)` (same as textdata DAT-03) | exists |
| AUD-31 | Warning | data line whose first token is not one of the 179 surface names (case-sensitive) | `GetSurfaceIdFromName` 0x55D220 returns **0** for unknown names (0x55E5AB-0x55E5B1) → the flags are applied to `DEFAULT`, the intended surface keeps 0 | exact match against the name list (`DEFAULT, TARMAC, TARMAC_FUCKED, …, RAILTRACK`; ids 0..178 — same order as surface.dat / the string refs of 0x55D220) |
| AUD-32 | Warning | data line with fewer than 10 tokens, or a non-integer among the 9 flags | `sscanf` leaves the missing `int`s **uninitialised on the stack** → random bits 10..18 for that surface | `tokens == 10`, tokens 2..10 integers (0/1) |
| AUD-33 | Crash | first token longer than 63 characters | `%s` into `local_40[64]` → stack overwrite | token length ≤ 63 |
| AUD-34 | Info | a surface listed twice / a surface missing | last line wins; missing surface keeps all-zero audio flags | count per surface == 1 |

### 2.6 IDE-side references (audio consequences of the map data)

| id | sev | condition | engine behaviour | how to check |
|---|---|---|---|---|
| AUD-35 | Error | `vehicles.ide` (`cars`) model id > 630 | `CAEVehicleAudioEntity::Initialise` 0x4F7670 copies 36 bytes from `0x860AF0 + (id-400)*36` — for id ≥ 631 that is the data after the 231-entry table (a byte table of 0/1 from 0x862B6C): sound type / bank ids / radio type are garbage (bank 0/1/257 etc.) → wrong or no engine sound, wrong radio behaviour; banks stay < 710 so no AUD-12 crash from this alone | model id of every `cars` line in 400..630; ids 612..630 → Warning "vehicle audio type 10 (silent engine, no horn/door sounds)" (dummy entries: type 10, banks −1) |
| AUD-36 | Info | `vehicles.ide` id in 400..611 whose line replaces a vanilla vehicle of a different class | the audio settings (engine bank, horn, door, radio type, siren) are those of the **vanilla** model id — a mod bike on a car id gets car sounds | informational: print the vanilla entry (type / banks) for the id |
| AUD-37 | Info | `peds.ide` voice names | `CFileLoader::LoadPedObject` resolves `VOICE_*` names against the exe's string tables; the bank ids come from the tables in §1.9 and are all < 710, so peds.ide cannot reference a bank outside a ≥ 710-record BankLkup. Unknown voice names are a PED-rule matter (voice −1 → no speech) | nothing audio-specific beyond AUD-12 |

---------------------------------------------------------------------------------------------------

## 3. Notes for the checker implementation

* All CONFIG files are read with `CFileMgr::OpenFile(name, "rb")` relative to the game root
  (`AUDIO\CONFIG\…`, `AUDIO\SFX\…`, `AUDIO\STREAMS\…`), except StrmPaks/TrakLkup and the stream paks
  (`CreateFileA`, same relative path). modloader redirects apply as for any other file.
* Counts are `int16` (`movsx`): files ≥ 32 768 records would go negative — irrelevant in practice, Info.
* Bank offsets in BankLkup are **byte** offsets (not sectors) — vanilla values are not 2048-aligned
  (bank 1 at 95 802). The engine re-aligns (§1.4).
* `Service` processes the 50-entry request ring on streaming channel 4 (`m_nStreamingChannel` +0x66A = 4,
  set in the ctor 0x4DFB10); a request that never completes is overwritten when the ring wraps — the
  failure modes in AUD-13/14/16 are therefore "silent", not "hang".
* To reproduce the vanilla calibration: BankSlot 45 slots / Σ 8 578 144; BankLkup 710; PakFiles 9;
  StrmPaks 17; TrakLkup 1922 (all `OggS`); EventVol 45 402; surfaud 179 lines; vehicles.ide ids 400..611.

## Appendix A — vanilla `numSounds` per bank (row label = first bank id, 40 values per row), for AUD-19

```
  0: 9 5 5 5 4 5 5 2 3 4 2 3 4 2 2 3 2 3 2 5 2 2 1 1 2 2 3 24 3 3 3 3 2 3 2 3 3 2 3 72
 40: 2 3 2 3 5 2 3 2 3 2 3 3 5 2 2 2 3 3 3 31 9 2 3 2 3 2 2 2 2 5 2 3 2 3 14 2 3 2 3 4
 80: 2 3 8 2 3 4 2 3 2 3 2 3 2 3 2 3 2 3 2 3 2 3 2 3 4 16 1 1 1 2 1 3 1 1 2 3 2 3 2 3
120: 2 6 2 3 2 3 3 3 5 2 3 6 10 6 2 2 2 3 45 2 3 2 3 89 169 131 101 164 15 5 9 14 58 2 2 1 1 2 4 1
160: 5 8 15 7 65 3 57 4 6 3 4 3 67 40 22 13 103 18 79 13 139 41 32 2 52 77 115 16 15 10 64 33 11 1 56 56 56 56 2 12
200: 6 33 39 17 20 47 4 42 1 11 1 1 29 59 9 4 151 19 1 105 15 4 7 1 56 23 8 26 17 35 17 8 136 20 4 5 1 73 49 25
240: 1 5 3 8 57 67 9 40 18 7 1 40 1 10 1 1 36 64 20 34 1 30 48 120 24 5 2 10 24 13 34 12 19 6 23 22 34 1 5 28
280: 23 12 156 18 14 66 26 83 22 17 1 4 2 6 1 6 11 1 2 3 1 48 89 105 4 77 90 68 74 16 7 32 76 41 89 134 84 1 6 2
320: 5 61 36 46 95 82 74 61 39 72 45 55 79 24 14 68 16 1 39 9 1 21 43 73 33 5 1 12 9 25 2 4 2 7 8 65 106 108 48 45
360: 32 21 12 56 1 37 38 41 62 62 68 54 37 37 51 53 42 42 110 78 126 95 95 134 126 94 121 134 93 97 81 93 89 86 62 86 83 27 108 113
400: 111 113 72 111 74 121 140 51 45 30 22 380 41 46 60 99 349 373 299 383 308 108 34 35 75 46 38 25 42 76 75 243 225 111 214 0 238 229 239 142
440: 142 117 82 198 100 209 143 195 125 99 37 246 15 42 227 161 164 45 20 20 228 217 167 132 111 183 151 164 177 103 178 218 187 142 148 129 202 187 213 205
480: 179 206 170 177 143 128 190 142 181 134 190 233 215 248 222 133 15 239 161 236 233 223 155 135 0 17 235 0 111 190 194 231 0 0 248 109 236 249 222 183
520: 182 138 238 0 74 189 181 166 126 238 207 182 141 211 255 263 250 223 133 237 180 144 184 148 208 164 131 155 190 189 228 137 206 235 250 0 80 135 243 159
560: 105 135 158 170 242 215 142 212 234 180 14 85 238 165 205 14 225 159 197 234 0 21 175 144 135 0 81 183 0 239 169 126 244 0 15 88 135 211 241 168
600: 206 28 39 7 15 188 27 230 175 105 42 89 37 104 209 107 98 56 233 254 210 139 40 88 72 84 33 67 45 29 254 70 91 76 241 213 212 49 294 341
640: 268 289 339 83 188 384 350 335 355 39 350 377 395 374 398 42 388 334 392 323 336 43 27 30 101 372 357 299 200 284 98 142 151 133 146 127 71 53 77 349
680: 387 373 353 389 142 136 146 136 136 91 128 124 157 145 293 267 285 253 361 331 400 400 8 8 85 76 201 187 203 196
```
