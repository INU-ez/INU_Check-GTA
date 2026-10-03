// gtacheck — зоны и сплайны запекания света карты.
//
// The prelit bake of one model (prelit_bake.cpp) works on a placed copy. Baking a whole district needs two more
// things, both of which the user must be able to place in 3D and get back at the next start:
//   * a box: everything standing inside it is baked (drag its sides, several boxes per map);
//   * a spline: a line of light that exists only for the bake — lamps are spread along it and light whatever is
//     in range, imitating street lighting, a neon front or a sunset side without touching the game files.
// Both live in a plain text file next to the game (inu_check\\bake.txt) so they survive restarts and can be edited
// or shared by hand. Nothing here touches the game's own files.
#include "gtacheck.h"

#include <math.h>
#include <stdio.h>

namespace gc {

// ------------------------------------------------------------------ boxes ---

bool BakeBoxContains(const BakeBox &b, const float *p)
{
	float dx = p[0] - b.centre[0], dy = p[1] - b.centre[1], dz = p[2] - b.centre[2];
	if(b.rot != 0.0f){	// the box turns about Z
		float c = cosf(-b.rot), s = sinf(-b.rot);
		float x = dx * c - dy * s, y = dx * s + dy * c;
		dx = x; dy = y;
	}
	return fabsf(dx) <= b.size[0] * 0.5f && fabsf(dy) <= b.size[1] * 0.5f && fabsf(dz) <= b.size[2] * 0.5f;
}

// the eight corners, world space (the 3D view draws them)
void BakeBoxCorners(const BakeBox &b, float out[8][3])
{
	float hx = b.size[0] * 0.5f, hy = b.size[1] * 0.5f, hz = b.size[2] * 0.5f;
	float c = cosf(b.rot), s = sinf(b.rot);
	for(int i = 0; i < 8; i++){
		float x = (i & 1) ? hx : -hx, y = (i & 2) ? hy : -hy, z = (i & 4) ? hz : -hz;
		out[i][0] = b.centre[0] + x * c - y * s;
		out[i][1] = b.centre[1] + x * s + y * c;
		out[i][2] = b.centre[2] + z;
	}
}

// --------------------------------------------------------------- splines ---

// Catmull-Rom through the nodes; with two nodes it is the straight segment between them
static V3 splinePoint(const std::vector<BakeNode> &pts, float t)
{
	int n = (int)pts.size();
	if(n == 0){ V3 z = { 0, 0, 0 }; return z; }
	if(n == 1){ V3 p = { pts[0].p[0], pts[0].p[1], pts[0].p[2] }; return p; }
	float ft = t * (float)(n - 1);
	int i = (int)ft; if(i > n - 2) i = n - 2; if(i < 0) i = 0;
	float u = ft - (float)i;
	const float *p1 = pts[(size_t)i].p, *p2 = pts[(size_t)(i + 1)].p;
	const float *p0 = pts[(size_t)(i > 0 ? i - 1 : i)].p, *p3 = pts[(size_t)(i + 2 < n ? i + 2 : n - 1)].p;
	V3 r;
	float *o = &r.x;
	for(int c = 0; c < 3; c++)
		o[c] = 0.5f * ((2.0f * p1[c]) + (-p0[c] + p2[c]) * u + (2.0f * p0[c] - 5.0f * p1[c] + 4.0f * p2[c] - p3[c]) * u * u +
		               (-p0[c] + 3.0f * p1[c] - 3.0f * p2[c] + p3[c]) * u * u * u);
	return r;
}

float BakeSplineLength(const BakeSpline &sp)
{
	if(sp.pts.size() < 2) return 0.0f;
	const int N = 64 * (int)sp.pts.size();
	float len = 0; V3 prev = splinePoint(sp.pts, 0.0f);
	for(int i = 1; i <= N; i++){
		V3 q = splinePoint(sp.pts, (float)i / (float)N);
		float dx = q.x - prev.x, dy = q.y - prev.y, dz = q.z - prev.z;
		len += sqrtf(dx * dx + dy * dy + dz * dz);
		prev = q;
	}
	return len;
}

void BakeSplinePoints(const BakeSpline &sp, int n, std::vector<V3> &out)
{
	out.clear();
	if(sp.pts.empty()) return;
	if(n < 2) n = 2;
	for(int i = 0; i < n; i++) out.push_back(splinePoint(sp.pts, (float)i / (float)(n - 1)));
}

// the lamps the spline stands for: one every `step` metres along it (at least one per node)
void BakeSplineLights(const BakeSpline &sp, std::vector<BakeLight> &out)
{
	if(!sp.on || sp.pts.empty() || sp.strength <= 0.0f || sp.range <= 0.01f) return;
	float step = sp.step > 0.5f ? sp.step : 0.5f;
	float len = BakeSplineLength(sp);
	int n = sp.pts.size() < 2 ? 1 : (int)(len / step) + 1;
	if(n > 4000) n = 4000;	// a very long line still stays within reason
	std::vector<V3> pts;
	BakeSplinePoints(sp, n < 2 ? 2 : n, pts);
	for(int i = 0; i < n && i < (int)pts.size(); i++){
		BakeLight L;
		L.pos = pts[(size_t)i];
		memcpy(L.rgb, sp.rgb, sizeof(L.rgb));
		L.range = sp.range; L.strength = sp.strength; L.when = sp.when; L.mode = sp.mode; L.shadow = sp.shadow;
		L.rig = true;	// it carries its own strength / time of day, like the rig lamps around a model
		L.local = L.aim = L.pos;
		out.push_back(L);
	}
}

// every spline's lamps that can reach a point (the copy being baked): the whole map's lines would otherwise be
// walked for every vertex
// a lamp as point lights: a point is one, a volume is a grid of them inside the box / sphere so the light does
// not all come from one spot (Itera's «Sphere Volume» / «Box Volume» do the same thing in the node tree)
void BakeLampLights(const BakeLamp &l, std::vector<BakeLight> &out)
{
	if(!l.on) return;
	BakeLight b;
	for(int c = 0; c < 3; c++) b.rgb[c] = l.rgb[c];
	b.range = l.range; b.strength = l.strength; b.when = l.when; b.mode = l.mode; b.shadow = l.shadow; b.rig = true;
	b.falloff = l.falloff; b.offset = l.offset; b.twoSided = l.twoSided;
	if(l.type == LAMP_SPOT){
		b.spot = 1;
		float d[3] = { l.dir[0], l.dir[1], l.dir[2] };
		float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if(len < 1e-4f){ d[0] = d[1] = 0; d[2] = -1; len = 1; }
		b.dir.x = d[0] / len; b.dir.y = d[1] / len; b.dir.z = d[2] / len;
		float half = l.spotSize * 0.5f; if(half < 0.02f) half = 0.02f; if(half > 1.55f) half = 1.55f;
		b.spotCos = cosf(half); b.spotBlend = l.spotBlend;
	}
	if(l.type == LAMP_POINT || l.type == LAMP_SPOT){
		b.pos.x = l.pos[0]; b.pos.y = l.pos[1]; b.pos.z = l.pos[2];
		b.local = b.aim = b.pos;
		out.push_back(b);
		return;
	}
	int n[3];
	for(int c = 0; c < 3; c++){ n[c] = (int)(l.size[c] / 4.0f) + 1; if(n[c] > 4) n[c] = 4; }
	int total = n[0] * n[1] * n[2]; if(total < 1) total = 1;
	b.strength = l.strength / (float)total;
	for(int i = 0; i < n[0]; i++) for(int k = 0; k < n[1]; k++) for(int m = 0; m < n[2]; m++){
		float f[3] = { n[0] > 1 ? (float)i / (n[0] - 1) - 0.5f : 0.0f, n[1] > 1 ? (float)k / (n[1] - 1) - 0.5f : 0.0f, n[2] > 1 ? (float)m / (n[2] - 1) - 0.5f : 0.0f };
		float p[3] = { l.pos[0] + f[0] * l.size[0], l.pos[1] + f[1] * l.size[1], l.pos[2] + f[2] * l.size[2] };
		if(l.type == LAMP_SPHERE){	// inside the ellipsoid only
			float e = 0;
			for(int c = 0; c < 3; c++){ float h = l.size[c] * 0.5f; if(h > 0.001f){ float d = (p[c] - l.pos[c]) / h; e += d * d; } }
			if(e > 1.0f) continue;
		}
		b.pos.x = p[0]; b.pos.y = p[1]; b.pos.z = p[2];
		b.local = b.aim = b.pos;
		out.push_back(b);
	}
}

void BakeSetupLightsNear(const BakeSetup &s, const float *centre, float radius, std::vector<BakeLight> &out)
{
	for(size_t i = 0; i < s.lamps.size(); i++){
		std::vector<BakeLight> ls;
		BakeLampLights(s.lamps[i], ls);
		for(size_t k = 0; k < ls.size(); k++){
			float dx = ls[k].pos.x - centre[0], dy = ls[k].pos.y - centre[1], dz = ls[k].pos.z - centre[2];
			float reach = ls[k].range + radius;
			if(dx * dx + dy * dy + dz * dz <= reach * reach) out.push_back(ls[k]);
		}
	}
	for(size_t i = 0; i < s.splines.size(); i++){
		std::vector<BakeLight> ls;
		BakeSplineLights(s.splines[i], ls);
		for(size_t k = 0; k < ls.size(); k++){
			float dx = ls[k].pos.x - centre[0], dy = ls[k].pos.y - centre[1], dz = ls[k].pos.z - centre[2];
			float reach = ls[k].range + radius;
			if(dx * dx + dy * dy + dz * dz <= reach * reach) out.push_back(ls[k]);
		}
	}
}

// ------------------------------------------------------------ the file ---

std::string BakeSetupPath(const std::string &root) { return joinPath(InuDir(root), "bake.txt"); }

bool BakeSetupSave(const std::string &root, const BakeSetup &s, std::string &err)
{
	std::string path = BakeSetupPath(root);
	FILE *f = openWriteUtf8(path);
	if(!f){ err = T("не удалось открыть файл на запись: ") + path; return false; }
	fprintf(f, "# gta_check: зоны и сплайны запекания света. Правится руками; координаты игровые (x y z).\n");
	if(!s.opt.empty()) fprintf(f, "opt %s\n", s.opt.c_str());
	for(size_t i = 0; i < s.boxes.size(); i++){
		const BakeBox &b = s.boxes[i];
		fprintf(f, "box %s | %.2f %.2f %.2f | %.2f %.2f %.2f | %.4f | %d\n", b.name.c_str(),
		        b.centre[0], b.centre[1], b.centre[2], b.size[0], b.size[1], b.size[2], b.rot, b.on ? 1 : 0);
	}
	for(size_t i = 0; i < s.lamps.size(); i++){
		const BakeLamp &l = s.lamps[i];
		fprintf(f, "lamp %s | %d | %.2f %.2f %.2f | %.2f %.2f %.2f | %02x%02x%02x | %.2f %.2f | %d %d %d %d | %d %.2f %d | %.3f %.3f %.3f %.4f %.3f\n", l.name.c_str(), l.type,
		        l.pos[0], l.pos[1], l.pos[2], l.size[0], l.size[1], l.size[2],
		        (int)(l.rgb[0] * 255), (int)(l.rgb[1] * 255), (int)(l.rgb[2] * 255),
		        l.range, l.strength, l.when, l.mode, l.shadow ? 1 : 0, l.on ? 1 : 0,
		        l.falloff, l.offset, l.twoSided ? 1 : 0,
		        l.dir[0], l.dir[1], l.dir[2], l.spotSize, l.spotBlend);
	}
	for(size_t i = 0; i < s.splines.size(); i++){
		const BakeSpline &sp = s.splines[i];
		fprintf(f, "spline %s | %02x%02x%02x | %.2f %.2f %.2f | %d %d %d %d |", sp.name.c_str(),
		        (int)(sp.rgb[0] * 255), (int)(sp.rgb[1] * 255), (int)(sp.rgb[2] * 255),
		        sp.range, sp.strength, sp.step, sp.when, sp.mode, sp.shadow ? 1 : 0, sp.on ? 1 : 0);
		for(size_t k = 0; k < sp.pts.size(); k++) fprintf(f, " %.2f %.2f %.2f", sp.pts[k].p[0], sp.pts[k].p[1], sp.pts[k].p[2]);
		fprintf(f, "\n");
	}
	fclose(f);
	return true;
}

static std::vector<std::string> splitBars(const std::string &s)
{
	std::vector<std::string> out; size_t p = 0;
	for(;;){
		size_t q = s.find('|', p);
		out.push_back(trim(s.substr(p, q == std::string::npos ? std::string::npos : q - p)));
		if(q == std::string::npos) break;
		p = q + 1;
	}
	return out;
}

bool BakeSetupLoad(const std::string &root, BakeSetup &out, std::string &err)
{
	out = BakeSetup();
	std::vector<std::string> lines;
	if(!readTextLines(BakeSetupPath(root), lines)){ err = T("файла нет"); return false; }
	for(size_t i = 0; i < lines.size(); i++){
		std::string l = trim(lines[i]);
		if(l.empty() || l[0] == '#') continue;
		size_t sp = l.find(' ');
		std::string kw = lower(l.substr(0, sp));
		std::string rest = sp == std::string::npos ? "" : trim(l.substr(sp + 1));
		if(kw == "opt"){ out.opt = rest; continue; }
		if(kw == "box"){
			std::vector<std::string> t = splitBars(rest);
			if(t.size() < 4) continue;
			BakeBox b; b.name = t[0];
			sscanf(t[1].c_str(), "%f %f %f", &b.centre[0], &b.centre[1], &b.centre[2]);
			sscanf(t[2].c_str(), "%f %f %f", &b.size[0], &b.size[1], &b.size[2]);
			b.rot = t.size() > 3 ? (float)atof(t[3].c_str()) : 0.0f;
			b.on = t.size() > 4 ? atoi(t[4].c_str()) != 0 : true;
			for(int c = 0; c < 3; c++) if(b.size[c] < 1.0f) b.size[c] = 1.0f;
			out.boxes.push_back(b);
			continue;
		}
		if(kw == "lamp"){
			std::vector<std::string> t = splitBars(rest);
			if(t.size() < 6) continue;
			BakeLamp l; l.name = t[0];
			l.type = atoi(t[1].c_str());
			sscanf(t[2].c_str(), "%f %f %f", &l.pos[0], &l.pos[1], &l.pos[2]);
			sscanf(t[3].c_str(), "%f %f %f", &l.size[0], &l.size[1], &l.size[2]);
			unsigned rgb = (unsigned)strtoul(t[4].c_str(), nullptr, 16);
			l.rgb[0] = ((rgb >> 16) & 255) / 255.0f; l.rgb[1] = ((rgb >> 8) & 255) / 255.0f; l.rgb[2] = (rgb & 255) / 255.0f;
			sscanf(t[5].c_str(), "%f %f", &l.range, &l.strength);
			int shadow = 1, on = 1;
			if(t.size() > 6) sscanf(t[6].c_str(), "%d %d %d %d", &l.when, &l.mode, &shadow, &on);
			l.shadow = shadow != 0; l.on = on != 0;
			if(t.size() > 7){ int ts = 0; sscanf(t[7].c_str(), "%d %f %d", &l.falloff, &l.offset, &ts); l.twoSided = ts != 0; }
			if(t.size() > 8) sscanf(t[8].c_str(), "%f %f %f %f %f", &l.dir[0], &l.dir[1], &l.dir[2], &l.spotSize, &l.spotBlend);
			if(l.range <= 0.0f) l.range = 12.0f;
			if(l.falloff < 0 || l.falloff > 3) l.falloff = 0;
			if(l.spotSize < 0.05f || l.spotSize > 3.1f) l.spotSize = 0.785f;
			if(l.type < LAMP_POINT || l.type > LAMP_SPOT) l.type = LAMP_POINT;
			out.lamps.push_back(l);
			continue;
		}
		if(kw == "spline"){
			std::vector<std::string> t = splitBars(rest);
			if(t.size() < 5) continue;
			BakeSpline s; s.name = t[0];
			unsigned rgb = (unsigned)strtoul(t[1].c_str(), nullptr, 16);
			s.rgb[0] = ((rgb >> 16) & 255) / 255.0f; s.rgb[1] = ((rgb >> 8) & 255) / 255.0f; s.rgb[2] = (rgb & 255) / 255.0f;
			sscanf(t[2].c_str(), "%f %f %f", &s.range, &s.strength, &s.step);
			int shadow = 1, on = 1;
			sscanf(t[3].c_str(), "%d %d %d %d", &s.when, &s.mode, &shadow, &on);
			s.shadow = shadow != 0; s.on = on != 0;
			const char *p = t[4].c_str();
			while(*p){
				BakeNode n;
				int used = 0;
				if(sscanf(p, "%f %f %f%n", &n.p[0], &n.p[1], &n.p[2], &used) != 3) break;
				s.pts.push_back(n);
				p += used;
				while(*p == ' ') p++;
			}
			if(s.step <= 0.0f) s.step = 6.0f;
			if(s.range <= 0.0f) s.range = 20.0f;
			out.splines.push_back(s);
			continue;
		}
	}
	return true;
}

}	// namespace gc
