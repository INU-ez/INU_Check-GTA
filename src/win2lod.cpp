// gtacheck — окна → LOD: the glowing windows of a model (the faces whose night vertex colours are bright) are cut out
// and grafted onto its LOD, snapped to the LOD's walls. A port of the user's Blender script windows_to_lod.py
// (21.09.2026) onto raw DFF bytes: one GEOMETRY chunk of the LOD is rebuilt in place, everything else stays bit-exact.
// docs/WINDOWS_TO_LOD.md explains the steps.
#include "gtacheck.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <map>

namespace gc {

// ------------------------------------------------------------------------------------------------- small vector maths ---
static V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float length(V3 a) { return sqrtf(dot(a, a)); }
static V3 normalize(V3 a) { float l = length(a); return l > 1e-12f ? a * (1.0f / l) : v3(0, 0, 0); }
static V3 mulPoint(const float *m, V3 p)	// row-major 4×4 (m[row*4+col]), point
{ return v3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]); }
static V3 mulDir(const float *m, V3 p)
{ return v3(m[0] * p.x + m[1] * p.y + m[2] * p.z, m[4] * p.x + m[5] * p.y + m[6] * p.z, m[8] * p.x + m[9] * p.y + m[10] * p.z); }

static void put32(std::vector<uint8_t> &v, size_t at, uint32_t x) { v[at] = (uint8_t)x; v[at + 1] = (uint8_t)(x >> 8); v[at + 2] = (uint8_t)(x >> 16); v[at + 3] = (uint8_t)(x >> 24); }
static uint32_t get32(const std::vector<uint8_t> &v, size_t at) { return (uint32_t)v[at] | ((uint32_t)v[at + 1] << 8) | ((uint32_t)v[at + 2] << 16) | ((uint32_t)v[at + 3] << 24); }
static void add32(std::vector<uint8_t> &v, uint32_t x) { size_t n = v.size(); v.resize(n + 4); put32(v, n, x); }
static void add16(std::vector<uint8_t> &v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void addF(std::vector<uint8_t> &v, float f) { uint32_t x; memcpy(&x, &f, 4); add32(v, x); }
static void addHdr(std::vector<uint8_t> &v, uint32_t type, uint32_t size, uint32_t libid) { add32(v, type); add32(v, size); add32(v, libid); }

// ------------------------------------------------------------------------------------------------- a DFF geometry ---
enum { GF_TRISTRIP = 1, GF_POSITIONS = 2, GF_TEXTURED = 4, GF_PRELIT = 8, GF_NORMALS = 0x10, GF_LIGHT = 0x20, GF_MODULATE = 0x40, GF_TEXTURED2 = 0x80, GF_NATIVE = 0x01000000 };
static const uint32_t ID_GEOMETRY = 0x0F, ID_MATLIST = 0x08, ID_MATERIAL = 0x07, ID_TEXTURE = 0x06, ID_STRING = 0x02, ID_STRUCT = 0x01, ID_EXTENSION = 0x03,
	ID_GEOMLIST = 0x1A, ID_CLUMP = 0x10, ID_BINMESH = 0x50E, ID_NIGHT = 0x253F2F9, ID_BREAKABLE = 0x253F2FD, ID_EXTRANORMALS = 0x253F2F2;


static std::string readStr(Buf &b, size_t limit)
{
	Chunk s; if(!findChunk(b, ID_STRING, s, limit)) return "";
	std::string r; for(size_t i = s.start; i < s.end; i++){ char c = (char)b.p[i]; if(!c) break; r += c; }
	b.seek(s.end); return r;
}

static bool parseMaterial(Buf &b, const Chunk &mc, GMat &m, std::string &err)
{
	m.raw.assign(b.p + mc.start - 12, b.p + mc.end);
	Chunk st; if(!findChunk(b, ID_STRUCT, st, mc.end) || st.size < 16){ err = T("MATERIAL без STRUCT"); return false; }
	b.i32(); m.color = b.u32(); b.i32(); m.textured = b.i32() != 0;
	b.seek(st.end);
	if(m.textured){
		Chunk tc; if(!findChunk(b, ID_TEXTURE, tc, mc.end)){ err = T("материал с textured=1 без чанка TEXTURE"); return false; }
		Chunk ts; if(findChunk(b, ID_STRUCT, ts, tc.end)) b.seek(ts.end);
		m.tex = readStr(b, tc.end); m.mask = readStr(b, tc.end);
	}
	b.seek(mc.end);
	return true;
}

static bool parseGeometry(Buf &b, const Chunk &gc_, uint32_t version, GGeom &g, std::string &err)
{
	g.hdrAt = gc_.start - 12; g.endAt = gc_.end; g.libid = gc_.libid;
	Chunk st; if(!findChunk(b, ID_STRUCT, st, gc_.end) || st.size < 16){ err = T("GEOMETRY без STRUCT"); return false; }
	g.flags = b.u32(); int numTris = b.i32(), numVerts = b.i32(), numMorph = b.i32();
	if(numTris < 0 || numVerts < 0 || numVerts > 0xFFFF || numTris > 4000000){ err = T("мусорные счётчики геометрии"); return false; }
	if(numMorph != 1){ err = T("геометрия с морф-целями — не поддерживается"); return false; }
	g.native = (g.flags & GF_NATIVE) != 0;
	if(g.native){ err = T("геометрия в платформенном (native) формате — не поддерживается"); return false; }
	g.prelit = (g.flags & GF_PRELIT) != 0; g.normals = (g.flags & GF_NORMALS) != 0;
	g.numTexSets = (int)((g.flags >> 16) & 0xFF);
	if(g.numTexSets == 0) g.numTexSets = (g.flags & GF_TEXTURED2) ? 2 : (g.flags & GF_TEXTURED) ? 1 : 0;
	if(version < 0x34000 && st.size >= 28){ g.hasSurf = true; g.surf[0] = b.f32(); g.surf[1] = b.f32(); g.surf[2] = b.f32(); }
	if(g.prelit){ g.day.resize((size_t)numVerts); for(int i = 0; i < numVerts; i++) g.day[(size_t)i] = b.u32(); }
	g.uv.resize((size_t)g.numTexSets);
	for(int s = 0; s < g.numTexSets; s++){ g.uv[(size_t)s].resize((size_t)numVerts * 2); for(int i = 0; i < numVerts * 2; i++) g.uv[(size_t)s][(size_t)i] = b.f32(); }
	g.tri.resize((size_t)numTris);
	for(int i = 0; i < numTris; i++){ uint32_t a = b.u32(), c = b.u32(); GTri &t = g.tri[(size_t)i]; t.v[0] = a >> 16; t.v[1] = a & 0xFFFF; t.v[2] = c >> 16; t.mat = c & 0xFFFF; }
	for(int i = 0; i < 4; i++) g.sphere[i] = b.f32();
	int hasV = b.i32(), hasN = b.i32();
	if(hasV){ g.pos.resize((size_t)numVerts); for(int i = 0; i < numVerts; i++){ float x = b.f32(), y = b.f32(), z = b.f32(); g.pos[(size_t)i] = v3(x, y, z); } }
	else g.pos.assign((size_t)numVerts, v3(0, 0, 0));
	if(hasN){ g.nrm.resize((size_t)numVerts); for(int i = 0; i < numVerts; i++){ float x = b.f32(), y = b.f32(), z = b.f32(); g.nrm[(size_t)i] = v3(x, y, z); } g.normals = true; }
	else g.normals = false;
	if(!b.ok){ err = T("STRUCT геометрии обрезан"); return false; }
	for(size_t i = 0; i < g.tri.size(); i++) for(int k = 0; k < 3; k++) if(g.tri[i].v[k] >= (uint32_t)numVerts){ err = T("индекс вершины вне диапазона"); return false; }
	b.seek(st.end);
	// materials: STRUCT (count, index per slot: -1 = a MATERIAL chunk follows, ≥ 0 = the same material as that slot)
	Chunk ml; if(!findChunk(b, ID_MATLIST, ml, gc_.end)){ err = T("нет MATERIALLIST"); return false; }
	Chunk ms; if(!findChunk(b, ID_STRUCT, ms, ml.end)){ err = T("MATERIALLIST без STRUCT"); return false; }
	int numMat = b.i32(); if(numMat < 0 || numMat > 10000){ err = T("мусорное число материалов"); return false; }
	std::vector<int> idx((size_t)numMat); for(int i = 0; i < numMat; i++) idx[(size_t)i] = b.i32();
	b.seek(ms.end);
	g.mats.resize((size_t)numMat);
	for(int i = 0; i < numMat; i++){
		if(idx[(size_t)i] >= 0 && idx[(size_t)i] < i){ g.mats[(size_t)i] = g.mats[(size_t)idx[(size_t)i]]; continue; }
		Chunk mc; if(!findChunk(b, ID_MATERIAL, mc, ml.end)){ err = T("не хватает чанков MATERIAL"); return false; }
		if(!parseMaterial(b, mc, g.mats[(size_t)i], err)) return false;
	}
	for(size_t i = 0; i < g.tri.size(); i++) if(g.tri[i].mat >= (uint32_t)numMat){ err = T("индекс материала вне диапазона"); return false; }
	b.seek(ml.end);
	// extension: BinMesh and the night colours are rebuilt, the rest is kept as it is
	Chunk ext; if(!findChunk(b, ID_EXTENSION, ext, gc_.end)){ err = T("GEOMETRY без EXTENSION"); return false; }
	size_t q = ext.start;
	while(q + 12 <= ext.end){
		b.seek(q); Chunk c; if(!readChunk(b, c) || c.truncated || c.end > ext.end){ err = T("плагин выходит за EXTENSION"); return false; }
		if(c.type == ID_BINMESH){ g.binFlags = (int)b.u32(); }
		else if(c.type == ID_NIGHT){
			uint32_t has = b.u32();
			if(has && c.size >= 4 + (size_t)numVerts * 4){ g.night = true; g.nightCol.resize((size_t)numVerts); for(int i = 0; i < numVerts; i++) g.nightCol[(size_t)i] = b.u32(); }
		}
		else if((c.type == ID_BREAKABLE && c.size > 4) || c.type == ID_EXTRANORMALS) g.extDropped.push_back(rwChunkName(c.type));	// a 4-byte breakable chunk is the «none» marker every SA model carries: kept as it is
		else g.extKeep.push_back(std::vector<uint8_t>(b.p + c.start - 12, b.p + c.end));
		q = c.end;
	}
	if(!g.night && g.prelit) g.nightCol.clear();
	return true;
}

bool ParseDffGeoms(const std::vector<uint8_t> &data, GDff &d, std::string &err)
{
	Buf b(data.data(), data.size());
	Chunk clump; if(!findChunk(b, ID_CLUMP, clump, data.size())){ err = T("нет чанка CLUMP"); return false; }
	if(clump.truncated){ err = T("CLUMP обрезан"); return false; }
	d.clumpHdr = clump.start - 12; d.version = clump.version;
	// RW < 3.3 files size the CLUMP short of its extension (III): scan to the end of the file for the lists
	size_t limit = clump.version < 0x33000 ? data.size() : clump.end;
	Chunk gl; if(!findChunk(b, ID_GEOMLIST, gl, limit)){ err = T("нет GEOMETRYLIST"); return false; }
	d.listHdr = gl.start - 12;
	Chunk st; if(!findChunk(b, ID_STRUCT, st, gl.end)){ err = T("GEOMETRYLIST без STRUCT"); return false; }
	int n = b.i32(); if(n < 0 || n > 10000){ err = T("мусорное число геометрий"); return false; }
	b.seek(st.end);
	for(int i = 0; i < n; i++){
		Chunk gc_; if(!findChunk(b, ID_GEOMETRY, gc_, gl.end)){ err = T("не хватает чанков GEOMETRY"); return false; }
		if(gc_.truncated){ err = T("GEOMETRY обрезан"); return false; }
		GGeom g; if(!parseGeometry(b, gc_, clump.version, g, err)){ err = fmt(T("геометрия %d: %s"), i, err.c_str()); return false; }
		d.geoms.push_back(g);
		b.seek(gc_.end);
	}
	return true;
}

void WriteDffGeometry(const GGeom &g, uint32_t version, std::vector<uint8_t> &out)
{
	const uint32_t libid = g.libid;
	int nv = g.numVerts(), nt = (int)g.tri.size();
	std::vector<uint8_t> st;
	uint32_t flags = (g.flags & ~(uint32_t)(GF_TRISTRIP | GF_PRELIT | GF_NORMALS | GF_TEXTURED | GF_TEXTURED2)) & 0xFF00FFFF;
	if(g.prelit) flags |= GF_PRELIT; if(g.normals) flags |= GF_NORMALS;
	if(g.numTexSets == 1) flags |= GF_TEXTURED; else if(g.numTexSets >= 2) flags |= GF_TEXTURED2;
	if(g.flags & 0xFF0000) flags |= ((uint32_t)g.numTexSets & 0xFF) << 16;	// the exporter wrote the count in the high byte: keep that convention
	add32(st, flags); add32(st, (uint32_t)nt); add32(st, (uint32_t)nv); add32(st, 1);
	if(version < 0x34000){ addF(st, g.surf[0]); addF(st, g.surf[1]); addF(st, g.surf[2]); }
	if(g.prelit) for(int i = 0; i < nv; i++) add32(st, g.day[(size_t)i]);
	for(int s = 0; s < g.numTexSets; s++) for(int i = 0; i < nv * 2; i++) addF(st, g.uv[(size_t)s][(size_t)i]);
	for(int i = 0; i < nt; i++){ const GTri &t = g.tri[(size_t)i]; add32(st, (t.v[0] << 16) | (t.v[1] & 0xFFFF)); add32(st, (t.v[2] << 16) | (t.mat & 0xFFFF)); }
	for(int i = 0; i < 4; i++) addF(st, g.sphere[i]);
	add32(st, 1); add32(st, g.normals ? 1 : 0);
	for(int i = 0; i < nv; i++){ addF(st, g.pos[(size_t)i].x); addF(st, g.pos[(size_t)i].y); addF(st, g.pos[(size_t)i].z); }
	if(g.normals) for(int i = 0; i < nv; i++){ addF(st, g.nrm[(size_t)i].x); addF(st, g.nrm[(size_t)i].y); addF(st, g.nrm[(size_t)i].z); }
	// material list: every slot its own chunk (references collapsed)
	std::vector<uint8_t> ml, mls;
	add32(mls, (uint32_t)g.mats.size()); for(size_t i = 0; i < g.mats.size(); i++) add32(mls, 0xFFFFFFFFu);
	addHdr(ml, ID_STRUCT, (uint32_t)mls.size(), libid); ml.insert(ml.end(), mls.begin(), mls.end());
	for(size_t i = 0; i < g.mats.size(); i++) ml.insert(ml.end(), g.mats[i].raw.begin(), g.mats[i].raw.end());
	// extension: BinMesh (trilist, one mesh per material that has triangles), night colours, the kept plugins
	std::vector<uint8_t> ext;
	{
		std::vector<std::vector<uint32_t> > perMat(g.mats.size());
		for(size_t i = 0; i < g.tri.size(); i++){ const GTri &t = g.tri[i]; std::vector<uint32_t> &m = perMat[t.mat]; m.push_back(t.v[0]); m.push_back(t.v[1]); m.push_back(t.v[2]); }
		std::vector<uint8_t> bm; uint32_t meshes = 0, total = 0;
		for(size_t m = 0; m < perMat.size(); m++) if(!perMat[m].empty()){ meshes++; total += (uint32_t)perMat[m].size(); }
		add32(bm, 0); add32(bm, meshes); add32(bm, total);
		for(size_t m = 0; m < perMat.size(); m++){ if(perMat[m].empty()) continue; add32(bm, (uint32_t)perMat[m].size()); add32(bm, (uint32_t)m); for(size_t k = 0; k < perMat[m].size(); k++) add32(bm, perMat[m][k]); }
		addHdr(ext, ID_BINMESH, (uint32_t)bm.size(), libid); ext.insert(ext.end(), bm.begin(), bm.end());
	}
	if(g.night){ addHdr(ext, ID_NIGHT, 4 + (uint32_t)nv * 4, libid); add32(ext, 1); for(int i = 0; i < nv; i++) add32(ext, g.nightCol[(size_t)i]); }
	for(size_t i = 0; i < g.extKeep.size(); i++) ext.insert(ext.end(), g.extKeep[i].begin(), g.extKeep[i].end());
	std::vector<uint8_t> body;
	addHdr(body, ID_STRUCT, (uint32_t)st.size(), libid); body.insert(body.end(), st.begin(), st.end());
	addHdr(body, ID_MATLIST, (uint32_t)ml.size(), libid); body.insert(body.end(), ml.begin(), ml.end());
	addHdr(body, ID_EXTENSION, (uint32_t)ext.size(), libid); body.insert(body.end(), ext.begin(), ext.end());
	addHdr(out, ID_GEOMETRY, (uint32_t)body.size(), libid); out.insert(out.end(), body.begin(), body.end());
}

// ------------------------------------------------------------------------------------------------- the window mesh ---
struct WVert { V3 pos, nrm; float u, v; uint32_t day, night; };
struct WTri { uint32_t v[3]; int mat; V3 n; float area; };	// mat: index into WMesh::mats
struct WMat { GMat m; std::string key; };
struct WMesh { std::vector<WVert> vert; std::vector<WTri> tri; std::vector<WMat> mats; };

static float lum(uint32_t rgba) { return 0.299f * (float)(rgba & 0xFF) + 0.587f * (float)((rgba >> 8) & 0xFF) + 0.114f * (float)((rgba >> 16) & 0xFF); }
static std::string matKey(const GMat &m) { return m.textured && !m.tex.empty() ? lower(m.tex) : fmt("#%08X", m.color); }	// the script compares Blender material names = texture names
static V3 faceNormal(const std::vector<V3> &p, const uint32_t *v, float *area)
{ V3 n = cross(p[v[1]] - p[v[0]], p[v[2]] - p[v[0]]); float l = length(n); if(area) *area = 0.5f * l; return l > 1e-12f ? n * (1.0f / l) : v3(0, 0, 1); }

// the faces whose night colour is bright at every corner, grown over the edge onto the other half of a window quad
// (same material, same plane, ≥ growMinMatch bright corners) — find_window_faces() of the script
static void findWindowFaces(const GGeom &g, const W2lOptions &o, std::vector<int> &faces, int &hits)
{
	faces.clear(); hits = 0;
	if(!g.night) return;
	std::vector<uint8_t> bright((size_t)g.numVerts());
	for(int i = 0; i < g.numVerts(); i++) bright[(size_t)i] = lum(g.nightCol[(size_t)i]) >= o.minBrightness;
	std::vector<int> matches(g.tri.size()); std::vector<char> sel(g.tri.size(), 0); std::vector<int> stack;
	for(size_t t = 0; t < g.tri.size(); t++){ int c = 0; for(int k = 0; k < 3; k++) c += bright[g.tri[t].v[k]]; matches[t] = c; hits += c; if(c == 3){ sel[t] = 1; stack.push_back((int)t); } }
	if(stack.empty()) return;
	if(o.grow){
		std::map<std::pair<uint32_t, uint32_t>, std::vector<int> > edges;
		for(size_t t = 0; t < g.tri.size(); t++) for(int k = 0; k < 3; k++){ uint32_t a = g.tri[t].v[k], b = g.tri[t].v[(k + 1) % 3]; if(a > b) std::swap(a, b); edges[std::make_pair(a, b)].push_back((int)t); }
		std::vector<V3> fn(g.tri.size()); for(size_t t = 0; t < g.tri.size(); t++) fn[t] = faceNormal(g.pos, g.tri[t].v, nullptr);
		while(!stack.empty()){
			int t = stack.back(); stack.pop_back();
			for(int k = 0; k < 3; k++){
				uint32_t a = g.tri[(size_t)t].v[k], b = g.tri[(size_t)t].v[(k + 1) % 3]; if(a > b) std::swap(a, b);
				const std::vector<int> &nb = edges[std::make_pair(a, b)];
				for(size_t i = 0; i < nb.size(); i++){
					int u = nb[i]; if(sel[(size_t)u]) continue;
					if(g.tri[(size_t)u].mat != g.tri[(size_t)t].mat) continue;
					if(dot(fn[(size_t)u], fn[(size_t)t]) < o.growDot) continue;
					if(matches[(size_t)u] < o.growMinMatch) continue;
					sel[(size_t)u] = 1; stack.push_back(u);
				}
			}
		}
	}
	for(size_t t = 0; t < g.tri.size(); t++) if(sel[t]) faces.push_back((int)t);
}

// ------------------------------------------------------------------------------------------------- the LOD surface ---
struct Surf { std::vector<V3> p; std::vector<uint32_t> tri; std::vector<V3> n; V3 lo, hi; };	// all LOD triangles, raw coordinates (the game ignores the frames of objs)
static void closestOnTri(V3 p, V3 a, V3 b, V3 c, V3 &out)	// Ericson, Real-Time Collision Detection 5.1.5
{
	V3 ab = b - a, ac = c - a, ap = p - a;
	float d1 = dot(ab, ap), d2 = dot(ac, ap);
	if(d1 <= 0 && d2 <= 0){ out = a; return; }
	V3 bp = p - b; float d3 = dot(ab, bp), d4 = dot(ac, bp);
	if(d3 >= 0 && d4 <= d3){ out = b; return; }
	float vc = d1 * d4 - d3 * d2;
	if(vc <= 0 && d1 >= 0 && d3 <= 0){ float v = d1 / (d1 - d3); out = a + ab * v; return; }
	V3 cp = p - c; float d5 = dot(ab, cp), d6 = dot(ac, cp);
	if(d6 >= 0 && d5 <= d6){ out = c; return; }
	float vb = d5 * d2 - d1 * d6;
	if(vb <= 0 && d2 >= 0 && d6 <= 0){ float w = d2 / (d2 - d6); out = a + ac * w; return; }
	float va = d3 * d6 - d5 * d4;
	if(va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0){ float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); out = b + (c - b) * w; return; }
	float denom = 1.0f / (va + vb + vc); float v = vb * denom, w = vc * denom;
	out = a + ab * v + ac * w;
}
static bool surfNearest(const Surf &s, V3 p, float maxDist, V3 &hit, V3 &n)
{
	float best = maxDist > 0 ? maxDist * maxDist : 1e30f; bool ok = false;
	for(size_t t = 0; t < s.n.size(); t++){
		V3 q; closestOnTri(p, s.p[s.tri[t * 3]], s.p[s.tri[t * 3 + 1]], s.p[s.tri[t * 3 + 2]], q);
		float d = dot(q - p, q - p); if(d < best){ best = d; hit = q; n = s.n[t]; ok = true; }
	}
	return ok;
}
static bool surfRay(const Surf &s, V3 o, V3 d, float maxDist, V3 &hit, V3 &n)	// two-sided Möller–Trumbore, nearest hit
{
	float best = maxDist; bool ok = false;
	for(size_t t = 0; t < s.n.size(); t++){
		V3 a = s.p[s.tri[t * 3]], e1 = s.p[s.tri[t * 3 + 1]] - a, e2 = s.p[s.tri[t * 3 + 2]] - a;
		V3 pv = cross(d, e2); float det = dot(e1, pv); if(fabsf(det) < 1e-9f) continue;
		float inv = 1.0f / det; V3 tv = o - a; float u = dot(tv, pv) * inv; if(u < 0 || u > 1) continue;
		V3 qv = cross(tv, e1); float v = dot(d, qv) * inv; if(v < 0 || u + v > 1) continue;
		float tt = dot(e2, qv) * inv; if(tt <= 0 || tt >= best) continue;
		best = tt; hit = o + d * tt; n = s.n[t]; ok = true;
	}
	return ok;
}

// ------------------------------------------------------------------------------------------------- islands ---
struct Group { std::vector<int> tris; std::vector<int> plane; bool flat; };	// plane: the glass = the largest coplanar part
static std::vector<int> uniqueVerts(const WMesh &w, const std::vector<int> &tris)
{
	std::vector<int> vs; for(size_t i = 0; i < tris.size(); i++) for(int k = 0; k < 3; k++) vs.push_back((int)w.tri[(size_t)tris[i]].v[k]);
	std::sort(vs.begin(), vs.end()); vs.erase(std::unique(vs.begin(), vs.end()), vs.end()); return vs;
}
static void planeOf(const WMesh &w, const std::vector<int> &tris, V3 &centroid, V3 &normal)
{
	std::vector<int> vs = uniqueVerts(w, tris);
	centroid = v3(0, 0, 0); for(size_t i = 0; i < vs.size(); i++) centroid = centroid + w.vert[(size_t)vs[i]].pos; if(!vs.empty()) centroid = centroid * (1.0f / (float)vs.size());
	normal = v3(0, 0, 0); for(size_t i = 0; i < tris.size(); i++) normal = normal + w.tri[(size_t)tris[i]].n * w.tri[(size_t)tris[i]].area;
	if(length(normal) < 1e-9f && !tris.empty()) normal = w.tri[(size_t)tris[0]].n;
	normal = normalize(normal);
}
// connected sets of triangles over shared vertices; coplanar: never step across a crease
static std::vector<std::vector<int> > walk(const WMesh &w, const std::vector<int> &tris, bool coplanar, float coplanarDot)
{
	std::map<uint32_t, std::vector<int> > byVert;
	for(size_t i = 0; i < tris.size(); i++) for(int k = 0; k < 3; k++) byVert[w.tri[(size_t)tris[i]].v[k]].push_back(tris[i]);
	std::map<int, char> seen; std::vector<std::vector<int> > groups;
	for(size_t i = 0; i < tris.size(); i++){
		if(seen.count(tris[i])) continue;
		std::vector<int> g, stack; stack.push_back(tris[i]); seen[tris[i]] = 1;
		while(!stack.empty()){
			int t = stack.back(); stack.pop_back(); g.push_back(t);
			for(int k = 0; k < 3; k++){
				const std::vector<int> &nb = byVert[w.tri[(size_t)t].v[k]];
				for(size_t j = 0; j < nb.size(); j++){
					int u = nb[j]; if(seen.count(u)) continue;
					if(coplanar && dot(w.tri[(size_t)t].n, w.tri[(size_t)u].n) < coplanarDot) continue;
					seen[u] = 1; stack.push_back(u);
				}
			}
		}
		groups.push_back(g);
	}
	return groups;
}
static void updateNormals(WMesh &w) { for(size_t t = 0; t < w.tri.size(); t++){ V3 n = cross(w.vert[w.tri[t].v[1]].pos - w.vert[w.tri[t].v[0]].pos, w.vert[w.tri[t].v[2]].pos - w.vert[w.tri[t].v[0]].pos); float l = length(n); w.tri[t].area = 0.5f * l; w.tri[t].n = l > 1e-12f ? n * (1.0f / l) : v3(0, 0, 1); } }

// ------------------------------------------------------------------------------------------------- the whole thing ---
bool WindowsToLod(const std::vector<uint8_t> &mainDff, const std::vector<uint8_t> &lodDff, const float *delta, const W2lOptions &o, W2lResult &r)
{
	r = W2lResult();
	GDff mainD, lodD;
	if(!ParseDffGeoms(mainDff, mainD, r.err)){ r.err = T("основная модель: ") + r.err; return false; }
	if(!ParseDffGeoms(lodDff, lodD, r.err)){ r.err = T("LOD: ") + r.err; return false; }
	if(lodD.geoms.empty()){ r.err = T("в LOD нет геометрии"); return false; }
	// 1. the windows of every geometry of the main model → one mesh, raw coordinates moved by `delta` (inv(M_lod)·M_main)
	WMesh w; int totalHits = 0; bool anyNight = false; std::map<std::string, int> matIndex;
	for(size_t gi = 0; gi < mainD.geoms.size(); gi++){
		const GGeom &g = mainD.geoms[gi];
		if(g.night) anyNight = true;
		std::vector<int> faces; int hits = 0; findWindowFaces(g, o, faces, hits); totalHits += hits;
		if(faces.empty()) continue;
		std::map<uint32_t, uint32_t> remap;
		for(size_t i = 0; i < faces.size(); i++){
			const GTri &t = g.tri[(size_t)faces[i]];
			WTri wt; wt.n = v3(0, 0, 1); wt.area = 0;
			for(int k = 0; k < 3; k++){
				std::map<uint32_t, uint32_t>::iterator it = remap.find(t.v[k]);
				if(it == remap.end()){
					WVert v; v.pos = g.pos[t.v[k]]; v.nrm = g.normals ? g.nrm[t.v[k]] : v3(0, 0, 0);
					v.u = g.numTexSets ? g.uv[0][t.v[k] * 2] : 0; v.v = g.numTexSets ? g.uv[0][t.v[k] * 2 + 1] : 0;
					v.day = g.prelit ? g.day[t.v[k]] : 0xFFFFFFFFu; v.night = g.nightCol[t.v[k]];
					if(delta){ v.pos = mulPoint(delta, v.pos); v.nrm = normalize(mulDir(delta, v.nrm)); }
					it = remap.insert(std::make_pair(t.v[k], (uint32_t)w.vert.size())).first; w.vert.push_back(v);
				}
				wt.v[k] = it->second;
			}
			std::string key = fmt("%d:%u", (int)gi, t.mat);
			std::map<std::string, int>::iterator mi = matIndex.find(key);
			if(mi == matIndex.end()){ WMat wm; wm.m = g.mats[t.mat]; wm.key = matKey(wm.m); mi = matIndex.insert(std::make_pair(key, (int)w.mats.size())).first; w.mats.push_back(wm); }
			wt.mat = mi->second; w.tri.push_back(wt);
		}
	}
	r.windowFaces = (int)w.tri.size();
	if(!anyNight){ r.err = T("у основной модели нет ночных цветов вершин (чанк 0x253F2F9) — окна искать негде"); return false; }
	if(w.tri.empty()){
		r.err = totalHits ? fmt(T("окон ярче %.0f не найдено: ярких углов %d, но ни одного треугольника целиком — опусти порог яркости"), o.minBrightness, totalHits)
		                  : fmt(T("окон ярче %.0f не найдено: ни одного яркого угла в ночных цветах"), o.minBrightness);
		return false;
	}
	updateNormals(w);
	// 2. the LOD surface and its largest geometry (the target)
	Surf s; s.lo = v3(1e30f, 1e30f, 1e30f); s.hi = v3(-1e30f, -1e30f, -1e30f);
	size_t target = 0;
	for(size_t gi = 0; gi < lodD.geoms.size(); gi++){
		const GGeom &g = lodD.geoms[gi];
		if(g.tri.size() > lodD.geoms[target].tri.size()) target = gi;
		uint32_t base = (uint32_t)s.p.size();
		for(size_t i = 0; i < g.pos.size(); i++){ V3 p = g.pos[i]; s.p.push_back(p); s.lo = v3(std::min(s.lo.x, p.x), std::min(s.lo.y, p.y), std::min(s.lo.z, p.z)); s.hi = v3(std::max(s.hi.x, p.x), std::max(s.hi.y, p.y), std::max(s.hi.z, p.z)); }
		for(size_t t = 0; t < g.tri.size(); t++){ for(int k = 0; k < 3; k++) s.tri.push_back(base + g.tri[t].v[k]); s.n.push_back(faceNormal(g.pos, g.tri[t].v, nullptr)); }
	}
	V3 lodCenter = (s.lo + s.hi) * 0.5f;
	r.lodTris = (int)s.n.size();
	// 3. groups (one window = one connected set, no crease check), the glass of each, flattening
	std::vector<int> all; for(size_t t = 0; t < w.tri.size(); t++) all.push_back((int)t);
	std::vector<std::vector<int> > groupsRaw = walk(w, all, false, o.islandDot);
	std::vector<Group> groups;
	for(size_t i = 0; i < groupsRaw.size(); i++){
		Group g; g.tris = groupsRaw[i];
		std::vector<std::vector<int> > subs = walk(w, g.tris, true, o.islandDot);
		g.flat = subs.size() == 1; size_t best = 0; float bestA = -1;
		for(size_t k = 0; k < subs.size(); k++){ float a = 0; for(size_t j = 0; j < subs[k].size(); j++) a += w.tri[(size_t)subs[k][j]].area; if(a > bestA){ bestA = a; best = k; } }
		g.plane = subs[best]; groups.push_back(g);
	}
	r.islands = (int)groups.size();
	if(o.flatten){
		for(size_t i = 0; i < groups.size(); i++){
			if(!groups[i].flat) continue;	// a window with a reveal is not flat by construction
			V3 c, n; planeOf(w, groups[i].plane, c, n);
			std::vector<int> vs = uniqueVerts(w, groups[i].plane);
			for(size_t k = 0; k < vs.size(); k++){ V3 &p = w.vert[(size_t)vs[k]].pos; p = p - n * dot(p - c, n); }
		}
		updateNormals(w);
	}
	std::vector<char> dropGroup(groups.size(), 0);
	if(o.snap && !s.n.empty()){
		// 4. a LOD modelled turned against its main model: try the four quarter turns about Z, keep a clear winner
		if(o.fixRotation){
			int step = std::max(1, (int)w.vert.size() / 200); std::vector<V3> sample;
			for(size_t i = 0; i < w.vert.size(); i += (size_t)step) sample.push_back(w.vert[i].pos);
			float dist[4]; bool have[4];
			for(int a = 0; a < 4; a++){
				float ang = 1.5707963f * (float)a, cs = cosf(ang), sn = sinf(ang); float total = 0; int count = 0;
				for(size_t i = 0; i < sample.size(); i++){
					V3 d = sample[i] - lodCenter; V3 m = lodCenter + v3(cs * d.x - sn * d.y, sn * d.x + cs * d.y, d.z);
					V3 h, n; if(surfNearest(s, m, 0, h, n)){ total += length(h - m); count++; }
				}
				have[a] = count > 0; dist[a] = count ? total / (float)count : 0;
			}
			int best = 0; for(int a = 1; a < 4; a++) if(have[a] && (!have[best] || dist[a] < dist[best])) best = a;
			r.log.push_back(fmt(T("доворот: среднее расстояние окон до LOD при 0° %.2f, 90° %.2f, 180° %.2f, 270° %.2f м"), dist[0], dist[1], dist[2], dist[3]));
			if(have[0] && best != 0 && dist[best] <= dist[0] * o.rotationGain){
				r.angle = best * 90; float ang = 1.5707963f * (float)best, cs = cosf(ang), sn = sinf(ang);
				for(size_t i = 0; i < w.vert.size(); i++){ V3 d = w.vert[i].pos - lodCenter; w.vert[i].pos = lodCenter + v3(cs * d.x - sn * d.y, sn * d.x + cs * d.y, d.z); V3 &nn = w.vert[i].nrm; nn = v3(cs * nn.x - sn * nn.y, sn * nn.x + cs * nn.y, nn.z); }
				updateNormals(w);
			}
		}
		// 5. windows a previous run already grafted: LOD faces with a window material that are bright at night
		Surf dup;
		if(o.skipExisting){
			for(size_t gi = 0; gi < lodD.geoms.size(); gi++){
				const GGeom &g = lodD.geoms[gi];
				std::vector<char> wanted(g.mats.size(), 0); bool any = false;
				for(size_t m = 0; m < g.mats.size(); m++){ std::string k = matKey(g.mats[m]); for(size_t j = 0; j < w.mats.size(); j++) if(w.mats[j].key == k){ wanted[m] = 1; any = true; } }
				if(!any) continue;
				std::vector<int> faces; int hits; findWindowFaces(g, o, faces, hits);
				uint32_t base = (uint32_t)dup.p.size(); bool used = false;
				for(size_t i = 0; i < faces.size(); i++){ const GTri &t = g.tri[(size_t)faces[i]]; if(!wanted[t.mat]) continue; for(int k = 0; k < 3; k++) dup.tri.push_back(base + t.v[k]); dup.n.push_back(faceNormal(g.pos, t.v, nullptr)); used = true; }
				if(used) dup.p.insert(dup.p.end(), g.pos.begin(), g.pos.end());
			}
		}
		// 6. every window onto the wall, moved along the wall's own normal (outward by construction)
		for(size_t i = 0; i < groups.size(); i++){
			V3 c, n; planeOf(w, groups[i].plane, c, n);
			if(!dup.n.empty()){ V3 h, hn; if(surfNearest(dup, c, o.existingDist, h, hn)){ r.dup++; dropGroup[i] = 1; continue; } }
			V3 hit, wn; bool ok = false; float bestD = 1e30f;
			for(int side = 0; side < 2; side++){ V3 h, hn; if(surfRay(s, c, side ? n * -1.0f : n, o.maxSnap, h, hn)){ float d = length(h - c); if(d < bestD){ bestD = d; hit = h; wn = hn; ok = true; } } }
			if(!ok && !surfNearest(s, c, o.maxSnap, hit, wn)){ r.noWall++; continue; }
			V3 out = normalize(wn); if(length(out) < 1e-6f){ r.noWall++; continue; }
			float shift = dot((hit + out * o.wallOffset) - c, out);
			if(fabsf(shift) > o.maxSnap){ r.tooFar++; continue; }
			std::vector<int> vs = uniqueVerts(w, groups[i].tris);
			for(size_t k = 0; k < vs.size(); k++) w.vert[(size_t)vs[k]].pos = w.vert[(size_t)vs[k]].pos + out * shift;
			r.snapped++;
		}
		updateNormals(w);
	}else if(o.snap) r.log.push_back(T("у LOD нет геометрии — посадка пропущена"));
	// 7. the windows that stay → appended to the LOD's largest geometry
	std::vector<char> keepTri(w.tri.size(), 1);
	for(size_t i = 0; i < groups.size(); i++) if(dropGroup[i]) for(size_t k = 0; k < groups[i].tris.size(); k++) keepTri[(size_t)groups[i].tris[k]] = 0;
	r.added = r.islands - r.dup;
	if(r.added <= 0){ r.log.push_back(T("все окна уже вживлены в LOD — файл не переписан")); r.changed = false; return true; }
	GGeom &tg = lodD.geoms[target];
	int oldVerts = tg.numVerts(), oldTris = (int)tg.tri.size(), oldMats = (int)tg.mats.size();
	if((size_t)oldVerts + w.vert.size() > 0xFFFF){ r.err = fmt(T("в геометрии LOD станет %d вершин — больше 65535"), oldVerts + (int)w.vert.size()); return false; }
	// materials: reuse an equal one of the LOD (texture + mask + colour), otherwise the main model's chunk as it is
	std::vector<int> matMap(w.mats.size(), -1); std::vector<char> matUsed(w.mats.size(), 0);
	for(size_t t = 0; t < w.tri.size(); t++) if(keepTri[t]) matUsed[(size_t)w.tri[t].mat] = 1;
	for(size_t m = 0; m < w.mats.size(); m++){
		if(!matUsed[m]) continue;	// the script drops the material slots no window polygon uses
		const GMat &wm = w.mats[m].m;
		for(size_t k = 0; k < tg.mats.size() && matMap[m] < 0; k++){ const GMat &lm = tg.mats[k]; if(lm.textured == wm.textured && lm.color == wm.color && ieq(lm.tex, wm.tex) && ieq(lm.mask, wm.mask)) matMap[m] = (int)k; }
		if(matMap[m] < 0){ matMap[m] = (int)tg.mats.size(); tg.mats.push_back(wm); }
		if(wm.textured && !wm.tex.empty()){ std::string tn = lower(wm.tex); if(std::find(r.texNeeded.begin(), r.texNeeded.end(), tn) == r.texNeeded.end()) r.texNeeded.push_back(tn); }	// reused or not, the LOD's TXD must have it
	}
	// vertex attributes the LOD lacks get a default for its own vertices: white day, black night (the script's vcol_ensure), zero UVs
	bool wantNormals = tg.normals;	// a LOD without normals stays without (the game lights prelit buildings without them)
	if(!tg.prelit){ tg.prelit = true; tg.day.assign((size_t)oldVerts, 0xFFFFFFFFu); r.log.push_back(T("у LOD не было prelit — его вершины получили белый день (в SA это пересвет ×2; запеки свет LOD'а отдельно)")); }
	if(!tg.night){ tg.night = true; tg.nightCol.assign((size_t)oldVerts, 0xFF000000u); }
	if(tg.numTexSets == 0){ tg.numTexSets = 1; tg.uv.resize(1); tg.uv[0].assign((size_t)oldVerts * 2, 0.0f); r.log.push_back(T("у LOD не было UV — добавлен набор 0 (нули у его вершин)")); }
	std::vector<int> vmap(w.vert.size(), -1);
	// vertex normals for a window of a model without them: the mean of its faces
	std::vector<V3> vnrm(w.vert.size(), v3(0, 0, 0));
	for(size_t t = 0; t < w.tri.size(); t++) for(int k = 0; k < 3; k++) vnrm[w.tri[t].v[k]] = vnrm[w.tri[t].v[k]] + w.tri[t].n * w.tri[t].area;
	for(size_t t = 0; t < w.tri.size(); t++){
		if(!keepTri[t]) continue;
		GTri nt; nt.mat = (uint32_t)matMap[(size_t)w.tri[t].mat];
		for(int k = 0; k < 3; k++){
			uint32_t v = w.tri[t].v[k];
			if(vmap[v] < 0){
				const WVert &wv = w.vert[v]; vmap[v] = tg.numVerts();
				tg.pos.push_back(wv.pos);
				if(wantNormals) tg.nrm.push_back(length(wv.nrm) > 1e-6f ? wv.nrm : normalize(vnrm[v]));
				for(int sIdx = 0; sIdx < tg.numTexSets; sIdx++){ tg.uv[(size_t)sIdx].push_back(wv.u); tg.uv[(size_t)sIdx].push_back(wv.v); }
				tg.day.push_back(wv.day); tg.nightCol.push_back(wv.night);
			}
			nt.v[k] = (uint32_t)vmap[v];
		}
		tg.tri.push_back(nt);
	}
	r.addedVerts = tg.numVerts() - oldVerts; r.addedTris = (int)tg.tri.size() - oldTris; r.addedMats = (int)tg.mats.size() - oldMats;
	// bounding sphere: the box centre and the farthest vertex
	V3 lo = v3(1e30f, 1e30f, 1e30f), hi = v3(-1e30f, -1e30f, -1e30f);
	for(size_t i = 0; i < tg.pos.size(); i++){ lo = v3(std::min(lo.x, tg.pos[i].x), std::min(lo.y, tg.pos[i].y), std::min(lo.z, tg.pos[i].z)); hi = v3(std::max(hi.x, tg.pos[i].x), std::max(hi.y, tg.pos[i].y), std::max(hi.z, tg.pos[i].z)); }
	V3 c = (lo + hi) * 0.5f; float rad = 0; for(size_t i = 0; i < tg.pos.size(); i++) rad = std::max(rad, length(tg.pos[i] - c));
	tg.sphere[0] = c.x; tg.sphere[1] = c.y; tg.sphere[2] = c.z; tg.sphere[3] = rad;
	for(size_t i = 0; i < tg.extDropped.size(); i++) r.log.push_back(fmt(T("плагин %s геометрии LOD — на каждую вершину, с новым числом вершин не переносится, удалён"), tg.extDropped[i].c_str()));
	if(tg.binFlags == 1) r.log.push_back(T("BinMesh LOD был tristrip — пересобран как trilist"));
	// 8. the GEOMETRY chunk back in place; GEOMETRYLIST and CLUMP grow by the difference
	std::vector<uint8_t> chunk; WriteDffGeometry(tg, lodD.version, chunk);
	std::vector<uint8_t> out; out.reserve(lodDff.size() + chunk.size());
	out.insert(out.end(), lodDff.begin(), lodDff.begin() + (long)tg.hdrAt);
	out.insert(out.end(), chunk.begin(), chunk.end());
	out.insert(out.end(), lodDff.begin() + (long)tg.endAt, lodDff.end());
	long d = (long)chunk.size() - (long)(tg.endAt - tg.hdrAt);
	put32(out, lodD.listHdr + 4, (uint32_t)((long)get32(lodDff, lodD.listHdr + 4) + d));
	put32(out, lodD.clumpHdr + 4, (uint32_t)((long)get32(lodDff, lodD.clumpHdr + 4) + d));
	r.lodOut.swap(out); r.changed = true;
	return true;
}

}	// namespace gc
