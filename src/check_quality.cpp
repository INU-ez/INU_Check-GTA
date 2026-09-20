// gtacheck — build quality / performance rules (QLT-nn): things the engine survives but the player
// notices — objects popping, heavy textures, overloaded sectors, blank names, mod conflicts, dead
// weight in the archives. Runs last, on top of everything the other passes resolved.
#include "gtacheck.h"
#include "tables_ped.h"

#include <math.h>
#include <algorithm>
#include <unordered_map>

namespace gc {

static uint32_t entryBytes(const GameData &gd, int e) { return e >= 0 && e < (int)gd.entries.size() ? gd.entries[(size_t)e].sizeSectors * 2048u : 0u; }
// "gta3.img/foo.txd" like the other rules (Entry::where() is the bare name for archive entries)
static std::string entryWhere(const GameData &gd, const Entry &e) { return e.img >= 0 ? basename(gd.archives[(size_t)e.img].logical) + "/" + e.name : e.where(); }

// TXDs that legitimately have no IDE user: UI, loading screens, radar tiles, particles, generic.
static bool txdIsSystem(const std::string &n)
{
	static const char *pre[] = { "generic", "particle", "vehicle", "effectspc", "load", "splash", "fronten", "hud", "pcbtns", "ld_", "intro", "logo", "radar", "gta_logo", "mapzoom", "txdcut", "plant", "grass", "fonts", "font", "misc", "menu", "shopping", "lod", "cutscene" };
	for(size_t i = 0; i < sizeof(pre) / sizeof(pre[0]); i++) if(n.compare(0, strlen(pre[i]), pre[i]) == 0) return true;
	return false;
}

// Every run of [A-Z0-9_] (≥ 3 chars) in main.scm / script.img: model, IFP and TXD names the scripts
// load by name (LOAD_SPECIAL_CHARACTER, REQUEST_ANIMATION, paint jobs…) live there as NUL-padded strings.
static void scriptNames(const GameData &gd, std::set<std::string> &out)
{
	const char *files[] = { "data/script/main.scm", "data/script/script.img" };
	for(int f = 0; f < 2; f++){
		std::vector<uint8_t> d;
		if(!readFile(DataFilePath(gd, files[f]), d)) continue;
		std::string cur;
		for(size_t i = 0; i <= d.size(); i++){
			unsigned char c = i < d.size() ? d[i] : 0;
			if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (c >= 'a' && c <= 'z')){ cur.push_back((char)(c >= 'a' && c <= 'z' ? c - 32 : c)); continue; }
			if(cur.size() >= 3 && cur.size() <= 24) out.insert(cur);
			cur.clear();
		}
	}
}
static std::string upper(const std::string &s) { std::string r = s; for(size_t i = 0; i < r.size(); i++) if(r[i] >= 'a' && r[i] <= 'z') r[i] = (char)(r[i] - 32); return r; }

// Files in the archives nothing refers to: not in any IDE / animgrp, not named in the scripts, not a
// vehicle paint job, not a system TXD.
void CollectUnused(const GameData &gd, std::vector<UnusedFile> &out)
{
	out.clear();
	std::set<std::string> scm;
	scriptNames(gd, scm);
	std::set<std::string> carNames;
	for(size_t i = 0; i < gd.objs.size(); i++) if(gd.objs[i].type == OT_CARS) carNames.insert(lower(gd.objs[i].name));
	auto paintJob = [&](const std::string &base){	// <car><digit>.txd
		if(base.size() < 2 || base.back() < '0' || base.back() > '9') return false;
		return carNames.count(base.substr(0, base.size() - 1)) > 0;
	};
	// COL: a file counts as used when at least one of its entries resolved to a model
	std::set<int> colUsed;
	for(size_t i = 0; i < gd.colEntries.size(); i++) if(gd.colEntries[i].modelId >= 0 && gd.colEntries[i].entryIdx >= 0) colUsed.insert(gd.colEntries[i].entryIdx);
	// IFP: referenced by animgrp.dat blocks or an IDE anim column
	std::set<std::string> ifpUsed;
	for(size_t i = 0; i < gd.animGroups.size(); i++) ifpUsed.insert(lower(gd.animGroups[i].block));
	for(size_t i = 0; i < gd.objs.size(); i++) if(!gd.objs[i].anim.empty()) ifpUsed.insert(lower(gd.objs[i].anim));
	ifpUsed.insert("ped"); ifpUsed.insert("cutscene");
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(!gd.isWinner((int)i)) continue;
		if(e.img >= 0){
			std::string a = lower(basename(gd.archives[(size_t)e.img].logical));
			if(a == "player.img" || a == "cutscene.img" || a == "cuts.img" || a == "script.img") continue;	// clothes / cutscene assets / scripts: other rules
		}
		UnusedFile u; u.entry = (int)i;
		if(scm.count(upper(e.base))) continue;	// named in a script
		if(e.base == "player") continue;
		if(e.kind == EK_DFF){
			if(gd.objByName.count(e.base)) continue;
			u.why = "DFF не объявлен ни в одной IDE и не назван в скриптах";
		}else if(e.kind == EK_TXD){
			int slot = gd.findTxdSlot(e.base);
			if(slot >= 0 && gd.txdSlots[(size_t)slot].fromIde) continue;
			if(slot >= 0 && gd.txdSlots[(size_t)slot].users > 0) continue;
			if(txdIsSystem(e.base) || paintJob(e.base)) continue;
			u.why = "TXD не использует ни одна модель";
		}else if(e.kind == EK_COL){
			if(colUsed.count((int)i)) continue;
			bool any = false;
			for(size_t k = 0; k < gd.colEntries.size(); k++) if(gd.colEntries[k].entryIdx == (int)i){ any = true; break; }
			if(!any) continue;	// not parsed (COL pass off) — unknown, not unused
			u.why = "COL: ни одна запись не совпала с моделью из IDE";
		}else if(e.kind == EK_IFP){
			if(ifpUsed.count(e.base)) continue;
			u.why = "IFP не упомянут ни в animgrp.dat, ни в IDE, ни в скриптах";
		}else continue;
		out.push_back(u);
	}
}

// Streaming load per map cell: with the camera in that cell, every instance whose draw distance
// reaches the cell centre would be resident — sum the unique DFF + TXD bytes (what CStreaming keeps).
// `n` cells per side over ±half; mb[y*n+x], count[y*n+x] = instances in range.
void StreamHeat(const GameData &gd, float half, int n, std::vector<float> &mb, std::vector<int> &count)
{
	mb.assign((size_t)n * n, 0.0f); count.assign((size_t)n * n, 0);
	float cell = half * 2.0f / (float)n;
	struct Src { float x, y, r; int obj; };
	std::vector<Src> src; src.reserve(gd.insts.size());
	for(size_t i = 0; i < gd.insts.size(); i++){
		const Inst &in = gd.insts[i];
		if((in.interior & 0xFF) != 0) continue;
		auto it = gd.objById.find(in.id);
		if(it == gd.objById.end()) continue;
		const ObjDef &o = gd.objs[(size_t)it->second];
		if(o.type != OT_OBJS && o.type != OT_TOBJ) continue;
		Src s; s.x = in.pos[0]; s.y = in.pos[1]; s.r = o.maxDrawDist(); s.obj = it->second;
		if(s.r < 1.0f) continue;
		src.push_back(s);
	}
	// bytes per model (DFF + its TXD; a TXD shared by several models is counted once per cell)
	std::vector<uint32_t> dffB(gd.objs.size(), 0); std::vector<int> txdOf(gd.objs.size(), -1);
	for(size_t i = 0; i < gd.objs.size(); i++){ dffB[i] = entryBytes(gd, gd.objs[i].dffEntry); txdOf[i] = gd.objs[i].txdSlot >= 0 ? gd.txdSlots[(size_t)gd.objs[i].txdSlot].entry : -1; }
	std::vector<unsigned> objStamp(gd.objs.size(), 0), txdStamp(gd.entries.size() + 1, 0);
	unsigned stamp = 0;
	for(int cy = 0; cy < n; cy++) for(int cx = 0; cx < n; cx++){
		float px = -half + (cx + 0.5f) * cell, py = -half + (cy + 0.5f) * cell;
		stamp++;
		double bytes = 0; int cnt = 0;
		for(size_t i = 0; i < src.size(); i++){
			const Src &s = src[i];
			float dx = s.x - px, dy = s.y - py;
			float reach = s.r + cell * 0.7071f;	// anywhere inside the cell
			if(dx * dx + dy * dy > reach * reach) continue;
			cnt++;
			if(objStamp[(size_t)s.obj] != stamp){ objStamp[(size_t)s.obj] = stamp; bytes += dffB[(size_t)s.obj]; }
			int t = txdOf[(size_t)s.obj];
			if(t >= 0 && txdStamp[(size_t)t] != stamp){ txdStamp[(size_t)t] = stamp; bytes += entryBytes(gd, t); }
		}
		mb[(size_t)cy * n + cx] = (float)(bytes / (1024.0 * 1024.0));
		count[(size_t)cy * n + cx] = cnt;
	}
}

// ------------------------------------------------------------- rules -----

static void ruleDrawDist(Context &ctx)
{
	GameData &gd = *ctx.gd;
	int shortN = 0, bigN = 0;
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if((o.type != OT_OBJS && o.type != OT_TOBJ) || o.numInstances == 0 || o.dffRadius <= 0) continue;
		float dd = o.maxDrawDist();
		if(dd > 0 && dd < o.dffRadius * 0.9f && ++shortN <= 300)
			ctx.add(SEV_WARN, CAT_QUALITY, "QLT-01", o.file, o.line, o.name,
			        fmt("дальность отрисовки %g меньше радиуса модели %.1f м — объект пропадает, пока камера ещё рядом с ним или внутри", dd, o.dffRadius),
			        "Дальность считается до центра модели: ставь её больше радиуса (обычно 1.5–3 радиуса; крупные объекты — LOD со своей дальностью). «Исправить: авто» ставит 2 × радиус.", o.id, FIX_IDE_DRAWDIST);
		bool lodName = !gd.isSA() && lower(o.name).compare(0, 3, "lod") == 0;	// III/VC: LOD by naming convention even without a paired model (drawn from 100 m)
		if(dd > 300.0f && !o.isLod && !lodName && o.dffRadius < 40.0f && ++bigN <= 300)
			ctx.add(SEV_WARN, CAT_QUALITY, "QLT-02", o.file, o.line, o.name,
			        fmt("дальность %g > 300 при радиусе %.1f м — SA заводит такой объект как big building (SetupBigBuilding): рисуется в проходе LOD-ов и запрашивается с 300 м, как большие здания", dd, o.dffRadius),
			        "Мелким объектам ≤ 300; далеко видимое делай парой модель + LOD.", o.id);
	}
	if(shortN > 300) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-01", "IDE", -1, "", fmt("…и ещё %d моделей с дальностью меньше радиуса", shortN - 300), "");
	if(bigN > 300) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-02", "IDE", -1, "", fmt("…и ещё %d мелких моделей с дальностью > 300", bigN - 300), "");
}

static bool inTable(const char *const *tab, int n, const std::string &s) { for(int i = 0; i < n; i++) if(s == tab[i]) return true; return false; }

static void ruleProcObj(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	const std::string logical = "data/procobj.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)) return;	// absent: the engine goes on without procedural objects (DAT-03 elsewhere if listed)
	ctx.rep->countFile(CAT_QUALITY);
	int n = 0;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.tok.empty() || l.raw[0] == ';' || l.raw[0] == '#' || l.raw[0] == '*') continue;
		const std::vector<std::string> &t = l.tok;
		if(t.size() < 14){ ctx.add(SEV_ERROR, CAT_QUALITY, "QLT-03", logical, l.number, t[0], fmt("%d полей вместо 14 — строка разобрана не полностью, недостающие параметры = мусор", (int)t.size()), "surface model spacing minDist minRot maxRot minScale maxScale minScaleZ maxScaleZ zOffMin zOffMax align useGrid"); continue; }
		n++;
		if(!inTable(SURFACE_NAMES, 179, t[0]))
			ctx.add(SEV_WARN, CAT_QUALITY, "QLT-03", logical, l.number, t[0], fmt("поверхность «%s» не из 179 встроенных (регистр важен) — GetSurfaceIdFromName даёт 0: объекты будут расти на DEFAULT-поверхностях", t[0].c_str()), "");
		if(!gd.findObjByName(t[1]))
			ctx.add(SEV_ERROR, CAT_QUALITY, "QLT-03", logical, l.number, t[1], fmt("модель «%s» не объявлена ни в одной IDE — CModelInfo::GetModelInfo не найдёт её, при появлении объектов на поверхности «%s» вероятен краш", t[1].c_str(), t[0].c_str()), "Объяви модель в IDE (objs) или убери строку.");
		double mn = atof(t[6].c_str()), mx = atof(t[7].c_str());
		if(mn > mx) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-03", logical, l.number, t[1], fmt("minScale %g > maxScale %g", mn, mx), "");
		double sp = atof(t[2].c_str());
		if(sp <= 0) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-03", logical, l.number, t[1], fmt("spacing %g ≤ 0 — деление на шаг сетки", sp), "");
	}
	if(n > 200) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-03", logical, -1, "", fmt("%d строк — таблица CProcObjectMan рассчитана на 200 (лишние отброшены)", n), "");
}

static void ruleHeavyTxd(Context &ctx)
{
	GameData &gd = *ctx.gd;
	double totalTxd = 0, totalDff = 0;
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.numInstances == 0) continue;
		totalDff += entryBytes(gd, o.dffEntry);
	}
	std::set<int> seenTxd;
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.numInstances == 0 || o.txdSlot < 0) continue;
		int e = gd.txdSlots[(size_t)o.txdSlot].entry;
		if(e >= 0 && seenTxd.insert(e).second) totalTxd += entryBytes(gd, e);
	}
	int heavy = 0;
	for(std::unordered_map<std::string, TxdInfo>::const_iterator it = gd.txdTextures.begin(); it != gd.txdTextures.end(); ++it){
		const TxdInfo &ti = it->second;
		if(!ti.parsed || ti.bytes < 4u * 1024 * 1024) continue;
		int e = gd.findEntry(it->first + ".txd");
		std::string where = e >= 0 ? entryWhere(gd, gd.entries[(size_t)e]) : it->first + ".txd";
		double mb = ti.bytes / (1024.0 * 1024.0);
		int users = 0; int slot = gd.findTxdSlot(it->first); if(slot >= 0) users = gd.txdSlots[(size_t)slot].users;
		// what the same dictionary would weigh compressed (DXT1 / DXT5 by alpha, full mip chain) with every side
		// capped at 1024 and at 512 — the numbers «Диета TXD» reaches; plus the biggest texture by name
		double at1024 = 0, at512 = 0; int uncompressed = 0; std::string biggest; uint32_t biggestBytes = 0;
		for(size_t k = 0; k < ti.texs.size(); k++){
			const TxdInfo::Tex &t = ti.texs[k];
			if(t.w <= 0 || t.h <= 0) continue;
			if(t.bytes > (uint32_t)t.w * (uint32_t)t.h * 2) uncompressed++;	// more than 16 bpp with mips → not DXT
			if(t.bytes > biggestBytes){ biggestBytes = t.bytes; biggest = t.name; }
			double bpt = t.alpha ? 1.0 : 0.5;	// DXT5 / DXT1 bytes per texel
			int w1 = t.w, h1 = t.h; while((w1 > 1024 || h1 > 1024) && w1 > 4 && h1 > 4){ w1 /= 2; h1 /= 2; }
			int w2 = t.w, h2 = t.h; while((w2 > 512 || h2 > 512) && w2 > 4 && h2 > 4){ w2 /= 2; h2 /= 2; }
			at1024 += (double)w1 * h1 * bpt * 4.0 / 3.0;
			at512 += (double)w2 * h2 * bpt * 4.0 / 3.0;
		}
		std::string hint = fmt("Ориентир: в DXT со стороной ≤ 1024 этот TXD весил бы ~%.1f МБ, ≤ 512 — ~%.1f МБ (сейчас %.1f МБ%s; самая тяжёлая текстура — «%s», %.1f МБ). Для обычных объектов хватает 512, для крупных стен/земли — 1024. «Диета TXD» в Настройках делает это автоматически.",
		                       at1024 / 1048576.0, at512 / 1048576.0, mb, uncompressed ? fmt(", несжатых текстур %d", uncompressed).c_str() : "", biggest.c_str(), biggestBytes / 1048576.0);
		if(ti.bytes >= 16u * 1024 * 1024)
			ctx.add(SEV_ERROR, CAT_QUALITY, "QLT-04", where, -1, it->first, fmt("TXD занимает %.1f МБ в памяти (%d текстур со стороной ≥ 1024, моделей %d) — один словарь съедает заметную часть бюджета стриминга, его загрузка = фриз", mb, ti.big, users), hint);
		else if(ti.bytes >= 8u * 1024 * 1024)
			ctx.add(SEV_WARN, CAT_QUALITY, "QLT-04", where, -1, it->first, fmt("TXD занимает %.1f МБ в памяти (%d текстур ≥ 1024², моделей %d) — тяжёлый словарь, подгрузка заметна", mb, ti.big, users), hint);
		else if(++heavy <= 200)
			ctx.add(SEV_INFO, CAT_QUALITY, "QLT-04", where, -1, it->first, fmt("TXD занимает %.1f МБ в памяти (%d текстур ≥ 1024²)", mb, ti.big), hint);
	}
	if(totalDff + totalTxd > 0)
		ctx.add(SEV_INFO, CAT_QUALITY, "QLT-04", "IMG", -1, "", fmt("размещённые модели весят в архивах: DFF %.0f МБ + TXD %.0f МБ (стриминг держит в памяти только видимое)", totalDff / 1048576.0, totalTxd / 1048576.0), "");
}

static void ruleUnderWater(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(gd.water.empty()) return;
	int n = 0;
	for(size_t i = 0; i < gd.insts.size(); i++){
		const Inst &in = gd.insts[i];
		if((in.interior & 0xFF) != 0) continue;
		const ObjDef *o = gd.findObj(in.id);
		if(!o || (o->type != OT_OBJS && o->type != OT_TOBJ)) continue;
		float r = o->dffRadius > 0 ? o->dffRadius : (o->colEntry >= 0 ? gd.colEntries[(size_t)o->colEntry].radius : 0);
		if(r <= 0) continue;
		float top = in.pos[2] + r;
		for(size_t q = 0; q < gd.water.size(); q++){
			const WaterQuad &w = gd.water[q];
			if(in.pos[0] < w.x0 || in.pos[0] > w.x1 || in.pos[1] < w.y0 || in.pos[1] > w.y1) continue;
			if(top < w.z - 1.0f){
				if(++n <= 200)
					ctx.add(SEV_WARN, CAT_QUALITY, "QLT-05", gd.ipls[(size_t)in.fileIdx].logical, in.line, o->name,
					        fmt("«%s» целиком под водой: верх объекта (%.1f) на %.1f м ниже уровня воды %.1f — если это не дно/риф, ошибка в Z", o->name.c_str(), top, w.z - top, w.z), "", in.id);
			}
			break;
		}
	}
	if(n > 200) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-05", "IPL", -1, "", fmt("…и ещё %d объектов под водой", n - 200), "");
}

static void ruleSectors(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(gd.insts.empty()) return;
	const float half = gd.isSA() ? 3000.0f : 2000.0f, cell = 50.0f;
	const int n = (int)(half * 2 / cell);
	std::vector<int> cnt((size_t)n * n, 0);
	std::unordered_map<int, std::map<int, int> > byIpl;	// cell → IPL file index → objects (only cells that turn out hot are used)
	for(size_t i = 0; i < gd.insts.size(); i++){
		const Inst &in = gd.insts[i];
		if((in.interior & 0xFF) != 0) continue;
		const ObjDef *o = gd.findObj(in.id);
		if(!o || (o->type != OT_OBJS && o->type != OT_TOBJ) || o->isLod) continue;
		int cx = (int)floorf((in.pos[0] + half) / cell), cy = (int)floorf((in.pos[1] + half) / cell);
		if(cx < 0 || cy < 0 || cx >= n || cy >= n) continue;
		cnt[(size_t)cy * n + cx]++;
		byIpl[cy * n + cx][in.fileIdx]++;
	}
	// vanilla SA peaks at 79 objects in a 50×50 sector, FlareMTA at 126; three times vanilla is where
	// the sector lists and the render loop start to cost frames
	const int threshold = 250;
	int hot = 0, mx = 0;
	for(int cy = 0; cy < n; cy++) for(int cx = 0; cx < n; cx++){
		int c = cnt[(size_t)cy * n + cx];
		if(c > mx) mx = c;
		if(c > threshold && ++hot <= 100){
			// the IPL files behind the pile, biggest first; the row points at the biggest one («Показать IPL в 3D»)
			std::vector<std::pair<int, int> > top;	// objects, file
			for(auto &kv : byIpl[cy * n + cx]) top.push_back(std::make_pair(kv.second, kv.first));
			std::sort(top.begin(), top.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b){ return a.first > b.first; });
			std::string who;
			for(size_t k = 0; k < top.size() && k < 4; k++){
				if(!who.empty()) who += ", ";
				who += (top[k].second >= 0 && top[k].second < (int)gd.ipls.size() ? basename(gd.ipls[(size_t)top[k].second].logical) : std::string("?")) + fmt(" (%d)", top[k].first);
			}
			std::string file = !top.empty() && top[0].second >= 0 && top[0].second < (int)gd.ipls.size() ? gd.ipls[(size_t)top[0].second].logical : std::string("IPL");
			ctx.add(SEV_WARN, CAT_QUALITY, "QLT-06", file, -1, fmt("%d,%d", (int)(-half + cx * cell), (int)(-half + cy * cell)),
			        fmt("сектор x %d..%d, y %d..%d: %d объектов в 50×50 м (в ванили не больше ~80) — списки сектора и рендер этого квадрата тормозят; больше всего кладут: %s", (int)(-half + cx * cell), (int)(-half + (cx + 1) * cell), (int)(-half + cy * cell), (int)(-half + (cy + 1) * cell), c, who.c_str()),
			        "Объединяй мелочь в одну модель, лишние копии убери. Кнопка «Показать IPL в 3D» оставляет в 3D только объекты этого IPL и летит к сектору; тепловая карта — в панели «Карта».");
		}
	}
	if(hot > 100) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-06", "IPL", -1, "", fmt("…и ещё %d перегруженных секторов", hot - 100), "");
	// the heaviest streaming spot on the map
	std::vector<float> mb; std::vector<int> c2;
	const int hn = (int)(half * 2 / 100.0f);
	StreamHeat(gd, half, hn, mb, c2);
	int best = -1; float bm = 0;
	for(size_t i = 0; i < mb.size(); i++) if(mb[i] > bm){ bm = mb[i]; best = (int)i; }
	if(best >= 0){
		int bx = best % hn, by = best / hn;
		float sx = -half + (bx + 0.5f) * 100, sy = -half + (by + 0.5f) * 100;
		// the IPL files with the most objects within 150 m of the spot — the row points at the biggest («Показать IPL в 3D»)
		std::map<int, int> near;
		for(size_t i = 0; i < gd.insts.size(); i++){
			const Inst &in = gd.insts[i];
			if((in.interior & 0xFF) != 0) continue;
			float dx = in.pos[0] - sx, dy = in.pos[1] - sy;
			if(dx * dx + dy * dy <= 150.0f * 150.0f) near[in.fileIdx]++;
		}
		std::vector<std::pair<int, int> > top;
		for(auto &kv : near) top.push_back(std::make_pair(kv.second, kv.first));
		std::sort(top.begin(), top.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b){ return a.first > b.first; });
		std::string who;
		for(size_t k = 0; k < top.size() && k < 4; k++){
			if(!who.empty()) who += ", ";
			who += (top[k].second >= 0 && top[k].second < (int)gd.ipls.size() ? basename(gd.ipls[(size_t)top[k].second].logical) : std::string("?")) + fmt(" (%d)", top[k].first);
		}
		std::string file = !top.empty() && top[0].second >= 0 && top[0].second < (int)gd.ipls.size() ? gd.ipls[(size_t)top[0].second].logical : std::string("IPL");
		ctx.add(bm > 96.0f ? SEV_WARN : SEV_INFO, CAT_QUALITY, "QLT-06", file, -1, fmt("%d,%d", (int)sx, (int)sy),
		        fmt("самое тяжёлое место карты: около (%d, %d) в радиусе видимости %d объектов на %.0f МБ моделей и текстур%s (самый нагруженный сектор — %d объектов); в 150 м больше всего объектов из: %s", (int)sx, (int)sy, c2[(size_t)best], bm, bm > 96.0f ? " — больше стандартного бюджета стриминга, будут подгрузки и пропадания" : "", mx, who.c_str()),
		        "Тепловая карта нагрузки — в панели «Карта» (слой «стриминг»); «Показать IPL в 3D» оставляет в 3D объекты одного файла и летит к месту.");
	}
}

static uint32_t jamcrcUpper(const std::string &s)
{
	static uint32_t table[256]; static bool init = false;
	if(!init){ for(uint32_t i = 0; i < 256; i++){ uint32_t c = i; for(int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
	uint32_t crc = 0xFFFFFFFFu;
	for(size_t i = 0; i < s.size(); i++){ unsigned char c = (unsigned char)s[i]; if(c >= 'a' && c <= 'z') c = (unsigned char)(c - 32); crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8); }
	return crc;
}

static void ruleGxtNames(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA() || gd.gxtMainHashes.empty()) return;
	int n = 0;
	for(size_t i = 0; i < gd.zones.size(); i++){
		const ZoneRef &z = gd.zones[i];
		std::string key = z.gxt.size() > 7 ? z.gxt.substr(0, 7) : z.gxt;
		if(key.empty() || lower(key) == "unused" || gd.gxtMainHashes.count(jamcrcUpper(key))) continue;
		if(++n <= 200) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-07", z.file, z.line, z.name, fmt("зона «%s»: gxt-имени «%s» нет в MAIN american.gxt — название района на экране будет пустым", z.name.c_str(), key.c_str()), "Добавь ключ в GXT (таблица MAIN) или используй существующий.");
	}
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.type != OT_CARS || o.tok.size() < 6 || lower(o.tok[3]) == "trailer") continue;	// trailers cannot be entered: no name shown
		const std::string &key = o.tok[5];
		if(key.empty() || key == "null" || gd.gxtMainHashes.count(jamcrcUpper(key))) continue;
		if(++n <= 200) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-07", o.file, o.line, o.name, fmt("машина «%s»: gxt-имени «%s» нет в MAIN american.gxt — при посадке название не покажется", o.name.c_str(), key.c_str()), "", o.id);
	}
	if(n > 200) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-07", "GXT", -1, "", fmt("…и ещё %d имён без ключа GXT", n - 200), "");
}

static void ruleModConflicts(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.modloaderActive || gd.modFiles.empty()) return;
	std::map<std::string, std::vector<int>> byName;	// asset name / data rel path → mod files
	for(size_t i = 0; i < gd.modFiles.size(); i++){
		const ModFile &mf = gd.modFiles[i];
		std::string ext = extOf(mf.rel);
		std::string key;
		if(ext == "dff" || ext == "txd" || ext == "col" || ext == "ifp") key = "asset:" + basename(mf.rel);
		else if(ext == "ide" || ext == "ipl" || ext == "dat" || ext == "cfg" || ext == "gxt" || ext == "scm" || ext == "img" || ext == "fxp" || ext == "zon") key = "data:" + mf.rel;
		else continue;
		byName[key].push_back((int)i);
	}
	int n = 0;
	for(std::map<std::string, std::vector<int>>::const_iterator it = byName.begin(); it != byName.end(); ++it){
		const std::vector<int> &v = it->second;
		std::set<std::string> mods;
		for(size_t k = 0; k < v.size(); k++) mods.insert(gd.modFiles[(size_t)v[k]].mod);
		if(mods.size() < 2) continue;
		int win = v[0];
		for(size_t k = 1; k < v.size(); k++) if(gd.modFiles[(size_t)v[k]].priority >= gd.modFiles[(size_t)win].priority) win = v[k];
		std::string list; int c = 0;
		for(std::set<std::string>::const_iterator m = mods.begin(); m != mods.end(); ++m){ if(c++) list += ", "; list += *m; }
		std::string name = it->first.substr(it->first.find(':') + 1);
		if(++n <= 200)
			ctx.add(SEV_WARN, CAT_QUALITY, "QLT-08", "modloader/" + gd.modFiles[(size_t)win].mod, -1, name,
			        fmt("«%s» есть в %d модах (%s) — modloader берёт версию из «%s» (приоритет %d), остальные молча проигнорированы", name.c_str(), (int)mods.size(), list.c_str(), gd.modFiles[(size_t)win].mod.c_str(), gd.modFiles[(size_t)win].priority),
			        "Если нужна другая — подними приоритет мода в modloader.ini или убери дубликат.");
	}
	if(n > 200) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-08", "modloader", -1, "", fmt("…и ещё %d файлов с конфликтом модов", n - 200), "");
}

static void ruleUnused(Context &ctx)
{
	GameData &gd = *ctx.gd;
	std::vector<UnusedFile> u;
	CollectUnused(gd, u);
	if(u.empty()) return;
	double bytes = 0; int n = 0;
	for(size_t i = 0; i < u.size(); i++){
		const Entry &e = gd.entries[(size_t)u[i].entry];
		bytes += e.sizeSectors * 2048.0;
		if(++n <= 300) ctx.add(SEV_INFO, CAT_QUALITY, "QLT-09", entryWhere(gd, e), e.dirIndex, e.name, fmt("%s (%u КБ)", u[i].why.c_str(), e.sizeSectors * 2), "");
	}
	ctx.add(SEV_INFO, CAT_QUALITY, "QLT-09", "IMG", -1, "", fmt("%d файлов в архивах ни на что не ссылаются (%.1f МБ) — список с размерами: кнопка «Лишнее»; special-модели main.scm грузятся по имени и тоже попадают сюда", (int)u.size(), bytes / 1048576.0), "");
}

// ---- QLT-10: the same model twice in one spot ----
static void ruleDuplicates(Context &ctx)
{
	GameData &gd = *ctx.gd;
	std::unordered_map<std::string, std::vector<int> > cells;	// 2 m grid over id
	auto key = [](int id, float x, float y, float z){ return fmt("%d|%d|%d|%d", id, (int)floorf(x / 2.0f), (int)floorf(y / 2.0f), (int)floorf(z / 2.0f)); };
	for(size_t i = 0; i < gd.insts.size(); i++){
		const Inst &in = gd.insts[i];
		const ObjDef *o = gd.findObj(in.id);
		if(!o || (o->type != OT_OBJS && o->type != OT_TOBJ)) continue;
		cells[key(in.id, in.pos[0], in.pos[1], in.pos[2])].push_back((int)i);
	}
	int n = 0;
	std::set<int> done;
	for(auto &kv : cells){
		const std::vector<int> &v = kv.second;
		if(v.size() < 2) continue;
		for(size_t a = 0; a < v.size(); a++) for(size_t b = a + 1; b < v.size(); b++){
			const Inst &p = gd.insts[(size_t)v[a]], &q = gd.insts[(size_t)v[b]];
			float dx = p.pos[0] - q.pos[0], dy = p.pos[1] - q.pos[1], dz = p.pos[2] - q.pos[2];
			if(dx * dx + dy * dy + dz * dz > 0.25f * 0.25f) continue;	// same spot within 25 cm
			if(done.count(v[b])) continue;
			done.insert(v[b]);
			const ObjDef *o = gd.findObj(p.id);
			if(++n <= 300){
				ctx.add(SEV_WARN, CAT_QUALITY, "QLT-10", gd.ipls[(size_t)q.fileIdx].logical, q.line, o ? o->name : fmt("id %d", p.id),
				        fmt("«%s» стоит дважды в точке (%.1f, %.1f, %.1f): вторая копия в %s:%d — z-fighting, двойная коллизия, двойной вес", o ? o->name.c_str() : "?", p.pos[0], p.pos[1], p.pos[2], basename(gd.ipls[(size_t)p.fileIdx].logical).c_str(), p.line),
				        "Удали одну из копий (обычно та, что пришла из второго мода / IPL).", p.id);
				if(done.insert(v[a]).second)	// the first copy gets its own row too, so both lines are in the table (the user asked to see the duplicates)
					ctx.add(SEV_WARN, CAT_QUALITY, "QLT-10", gd.ipls[(size_t)p.fileIdx].logical, p.line, o ? o->name : fmt("id %d", p.id),
					        fmt("«%s» стоит дважды в точке (%.1f, %.1f, %.1f): это первая копия, дубль в %s:%d", o ? o->name.c_str() : "?", p.pos[0], p.pos[1], p.pos[2], basename(gd.ipls[(size_t)q.fileIdx].logical).c_str(), q.line),
					        "Удали одну из копий (обычно та, что пришла из второго мода / IPL).", p.id);
			}
		}
	}
	if(n > 300) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-10", "IPL", -1, "", fmt("…и ещё %d двойных расстановок", n - 300), "");
}

// ---- QLT-11: IDE alpha flags vs the textures ----
// SA: flags 4 / 8 put the model into the alpha render lists (sorted, drawn after the opaque pass);
// without them a texture with an alpha channel is drawn opaque — smooth alpha turns into a hard
// cut-out or black. The other way round the model is sorted for nothing.
static bool texHasAlpha(const GameData &gd, int slot, const std::string &texLower, int depth = 0)
{
	if(slot < 0 || slot >= (int)gd.txdSlots.size() || depth > 8) return false;
	auto it = gd.txdTextures.find(gd.txdSlots[(size_t)slot].name);
	if(it != gd.txdTextures.end()) for(size_t k = 0; k < it->second.texs.size(); k++) if(it->second.texs[k].name == texLower) return it->second.texs[k].alpha;
	return texHasAlpha(gd, gd.txdSlots[(size_t)slot].parent, texLower, depth + 1);
}
// Is the texture's alpha smooth (gradients, glass, shadows) or a hard cut-out (leaves, fences)? The
// engine's alpha test handles cut-outs in the opaque pass; only smooth alpha needs the sorted lists.
// Decoded on demand, cached per TXD + texture.
static bool texAlphaSmooth(const GameData &gd, int slot, const std::string &texLower, std::map<std::string, bool> &cache)
{
	int s = slot, depth = 0;
	while(s >= 0 && depth++ < 8){
		const std::string &nm = gd.txdSlots[(size_t)s].name;
		auto it = gd.txdTextures.find(nm);
		bool here = false;
		if(it != gd.txdTextures.end()) for(size_t k = 0; k < it->second.texs.size(); k++) if(it->second.texs[k].name == texLower){ here = true; break; }
		if(here){
			std::string key = nm + "|" + texLower;
			auto c = cache.find(key);
			if(c != cache.end()) return c->second;
			bool smooth = false;
			int e = gd.txdSlots[(size_t)s].entry;
			std::vector<uint8_t> data; std::string err; TxdFile f;
			if(e >= 0 && ReadEntryTrimmed(gd, e, data, err) && TxdParse(data, f)){
				for(size_t k = 0; k < f.tex.size(); k++){
					if(lower(f.tex[k].name) != texLower) continue;
					std::vector<uint8_t> rgba;
					if(TxdDecode(f.tex[k], rgba) && f.tex[k].width > 0 && f.tex[k].height > 0){
						size_t n = (size_t)f.tex[k].width * f.tex[k].height, mid = 0;
						for(size_t p = 0; p < n; p++){ uint8_t a = rgba[p * 4 + 3]; if(a > 24 && a < 231) mid++; }
						smooth = mid * 100 >= n * 30;	// ≥ 30 % of the pixels partially transparent (fences and leaves stay below with their antialiased edges)
					}
					break;
				}
			}
			cache[key] = smooth;
			return smooth;
		}
		s = gd.txdSlots[(size_t)s].parent;
	}
	return false;
}
static void ruleAlphaFlags(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA() || gd.txdTextures.empty()) return;
	ctx.prog->set("Качество: альфа текстур");
	std::map<std::string, bool> smoothCache;
	int missing = 0, extra = 0;
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if((o.type != OT_OBJS && o.type != OT_TOBJ) || !o.dffParsed || o.dffTex.empty() || o.txdSlot < 0) continue;
		if(gd.txdTextures.find(gd.txdSlots[(size_t)o.txdSlot].name) == gd.txdTextures.end()) continue;	// TXD not parsed: unknown
		if(ctx.cancelled()) return;
		std::string alphaTex, smoothTex;
		bool flag = (o.flags & 0xC) != 0;
		for(size_t k = 0; k < o.dffTex.size(); k++) if(texHasAlpha(gd, o.txdSlot, o.dffTex[k])){
			if(alphaTex.empty()) alphaTex = o.dffTex[k];
			if(!flag && texAlphaSmooth(gd, o.txdSlot, o.dffTex[k], smoothCache)){ smoothTex = o.dffTex[k]; break; }
		}
		if(!smoothTex.empty() && !flag){
			if(++missing <= 300)
				ctx.add(SEV_WARN, CAT_QUALITY, "QLT-11", o.file, o.line, o.name,
				        fmt("текстура «%s» с плавной альфой (полупрозрачные пиксели), а у модели нет флага прозрачности (4/8) в IDE — рисуется в непрозрачном проходе: полупрозрачные части (стекло, тени) могут стать ступенчатыми или чёрными и перекрывать объекты позади", smoothTex.c_str()),
				        "Поставь флаг 4 (alpha 1) — кнопка «Исправить: авто» добавит его в строку IDE. Жёсткие вырезы (листва, решётки) флага не требуют — их режет alpha test.", o.id, FIX_IDE_ALPHA);
		}else if(alphaTex.empty() && flag && o.numInstances > 0){
			if(++extra <= 200)
				ctx.add(SEV_INFO, CAT_QUALITY, "QLT-11", o.file, o.line, o.name, "флаг прозрачности в IDE, но ни одна текстура модели не имеет альфы — лишняя сортировка в alpha-списке", "", o.id);
		}
	}
	if(missing > 300) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-11", "IDE", -1, "", fmt("…и ещё %d моделей с альфой без флага", missing - 300), "");
}

// ---- QLT-12: textures in a TXD no material uses; QLT-13: the same texture in many TXDs ----
// which textures of which TXD are used: every model marks its names in the first TXD of its chain that has them
static void textureUsage(const GameData &gd, std::unordered_map<std::string, std::set<std::string> > &used, std::set<std::string> &anyModel, std::set<std::string> &unknown)
{
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.txdSlot < 0) continue;
		if(!o.dffParsed){ unknown.insert(gd.txdSlots[(size_t)o.txdSlot].name); continue; }
		anyModel.insert(gd.txdSlots[(size_t)o.txdSlot].name);
		for(size_t k = 0; k < o.dffTex.size(); k++){
			int slot = o.txdSlot; int depth = 0;
			while(slot >= 0 && depth++ < 8){
				const std::string &nm = gd.txdSlots[(size_t)slot].name;
				auto it = gd.txdTextures.find(nm);
				if(it != gd.txdTextures.end() && std::find(it->second.names.begin(), it->second.names.end(), o.dffTex[k]) != it->second.names.end()){ used[nm].insert(o.dffTex[k]); break; }
				slot = gd.txdSlots[(size_t)slot].parent;
			}
		}
	}
}
// a dictionary whose usage is certain: parsed, every model parsed, not a system TXD, not a txdp parent
static bool deadTexturesOf(const GameData &gd, const std::string &nm, const TxdInfo &ti, const std::unordered_map<std::string, std::set<std::string> > &used,
                           const std::set<std::string> &anyModel, const std::set<std::string> &unknown, std::vector<std::string> &dead, uint32_t &bytes)
{
	dead.clear(); bytes = 0;
	if(!ti.parsed || ti.texs.empty() || !anyModel.count(nm) || unknown.count(nm) || txdIsSystem(nm)) return false;
	int slot = gd.findTxdSlot(nm);
	if(slot < 0) return false;
	for(size_t s = 0; s < gd.txdSlots.size(); s++) if(gd.txdSlots[s].parent == slot) return false;	// a txdp parent serves other TXDs' models — every child's materials were counted, but be safe
	auto uit = used.find(nm);
	static const std::set<std::string> none;
	const std::set<std::string> &u = uit == used.end() ? none : uit->second;
	for(size_t k = 0; k < ti.texs.size(); k++){
		if(ti.texs[k].name.empty() || u.count(ti.texs[k].name)) continue;
		dead.push_back(ti.texs[k].name); bytes += ti.texs[k].bytes;
	}
	return true;
}
bool DeadTextures(const GameData &gd, const std::string &txdLower, std::vector<std::string> &out)
{
	auto it = gd.txdTextures.find(txdLower);
	if(it == gd.txdTextures.end()) return false;
	std::unordered_map<std::string, std::set<std::string> > used;
	std::set<std::string> anyModel, unknown;
	textureUsage(gd, used, anyModel, unknown);
	uint32_t bytes;
	return deadTexturesOf(gd, txdLower, it->second, used, anyModel, unknown, out, bytes);
}
static void ruleTextureUse(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(gd.txdTextures.empty()) return;
	std::unordered_map<std::string, std::set<std::string> > used;	// txd name → texture names
	std::set<std::string> anyModel, unknown;			// TXDs with at least one parsed model / with a model whose DFF was not parsed (usage unknown)
	textureUsage(gd, used, anyModel, unknown);
	if(const char *dbgTxd = getenv("GTACHECK_TEXUSE")){	// GTACHECK_TEXUSE=<txd>: every model of the TXD with its material textures (debugging QLT-12)
		FILE *f = fopen("gta_check_texuse.txt", "wb");
		if(f){
			for(size_t i = 0; i < gd.objs.size(); i++){
				const ObjDef &o = gd.objs[i];
				if(o.txdSlot < 0 || lower(gd.txdSlots[(size_t)o.txdSlot].name) != lower(dbgTxd)) continue;
				fprintf(f, "%s id %d type %d entry %d parsed %d failed %d tex:", o.name.c_str(), o.id, o.type, o.dffEntry, o.dffParsed ? 1 : 0, o.dffFailed ? 1 : 0);
				for(size_t k = 0; k < o.dffTex.size(); k++) fprintf(f, " %s", o.dffTex[k].c_str());
				fprintf(f, "\n");
			}
			auto ti = gd.txdTextures.find(lower(dbgTxd));
			if(ti != gd.txdTextures.end()){ fprintf(f, "TXD names (%d):", (int)ti->second.names.size()); for(size_t k = 0; k < ti->second.names.size(); k++) fprintf(f, " %s", ti->second.names[k].c_str()); fprintf(f, "\n"); }
			else fprintf(f, "TXD not in txdTextures\n");
			auto ui = used.find(lower(dbgTxd));
			if(ui != used.end()){ fprintf(f, "used (%d):", (int)ui->second.size()); for(auto &nm : ui->second) fprintf(f, " %s", nm.c_str()); fprintf(f, "\n"); }
			int slot = gd.findTxdSlot(lower(dbgTxd)); fprintf(f, "slot %d parent %d\n", slot, slot >= 0 ? gd.txdSlots[(size_t)slot].parent : -2);
			fclose(f);
		}
	}
	int n = 0; double deadBytes = 0;
	for(auto &kv : gd.txdTextures){
		const std::string &nm = kv.first; const TxdInfo &ti = kv.second;
		std::vector<std::string> deadList; uint32_t bytes = 0;
		if(!deadTexturesOf(gd, nm, ti, used, anyModel, unknown, deadList, bytes) || deadList.empty()) continue;
		int dead = (int)deadList.size();
		int slotN = gd.findTxdSlot(nm), models = 0;
		for(size_t i = 0; i < gd.objs.size(); i++) if(gd.objs[i].txdSlot == slotN) models++;
		std::string list;
		for(int k = 0; k < dead && k < 6; k++){ if(!list.empty()) list += ", "; list += deadList[(size_t)k]; }
		deadBytes += bytes;
		int e = gd.findEntry(nm + ".txd");
		if(++n <= 300)
			ctx.add(bytes >= 1024 * 1024 ? SEV_WARN : SEV_INFO, CAT_QUALITY, "QLT-12", e >= 0 ? entryWhere(gd, gd.entries[(size_t)e]) : nm + ".txd", -1, nm,
			        fmt("%d из %d текстур не использует ни одна из %d моделей этого TXD (%u КБ): %s%s — игра ищет текстуры только в TXD модели и его txdp-родителях, копии тех же имён в других словарях не считаются", dead, (int)ti.texs.size(), models, bytes / 1024, list.c_str(), dead > 6 ? ", …" : ""),
			        "Удали лишние текстуры из TXD — они грузятся в память вместе со словарём. «Исправить» удаляет их из файла (оригинал остаётся в бэкапе).", -1, FIX_TXD_DEADTEX, e);
	}
	if(n) ctx.add(SEV_INFO, CAT_QUALITY, "QLT-12", "TXD", -1, "", fmt("мёртвых текстур в TXD: %.1f МБ суммарно в %d словарях", deadBytes / 1048576.0, n), "");
	// the same texture (name + size) in many TXDs
	std::map<std::string, std::pair<int, uint32_t> > dup;	// name|w|h → (TXDs, bytes each)
	for(auto &kv : gd.txdTextures){
		if(!kv.second.parsed || txdIsSystem(kv.first)) continue;
		std::set<std::string> seen;
		for(size_t k = 0; k < kv.second.texs.size(); k++){
			const TxdInfo::Tex &t = kv.second.texs[k];
			if(t.name.empty() || t.w < 64) continue;
			std::string key = fmt("%s|%d|%d", t.name.c_str(), t.w, t.h);
			if(!seen.insert(key).second) continue;
			std::pair<int, uint32_t> &d = dup[key]; d.first++; d.second = t.bytes;
		}
	}
	std::vector<std::pair<double, std::string> > worst;
	for(auto &kv : dup) if(kv.second.first >= 5) worst.push_back(std::make_pair((double)kv.second.second * (kv.second.first - 1), kv.first));
	std::sort(worst.begin(), worst.end()); std::reverse(worst.begin(), worst.end());
	double total = 0; for(size_t i = 0; i < worst.size(); i++) total += worst[i].first;
	for(size_t i = 0; i < worst.size() && i < 100; i++){
		const std::pair<int, uint32_t> &d = dup[worst[i].second];
		std::string name = worst[i].second.substr(0, worst[i].second.find('|'));
		ctx.add(d.second * (d.first - 1) >= 2u * 1024 * 1024 ? SEV_WARN : SEV_INFO, CAT_QUALITY, "QLT-13", "TXD", -1, name,
		        fmt("текстура «%s» (%u КБ) лежит в %d разных TXD — в памяти по копии на каждый словарь, лишних %.1f МБ", name.c_str(), d.second / 1024, d.first, worst[i].first / 1048576.0),
		        "Вынеси общие текстуры в один TXD и подключи его родителем через txdp (или в generic.txd).");
	}
	if(!worst.empty()) ctx.add(SEV_INFO, CAT_QUALITY, "QLT-13", "TXD", -1, "", fmt("%d текстур повторяются в ≥ 5 словарях — лишних %.1f МБ в памяти при полной загрузке", (int)worst.size(), total / 1048576.0), "");
}

// ---- QLT-14: IMG entries that overlap / gaps in the archive ----
static void ruleImgLayout(Context &ctx)
{
	GameData &gd = *ctx.gd;
	for(size_t a = 0; a < gd.archives.size(); a++){
		std::vector<std::pair<uint32_t, int> > order;	// offset, entry
		for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].img == (int)a && gd.entries[i].loose.empty()) order.push_back(std::make_pair(gd.entries[i].offset, (int)i));
		if(order.size() < 2) continue;
		std::sort(order.begin(), order.end());
		uint64_t gap = 0; int overlaps = 0;
		for(size_t k = 1; k < order.size(); k++){
			const Entry &p = gd.entries[(size_t)order[k - 1].second], &q = gd.entries[(size_t)order[k].second];
			uint32_t pEnd = p.offset + (p.sizeSectors ? p.sizeSectors : p.sizeArchive);
			if(q.offset < pEnd){
				if(++overlaps <= 100)
					ctx.add(SEV_ERROR, CAT_QUALITY, "QLT-14", basename(gd.archives[a].logical), q.dirIndex, q.name,
					        fmt("запись «%s» (сектор %u, %u сект.) накладывается на «%s» (сектор %u, %u сект.) — они делят байты: перезапись одной портит другую, стриминг читает чужой хвост", q.name.c_str(), q.offset, q.sizeSectors, p.name.c_str(), p.offset, p.sizeSectors),
					        "Пересобери архив (IMG Tool / Modloader-папка вместо правки на месте).");
			}else gap += q.offset - pEnd;
		}
		if(overlaps > 100) ctx.add(SEV_ERROR, CAT_QUALITY, "QLT-14", basename(gd.archives[a].logical), -1, "", fmt("…и ещё %d наложений записей", overlaps - 100), "");
		if(gap * 2048 >= 8ull * 1024 * 1024) ctx.add(SEV_INFO, CAT_QUALITY, "QLT-14", basename(gd.archives[a].logical), -1, "", fmt("между записями %.1f МБ неиспользуемых секторов (после удалений/замен) — архив можно ужать пересборкой", gap * 2048 / 1048576.0), "");
	}
}

// ---- QLT-15: floating / buried objects ----
// A ray straight down (and up) from the placement against the collision meshes of the neighbours:
// nothing under the object's bottom within 1.5 m (or 30 m at all) = it hangs in the air; the
// highest surface at that spot above the object's top = it is buried. Water counts as support.
struct ColWorld { int inst; float bmin[3], bmax[3]; };	// world AABB of one placement's COL (sphere-based)
static bool rayTri(const float *o, float dirZ, const float *a, const float *b, const float *c, float &t)
{
	// vertical ray (0,0,dirZ): 2D point-in-triangle in XY, then the plane height
	float d1 = (b[0] - a[0]) * (o[1] - a[1]) - (b[1] - a[1]) * (o[0] - a[0]);
	float d2 = (c[0] - b[0]) * (o[1] - b[1]) - (c[1] - b[1]) * (o[0] - b[0]);
	float d3 = (a[0] - c[0]) * (o[1] - c[1]) - (a[1] - c[1]) * (o[0] - c[0]);
	bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
	if(neg && pos) return false;
	float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
	float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
	if(fabsf(nz) < 1e-6f) return false;
	float z = a[2] - (nx * (o[0] - a[0]) + ny * (o[1] - a[1])) / nz;
	t = (z - o[2]) * (dirZ > 0 ? 1.0f : -1.0f);
	return t >= 0;
}
static void ruleFloating(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(gd.colEntries.empty() || gd.insts.empty()) return;
	ctx.prog->set("Качество: опора объектов");
	// COL shapes per model (loaded lazily, kept for the pass)
	std::unordered_map<int, ColShape> shapes; std::set<int> failed;
	auto shapeOf = [&](int id) -> const ColShape* {
		auto it = shapes.find(id);
		if(it != shapes.end()) return &it->second;
		if(failed.count(id)) return nullptr;
		const ObjDef *o = gd.findObj(id);
		ColShape cs; std::string err;
		if(!o || o->colEntry < 0 || !LoadColShape(gd, o->colEntry, cs, err) || (cs.faces.empty() && cs.boxes.empty())){ failed.insert(id); return nullptr; }
		return &shapes.emplace(id, cs).first->second;
	};
	// spatial grid of placements with collision (by COL sphere), 50 m cells
	const float half = gd.isSA() ? 3000.0f : 2000.0f, cell = 50.0f; const int n = (int)(half * 2 / cell);
	std::vector<std::vector<int> > grid((size_t)n * n);
	std::vector<float> radius(gd.insts.size(), 0.0f);
	for(size_t i = 0; i < gd.insts.size(); i++){
		const Inst &in = gd.insts[i];
		const ObjDef *o = gd.findObj(in.id);
		if(!o || o->colEntry < 0 || (in.interior & 0xFF) != 0) continue;
		const ColEntry &ce = gd.colEntries[(size_t)o->colEntry];
		if(!ce.hasVolumes || ce.radius <= 0) continue;
		radius[i] = ce.radius + sqrtf(ce.center[0] * ce.center[0] + ce.center[1] * ce.center[1] + ce.center[2] * ce.center[2]);
		int x0 = (int)floorf((in.pos[0] - radius[i] + half) / cell), x1 = (int)floorf((in.pos[0] + radius[i] + half) / cell);
		int y0 = (int)floorf((in.pos[1] - radius[i] + half) / cell), y1 = (int)floorf((in.pos[1] + radius[i] + half) / cell);
		for(int cy = y0; cy <= y1; cy++) for(int cx = x0; cx <= x1; cx++) if(cx >= 0 && cy >= 0 && cx < n && cy < n) grid[(size_t)cy * n + cx].push_back((int)i);
	}
	// world-space transform of a COL vertex through the instance's rotation (quaternion) + position
	auto xform = [&](const Inst &in, const float *v, float *out){
		float qx = in.rot[0], qy = in.rot[1], qz = in.rot[2], qw = in.rot[3];
		// SA loader: tiny X/Y → heading only; conjugate quaternion rotation (same as the 3D view)
		float x = v[0], y = v[1], z = v[2];
		if(gd.isSA() && fabsf(qx) <= 0.05f && fabsf(qy) <= 0.05f && !((in.interior & 0x200) && qx != 0 && qy != 0)){
			float ww = qw; if(ww < -1) ww = -1; if(ww > 1) ww = 1;
			float h = acosf(ww) * (qz < 0 ? 2.0f : -2.0f), s = sinf(h), c = cosf(h);
			out[0] = c * x - s * y; out[1] = s * x + c * y; out[2] = z;
		}else{
			// rotate by conj(q): q' = (-x,-y,-z,w)
			qx = -qx; qy = -qy; qz = -qz;
			float tx = 2 * (qy * z - qz * y), ty = 2 * (qz * x - qx * z), tz = 2 * (qx * y - qy * x);
			out[0] = x + qw * tx + (qy * tz - qz * ty); out[1] = y + qw * ty + (qz * tx - qx * tz); out[2] = z + qw * tz + (qx * ty - qy * tx);
		}
		out[0] += in.pos[0]; out[1] += in.pos[1]; out[2] += in.pos[2];
	};
	int floating = 0, buried = 0, tested = 0;
	std::vector<float> wv;	// scratch: transformed vertices of one neighbour
	for(size_t i = 0; i < gd.insts.size(); i++){
		if(ctx.cancelled()) return;
		const Inst &in = gd.insts[i];
		const ObjDef *o = gd.findObj(in.id);
		if(!o || (o->type != OT_OBJS && o->type != OT_TOBJ) || o->isLod || (in.interior & 0xFF) != 0 || o->colEntry < 0) continue;
		const ColEntry &ce = gd.colEntries[(size_t)o->colEntry];
		if(!ce.hasVolumes || ce.radius <= 0 || ce.radius > 40.0f) continue;	// big pieces (terrain, buildings) are the support, not the supported
		if(o->dffRadius > 40.0f) continue;
		// bottom / top of the object: the COL box rotated
		float bottom = 1e9f, top = -1e9f, cx = 0, cy = 0;
		for(int c = 0; c < 8; c++){
			float v[3] = { (c & 1) ? ce.bmax[0] : ce.bmin[0], (c & 2) ? ce.bmax[1] : ce.bmin[1], (c & 4) ? ce.bmax[2] : ce.bmin[2] }, w[3];
			xform(in, v, w);
			if(w[2] < bottom) bottom = w[2]; if(w[2] > top) top = w[2];
			cx += w[0] * 0.125f; cy += w[1] * 0.125f;
		}
		if(top - bottom < 0.3f) continue;	// flat decals / ground patches
		tested++;
		cx = in.pos[0]; cy = in.pos[1];	// the pivot is where the object stands (a canopy centre would hang over a slope)
		float origin[3] = { cx, cy, top + 0.05f };
		// the highest collision surface at (cx, cy) below the top, and anything above the top
		float best = -1e9f; bool found = false; float above = 1e9f;
		int gx = (int)floorf((cx + half) / cell), gy = (int)floorf((cy + half) / cell);
		if(gx < 0 || gy < 0 || gx >= n || gy >= n) continue;
		const std::vector<int> &cand = grid[(size_t)gy * n + gx];
		for(size_t k = 0; k < cand.size(); k++){
			int j = cand[k];
			if(j == (int)i) continue;
			const Inst &nb = gd.insts[(size_t)j];
			float dx = nb.pos[0] - cx, dy = nb.pos[1] - cy;
			if(dx * dx + dy * dy > radius[(size_t)j] * radius[(size_t)j]) continue;
			const ColShape *cs = shapeOf(nb.id);
			if(!cs) continue;
			wv.resize(cs->verts.size());
			for(size_t v = 0; v + 2 < cs->verts.size(); v += 3) xform(nb, &cs->verts[v], &wv[v]);
			for(size_t f = 0; f + 2 < cs->faces.size(); f += 3){
				const float *a = &wv[(size_t)cs->faces[f] * 3], *b = &wv[(size_t)cs->faces[f + 1] * 3], *c = &wv[(size_t)cs->faces[f + 2] * 3];
				float t;
				if(rayTri(origin, -1.0f, a, b, c, t)){ float z = origin[2] - t; if(z > best){ best = z; found = true; } }
			}
			// boxes: axis-aligned in model space — transform the 8 corners and take the box top as a flat surface
			for(size_t bx = 0; bx + 5 < cs->boxes.size(); bx += 6){
				float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
				for(int c = 0; c < 8; c++){ float v[3] = { (c & 1) ? cs->boxes[bx + 3] : cs->boxes[bx], (c & 2) ? cs->boxes[bx + 4] : cs->boxes[bx + 1], (c & 4) ? cs->boxes[bx + 5] : cs->boxes[bx + 2] }, w[3]; xform(nb, v, w); for(int q = 0; q < 3; q++){ if(w[q] < mn[q]) mn[q] = w[q]; if(w[q] > mx[q]) mx[q] = w[q]; } }
				if(cx < mn[0] || cx > mx[0] || cy < mn[1] || cy > mx[1]) continue;
				if(mx[2] <= origin[2] && mx[2] > best){ best = mx[2]; found = true; }
			}
		}
		// water as support
		for(size_t q = 0; q < gd.water.size(); q++){ const WaterQuad &w = gd.water[q]; if(cx >= w.x0 && cx <= w.x1 && cy >= w.y0 && cy <= w.y1 && w.z <= origin[2] && w.z > best){ best = w.z; found = true; } }
		std::string where = gd.ipls[(size_t)in.fileIdx].logical;
		(void)above;
		if(found && bottom - best > 1.5f && bottom - best < 200.0f){
			if(++floating <= 300)
				ctx.add(SEV_WARN, CAT_QUALITY, "QLT-15", where, in.line, o->name, fmt("«%s» висит в воздухе: низ объекта на %.1f м выше ближайшей опоры (коллизии соседей / воды) под точкой (%.1f, %.1f)", o->name.c_str(), bottom - best, cx, cy), "Опусти Z в IPL или проверь, что опора (земля, здание) действительно есть под ним.", in.id);
		}else if(!found && bottom > -50.0f){
			if(++floating <= 300)
				ctx.add(SEV_INFO, CAT_QUALITY, "QLT-15", where, in.line, o->name, fmt("«%s»: под объектом нет никакой коллизии в точке (%.1f, %.1f) — висит над пустотой или над объектом без COL", o->name.c_str(), cx, cy), "", in.id);
		}
		// «buried» (support above the top) is not reported: wall panels, signs and lamps legitimately sit inside a building's COL box
	}
	if(floating > 300) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-15", "IPL", -1, "", fmt("…и ещё %d висящих объектов", floating - 300), "");
	if(buried > 300) ctx.add(SEV_WARN, CAT_QUALITY, "QLT-15", "IPL", -1, "", fmt("…и ещё %d объектов под поверхностью", buried - 300), "");
	if(tested) ctx.add(SEV_INFO, CAT_QUALITY, "QLT-15", "IPL", -1, "", fmt("опора проверена у %d объектов (радиус COL ≤ 40 м): висят %d, под поверхностью %d", tested, floating, buried), "");
}

// ---- QLT-16: no night vertex colours ----
static void ruleNightColours(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	int n = 0;
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if((o.type != OT_OBJS && o.type != OT_TOBJ) || !o.dffParsed || o.numInstances == 0 || !o.dffPrelit || o.dffNight) continue;
		if(o.dffRadius < 3.0f) continue;	// small props: not worth a note
		if(++n <= 300)
			ctx.add(SEV_INFO, CAT_QUALITY, "QLT-16", o.file, o.line, o.name, fmt("«%s»: vertex colours без ночного набора (Night Vertex Colours 0x253F2F9) — ночью модель остаётся дневной яркости, выделяется на фоне соседей", o.name.c_str()), "Экспортируй с ночными цветами (Extra Vert Colours) или запеки более тёмный prelit.", o.id);
	}
	if(n > 300) ctx.add(SEV_INFO, CAT_QUALITY, "QLT-16", "IDE", -1, "", fmt("…и ещё %d моделей без ночных цветов", n - 300), "");
}

void CheckQuality(Context &ctx)
{
	ctx.prog->set("Качество сборки");
	if(ctx.cancelled()) return; ruleDrawDist(ctx);
	if(ctx.cancelled()) return; ruleProcObj(ctx);
	if(ctx.cancelled()) return; ruleHeavyTxd(ctx);
	if(ctx.cancelled()) return; ruleUnderWater(ctx);
	if(ctx.cancelled()) return; ruleSectors(ctx);
	if(ctx.cancelled()) return; ruleGxtNames(ctx);
	if(ctx.cancelled()) return; ruleModConflicts(ctx);
	if(ctx.cancelled()) return; ruleUnused(ctx);
	if(ctx.cancelled()) return; ruleDuplicates(ctx);
	if(ctx.cancelled()) return; ruleAlphaFlags(ctx);
	if(ctx.cancelled()) return; ruleTextureUse(ctx);
	if(ctx.cancelled()) return; ruleImgLayout(ctx);
	if(ctx.cancelled()) return; ruleNightColours(ctx);
	// QLT-15 (floating objects, «опусти Z») is switched off at the user's request (16.09) — too many judgement calls
	(void)ruleFloating;
}

} // namespace gc
