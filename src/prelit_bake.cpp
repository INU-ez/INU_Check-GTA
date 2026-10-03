#include <tuple>
// gtacheck — запекание prelit: the day and night vertex colours of one placed model, computed in the world (its own
// geometry, the neighbours within a radius, the sun by the timecycle, the 2dfx lamps at night) and written back into
// the DFF: the prelit block of every GEOMETRY struct and the night-colours chunk (0x253F2F9), everything else bit-exact.
// The lighting model and the calibration targets are in docs/PRELIT_PLAN.md: vanilla SA walls carry 30..115 by day
// (the pipeline doubles them) and 7..41 by night, so «open sky + sun» lands at ~120 and «deep shade» at ~30.
#include "gtacheck.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <tuple>

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
static float vmin(float a, float b) { return a < b ? a : b; }
static float vmax(float a, float b) { return a > b ? a : b; }

static uint32_t get32(const std::vector<uint8_t> &v, size_t at) { return (uint32_t)v[at] | ((uint32_t)v[at + 1] << 8) | ((uint32_t)v[at + 2] << 16) | ((uint32_t)v[at + 3] << 24); }
static void put32(std::vector<uint8_t> &v, size_t at, uint32_t x) { v[at] = (uint8_t)x; v[at + 1] = (uint8_t)(x >> 8); v[at + 2] = (uint8_t)(x >> 16); v[at + 3] = (uint8_t)(x >> 24); }
static const uint32_t ID_STRUCT = 0x01, ID_EXTENSION = 0x03, ID_MATLIST = 0x08, ID_NIGHT = 0x253F2F9, ID_2DFX = 0x253F2F8;
enum { GF_PRELIT = 8 };

// the placement matrix as worldBuild / instMatrix build it: SA takes a quaternion with |x|,|y| ≤ 0.05 as a pure heading
// (CFileLoader::LoadObjectInstance), anything else — and III/VC always — is the conjugate quaternion (librw makeRotation)
void InstMatrixRaw(const Inst &in, bool sa, float *m)
{
	float qx = in.rot[0], qy = in.rot[1], qz = in.rot[2], qw = in.rot[3];
	V3 right, up, at;
	if(sa && fabsf(qx) <= 0.05f && fabsf(qy) <= 0.05f && !((in.interior & 0x200) && qx != 0.0f && qy != 0.0f)){
		float ww = qw; if(ww < -1.0f) ww = -1.0f; if(ww > 1.0f) ww = 1.0f;
		float heading = acosf(ww) * (qz < 0.0f ? 2.0f : -2.0f), sn = sinf(heading), cs = cosf(heading);
		right = v3(cs, sn, 0); up = v3(-sn, cs, 0); at = v3(0, 0, 1);
	}else{
		float x = -qx, y = -qy, z = -qz, w = qw;	// conj(q)
		float xx = x * x, yy = y * y, zz = z * z, yz = y * z, zx = z * x, xy = x * y, wx = w * x, wy = w * y, wz = w * z;
		right = v3(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (zx - wy));
		up = v3(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx));
		at = v3(2.0f * (zx + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy));
	}
	// p' = right·x + up·y + at·z + pos  →  row-major rows are (right.x up.x at.x pos.x) …
	m[0] = right.x; m[1] = up.x; m[2] = at.x; m[3] = in.pos[0];
	m[4] = right.y; m[5] = up.y; m[6] = at.y; m[7] = in.pos[1];
	m[8] = right.z; m[9] = up.z; m[10] = at.z; m[11] = in.pos[2];
	m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
}

// ------------------------------------------------------------------------------------------------- the scene ---
// the 2dfx light entries of a geometry's extension chunk (the same layout the 3D map's plugin reads): position, colour, point-light range
static void parseLights(const std::vector<uint8_t> &chunk, const float *mat, std::vector<BakeLight> &out)
{
	if(chunk.size() < 16) return;
	size_t p = 12; uint32_t count = get32(chunk, p); p += 4;
	for(uint32_t i = 0; i < count && p + 20 <= chunk.size(); i++){
		float pos[3]; memcpy(pos, &chunk[p], 12); uint32_t type = get32(chunk, p + 12), size = get32(chunk, p + 16); p += 20;
		if(p + size > chunk.size()) break;
		if(type == 0 && size >= 76){	// light: RGBA, corona far clip, point light range, corona size, shadow size, …
			BakeLight l; V3 lp = v3(pos[0], pos[1], pos[2]); l.pos = mat ? mulPoint(mat, lp) : lp;
			l.rgb[0] = chunk[p] / 255.0f; l.rgb[1] = chunk[p + 1] / 255.0f; l.rgb[2] = chunk[p + 2] / 255.0f;
			memcpy(&l.range, &chunk[p + 8], 4);
			l.strength = 1.0f; l.when = 1; l.shadow = true; l.mode = 0; l.rig = false;
			if(l.range < 0.0f || l.range >= 1000.0f) l.range = 0.0f;	// corona-only lamps keep range 0: the bake gives them the default reach or skips them
			if((l.rgb[0] + l.rgb[1] + l.rgb[2]) > 0.0f) out.push_back(l);
		}
		p += size;
	}
}

bool BakeScene::addDff(const std::vector<uint8_t> &dff, const float *mat, bool withLights, std::string *err)
{
	GDff d; std::string e;
	if(!ParseDffGeoms(dff, d, e)){ if(err) *err = e; return false; }
	for(size_t gi = 0; gi < d.geoms.size(); gi++){
		const GGeom &g = d.geoms[gi];
		for(size_t t = 0; t < g.tri.size(); t++) for(int k = 0; k < 3; k++){ V3 p = g.pos[g.tri[t].v[k]]; tri.push_back(mat ? mulPoint(mat, p) : p); }
		if(withLights) for(size_t k = 0; k < g.extKeep.size(); k++) if(g.extKeep[k].size() >= 12 && get32(g.extKeep[k], 0) == ID_2DFX) parseLights(g.extKeep[k], mat, lights);
	}
	models++;
	return true;
}

// ------------------------------------------------------------------------------------------------- the lamp rig ---
bool BakeRigLights(const std::vector<uint8_t> &dff, const float *mat, const BakeRig &rig, std::vector<BakeLight> &out, std::string &err)
{
	out.clear();
	GDff d; if(!ParseDffGeoms(dff, d, err)) return false;
	int n = rig.sectors < 1 ? 1 : rig.sectors > 64 ? 64 : rig.sectors;
	std::vector<float> area((size_t)n, 0.0f); std::vector<V3> cen((size_t)n, v3(0, 0, 0)), nrm((size_t)n, v3(0, 0, 0));
	V3 lo = v3(1e30f, 1e30f, 1e30f), hi = v3(-1e30f, -1e30f, -1e30f); float total = 0;
	for(size_t gi = 0; gi < d.geoms.size(); gi++){
		const GGeom &g = d.geoms[gi];
		for(size_t i = 0; i < g.pos.size(); i++){ const V3 &p = g.pos[i]; lo = v3(vmin(lo.x, p.x), vmin(lo.y, p.y), vmin(lo.z, p.z)); hi = v3(vmax(hi.x, p.x), vmax(hi.y, p.y), vmax(hi.z, p.z)); }
		for(size_t t = 0; t < g.tri.size(); t++){
			const uint32_t *v = g.tri[t].v; V3 fn = cross(g.pos[v[1]] - g.pos[v[0]], g.pos[v[2]] - g.pos[v[0]]);
			float a = 0.5f * length(fn); if(a < 1e-6f) continue; fn = fn * (1.0f / (2.0f * a));
			if(fabsf(fn.z) >= 0.6f) continue;	// roofs and floors are not facades
			float az = atan2f(fn.y, fn.x); int k = (int)floorf((az + 3.14159265f) / 6.2831853f * (float)n); if(k < 0) k = 0; if(k >= n) k = n - 1;
			V3 c = (g.pos[v[0]] + g.pos[v[1]] + g.pos[v[2]]) * (1.0f / 3.0f);
			area[(size_t)k] += a; cen[(size_t)k] = cen[(size_t)k] + c * a; nrm[(size_t)k] = nrm[(size_t)k] + v3(fn.x, fn.y, 0) * a; total += a;
		}
	}
	if(lo.x > hi.x){ err = T("в DFF нет геометрии"); return false; }
	auto add = [&](V3 local, V3 aim){
		BakeLight l; l.rig = true; l.local = local; l.aim = aim; l.pos = mat ? mulPoint(mat, local) : local;
		l.rgb[0] = rig.rgb[0]; l.rgb[1] = rig.rgb[1]; l.rgb[2] = rig.rgb[2]; l.range = rig.range; l.strength = rig.strength; l.when = rig.when; l.shadow = rig.shadow; l.mode = rig.mode;
		out.push_back(l);
	};
	float z = lo.z + rig.height;
	if(total > 1e-6f){
		for(int k = 0; k < n; k++){
			if(area[(size_t)k] < total * 0.005f) continue;	// a sliver of wall in this direction: no lamp
			V3 c = cen[(size_t)k] * (1.0f / area[(size_t)k]), nn = normalize(nrm[(size_t)k]); if(length(nn) < 0.5f) continue;
			V3 p = c + nn * rig.dist; p.z = z; c.z = z;
			add(p, c);
		}
	}
	if(out.empty()){	// no facades (terrain, a flat sign): a ring around the box
		V3 c = (lo + hi) * 0.5f; float r = 0.5f * sqrtf((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y)) + rig.dist;
		for(int k = 0; k < n; k++){ float a = 6.2831853f * ((float)k + 0.5f) / (float)n; V3 p = v3(c.x + cosf(a) * r, c.y + sinf(a) * r, z); V3 aim = v3(c.x, c.y, z); add(p, aim); }
	}
	return true;
}

// ------------------------------------------------------------------------------------------------- BVH ---
struct BvhNode { V3 lo, hi; int left, right, first, count; };	// count > 0: a leaf over idx[first .. first+count)
enum { HIT_SCENE = 1, HIT_SELF = 2 };	// occluder classes: the neighbours' triangles come first in `v`, the model's own after `selfFrom`
struct BvhHit { int tri; float t; V3 n; };
struct Bvh {
	std::vector<V3> v;		// 3 per triangle
	std::vector<int> idx;
	std::vector<BvhNode> nodes;
	int selfFrom;			// triangle index where the model's own triangles start
	bool twoSided;			// back faces block too (SA models keep inverted faces inside)
	Bvh() : selfFrom(0), twoSided(true) {}
	int build(int a, int b, std::vector<V3> &cen)
	{
		BvhNode n; n.lo = v3(1e30f, 1e30f, 1e30f); n.hi = v3(-1e30f, -1e30f, -1e30f); n.left = n.right = -1; n.first = a; n.count = 0;
		V3 clo = n.lo, chi = n.hi;
		for(int i = a; i < b; i++){
			const V3 *t = &v[(size_t)idx[(size_t)i] * 3];
			for(int k = 0; k < 3; k++){ n.lo = v3(vmin(n.lo.x, t[k].x), vmin(n.lo.y, t[k].y), vmin(n.lo.z, t[k].z)); n.hi = v3(vmax(n.hi.x, t[k].x), vmax(n.hi.y, t[k].y), vmax(n.hi.z, t[k].z)); }
			V3 c = cen[(size_t)idx[(size_t)i]]; clo = v3(vmin(clo.x, c.x), vmin(clo.y, c.y), vmin(clo.z, c.z)); chi = v3(vmax(chi.x, c.x), vmax(chi.y, c.y), vmax(chi.z, c.z));
		}
		int me = (int)nodes.size(); nodes.push_back(n);
		if(b - a <= 4){ nodes[(size_t)me].count = b - a; return me; }
		V3 ext = chi - clo; int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
		if((&ext.x)[axis] < 1e-6f){ nodes[(size_t)me].count = b - a; return me; }	// all centroids in one point
		int mid = (a + b) / 2;
		std::nth_element(idx.begin() + a, idx.begin() + mid, idx.begin() + b, [&](int p, int q){ return (&cen[(size_t)p].x)[axis] < (&cen[(size_t)q].x)[axis]; });
		int l = build(a, mid, cen), r = build(mid, b, cen);
		nodes[(size_t)me].left = l; nodes[(size_t)me].right = r;
		return me;
	}
	void build()
	{
		nodes.clear(); idx.clear();
		int n = (int)(v.size() / 3); if(n == 0) return;
		idx.resize((size_t)n); std::vector<V3> cen((size_t)n);
		for(int i = 0; i < n; i++){ idx[(size_t)i] = i; const V3 *t = &v[(size_t)i * 3]; cen[(size_t)i] = (t[0] + t[1] + t[2]) * (1.0f / 3.0f); }
		nodes.reserve((size_t)n / 2 + 16);
		build(0, n, cen);
	}
	// the ray o + t·d against the triangles of the classes in `mask`, t in (tmin, tmax): any hit (closest = false) or the nearest
	bool trace(V3 o, V3 d, float tmin, float tmax, int mask, bool closest, BvhHit *hit) const
	{
		if(nodes.empty() || mask == 0) return false;
		V3 inv = v3(1.0f / (fabsf(d.x) > 1e-12f ? d.x : 1e-12f), 1.0f / (fabsf(d.y) > 1e-12f ? d.y : 1e-12f), 1.0f / (fabsf(d.z) > 1e-12f ? d.z : 1e-12f));
		int stack[96]; int sp = 0; stack[sp++] = 0; bool found = false; float best = tmax;
		while(sp){
			const BvhNode &n = nodes[(size_t)stack[--sp]];
			float tx0 = (n.lo.x - o.x) * inv.x, tx1 = (n.hi.x - o.x) * inv.x, ty0 = (n.lo.y - o.y) * inv.y, ty1 = (n.hi.y - o.y) * inv.y, tz0 = (n.lo.z - o.z) * inv.z, tz1 = (n.hi.z - o.z) * inv.z;
			float t0 = vmax(vmax(vmin(tx0, tx1), vmin(ty0, ty1)), vmax(vmin(tz0, tz1), tmin));
			float t1 = vmin(vmin(vmax(tx0, tx1), vmax(ty0, ty1)), vmin(vmax(tz0, tz1), best));
			if(t0 > t1) continue;
			if(n.count > 0){
				for(int i = 0; i < n.count; i++){
					int ti = idx[(size_t)(n.first + i)];
					if(!((ti >= selfFrom ? HIT_SELF : HIT_SCENE) & mask)) continue;
					const V3 *t = &v[(size_t)ti * 3];
					V3 e1 = t[1] - t[0], e2 = t[2] - t[0], h = cross(d, e2);
					float det = dot(e1, h); if(fabsf(det) < 1e-12f) continue;
					if(!twoSided && det < 0.0f) continue;	// det = −d·n: a back face
					float f = 1.0f / det; V3 s = o - t[0];
					float u = f * dot(s, h); if(u < 0.0f || u > 1.0f) continue;
					V3 q = cross(s, e1); float w = f * dot(d, q); if(w < 0.0f || u + w > 1.0f) continue;
					float tt = f * dot(e2, q);
					if(tt > tmin && tt < best){
						if(!closest) return true;
						found = true; best = tt;
						if(hit){ hit->tri = ti; hit->t = tt; V3 nn = normalize(cross(e1, e2)); if(dot(nn, d) > 0) nn = nn * -1.0f; hit->n = nn; }
					}
				}
			}else{
				if(sp + 2 > 96) continue;
				stack[sp++] = n.left; stack[sp++] = n.right;
			}
		}
		return found;
	}
	bool anyHit(V3 o, V3 d, float tmin, float tmax, int mask = HIT_SCENE | HIT_SELF) const { return trace(o, d, tmin, tmax, mask, false, nullptr); }
};

// ------------------------------------------------------------------------------------------------- sampling ---
static float radicalInverse(uint32_t b) { b = (b << 16) | (b >> 16); b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1); b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2); b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4); b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8); return (float)b * 2.3283064365386963e-10f; }
static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static void tangentFrame(V3 n, V3 &t, V3 &b) { V3 a = fabsf(n.z) < 0.9f ? v3(0, 0, 1) : v3(1, 0, 0); t = normalize(cross(a, n)); b = cross(n, t); }

// vertex normals: the file's when it has them, else the area-weighted mean of the faces around the vertex. With `hardAngle` > 0
// a vertex on a sharp edge (its faces farther apart than the angle) takes the normal of its largest face group instead of the
// smoothed one — the smoothed normal at a wall / roof corner looked 45° up and painted a bright band along the top of every wall.
static void vertexNormals(const GGeom &g, float hardAngle, std::vector<V3> &n)
{
	int nv = g.numVerts(); n.assign((size_t)nv, v3(0, 0, 0));
	std::vector<char> ok((size_t)nv, 0);
	if(g.normals && g.nrm.size() == (size_t)nv) for(int i = 0; i < nv; i++){ float l = length(g.nrm[(size_t)i]); if(l > 0.5f && l < 2.0f){ n[(size_t)i] = g.nrm[(size_t)i] * (1.0f / l); ok[(size_t)i] = 1; } }
	std::vector<V3> acc((size_t)nv, v3(0, 0, 0));
	std::vector<std::vector<int> > adj; if(hardAngle > 0) adj.resize((size_t)nv);
	for(size_t t = 0; t < g.tri.size(); t++){ const uint32_t *v = g.tri[t].v; V3 fn = cross(g.pos[v[1]] - g.pos[v[0]], g.pos[v[2]] - g.pos[v[0]]); for(int k = 0; k < 3; k++){ acc[v[k]] = acc[v[k]] + fn; if(hardAngle > 0) adj[v[k]].push_back((int)t); } }
	for(int i = 0; i < nv; i++) if(!ok[(size_t)i]){ V3 a = normalize(acc[(size_t)i]); n[(size_t)i] = length(a) > 0.5f ? a : v3(0, 0, 1); }
	if(hardAngle <= 0) return;
	float cosA = cosf(hardAngle * 3.14159265f / 180.0f);
	for(int i = 0; i < nv; i++){
		const std::vector<int> &f = adj[(size_t)i]; if(f.size() < 2) continue;
		// the largest face sets the group; faces within the angle of it are averaged (area-weighted), the rest are another side of an edge
		int big = -1; float bigA = -1; std::vector<V3> fn(f.size()); std::vector<float> fa(f.size());
		for(size_t k = 0; k < f.size(); k++){ const uint32_t *v = g.tri[(size_t)f[k]].v; V3 c = cross(g.pos[v[1]] - g.pos[v[0]], g.pos[v[2]] - g.pos[v[0]]); fa[k] = 0.5f * length(c); fn[k] = normalize(c); if(fa[k] > bigA){ bigA = fa[k]; big = (int)k; } }
		if(big < 0 || bigA < 1e-9f) continue;
		bool sharp = false; for(size_t k = 0; k < f.size(); k++) if(dot(fn[k], fn[(size_t)big]) < cosA) sharp = true;
		if(!sharp) continue;
		V3 s = v3(0, 0, 0); for(size_t k = 0; k < f.size(); k++) if(dot(fn[k], fn[(size_t)big]) >= cosA) s = s + fn[k] * fa[k];
		s = normalize(s); if(length(s) > 0.5f) n[(size_t)i] = s;
	}
}

// vertices that share a position (split by UV / material) as one node: the neighbour smoothing must cross those seams
static void positionAdjacency(const GGeom &g, std::vector<std::vector<int> > &adj)
{
	int nv = g.numVerts(); adj.assign((size_t)nv, std::vector<int>());
	std::unordered_map<uint64_t, std::vector<int> > cell;
	auto key = [](V3 p){ int64_t x = (int64_t)floorf(p.x * 1000.0f), y = (int64_t)floorf(p.y * 1000.0f), z = (int64_t)floorf(p.z * 1000.0f); return (uint64_t)((x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL)); };
	std::vector<int> rep((size_t)nv);	// representative vertex per position
	for(int i = 0; i < nv; i++){ std::vector<int> &c = cell[key(g.pos[(size_t)i])]; int r = -1; for(size_t k = 0; k < c.size(); k++){ V3 d = g.pos[(size_t)c[k]] - g.pos[(size_t)i]; if(dot(d, d) < 1e-8f){ r = c[k]; break; } } if(r < 0){ r = i; c.push_back(i); } rep[(size_t)i] = r; }
	std::vector<std::set<int> > nb((size_t)nv);
	for(size_t t = 0; t < g.tri.size(); t++) for(int k = 0; k < 3; k++){ int a = rep[g.tri[t].v[k]], b = rep[g.tri[t].v[(k + 1) % 3]]; if(a != b){ nb[(size_t)a].insert(b); nb[(size_t)b].insert(a); } }
	for(int i = 0; i < nv; i++){ const std::set<int> &s = nb[(size_t)rep[(size_t)i]]; adj[(size_t)i].assign(s.begin(), s.end()); if(rep[(size_t)i] != i) adj[(size_t)i].push_back(rep[(size_t)i]); }
}

static float lum(uint32_t rgba) { return 0.299f * (float)(rgba & 0xFF) + 0.587f * (float)((rgba >> 8) & 0xFF) + 0.114f * (float)((rgba >> 16) & 0xFF); }
static uint32_t packRgba(const float *rgb, uint8_t a)
{
	int c[3]; for(int k = 0; k < 3; k++){ float v = rgb[k] * 255.0f + 0.5f; c[k] = v < 0 ? 0 : v > 255 ? 255 : (int)v; }
	return (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)a << 24);
}
static void unpackRgb(uint32_t c, float *rgb) { rgb[0] = (c & 0xFF) / 255.0f; rgb[1] = ((c >> 8) & 0xFF) / 255.0f; rgb[2] = ((c >> 16) & 0xFF) / 255.0f; }

// ------------------------------------------------------------------------------------------------- writing ---
// the prelit block of a GEOMETRY struct sits after the 16-byte header (and the 12-byte surface properties of RW < 3.4); the
// night chunk lives in the geometry's EXTENSION. Both are overwritten in place when present and inserted when not — sizes of
// STRUCT / EXTENSION / GEOMETRY / GEOMETRYLIST / CLUMP follow; nothing else in the file moves relative to its chunk.
struct GeomLayout { size_t structHdr, structEnd, prelitAt, extHdr, extEnd, nightHdr, nightEnd; bool hasPrelit, hasNight; uint32_t libid; };
static bool geomLayout(const std::vector<uint8_t> &dff, const GGeom &g, uint32_t version, GeomLayout &L)
{
	Buf b(dff.data(), dff.size()); b.seek(g.hdrAt);
	Chunk gc_; if(!readChunk(b, gc_) || gc_.end > dff.size()) return false;
	L.libid = gc_.libid;
	Chunk st; if(!findChunk(b, ID_STRUCT, st, gc_.end) || st.size < 16) return false;
	L.structHdr = st.start - 12; L.structEnd = st.end;
	uint32_t flags = b.u32();
	L.hasPrelit = (flags & GF_PRELIT) != 0;
	L.prelitAt = st.start + 16 + (version < 0x34000 && st.size >= 28 ? 12 : 0);
	b.seek(st.end);
	Chunk ml; if(!findChunk(b, ID_MATLIST, ml, gc_.end)) return false;
	b.seek(ml.end);
	Chunk ext; if(!findChunk(b, ID_EXTENSION, ext, gc_.end)) return false;
	L.extHdr = ext.start - 12; L.extEnd = ext.end; L.hasNight = false; L.nightHdr = L.nightEnd = 0;
	size_t q = ext.start;
	while(q + 12 <= ext.end){
		b.seek(q); Chunk c; if(!readChunk(b, c) || c.truncated || c.end > ext.end) return false;
		if(c.type == ID_NIGHT){ L.hasNight = true; L.nightHdr = c.start - 12; L.nightEnd = c.end; }
		q = c.end;
	}
	return true;
}
static V3 closestOnTri(V3 p, V3 a, V3 b, V3 c)
{
	V3 ab = b - a, ac = c - a, ap = p - a;
	float d1 = dot(ab, ap), d2 = dot(ac, ap);
	if(d1 <= 0 && d2 <= 0) return a;
	V3 bp = p - b; float d3 = dot(ab, bp), d4 = dot(ac, bp);
	if(d3 >= 0 && d4 <= d3) return b;
	float vc = d1 * d4 - d3 * d2;
	if(vc <= 0 && d1 >= 0 && d3 <= 0){ float v = d1 / (d1 - d3); return v3(a.x + ab.x * v, a.y + ab.y * v, a.z + ab.z * v); }
	V3 cp = p - c; float d5 = dot(ab, cp), d6 = dot(ac, cp);
	if(d6 >= 0 && d5 <= d6) return c;
	float vb = d5 * d2 - d1 * d6;
	if(vb <= 0 && d2 >= 0 && d6 <= 0){ float w = d2 / (d2 - d6); return v3(a.x + ac.x * w, a.y + ac.y * w, a.z + ac.z * w); }
	float va = d3 * d6 - d5 * d4;
	if(va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0){ float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return v3(b.x + (c.x - b.x) * w, b.y + (c.y - b.y) * w, b.z + (c.z - b.z) * w); }
	float den = 1.0f / (va + vb + vc), v = vb * den, w = vc * den;
	return v3(a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w);
}

// ------------------------------------------------------------------------------------------- the fast pass ---
// What Itera's geometry nodes do in the viewport, and what makes a lamp draggable here: no rays, no BVH — the
// ambient comes from how much sky the normal faces, every lamp adds lambert × falloff. A model of a few thousand
// vertices takes a fraction of a millisecond, so the picture follows the mouse; shadows, sky visibility and the
// bounce are what the full BakePrelit adds afterwards.
bool BakeQuick(const std::vector<uint8_t> &dff, const float *mat, const BakeOptions &o, BakeResult &res)
{
	double t0 = nowMs();
	res = BakeResult();
	GDff d; if(!ParseDffGeoms(dff, d, res.err)) return false;
	res.geoms.resize(d.geoms.size());
	float sunDir[3] = { o.sunDir[0], o.sunDir[1], o.sunDir[2] };
	if(!o.suns.empty()){ float w = 0; sunDir[0] = sunDir[1] = sunDir[2] = 0; for(size_t i = 0; i < o.suns.size(); i++){ for(int c = 0; c < 3; c++) sunDir[c] += o.suns[i].dir[c] * o.suns[i].weight; w += o.suns[i].weight; } if(w > 0) for(int c = 0; c < 3; c++) sunDir[c] /= w; }
	V3 sd = v3(sunDir[0], sunDir[1], sunDir[2]);
	{ float l = sqrtf(dot(sd, sd)); if(l > 0.0001f) sd = v3(sd.x / l, sd.y / l, sd.z / l); }
	for(size_t gi = 0; gi < d.geoms.size(); gi++){
		const GGeom &g = d.geoms[gi];
		BakeGeom &r = res.geoms[gi];
		int nv = (int)g.pos.size();
		r.verts = nv;
		r.day.assign((size_t)nv, 0); r.night.assign((size_t)nv, 0);
		std::vector<float> fday((size_t)nv * 3, 0.0f), fnight((size_t)nv * 3, 0.0f);	// kept as floats until the smoothing is done
		std::vector<V3> nrm; vertexNormals(g, o.hardAngle, nrm);
		res.verts += nv; res.tris += (int)g.tri.size();
		// how close the lamp gets to the surface around each vertex: on a coarse mesh (a road tile with four
		// corners) the lamp can sit in the middle of a face, metres away from every vertex — measuring only to
		// the vertices would leave the tile black although the light plainly covers it
		std::vector<std::vector<float> > faceDist;
		if(!o.lights.empty() && o.lampSurface){
			faceDist.assign((size_t)nv, std::vector<float>(o.lights.size(), 1e30f));
			std::vector<V3> wp((size_t)nv);
			for(int i = 0; i < nv; i++) wp[(size_t)i] = mat ? mulPoint(mat, g.pos[(size_t)i]) : g.pos[(size_t)i];
			for(size_t t = 0; t < g.tri.size(); t++){
				const uint32_t *iv = g.tri[t].v;
				if(iv[0] >= (uint32_t)nv || iv[1] >= (uint32_t)nv || iv[2] >= (uint32_t)nv) continue;
				for(size_t li = 0; li < o.lights.size(); li++){
					const BakeLight &L = o.lights[li];
					V3 lp = v3(L.pos.x, L.pos.y, L.pos.z);
					V3 q = closestOnTri(lp, wp[iv[0]], wp[iv[1]], wp[iv[2]]);
					V3 d = lp - q; float dist = sqrtf(dot(d, d));
					for(int k = 0; k < 3; k++) if(dist < faceDist[iv[k]][li]) faceDist[iv[k]][li] = dist;
				}
			}
		}
		for(int i = 0; i < nv; i++){
			V3 wp = mat ? mulPoint(mat, g.pos[(size_t)i]) : g.pos[(size_t)i];
			V3 n = mat ? mulDir(mat, nrm[(size_t)i]) : nrm[(size_t)i];
			{ float l = sqrtf(dot(n, n)); if(l > 0.0001f) n = v3(n.x / l, n.y / l, n.z / l); }
			float up = 0.5f + 0.5f * n.z;		// how much of the sky the normal faces (no visibility test)
			float day[3], night[3];
			if(o.lampsAdd){	// the model keeps the light it was shipped with; the lamps only add to it
				uint32_t d0 = g.day.size() > (size_t)i ? g.day[(size_t)i] : 0xFF808080u;
				uint32_t n0 = g.nightCol.size() > (size_t)i ? g.nightCol[(size_t)i] : d0;
				unpackRgb(d0, day); unpackRgb(n0, night);
			}else{
				for(int c = 0; c < 3; c++){
					float sky = o.skyBot[c] + (o.skyTop[c] - o.skyBot[c]) * up * o.skyGradient;
					day[c] = o.shadeFloor + o.skyLevel * up * sky;
					night[c] = o.night ? o.nightFloor + o.nightLevel * up * o.nightSky[c] : 0.0f;
				}
				if(o.sun){ float c0 = dot(n, sd); if(c0 > 0) for(int c = 0; c < 3; c++) day[c] += o.sunLevel * c0 * o.sunRgb[c]; }
			}
			for(size_t li = 0; li < o.lights.size(); li++){
				const BakeLight &L = o.lights[li];
				float range = L.range > 0.0f ? L.range * o.lampRangeMul : o.lampDefaultRange;
				if(range <= 0.0f) continue;
				V3 dv = v3(L.pos.x - wp.x, L.pos.y - wp.y, L.pos.z - wp.z);
				float dist = sqrtf(dot(dv, dv));
				float surf = faceDist.empty() ? dist : faceDist[(size_t)i][li];	// the lamp's distance to the surface here
				if(surf > range) continue;					// out of reach of this vertex's faces
				if(dist < 0.001f) dist = 0.001f;
				V3 ld = v3(dv.x / dist, dv.y / dist, dv.z / dist);
				float c0 = dot(n, ld);
				if(c0 <= 0.0f){ if(!L.twoSided) continue; c0 = -c0 * 0.5f; }	// the back of a face, at half strength
				if(L.spot){	// a cone about the lamp's direction: full inside, fading over the blend
					float cs = -dot(ld, L.dir);		// ld points at the lamp, the cone points away from it
					float outer = L.spotCos - L.spotBlend;
					if(cs <= outer) continue;
					if(cs < L.spotCos){ float k = (cs - outer) / (L.spotCos - outer + 1e-6f); c0 *= k * k * (3.0f - 2.0f * k); }
				}
				int fmode = L.falloff >= 0 ? L.falloff : o.falloff;
				float sd = surf + L.offset;			// Offset pushes the curve outwards: no blow-out right under the lamp
				float t = sd / range; if(t > 1.0f) t = 1.0f;
				float att = fmode == 1 ? (1.0f - t) * (1.0f - t)
				          : fmode == 2 ? (1.0f - t) * (1.0f - t) * (3.0f - 2.0f * (1.0f - t))
				          : fmode == 3 ? (sd > 0.001f ? 0.5f / sd : 500.0f) * (1.0f - t)	// Itera's 1/d, faded out at the sphere so there is no hard edge
				          : 1.0f - t;
				float w = (L.rig ? L.strength : o.lampLevel) * att * c0;	// the same weight the full bake uses: a hand-placed lamp goes by its own strength
				float *dst = L.when == 0 ? day : night;
				int passes = L.when == 2 ? 2 : 1;
				for(int q = 0; q < passes; q++){
					float *tgt = L.when == 2 ? (q == 0 ? day : night) : dst;
					if(L.mode == 1) for(int c = 0; c < 3; c++) tgt[c] = tgt[c] * (1.0f - w) + w * L.rgb[c];	// tint
					else for(int c = 0; c < 3; c++) tgt[c] += w * L.rgb[c];					// add
				}
			}
			for(int c = 0; c < 3; c++){
				if(!o.lampsAdd){ day[c] = day[c] * o.scale * o.tintDay[c]; night[c] = night[c] * o.scale * o.tintNight[c]; }
				if(day[c] < 0) day[c] = 0; if(day[c] > 1) day[c] = 1;
				if(night[c] < 0) night[c] = 0; if(night[c] > 1) night[c] = 1;
			}
			for(int c = 0; c < 3; c++){ fday[(size_t)i * 3 + c] = day[c]; fnight[(size_t)i * 3 + c] = night[c]; }
		}
		if(o.smoothIter > 0 && nv > 0){	// average over the neighbours that share a position (across UV seams too)
			// Smooth only the lamp contribution; the artist's prelit must stay intact.
			std::vector<float> baseD, baseN;
			if(o.lampsAdd){
				baseD.resize(fday.size()); baseN.resize(fnight.size());
				for(int i = 0; i < nv; i++){
					uint32_t d0 = g.day.size() > (size_t)i ? g.day[(size_t)i] : 0xFF808080u;
					uint32_t n0 = g.nightCol.size() > (size_t)i ? g.nightCol[(size_t)i] : d0;
					unpackRgb(d0, &baseD[(size_t)i * 3]); unpackRgb(n0, &baseN[(size_t)i * 3]);
					for(int c = 0; c < 3; c++){ fday[(size_t)i * 3 + c] -= baseD[(size_t)i * 3 + c]; fnight[(size_t)i * 3 + c] -= baseN[(size_t)i * 3 + c]; }
				}
			}
			std::vector<std::vector<int> > adj;
			positionAdjacency(g, adj);
			std::vector<float> tmpD(fday.size()), tmpN(fnight.size());
			for(int it = 0; it < o.smoothIter; it++){
				for(int i = 0; i < nv; i++){
					const std::vector<int> &nb = adj[(size_t)i];
					float wd[3] = { fday[(size_t)i * 3], fday[(size_t)i * 3 + 1], fday[(size_t)i * 3 + 2] };
					float wn[3] = { fnight[(size_t)i * 3], fnight[(size_t)i * 3 + 1], fnight[(size_t)i * 3 + 2] };
					for(size_t k = 0; k < nb.size(); k++) for(int c = 0; c < 3; c++){ wd[c] += fday[(size_t)nb[k] * 3 + c]; wn[c] += fnight[(size_t)nb[k] * 3 + c]; }
					float inv = 1.0f / (float)(nb.size() + 1);
					for(int c = 0; c < 3; c++){ tmpD[(size_t)i * 3 + c] = wd[c] * inv; tmpN[(size_t)i * 3 + c] = wn[c] * inv; }
				}
				fday.swap(tmpD); fnight.swap(tmpN);
			}
			if(o.lampsAdd) for(size_t i = 0; i < fday.size(); i++){ fday[i] += baseD[i]; fnight[i] += baseN[i]; }
		}
		for(int i = 0; i < nv; i++){
			float day[3], night[3];
			for(int c = 0; c < 3; c++){
				day[c] = fday[(size_t)i * 3 + c]; night[c] = fnight[(size_t)i * 3 + c];
				if(day[c] < 0) day[c] = 0; if(day[c] > 1) day[c] = 1;
				if(night[c] < 0) night[c] = 0; if(night[c] > 1) night[c] = 1;
			}
			uint8_t a = (uint8_t)(g.day.size() > (size_t)i ? (g.day[(size_t)i] >> 24) & 0xFF : 255);
			r.day[(size_t)i] = packRgba(day, a);
			r.night[(size_t)i] = packRgba(night, a);
		}
	}
	res.lamps = (int)o.lights.size();
	BakeStats(res);
	res.ms = nowMs() - t0;
	return true;
}

bool BakeWriteColours(const std::vector<uint8_t> &dff, const std::vector<BakeGeom> &res, std::vector<uint8_t> &out, std::string &err)
{
	GDff d; if(!ParseDffGeoms(dff, d, err)) return false;
	if(res.size() != d.geoms.size()){ err = T("число геометрий не совпало"); return false; }
	out.clear(); out.reserve(dff.size() + d.geoms.size() * 8);
	size_t cursor = 0; long total = 0;
	for(size_t gi = 0; gi < d.geoms.size(); gi++){
		const GGeom &g = d.geoms[gi]; const BakeGeom &r = res[gi];
		GeomLayout L; if(!geomLayout(dff, g, d.version, L)){ err = fmt(T("геометрия %d: не разобрана структура чанка"), (int)gi); return false; }
		int nv = g.numVerts(); if((int)r.day.size() != nv || (int)r.night.size() != nv){ err = fmt(T("геометрия %d: число вершин не совпало"), (int)gi); return false; }
		out.insert(out.end(), dff.begin() + (long)cursor, dff.begin() + (long)g.hdrAt);
		std::vector<uint8_t> piece(dff.begin() + (long)g.hdrAt, dff.begin() + (long)g.endAt);
		size_t base = g.hdrAt; long dExt = 0, dSt = 0;
		// the extension first (it lies after the struct: the struct's insertion would shift it)
		std::vector<uint8_t> night(4 + (size_t)nv * 4); put32(night, 0, 1); for(int i = 0; i < nv; i++) put32(night, 4 + (size_t)i * 4, r.night[(size_t)i]);
		if(L.hasNight && L.nightEnd - L.nightHdr == 12 + night.size()){ memcpy(&piece[L.nightHdr - base + 12], night.data(), night.size()); }
		else{
			if(L.hasNight){ piece.erase(piece.begin() + (long)(L.nightHdr - base), piece.begin() + (long)(L.nightEnd - base)); dExt -= (long)(L.nightEnd - L.nightHdr); L.extEnd -= L.nightEnd - L.nightHdr; }
			std::vector<uint8_t> ch(12); put32(ch, 0, ID_NIGHT); put32(ch, 4, (uint32_t)night.size()); put32(ch, 8, L.libid); ch.insert(ch.end(), night.begin(), night.end());
			piece.insert(piece.begin() + (long)(L.extEnd - base), ch.begin(), ch.end()); dExt += (long)ch.size();
			put32(piece, L.extHdr - base + 4, (uint32_t)((long)get32(piece, L.extHdr - base + 4) + dExt));
		}
		// the struct: overwrite or insert the prelit block, set the flag
		if(L.hasPrelit){ for(int i = 0; i < nv; i++) put32(piece, L.prelitAt - base + (size_t)i * 4, r.day[(size_t)i]); }
		else{
			std::vector<uint8_t> blk((size_t)nv * 4); for(int i = 0; i < nv; i++) put32(blk, (size_t)i * 4, r.day[(size_t)i]);
			piece.insert(piece.begin() + (long)(L.prelitAt - base), blk.begin(), blk.end()); dSt = (long)blk.size();
			put32(piece, L.structHdr - base + 4, (uint32_t)((long)get32(piece, L.structHdr - base + 4) + dSt));
			put32(piece, L.structHdr - base + 12, get32(piece, L.structHdr - base + 12) | GF_PRELIT);
		}
		put32(piece, 4, (uint32_t)((long)get32(piece, 4) + dExt + dSt));
		total += dExt + dSt;
		out.insert(out.end(), piece.begin(), piece.end());
		cursor = g.endAt;
	}
	out.insert(out.end(), dff.begin() + (long)cursor, dff.end());
	put32(out, d.listHdr + 4, (uint32_t)((long)get32(dff, d.listHdr + 4) + total));
	put32(out, d.clumpHdr + 4, (uint32_t)((long)get32(dff, d.clumpHdr + 4) + total));
	return true;
}

void BakeStats(BakeResult &r)
{
	double sd = 0, sn = 0; int cnt = 0; r.minDay = r.minNight = 255; r.maxDay = r.maxNight = 0; memset(r.histDay, 0, sizeof(r.histDay)); memset(r.histNight, 0, sizeof(r.histNight));
	for(size_t gi = 0; gi < r.geoms.size(); gi++) for(size_t i = 0; i < r.geoms[gi].day.size(); i++){
		float ld = lum(r.geoms[gi].day[i]), ln = lum(r.geoms[gi].night[i]);
		sd += ld; sn += ln; cnt++; r.minDay = vmin(r.minDay, ld); r.maxDay = vmax(r.maxDay, ld); r.minNight = vmin(r.minNight, ln); r.maxNight = vmax(r.maxNight, ln);
		int bd = (int)(ld / 16.0f), bn = (int)(ln / 16.0f); r.histDay[bd > 15 ? 15 : bd]++; r.histNight[bn > 15 ? 15 : bn]++;
	}
	if(cnt){ r.meanDay = (float)(sd / cnt); r.meanNight = (float)(sn / cnt); } else { r.meanDay = r.meanNight = r.minDay = r.minNight = 0; }
}

// ------------------------------------------------------------------------------------------------- bad geometry ---
// Coarse walls of a few huge triangles fanning from one vertex cannot carry a lighting field: the card interpolates each
// triangle on its own and the shared edges show as spokes. The remedy: planar regions — connected coplanar faces — get ONE
// linear function fitted to light samples spread over their area (a linear function every triangle reproduces exactly, so
// the whole wall is one gradient).
// the planar regions of a geometry: faces joined over shared edges (by position, across UV seams) when their normals agree
struct Region { std::vector<int> faces; float area; V3 n, c, tu, tv; };
static void planarRegions(const GGeom &g, float angleDeg, float minArea, std::vector<Region> &out, std::vector<int> &faceRegion)
{
	out.clear(); size_t nt = g.tri.size(); faceRegion.assign(nt, -1); if(nt == 0) return;
	std::vector<V3> fn(nt); std::vector<float> fa(nt);
	for(size_t t = 0; t < nt; t++){ const uint32_t *v = g.tri[t].v; V3 c = cross(g.pos[v[1]] - g.pos[v[0]], g.pos[v[2]] - g.pos[v[0]]); fa[t] = 0.5f * length(c); fn[t] = normalize(c); }
	std::vector<int> rep(g.pos.size());	// vertices sharing a position count as one (across UV seams)
	{ std::unordered_map<uint64_t, std::vector<int> > cell; auto key = [](V3 p){ int64_t x = (int64_t)floorf(p.x * 1000.0f), y = (int64_t)floorf(p.y * 1000.0f), z = (int64_t)floorf(p.z * 1000.0f); return (uint64_t)((x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL)); };
	  for(size_t i = 0; i < g.pos.size(); i++){ std::vector<int> &c = cell[key(g.pos[i])]; int r = -1; for(size_t k = 0; k < c.size(); k++){ V3 dd = g.pos[(size_t)c[k]] - g.pos[i]; if(dot(dd, dd) < 1e-8f){ r = c[k]; break; } } if(r < 0){ r = (int)i; c.push_back((int)i); } rep[i] = r; } }
	std::vector<int> parent(nt); for(size_t t = 0; t < nt; t++) parent[t] = (int)t;
	auto find = [&parent](int x){ while(parent[(size_t)x] != x){ parent[(size_t)x] = parent[(size_t)parent[(size_t)x]]; x = parent[(size_t)x]; } return x; };
	float cosA = cosf(angleDeg * 3.14159265f / 180.0f);
	std::map<std::pair<int, int>, int> edgeFace;
	for(size_t t = 0; t < nt; t++){
		if(fa[t] < 1e-7f) continue;
		for(int k = 0; k < 3; k++){
			int a = rep[g.tri[t].v[k]], b = rep[g.tri[t].v[(k + 1) % 3]]; std::pair<int, int> key(a < b ? a : b, a < b ? b : a);
			auto it = edgeFace.find(key);
			if(it == edgeFace.end()){ edgeFace[key] = (int)t; continue; }
			int u = it->second; if(dot(fn[t], fn[(size_t)u]) >= cosA){ int ra = find((int)t), rb = find(u); if(ra != rb) parent[(size_t)ra] = rb; }
		}
	}
	std::map<int, int> regionOf;
	for(size_t t = 0; t < nt; t++){ if(fa[t] < 1e-7f) continue; int r = find((int)t); auto it = regionOf.find(r); if(it == regionOf.end()){ Region q; q.area = 0; q.n = q.c = v3(0, 0, 0); it = regionOf.insert(std::make_pair(r, (int)out.size())).first; out.push_back(q); } Region &q = out[(size_t)it->second]; q.faces.push_back((int)t); q.area += fa[t]; q.n = q.n + fn[t] * fa[t]; const uint32_t *v = g.tri[t].v; q.c = q.c + (g.pos[v[0]] + g.pos[v[1]] + g.pos[v[2]]) * (fa[t] / 3.0f); }
	std::vector<Region> kept;
	for(size_t i = 0; i < out.size(); i++){ Region &q = out[i]; if(q.area < minArea || q.faces.size() < 2) continue; q.c = q.c * (1.0f / q.area); q.n = normalize(q.n); if(length(q.n) < 0.5f) continue; tangentFrame(q.n, q.tu, q.tv); for(size_t k = 0; k < q.faces.size(); k++) faceRegion[(size_t)q.faces[k]] = (int)kept.size(); kept.push_back(q); }
	out.swap(kept);
}
// least squares of value = a + b·u + c·v over the samples of one region (per channel); a plain mean when the samples are degenerate
static void fitPlane(const std::vector<float> &u, const std::vector<float> &v, const float *vals, int stride, int ch, float *abc)
{
	double S = 0, Su = 0, Sv = 0, Suu = 0, Suv = 0, Svv = 0, Sy = 0, Suy = 0, Svy = 0; size_t n = u.size();
	for(size_t i = 0; i < n; i++){ double y = vals[i * (size_t)stride + (size_t)ch]; S += 1; Su += u[i]; Sv += v[i]; Suu += (double)u[i] * u[i]; Suv += (double)u[i] * v[i]; Svv += (double)v[i] * v[i]; Sy += y; Suy += u[i] * y; Svy += v[i] * y; }
	double det = S * (Suu * Svv - Suv * Suv) - Su * (Su * Svv - Suv * Sv) + Sv * (Su * Suv - Suu * Sv);
	if(n < 4 || fabs(det) < 1e-9 * (S * S * S + 1e-9)){ abc[0] = n ? (float)(Sy / S) : 0; abc[1] = abc[2] = 0; return; }
	double a = (Sy * (Suu * Svv - Suv * Suv) - Su * (Suy * Svv - Suv * Svy) + Sv * (Suy * Suv - Suu * Svy)) / det;
	double b = (S * (Suy * Svv - Suv * Svy) - Sy * (Su * Svv - Suv * Sv) + Sv * (Su * Svy - Suy * Sv)) / det;
	double c = (S * (Suu * Svy - Suy * Suv) - Su * (Su * Svy - Suy * Sv) + Sy * (Su * Suv - Suu * Sv)) / det;
	abc[0] = (float)a; abc[1] = (float)b; abc[2] = (float)c;
}

// ------------------------------------------------------------------------------------------------- the bake ---
static float falloff(float d, float R, int kind)
{
	float t = 1.0f - d / R; if(t <= 0) return 0; if(t > 1) t = 1;
	return kind == 1 ? t * t : kind == 2 ? t * t * (3.0f - 2.0f * t) : t;
}
bool BakePrelit(const std::vector<uint8_t> &dff, const float *mat, const BakeScene &scene, const BakeOptions &o, BakeResult &r, const volatile bool *cancel, volatile float *progress)
{
	auto t0 = std::chrono::steady_clock::now();
	r = BakeResult();
	GDff d; if(!ParseDffGeoms(dff, d, r.err)) return false;
	if(d.geoms.empty()){ r.err = T("в DFF нет геометрии"); return false; }
	// the scene: the neighbours first, then the model itself, placed
	Bvh bvh; bvh.v = scene.tri; bvh.selfFrom = (int)(scene.tri.size() / 3); bvh.twoSided = o.twoSided;
	std::vector<BakeLight> lights = scene.lights;
	for(size_t gi = 0; gi < d.geoms.size(); gi++){
		const GGeom &g = d.geoms[gi];
		for(size_t t = 0; t < g.tri.size(); t++) for(int k = 0; k < 3; k++){ V3 p = g.pos[g.tri[t].v[k]]; bvh.v.push_back(mat ? mulPoint(mat, p) : p); }
		for(size_t k = 0; k < g.extKeep.size(); k++) if(g.extKeep[k].size() >= 12 && get32(g.extKeep[k], 0) == ID_2DFX) parseLights(g.extKeep[k], mat, lights);
		r.verts += g.numVerts(); r.tris += (int)g.tri.size();
		if(g.prelit) r.hadPrelit = true; if(g.night) r.hadNight = true;
	}
	// glowing windows: the faces whose material texture is in the list emit at night. Their vertices keep a bright
	// colour (below, after the curve) and each cluster of such faces becomes one lamp so the light lands on the wall
	// around them, on the ground and on the neighbours — that is what «окна светятся» means in the window.
	if(o.windowsOnly) lights.clear();
	std::vector<std::vector<char> > winVert(d.geoms.size());
	for(size_t k = 0; k < o.winTex.size(); k++) if(o.winTex[k] == "?"){	// wintex=? — just list the model's textures (CLI helper)
		std::string all; std::vector<std::string> seen;
		for(size_t gi = 0; gi < d.geoms.size(); gi++) for(size_t m = 0; m < d.geoms[gi].mats.size(); m++){
			std::string t = lower(d.geoms[gi].mats[m].tex); size_t dot = t.rfind('.'); if(dot != std::string::npos) t = t.substr(0, dot);
			if(t.empty()) continue; bool dup = false; for(size_t q = 0; q < seen.size(); q++) if(seen[q] == t) dup = true;
			if(dup) continue; seen.push_back(t); if(!all.empty()) all += ", "; all += t;
		}
		r.log.push_back(fmt(T("текстуры модели (%d): %s"), (int)seen.size(), all.c_str()));
	}
	if(!o.winTex.empty()){
		struct Cell { V3 c, n; float area; };
		std::map<int64_t, Cell> cells;
		const float CELL = 3.0f;	// one lamp per 3 m cell of window faces
		for(size_t gi = 0; gi < d.geoms.size(); gi++){
			const GGeom &g = d.geoms[gi];
			winVert[gi].assign((size_t)g.numVerts(), 0);
			std::vector<char> isWin(g.mats.size(), 0);
			for(size_t m = 0; m < g.mats.size(); m++){
				std::string t = lower(g.mats[m].tex);
				size_t dot = t.rfind('.'); if(dot != std::string::npos) t = t.substr(0, dot);
				for(size_t k = 0; k < o.winTex.size(); k++){	// an exact name, or a mask with * (CLI: wintex=*window*)
					const std::string &w = o.winTex[k];
					if(w.find('*') == std::string::npos){ if(t == w) isWin[m] = 1; continue; }
					std::string core; for(size_t x = 0; x < w.size(); x++) if(w[x] != '*') core += w[x];
					if(!core.empty() && t.find(core) != std::string::npos) isWin[m] = 1;
				}
			}

			// Connected coplanar faces sharing a welded edge form one window, even across UV seams.
			std::vector<int> parent(g.tri.size()), welded(g.pos.size());
			std::map<std::tuple<long long,long long,long long>, int> positions;
			for(size_t i = 0; i < g.pos.size(); i++){
				const V3 &v = g.pos[i];
				auto key = std::make_tuple((long long)llround(v.x * 10000.0), (long long)llround(v.y * 10000.0), (long long)llround(v.z * 10000.0));
				auto added = positions.insert(std::make_pair(key, (int)positions.size())); welded[i] = added.first->second;
			}
			auto root = [&](int i){ while(parent[(size_t)i] != i){ parent[(size_t)i] = parent[(size_t)parent[(size_t)i]]; i = parent[(size_t)i]; } return i; };
			std::map<std::pair<int,int>, std::vector<int> > edges;
			std::vector<V3> normals(g.tri.size());
			for(size_t t = 0; t < g.tri.size(); t++){
				parent[t] = (int)t; const GTri &tr = g.tri[t];
				if(tr.mat >= isWin.size() || !isWin[tr.mat] || tr.v[0] >= g.pos.size() || tr.v[1] >= g.pos.size() || tr.v[2] >= g.pos.size()) continue;
				normals[t] = normalize(cross(g.pos[tr.v[1]] - g.pos[tr.v[0]], g.pos[tr.v[2]] - g.pos[tr.v[0]]));
				for(int k = 0; k < 3; k++){
					int a = welded[tr.v[k]], b = welded[tr.v[(k + 1) % 3]]; if(a > b) std::swap(a,b);
					auto &touch = edges[std::make_pair(a,b)];
					for(int other : touch) if(g.mats[tr.mat].tex == g.mats[g.tri[(size_t)other].mat].tex && dot(normals[t], normals[(size_t)other]) > 0.996f)
						parent[(size_t)root((int)t)] = root(other);
					touch.push_back((int)t);
				}
			}
			std::map<int, V3> centres; std::map<int,int> counts;
			for(size_t t = 0; t < g.tri.size(); t++){
				const GTri &tr = g.tri[t];
				if(tr.mat >= isWin.size() || !isWin[tr.mat] || tr.v[0] >= g.pos.size() || tr.v[1] >= g.pos.size() || tr.v[2] >= g.pos.size()) continue;
				int id = root((int)t); centres[id] = centres[id] + (g.pos[tr.v[0]] + g.pos[tr.v[1]] + g.pos[tr.v[2]]) * (1.0f / 3.0f); counts[id]++;
			}
			std::map<int,bool> enabled;
			for(auto &item : centres){
				V3 c = item.second * (1.0f / counts[item.first]);
				uint32_t hash = (uint32_t)o.winSeed ^ 2166136261u;
				auto mix = [&](int v){ hash ^= (uint32_t)v; hash *= 16777619u; hash ^= hash >> 13; };
				if(o.winRandom == 2) mix((int)floorf(c.z / 3.0f));
				else { mix((int)lroundf(c.x * 100)); mix((int)lroundf(c.y * 100)); mix((int)lroundf(c.z * 100)); }
				hash ^= hash >> 16; hash *= 0x7feb352du; hash ^= hash >> 15;
				enabled[item.first] = o.winRandom == 0 || (double)hash / 4294967296.0 < o.winChance;
			}
			for(size_t t = 0; t < g.tri.size(); t++){
				const GTri &tr = g.tri[t];
				if(tr.mat >= g.mats.size() || !isWin[tr.mat] || tr.v[0] >= g.pos.size() || tr.v[1] >= g.pos.size() || tr.v[2] >= g.pos.size()) continue;
				if(!enabled[root((int)t)]){
					for(int k = 0; k < 3; k++) if(!winVert[gi][tr.v[k]]) winVert[gi][tr.v[k]] = 2;
					continue;
				}
				r.winFaces++;
				for(int k = 0; k < 3; k++) if(tr.v[k] < winVert[gi].size()) winVert[gi][tr.v[k]] = 1;
				V3 a = g.pos[tr.v[0]], b = g.pos[tr.v[1]], c = g.pos[tr.v[2]];
				if(mat){ a = mulPoint(mat, a); b = mulPoint(mat, b); c = mulPoint(mat, c); }
				V3 fn = cross(b - a, c - a); float area = 0.5f * length(fn);
				if(area < 1e-5f) continue;
				fn = fn * (1.0f / (2.0f * area));
				V3 ctr = (a + b + c) * (1.0f / 3.0f);
				int64_t key = ((int64_t)(int)floorf(ctr.x / CELL) << 42) ^ ((int64_t)(int)floorf(ctr.y / CELL) << 21) ^ (int64_t)(int)floorf(ctr.z / CELL);
				Cell &q = cells[key];
				q.c = q.c + ctr * area; q.n = q.n + fn * area; q.area += area;
			}
		}
		for(std::map<int64_t, Cell>::iterator it = cells.begin(); it != cells.end(); ++it){
			Cell &q = it->second; if(q.area <= 1e-4f) continue;
			V3 c = q.c * (1.0f / q.area), n = normalize(q.n);
			if(length(n) < 0.5f) n = v3(0, 0, 1);
			BakeLight L;
			L.pos = c + n * 0.25f;	// just outside the glass, so the wall it sits in does not block it
			memcpy(L.rgb, o.winRgb, sizeof(L.rgb));
			L.range = o.winRange > 0.5f ? o.winRange : 0.5f;
			L.strength = o.winLight;
			L.when = 1; L.shadow = true; L.mode = 0; L.rig = true;	// rig: its own strength, night only, added
			L.local = L.aim = L.pos;
			if(L.strength > 0.0f){ lights.push_back(L); r.winLamps++; }
		}
	}
	// the 2dfx lamps' reach: range × multiplier, corona-only lamps get the default range (0 = skipped)
	{ std::vector<BakeLight> keep; for(size_t i = 0; i < lights.size(); i++){ BakeLight L = lights[i]; L.range = L.range > 0 ? L.range * o.lampRangeMul : o.lampDefaultRange; if(L.range > 0.01f) keep.push_back(L); } lights.swap(keep); }
	r.lamps = (int)lights.size();
	if(!o.windowsOnly) lights.insert(lights.end(), o.lights.begin(), o.lights.end());
	r.sceneTris = (int)(bvh.v.size() / 3);
	bvh.build();
	// the hemisphere samples: cosine-weighted (Malley), Hammersley set, one common set turned about the normal per point
	int N = o.rays < 8 ? 8 : o.rays > 4096 ? 4096 : o.rays;
	std::vector<V3> samp((size_t)N);
	for(int i = 0; i < N; i++){ float u = ((float)i + 0.5f) / (float)N, v = radicalInverse((uint32_t)i); float rr = sqrtf(u), ph = 6.2831853f * v; samp[(size_t)i] = v3(rr * cosf(ph), rr * sinf(ph), sqrtf(1.0f - u)); }
	// the suns: the list (several hours) or the single direction; weights normalised
	struct Sun { V3 dir; float rgb[3]; float w; }; std::vector<Sun> suns;
	if(o.sun){
		if(!o.suns.empty()){ float tw = 0; for(size_t i = 0; i < o.suns.size(); i++) tw += o.suns[i].weight; for(size_t i = 0; i < o.suns.size(); i++){ Sun s; s.dir = normalize(v3(o.suns[i].dir[0], o.suns[i].dir[1], o.suns[i].dir[2])); memcpy(s.rgb, o.suns[i].rgb, sizeof(s.rgb)); s.w = tw > 0 ? o.suns[i].weight / tw : 1.0f; if(length(s.dir) > 0.5f) suns.push_back(s); } }
		else { Sun s; s.dir = normalize(v3(o.sunDir[0], o.sunDir[1], o.sunDir[2])); memcpy(s.rgb, o.sunRgb, sizeof(s.rgb)); s.w = 1; if(length(s.dir) > 0.5f) suns.push_back(s); }
	}
	int sunRays = o.sunRays < 1 ? 1 : o.sunRays > 64 ? 64 : o.sunRays; float sunTan = tanf(o.sunAngle * 3.14159265f / 180.0f);
	int sunMask = (o.sunSelf ? HIT_SELF : 0) | (o.sunNeighbours ? HIT_SCENE : 0), skyMask = (o.skySelf ? HIT_SELF : 0) | (o.skyNeighbours ? HIT_SCENE : 0);
	V3 moonDir = normalize(v3(0.2f, 0.3f, 0.93f));
	float bias = o.bias > 0.001f ? o.bias : 0.001f;
	// the light at one point of the surface (world space), before the result curve: day and night as fractions of 255
	auto evalPoint = [&](V3 p, V3 n, uint32_t seed, float *day, float *night, const float *surfRow){
		V3 t, bt; tangentFrame(n, t, bt);
		float ang = (float)(hash32(seed) & 0xFFFF) * (6.2831853f / 65536.0f), ca = cosf(ang), sa = sinf(ang);
		V3 t2 = t * ca + bt * sa, b2 = bt * ca - t * sa;
		V3 org = p + n * bias;
		float open = 0, up = 0;	// unblocked samples, and their mean elevation (0 = horizon, 1 = zenith)
		float bounce[3] = { 0, 0, 0 };
		for(int s = 0; !o.windowsOnly && s < N; s++){
			const V3 &q = samp[(size_t)s];
			V3 dir = t2 * q.x + b2 * q.y + n * q.z;
			if(o.bounce > 0.0f){
				BvhHit h; if(!bvh.trace(org, dir, bias, 1e9f, skyMask, true, &h)){ open += 1.0f; up += dir.z > 0 ? dir.z : 0.0f; continue; }
				// one bounce: the hit surface returns light in proportion to how much it faces the sky; rays going down land on the ground and bring its colour
				float facing = 0.3f + 0.7f * (h.n.z > 0 ? h.n.z : 0.0f), down = dir.z < 0 ? -dir.z : 0.0f;
				for(int c = 0; c < 3; c++) bounce[c] += facing * (1.0f + (o.bounceRgb[c] - 1.0f) * down);
			}else{
				if(bvh.anyHit(org, dir, bias, 1e9f, skyMask)) continue;
				open += 1.0f; up += dir.z > 0 ? dir.z : 0.0f;
			}
		}
		float ao = open / (float)N, el = open > 0 ? up / open : 0.0f;	// ao: visible sky; el: how much of it is high
		el = 0.5f + (el - 0.5f) * o.skyGradient;
		for(int c = 0; c < 3; c++){ day[c] = o.shadeFloor + o.skyLevel * ao * (o.skyBot[c] + (o.skyTop[c] - o.skyBot[c]) * el) + o.bounce * o.skyLevel * bounce[c] / (float)N; night[c] = o.nightFloor + o.nightLevel * ao * o.nightSky[c]; }
		for(size_t si = 0; si < suns.size(); si++){
			const Sun &S = suns[si]; float cs = dot(n, S.dir); if(cs <= 0.0f) continue;
			float vis = 0;
			if(sunRays <= 1) vis = bvh.anyHit(org, S.dir, bias, 1e9f, sunMask) ? 0.0f : 1.0f;
			else{	// a soft sun: rays spread in a cone, the set turned per point
				V3 st, sb; tangentFrame(S.dir, st, sb); int hitc = 0;
				for(int k = 0; k < sunRays; k++){ float rr = sqrtf(((float)k + 0.5f) / (float)sunRays) * sunTan, ph = 6.2831853f * radicalInverse((uint32_t)k) + ang; V3 dd = normalize(S.dir + st * (rr * cosf(ph)) + sb * (rr * sinf(ph))); if(bvh.anyHit(org, dd, bias, 1e9f, sunMask)) hitc++; }
				vis = 1.0f - (float)hitc / (float)sunRays;
			}
			if(vis <= 0.0f) continue;
			for(int c = 0; c < 3; c++) day[c] += o.sunLevel * S.w * cs * vis * S.rgb[c];
		}
		if(o.moon > 0.0f){ float cs = dot(n, moonDir); if(cs > 0.0f && !bvh.anyHit(org, moonDir, bias, 1e9f, skyMask)) for(int c = 0; c < 3; c++) night[c] += o.moon * cs * o.nightSky[c]; }
		if(o.windowsOnly) for(int c = 0; c < 3; c++) day[c] = night[c] = 0;
		float tintD[4] = { 0, 0, 0, 0 }, tintN[4] = { 0, 0, 0, 0 };	// the tint lamps: colour × weight, weight
		for(size_t li = 0; li < lights.size(); li++){
			const BakeLight &L = lights[li];
			if(!L.rig && !o.night) continue;	// «ночь от фонарей» off: the 2dfx lamps are out, the rig stays
			V3 dl = L.pos - p; float dist = length(dl); if(dist < 1e-3f) continue;
			float sd = surfRow ? surfRow[li] : dist;	// how close the lamp gets to the surface here
			if(sd >= L.range) continue;
			V3 l = dl * (1.0f / dist); float cs = dot(n, l); if(cs <= 0.0f) continue;
			if(L.shadow && bvh.anyHit(org, l, bias, dist - 0.05f)) continue;
			float f = (L.rig ? L.strength : o.lampLevel) * falloff(sd, L.range, o.falloff) * cs;
			bool toDay = L.when != 1, toNight = L.when != 0;
			if(L.mode == 1){ if(toDay){ for(int c = 0; c < 3; c++) tintD[c] += f * L.rgb[c]; tintD[3] += f; } if(toNight){ for(int c = 0; c < 3; c++) tintN[c] += f * L.rgb[c]; tintN[3] += f; } }
			else { if(toDay) for(int c = 0; c < 3; c++) day[c] += f * L.rgb[c]; if(toNight) for(int c = 0; c < 3; c++) night[c] += f * L.rgb[c]; }
		}
		// tint: the lit colour is pulled towards the lamp's colour by the weight (the doubled SA prelit would blow out if the light were added)
		if(tintD[3] > 0.0f){ float w = tintD[3] > 1.0f ? 1.0f : tintD[3]; for(int c = 0; c < 3; c++) day[c] *= 1.0f - w + w * tintD[c] / tintD[3]; }
		if(tintN[3] > 0.0f){ float w = tintN[3] > 1.0f ? 1.0f : tintN[3]; for(int c = 0; c < 3; c++) night[c] *= 1.0f - w + w * tintN[c] / tintN[3]; }
	};
	// the result curve: contrast about the vanilla mid-tone, gamma, the hand tints, the game's scale
	auto curve = [&](float *day, float *night){
		if(o.windowsOnly) return;
		const float pivot = 0.2f;
		for(int c = 0; c < 3; c++){
			float v = pivot + (day[c] - pivot) * o.contrast; if(v < 0) v = 0; if(o.gamma > 0.05f && o.gamma != 1.0f) v = powf(v, 1.0f / o.gamma); day[c] = v * o.tintDay[c] * o.scale;
			v = pivot * 0.4f + (night[c] - pivot * 0.4f) * o.contrast; if(v < 0) v = 0; if(o.gamma > 0.05f && o.gamma != 1.0f) v = powf(v, 1.0f / o.gamma); night[c] = v * o.tintNight[c] * o.scale;
		}
	};
	r.geoms.resize(d.geoms.size());
	int totalJobs = 0; for(size_t gi = 0; gi < d.geoms.size(); gi++) totalJobs += d.geoms[gi].numVerts();
	std::atomic<int> done(0); std::atomic<bool> stop(false);
	int threads = o.threads > 0 ? o.threads : (int)std::thread::hardware_concurrency(); if(threads < 1) threads = 1; if(threads > 64) threads = 64;
	for(size_t gi = 0; gi < d.geoms.size(); gi++){
		const GGeom &g = d.geoms[gi]; BakeGeom &res = r.geoms[gi];
		int nv = g.numVerts(); res.verts = nv; res.day.assign((size_t)nv, 0xFF000000u); res.night.assign((size_t)nv, 0xFF000000u);
		std::vector<V3> nrm; vertexNormals(g, o.hardAngle, nrm);
		// the jobs: every vertex, then (remedy 1) the light samples spread over the planar regions
		struct Job { V3 p, n; uint32_t seed; int tri; };	// tri: the face a planar sample sits on (-1 for a vertex)
		std::vector<Job> jobs((size_t)nv);
		for(int i = 0; i < nv; i++){ Job &j = jobs[(size_t)i]; j.p = mat ? mulPoint(mat, g.pos[(size_t)i]) : g.pos[(size_t)i]; j.n = normalize(mat ? mulDir(mat, nrm[(size_t)i]) : nrm[(size_t)i]); if(length(j.n) < 0.5f) j.n = v3(0, 0, 1); j.seed = (uint32_t)i * 2654435761u + (uint32_t)gi; j.tri = -1; }
		std::vector<Region> regions; std::vector<int> faceRegion; std::vector<int> sampleRegion; std::vector<float> sampleU, sampleV;
		if(o.planarFit){
			planarRegions(g, o.planarAngle, o.planarMinArea, regions, faceRegion);
			float sp = o.planarSpacing > 0.2f ? o.planarSpacing : 0.2f;
			for(size_t ri = 0; ri < regions.size(); ri++){
				const Region &R = regions[ri]; V3 wn = normalize(mat ? mulDir(mat, R.n) : R.n);
				for(size_t k = 0; k < R.faces.size(); k++){
					const uint32_t *v = g.tri[(size_t)R.faces[k]].v; V3 a = g.pos[v[0]], b = g.pos[v[1]], c = g.pos[v[2]];
					float area = 0.5f * length(cross(b - a, c - a)); int K = (int)(area / (sp * sp) + 0.5f); if(K < 1) K = 1; if(K > 64) K = 64;
					for(int q = 0; q < K; q++){
						float x = radicalInverse((uint32_t)q + 1), y = ((float)q + 0.5f) / (float)K; if(x + y > 1.0f){ x = 1.0f - x; y = 1.0f - y; }
						x = 0.05f + 0.9f * x; y = 0.05f + 0.9f * y; if(x + y > 0.95f){ float s = 0.95f / (x + y); x *= s; y *= s; }	// off the edges: a sample on a shared edge would sit in the neighbour's corner
						V3 lp = a + (b - a) * x + (c - a) * y;
						Job j; j.p = mat ? mulPoint(mat, lp) : lp; j.n = wn; j.seed = (uint32_t)jobs.size() * 2246822519u + (uint32_t)gi * 7u; j.tri = R.faces[k]; jobs.push_back(j);
						V3 rel = lp - R.c; sampleRegion.push_back((int)ri); sampleU.push_back(dot(rel, R.tu)); sampleV.push_back(dot(rel, R.tv));
					}
				}
			}
			totalJobs += (int)jobs.size() - nv;
		}
		// the distance from every lamp to the surface around each job: on a coarse mesh (a road tile with four
		// corners) a lamp standing in the middle of a face is metres away from every vertex, and measuring to the
		// vertices alone would leave the tile black although the light plainly covers it
		std::vector<float> surf;
		if(o.lampSurface && !lights.empty()){
			std::vector<V3> wp((size_t)nv);
			for(int i = 0; i < nv; i++) wp[(size_t)i] = mat ? mulPoint(mat, g.pos[(size_t)i]) : g.pos[(size_t)i];
			surf.assign(jobs.size() * lights.size(), 1e30f);
			for(size_t t = 0; t < g.tri.size(); t++){
				const uint32_t *iv = g.tri[t].v;
				if(iv[0] >= (uint32_t)nv || iv[1] >= (uint32_t)nv || iv[2] >= (uint32_t)nv) continue;
				for(size_t li = 0; li < lights.size(); li++){
					V3 lp = lights[li].pos;
					V3 q = closestOnTri(lp, wp[iv[0]], wp[iv[1]], wp[iv[2]]);
					float dist = length(lp - q);
					for(int k = 0; k < 3; k++){ float &cur = surf[(size_t)iv[k] * lights.size() + li]; if(dist < cur) cur = dist; }
				}
			}
			for(size_t ji = (size_t)nv; ji < jobs.size(); ji++){	// a planar sample: the face it sits on
				int t = jobs[ji].tri;
				if(t < 0 || (size_t)t >= g.tri.size()){ for(size_t li = 0; li < lights.size(); li++) surf[ji * lights.size() + li] = length(lights[li].pos - jobs[ji].p); continue; }
				const uint32_t *iv = g.tri[(size_t)t].v;
				for(size_t li = 0; li < lights.size(); li++){
					V3 lp = lights[li].pos;
					V3 q = closestOnTri(lp, wp[iv[0]], wp[iv[1]], wp[iv[2]]);
					surf[ji * lights.size() + li] = length(lp - q);
				}
			}
		}
		std::vector<float> raw(jobs.size() * 6, 0.0f);	// day rgb, night rgb per job, before the curve
		std::atomic<int> next(0);
		auto work = [&](){
			for(;;){
				int i = next.fetch_add(1); if(i >= (int)jobs.size()) break;
				if(stop.load()) break;
				if(cancel && *cancel){ stop.store(true); break; }
				const Job &j = jobs[(size_t)i];
				evalPoint(j.p, j.n, j.seed, &raw[(size_t)i * 6], &raw[(size_t)i * 6 + 3], surf.empty() ? nullptr : &surf[(size_t)i * lights.size()]);
				int k = done.fetch_add(1) + 1; if(progress && (k & 63) == 0) *progress = (float)k / (float)(totalJobs > 0 ? totalJobs : 1);
			}
		};
		std::vector<std::thread> pool;
		for(int k = 1; k < threads; k++) pool.push_back(std::thread(work));
		work();
		for(size_t k = 0; k < pool.size(); k++) pool[k].join();
		if(stop.load()){ r.err = T("отменено"); return false; }
		std::vector<float> dayF((size_t)nv * 3), nightF((size_t)nv * 3);
		for(int i = 0; i < nv; i++) for(int c = 0; c < 3; c++){ dayF[(size_t)i * 3 + c] = raw[(size_t)i * 6 + c]; nightF[(size_t)i * 3 + c] = raw[(size_t)i * 6 + 3 + c]; }
		if(o.planarFit && !regions.empty()){
			// one linear function per region and channel; a vertex of the region takes the fitted value (clamped to the samples'
			// range); a vertex shared by several regions (a corner) — the area-weighted mean of their fits
			std::vector<float> accD((size_t)nv * 3, 0.0f), accN((size_t)nv * 3, 0.0f), accW((size_t)nv, 0.0f);
			std::vector<std::vector<int> > regSamples(regions.size());
			for(size_t s = 0; s < sampleRegion.size(); s++) regSamples[(size_t)sampleRegion[s]].push_back((int)s);
			for(size_t ri = 0; ri < regions.size(); ri++){
				const Region &R = regions[ri]; const std::vector<int> &ss = regSamples[ri]; if(ss.empty()) continue;
				std::vector<float> u(ss.size()), v(ss.size()), vals(ss.size() * 6); float lo[6], hi[6]; for(int c = 0; c < 6; c++){ lo[c] = 1e30f; hi[c] = -1e30f; }
				for(size_t k = 0; k < ss.size(); k++){ int s = ss[k]; u[k] = sampleU[(size_t)s]; v[k] = sampleV[(size_t)s]; const float *rv = &raw[((size_t)nv + (size_t)s) * 6]; for(int c = 0; c < 6; c++){ vals[k * 6 + (size_t)c] = rv[c]; lo[c] = vmin(lo[c], rv[c]); hi[c] = vmax(hi[c], rv[c]); } }
				float abc[6][3]; for(int c = 0; c < 6; c++) fitPlane(u, v, vals.data(), 6, c, abc[c]);
				std::vector<char> seen((size_t)nv, 0);
				for(size_t k = 0; k < R.faces.size(); k++) for(int q = 0; q < 3; q++){
					uint32_t vi = g.tri[(size_t)R.faces[k]].v[q]; if(seen[vi]) continue; seen[vi] = 1;
					V3 rel = g.pos[vi] - R.c; float uu = dot(rel, R.tu), vv = dot(rel, R.tv);
					for(int c = 0; c < 6; c++){ float val = abc[c][0] + abc[c][1] * uu + abc[c][2] * vv; if(val < lo[c]) val = lo[c]; if(val > hi[c]) val = hi[c]; if(c < 3) accD[vi * 3 + (size_t)c] += val * R.area; else accN[vi * 3 + (size_t)(c - 3)] += val * R.area; }
					accW[vi] += R.area;
				}
			}
			for(int i = 0; i < nv; i++) if(accW[(size_t)i] > 0) for(int c = 0; c < 3; c++){ dayF[(size_t)i * 3 + c] = accD[(size_t)i * 3 + c] / accW[(size_t)i]; nightF[(size_t)i * 3 + c] = accN[(size_t)i * 3 + c] / accW[(size_t)i]; }
			r.regions += (int)regions.size();
		}
		for(int i = 0; i < nv; i++) curve(&dayF[(size_t)i * 3], &nightF[(size_t)i * 3]);

        // Match only the added window light at duplicated vertices. Original prelit is added later.
        auto weldWindowLight = [&](){
            if(!o.windowsOnly || o.smoothIter <= 0) return;
            std::map<std::tuple<long long,long long,long long>, std::vector<int> > groups;
            for(int i=0;i<nv;i++){ const V3 &p=g.pos[(size_t)i]; groups[std::make_tuple(llround(p.x*10000.0),llround(p.y*10000.0),llround(p.z*10000.0))].push_back(i); }
            for(const auto &group:groups){ if(group.second.size()<2) continue; float sum[3]={0,0,0};
                for(int i:group.second) for(int c=0;c<3;c++) sum[c]+=nightF[(size_t)i*3+c];
                for(int i:group.second) for(int c=0;c<3;c++) nightF[(size_t)i*3+c]=sum[c]/(float)group.second.size();
            }
        };
        weldWindowLight();
		// neighbour smoothing (across UV seams: by position), then the mix with the colours the file had
		if(o.smoothIter > 0){
			std::vector<std::vector<int> > adj; positionAdjacency(g, adj);
			for(int it = 0; it < o.smoothIter; it++){
				std::vector<float> d2 = dayF, n2 = nightF;
				for(int i = 0; i < nv; i++){ const std::vector<int> &a = adj[(size_t)i]; if(a.empty()) continue; float sd[3] = { 0, 0, 0 }, sn[3] = { 0, 0, 0 }; for(size_t k = 0; k < a.size(); k++) for(int c = 0; c < 3; c++){ sd[c] += dayF[(size_t)a[k] * 3 + c]; sn[c] += nightF[(size_t)a[k] * 3 + c]; } for(int c = 0; c < 3; c++){ d2[(size_t)i * 3 + c] = 0.5f * dayF[(size_t)i * 3 + c] + 0.5f * sd[c] / (float)a.size(); n2[(size_t)i * 3 + c] = 0.5f * nightF[(size_t)i * 3 + c] + 0.5f * sn[c] / (float)a.size(); } }
				dayF.swap(d2); nightF.swap(n2); weldWindowLight();
			}
		}
		for(int i = 0; i < nv; i++){
			float day[3] = { dayF[(size_t)i * 3], dayF[(size_t)i * 3 + 1], dayF[(size_t)i * 3 + 2] }, night[3] = { nightF[(size_t)i * 3], nightF[(size_t)i * 3 + 1], nightF[(size_t)i * 3 + 2] };
			uint8_t alpha = g.prelit ? (uint8_t)(g.day[(size_t)i] >> 24) : 255;
			if(g.prelit && o.keepOld > 0.0f){ float old[3]; unpackRgb(g.day[(size_t)i], old); for(int c = 0; c < 3; c++) day[c] = old[c] * o.keepOld + day[c] * (1.0f - o.keepOld); }
			if(g.night && o.keepOld > 0.0f){ float old[3]; unpackRgb(g.nightCol[(size_t)i], old); for(int c = 0; c < 3; c++) night[c] = old[c] * o.keepOld + night[c] * (1.0f - o.keepOld); }
			if(g.night && o.keepBright > 0.0f && lum(g.nightCol[(size_t)i]) >= o.keepBright){ unpackRgb(g.nightCol[(size_t)i], night); }	// the glowing windows stay as they were
			if(o.windowsOnly){
				uint32_t d0 = g.day.size() > (size_t)i ? g.day[(size_t)i] : 0xFF808080u;
				uint32_t n0 = g.nightCol.size() > (size_t)i ? g.nightCol[(size_t)i] : d0;
				unpackRgb(d0, day); float old[3]; unpackRgb(n0, old);
				for(int c = 0; c < 3; c++) night[c] = old[c] + nightF[(size_t)i * 3 + c];
			}
			char window = gi < winVert.size() && (size_t)i < winVert[gi].size() ? winVert[gi][(size_t)i] : 0;
			if(window == 1) for(int c = 0; c < 3; c++){
				float e = o.winEmit * o.winRgb[c];
				if(o.windowsOnly || night[c] < e) night[c] = e;
			}
			res.day[(size_t)i] = packRgba(day, alpha); res.night[(size_t)i] = packRgba(night, alpha);
			if(o.windowsOnly){
				if(g.day.size() > (size_t)i) res.day[(size_t)i] = g.day[(size_t)i];
				if(window != 1 && o.winLight <= 0 && g.nightCol.size() > (size_t)i) res.night[(size_t)i] = g.nightCol[(size_t)i];
			}
		}
	}
	if(progress) *progress = 1.0f;
	BakeStats(r);
	if(!BakeWriteColours(dff, r.geoms, r.dffOut, r.err)) return false;
	if(!r.hadPrelit) r.log.push_back(T("у модели не было цветов вершин — блок prelit добавлен (флаг 0x8)"));
	if(!r.hadNight) r.log.push_back(T("ночных цветов не было — добавлен чанк 0x253F2F9"));
	if(o.planarFit) r.log.push_back(fmt(T("плоских участков с общим градиентом: %d"), r.regions));
	if(r.lamps == 0 && o.night && o.lights.empty()) r.log.push_back(T("фонарей (2dfx с радиусом) в радиусе нет — ночь только от неба"));
	if(!o.winTex.empty()) r.log.push_back(r.winFaces ? fmt(T("светящихся окон: граней %d, фонарей от них %d"), r.winFaces, r.winLamps) : T("текстуры окон выбраны, но таких материалов в модели нет"));
	if(!o.lights.empty()) r.log.push_back(fmt(T("своих фонарей: %d (%s)"), (int)o.lights.size(), o.lights[0].mode == 1 ? T("окрашивают") : T("светят")));
	if(o.keepOld > 0.0f && !r.hadPrelit) r.log.push_back(T("«сохранить старый prelit»: у модели его не было — смешивать не с чем"));
	if(o.keepBright > 0.0f && !r.hadNight) r.log.push_back(T("«не трогать яркие ночью»: ночных цветов не было — нечего оставлять"));
	r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	return true;
}

}	// namespace gc
