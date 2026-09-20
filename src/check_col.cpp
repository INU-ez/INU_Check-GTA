// gtacheck — COL archive parser + the COL-nn rules from E:\RE\addon_check\col_path.md.
#include "gtacheck.h"

#include <math.h>
#include <algorithm>

namespace gc {

static const uint32_t FCC_COLL = 0x4C4C4F43, FCC_COL2 = 0x324C4F43, FCC_COL3 = 0x334C4F43, FCC_COL4 = 0x344C4F43;
static const int MAX_SURFACE = 178;

struct CP {
	Context *ctx;
	const std::string *where;
	std::string obj;
	int id;
	int index;
	bool fatal;
	void add(Severity s, const char *code, const std::string &msg, const std::string &detail = "")
	{ ctx->add(s, CAT_COL, code, *where, index, obj, msg, detail, id); if(s == SEV_FATAL) fatal = true; }
};

static void checkSurface(CP &p, int surf, const char *what, int &reported)
{
	if(surf > MAX_SURFACE && p.ctx->gd->isSA() && reported++ < 3)
		p.add(SEV_ERROR, "COL-18", fmt("%s: id поверхности %d > 178 — SurfaceInfos читается за пределами таблицы (трение/звук из мусора)", what, surf));
}

struct Bounds { float bmin[3], bmax[3], c[3], r; };

static void checkBoundsPoint(const Bounds &b, const float *v, bool &outBox, bool &outSphere)
{
	for(int k = 0; k < 3; k++) if(v[k] < b.bmin[k] - 0.05f || v[k] > b.bmax[k] + 0.05f) outBox = true;
	float dx = v[0] - b.c[0], dy = v[1] - b.c[1], dz = v[2] - b.c[2];
	if(sqrtf(dx*dx + dy*dy + dz*dz) > b.r * 1.10f + 0.5f) outSphere = true;
}
// a collision sphere: its extent along the axes for the box test, its far point for the sphere test
static void checkBoundsSphere(const Bounds &b, const float *c, float r, bool &outBox, bool &outSphere)
{
	for(int k = 0; k < 3; k++) if(c[k] - r < b.bmin[k] - 0.05f || c[k] + r > b.bmax[k] + 0.05f) outBox = true;
	float dx = c[0] - b.c[0], dy = c[1] - b.c[1], dz = c[2] - b.c[2];
	if(sqrtf(dx*dx + dy*dy + dz*dz) + r > b.r * 1.10f + 0.5f) outSphere = true;
}

// COL1: byte-packed sequential payload
static void parseCol1(CP &p, Buf &b, size_t payloadEnd, ColEntry &ce)
{
	GameData &gd = *p.ctx->gd;
	Bounds bd;
	bd.r = b.f32(); for(int k = 0; k < 3; k++) bd.c[k] = b.f32();
	for(int k = 0; k < 3; k++) bd.bmin[k] = b.f32();
	for(int k = 0; k < 3; k++) bd.bmax[k] = b.f32();
	ce.radius = bd.r; memcpy(ce.center, bd.c, 12); memcpy(ce.bmin, bd.bmin, 12); memcpy(ce.bmax, bd.bmax, 12);
	int reported = 0;
	bool outBox = false, outSphere = false;
	int32_t numSpheres = b.i32();
	if((int16_t)numSpheres != numSpheres) p.add(SEV_ERROR, "COL-12", fmt("COL1: numSpheres %d читается как int16 — секции сместятся", numSpheres));
	int ns = (int16_t)numSpheres > 0 ? (int16_t)numSpheres : 0;
	ce.numSpheres = ns;
	for(int i = 0; i < ns && b.ok; i++){
		float r = b.f32(); float c[3]; for(int k = 0; k < 3; k++) c[k] = b.f32();
		int mat = b.u8(); b.u8(); b.u8(); b.u8();
		checkSurface(p, mat, "сфера", reported);
		checkBoundsSphere(bd, c, r, outBox, outSphere);
	}
	int32_t numLines = b.i32();
	int nl = (int8_t)(numLines & 0xFF);
	if(numLines != 0 && (nl <= 0)) p.add(SEV_ERROR, "COL-12", fmt("COL1: numLines %d — движок берёт младший байт со знаком (%d) и не пропустит данные линий", numLines, nl));
	if(nl > 0) b.skip((size_t)nl * 24);
	int32_t numBoxes = b.i32();
	if((int16_t)numBoxes != numBoxes) p.add(SEV_ERROR, "COL-12", fmt("COL1: numBoxes %d читается как int16", numBoxes));
	int nb = (int16_t)numBoxes > 0 ? (int16_t)numBoxes : 0;
	ce.numBoxes = nb;
	for(int i = 0; i < nb && b.ok; i++){
		float mn[3], mx[3];
		for(int k = 0; k < 3; k++) mn[k] = b.f32();
		for(int k = 0; k < 3; k++) mx[k] = b.f32();
		int mat = b.u8(); b.u8(); b.u8(); b.u8();
		checkSurface(p, mat, "бокс", reported);
		if(mn[0] > mx[0] || mn[1] > mx[1] || mn[2] > mx[2]) p.add(SEV_INFO, "COL-33", fmt("бокс %d: min > max — никогда не пересечётся", i));
		checkBoundsPoint(bd, mn, outBox, outSphere); checkBoundsPoint(bd, mx, outBox, outSphere);
	}
	int32_t numVerts = b.i32();
	if(numVerts < 0 || (size_t)numVerts * 12 > b.left()){ p.add(SEV_FATAL, "COL-05", fmt("COL1: numVertices %d не помещается в запись", numVerts)); return; }
	ce.numVerts = numVerts;
	int coordBad = 0;
	std::vector<float> verts((size_t)numVerts * 3);
	for(int i = 0; i < numVerts && b.ok; i++){
		for(int k = 0; k < 3; k++){
			float v = b.f32();
			verts[(size_t)i * 3 + k] = v;
			if(fabsf(v) >= 256.0f) coordBad++;
		}
		checkBoundsPoint(bd, &verts[(size_t)i * 3], outBox, outSphere);
	}
	if(coordBad) p.add(SEV_ERROR, "COL-13", fmt("COL1: %d координат вне ±255.99 — при загрузке *128 → int16 заворачивается на 512 м", coordBad));
	int32_t numFaces = b.i32();
	if((int16_t)numFaces != numFaces || numFaces >= 32768) p.add(SEV_FATAL, "COL-11", fmt("COL1: numFaces %d ≥ 32768 — счётчик int16, все треугольники игнорируются, m_pTriangles = NULL при флаге", numFaces));
	int nf = (int16_t)numFaces > 0 ? (int16_t)numFaces : 0;
	ce.numFaces = nf;
	if((size_t)nf * 16 > b.left()){ p.add(SEV_FATAL, "COL-05", fmt("COL1: %d граней не помещаются в запись", nf)); return; }
	int badIdx = 0, planeBad = 0;
	for(int i = 0; i < nf && b.ok; i++){
		int32_t a = b.i32(), bb = b.i32(), c = b.i32();
		int mat = b.u8(); b.u8(); b.u8(); b.u8();
		checkSurface(p, mat, "грань", reported);
		int ia = a & 0xFFFF, ib = bb & 0xFFFF, ic = c & 0xFFFF;
		if(a >= numVerts || bb >= numVerts || c >= numVerts || a < 0 || bb < 0 || c < 0 || ia >= numVerts || ib >= numVerts || ic >= numVerts) badIdx++;
		else{
			const float *va = &verts[(size_t)ia*3], *vb = &verts[(size_t)ib*3], *vc = &verts[(size_t)ic*3];
			float e1[3] = { vb[0]-va[0], vb[1]-va[1], vb[2]-va[2] }, e2[3] = { vc[0]-va[0], vc[1]-va[1], vc[2]-va[2] };
			float n[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0] };
			float ln = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
			if(ln > 1e-9f){ float dd = (n[0]*va[0]+n[1]*va[1]+n[2]*va[2]) / ln; if(fabsf(dd) >= 256.0f) planeBad++; }
		}
	}
	if(badIdx) p.add(SEV_ERROR, "COL-09", fmt("COL1: %d граней с индексом вершины ≥ %d — плоскости из мусора за массивом", badIdx, numVerts));
	if(planeBad) p.add(SEV_WARN, "COL-14", fmt("%d граней с |n·v| ≥ 256 — расстояние плоскости не влезает в int16 (заворачивается)", planeBad));
	ce.hasVolumes = ns || nb || nf;
	if(b.pos > payloadEnd) p.add(SEV_FATAL, "COL-05", "COL1: данные выходят за размер записи (size занижен)");
	if(outBox) p.add(SEV_WARN, "COL-23", "ограничивающий бокс не накрывает примитивы — объект попадает не во все секторы мира (нет коллизии в части объекта)");
	if(outSphere) p.add(SEV_ERROR, "COL-23", "ограничивающая сфера не накрывает примитивы — DFF будет отсекаться на экране, широкая фаза коллизии пропустит контакты");
}

// COL2/3/4: header + raw blob with offsets relative to (entry base + 4)
static void parseCol23(CP &p, const uint8_t *entry, size_t entryLen, int version, ColEntry &ce)
{
	// entryLen = 8 + size (whole entry incl. fourcc)
	GameData &gd = *p.ctx->gd;
	size_t hdrSize = version == 2 ? 0x4C : (version == 3 ? 0x58 : 0x5C);
	if(entryLen < 32 + hdrSize){
		p.add(SEV_FATAL, "COL-06", fmt("COL%d: size %u меньше заголовка (24 + 0x%X) — dataSize−0x%X заворачивается, Malloc(огромное) → NULL → краш", version, (unsigned)(entryLen - 8), (unsigned)hdrSize, (unsigned)hdrSize));
		return;
	}
	Buf h(entry + 32, hdrSize);
	Bounds bd;
	for(int k = 0; k < 3; k++) bd.bmin[k] = h.f32();
	for(int k = 0; k < 3; k++) bd.bmax[k] = h.f32();
	for(int k = 0; k < 3; k++) bd.c[k] = h.f32();
	bd.r = h.f32();
	ce.radius = bd.r; memcpy(ce.center, bd.c, 12); memcpy(ce.bmin, bd.bmin, 12); memcpy(ce.bmax, bd.bmax, 12);
	int numSpheres = h.u16(), numBoxes = h.u16(), numFaces = h.u16();
	int numLines = h.u8(); h.u8();
	uint32_t flags = h.u32();
	uint32_t offSpheres = h.u32(), offBoxes = h.u32(), offLines = h.u32(), offVerts = h.u32(), offFaces = h.u32(), offPlanes = h.u32();
	uint32_t numShadowFaces = 0, offShadowVerts = 0, offShadowFaces = 0;
	if(version >= 3){ numShadowFaces = h.u32(); offShadowVerts = h.u32(); offShadowFaces = h.u32(); }
	(void)numLines; (void)offLines; (void)offPlanes;
	ce.numSpheres = numSpheres; ce.numBoxes = numBoxes; ce.numFaces = numFaces;
	ce.hasVolumes = (flags & 2) != 0;
	bool faceGroups = (flags & 8) != 0;
	uint32_t firstData = (uint32_t)(hdrSize + 32 - 4);	// smallest valid offset (relative to base+4)
	uint32_t entryRel = (uint32_t)(entryLen - 4);		// end, relative to base+4
	auto ptrOk = [&](uint32_t off, uint32_t count, uint32_t stride, const char *what) -> bool {
		if(count == 0) return true;
		if(off == 0){ p.add(SEV_FATAL, "COL-07", fmt("%s: count %u при offset 0 — указатель NULL, краш при первом тесте коллизии", what, count)); return false; }
		if(off < firstData || (uint64_t)off + (uint64_t)count * stride > entryRel){
			p.add(SEV_FATAL, "COL-08", fmt("%s: offset 0x%X + %u×%u выходит за запись (0x%X..0x%X) — указатели вне блока", what, off, count, stride, firstData, entryRel));
			return false;
		}
		return true;
	};
	int reported = 0;
	bool outBox = false, outSphere = false;
	// dataSize == header only?
	if(entryLen - 32 == hdrSize){
		if(flags & 2) p.add(SEV_INFO, "COL-20", "флаг 2 установлен у пустой записи (только заголовок) — m_pColData = NULL, объект без коллизии");
		if(numSpheres || numBoxes || numFaces) p.add(SEV_FATAL, "COL-07", "счётчики > 0 у записи без данных");
		if(outBox || outSphere){}
		return;
	}
	if(numFaces >= 32768)
		p.add(faceGroups ? SEV_FATAL : SEV_ERROR, "COL-11", fmt("numFaces %d ≥ 32768 — читается как int16 со знаком, все треугольники игнорируются%s", numFaces, faceGroups ? ", а с face-группами — краш" : ""));
	if(!(flags & 2) && (numSpheres || numBoxes || numFaces))
		p.add(SEV_ERROR, "COL-19", "флаг 2 («есть коллизия») снят при наличии примитивов — все экземпляры получат SetUsesCollision(false)", "Ставь flags |= 2, когда есть хоть один примитив.");
	if((flags & 2) && !numSpheres && !numBoxes && !numFaces)
		p.add(SEV_INFO, "COL-20", "флаг 2 при нулевых счётчиках — коллизии нет, но объект считается коллизионным");
	if(flags & 1) p.add(SEV_WARN, "COL-19", "флаг 1 установлен — движок его сбрасывает (не используй)");
	// spheres
	if(ptrOk(offSpheres, numSpheres, 20, "сферы")){
		Buf s(entry + 4 + offSpheres, (size_t)numSpheres * 20);
		for(int i = 0; i < numSpheres; i++){
			float c[3]; for(int k = 0; k < 3; k++) c[k] = s.f32(); float r = s.f32();
			int mat = s.u8(); s.u8(); s.u8(); s.u8();
			checkSurface(p, mat, "сфера", reported);
			checkBoundsSphere(bd, c, r, outBox, outSphere);
		}
	}
	if(ptrOk(offBoxes, numBoxes, 28, "боксы")){
		Buf s(entry + 4 + offBoxes, (size_t)numBoxes * 28);
		for(int i = 0; i < numBoxes; i++){
			float mn[3], mx[3];
			for(int k = 0; k < 3; k++) mn[k] = s.f32();
			for(int k = 0; k < 3; k++) mx[k] = s.f32();
			int mat = s.u8(); s.u8(); s.u8(); s.u8();
			checkSurface(p, mat, "бокс", reported);
			if(mn[0] > mx[0] || mn[1] > mx[1] || mn[2] > mx[2]) p.add(SEV_INFO, "COL-33", fmt("бокс %d: min > max", i));
			checkBoundsPoint(bd, mn, outBox, outSphere); checkBoundsPoint(bd, mx, outBox, outSphere);
		}
	}
	// vertices: count derived from the region up to the next section
	int numVerts = 0;
	if(numFaces > 0 || offVerts){
		if(offVerts == 0 && numFaces > 0) p.add(SEV_FATAL, "COL-07", "грани есть, а offVertices = 0");
		else if(offVerts && (offVerts < firstData || offVerts > entryRel)) p.add(SEV_FATAL, "COL-08", fmt("offVertices 0x%X вне записи", offVerts));
		else if(offVerts){
			uint32_t next = entryRel;
			uint32_t cands[] = { offSpheres, offBoxes, offFaces, offShadowVerts, offShadowFaces };
			for(int k = 0; k < 5; k++) if(cands[k] > offVerts && cands[k] < next) next = cands[k];
			if(faceGroups && offFaces > offVerts && offFaces <= entryRel){
				// groups + count sit right before the faces: [groups][u32 count][faces]
				if(offFaces >= 4 + firstData){
					Buf cb(entry + 4 + offFaces - 4, 4);
					uint32_t cnt = cb.u32();
					if(cnt < 10000 && offFaces >= 4 + cnt * 28 + firstData) next = offFaces - 4 - cnt * 28;
				}
			}
			numVerts = (int)((next - offVerts) / 6);
		}
	}
	ce.numVerts = numVerts;
	int nf = numFaces < 32768 ? numFaces : 0;
	std::vector<int16_t> verts;
	if(numVerts > 0 && offVerts){
		verts.resize((size_t)numVerts * 3);
		Buf s(entry + 4 + offVerts, (size_t)numVerts * 6);
		for(int i = 0; i < numVerts; i++){
			for(int k = 0; k < 3; k++) verts[(size_t)i*3+k] = s.i16();
			float v[3] = { verts[(size_t)i*3] / 128.0f, verts[(size_t)i*3+1] / 128.0f, verts[(size_t)i*3+2] / 128.0f };
			checkBoundsPoint(bd, v, outBox, outSphere);
		}
	}
	if(ptrOk(offFaces, (uint32_t)nf, 8, "грани")){
		Buf s(entry + 4 + offFaces, (size_t)nf * 8);
		int badIdx = 0, planeBad = 0, maxIdx = -1;
		for(int i = 0; i < nf; i++){
			int a = s.u16(), b2 = s.u16(), c = s.u16();
			int mat = s.u8(); s.u8();
			checkSurface(p, mat, "грань", reported);
			int mx = std::max(a, std::max(b2, c));
			if(mx > maxIdx) maxIdx = mx;
			if(mx >= numVerts) badIdx++;
			else if(!verts.empty()){
				float va[3], vb[3], vc[3];
				for(int k = 0; k < 3; k++){ va[k] = verts[(size_t)a*3+k]/128.0f; vb[k] = verts[(size_t)b2*3+k]/128.0f; vc[k] = verts[(size_t)c*3+k]/128.0f; }
				float e1[3] = { vb[0]-va[0], vb[1]-va[1], vb[2]-va[2] }, e2[3] = { vc[0]-va[0], vc[1]-va[1], vc[2]-va[2] };
				float n[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0] };
				float ln = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
				if(ln > 1e-9f){ float dd = (n[0]*va[0]+n[1]*va[1]+n[2]*va[2]) / ln; if(fabsf(dd) >= 256.0f) planeBad++; }
			}
		}
		if(badIdx) p.add(SEV_ERROR, "COL-09", fmt("%d граней с индексом вершины ≥ %d (в файле столько вершин по размеру секции) — нормали из мусора за массивом", badIdx, numVerts));
		if(planeBad) p.add(SEV_WARN, "COL-14", fmt("%d граней с |n·v| ≥ 256 — расстояние плоскости заворачивается в int16", planeBad));
		// face groups
		if(faceGroups && nf > 0){
			if(offFaces < 4 + firstData || offFaces > entryRel) p.add(SEV_FATAL, "COL-15", "флаг face-групп (8), но перед гранями нет места для счётчика групп");
			else{
				Buf cb(entry + 4 + offFaces - 4, 4);
				uint32_t cnt = cb.u32();
				if(cnt == 0 || cnt > 10000 || offFaces < 4 + cnt * 28 + firstData)
					p.add(SEV_FATAL, "COL-15", fmt("флаг face-групп (8), а счётчик перед гранями = %u — группы читаются из чужих байт", cnt));
				else{
					Buf g(entry + 4 + offFaces - 4 - cnt * 28, (size_t)cnt * 28);
					std::vector<char> covered((size_t)nf, 0);
					int bad = 0;
					for(uint32_t i = 0; i < cnt; i++){
						float gmin[3], gmax[3];
						for(int k = 0; k < 3; k++) gmin[k] = g.f32();
						for(int k = 0; k < 3; k++) gmax[k] = g.f32();
						int first = g.i16(), last = g.i16();
						if(last >= nf || first < 0){ bad++; continue; }
						if(first > last) continue;
						for(int f = first; f <= last; f++) covered[(size_t)f] = 1;
						(void)gmin; (void)gmax;
					}
					if(bad) p.add(SEV_FATAL, "COL-16", fmt("%d face-групп с last ≥ numFaces — чтение треугольников и плоскостей за массивами", bad));
					int unc = 0; for(int f = 0; f < nf; f++) if(!covered[(size_t)f]) unc++;
					if(unc) p.add(SEV_WARN, "COL-16", fmt("%d граней не входят ни в одну face-группу — не сталкиваются с движущимися объектами", unc));
				}
			}
		}
	}
	// shadow mesh
	if(version >= 3 && numShadowFaces){
		if(!offShadowVerts || !offShadowFaces) p.add(SEV_INFO, "COL-21", "numShadowFaces > 0 при нулевых offsets — тень отключена (безвредно)");
		else if(ptrOk(offShadowFaces, numShadowFaces, 8, "shadow-грани")){
			uint32_t next = entryRel;
			uint32_t cands[] = { offSpheres, offBoxes, offFaces, offVerts, offShadowFaces };
			for(int k = 0; k < 5; k++) if(cands[k] > offShadowVerts && cands[k] < next) next = cands[k];
			int nsv = offShadowVerts >= firstData && next > offShadowVerts ? (int)((next - offShadowVerts) / 6) : 0;
			Buf s(entry + 4 + offShadowFaces, (size_t)numShadowFaces * 8);
			int maxIdx = -1;
			for(uint32_t i = 0; i < numShadowFaces && numShadowFaces < 100000; i++){ int a = s.u16(), b2 = s.u16(), c = s.u16(); s.u16(); int mx = std::max(a, std::max(b2, c)); if(mx > maxIdx) maxIdx = mx; }
			if(maxIdx >= nsv) p.add(SEV_ERROR, "COL-22", fmt("shadow-грани ссылаются на вершину %d, а в секции теневых вершин только %d — чтение за массивом при рендере тени", maxIdx, nsv));
		}
	}
	if(!(bd.r > 0.0f) || !(bd.r == bd.r)) p.add(SEV_ERROR, "COL-23", fmt("радиус ограничивающей сферы %.3f — DFF будет отсекаться всегда/никогда", bd.r));
	if(outBox) p.add(SEV_WARN, "COL-23", "ограничивающий бокс не накрывает примитивы — объект попадёт не во все секторы мира (нет коллизии в части объекта)");
	if(outSphere) p.add(SEV_ERROR, "COL-23", "ограничивающая сфера не накрывает примитивы — DFF отсекается на экране, широкая фаза коллизии пропускает контакты");
	(void)gd;
}

void CheckOneCol(Context &ctx, const std::string &where, const std::vector<uint8_t> &data, int entryIdx, bool bootPass)
{
	GameData &gd = *ctx.gd;
	ctx.rep->countFile(CAT_COL);
	bool colfile = entryIdx == -2;
	size_t pos = 0;
	int index = 0;
	std::set<std::string> namesHere;
	int matched = 0;
	while(pos + 8 <= data.size()){
		Buf b(data.data() + pos, data.size() - pos);
		uint32_t fcc = b.u32();
		uint32_t size = b.u32();
		int version = fcc == FCC_COLL ? 1 : fcc == FCC_COL2 ? 2 : fcc == FCC_COL3 ? 3 : fcc == FCC_COL4 ? 4 : 0;
		if(fcc == 0) break;	// sector padding
		CP p; p.ctx = &ctx; p.where = &where; p.id = -1; p.index = index; p.fatal = false;
		if(version == 0 || (version == 4 && bootPass)){
			p.obj = "";
			if(version == 4)
				p.add(SEV_ERROR, "COL-04", fmt("запись №%d в формате COL4 — стартовый проход (LoadCollisionFileFirstTime) принимает только COLL/COL2/COL3: эта и ВСЕ последующие записи файла пропущены", index), "Сохраняй COL3.");
			else
				p.add(SEV_ERROR, "COL-04", fmt("запись №%d: неизвестный fourcc 0x%08X (мусор между моделями?) — разбор файла прекращён, остальные записи не загрузятся", index, fcc));
			break;
		}
		if(size < 24 || (uint64_t)pos + 8 + size > data.size()){
			p.add(SEV_FATAL, "COL-05", fmt("запись №%d: size %u выходит за буфер (%u байт) — парсер уйдёт в следующую модель / за буфер", index, size, (unsigned)data.size()));
			break;
		}
		const uint8_t *nm = data.data() + pos + 8;
		size_t nlen = 0; while(nlen < 22 && nm[nlen]) nlen++;
		std::string name((const char*)nm, nlen);
		p.obj = name;
		uint16_t idHint = (uint16_t)(nm[22] | (nm[23] << 8));
		if(nlen == 22)
			p.add(SEV_ERROR, "COL-03", fmt("имя «%s» без NUL в 22 байтах — хэш считается по мусору со стека, модель не найдётся", name.c_str()), "Имя ≤ 21 символа + NUL.");
		// name resolution
		const ObjDef *def = gd.findObjByName(name);
		if(def){
			p.id = def->id;
			bool owns = def->type == OT_OBJS || def->type == OT_TOBJ || def->type == OT_ANIM || def->type == OT_HIER || def->type == OT_CARS;
			if(!owns) p.add(SEV_INFO, "COL-25", fmt("«%s» — %s-модель, она не владеет COL (SetColModel(x,false)) — запись пропущена", name.c_str(), objTypeName(def->type)));
			else matched++;
			if(idHint < 20000 && idHint != (uint16_t)def->id && idHint != 0 && p.ctx->gd->isSA()) p.add(SEV_INFO, "COL-26", fmt("подсказка id %u не совпадает с IDE (%d) — безвредно, имя главнее", idHint, def->id));	// III/VC: id в COL не читается вовсе
		}else if(!gd.objs.empty() && entryIdx != -1)
			p.add(SEV_WARN, "COL-25", fmt("«%s» не совпадает ни с одной IDE-моделью — запись молча пропущена", name.c_str()), "Имя сравнивается без регистра по хэшу.");
		if(namesHere.count(lower(name))) p.add(SEV_WARN, "COL-24", fmt("«%s» встречается в файле дважды — вторая CColModel течёт / перезаписывает данные", name.c_str()));
		namesHere.insert(lower(name));
		if(colfile && size - 24 > 0x8000)
			p.add(SEV_FATAL, "COL-30", fmt("COLFILE: запись «%s» %u байт > 32 КБ — статический буфер 0xBC40D8 переполнится (затирает gCurrIplInstancesCount)", name.c_str(), size - 24));
		ColEntry ce;
		ce.name = name; ce.version = version; ce.entryIdx = entryIdx; ce.index = index; ce.modelId = def ? def->id : -1;
		if(entryIdx == -2) ce.colfile = where;
		if(version == 1){
			Buf pb(data.data() + pos + 32, size - 24);
			parseCol1(p, pb, size - 24, ce);
		}else
			parseCol23(p, data.data() + pos, 8 + (size_t)size, version, ce);
		{	// IMG entries, loose files and COLFILE lines (III/VC keep the whole map collision there) all become entries
			gd.colEntries.push_back(ce);
			if(def && def->colEntry < 0){
				gd.objs[gd.objById[def->id]].colEntry = (int)gd.colEntries.size() - 1;
			}else if(def && def->colEntry >= 0 && entryIdx != -1){
				const ColEntry &prev = gd.colEntries[(size_t)def->colEntry];
				std::string prevWhere = prev.entryIdx >= 0 ? gd.entries[(size_t)prev.entryIdx].name : (prev.entryIdx == -2 ? "COLFILE" : "?");
				if(prev.entryIdx != entryIdx)
					p.add(SEV_WARN, "COL-24", fmt("«%s» есть и в %s — какая коллизия останется, зависит от порядка стриминга; утечка CColModel", name.c_str(), prevWhere.c_str()));
			}
		}
		pos += 8 + size;
		index++;
	}
	(void)matched;
}

// Geometry of one entry for the wireframe overlay: the same layouts the rules above walk, without
// the diagnostics. COL1 floats / u32 faces, COL2+ int16/128 vertices / u16 faces.
void ListColData(const std::vector<uint8_t> &data, std::vector<ColListEntry> &out)
{
	out.clear();
	size_t pos = 0;
	while(pos + 8 <= data.size()){
		Buf b(data.data() + pos, data.size() - pos);
		uint32_t fcc = b.u32(), size = b.u32();
		int version = fcc == FCC_COLL ? 1 : fcc == FCC_COL2 ? 2 : fcc == FCC_COL3 ? 3 : fcc == FCC_COL4 ? 4 : 0;
		if(version == 0 || size < 24 || (uint64_t)pos + 8 + size > data.size()) break;
		ColListEntry e; e.version = version; e.offset = (uint32_t)pos; e.size = size + 8;
		const char *nm = (const char*)data.data() + pos + 8; size_t n = 0; while(n < 22 && nm[n]) n++; e.name.assign(nm, n);
		out.push_back(e);
		pos += 8 + size;
	}
}

bool LoadColShape(const GameData &gd, int colEntry, ColShape &out, std::string &err)
{
	if(colEntry < 0 || colEntry >= (int)gd.colEntries.size()){ err = "нет записи COL"; return false; }
	const ColEntry &ce = gd.colEntries[(size_t)colEntry];
	std::vector<uint8_t> data;
	if(ce.entryIdx == -2 && !ce.colfile.empty()){	// III/VC: COLFILE line of gta.dat — a loose .col
		if(!readFile(DataFilePath(gd, ce.colfile), data)){ err = "COLFILE не прочитан: " + ce.colfile; return false; }
	}else{
		if(ce.entryIdx < 0){ err = "COLFILE не поддерживается"; return false; }
		if(!gd.readEntry(ce.entryIdx, data, &err)) return false;
	}
	std::string name; int version = 0;
	if(!LoadColShapeData(data, ce.index, out, name, version, err)) return false;
	memcpy(out.bmin, ce.bmin, 12); memcpy(out.bmax, ce.bmax, 12);
	return true;
}

bool LoadColShapeData(const std::vector<uint8_t> &data, int wantIndex, ColShape &out, std::string &outName, int &outVersion, std::string &err)
{
	// walk to the wanted entry index
	size_t pos = 0; int index = 0;
	while(pos + 8 <= data.size()){
		Buf b(data.data() + pos, data.size() - pos);
		uint32_t fcc = b.u32(), size = b.u32();
		int version = fcc == FCC_COLL ? 1 : fcc == FCC_COL2 ? 2 : fcc == FCC_COL3 ? 3 : fcc == FCC_COL4 ? 4 : 0;
		if(version == 0 || size < 24 || (uint64_t)pos + 8 + size > data.size()){ err = "запись COL повреждена"; return false; }
		if(index == wantIndex){
			const uint8_t *entry = data.data() + pos;
			size_t entryLen = 8 + size;
			{ const char *nm = (const char*)entry + 8; size_t n = 0; while(n < 22 && nm[n]) n++; outName.assign(nm, n); outVersion = version; }
			if(version == 1){
				Buf s(entry + 32, entryLen - 32);
				s.f32(); for(int k = 0; k < 3; k++) s.f32();	// sphere
				for(int k = 0; k < 3; k++) out.bmin[k] = s.f32(); for(int k = 0; k < 3; k++) out.bmax[k] = s.f32();	// box
				int ns = (int16_t)s.i32(); if(ns < 0) ns = 0;
				for(int i = 0; i < ns && s.ok; i++){ float r = s.f32(); float c[3]; for(int k = 0; k < 3; k++) c[k] = s.f32(); s.u32(); out.spheres.push_back(c[0]); out.spheres.push_back(c[1]); out.spheres.push_back(c[2]); out.spheres.push_back(r); }
				int nl = (int8_t)(s.i32() & 0xFF); if(nl > 0) s.skip((size_t)nl * 24);
				int nb = (int16_t)s.i32(); if(nb < 0) nb = 0;
				for(int i = 0; i < nb && s.ok; i++){ for(int k = 0; k < 6; k++) out.boxes.push_back(s.f32()); s.u32(); }
				int nv = s.i32(); if(nv < 0 || (size_t)nv * 12 > s.left()){ err = "COL1: вершины"; return false; }
				for(int i = 0; i < nv * 3 && s.ok; i++) out.verts.push_back(s.f32());
				int nf = (int16_t)s.i32(); if(nf < 0) nf = 0;
				for(int i = 0; i < nf && s.ok; i++){ uint32_t a = s.u32() & 0xFFFF, b2 = s.u32() & 0xFFFF, c = s.u32() & 0xFFFF; s.u32(); if((int)a < nv && (int)b2 < nv && (int)c < nv){ out.faces.push_back((uint16_t)a); out.faces.push_back((uint16_t)b2); out.faces.push_back((uint16_t)c); } }
				return true;
			}
			size_t hdrSize = version == 2 ? 0x4C : (version == 3 ? 0x58 : 0x5C);
			if(entryLen < 32 + hdrSize){ err = "COL: короткий заголовок"; return false; }
			Buf h(entry + 32, hdrSize);
			for(int k = 0; k < 3; k++) out.bmin[k] = h.f32(); for(int k = 0; k < 3; k++) out.bmax[k] = h.f32(); for(int k = 0; k < 4; k++) h.f32();	// bounds: min, max, centre, radius
			int numSpheres = h.u16(), numBoxes = h.u16(), numFaces = h.u16(); h.u8(); h.u8();
			uint32_t flags = h.u32();
			uint32_t offSpheres = h.u32(), offBoxes = h.u32(), offLines = h.u32(), offVerts = h.u32(), offFaces = h.u32(), offPlanes = h.u32();
			uint32_t offShadowVerts = 0, offShadowFaces = 0;
			if(version >= 3){ h.u32(); offShadowVerts = h.u32(); offShadowFaces = h.u32(); }
			(void)offLines; (void)offPlanes;
			uint32_t firstData = (uint32_t)(hdrSize + 32 - 4), entryRel = (uint32_t)(entryLen - 4);
			auto ok = [&](uint32_t off, uint32_t count, uint32_t stride){ return count == 0 || (off >= firstData && (uint64_t)off + (uint64_t)count * stride <= entryRel); };
			if(ok(offSpheres, (uint32_t)numSpheres, 20)){ Buf s(entry + 4 + offSpheres, (size_t)numSpheres * 20); for(int i = 0; i < numSpheres; i++){ float c[3]; for(int k = 0; k < 3; k++) c[k] = s.f32(); float r = s.f32(); s.u32(); out.spheres.push_back(c[0]); out.spheres.push_back(c[1]); out.spheres.push_back(c[2]); out.spheres.push_back(r); } }
			if(ok(offBoxes, (uint32_t)numBoxes, 28)){ Buf s(entry + 4 + offBoxes, (size_t)numBoxes * 28); for(int i = 0; i < numBoxes; i++){ for(int k = 0; k < 6; k++) out.boxes.push_back(s.f32()); s.u32(); } }
			int numVerts = 0;
			if(numFaces > 0 && offVerts >= firstData && offVerts <= entryRel){
				uint32_t next = entryRel;
				uint32_t cands[] = { offSpheres, offBoxes, offFaces, offShadowVerts, offShadowFaces };
				for(int k = 0; k < 5; k++) if(cands[k] > offVerts && cands[k] < next) next = cands[k];
				if((flags & 8) && offFaces > offVerts && offFaces <= entryRel && offFaces >= 4 + firstData){
					Buf cb(entry + 4 + offFaces - 4, 4); uint32_t cnt = cb.u32();
					if(cnt < 10000 && offFaces >= 4 + cnt * 28 + firstData) next = offFaces - 4 - cnt * 28;
				}
				numVerts = (int)((next - offVerts) / 6);
			}
			int nf = numFaces < 32768 ? numFaces : 0;
			if(numVerts > 0){ Buf s(entry + 4 + offVerts, (size_t)numVerts * 6); for(int i = 0; i < numVerts * 3; i++) out.verts.push_back(s.i16() / 128.0f); }
			if(ok(offFaces, (uint32_t)nf, 8)){ Buf s(entry + 4 + offFaces, (size_t)nf * 8); for(int i = 0; i < nf; i++){ int a = s.u16(), b2 = s.u16(), c = s.u16(); s.u16(); if(a < numVerts && b2 < numVerts && c < numVerts){ out.faces.push_back((uint16_t)a); out.faces.push_back((uint16_t)b2); out.faces.push_back((uint16_t)c); } } }
			return true;
		}
		pos += 8 + size; index++;
	}
	err = "запись не найдена в файле COL";
	return false;
}

void CheckCols(Context &ctx)
{
	GameData &gd = *ctx.gd;
	ctx.prog->set("COL");
	int total = 0;
	for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].kind == EK_COL) total++;
	if(gd.isSA() && total > 254)
		ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "COL-02", "IMG", -1, "", fmt("%d .col файлов во всех IMG — пул CColStore на 255 слотов (0 = generic), AddColSlot пишет по NULL при старте", total), ctx.opt.limitAdjuster ? "Лимит-аджастер включён." : "Ваниль: 251, свободно 3.");
	else if(gd.isSA() && total > 250)
		ctx.add(SEV_WARN, CAT_LIMIT, "COL-02", "IMG", -1, "", fmt("%d .col файлов из 254 возможных", total), "");
	int done = 0;
	for(size_t i = 0; i < gd.entries.size(); i++){
		if(ctx.cancelled()) return;
		Entry &e = gd.entries[i];
		if(e.kind != EK_COL || !gd.isWinner((int)i)) continue;
		done++;
		bool quiet = ctx.opt.skipVanillaImgs && e.img >= 0 && gd.archives[e.img].vanilla;
		std::string where = (e.img >= 0 ? basename(gd.archives[e.img].logical) + "/" : "") + e.name;
		ctx.prog->step(e.name.c_str(), done, total);
		std::vector<uint8_t> data;
		std::string err;
		if(!gd.readEntry((int)i, data, &err)){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-24", where, e.dirIndex, e.name, "не удалось прочитать запись: " + err, "");
			continue;
		}
		if(gd.isSA()){
			std::string bl = e.base;
			static const char *intPrefixes[] = { "int_la", "int_sf", "int_veg", "int_cont", "gen_int1", "gen_int2", "gen_int3", "gen_int4", "gen_int5", "gen_intb", "savehous", "levelmap", "stadint", nullptr };
			bool interior = bl == "props" || bl == "props2";
			for(int k = 0; intPrefixes[k] && !interior; k++) if(bl.compare(0, strlen(intPrefixes[k]), intPrefixes[k]) == 0) interior = true;
			if(interior && !quiet) ctx.add(SEV_INFO, CAT_COL, "COL-28", where, -1, "", "имя файла с интерьерным префиксом — коллизия грузится только когда игрок в интерьере (area ≠ 0)", "");
		}
		if(quiet){
			Report tmp; Context q = ctx; q.rep = &tmp;
			CheckOneCol(q, where, data, (int)i, true);
		}else
			CheckOneCol(ctx, where, data, (int)i, true);
	}
	// COLFILE lines
	for(size_t i = 0; i < gd.datFiles.size(); i++){
		if(gd.datFiles[i].compare(0, 8, "COLFILE:") != 0) continue;
		std::string logical = gd.datFiles[i].substr(8);
		std::string phys = resolvePath(gd.root, logical);
		std::vector<uint8_t> data;
		if(!readFile(phys, data)){ ctx.add(SEV_ERROR, CAT_DAT, "DAT-03", logical, -1, "", "COLFILE: файл не найден", ""); continue; }
		CheckOneCol(ctx, logical, data, -2, true);
	}
	// CColModel objects: one per matched model that owns its collision; the second model of a
	// tobj day/night pair shares the first one's (SetColModel(x,false))
	std::set<int> colModels; int tobjEntries = 0;
	for(size_t i = 0; i < gd.colEntries.size(); i++){
		const ColEntry &ce = gd.colEntries[i];
		if(ce.modelId < 0 || ce.entryIdx < 0) continue;
		const ObjDef *od = gd.findObj(ce.modelId);
		if(od == nullptr || od->type == OT_PEDS || od->type == OT_WEAP) continue;
		if(od->type == OT_TOBJ){ tobjEntries++; continue; }
		colModels.insert(ce.modelId);
	}
	int models = (int)colModels.size() + (tobjEntries + 1) / 2;
	if(gd.isSA() && models > 10150)
		ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "COL-01", "IMG", -1, "", fmt("%d COL-записей — пул CColModel на 10150 (каждая совпавшая запись занимает слот навсегда) → operator new вернёт NULL, краш", models), "");
	else if(gd.isSA() && models > 10000)
		ctx.add(SEV_WARN, CAT_LIMIT, "COL-01", "IMG", -1, "", fmt("≈%d объектов CColModel из 10150 в пуле (ваниль ≈ 10080; педы и динамика добавляют свои)", models), "");
}

} // namespace gc
