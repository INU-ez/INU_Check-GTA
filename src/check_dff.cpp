// gtacheck — DFF parser + the DFF-nn rules from E:\RE\addon_check\mapdff_path.md
// and the ped skin rules C01..C28 (ped_skin_path.md, mirrored from
// INU_tools/core/skin_lint.py).  Pure chunk walking, every read bounds-checked.
#include "gtacheck.h"

#include <math.h>
#include <algorithm>

namespace gc {


struct DMat {
	uint32_t color;
	bool textured;
	std::string tex, mask;
	int matfx;		// effect type, -1 none
	std::vector<std::string> uvAnims;
	bool uvAnimBad;
	float amb, spec, diff;
	DMat() : color(0xFFFFFFFF), textured(false), matfx(-1), uvAnimBad(false), amb(1), spec(1), diff(1) {}
};

struct DSkin {
	int numBones, numUsed, maxWeights;
	bool oldFormat;	// RW < 3.4 stream: usedBones/maxWeights derived from the weights
	std::vector<uint8_t> used;
	std::vector<uint8_t> indices;	// numVerts*4
	std::vector<float> weights;	// numVerts*4
	std::vector<float> mats;	// numBones*16
	bool ok;
	DSkin() : numBones(0), numUsed(0), maxWeights(0), oldFormat(false), ok(false) {}
};

struct DGeom {
	uint32_t flags;
	int numTris, numVerts, numMorph, numTexSets;
	bool native, prelit, normals, tristrip, modulate;
	int numMaterials;
	std::vector<DMat> mats;
	int maxTriMat;		// highest triangle material id
	int maxTriIdx;		// highest vertex index in triangles
	std::set<int> triMats, meshMats;	// material ids referenced by triangles / by non-empty BinMesh meshes
	// a material no polygon uses: its texture is still requested at load, but nothing is drawn with it
	bool matUsed(int m) const { return hasBinMesh ? meshMats.count(m) != 0 : triMats.count(m) != 0; }
	bool hasBinMesh;
	int binFlags, numMeshes;
	uint32_t totalIndices, sumIndices;
	int maxMeshIdx;
	int maxMeshMat;
	bool hasNight, nightHasColours;
	int num2dfx;
	bool has2dfx;
	bool hasSkin;
	DSkin skin;
	bool hasBreakable;
	float sphere[4];
	float vmin[3], vmax[3];
	bool boundsValid;
	bool hasNative;
	DGeom() : flags(0), numTris(0), numVerts(0), numMorph(0), numTexSets(0), native(false), prelit(false), normals(false),
		tristrip(false), modulate(false), numMaterials(0), maxTriMat(-1), maxTriIdx(-1), hasBinMesh(false), binFlags(0),
		numMeshes(0), totalIndices(0), sumIndices(0), maxMeshIdx(-1), maxMeshMat(-1), hasNight(false), nightHasColours(false),
		num2dfx(0), has2dfx(false), hasSkin(false), hasBreakable(false), boundsValid(false), hasNative(false)
	{ sphere[0]=sphere[1]=sphere[2]=sphere[3]=0; vmin[0]=vmin[1]=vmin[2]=1e30f; vmax[0]=vmax[1]=vmax[2]=-1e30f; }
};

struct DHAnimNode { int id, index, flags; };
struct DFrame {
	int parent;
	std::string name;
	bool nameTooLong;
	bool hasHAnim;
	int hanimVersion, hanimId, hanimFlags, hanimKeySize;
	std::vector<DHAnimNode> nodes;
	bool identity;
	DFrame() : parent(-1), nameTooLong(false), hasHAnim(false), hanimVersion(0), hanimId(0), hanimFlags(0), hanimKeySize(0), identity(true) {}
};

struct DAtomic {
	int frame, geom, flags;
	bool hasPipeline;
	uint32_t pipeline;
	int pipelineLen;
	bool hasMatfx;
	uint32_t matfx;
	DAtomic() : frame(-1), geom(-1), flags(0), hasPipeline(false), pipeline(0), pipelineLen(0), hasMatfx(false), matfx(0) {}
};

struct Dff {
	uint32_t version;
	bool hasUvDict, uvDictFirst;
	std::set<std::string> uvDictNames;
	int numAtomicsHdr, numLights, numCameras;
	std::vector<DFrame> frames;
	std::vector<DGeom> geoms;
	std::vector<DAtomic> atomics;
	bool hasCollision;
	size_t colStart, colEnd;
	bool fatal;	// engine LOAD-FAIL / crash already reported
	Dff() : version(0), hasUvDict(false), uvDictFirst(false), numAtomicsHdr(0), numLights(0), numCameras(0), hasCollision(false), colStart(0), colEnd(0), fatal(false) {}
};

struct P {	// parse context
	Context *ctx;
	const std::string *where;
	std::string obj;
	int id;
	int entry;	// GameData::entries index (for fixes), -1
	bool mapModel;	// objs / tobj (building pipeline rules apply)
	bool pedModel;
	Dff *d;
	Buf b;
	const std::vector<uint8_t> *data;
	void add(Severity s, const char *code, const std::string &msg, const std::string &detail = "", Category cat = CAT_DFF)
	{ ctx->add(s, cat, code, *where, -1, obj, msg, detail, id); }
	void fatal(const char *code, const std::string &msg, const std::string &detail = "")
	{ add(SEV_FATAL, code, msg, detail); d->fatal = true; }
};

// Walks an EXTENSION chunk: calls fn(child) for each child, checks DFF-02.
template<class F>
static bool walkExtension(P &p, size_t limit, const char *owner, F fn)
{
	Chunk ext;
	size_t save = p.b.pos;
	if(!findChunk(p.b, 0x3, ext, limit)){
		p.fatal("DFF-02", fmt("%s без чанка EXTENSION — _rwPluginRegistryReadDataChunks ищет его до конца файла → LOAD-FAIL", owner));
		p.b.seek(save);
		return false;
	}
	if(ext.truncated){
		p.fatal("DFF-02", fmt("%s: EXTENSION длиной %u выходит за конец файла", owner, ext.size));
		return false;
	}
	size_t pos = ext.start;
	while(pos + 12 <= ext.end){
		p.b.seek(pos);
		Chunk c;
		readChunk(p.b, c);
		if(c.end > ext.end || c.truncated){
			p.fatal("DFF-02", fmt("%s: плагин %s (0x%X) длиной %u не помещается в EXTENSION (осталось %u) — остаток читается как данные плагина → LOAD-FAIL",
			        owner, rwChunkName(c.type), c.type, c.size, (unsigned)(ext.end - pos - 12)));
			return false;
		}
		fn(c);
		pos = c.end;
	}
	if(pos != ext.end)
		p.fatal("DFF-02", fmt("%s: длины дочерних чанков EXTENSION не сходятся (%u байт хвоста)", owner, (unsigned)(ext.end - pos)));
	p.b.seek(ext.end);
	return true;
}

static std::string readString(P &p, size_t limit, bool *tooLong)
{
	Chunk s;
	if(tooLong) *tooLong = false;
	if(!findChunk(p.b, 0x2, s, limit)) return "";
	if(s.size > 128 && tooLong) *tooLong = true;
	std::string r;
	size_t n = s.end - s.start;
	const uint8_t *q = p.data->data() + s.start;
	size_t len = 0;
	while(len < n && q[len]) len++;
	r.assign((const char*)q, len);
	p.b.seek(s.end);
	return r;
}

static void parseUvDict(P &p, const Chunk &dict)
{
	Dff &d = *p.d;
	d.hasUvDict = true;
	Chunk st;
	if(!findChunk(p.b, 0x1, st, dict.end)) { p.b.seek(dict.end); return; }
	int count = p.b.i32();
	p.b.seek(st.end);
	for(int i = 0; i < count && i < 4096; i++){
		Chunk a;
		if(!findChunk(p.b, 0x1B, a, dict.end)) break;
		// RtAnimAnimation: i32 version(0x100), i32 typeID, i32 numFrames, i32 flags, f32 duration; custom: name[32], map[8]
		Buf s(p.data->data() + a.start, a.end - a.start);
		int ver = s.i32(); int typeID = s.i32(); int numFrames = s.i32(); s.i32(); s.f32();
		std::string name = s.str(32);
		(void)ver;
		if(typeID != 0x1C1 && typeID != 0x1C0)
			p.add(SEV_ERROR, "DFF-50", fmt("UV-анимация «%s»: тип интерполятора 0x%X (ожидается 0x1C1) — словарь не прочитается, анимации станут статичными", name.c_str(), typeID));
		if(numFrames < 2)
			p.add(SEV_WARN, "DFF-50", fmt("UV-анимация «%s»: %d кадров", name.c_str(), numFrames));
		if(!name.empty()) d.uvDictNames.insert(lower(name));
		p.b.seek(a.end);
	}
	p.b.seek(dict.end);
}

static void parseFrameList(P &p, const Chunk &fl)
{
	Dff &d = *p.d;
	Chunk st;
	if(!findChunk(p.b, 0x1, st, fl.end)){ p.fatal("DFF-02", "FRAMELIST без STRUCT"); return; }
	int numFrames = p.b.i32();
	if(numFrames < 0 || (size_t)numFrames * 56 > st.end - st.start + 4){
		p.fatal("DFF-03", fmt("FRAMELIST: numFrames = %d не помещается в STRUCT (%u байт)", numFrames, st.size));
		return;
	}
	for(int i = 0; i < numFrames; i++){
		DFrame f;
		float m[12];
		for(int k = 0; k < 12; k++) m[k] = p.b.f32();
		f.parent = p.b.i32();
		p.b.i32();	// matrix flags
		f.identity = fabsf(m[0]-1) < 1e-4f && fabsf(m[4]-1) < 1e-4f && fabsf(m[8]-1) < 1e-4f &&
		             fabsf(m[1]) < 1e-4f && fabsf(m[2]) < 1e-4f && fabsf(m[3]) < 1e-4f && fabsf(m[5]) < 1e-4f &&
		             fabsf(m[6]) < 1e-4f && fabsf(m[7]) < 1e-4f && fabsf(m[9]) < 1e-3f && fabsf(m[10]) < 1e-3f && fabsf(m[11]) < 1e-3f;
		for(int k = 0; k < 12; k++) if(!(m[k] == m[k])) { p.add(SEV_ERROR, "DFF-06", fmt("фрейм %d: NaN в матрице", i)); break; }
		if(f.parent >= i || f.parent >= numFrames){
			p.fatal("DFF-06", fmt("фрейм %d ссылается на родителя %d (%s) — RwFrameAddChild по неинициализированному указателю", i, f.parent,
			        f.parent >= numFrames ? "нет такого фрейма" : "родитель идёт ПОСЛЕ ребёнка"),
			        "Родитель должен быть записан раньше ребёнка (индекс меньше).");
		}
		d.frames.push_back(f);
	}
	p.b.seek(st.end);
	for(int i = 0; i < numFrames && p.b.ok; i++){
		DFrame &f = d.frames[i];
		walkExtension(p, fl.end, fmt("фрейм %d", i).c_str(), [&](const Chunk &c){
			if(c.type == 0x253F2FE){
				size_t n = c.end - c.start;
				f.name.assign((const char*)p.data->data() + c.start, n);
				size_t z = f.name.find('\0');
				if(z != std::string::npos) f.name.resize(z);
				if(n >= 24){
					f.nameTooLong = true;
					// the engine reads the WHOLE chunk into a 24-byte plugin and writes name[len] = 0:
					// a 24+ char name overruns by the excess, a name padded with zeros to 24 bytes overruns by one byte
					if(f.name.size() > 23){
						p.ctx->add(SEV_FATAL, CAT_DFF, "DFF-30", *p.where, -1, p.obj,
						           fmt("имя фрейма «%s» — %u символов (чанк NodeName %u байт) — NodeNameStreamRead читает весь чанк в 24-байтовый плагин и пишет name[%u]=0 за его пределы", f.name.c_str(), (unsigned)f.name.size(), (unsigned)n, (unsigned)n),
						           "Имя фрейма ≤ 23 символов. Суффиксы вроде _DFF игре не нужны (objs-модель ищется по id; из имени читаются только _dam / _l0). Кнопка «Исправить»: авто (обрезать до 23) или вручную.",
						           p.id, FIX_DFF_FRAMENAME, p.entry);
						d.fatal = true;
					}else
						p.ctx->add(SEV_ERROR, CAT_DFF, "DFF-30", *p.where, -1, p.obj,
						           fmt("имя фрейма «%s» — %u символов, но чанк NodeName %u байт (дополнен нулями) — движок пишет name[%u]=0 на один байт за пределы 24-байтового плагина", f.name.c_str(), (unsigned)f.name.size(), (unsigned)n, (unsigned)n),
						           "Так писал старый экспортёр INU Tools (паддинг до 24). Кнопка «Исправить» убирает паддинг (авто) или даёт переименовать фреймы вручную; суффикс _DFF игре безразличен.",
						           p.id, FIX_DFF_FRAMENAME, p.entry);
				}
			}else if(c.type == 0x11E){
				Buf s(p.data->data() + c.start, c.end - c.start);
				f.hasHAnim = true;
				f.hanimVersion = s.i32();
				f.hanimId = s.i32();
				int numNodes = s.i32();
				if(numNodes){
					f.hanimFlags = s.i32();
					f.hanimKeySize = s.i32();
					if(numNodes < 0 || numNodes > 100000){ p.fatal("PED-15", fmt("HAnim: numNodes = %d — мусор", numNodes)); return; }
					for(int k = 0; k < numNodes && s.ok; k++){
						DHAnimNode n;
						n.id = s.i32(); n.index = s.i32(); n.flags = s.i32();
						f.nodes.push_back(n);
					}
					if(!s.ok) p.fatal("PED-15", fmt("HAnim на фрейме «%s»: таблица узлов обрезана", f.name.c_str()));
				}
			}
		});
	}
}

static void parseMaterial(P &p, DGeom &g, const Chunk &mc, DMat &m)
{
	Chunk st;
	if(!findChunk(p.b, 0x1, st, mc.end)){ p.fatal("DFF-02", "MATERIAL без STRUCT"); return; }
	if(st.size > 28)
		p.fatal("DFF-15", fmt("STRUCT материала длиной %u > 28 — RwStreamRead переполняет локальный буфер, краш при возврате", st.size));
	else if(st.size < 28)
		p.add(SEV_ERROR, "DFF-15", fmt("STRUCT материала длиной %u < 28 — поля обнулятся (чёрный цвет, текстура не читается)", st.size));
	p.b.i32();	// flags
	m.color = p.b.u32();
	p.b.i32();	// unused
	m.textured = p.b.i32() != 0;
	if(st.size >= 28){ m.amb = p.b.f32(); m.spec = p.b.f32(); m.diff = p.b.f32(); }
	p.b.seek(st.end);
	if(m.textured){
		Chunk tc;
		if(!findChunk(p.b, 0x6, tc, mc.end)){
			p.fatal("DFF-02", "материал с textured=1 без чанка TEXTURE");
			return;
		}
		Chunk ts;
		if(findChunk(p.b, 0x1, ts, tc.end)){
			if(ts.size > 0x110) p.fatal("DFF-17", fmt("STRUCT текстуры длиной %u > 0x110 — переполнение стека RwTextureStreamRead", ts.size));
			p.b.seek(ts.end);
		}
		bool tl = false, ml = false;
		m.tex = readString(p, tc.end, &tl);
		m.mask = readString(p, tc.end, &ml);
		if(tl) p.fatal("DFF-18", fmt("имя текстуры «%s»: строковый чанк > 128 байт — переполнение буфера _rwStringStreamFindAndRead", m.tex.c_str()));
		if(ml) p.add(SEV_ERROR, "DFF-18", "имя маски > 128 байт");
		walkExtension(p, tc.end, "текстура", [&](const Chunk &){});
		p.b.seek(tc.end);
	}
	walkExtension(p, mc.end, "материал", [&](const Chunk &c){
		if(c.type == 0x120){
			Buf s(p.data->data() + c.start, c.end - c.start);
			m.matfx = s.i32();
		}else if(c.type == 0x135){
			Buf s(p.data->data() + c.start, c.end - c.start);
			// STRUCT chunk inside
			Chunk hdr;
			Buf s2 = s;
			if(readChunk(s2, hdr) && hdr.type == 0x1){
				Buf u(p.data->data() + c.start + hdr.start, hdr.end - hdr.start);
				uint32_t mask = u.u32();
				int n = 0;
				for(int i = 0; i < 32; i++) if(mask & (1u << i)) n++;
				if(mask & 0xFFFFFF00){
					p.fatal("DFF-54", fmt("UV-анимация: маска слотов 0x%X использует биты ≥ 8 — имена не читаются, STRUCT рассинхронизируется → LOAD-FAIL", mask));
					m.uvAnimBad = true;
				}
				for(int i = 0; i < 8; i++) if(mask & (1u << i)){
					std::string nm = u.str(32);
					if(!u.ok){ p.fatal("DFF-54", "UV-анимация: имя обрезано"); m.uvAnimBad = true; break; }
					m.uvAnims.push_back(nm);
				}
			}
		}
	});
	(void)g;
}

static void parseGeometry(P &p, const Chunk &gc_, DGeom &g)
{
	Dff &d = *p.d;
	Chunk st;
	if(!findChunk(p.b, 0x1, st, gc_.end)){ p.fatal("DFF-02", "GEOMETRY без STRUCT"); return; }
	g.flags = p.b.u32();
	g.numTris = p.b.i32();
	g.numVerts = p.b.i32();
	g.numMorph = p.b.i32();
	if(gc_.version < 0x34001) p.b.skip(12);
	g.native = (g.flags & 0x01000000) != 0;
	g.prelit = (g.flags & 8) != 0;
	g.normals = (g.flags & 0x10) != 0;
	g.tristrip = (g.flags & 1) != 0;
	g.modulate = (g.flags & 0x40) != 0;
	g.numTexSets = (g.flags >> 16) & 0xFF;
	if(g.numTexSets == 0) g.numTexSets = (g.flags & 0x80) ? 2 : ((g.flags & 4) ? 1 : 0);
	std::string gi = fmt("геометрия %d", (int)d.geoms.size());
	if(g.numVerts < 0 || g.numVerts >= 0x10000 || g.numTris < 0){
		p.fatal("DFF-09", fmt("%s: numVertices = %d, numTriangles = %d — RpGeometryCreate вернёт NULL (лимит 65535 вершин) → LOAD-FAIL", gi.c_str(), g.numVerts, g.numTris));
		return;
	}
	if(g.numTexSets > 8){
		p.fatal("DFF-11", fmt("%s: %d наборов UV — RpGeometryCreate пишет указатели за пределы массива (+0x34..+0x50) → краш", gi.c_str(), g.numTexSets));
		return;
	}
	if(g.numTexSets > 2) p.add(SEV_WARN, "DFF-11", fmt("%s: %d наборов UV, пайплайны используют максимум 2", gi.c_str(), g.numTexSets));
	if(g.native){
		p.fatal("DFF-12", fmt("%s: флаг NATIVE (0x01000000) — на PC массивы не выделяются, чтение вершин в NULL → краш", gi.c_str()));
		return;
	}
	if(g.numMorph == 0){
		p.fatal("DFF-10", fmt("%s: numMorphTargets = 0 — движок всё равно читает один morph-блок, поток рассинхронизируется → LOAD-FAIL", gi.c_str()));
		return;
	}
	if(g.numMorph > 1) p.add(SEV_INFO, "DFF-10", fmt("%s: %d morph-целей, рендерится только первая", gi.c_str(), g.numMorph));
	if(g.numVerts == 0 || g.numTris == 0)
		p.add(SEV_WARN, "DFF-26", fmt("%s: %d вершин, %d треугольников — пустая геометрия", gi.c_str(), g.numVerts, g.numTris));
	// vertex data
	if(g.numVerts){
		if(g.prelit){
			if(!p.b.skip((size_t)g.numVerts * 4)){ p.fatal("DFF-13", fmt("%s: prelit-цвета обрезаны", gi.c_str())); return; }
		}
		if(!p.b.skip((size_t)g.numVerts * 8 * g.numTexSets)){ p.fatal("DFF-13", fmt("%s: UV обрезаны", gi.c_str())); return; }
		for(int i = 0; i < g.numTris; i++){
			int v1 = p.b.u16(), v0 = p.b.u16(), mat = p.b.u16(), v2 = p.b.u16();
			if(!p.b.ok){ p.fatal("DFF-13", fmt("%s: треугольники обрезаны", gi.c_str())); return; }
			if(mat > g.maxTriMat) g.maxTriMat = mat;
			g.triMats.insert(mat);
			int mx = v0 > v1 ? v0 : v1; if(v2 > mx) mx = v2;
			if(mx > g.maxTriIdx) g.maxTriIdx = mx;
		}
	}
	for(int t = 0; t < g.numMorph; t++){
		float sx = p.b.f32(), sy = p.b.f32(), sz = p.b.f32(), sr = p.b.f32();
		int hasVerts = p.b.i32(), hasNormals = p.b.i32();
		if(!p.b.ok){ p.fatal("DFF-10", fmt("%s: morph-блок обрезан", gi.c_str())); return; }
		if(t == 0){ g.sphere[0] = sx; g.sphere[1] = sy; g.sphere[2] = sz; g.sphere[3] = sr; }
		if(hasNormals && !g.normals){
			p.fatal("DFF-13", fmt("%s: hasNormals=1, но у геометрии нет флага NORMALS — RwStreamRead в NULL → краш", gi.c_str()));
			return;
		}
		if(!hasVerts && g.numVerts > 0 && t == 0)
			p.add(SEV_ERROR, "DFF-13", fmt("%s: hasVertices=0 при %d вершинах — массив вершин останется мусором", gi.c_str(), g.numVerts));
		if(hasVerts){
			for(int i = 0; i < g.numVerts; i++){
				float x = p.b.f32(), y = p.b.f32(), z = p.b.f32();
				if(!p.b.ok) break;
				if(t == 0){
					if(x < g.vmin[0]) g.vmin[0] = x; if(x > g.vmax[0]) g.vmax[0] = x;
					if(y < g.vmin[1]) g.vmin[1] = y; if(y > g.vmax[1]) g.vmax[1] = y;
					if(z < g.vmin[2]) g.vmin[2] = z; if(z > g.vmax[2]) g.vmax[2] = z;
					if(!(x == x && y == y && z == z)) { p.add(SEV_ERROR, "DFF-13", fmt("%s: NaN в вершинах", gi.c_str())); }
				}
			}
			if(t == 0 && g.numVerts) g.boundsValid = true;
		}
		if(hasNormals && !p.b.skip((size_t)g.numVerts * 12)){ p.fatal("DFF-13", fmt("%s: нормали обрезаны", gi.c_str())); return; }
	}
	if(!p.b.ok){ p.fatal("DFF-13", fmt("%s: данные вершин короче объявленного (флаг PRELIT/UV не соответствует данным?) → LOAD-FAIL", gi.c_str())); return; }
	if(p.b.pos != st.end)
		p.add(SEV_WARN, "DFF-13", fmt("%s: STRUCT геометрии на %d байт %s, чем прочитано", gi.c_str(), (int)((long)st.end - (long)p.b.pos), st.end > p.b.pos ? "длиннее" : "короче"));
	p.b.seek(st.end);
	// bounding sphere vs vertices (DFF-25)
	if(g.boundsValid && g.numVerts){
		// farthest vertex distance would need a second pass; use the box corners as a bound
		float dx = std::max(fabsf(g.vmax[0] - g.sphere[0]), fabsf(g.vmin[0] - g.sphere[0]));
		float dy = std::max(fabsf(g.vmax[1] - g.sphere[1]), fabsf(g.vmin[1] - g.sphere[1]));
		float dz = std::max(fabsf(g.vmax[2] - g.sphere[2]), fabsf(g.vmin[2] - g.sphere[2]));
		float corner = sqrtf(dx*dx + dy*dy + dz*dz);
		float maxAxis = std::max(dx, std::max(dy, dz));
		if(g.sphere[3] <= 0.0f)
			p.add(SEV_ERROR, "DFF-25", fmt("%s: радиус ограничивающей сферы %.3f ≤ 0 — считается «всегда внутри фрустума», D3D-клиппинг выключается", gi.c_str(), g.sphere[3]));
		else if(maxAxis > g.sphere[3] * 1.05f + 0.05f)
			p.add(SEV_ERROR, "DFF-25", fmt("%s: сфера r=%.2f не накрывает вершины (вершины отстоят до %.2f по оси, до %.2f по диагонали) — треугольники у камеры не клипуются", gi.c_str(), g.sphere[3], maxAxis, corner),
			        "Пересчитай ограничивающую сферу при экспорте.");
	}
	// material list
	Chunk ml;
	if(!findChunk(p.b, 0x8, ml, gc_.end)){ p.fatal("DFF-02", fmt("%s: нет MATERIALLIST", gi.c_str())); return; }
	Chunk ms;
	if(!findChunk(p.b, 0x1, ms, ml.end)){ p.fatal("DFF-02", fmt("%s: MATERIALLIST без STRUCT", gi.c_str())); return; }
	g.numMaterials = p.b.i32();
	if(g.numMaterials < 0 || g.numMaterials > 100000){ p.fatal("DFF-14", fmt("%s: numMaterials = %d — мусор", gi.c_str(), g.numMaterials)); return; }
	std::vector<int> idx((size_t)g.numMaterials);
	for(int i = 0; i < g.numMaterials; i++) idx[(size_t)i] = p.b.i32();
	p.b.seek(ms.end);
	int readSoFar = 0;
	for(int i = 0; i < g.numMaterials && p.b.ok; i++){
		if(idx[(size_t)i] >= 0){
			if(idx[(size_t)i] >= readSoFar)
				p.fatal("DFF-16", fmt("%s: материал %d переиспользует слот %d, который ещё не прочитан — инкремент refCount по мусорному указателю", gi.c_str(), i, idx[(size_t)i]));
			DMat m;
			if(idx[(size_t)i] >= 0 && idx[(size_t)i] < (int)g.mats.size()) m = g.mats[(size_t)idx[(size_t)i]];
			g.mats.push_back(m);
			readSoFar++;
			continue;
		}
		Chunk mc;
		if(!findChunk(p.b, 0x7, mc, ml.end)){ p.fatal("DFF-02", fmt("%s: заявлено %d материалов, найдено %d чанков MATERIAL", gi.c_str(), g.numMaterials, i)); return; }
		DMat m;
		parseMaterial(p, g, mc, m);
		g.mats.push_back(m);
		readSoFar++;
		p.b.seek(mc.end);
	}
	p.b.seek(ml.end);
	// geometry extension
	walkExtension(p, gc_.end, gi.c_str(), [&](const Chunk &c){
		Buf s(p.data->data() + c.start, c.end - c.start);
		switch(c.type){
		case 0x50E: {
			g.hasBinMesh = true;
			g.binFlags = s.i32();
			g.numMeshes = s.i32();
			g.totalIndices = s.u32();
			if(g.numMeshes < 0 || g.numMeshes > 65535){ p.fatal("DFF-22", fmt("%s: BinMesh numMeshes = %d", gi.c_str(), g.numMeshes)); break; }
			for(int m = 0; m < g.numMeshes && s.ok; m++){
				uint32_t ni = s.u32();
				int mat = s.i32();
				if(mat > g.maxMeshMat) g.maxMeshMat = mat;
				if(ni > 0) g.meshMats.insert(mat);
				if(mat < 0) p.fatal("DFF-21", fmt("%s: BinMesh %d с materialIndex %d", gi.c_str(), m, mat));
				if(ni > 50000000){ p.fatal("DFF-22", fmt("%s: BinMesh %d: %u индексов — мусор", gi.c_str(), m, ni)); break; }
				g.sumIndices += ni;
				for(uint32_t k = 0; k < ni && s.ok; k++){
					int v = s.i32();
					if(v > g.maxMeshIdx) g.maxMeshIdx = v;
				}
				if(!s.ok){ p.fatal("DFF-22", fmt("%s: BinMesh обрезан (индексы меша %d)", gi.c_str(), m)); break; }
			}
			break; }
		case 0x253F2F9: {
			g.hasNight = true;
			uint32_t has = s.u32();
			g.nightHasColours = has != 0;
			if(has){
				size_t need = (size_t)g.numVerts * 4;
				size_t got = c.end - c.start - 4;
				if(got != need)
					p.fatal("DFF-45", fmt("%s: блок ночных цветов %u байт вместо %u (numVertices×4) — RwStreamRead игнорирует длину чанка → рассинхрон → LOAD-FAIL", gi.c_str(), (unsigned)got, (unsigned)need));
				if(!g.prelit)
					p.add(SEV_WARN, "DFF-43", fmt("%s: есть ночные цвета, но нет флага PRELIT / дневных цветов — DN-пайплайн не подключится, модель без вершинного цвета", gi.c_str()),
					        "Экспортируй дневные vertex colours вместе с ночными.");
			}
			break; }
		case 0x253F2F8: {
			g.has2dfx = true;
			int count = s.i32();
			if(count < 0 || count > 100000){ p.fatal("DFF-32", fmt("%s: 2dfx count = %d — мусор", gi.c_str(), count)); break; }
			g.num2dfx = count;
			for(int e = 0; e < count && s.ok; e++){
				float ex = s.f32(), ey = s.f32(), ez = s.f32();
				(void)ex; (void)ey; (void)ez;
				uint32_t type = s.u32() & 0xFF;
				uint32_t size = s.u32();
				if(!s.ok){ p.fatal("DFF-32", fmt("%s: 2dfx-запись %d обрезана", gi.c_str(), e)); break; }
				size_t dataStart = s.pos;
				bool consumed = true;
				if(type != 4 && type <= 10 && type != 2 && type != 5 && s.left() < size){ p.fatal("DFF-32", fmt("%s: 2dfx-запись %d объявляет %u байт, в чанке осталось %u", gi.c_str(), e, size, (unsigned)s.left())); break; }
				switch(type){
				case 0:
					if(size != 0x50 && size != 0x4C){ p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx light №%d с dataSize %u (нужно 0x50/0x4C) — эффект отброшен", gi.c_str(), e, size)); }
					else{
						Buf l(s.p + s.pos, size);
						l.skip(0x19);
						std::string corona = l.str(24), shadow = l.str(24);
						float shadowSize; { Buf l2(s.p + s.pos, size); l2.skip(0x10); shadowSize = l2.f32(); }
						uint8_t showMode; { Buf l3(s.p + s.pos, size); l3.skip(0x14); showMode = l3.u8(); }
						uint8_t flags1; { Buf l4(s.p + s.pos, size); l4.skip(0x18); flags1 = l4.u8(); }
						float coronaFar; { Buf l5(s.p + s.pos, size); l5.skip(4); coronaFar = l5.f32(); }
						if(p.ctx->gd->isSA() && !p.ctx->gd->particleTextures.empty()){
							if(!corona.empty() && !p.ctx->gd->particleTextures.count(lower(corona)))
								p.add(SEV_ERROR, "DFF-34", fmt("2dfx light №%d: текстура короны «%s» нет в particle.txd — корона не рисуется", e, corona.c_str()));
							if(shadowSize != 0.0f && (shadow.empty() || !p.ctx->gd->particleTextures.count(lower(shadow))))
								p.add(SEV_FATAL, "DFF-34", fmt("2dfx light №%d: shadowSize %.2f ≠ 0, а текстуры тени «%s» нет в particle.txd — RenderStaticShadows разыменует NULL", e, shadowSize, shadow.c_str()));
						}
						if(showMode > 13) p.add(SEV_WARN, "DFF-35", fmt("2dfx light №%d: coronaShowMode %d > 13 — свет никогда не включится", e, showMode));
						if(!(flags1 & 0x60)) p.add(SEV_WARN, "DFF-35", fmt("2dfx light №%d: не заданы флаги atDay/atNight (0x20/0x40) — свет никогда не включится", e));
						if(coronaFar <= 0.0f) p.add(SEV_INFO, "DFF-35", fmt("2dfx light №%d: coronaFarClip %.1f — корона не рисуется", e, coronaFar));
					}
					break;
				case 1:
					if(size != 0x18) p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx particle №%d с dataSize %u (нужно 0x18) — отброшен", gi.c_str(), e, size));
					else{
						Buf l(s.p + s.pos, size);
						std::string fx = l.str(24);
						if(fx.empty()) p.add(SEV_INFO, "DFF-36", fmt("2dfx particle №%d с пустым именем системы — эффект не создастся", e));
						else if(p.ctx->gd->isSA() && !p.ctx->gd->fxSystems.empty() && !p.ctx->gd->fxSystems.count(lower(fx)))
							p.add(SEV_ERROR, "DFF-36", fmt("2dfx particle №%d: система «%s» отсутствует в effects.fxp — эффект не создастся", e, fx.c_str()));
					}
					break;
				case 3: if(size != 0x38) p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx ped attractor №%d с dataSize %u (нужно 0x38) — отброшен", gi.c_str(), e, size)); break;
				case 4:
					if(size != 0){ p.fatal("DFF-31", fmt("%s: 2dfx sun glare №%d с dataSize %u — движок не читает и не пропускает данные → рассинхрон → LOAD-FAIL", gi.c_str(), e, size)); consumed = false; }
					break;
				case 6:
					if(size != 0x2C && size != 0x28) p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx enex №%d с dataSize %u (нужно 0x2C) — отброшен", gi.c_str(), e, size));
					else if(size == 0x28) p.add(SEV_WARN, "DFF-38", fmt("2dfx enex №%d размером 0x28 — flags2 остаётся мусором со стека; пиши 0x2C", e));
					break;
				case 7: if(size != 0x58) p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx roadsign №%d с dataSize %u (нужно 0x58) — отброшен", gi.c_str(), e, size)); break;
				case 8: if(size != 4) p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx trigger №%d с dataSize %u (нужно 4) — отброшен", gi.c_str(), e, size)); break;
				case 9: if(size != 0xC) p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx cover point №%d с dataSize %u (нужно 0xC) — отброшен", gi.c_str(), e, size)); break;
				case 10: if(size != 0x28) p.add(SEV_ERROR, "DFF-32", fmt("%s: 2dfx escalator №%d с dataSize %u (нужно 0x28) — отброшен", gi.c_str(), e, size)); break;
				default:
					p.fatal("DFF-32", fmt("%s: 2dfx №%d неизвестного типа %u — движок ничего не читает и не пропускает → рассинхрон → LOAD-FAIL", gi.c_str(), e, type));
					consumed = false;
					break;
				}
				if(!consumed) break;
				s.seek(dataStart + size);
				if(!s.ok){ p.fatal("DFF-32", fmt("%s: 2dfx-запись %d выходит за чанк", gi.c_str(), e)); break; }
			}
			if(s.ok && s.left() != 0 && count >= 0)
				p.add(SEV_WARN, "DFF-32", fmt("%s: в чанке 2dfx %u лишних байт после %d записей", gi.c_str(), (unsigned)s.left(), count));
			break; }
		case 0x116: {
			g.hasSkin = true;
			DSkin &sk = g.skin;
			sk.numBones = s.u8(); sk.numUsed = s.u8(); sk.maxWeights = s.u8(); s.u8();
			bool oldFormat = sk.numUsed == 0;
			if(!oldFormat){
				const uint8_t *u = s.ptr((size_t)sk.numUsed);
				if(u) sk.used.assign(u, u + sk.numUsed);
			}
			const uint8_t *ind = s.ptr((size_t)g.numVerts * 4);
			if(ind) sk.indices.assign(ind, ind + (size_t)g.numVerts * 4);
			sk.weights.resize((size_t)g.numVerts * 4);
			for(size_t k = 0; k < sk.weights.size() && s.ok; k++) sk.weights[k] = s.f32();
			sk.mats.resize((size_t)sk.numBones * 16);
			for(int bn = 0; bn < sk.numBones && s.ok; bn++){
				if(oldFormat) s.skip(4);
				for(int k = 0; k < 16; k++) sk.mats[(size_t)bn * 16 + k] = s.f32();
			}
			if(!oldFormat){ s.i32(); int nm = s.i32(); int rle = s.i32(); if(nm) s.skip((size_t)sk.numBones + 2 * ((size_t)nm + (size_t)rle)); }
			sk.oldFormat = oldFormat;
			if(oldFormat && s.ok){
				// RW < 3.4 (III/VC files): no usedBones/maxWeights in the stream — the plugin derives them from the weights
				std::set<int> usedSet;
				for(int v = 0; v < g.numVerts; v++){
					int n = 0;
					for(int k = 0; k < 4; k++) if(sk.weights[(size_t)v * 4 + k] != 0.0f){ n++; usedSet.insert(sk.indices[(size_t)v * 4 + k]); }
					if(n > sk.maxWeights) sk.maxWeights = n;
				}
				for(int bn : usedSet) sk.used.push_back((uint8_t)bn);
				sk.numUsed = (int)sk.used.size();
			}
			sk.ok = s.ok;
			if(!s.ok){ p.ctx->add(SEV_FATAL, CAT_PED, "PED-23", *p.where, -1, p.obj, fmt("%s: чанк Skin короче, чем требуют его счётчики (numBones %d, вершин %d) — RpClumpStreamRead вернёт NULL", gi.c_str(), sk.numBones, g.numVerts), "", p.id); d.fatal = true; }
			else if(s.left() != 0) p.add(SEV_WARN, "PED-23", fmt("%s: в чанке Skin %u лишних байт", gi.c_str(), (unsigned)s.left()));
			break; }
		case 0x253F2FD: {
			g.hasBreakable = true;
			uint32_t magic = s.u32();
			if(magic){
				int posRule = s.i32(); (void)posRule;
				int nv = s.u16(); s.u16(); s.skip(12);
				int nt = s.u16(); s.u16(); s.skip(8);
				int nm = s.u16(); s.u16(); s.skip(16);
				size_t need = (size_t)nv * (12 + 8 + 4) + (size_t)nt * (6 + 2) + (size_t)nm * (32 + 32 + 12);
				if(!s.ok || s.left() < need)
					p.add(SEV_ERROR, "DFF-60", fmt("%s: Breakable объявляет %d вершин / %d треугольников / %d материалов, но данных %u байт вместо %u — массивы останутся мусором", gi.c_str(), nv, nt, nm, (unsigned)s.left(), (unsigned)need));
				else{
					s.skip((size_t)nv * 24);
					int maxi = -1;
					for(int t = 0; t < nt; t++){ int a = s.u16(), b2 = s.u16(), c2 = s.u16(); int mx = std::max(a, std::max(b2, c2)); if(mx > maxi) maxi = mx; }
					int maxm = -1;
					for(int t = 0; t < nt; t++){ int m = s.u16(); if(m > maxm) maxm = m; }
					if(maxi >= nv) p.add(SEV_ERROR, "DFF-60", fmt("%s: Breakable: индекс вершины %d ≥ %d — краш при разрушении", gi.c_str(), maxi, nv));
					if(maxm >= nm) p.add(SEV_ERROR, "DFF-60", fmt("%s: Breakable: индекс материала %d ≥ %d", gi.c_str(), maxm, nm));
				}
			}
			break; }
		case 0x510:
			g.hasNative = true;
			p.add(SEV_WARN, "DFF-12", fmt("%s: чанк NativeData (0x510) — платформенные данные, на PC-экспорте не место", gi.c_str()));
			break;
		default: break;
		}
	});
	// consistency after extension
	if(g.numMaterials == 0 && ((g.hasBinMesh && g.numMeshes > 0) || (!g.hasBinMesh && g.numTris > 0)))
		p.fatal("DFF-14", fmt("%s: 0 материалов при наличии мешей/треугольников — _rpMaterialListGetMaterial разыменует NULL → краш", gi.c_str()));
	if(g.hasBinMesh){
		if(g.maxMeshMat >= g.numMaterials && g.numMaterials > 0)
			p.fatal("DFF-21", fmt("%s: BinMesh materialIndex %d ≥ numMaterials %d — указатель за массивом, краш при рендере", gi.c_str(), g.maxMeshMat, g.numMaterials));
		if(g.sumIndices > g.totalIndices)
			p.fatal("DFF-22", fmt("%s: сумма индексов мешей %u > totalIndices %u — запись за выделенный буфер (порча кучи)", gi.c_str(), g.sumIndices, g.totalIndices));
		else if(g.sumIndices < g.totalIndices)
			p.add(SEV_INFO, "DFF-22", fmt("%s: сумма индексов %u < totalIndices %u", gi.c_str(), g.sumIndices, g.totalIndices));
		if(g.numMeshes == 0 && g.numTris > 0)
			p.add(SEV_ERROR, "DFF-22", fmt("%s: BinMesh без мешей — ничего не рисуется (невидимая модель)", gi.c_str()));
		if(g.maxMeshIdx >= g.numVerts && g.numVerts > 0)
			p.add(g.maxMeshIdx >= g.numVerts * 4 ? SEV_FATAL : SEV_ERROR, "DFF-23", fmt("%s: индекс %d в BinMesh ≥ numVertices %d — треугольники из мусора (при большом выходе — краш инстансера)", gi.c_str(), g.maxMeshIdx, g.numVerts));
		if((g.binFlags == 1) != g.tristrip)
			p.add(SEV_INFO, "DFF-24", fmt("%s: BinMesh flags=%d (%s), а у геометрии флаг TRISTRIP %s — при наличии BinMesh рисуется по его флагу", gi.c_str(), g.binFlags, g.binFlags == 1 ? "strip" : "list", g.tristrip ? "есть" : "нет"));
	}else{
		if(g.maxTriMat >= g.numMaterials && g.numMaterials > 0 && g.numTris > 0)
			p.fatal("DFF-20", fmt("%s: без BinMesh; matId %d в треугольниках ≥ numMaterials %d — RpGeometryUnlock разыменует мусор", gi.c_str(), g.maxTriMat, g.numMaterials));
		if(g.maxTriIdx >= g.numVerts && g.numVerts > 0)
			p.add(SEV_ERROR, "DFF-23", fmt("%s: индекс вершины %d ≥ numVertices %d", gi.c_str(), g.maxTriIdx, g.numVerts));
		if(g.numTris > 0)
			p.add(SEV_INFO, "DFF-24", fmt("%s: без чанка BinMesh — меши строятся при загрузке из треугольников", gi.c_str()));
	}
	int blackMats = 0;
	for(size_t i = 0; i < g.mats.size(); i++){
		const DMat &m = g.mats[i];
		unsigned r = m.color & 0xFF, gg = (m.color >> 8) & 0xFF, bb = (m.color >> 16) & 0xFF;
		if(g.modulate && r == 0 && gg == 0 && bb == 0 && p.mapModel) blackMats++;
	}
	if(blackMats && p.mapModel)
		p.add(blackMats == (int)g.mats.size() ? SEV_ERROR : SEV_WARN, "DFF-46",
		      fmt("%s: %d из %d материалов чёрные (0,0,0) при флаге MODULATEMATERIALCOLOR — prelit × 0 = %s", gi.c_str(), blackMats, (int)g.mats.size(),
		          blackMats == (int)g.mats.size() ? "вся геометрия чёрная" : "эти меши чёрные (если не задумано)"));
	for(size_t i = 0; i < g.mats.size(); i++){
		const DMat &m = g.mats[i];
		unsigned a = (m.color >> 24) & 0xFF;
		if(a != 255 && p.mapModel)
			p.add(SEV_INFO, "DFF-46", fmt("%s: материал %d с alpha %u ≠ 255 — меш уходит в альфа-проход (сортировка)", gi.c_str(), (int)i, a));
		if(m.textured){
			if(m.tex.empty()) p.add(SEV_ERROR, "DFF-19", fmt("%s: материал %d textured=1 с пустым именем — texture = NULL", gi.c_str(), (int)i));
			else if(m.tex.size() > 31) p.add(SEV_ERROR, "DFF-19", fmt("%s: имя текстуры «%s» длиннее 31 символа — в TXD имена обрезаются до 31, совпадения не будет", gi.c_str(), m.tex.c_str()));
		}
		if(m.uvAnims.size() > 8) p.fatal("DFF-54", fmt("%s: материал %d: %d UV-анимаций (максимум 8)", gi.c_str(), (int)i, (int)m.uvAnims.size()));
	}
}

static void parseAtomic(P &p, const Chunk &ac, DAtomic &a)
{
	Dff &d = *p.d;
	Chunk st;
	if(!findChunk(p.b, 0x1, st, ac.end)){ p.fatal("DFF-02", "ATOMIC без STRUCT"); return; }
	if(st.size > 16) p.fatal("DFF-04", fmt("STRUCT атомика длиной %u > 16 — RwStreamRead в 16-байтовый локальный буфер затирает адрес возврата → краш", st.size));
	a.frame = p.b.i32();
	a.geom = p.b.i32();
	a.flags = p.b.i32();
	p.b.seek(st.end);
	int ai = (int)d.atomics.size();
	if(!d.frames.empty()){
		if(a.frame < 0 || a.frame >= (int)d.frames.size())
			p.fatal("DFF-05", fmt("атомик %d: frameIndex %d вне 0..%d — атомик привязывается к мусорному фрейму", ai, a.frame, (int)d.frames.size() - 1));
	}else
		p.fatal("DFF-05", fmt("атомик %d при пустом списке фреймов — GetFrameNodeName(NULL) → краш", ai));
	if(!d.geoms.empty()){
		if(a.geom < 0 || a.geom >= (int)d.geoms.size())
			p.fatal("DFF-07", fmt("атомик %d: geometryIndex %d вне 0..%d — RpGeometryAddRef по мусору", ai, a.geom, (int)d.geoms.size() - 1));
	}else{
		// inline geometry
		Chunk gcc;
		if(!findChunk(p.b, 0xF, gcc, ac.end)){ p.fatal("DFF-03", fmt("атомик %d: список геометрий пуст и нет встроенной GEOMETRY", ai)); return; }
		DGeom g;
		parseGeometry(p, gcc, g);
		d.geoms.push_back(g);
		a.geom = (int)d.geoms.size() - 1;
		p.b.seek(gcc.end);
	}
	walkExtension(p, ac.end, fmt("атомик %d", ai).c_str(), [&](const Chunk &c){
		Buf s(p.data->data() + c.start, c.end - c.start);
		if(c.type == 0x253F2F3){
			a.hasPipeline = true;
			a.pipelineLen = (int)(c.end - c.start);
			a.pipeline = s.u32();
			if(a.pipelineLen != 4)
				p.fatal("DFF-08", fmt("атомик %d: плагин Pipeline Set длиной %d вместо 4 — PipelineStreamRead пишет %s", ai, a.pipelineLen, a.pipelineLen > 4 ? "за пределы плагина (порча кучи)" : "неполный id"));
		}else if(c.type == 0x120){
			a.hasMatfx = true;
			a.matfx = s.u32();
		}
	});
}

static void parseDff(P &p)
{
	Dff &d = *p.d;
	Buf &b = p.b;
	Chunk first;
	size_t at = 0;
	if(!readChunk(b, first)){ p.fatal("DFF-01", "файл короче 12 байт"); return; }
	b.seek(at);
	if(first.type == 0x2B){
		readChunk(b, first);
		d.uvDictFirst = true;
		parseUvDict(p, first);
	}
	Chunk clump;
	if(!findChunk(b, 0x10, clump, b.n)){
		p.fatal("DFF-01", "не найден чанк CLUMP (0x10) — не DFF или мусор");
		return;
	}
	d.version = clump.version;
	if(!p.ctx->gd->rwVersionOk(clump.version)){
		p.fatal("DFF-01", fmt("версия RW 0x%X (libid 0x%08X) вне %s — RwErrorSet, RpClumpStreamRead вернёт NULL → модель никогда не загрузится", clump.version, clump.libid, p.ctx->gd->rwRange()),
		        p.ctx->gd->isSA() ? "SA: сохраняй с libid 0x1803FFFF. Файлы III/VC (0x31000/0x33002) в SA не работают."
		        : p.ctx->gd->isVC() ? "VC (RW 3.4.0.3) читает файлы III и VC; файлы SA (0x36003) нужно пересохранить с libid 0x0C02FFFF."
		        : "III (RW 3.3.0.2) читает только 3.1–3.3; файлы VC/SA нужно пересохранить с libid 0x0800FFFF.");
	}
	if(clump.truncated) p.fatal("DFF-01", fmt("CLUMP объявляет %u байт, файл короче", clump.size));
	Chunk st;
	if(!findChunk(b, 0x1, st, clump.end)){ p.fatal("DFF-02", "CLUMP без STRUCT"); return; }
	d.numAtomicsHdr = b.i32();
	if(st.size >= 12){ d.numLights = b.i32(); d.numCameras = b.i32(); }
	b.seek(st.end);
	Chunk fl;
	if(!findChunk(b, 0xE, fl, clump.end)){ p.fatal("DFF-02", "нет FRAMELIST"); return; }
	parseFrameList(p, fl);
	b.seek(fl.end);
	Chunk gl;
	if(!findChunk(b, 0x1A, gl, clump.end)){ p.fatal("DFF-02", "нет GEOMETRYLIST"); return; }
	Chunk gs;
	if(!findChunk(b, 0x1, gs, gl.end)){ p.fatal("DFF-02", "GEOMETRYLIST без STRUCT"); return; }
	int numGeoms = b.i32();
	b.seek(gs.end);
	if(numGeoms < 0 || numGeoms > 100000){ p.fatal("DFF-03", fmt("numGeometries = %d", numGeoms)); return; }
	for(int i = 0; i < numGeoms; i++){
		Chunk gcc;
		if(!findChunk(b, 0xF, gcc, gl.end)){ p.fatal("DFF-03", fmt("GEOMETRYLIST объявляет %d геометрий, найдено %d → LOAD-FAIL", numGeoms, i)); return; }
		DGeom g;
		parseGeometry(p, gcc, g);
		d.geoms.push_back(g);
		if(d.fatal && !p.b.ok) return;
		b.seek(gcc.end);
	}
	b.seek(gl.end);
	if(d.numAtomicsHdr < 0 || d.numAtomicsHdr > 100000){ p.fatal("DFF-03", fmt("numAtomics = %d", d.numAtomicsHdr)); return; }
	for(int i = 0; i < d.numAtomicsHdr; i++){
		Chunk ac;
		if(!findChunk(b, 0x14, ac, clump.end)){ p.fatal("DFF-03", fmt("CLUMP объявляет %d атомиков, найдено %d → LOAD-FAIL", d.numAtomicsHdr, i)); return; }
		DAtomic a;
		parseAtomic(p, ac, a);
		d.atomics.push_back(a);
		b.seek(ac.end);
	}
	// lights / cameras: skip by finding their chunks
	for(int i = 0; i < d.numLights; i++){ Chunk c; if(!findChunk(b, 0x12, c, clump.end)) break; b.seek(c.end); }
	for(int i = 0; i < d.numCameras; i++){ Chunk c; if(!findChunk(b, 0x5, c, clump.end)) break; b.seek(c.end); }
	// clump extension: collision plugin (vehicles)
	// RpClumpStreamRead does not use the CLUMP size: III-era files (0x310) keep the extension past it
	walkExtension(p, b.n, "CLUMP", [&](const Chunk &c){
		if(c.type == 0x253F2FA){ d.hasCollision = true; d.colStart = c.start; d.colEnd = c.end; }
	});
	// anything after the clump?
	if(b.pos < clump.end) b.seek(clump.end);
	Chunk tail;
	while(b.pos + 12 <= b.n && readChunk(b, tail)){
		if(tail.type == 0x2B){
			d.hasUvDict = true;
			p.add(SEV_ERROR, "DFF-50", "словарь UV-анимаций (0x2B) стоит ПОСЛЕ CLUMP — ConvertBufferToObject смотрит только первый чанк файла, словарь не читается, все UV-анимации станут статичными");
			parseUvDict(p, tail);
		}else if(tail.type == 0x10){
			if(p.ctx->gd->isIII() && p.pedModel)
				p.add(SEV_INFO, "DFF-03", "второй CLUMP — низкодетальная модель педа (III: CFileLoader::LoadClumpFile → SetLowDetailClump)");
			else
				p.add(SEV_INFO, "DFF-03", "в файле несколько CLUMP — читаются только для мультикламповых катсценных объектов");
		}
		if(tail.truncated || tail.end <= tail.start) break;
		b.seek(tail.end);
	}
}

// -------------------------------------------------- ped skin rules (C-nn) ---

static const int ALWAYS_USED_BONES[] = { 0,1,2,3,4,5,21,22,23,24,31,32,33,34,41,42,43,51,52,53 };
static const int CONDITIONAL_BONES[] = { 6,7,8,25,26,35,36,44,54,201,301,302 };
// VC (reVC RpAnimBlendClumpFillFrameArraySkin): every CPed fills m_pFrames[PED_MID..PED_NECK] through
// RpHAnimIDGetIndex(ConvertPedNode2BoneTag(i)) — a missing tag gives index -1 → &frames[-1]
static const int VC_ALWAYS_USED_BONES[] = { 3,5,32,22,34,24,41,51,43,53,52,42,33,23,31,21,4 };
static const int VC_CONDITIONAL_BONES[] = { 0,1,2 };	// root / pelvis / spine (PedIK GetBoneMatrix(BONE_spine))

static void checkSkinRules(P &p, Dff &d)
{
	bool isPed = p.pedModel;
	auto ped = [&](Severity s, const char *code, const std::string &msg){ p.ctx->add(s, CAT_PED, code, *p.where, -1, p.obj, msg, "", p.id); if(s == SEV_FATAL) d.fatal = true; };
	std::vector<int> skinned;
	for(size_t i = 0; i < d.geoms.size(); i++) if(d.geoms[i].hasSkin) skinned.push_back((int)i);
	if(skinned.empty()) return;
	// C21: HAnim version
	for(size_t i = 0; i < d.frames.size(); i++)
		if(d.frames[i].hasHAnim && d.frames[i].hanimVersion != 0x100)
			ped(SEV_FATAL, "PED-21", fmt("HAnim «%s»: версия 0x%X вместо 0x100 — RpClumpStreamRead вернёт NULL", d.frames[i].name.c_str(), d.frames[i].hanimVersion));
	// C01/C22/C26: node table
	std::vector<int> tables;
	for(size_t i = 0; i < d.frames.size(); i++) if(d.frames[i].hasHAnim && !d.frames[i].nodes.empty()) tables.push_back((int)i);
	int numNodes = -1;
	if(tables.empty())
		ped(SEV_FATAL, "PED-01", "нет таблицы узлов HAnim (numNodes > 0) ни на одном фрейме — движок пишет по нулевому указателю при загрузке");
	else{
		if(tables.size() > 1) ped(SEV_WARN, "PED-26", fmt("таблица узлов HAnim на %d фреймах — используется первая, остальные утекут", (int)tables.size()));
		DFrame &tf = d.frames[(size_t)tables[0]];
		numNodes = (int)tf.nodes.size();
		if(tf.hanimFlags & 2) ped(SEV_FATAL, "PED-03", fmt("HAnim «%s»: флаг 0x2 (без матриц) — краш при загрузке", tf.name.c_str()));
		if(tf.hanimKeySize < 28) ped(SEV_FATAL, "PED-04", fmt("HAnim «%s»: размер ключа %d < 28 — переполнение кучи при каждом спавне", tf.name.c_str(), tf.hanimKeySize));
		if(numNodes > 256) ped(SEV_FATAL, "PED-15", fmt("HAnim: %d узлов > 256 — переполнение буфера матриц", numNodes));
		if(tf.parent < 0) ped(SEV_FATAL, "PED-16", fmt("таблица узлов лежит на корневом фрейме «%s» — пед отрисуется в начале координат; таблицу несёт первая кость", tf.name.c_str()));
		std::map<int,int> seen;
		for(int i = 0; i < numNodes; i++){
			int id = tf.nodes[(size_t)i].id;
			if(seen.count(id)) ped(SEV_WARN, "PED-18", fmt("id кости %d повторяется (узлы %d и %d) — коллизия и анимация крутят разные копии", id, seen[id], i));
			else seen[id] = i;
		}
		for(int i = 0; i < numNodes; i++) if(tf.nodes[(size_t)i].index != i){ ped(SEV_WARN, "PED-19", fmt("узел %d (id %d) несёт индекс %d — порядок узлов не совпадает с порядком матриц скина", i, tf.nodes[(size_t)i].id, tf.nodes[(size_t)i].index)); break; }
		int depth = 0;
		for(int i = 0; i < numNodes; i++){
			int kind = tf.nodes[(size_t)i].flags & 3;
			if(kind == 2){ depth++; if(depth > 31){ ped(SEV_FATAL, "PED-17", fmt("глубина push %d на узле %d > 31 — стек костей затрёт адрес возврата", depth, i)); break; } }
			else if(kind == 1){ depth--; if(depth < 0 && i != numNodes - 1){ ped(SEV_FATAL, "PED-17", fmt("узел %d (id %d) делает pop из пустого стека, а за ним ещё есть узлы — мусорная родительская матрица", i, tf.nodes[(size_t)i].id)); break; } }
		}
		std::set<int> ids;
		for(int i = 0; i < numNodes; i++) ids.insert(tf.nodes[(size_t)i].id);
		std::string missing, cond;
		bool vc = p.ctx->gd->isVC();
		const int *always = vc ? VC_ALWAYS_USED_BONES : ALWAYS_USED_BONES; size_t nAlways = vc ? sizeof(VC_ALWAYS_USED_BONES)/sizeof(int) : sizeof(ALWAYS_USED_BONES)/sizeof(int);
		const int *condT = vc ? VC_CONDITIONAL_BONES : CONDITIONAL_BONES; size_t nCond = vc ? sizeof(VC_CONDITIONAL_BONES)/sizeof(int) : sizeof(CONDITIONAL_BONES)/sizeof(int);
		for(size_t k = 0; k < nAlways; k++) if(!ids.count(always[k])) missing += (missing.empty() ? "" : ", ") + fmt("%d", always[k]);
		for(size_t k = 0; k < nCond; k++) if(!ids.count(condT[k])) cond += (cond.empty() ? "" : ", ") + fmt("%d", condT[k]);
		if(isPed && !missing.empty()) ped(SEV_FATAL, "PED-20", vc ? fmt("нет костей с id %s — CPed заполняет m_pFrames по этим тегам (RpHAnimIDGetIndex даёт −1 → указатель перед массивом)", missing.c_str())
		                                                       : fmt("нет костей с id %s — движок обращается к ним на каждом педе (хит-сферы, тени, m_apBones) и пишет мимо массива матриц", missing.c_str()));
		if(isPed && !cond.empty()) ped(SEV_WARN, "PED-20", vc ? fmt("нет костей с id %s — root/pelvis/spine (PedIK берёт spine по id)", cond.c_str())
                                                             : fmt("нет костей с id %s — используются по условию (дождь, открытая машина, байк, пальцы, лицо)", cond.c_str()));
	}
	// C02: the engine's first atomic = last in file, must be skinned
	if(d.atomics.empty()) ped(SEV_FATAL, "PED-02", "в клампе нет атомиков — CreateHitColModelSkinned обратится к NULL-иерархии");
	else if(isPed){
		int gl = d.atomics.back().geom;
		if(gl < 0 || gl >= (int)d.geoms.size() || !d.geoms[(size_t)gl].hasSkin)
			ped(SEV_FATAL, "PED-02", fmt("последний атомик файла (геометрия %d) без скина — движок берёт его как первый, иерархия не назначится → краш", gl));
	}
	int lastGi = d.atomics.empty() ? -1 : d.atomics.back().geom;
	for(size_t si = 0; si < skinned.size(); si++){
		int gi = skinned[si];
		DGeom &g = d.geoms[(size_t)gi];
		DSkin &sk = g.skin;
		if(!sk.ok) continue;
		int nb = sk.numBones;
		if(nb < 1) ped(SEV_FATAL, "PED-07", fmt("геометрия %d: skin.numBones = 0 — таблица костей нулевого размера, порча кучи при спавне", gi));
		else if(nb > 64) ped(SEV_FATAL, "PED-05", fmt("геометрия %d: %d костей > 64 — переполнение стека при спавне", gi, nb));
		if(numNodes >= 0 && nb != numNodes) ped(SEV_FATAL, "PED-06", fmt("геометрия %d: костей в скине %d, а узлов в HAnim %d — палитра и ключи читаются мимо массивов", gi, nb, numNodes));
		if(sk.maxWeights < 1 || sk.maxWeights > 4) ped(SEV_FATAL, "PED-11", fmt("геометрия %d: maxWeights = %d — допустимо 1..4", gi, sk.maxWeights));
		if(sk.numUsed < 1) ped(SEV_FATAL, "PED-11", fmt("геометрия %d: список используемых костей пуст — палитра не загрузится", gi));
		std::set<int> referenced, oob;
		int zeroVerts = 0, badSum = 0, negative = 0, nan = 0;
		for(int v = 0; v < g.numVerts; v++){
			float total = 0;
			for(int k = 0; k < 4; k++){
				float w = sk.weights[(size_t)v * 4 + k];
				int bi = sk.indices[(size_t)v * 4 + k];
				if(!(w == w)){ nan++; continue; }
				if(w < 0) negative++;
				if(w != 0){ referenced.insert(bi); if(bi >= nb) oob.insert(bi); }
				total += w;
			}
			if(total == 0) zeroVerts++;
			else if(fabsf(total - 1.0f) > 1e-3f) badSum++;
		}
		if(zeroVerts && gi == lastGi) ped(SEV_WARN, "PED-08", fmt("геометрия %d: %d вершин без единого веса — деление 1/0 при нормализации, вершины NaN", gi, zeroVerts));
		if(badSum) ped(SEV_WARN, "PED-09", fmt("геометрия %d: у %d вершин сумма весов ≠ 1", gi, badSum));
		if(negative) ped(SEV_WARN, "PED-09", fmt("геометрия %d: %d отрицательных весов", gi, negative));
		if(nan) ped(SEV_WARN, "PED-09", fmt("геометрия %d: %d весов NaN/inf", gi, nan));
		if(!oob.empty()) ped(SEV_WARN, "PED-10", fmt("геометрия %d: индексы костей ≥ numBones (%d) — такие вершины прилипнут к кости 0", gi, nb));
		if(!sk.used.empty()){
			std::string miss;
			for(auto it = referenced.begin(); it != referenced.end(); ++it)
				if(std::find(sk.used.begin(), sk.used.end(), (uint8_t)*it) == sk.used.end()) miss += (miss.empty() ? "" : ", ") + fmt("%d", *it);
			if(!miss.empty()) ped(SEV_WARN, "PED-30", fmt("геометрия %d: кости %s имеют веса, но не входят в список используемых — их матрица останется от предыдущей модели", gi, miss.c_str()));
		}
		for(int bi = 0; bi < nb; bi++){
			if(!referenced.count(bi)) continue;	// unused bones may carry zero matrices (vanilla does)
			const float *m = &sk.mats[(size_t)bi * 16];
			bool finite = true;
			for(int k = 0; k < 16; k++) if((k & 3) != 3 && (!(m[k] == m[k]) || fabsf(m[k]) > 1e30f)) finite = false;
			if(!finite){ ped(SEV_WARN, "PED-25", fmt("геометрия %d: матрица кости %d содержит NaN/inf", gi, bi)); continue; }
			float det = m[0]*(m[5]*m[10]-m[6]*m[9]) - m[1]*(m[4]*m[10]-m[6]*m[8]) + m[2]*(m[4]*m[9]-m[5]*m[8]);
			if(fabsf(det) < 1e-9f) ped(SEV_WARN, "PED-25", fmt("геометрия %d: матрица кости %d вырождена (det ≈ 0) — мусорные смещения костей", gi, bi));
		}
	}
}

// ------------------------------------------------------- model rules ----

void CheckOneDff(Context &ctx, const std::string &where, const std::vector<uint8_t> &data, const ObjDef *def, int txdSlot)
{
	GameData &gd = *ctx.gd;
	bool sa = gd.isSA();
	ctx.rep->countFile(CAT_DFF);
	Dff d;
	P p;
	p.ctx = &ctx;
	p.where = &where;
	p.obj = def ? def->name : stem(where);
	p.id = def ? def->id : -1;
	p.entry = def ? def->dffEntry : -1;
	p.mapModel = def == nullptr || def->type == OT_OBJS || def->type == OT_TOBJ;
	p.pedModel = def && def->type == OT_PEDS;
	p.d = &d;
	p.b = Buf(data.data(), data.size());
	p.data = &data;
	parseDff(p);
	ObjDef *mdef = def && def->id >= 0 && gd.objById.count(def->id) ? &gd.objs[(size_t)gd.objById[def->id]] : nullptr;
	if(mdef){ mdef->dffParsed = true; mdef->dffFailed = d.fatal; }
	if(d.fatal && d.atomics.empty()) return;

	bool atomicModel = def == nullptr || def->type == OT_OBJS || def->type == OT_TOBJ;
	bool clumpModel = def && (def->type == OT_ANIM || def->type == OT_HIER || def->type == OT_WEAP || def->type == OT_CARS || def->type == OT_PEDS);
	bool damageable = def && (def->flags & 0x1000) && (def->type == OT_OBJS || def->type == OT_TOBJ);

	// bounding radius for COL cross reference: max over atomics
	float bestR = -1; float bc[3] = {0,0,0};
	for(size_t i = 0; i < d.atomics.size(); i++){
		int gi = d.atomics[i].geom;
		if(gi < 0 || gi >= (int)d.geoms.size()) continue;
		const DGeom &g = d.geoms[(size_t)gi];
		if(g.sphere[3] > bestR){ bestR = g.sphere[3]; bc[0] = g.sphere[0]; bc[1] = g.sphere[1]; bc[2] = g.sphere[2]; }
	}
	if(mdef){
		mdef->dffRadius = bestR; memcpy(mdef->dffCenter, bc, sizeof(bc));
		mdef->dffTex.clear(); mdef->dffPrelit = false; mdef->dffNight = false;
		std::set<std::string> seenTex;
		for(size_t g = 0; g < d.geoms.size(); g++){
			if(d.geoms[g].prelit) mdef->dffPrelit = true;
			if(d.geoms[g].hasNight) mdef->dffNight = true;
			for(size_t m = 0; m < d.geoms[g].mats.size(); m++){ const DMat &mt = d.geoms[g].mats[m]; if(mt.textured && !mt.tex.empty() && d.geoms[g].matUsed((int)m) && seenTex.insert(lower(mt.tex)).second) mdef->dffTex.push_back(lower(mt.tex)); }
		}
	}

	// atomics vs model type
	if(atomicModel && sa){
		int plain = 0, dam = 0;
		for(size_t i = 0; i < d.atomics.size(); i++){
			const DAtomic &a = d.atomics[i];
			std::string fname = (a.frame >= 0 && a.frame < (int)d.frames.size()) ? d.frames[(size_t)a.frame].name : "";
			std::string fl = lower(fname);
			bool isDam = fl.size() >= 4 && fl.compare(fl.size() - 4, 4, "_dam") == 0;
			if(isDam) dam++; else plain++;
			if(isDam && !damageable)
				p.fatal("DFF-28", fmt("атомик «%s» с суффиксом _dam, а модель в IDE без флага 0x1000 — SetDamagedAtomic(this=NULL) → краш", fname.c_str()), "Добавь флаг 0x1000 в IDE или переименуй фрейм.");
			if(a.frame >= 0 && a.frame < (int)d.frames.size() && !d.frames[(size_t)a.frame].identity)
				p.add(SEV_INFO, "DFF-29", fmt("атомик «%s»: трансформация фрейма будет отброшена (objs-модель получает новый единичный фрейм)", fname.c_str()));
		}
		if(d.atomics.empty())
			p.fatal("DFF-03", "0 атомиков в objs/tobj-модели — SetAtomic не вызывается, LoadAtomicFile вернёт 0 → LOAD-FAIL");
		if(plain > 1)
			p.add(SEV_ERROR, "DFF-27", fmt("%d обычных атомиков в objs/tobj-модели — SetAtomic хранит один, отрисуется только ПЕРВЫЙ в файле, остальные (и их 2dfx) потеряны", plain), "objs-модель = ровно один атомик (+ один _dam для разрушаемых).");
		if(damageable && dam == 0)
			p.add(SEV_ERROR, "DFF-28", "модель с флагом 0x1000 (разрушаемая) без атомика _dam — повреждённая версия невидима");
		if(dam > 1)
			p.add(SEV_ERROR, "DFF-28", fmt("%d атомиков _dam — используется один", dam));
	}
	if(clumpModel && sa){
		if(d.atomics.empty())
			p.fatal("DFF-40", "кламп-модель (anim/hier) без атомиков — CEntity::CreateRwObject: GetFirstAtomic → NULL → краш при первом размещении");
		for(size_t i = 0; i < d.atomics.size(); i++)
			if(!(d.atomics[i].flags & 4))
				p.add(SEV_WARN, "DFF-41", fmt("атомик %d без флага rpATOMICRENDER (4) — RpClumpRender его пропустит (невидим)", (int)i));
		bool anyUv = false;
		for(size_t g = 0; g < d.geoms.size(); g++) for(size_t m = 0; m < d.geoms[g].mats.size(); m++) if(!d.geoms[g].mats[m].uvAnims.empty()) anyUv = true;
		if(anyUv || d.hasUvDict)
			p.add(SEV_WARN, "DFF-52", "UV-анимации в кламп-модели (anim/hier) — словарь читается только для атомик-моделей, PreRender их не обновляет");
		if(def->type == OT_ANIM){
			int fx = 0, atomicsWithFx = 0;
			for(size_t i = 0; i < d.atomics.size(); i++){
				int gi = d.atomics[i].geom;
				if(gi >= 0 && gi < (int)d.geoms.size() && d.geoms[(size_t)gi].num2dfx){ fx += d.geoms[(size_t)gi].num2dfx; atomicsWithFx++; }
			}
			if(atomicsWithFx > 1)
				p.add(SEV_ERROR, "DFF-37", fmt("2dfx на %d атомиках кламп-модели — учитываются только эффекты последнего в файле атомика с эффектами", atomicsWithFx));
		}
	}
	// pipeline ids
	for(size_t i = 0; i < d.atomics.size(); i++){
		const DAtomic &a = d.atomics[i];
		if(a.hasPipeline && a.pipelineLen == 4 && sa){
			if(a.pipeline != 0x53F2009C && a.pipeline != 0x53F20098 && a.pipeline != 0x53F2009A && a.pipeline != 0)
				p.add(SEV_ERROR, "DFF-08", fmt("атомик %d: Pipeline Set 0x%08X неизвестен — стандартный RW-пайплайн с динамическим освещением вместо building-шейдера", (int)i, a.pipeline), "0x53F2009C building, 0x53F20098 building-DN, 0x53F2009A car.");
			else if(a.pipeline == 0x53F2009A && atomicModel)
				p.add(SEV_INFO, "DFF-08", fmt("атомик %d: car-пайплайн 0x53F2009A на объекте карты (тюнинг-детали так и делают)", (int)i));
		}
		int gi = a.geom;
		if(gi < 0 || gi >= (int)d.geoms.size()) continue;
		const DGeom &g = d.geoms[(size_t)gi];
		// DFF-53: UV anim needs the atomic MatFX flag
		bool anyUv = false;
		for(size_t m = 0; m < g.mats.size(); m++) if(!g.mats[m].uvAnims.empty()) anyUv = true;
		if(anyUv && atomicModel && sa && (!a.hasMatfx || a.matfx == 0))
			p.add(SEV_ERROR, "DFF-53", fmt("атомик %d: UV-анимации в материалах, но у атомика нет плагина MatFX (0x120) с ненулевым флагом — RpMatFXAtomicQueryEffects = 0, анимации заморожены", (int)i));
		if(g.hasNight && !g.nightHasColours && g.prelit)
			p.add(SEV_INFO, "DFF-44", fmt("геометрия %d: prelit без ночных цветов — статичный vertex colour (не реагирует на день/ночь)", gi));
	}
	// 2dfx totals
	int total2dfx = 0;
	for(size_t g = 0; g < d.geoms.size(); g++) total2dfx += d.geoms[g].num2dfx;
	if(total2dfx > 255) p.add(SEV_ERROR, "DFF-33", fmt("%d 2dfx-эффектов — счётчик модели байтовый, эффекты выше 255 перепутаются", total2dfx));
	// UV anim names vs dictionary
	if(sa && atomicModel){
		for(size_t g = 0; g < d.geoms.size(); g++) for(size_t m = 0; m < d.geoms[g].mats.size(); m++){
			const DMat &mt = d.geoms[g].mats[m];
			for(size_t k = 0; k < mt.uvAnims.size(); k++){
				if(!d.hasUvDict) { p.add(SEV_WARN, "DFF-51", fmt("материал ссылается на UV-анимацию «%s», но в файле нет словаря — подставится статичная identity-анимация", mt.uvAnims[k].c_str())); }
				else if(!d.uvDictNames.count(lower(mt.uvAnims[k]))) p.add(SEV_INFO, "DFF-51", fmt("UV-анимация «%s» отсутствует в словаре — статичная identity-анимация (в ванили встречается)", mt.uvAnims[k].c_str()));
				if(mt.matfx != 5 && mt.matfx != -1) p.add(SEV_WARN, "DFF-53", fmt("материал с UV-анимацией имеет MatFX effect %d вместо 5 (UV transform)", mt.matfx));
			}
		}
	}
	// textures vs TXD chain
	if(ctx.opt.texXref && txdSlot >= 0 && !gd.txdTextures.empty()){
		std::set<std::string> reported;
		if(getenv("GTACHECK_TRACE") && p.obj == getenv("GTACHECK_TRACE")){
			const TxdSlot &ts = gd.txdSlots[(size_t)txdSlot];
			auto it = gd.txdTextures.find(ts.name);
			fprintf(stderr, "TRACE %s: txd slot %d name=%s parent=%d parsed=%d names=%d\n", p.obj.c_str(), txdSlot, ts.name.c_str(), ts.parent, it != gd.txdTextures.end(), it != gd.txdTextures.end() ? (int)it->second.names.size() : -1);
			if(it != gd.txdTextures.end()) for(size_t k = 0; k < it->second.names.size(); k++) fprintf(stderr, "   tex[%d]=%s\n", (int)k, it->second.names[k].c_str());
			for(size_t g = 0; g < d.geoms.size(); g++) for(size_t m = 0; m < d.geoms[g].mats.size(); m++) fprintf(stderr, "   mat g%d m%d textured=%d tex=%s\n", (int)g, (int)m, d.geoms[g].mats[m].textured, d.geoms[g].mats[m].tex.c_str());
		}
		for(size_t g = 0; g < d.geoms.size(); g++) for(size_t m = 0; m < d.geoms[g].mats.size(); m++){
			const DMat &mt = d.geoms[g].mats[m];
			if(!mt.textured || mt.tex.empty()) continue;
			std::string tl = lower(mt.tex);
			if(reported.count(tl)) continue;
			// a material no triangle / mesh references (an exporter leftover): the texture is looked up at load
			// but never drawn — not a DFF-19, just a note
			bool used = false;
			for(size_t g2 = 0; g2 < d.geoms.size() && !used; g2++) for(size_t m2 = 0; m2 < d.geoms[g2].mats.size() && !used; m2++)
				if(d.geoms[g2].mats[m2].textured && lower(d.geoms[g2].mats[m2].tex) == tl && d.geoms[g2].matUsed((int)m2)) used = true;
			if(!used){	// a leftover material: reported whether or not the TXD has its texture (the TXD side is QLT-12)
				reported.insert(tl);
				bool known = gd.txdTextures.find(gd.txdSlots[(size_t)txdSlot].name) != gd.txdTextures.end();
				if(known && gd.txdChainHasTexture(txdSlot, tl))
					ctx.add(SEV_INFO, CAT_DFF, "DFF-19u", where, -1, p.obj, fmt("материал с текстурой «%s» не используется ни одним полигоном — текстура из TXD «%s» грузится зря", mt.tex.c_str(), gd.txdSlots[(size_t)txdSlot].shown.c_str()), "Лишний материал экспорта; убери его при переэкспорте, а текстуру — из TXD, если её не рисует никто другой (QLT-12).", p.id);
				else if(known)
					ctx.add(SEV_INFO, CAT_DFF, "DFF-19u", where, -1, p.obj, fmt("материал с текстурой «%s» не используется ни одним полигоном, самой текстуры в TXD «%s» тоже нет — на вид не влияет", mt.tex.c_str(), gd.txdSlots[(size_t)txdSlot].shown.c_str()), "Лишний материал экспорта; можно убрать при переэкспорте.", p.id);
				continue;
			}
			reported.insert(tl);
			if(gd.txdChainHasTexture(txdSlot, tl)) continue;
			// unknown TXD content (not parsed) → skip
			if(gd.txdTextures.find(gd.txdSlots[(size_t)txdSlot].name) == gd.txdTextures.end()) continue;
			bool inGeneric = false;
			auto gen = gd.txdTextures.find("generic");
			if(gen != gd.txdTextures.end() && std::find(gen->second.names.begin(), gen->second.names.end(), tl) != gen->second.names.end()) inGeneric = true;
			ctx.add(inGeneric ? SEV_WARN : SEV_ERROR, CAT_XREF, "DFF-19", where, -1, p.obj,
			        fmt("текстура «%s» отсутствует в TXD «%s»%s — материал отрисуется без текстуры (белый/серый)", mt.tex.c_str(), gd.txdSlots[(size_t)txdSlot].shown.c_str(), inGeneric ? " (есть только в generic.txd)" : ""),
			        "RwTextureRead ищет по цепочке TXD → родители (регистр не важен); не найдено → material->texture = NULL.", p.id);
		}
	}
	// ped skin rules
	bool anySkin = false;
	for(size_t g = 0; g < d.geoms.size(); g++) if(d.geoms[g].hasSkin) anySkin = true;
	if(mdef) mdef->dffSkinned = anySkin;
	if(anySkin) checkSkinRules(p, d);
	if(def && def->type == OT_PEDS && !anySkin && !gd.isIII())
		p.add(SEV_FATAL, "PED-02", "ped-модель без Skin-плагина — CPedModelInfo::SetClump ждёт скин на первом атомике");
	if(def && def->type == OT_PEDS && gd.isIII()){
		// III PC peds are frame hierarchies: CPedModelInfo::SetFrameIds(m_pPedIds) looks the limbs up by name
		// (faststricmp), RpAnimBlendClumpFillFrameArray puts them into m_pFrames[PED_*]; PedIK / weapons / hit
		// spheres dereference m_pFrames[node] without a check
		static const char *III_PED_FRAMES[] = { "Smid", "Shead", "Supperarml", "Supperarmr", "SLhand", "SRhand", "Supperlegl", "Supperlegr", "Sfootl", "Sfootr", "Slowerlegr" };
		std::string missing;
		for(size_t k = 0; k < sizeof(III_PED_FRAMES)/sizeof(*III_PED_FRAMES); k++){
			bool found = false;
			for(size_t i = 0; i < d.frames.size() && !found; i++) if(_stricmp(d.frames[i].name.c_str(), III_PED_FRAMES[k]) == 0) found = true;
			if(!found) missing += (missing.empty() ? "" : ", ") + std::string(III_PED_FRAMES[k]);
		}
		if(!missing.empty())
			p.ctx->add(SEV_FATAL, CAT_PED, "PED-02", where, -1, p.obj, fmt("нет фреймов %s — m_pFrames[PED_*] остаются пустыми, PedIK/оружие/хит-сферы разыменуют NULL", missing.c_str()),
			           "III (без скина): CPedModelInfo::SetFrameIds ищет фреймы по имени без учёта регистра: Smid Shead Supperarml Supperarmr SLhand SRhand Supperlegl Supperlegr Sfootl Sfootr Slowerlegr.", p.id);
		if(anySkin)
			p.add(SEV_WARN, "PED-02", "ped-модель со Skin-плагином — gta3.exe (PC) рисует педов иерархией фреймов, скин не используется");
	}
	// vehicle rules (VEH-nn, check_veh.cpp): frame names vs the per-type dummy table + embedded collision
	if(def && def->type == OT_CARS && sa){
		VehDff v;
		v.frameNames.reserve(d.frames.size());
		v.frameParent.reserve(d.frames.size());
		v.frameHasAtomic.assign(d.frames.size(), false);
		for(size_t i = 0; i < d.frames.size(); i++){ v.frameNames.push_back(d.frames[i].name); v.frameParent.push_back(d.frames[i].parent); }
		for(size_t i = 0; i < d.atomics.size(); i++)
			if(d.atomics[i].frame >= 0 && d.atomics[i].frame < (int)d.frames.size()) v.frameHasAtomic[(size_t)d.atomics[i].frame] = true;
		v.hasCollision = d.hasCollision && d.colEnd > d.colStart;
		if(v.hasCollision){
			v.colSize = d.colEnd - d.colStart;
			if(v.colSize >= 8){ Buf cb(data.data() + d.colStart, 8); v.colFourcc = cb.u32(); v.colSizeField = cb.u32(); }
			else if(v.colSize >= 4){ Buf cb(data.data() + d.colStart, 4); v.colFourcc = cb.u32(); }
		}
		CheckVehicleDff(ctx, where, *def, v);
	}
	// embedded collision (vehicles)
	if(d.hasCollision && d.colEnd > d.colStart){
		// the plugin body is a complete COL3 entry (fourcc + size + name + payload); anything else is the
		// old III/VC-style blob (vanilla rccam.dff) which the SA loader ignores
		std::vector<uint8_t> col(data.begin() + (long)d.colStart, data.begin() + (long)d.colEnd);
		if(col.size() >= 4 && col[0] == 'C' && col[1] == 'O' && col[2] == 'L')
			CheckOneCol(ctx, where + " (COLLISION)", col, -1, false);
		else
			p.add(SEV_INFO, "COL-04", "плагин коллизии в DFF не начинается с COL2/COL3 — старый формат (III/VC), в SA не используется");
	}
	if(mdef) mdef->dffFailed = d.fatal;
}

// ------------------------------------------------------------- driver ----

void CheckDffs(Context &ctx)
{
	GameData &gd = *ctx.gd;
	ctx.prog->set("DFF");
	// per entry (models referenced by IDE first; unreferenced DFFs are reported as orphans)
	std::vector<int> order;
	std::set<int> usedEntries;
	for(size_t i = 0; i < gd.objs.size(); i++) if(gd.objs[i].dffEntry >= 0 && !usedEntries.count(gd.objs[i].dffEntry)){ order.push_back((int)i); usedEntries.insert(gd.objs[i].dffEntry); }
	int total = (int)order.size();
	for(int k = 0; k < total; k++){
		if(ctx.cancelled()) return;
		ObjDef &o = gd.objs[(size_t)order[(size_t)k]];
		Entry &e = gd.entries[(size_t)o.dffEntry];
		bool quiet = ctx.opt.skipVanillaImgs && e.img >= 0 && gd.archives[e.img].vanilla;
		if(quiet) continue;
		std::string where = (e.img >= 0 ? basename(gd.archives[e.img].logical) + "/" : "") + e.name;
		ctx.prog->step(e.name.c_str(), k, total);
		std::vector<uint8_t> data;
		std::string err;
		if(!gd.readEntry(o.dffEntry, data, &err)){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-24", where, e.dirIndex, e.name, "не удалось прочитать запись: " + err, "", o.id);
			continue;
		}
		CheckOneDff(ctx, where, data, &o, o.txdSlot);
	}
	// DFF entries no IDE references
	int orphans = 0;
	for(size_t i = 0; i < gd.entries.size(); i++){
		Entry &e = gd.entries[i];
		if(e.kind != EK_DFF || usedEntries.count((int)i) || !gd.isWinner((int)i)) continue;
		if(ctx.opt.skipVanillaImgs && e.img >= 0 && gd.archives[e.img].vanilla) continue;
		orphans++;
		if(orphans <= 200)
			ctx.add(SEV_INFO, CAT_XREF, "DFF-72", (e.img >= 0 ? basename(gd.archives[e.img].logical) + "/" : "") + e.name, e.dirIndex, e.base,
			        "DFF в архиве, но ни один IDE его не объявляет — мёртвый груз (или IDE не в gta.dat)", "");
	}
	if(orphans > 200) ctx.add(SEV_INFO, CAT_XREF, "DFF-72", "IMG", -1, "", fmt("…и ещё %d DFF без IDE-объявления", orphans - 200), "");
}

} // namespace gc
