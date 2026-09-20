// gtacheck — offline checker for a GTA III / VC / SA game folder.
//
// Walks default.dat / gta.dat (+ modloader), every IMG directory, every IDE
// and IPL (text + bnry), then runs every DFF / TXD / COL / IFP through its
// own bounds-checked parsers and reports what gta_sa.exe would crash on,
// render as garbage, or silently ignore.  The rules mirror the engine
// research in E:\RE\addon_check\*_path.md (check ids DAT-nn / DFF-nn /
// TXD-* / COL-nn / IFP-nn / PED-Cnn) — every message names its rule so the
// report can be traced back to the decompile evidence.
//
// Everything here is independent of librw / the euryopa scene: the only
// shared code is the librw skeleton + ImGui used by the window.
#ifndef GTACHECK_H
#define GTACHECK_H
#define GC_VERSION "1.0"	// program version: window title, top bar, Help, reports, crash log, CLI (also src/gtacheck.rc and the README badges)

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>

namespace gc {

// ---------------------------------------------------------------- report ---

enum Severity {
	SEV_INFO,	// note, nothing wrong
	SEV_WARN,	// suspicious / silently ignored by the engine
	SEV_ERROR,	// visible garbage (wrong render, missing texture, dropped data)
	SEV_FATAL,	// crash, hang, or the asset can never load
	SEV_NUM
};

enum Category {
	CAT_DAT,	// gta.dat / default.dat
	CAT_IMG,	// IMG archives
	CAT_IDE,
	CAT_IPL,
	CAT_DFF,
	CAT_TXD,
	CAT_COL,
	CAT_IFP,
	CAT_PED,	// skinned ped DFF rules
	CAT_WATER,
	CAT_TIMECYC,
	CAT_PLANTS,
	CAT_FXP,
	CAT_PATHS,
	CAT_XREF,	// cross references (IDE↔IPL↔IMG↔TXD↔COL↔IFP)
	CAT_LIMIT,	// engine pool / array capacities
	CAT_VEH,	// vehicle DFF / vehicles.ide (VEH-nn)
	CAT_HANDLING,	// handling.cfg / carcols / carmods / cargrp (HND-nn)
	CAT_WEAPON,	// weapon.dat (WPN-nn)
	CAT_PEDDATA,	// ped.dat, pedstats, pedgrp, popcycle, decision, surfaces, shopping, stats (PDD-nn)
	CAT_CLOTHES,	// player.img / clothes.dat (CLO-nn)
	CAT_CUTSCENE,	// cuts.img / .cut / cutscene.img (CUT-nn)
	CAT_GXT,	// text\*.gxt (GXT-nn)
	CAT_SCM,	// main.scm / script.img (SCM-nn)
	CAT_UI,		// UI TXDs, fonts.dat, stream.ini (UI-nn)
	CAT_AUDIO,	// audio\CONFIG, SFX, streams (AUD-nn)
	CAT_QUALITY,	// build quality / performance, not crashes (QLT-nn)
	CAT_NUM
};

enum FixKind {
	FIX_NONE,
	FIX_DFF_FRAMENAME,	// rewrite NodeName chunks (auto rule or manual names)
	FIX_IDE_DRAWDIST,	// IDE objs/tobj line: draw distance := 2 × model radius (QLT-01)
	FIX_IDE_ALPHA,		// IDE objs/tobj line: set flag 4 (alpha) when a texture has alpha (QLT-11)
	FIX_TXD_POW2,		// TXD: resize non-power-of-two textures to nearest power of two
	FIX_TXD_DXTALPHA,	// TXD: DXT3/DXT5 whose alpha is all 255 → DXT1; real alpha → the alpha flag is set
	FIX_TXD_DEADTEX,	// TXD: drop the textures no material of its models uses (QLT-12)
};
inline bool FixNeedsEntry(int k) { return k == FIX_DFF_FRAMENAME || k == FIX_TXD_POW2 || k == FIX_TXD_DXTALPHA || k == FIX_TXD_DEADTEX; }	// the others edit a text line (Issue::file + line)
inline bool FixIsTxd(int k) { return k == FIX_TXD_POW2 || k == FIX_TXD_DXTALPHA || k == FIX_TXD_DEADTEX; }

struct Issue {
	Severity sev;
	Category cat;
	std::string code;	// rule id, e.g. "DFF-19"
	std::string file;	// logical path or "archive.img/entry.dff"
	int line;		// text line, entry index, or -1
	std::string object;	// model / texture / animation name, or ""
	std::string msg;	// one-line human message (UTF-8, in the current language)
	std::string detail;	// longer explanation: what the engine does, how to fix
	std::string msgRu, detailRu, objectRu;	// the Russian originals (translation source, vanilla-baseline key)
	int id;			// model id if known, else -1
	int fix;		// FixKind the tool can apply, FIX_NONE otherwise
	int entry;		// GameData::entries index the fix applies to, -1
	bool vanilla;		// same code / file name / object / message as in stock SA 1.0 US (embedded baseline vanilla_sa.h)
	int phase;		// which pass produced the row (PH_*): the partial re-check keeps the rows of the passes it skipped
	bool ignored;		// GUI: listed in the game folder's gta_check_ignore.txt (the rule or this exact issue)
	bool isNew;		// GUI: was not in the previous check of this folder
	Issue() : sev(SEV_INFO), cat(CAT_DAT), line(-1), id(-1), fix(FIX_NONE), entry(-1), vanilla(false), phase(0), ignored(false), isNew(false) {}
};

struct Counters {
	int bySev[SEV_NUM];
	int byCat[CAT_NUM];
	int files[CAT_NUM];	// files inspected per category
	int vanillaBySev[SEV_NUM];	// how many of bySev are Issue::vanilla
	int vanillaByCat[CAT_NUM];
};

struct Report {
	std::mutex mtx;
	std::vector<Issue> issues;
	Counters counters;
	std::atomic<unsigned> version;	// bumped on every change so the UI can resync cheaply

	Report() : version(0) { memset(&counters, 0, sizeof(counters)); }
	void add(const Issue &is);
	void countFile(Category c);
	void clear();
};

// ------------------------------------------------------------- progress ---

struct Progress {
	std::mutex mtx;
	std::string phase;	// current phase (UTF-8)
	std::string item;	// current file / model
	int done, total;	// within the phase
	std::atomic<bool> cancel;
	std::atomic<bool> running;
	std::atomic<bool> finished;
	double startMs, endMs;

	Progress() : done(0), total(0), cancel(false), running(false), finished(false), startMs(0), endMs(0) {}
	void set(const char *ph, int d = 0, int t = 0);
	void step(const char *it, int d, int t);
};

// -------------------------------------------------------------- options ---

enum GameVersion { GAME_UNKNOWN, GAME_III, GAME_VC, GAME_SA };

struct Options {
	std::string root;		// game folder
	bool useModloader;		// overlay modloader/ folders
	bool limitAdjuster;		// pool/array limits raised (fastman92) → limit rules become notes
	bool checkDff, checkTxd, checkCol, checkIfp, checkData;
	bool partial = false;	// a partial re-check: the cross-reference / quality passes are skipped (their rows are kept from the previous run)
	bool texXref;			// DFF material textures vs TXD chain (needs the TXD pass)
	bool reportInfo;		// keep SEV_INFO issues
	bool skipVanillaImgs;		// only check IMGs / files that are not the stock gta3.img/gta_int.img/... (mod-only sweep)
	bool hideVanilla;		// hide issues that stock SA already has (the game runs with them) — for custom builds

	Options() : useModloader(true), limitAdjuster(false),
		checkDff(true), checkTxd(true), checkCol(true), checkIfp(true), checkData(true),
		texXref(true), reportInfo(false), skipVanillaImgs(false), hideVanilla(true) {}
};

// ------------------------------------------------------------ utilities ---

// Bounds-checked little-endian reader. Every accessor clears `ok` instead of
// reading past the end, so a garbage file can never take the checker down.
struct Buf {
	const uint8_t *p;
	size_t n, pos;
	bool ok;

	Buf() : p(nullptr), n(0), pos(0), ok(true) {}
	Buf(const uint8_t *data, size_t size) : p(data), n(size), pos(0), ok(true) {}
	bool can(size_t k) const { return pos + k <= n; }
	size_t left() const { return pos <= n ? n - pos : 0; }
	uint8_t  u8();
	uint16_t u16();
	uint32_t u32();
	int16_t  i16() { return (int16_t)u16(); }
	int32_t  i32() { return (int32_t)u32(); }
	float    f32();
	bool skip(size_t k);
	bool seek(size_t at);
	const uint8_t *ptr(size_t k);	// pointer to k bytes at pos (advances), nullptr if short
	std::string str(size_t k);	// fixed field, stops at NUL
};

// RenderWare chunk header.
struct Chunk {
	uint32_t type, size, libid, version, build;
	size_t start;	// body offset
	size_t end;	// body end (start + size), clamped to buffer
	bool truncated;	// size ran past the buffer
};
bool readChunk(Buf &b, Chunk &c);		// reads header at b.pos, leaves b.pos at body start
bool findChunk(Buf &b, uint32_t type, Chunk &c, size_t limit);	// scans forward until `limit`
uint32_t rwUnpackVersion(uint32_t libid);
const char *rwChunkName(uint32_t type);

std::string lower(const std::string &s);
std::string trim(const std::string &s);
std::string basename(const std::string &path);	// "a/b/c.dff" -> "c.dff"
std::string stem(const std::string &path);	// "a/b/c.dff" -> "c"
std::string extOf(const std::string &path);	// "a/b/c.DFF" -> "dff"
std::string joinPath(const std::string &a, const std::string &b);
std::string normSlashes(const std::string &s);	// '\' -> '/'
bool fileExists(const std::string &p);
bool dirExists(const std::string &p);
bool readFile(const std::string &p, std::vector<uint8_t> &out);
FILE *OpenReadUtf8(const std::string &p);	// fopen "rb" with a UTF-8 path
FILE *openWriteUtf8(const std::string &p);	// fopen "wb" with a UTF-8 path
bool readTextLines(const std::string &p, std::vector<std::string> &lines);
// Case-insensitive resolution of a logical path inside the game root.
std::string resolvePath(const std::string &root, const std::string &logical);
double nowMs();
std::string fmt(const char *f, ...);
int ieq(const std::string &a, const std::string &b);	// case-insensitive equal

// ------------------------------------------------------------ game data ---

enum ObjType { OT_OBJS, OT_TOBJ, OT_ANIM, OT_HIER, OT_WEAP, OT_CARS, OT_PEDS, OT_NUM };
const char *objTypeName(int t);

struct ObjDef {
	int id;
	int type;		// ObjType
	std::string name;	// model name as in the IDE
	std::string txd;
	std::string anim;	// anim / weap / peds anim file, cars anims
	float drawDist[3];
	int numAtomics;
	int flags;
	int timeOn, timeOff;
	std::string file;	// IDE logical path
	int line;
	// resolved later
	int dffEntry;		// index into GameData::entries, -1 = no DFF anywhere
	int txdSlot;		// index into txdSlots
	int colEntry;		// COL entry index that matched this model, -1
	int numInstances;	// placements in all IPLs
	int lodChildren;	// how many instances point at an instance of this model as LOD
	bool isLod;		// referenced as a LOD by some instance
	// filled by the DFF pass
	bool dffParsed, dffFailed, dffSkinned;
	float dffRadius;	// bounding sphere radius of the render geometry (-1 = unknown)
	float dffCenter[3];
	bool dffPrelit, dffNight;		// any geometry with vertex colours / with the night-colours plugin (DFF pass)
	std::vector<std::string> dffTex;	// lower-case texture names the materials use (DFF pass)
	// peds / cars extras
	std::string pedAnimGroup, pedType, pedStats;
	std::string carType, carClass, carHandling;
	std::vector<std::string> tok;		// all tokens of the IDE line (cars / peds) for the data-file rules

	ObjDef() : id(-1), type(OT_OBJS), numAtomics(1), flags(0), timeOn(0), timeOff(0), line(0),
		dffEntry(-1), txdSlot(-1), colEntry(-1), numInstances(0), lodChildren(0), isLod(false),
		dffParsed(false), dffFailed(false), dffSkinned(false), dffRadius(-1), dffPrelit(false), dffNight(false)
	{ drawDist[0] = drawDist[1] = drawDist[2] = 0; dffCenter[0] = dffCenter[1] = dffCenter[2] = 0; }
	float maxDrawDist() const {
		float m = drawDist[0];
		for(int i = 1; i < numAtomics && i < 3; i++) if(drawDist[i] > m) m = drawDist[i];
		return m;
	}
};

struct TxdSlot {
	std::string name;	// lower-case
	std::string shown;	// as first written
	int entry;		// IMG entry / loose file, -1 = nowhere
	int parent;		// slot index, -1
	int users;		// models referencing it
	bool fromImg;		// created by an IMG .txd entry
	bool fromIde;		// referenced by an IDE line
	TxdSlot() : entry(-1), parent(-1), users(0), fromImg(false), fromIde(false) {}
};

enum EntryKind { EK_OTHER, EK_DFF, EK_TXD, EK_COL, EK_IPL, EK_DAT, EK_IFP, EK_RRR, EK_SCM };

struct Entry {
	std::string name;	// full entry name as stored ("foo.dff"), max 23 chars
	std::string base;	// lower-case stem ("foo")
	int kind;
	int img;		// archive index, -1 = loose file (modloader)
	uint32_t offset;	// sectors
	uint32_t sizeSectors;	// streaming size in sectors
	uint32_t sizeArchive;	// sizeInArchive field (SA), 0 if unused
	std::string loose;	// physical path when overridden / loose
	int dirIndex;		// position in the archive directory
	bool duplicate;		// same name appeared earlier (this one wins)
	Entry() : kind(EK_OTHER), img(-1), offset(0), sizeSectors(0), sizeArchive(0), dirIndex(-1), duplicate(false) {}
	std::string where() const;	// "gta3.img/foo.dff" or loose path
};

struct Archive {
	std::string logical;	// as in gta.dat
	std::string phys;	// resolved path
	bool ver2;
	bool vanilla;		// one of the stock archives
	int numEntries;
	std::string mod;	// modloader mod name if the archive was added by a mod
	Archive() : ver2(false), vanilla(false), numEntries(0) {}
};

struct Inst {
	int id;
	std::string name;	// name column (text IPL only; SA ignores it)
	int interior;
	float pos[3];
	float rot[4];
	float scale[3];		// III/VC only
	int lod;		// index into the same text IPL (or the same-basename text IPL for streamed)
	int line;		// text line, or record index for bnry
	int fileIdx;		// IplFile index
	int ordinal;		// index within the file's inst list (what LOD numbers refer to)
	Inst() : id(-1), interior(0), lod(-1), line(0), fileIdx(-1), ordinal(-1) {
		pos[0]=pos[1]=pos[2]=0; rot[0]=rot[1]=rot[2]=0; rot[3]=1; scale[0]=scale[1]=scale[2]=1; }
};

struct IplFile {
	std::string logical;	// "data/maps/x/y.ipl" or "gta3.img/y_stream0.ipl"
	std::string phys;
	bool binary;
	bool streamed;		// came from an IMG (or a loose bnry override)
	int textParent;		// for streamed: index of the text IPL with the same basename, -1
	int firstInst, numInst;	// range in GameData::insts
	int numCull, numZone, numGrge, numEnex, numPick, numCars, numJump, numTcyc, numAuzo, numOccl;
	int entry;		// IMG entry, -1
	IplFile() : binary(false), streamed(false), textParent(-1), firstInst(0), numInst(0),
		numCull(0), numZone(0), numGrge(0), numEnex(0), numPick(0), numCars(0), numJump(0),
		numTcyc(0), numAuzo(0), numOccl(0), entry(-1) {}
};

struct ColEntry {		// one model inside a .col archive
	std::string name;	// as stored (≤ 21 chars + NUL expected)
	int version;		// 1..4
	int entryIdx;		// which .col file (GameData::entries index); -2 = COLFILE line of gta.dat (III/VC, see `colfile`)
	std::string colfile;	// logical path of the COLFILE .col (entryIdx == -2)
	int index;		// position inside the file
	int modelId;		// resolved IDE id, -1
	bool hasVolumes;	// flag 2 / non-empty
	int numFaces, numVerts, numSpheres, numBoxes;
	float bmin[3], bmax[3], center[3], radius;
	ColEntry() : version(0), entryIdx(-1), index(-1), modelId(-1), hasVolumes(false),
		numFaces(0), numVerts(0), numSpheres(0), numBoxes(0), radius(0) {
		bmin[0]=bmin[1]=bmin[2]=bmax[0]=bmax[1]=bmax[2]=center[0]=center[1]=center[2]=0; }
};

struct TxdInfo {		// texture names of one TXD (for DFF material cross-reference)
	std::vector<std::string> names;	// lower-case
	bool parsed;
	bool failed;		// engine would LOAD-FAIL it
	uint32_t bytes;		// raster bytes of all textures with their mip chains (what the TXD takes in memory)
	int big;		// textures with a side ≥ 1024
	struct Tex { std::string name; int w, h; uint32_t bytes; bool alpha; Tex() : w(0), h(0), bytes(0), alpha(false) {} };
	std::vector<Tex> texs;	// one per TEXTURENATIVE, in file order (name lower-case)
	TxdInfo() : parsed(false), failed(false), bytes(0), big(0) {}
};

struct ZoneRef {		// IPL zone line: the name shown on screen is a GXT key
	std::string name, gxt, file;
	int line;
	ZoneRef() : line(0) {}
};

struct WaterQuad {		// axis-aligned water.dat quad (for «object under water»)
	float x0, y0, x1, y1, z;
};

// Collision geometry of one .col entry for the wireframe overlay (spheres / boxes / mesh).
struct ColShape {
	std::vector<float> spheres;	// x y z r per sphere
	std::vector<float> boxes;	// min xyz, max xyz per box
	std::vector<float> verts;	// xyz
	std::vector<uint16_t> faces;	// 3 indices per triangle
	float bmin[3], bmax[3];
	ColShape() { bmin[0]=bmin[1]=bmin[2]=bmax[0]=bmax[1]=bmax[2]=0; }
};
struct GameData;
bool LoadColShape(const GameData &gd, int colEntry, ColShape &out, std::string &err);
// the same from the bytes of a .col file: entry number `index` (bounds from its header); name / version filled
bool LoadColShapeData(const std::vector<uint8_t> &data, int index, ColShape &out, std::string &name, int &version, std::string &err);
struct ColListEntry { std::string name; int version; uint32_t offset, size; };
void ListColData(const std::vector<uint8_t> &data, std::vector<ColListEntry> &out);	// the entries of a .col file

// «оптимизация по районам» (district.cpp): the map on a div×div grid; every cell gets its own IPL / IDE / COL / LOD TXD
struct DistrictOptions { int div, minObjs, maxObjs; bool ipl, ide, col, lodTxd, lodOne; DistrictOptions() : div(8), minObjs(0), maxObjs(0), ipl(true), ide(true), col(true), lodTxd(true), lodOne(false) {} };	// minObjs: a cell with fewer movable placements joins a neighbour; maxObjs: warn above
struct DistrictCell { int gx, gy; std::vector<std::pair<int, int> > parts; std::vector<int> insts; std::vector<int> models; int objs, lodModels, all; double mb; float zMin, zMax; };	// gx,gy: the primary square (the name); parts: every grid square of the district after merging; all: every outdoor placement (the map shows it); insts: the movable ones	// insts: GameData::insts moved here; models: objs indices that live only here
struct DistrictPlan { int div; float cell, half; std::vector<DistrictCell> cells; std::vector<int> cellOfInst; std::vector<std::string> skippedIpl; int total, sharedModels, iplSlots, merged, overMax; std::string note;
	DistrictPlan() : div(0), cell(0), half(3000), total(0), sharedModels(0), iplSlots(0), merged(0), overMax(0) {} };
void DistrictPlanBuild(const GameData &gd, const DistrictOptions &o, DistrictPlan &p);
bool DistrictApply(GameData &gd, const DistrictOptions &o, const DistrictPlan &p, std::string &log);

struct UnusedFile { int entry; std::string why; };	// archive entry nothing refers to (check_quality.cpp)
void CollectUnused(const GameData &gd, std::vector<UnusedFile> &out);
// QLT-12 for one dictionary: the textures no material of its models uses (false when usage is unknown)
bool DeadTextures(const GameData &gd, const std::string &txdLower, std::vector<std::string> &out);
// Streaming load per map cell (n×n over ±half): MB of unique DFF+TXD of every instance whose draw
// distance reaches the cell, and how many instances that is.
void StreamHeat(const GameData &gd, float half, int n, std::vector<float> &mb, std::vector<int> &count);

struct AnimGroup {		// animgrp.dat
	std::string name, block, model;
	std::vector<std::string> anims;
	int line;
	AnimGroup() : line(0) {}
};

struct ModFile {		// modloader loose file
	std::string phys;
	std::string rel;	// path relative to the mod folder, lower-case, '/'
	std::string mod;
	int priority;
	ModFile() : priority(0) {}
};

struct GameData {
	GameVersion game;
	std::string root;
	Options opt;

	std::vector<Archive> archives;
	std::vector<Entry> entries;
	std::unordered_map<std::string, int> entryByName;	// "foo.dff" lower → winning entry index

	std::vector<ObjDef> objs;				// in IDE order
	std::unordered_map<int, int> objById;			// id → objs index (last definition wins, like the engine)
	std::unordered_map<std::string, int> objByName;		// lower name → objs index (first definition)
	int numDefs[OT_NUM];

	std::vector<TxdSlot> txdSlots;
	std::unordered_map<std::string, int> txdByName;

	std::vector<IplFile> ipls;
	std::vector<Inst> insts;

	std::vector<ColEntry> colEntries;
	int numColFiles;

	std::vector<std::string> ifpEntries;			// entry names (foo.ifp)
	std::vector<AnimGroup> animGroups;
	std::set<std::string> fxSystems;			// effects.fxp blueprint names (lower)
	std::set<std::string> particleTextures;		// particle.txd texture names (lower)
	std::unordered_map<std::string, TxdInfo> txdTextures;	// txd stem lower → names
	std::set<std::string> objectDatNames;			// object.dat model names
	std::set<uint32_t> gxtMainHashes;			// keys of the MAIN table of the selected GXT (CRC32 upper), empty = not read
	std::vector<ZoneRef> zones;				// IPL zone lines
	std::vector<WaterQuad> water;				// water.dat rectangles
	std::map<std::string, std::vector<std::string>> ifpAnims;	// IFP block (lower stem) → animation names (lower), filled by the IFP pass

	// data files actually used (for the summary)
	std::vector<std::string> datFiles;	// gta.dat / default.dat (resolved)
	struct LevelZone { float x0, y0, x1, y1; int level; };
	std::vector<LevelZone> mapZones;	// III/VC: map.zon (zone type 3) in file order — CTheZones::GetLevelFromPosition (index 0 = whole map)
	std::unordered_map<std::string, int> lodBySuffix;	// III/VC: model name without its first 3 chars → id of the LOD model paired by FindRelatedModel
	std::set<std::string> modelFileClumps;	// III/VC: clump root-frame names from MODELFILE/HIERFILE (models/generic/*.dff), lower-case
	std::vector<std::string> ideFiles;
	// modloader
	bool modloaderActive;
	std::vector<ModFile> modFiles;
	std::unordered_map<std::string, int> looseByName;	// "foo.dff" lower → modFiles index (winner)
	std::unordered_map<std::string, int> redirectByRel;	// "data/maps/x.ipl" lower → modFiles index

	// section counters summed over all text IPLs (capacity rules)
	int totalNaviZones, totalMapZones, totalCull, totalTunnel, totalMirror, totalOccl, totalOcclInt,
	    totalGrge, totalEnex, totalCarGen, totalTcyc, totalAuzoBox, totalAuzoSphere, totalPickups, totalJumps;
	int totalInfoZones, total2dfx;	// III/VC: zone type 2 (VC InfoZoneArray) и строки 2dfx в IDE (C2dEffect store)

	GameData() : game(GAME_UNKNOWN), numColFiles(0), modloaderActive(false),
		totalNaviZones(0), totalMapZones(0), totalCull(0), totalTunnel(0), totalMirror(0),
		totalOccl(0), totalOcclInt(0), totalGrge(0), totalEnex(0), totalCarGen(0), totalTcyc(0),
		totalAuzoBox(0), totalAuzoSphere(0), totalPickups(0), totalJumps(0), totalInfoZones(0), total2dfx(0)
	{ memset(numDefs, 0, sizeof(numDefs)); }

	bool isSA() const { return game == GAME_SA; }
	bool isIII() const { return game == GAME_III; }
	bool isVC() const { return game == GAME_VC; }
	bool d3d8() const { return game == GAME_III || game == GAME_VC; }	// gta3.exe / gta-vc.exe: RW on Direct3D 8
	// RW stream versions the exe accepts (rwLIBRARYBASEVERSION..rwLIBRARYCURRENTVERSION, read from the exes):
	// gta3.exe 3.3.0.2, gta-vc.exe 3.4.0.3 (both base 0x31000), gta_sa.exe 3.6.0.3 (base 0x34000)
	uint32_t rwMin() const { return game == GAME_SA ? 0x34000 : 0x31000; }
	uint32_t rwMax() const { return game == GAME_III ? 0x33002 : game == GAME_VC ? 0x34003 : 0x36003; }
	bool rwVersionOk(uint32_t v) const { return v >= rwMin() && v <= rwMax(); }
	// CModelInfo::ms_modelInfoPtrs: MODELINFOSIZE (re3 5500, reVC 6500, SA 20000) — Add*Model пишет по id без проверки
	int modelInfoSize() const { return game == GAME_III ? 5500 : game == GAME_VC ? 6500 : 20000; }
	const char *rwRange() const { return game == GAME_III ? "3.1.0.0â""3.3.0.2" : game == GAME_VC ? "3.1.0.0â""3.4.0.3" : "3.4.0.0â""3.6.0.3"; }
	int findEntry(const std::string &nameWithExt) const;	// lower-case lookup
	bool isWinner(int entryIdx) const;			// the entry the engine actually uses for that name
	const ObjDef *findObj(int id) const;
	const ObjDef *findObjByName(const std::string &name) const;
	int txdSlotOf(const std::string &name);			// creates
	int findTxdSlot(const std::string &name) const;
	// Reads an entry (IMG sectors or loose file) into `out`. Returns false on I/O error.
	bool readEntry(int entryIdx, std::vector<uint8_t> &out, std::string *err = nullptr) const;
	// Texture lookup through the TXD parent chain (lower-case names).
	bool txdChainHasTexture(int slot, const std::string &texLower, int depth = 0) const;
};

// ------------------------------------------------------------- checker ---

enum { PH_LOAD, PH_TXD, PH_DATA, PH_COL, PH_IFP, PH_DFF, PH_XREF, PH_QUALITY };	// the passes of RunCheck, in order
struct Context {
	GameData *gd;
	Report *rep;
	Progress *prog;
	Options opt;
	int phase = PH_LOAD;	// the pass running now (RunCheck sets it; every row is stamped with it)

	void add(Severity sev, Category cat, const char *code, const std::string &file, int line,
	         const std::string &object, const std::string &msg, const std::string &detail = "", int id = -1,
	         int fix = FIX_NONE, int entry = -1);
	bool cancelled() const { return prog->cancel.load(); }
};

// ----------------------------------------------------------------- fixes ---

struct FrameName { int index; std::string name; int chunkLen; bool isAtomic; };	// chunkLen -1 = no NodeName plugin; isAtomic = an ATOMIC uses this frame
// What the automatic rule knows about the model the DFF belongs to.
struct FixHint {
	std::string modelName;	// IDE model name (exact case), "" = unknown
	bool atomicModel;	// objs / tobj: atomic frames may be renamed after the model; clumps keep their frame names
	FixHint() : atomicModel(false) {}
};
FixHint HintForEntry(const GameData &gd, int entryIdx);
bool ListDffFrames(const std::vector<uint8_t> &data, std::vector<FrameName> &out, std::string &err);
bool RewriteDffFrameNames(std::vector<uint8_t> &data, const std::vector<std::string> &names, int &changed, std::string &err);
std::string TrimFrameName(const std::string &name);	// strip padding, cut to 23
std::string AutoFrameName(const FrameName &frame, const FixHint &hint);	// model name for atomic frames of objs/tobj, TrimFrameName otherwise
bool FixDffFrameNames(std::vector<uint8_t> &data, const FixHint &hint, int &fixed, std::string &err);
bool ReadEntryTrimmed(const GameData &gd, int entryIdx, std::vector<uint8_t> &data, std::string &err);
// Undo journal (<root>\gta_check_backup\undo.txt + undo\ copies): every write of WriteEntryFixed / SaveTextFile
// records what it replaced; UndoLastFix puts the previous bytes back (or deletes a file that was created).
// Text IPL: the LOD field (last of an inst line) of `line` (1-based) := newLod (-1 = none); backup + undo journal
bool SetIplLod(GameData &gd, const std::string &logical, int line, int newLod, std::string &msg);
// Every inst line of `modelId` in a text IPL: link=true → lod := the nearest placement of `lodId` (a LOD line is
// appended at the model's position when none is within 300 units); link=false → lod := -1. Backup + journal.
bool LinkIplLods(GameData &gd, const std::string &logical, int modelId, int lodId, bool link, std::string &msg);
// Text IPL: every model line is followed by its LOD line (the pair stays inside its inst section); all lod
// indices of the file are renumbered accordingly. Backup + journal.
bool SortIplLods(GameData &gd, const std::string &logical, std::string &msg);
// IDE: the txd field (3rd) of the given models' lines := new name (grouped by file, one write per file). Backup + journal.
int SetIdeTxdNames(GameData &gd, const std::vector<std::pair<int, std::string> > &modelTxd, std::string &summary);
int UndoCount(const std::string &root);
std::string UndoLastLabel(const std::string &root);
bool UndoLastFix(GameData &gd, std::string &msg);
// Writes an edited text file back; the first save keeps the original under <root>/gta_check_backup/text/.
// ---------------------------------------------------------------- TXD editing (txd_edit.cpp) ---
struct TxdTex {
	std::string name, mask;
	uint32_t platform, filterAddr, rasterFormat, d3dFormat, version;	// d3dFormat: FOURCC or D3DFORMAT (derived for D3D8)
	bool hasAlpha, compressed; int compression;			// compression: the D3D8 byte (1 = DXT1 … 5 = DXT5)
	int width, height, depth, mips, type; uint8_t flags;		// flags: the D3D9 byte (1 alpha, 2 cube, 4 automip, 8 compressed)
	std::vector<uint8_t> palette;					// RGBA × 32 / 256 when PAL4 / PAL8
	std::vector<std::vector<uint8_t> > levels;
	std::vector<uint8_t> extension;					// the texture's EXTENSION chunk, verbatim
	bool ok; std::string err;
	TxdTex() : platform(9), filterAddr(0x1106), rasterFormat(0), d3dFormat(0), version(0), hasAlpha(false), compressed(false), compression(0),
	           width(0), height(0), depth(0), mips(1), type(4), flags(0), ok(true) {}
};
struct TxdFile {
	uint32_t version, libid; uint16_t deviceId;
	std::vector<TxdTex> tex;
	std::vector<uint8_t> extension;
	bool ok; std::string err;
	TxdFile() : version(0), libid(0x1803FFFF), deviceId(0), ok(false) {}
};
struct TxdFixOptions {
	bool toD3D9, unpalettize, stripAutoMip, fullMips, pow2, fixFilter, clearMask, dxt;
	bool fixDxtAlpha;	// DXT2/3/4/5 whose decoded alpha is all-255 → re-encode as DXT1 (half the block bytes)
	int maxSide;		// > 0: halve textures until both sides fit (the «диета»)
	TxdFixOptions() : toD3D9(true), unpalettize(true), stripAutoMip(true), fullMips(true), pow2(true), fixFilter(true), clearMask(false), dxt(true), fixDxtAlpha(true), maxSide(0) {}
};
bool TxdParse(const std::vector<uint8_t> &data, TxdFile &out);
bool TxdDecode(const TxdTex &t, std::vector<uint8_t> &rgba);		// level 0 → RGBA8 (width × height × 4)
const char *TxdFormatName(const TxdTex &t);
bool TxdNeedsFix(const TxdTex &t, const TxdFixOptions &o);
int TxdFillEmptyMips(TxdFile &f);		// zero-size tail levels ← first block/pixel of the level above; levels filled
int TxdApplyFixes(TxdFile &f, const TxdFixOptions &o, std::vector<std::string> &log);	// textures changed
bool TxdWrite(const TxdFile &f, std::vector<uint8_t> &out, std::string &err);
// A new texture from RGBA (level 0): power-of-two size (resampled when needed), full mip chain, DXT1 / DXT5 by alpha
// (or 8888 when !dxt), D3D9 layout, LINEARMIPLINEAR + WRAP
bool TxdFromRgba(const std::string &name, const std::vector<uint8_t> &rgba, int w, int h, bool dxt, TxdTex &out);
// one texture re-encoded: w/h 0 = keep; fmt 0 = keep compression, 1..5 = DXT1..DXT5 (DXT1 drops the alpha), 6 = 32-bit
bool TxdRebuildTexture(TxdTex &t, int w, int h, int fmt, std::string &err);
bool TxdRebuildTexture(TxdTex &t, int w, int h, int fmt, int mips, std::string &err);	// mips: -1 keep, 0 one level, 1 full chain
bool TxdHasAlphaFormat(const TxdTex &t);	// alpha by the pixel format (DXT2..5, 8888, 1555, 4444) or the alpha flag
bool WriteBytes(const std::string &path, const std::vector<uint8_t> &data);	// plain write (UTF-8 path), no backup, no journal
bool EnsureDir(const std::string &p);
// Output folder for every write (TXD resave, DFF frame fixes, text editor): "" = in place with a
// backup under gta_check_backup; otherwise nothing in the game is touched and files land under this
// folder (IMG entries flat by name, loose files by their relative path — a modloader-ready tree).
void SetOutputDir(const std::string &dir);
const std::string &OutputDir();
std::string OutputPathFor(const std::string &logicalOrName);	// <outdir>\<relative>, directories created

// Plain-language help for a rule code (rule_help.cpp): what it is, what it does to the game, what to do.
bool RuleHelp(const std::string &code, std::string &what, std::string &risk, std::string &fix);
int RuleHelpCount();
bool RuleHelpAt(int i, std::string &code, std::string &what, std::string &risk, std::string &fix);
bool SaveTextFile(const GameData &gd, const std::string &logical, const std::string &phys, const std::vector<uint8_t> &orig, const std::vector<uint8_t> &data, std::string &backup, std::string &err);
// anim/cuts.img directory (own VER2 format, not in gta.dat) — used by the cutscene checks and the file viewer.
struct CutEntry { std::string name; std::string base; std::string ext; uint32_t offset, size; int index; bool nameNoNul; };
bool ReadCutsDir(const std::string &phys, std::vector<CutEntry> &out, bool &ver2, int &count);
bool ReadCutsEntry(const std::string &phys, const CutEntry &e, std::vector<uint8_t> &out);
bool WriteEntryFixed(GameData &gd, int entryIdx, const std::vector<uint8_t> &orig, const std::vector<uint8_t> &data, std::string &backup, std::string &err);
int ApplyFixes(GameData &gd, Report &rep, const std::vector<int> &issueIdx, std::string &summary);

// Top-level run (worker thread). Fills gd from opt.root and appends to rep.
void RunCheck(Context &ctx);

// Phases (runner.cpp orchestrates these).
void LoadGameData(Context &ctx);		// gamedata.cpp: dat/img/ide/ipl + DAT/IDE/IPL/IMG checks
void CheckTxds(Context &ctx);		// check_txd.cpp
void CheckDffs(Context &ctx);		// check_dff.cpp
void CheckCols(Context &ctx);		// check_col.cpp
void CheckIfps(Context &ctx);		// check_ifp.cpp
void CheckDataFiles(Context &ctx);	// check_misc.cpp: water/timecyc/plants/fxp/object.dat/animgrp/nodes
void CheckCrossRefs(Context &ctx);	// runner.cpp: everything that needs all passes

// Single-file entry points (also used by the UI drag & drop / file mode).
void CheckOneDff(Context &ctx, const std::string &where, const std::vector<uint8_t> &data,
                 const ObjDef *def, int txdSlot);
void CheckOneTxd(Context &ctx, const std::string &where, const std::vector<uint8_t> &data,
                 const std::string &stemLower, TxdInfo *outInfo, int entry = -1);	// entry: for the auto-fix rows
void CheckOneCol(Context &ctx, const std::string &where, const std::vector<uint8_t> &data,
                 int entryIdx, bool bootPass);
void CheckOneIfp(Context &ctx, const std::string &where, const std::vector<uint8_t> &data,
                 const std::string &fileStem, int *outNumAnims, std::vector<std::string> *outAnimNames,
                 bool cutscene = false);	// cutscene: cuts.img IFP (never looped, kept uncompressed) — time/quantisation rules relax

// check_veh.cpp: vehicle (vehicles.ide `cars`) rules VEH-nn, fed by the DFF parser.
struct VehDff {
	std::vector<std::string> frameNames;	// NodeName of every frame, file order ("" = none)
	std::vector<int> frameParent;		// parent frame index per frame (-1 = root)
	std::vector<bool> frameHasAtomic;	// an ATOMIC references this frame
	bool hasCollision;			// CLUMP extension carries a Collision Plugin (0x253F2FA)
	uint32_t colFourcc;			// first 4 bytes of the plugin body (little-endian), 0 if shorter
	uint32_t colSizeField;			// the entry's size field (bytes 4..7), 0 if shorter
	size_t colSize;				// plugin body size
	VehDff() : hasCollision(false), colFourcc(0), colSizeField(0), colSize(0) {}
};
void CheckVehicleDff(Context &ctx, const std::string &where, const ObjDef &def, const VehDff &v);

// ------------------------------------------------------ data-file access ---

// One line of a game text file after CFileLoader::LoadLine normalisation (bytes < 0x20 and ',' → ' ',
// leading blanks trimmed). `raw` keeps the original line for rules that look at exact bytes.
struct DataLine {
	std::string raw;
	std::string norm;
	std::vector<std::string> tok;	// norm split on spaces
	int number;			// 1-based
	bool tooLong;			// original line > 511 bytes (LoadLine buffer)
};
// Physical path of a logical game path honouring modloader redirects (data files by relative path / basename).
std::string DataFilePath(const GameData &gd, const std::string &logical);
// Loads a text file into DataLines. Empty lines are kept (rules need them). Returns false when unreadable.
bool ReadDataLines(const GameData &gd, const std::string &logical, std::vector<DataLine> &out, std::string *physOut = nullptr);
void SplitTokens(const std::string &s, std::vector<std::string> &out);	// split on ' ' and '\t'
bool IsIntToken(const std::string &t);
bool IsNumToken(const std::string &t);
bool IsHexToken(const std::string &t);

// Round-3 rule modules (runner.cpp calls them after the IDE/IMG/TXD/DFF/IFP passes).
void CheckQuality(Context &ctx);	// check_quality.cpp: QLT-nn — build quality / performance (after every other pass)
void CheckHandling(Context &ctx);	// check_handling.cpp: HND-nn + vehicles.ide VEH-13..21
void CheckWeapons(Context &ctx);	// check_weapon.cpp: WPN-nn
void CheckPedData(Context &ctx);	// check_ped.cpp: PDD-nn
void CheckClothes(Context &ctx);	// check_clothes.cpp: CLO-nn
void CheckCutscenes(Context &ctx);	// check_cuts.cpp: CUT-nn
void CheckTextScriptImg(Context &ctx);	// check_text.cpp: GXT-nn, SCM-nn, IMG-nn, UI-nn
void CheckAudio(Context &ctx);		// check_audio.cpp: AUD-nn

// ---------------------------------------------------------- localisation ---

enum Lang { LANG_RU, LANG_EN, LANG_ES };
extern Lang gLang;
const char *T(const char *ru);				// exact lookup (UI labels, format strings before formatting)
std::string TranslateMessage(const std::string &ru);	// formatted Russian message → current language (pattern match)
void SetLanguage(Lang l);
const char *LangCode(Lang l);				// "ru" / "en" / "es"
Lang LangFromCode(const std::string &code);
void LangMissing(std::vector<std::string> &out);	// Russian strings no pattern matched (dev aid --lang-missing)
void RetranslateReport(Report &rep);			// after a language switch: rebuild msg/detail from the Russian originals

// Names for the UI / export (in the current language).
const char *severityName(int s);
const char *categoryName(int c);
const char *gameName(GameVersion g);

// Vanilla baseline: FNV-1a of "code|file name|object|message" (archive / directory prefix of the file dropped,
// line number ignored) looked up in the embedded list generated from stock SA 1.0 US (vanilla_sa.h).
uint64_t IssueKey(const Issue &is);
bool IsVanillaKey(uint64_t key, int game);	// GameVersion: vanilla_sa.h / vanilla_iii.h / vanilla_vc.h

// Export (hideVanilla: leave out Issue::vanilla entries, like the window does).
bool ExportTxt(const Report &rep, const GameData &gd, const std::string &path, bool hideVanilla = false);
bool ExportHtml(const Report &rep, const GameData &gd, const std::string &path, bool hideVanilla = false, bool hideIgnored = true);	// with the rule help per code
bool ExportCsv(const Report &rep, const std::string &path, bool hideVanilla = false);
bool ExportJson(const Report &rep, const GameData &gd, const std::string &path, bool hideVanilla = false);

} // namespace gc

#endif
