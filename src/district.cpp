// district.cpp — «оптимизация по районам»: the map cut into a grid, and the text IPL placements, the IDE
// definitions, the COL entries and the LOD textures of every cell moved into files of their own
// (d<x>_<y>.ipl / .ide / .col, lod_d<x>_<y>.txd), registered in gta.dat. The plan is built first (the map
// shows the cells), the apply step writes with backups and the undo journal.
//
// What the engine allows (SA): text IPLs are read whole at start-up, so the cut by itself changes nothing in
// memory — it is the ground for the next step, turning each cell into a binary streamed IPL. A text IPL that
// binary stream IPLs point at for their LODs (textParent) is left alone: the lod field of a stream record is an
// ordinal into that text file, and moving any line would shift it. The COL split does stream today: CColStore
// bounds a .col by the placements of its models, so a cell's .col only loads near the cell. IDE lines follow
// the models that live in one cell only.
#include "gtacheck.h"
#include <map>
#include <set>
#include <algorithm>

namespace gc {

static std::string cellName(int gx, int gy) { return fmt("d%d_%d", gx, gy); }

void DistrictPlanBuild(const GameData &gd, const DistrictOptions &o, DistrictPlan &p)
{
	p = DistrictPlan();
	p.div = o.div < 2 ? 2 : o.div > 200 ? 200 : o.div;
	p.half = gd.isSA() ? 3000.0f : 2000.0f;
	p.cell = 2.0f * p.half / p.div;
	p.cellOfInst.assign(gd.insts.size(), -1);
	// III/VC: no lod ordinal (LOD pairs by name), so lines move freely; COL split (III loads COLFILEs whole) and the LOD TXD
	// step are SA-only — the apply step says so
	if(!gd.isSA()) p.note = T("III/VC: делятся IPL и IDE; COL и LOD-TXD не трогаются");
	// eligible files: text, not streamed, not the LOD parent of a stream IPL
	std::vector<char> okFile(gd.ipls.size(), 0);
	for(size_t i = 0; i < gd.ipls.size(); i++){
		const IplFile &f = gd.ipls[i];
		if(f.binary || f.streamed || f.numInst == 0) continue;
		bool parent = false;
		for(size_t j = 0; j < gd.ipls.size() && !parent; j++) if(gd.ipls[j].streamed && gd.ipls[j].textParent == (int)i) parent = true;
		if(parent){ p.skippedIpl.push_back(basename(f.logical)); continue; }
		okFile[i] = 1;
	}
	// LOD clusters: an instance and its LOD line share a cell (the lod field is an ordinal into the same file)
	std::vector<int> parentOf(gd.insts.size(), -1);
	for(size_t i = 0; i < gd.insts.size(); i++){
		const Inst &in = gd.insts[i];
		if(!gd.isSA() || in.fileIdx < 0 || !okFile[(size_t)in.fileIdx] || in.lod < 0) continue;
		const IplFile &f = gd.ipls[(size_t)in.fileIdx];
		if(in.lod < f.numInst) parentOf[i] = f.firstInst + in.lod;
	}
	auto rootOf = [&](size_t i){ int r = (int)i, guard = 0; while(parentOf[(size_t)r] >= 0 && parentOf[(size_t)r] != r && guard++ < 64) r = parentOf[(size_t)r]; return r; };
	auto cellOfPos = [&](const float *pos, int &gx, int &gy){
		gx = (int)floorf((pos[0] + p.half) / p.cell); gy = (int)floorf((pos[1] + p.half) / p.cell);
		if(gx < 0) gx = 0; if(gy < 0) gy = 0; if(gx >= p.div) gx = p.div - 1; if(gy >= p.div) gy = p.div - 1;
	};
	std::map<std::pair<int, int>, int> cellIdx;
	for(size_t i = 0; i < gd.insts.size(); i++){
		const Inst &in = gd.insts[i];
		if(in.fileIdx < 0 || (in.interior & 0xFF) != 0) continue;	// interiors stay where they are
		bool movable = okFile[(size_t)in.fileIdx] != 0;
		int r = movable ? rootOf(i) : (int)i;
		int gx, gy; cellOfPos(gd.insts[(size_t)r].pos, gx, gy);
		std::pair<int, int> key(gx, gy);
		auto it = cellIdx.find(key);
		int ci;
		if(it == cellIdx.end()){ DistrictCell c; c.gx = gx; c.gy = gy; c.parts.push_back(key); c.objs = 0; c.mb = 0; c.lodModels = 0; c.all = 0; c.zMin = 1e9f; c.zMax = -1e9f; ci = (int)p.cells.size(); p.cells.push_back(c); cellIdx[key] = ci; }
		else ci = it->second;
		DistrictCell &cc = p.cells[(size_t)ci];
		cc.all++;	// every placement counts for the picture; only the movable ones move
		if(in.pos[2] < cc.zMin) cc.zMin = in.pos[2]; if(in.pos[2] > cc.zMax) cc.zMax = in.pos[2];
		if(!movable) continue;
		p.cellOfInst[i] = ci;
		p.cells[(size_t)ci].insts.push_back((int)i);
		p.total++;
	}
	// small districts join a neighbour (the user: an IPL with too few objects is not worth a file): the emptiest first,
	// into the touching district with the most placements, else the nearest one
	if(o.minObjs > 1) while(p.cells.size() > 1){
		int small = -1;
		for(size_t ci = 0; ci < p.cells.size(); ci++) if(!p.cells[ci].insts.empty() && (int)p.cells[ci].insts.size() < o.minObjs && (small < 0 || p.cells[ci].insts.size() < p.cells[(size_t)small].insts.size())) small = (int)ci;
		if(small < 0) break;
		const DistrictCell &a = p.cells[(size_t)small];
		int best = -1; bool bestTouch = false; double bestD = 1e30;
		for(size_t ci = 0; ci < p.cells.size(); ci++){
			if((int)ci == small || p.cells[ci].insts.empty()) continue;
			const DistrictCell &b = p.cells[ci];
			bool touch = false; double dmin = 1e30;
			for(size_t x = 0; x < a.parts.size(); x++) for(size_t y = 0; y < b.parts.size(); y++){
				int dx = abs(a.parts[x].first - b.parts[y].first), dy = abs(a.parts[x].second - b.parts[y].second);
				if(dx <= 1 && dy <= 1) touch = true;
				double d = (double)dx * dx + (double)dy * dy; if(d < dmin) dmin = d;
			}
			if(touch && !bestTouch){ best = (int)ci; bestTouch = true; bestD = dmin; }
			else if(touch == bestTouch && (best < 0 || (touch ? b.insts.size() > p.cells[(size_t)best].insts.size() : dmin < bestD))){ best = (int)ci; bestD = dmin; }
		}
		if(best < 0) break;
		DistrictCell &b = p.cells[(size_t)best];
		b.parts.insert(b.parts.end(), a.parts.begin(), a.parts.end());
		b.insts.insert(b.insts.end(), a.insts.begin(), a.insts.end());
		b.all += a.all; if(a.zMin < b.zMin) b.zMin = a.zMin; if(a.zMax > b.zMax) b.zMax = a.zMax;
		p.cells.erase(p.cells.begin() + small);
		p.merged++;
	}
	if(o.maxObjs > 0) for(size_t ci = 0; ci < p.cells.size(); ci++) if((int)p.cells[ci].insts.size() > o.maxObjs) p.overMax++;
	// per cell: the models that live only there (their IDE line, COL entry and LOD textures can follow), and the weight
	std::map<int, std::set<int> > cellsOfModel;
	std::fill(p.cellOfInst.begin(), p.cellOfInst.end(), -1);
	for(size_t ci = 0; ci < p.cells.size(); ci++) for(size_t k = 0; k < p.cells[ci].insts.size(); k++) p.cellOfInst[(size_t)p.cells[ci].insts[k]] = (int)ci;
	for(size_t i = 0; i < gd.insts.size(); i++){ int ci = p.cellOfInst[i]; if(ci >= 0) cellsOfModel[gd.insts[i].id].insert(ci); else if(gd.insts[i].id >= 0) cellsOfModel[gd.insts[i].id].insert(-1); }	// −1: a placement that stays → the model is shared
	for(auto &kv : cellsOfModel){
		if(kv.second.size() != 1 || *kv.second.begin() < 0){ p.sharedModels++; continue; }
		const ObjDef *d = gd.findObj(kv.first); if(!d) continue;
		DistrictCell &c = p.cells[(size_t)*kv.second.begin()];
		c.models.push_back((int)(d - &gd.objs[0]));
		c.objs++;
		if(d->isLod || lower(d->name).compare(0, 3, "lod") == 0) c.lodModels++;
	}
	for(size_t ci = 0; ci < p.cells.size(); ci++){
		DistrictCell &c = p.cells[ci];
		std::set<int> ents;
		for(size_t k = 0; k < c.insts.size(); k++){	// the weight of the movable placements (unique DFF + TXD)
			const ObjDef *d = gd.findObj(gd.insts[(size_t)c.insts[k]].id); if(!d) continue;
			if(d->dffEntry >= 0 && ents.insert(d->dffEntry).second) c.mb += gd.entries[(size_t)d->dffEntry].sizeSectors * 2048.0 / 1048576.0;
			int te = d->txdSlot >= 0 ? gd.txdSlots[(size_t)d->txdSlot].entry : -1;
			if(te >= 0 && ents.insert(te).second) c.mb += gd.entries[(size_t)te].sizeSectors * 2048.0 / 1048576.0;
		}
	}
	std::sort(p.cells.begin(), p.cells.end(), [](const DistrictCell &a, const DistrictCell &b){ return a.gy != b.gy ? a.gy < b.gy : a.gx < b.gx; });
	// the sort moved the cells: rebuild the instance → cell index
	for(size_t ci = 0; ci < p.cells.size(); ci++) for(size_t k = 0; k < p.cells[ci].insts.size(); k++) p.cellOfInst[(size_t)p.cells[ci].insts[k]] = (int)ci;
	int iplNow = 0, newFiles = 0; for(size_t i = 0; i < gd.ipls.size(); i++) if(!gd.ipls[i].streamed) iplNow++;
	for(size_t ci = 0; ci < p.cells.size(); ci++) if(!p.cells[ci].insts.empty()) newFiles++;
	p.iplSlots = iplNow + newFiles;
	if(p.iplSlots > 256) p.note = fmt(T("IPL-файлов станет %d — больше 256 слотов CIplStore (без limit adjuster игра упадёт); возьми делений меньше"), p.iplSlots);
	else if(p.overMax) p.note = fmt(T("Районов с числом объектов выше максимума: %d — возьми делений больше"), p.overMax);
}

// the physical text of a data file as lines, keeping the line ending
struct TextDoc { std::string phys; std::vector<uint8_t> orig; std::vector<std::string> lines; bool crlf, trailing; };
static bool docRead(const GameData &gd, const std::string &logical, TextDoc &d)
{
	d.phys = DataFilePath(gd, logical);
	if(!readFile(d.phys, d.orig)) return false;
	std::string text(d.orig.begin(), d.orig.end()); std::string cur;
	d.crlf = text.find("\r\n") != std::string::npos; d.lines.clear();
	for(size_t i = 0; i < text.size(); i++){ if(text[i] == '\n'){ d.lines.push_back(cur); cur.clear(); } else if(text[i] != '\r') cur += text[i]; }
	d.trailing = !text.empty() && text.back() == '\n'; if(!cur.empty() || !d.trailing) d.lines.push_back(cur);
	return true;
}
static std::vector<uint8_t> docBytes(const TextDoc &d, const std::vector<std::string> &lines)
{
	std::string j; const char *nl = d.crlf ? "\r\n" : "\n";
	for(size_t i = 0; i < lines.size(); i++){ j += lines[i]; if(i + 1 < lines.size() || d.trailing) j += nl; }
	return std::vector<uint8_t>(j.begin(), j.end());
}
// the comma / space separated tokens of a line as (start, length)
static void lineTokens(const std::string &l, std::vector<std::pair<size_t, size_t> > &tok)
{
	tok.clear();
	for(size_t i = 0; i < l.size();){ while(i < l.size() && (l[i] == ' ' || l[i] == ',' || l[i] == '\t')) i++; size_t s = i; while(i < l.size() && l[i] != ' ' && l[i] != ',' && l[i] != '\t') i++; if(i > s) tok.push_back(std::make_pair(s, i - s)); }
}
static std::string setToken(const std::string &l, size_t idx, const std::string &v)
{
	std::vector<std::pair<size_t, size_t> > tok; lineTokens(l, tok);
	if(idx >= tok.size()) return l;
	return l.substr(0, tok[idx].first) + v + l.substr(tok[idx].first + tok[idx].second);
}
static bool saveNew(const GameData &gd, const std::string &logical, const std::string &path, const std::vector<uint8_t> &data, std::string &log)
{
	std::vector<uint8_t> none; std::string backup, err;
	if(!SaveTextFile(gd, logical, path, none, data, backup, err)){ log += path + ": " + err + "\n"; return false; }
	return true;
}
static bool saveEdit(const GameData &gd, const std::string &logical, const TextDoc &d, const std::vector<std::string> &lines, std::string &log)
{
	std::string backup, err;
	if(!SaveTextFile(gd, logical, d.phys, d.orig, docBytes(d, lines), backup, err)){ log += logical + ": " + err + "\n"; return false; }
	return true;
}

bool DistrictApply(GameData &gd, const DistrictOptions &o, const DistrictPlan &p, std::string &log)
{
	if(p.total == 0){ log += T("Нечего делить: ни одной подходящей копии (текстовые IPL без стрим-детей, вне интерьеров)\n"); return false; }
	std::string mapsDir = joinPath(joinPath(joinPath(gd.root, "data"), "maps"), "district");
	std::string imgDir = !OutputDir().empty() ? OutputDir() : dirExists(joinPath(gd.root, "modloader")) ? joinPath(joinPath(gd.root, "modloader"), "gta_check_fix") : joinPath(gd.root, "gta_check_out");
	EnsureDir(mapsDir); EnsureDir(imgDir);
	int newIpl = 0, newIde = 0, newCol = 0, newTxd = 0, movedInst = 0, movedObj = 0, movedCol = 0, movedTex = 0;
	std::vector<std::string> datIpl, datIde;
	// ---- the new TXD name per model (LOD textures), decided first: the IDE lines carry it wherever they end up
	std::map<int, std::string> txdOf;	// objs index → new txd stem
	if(!gd.isSA() && (o.col || o.lodTxd)) log += T("III/VC: COL и LOD-TXD не делятся (COLFILE грузится целиком, txd-родителей нет) — только IPL и IDE\n");
	if(o.lodTxd && gd.isSA()){
		std::map<std::string, std::vector<int> > groups;	// new txd stem → objs indices
		for(size_t ci = 0; ci < p.cells.size(); ci++){
			const DistrictCell &c = p.cells[ci];
			for(size_t k = 0; k < c.models.size(); k++){
				const ObjDef &d = gd.objs[(size_t)c.models[k]];
				if(!(d.isLod || lower(d.name).compare(0, 3, "lod") == 0) || !d.dffParsed || d.dffTex.empty() || d.txdSlot < 0) continue;
				groups[o.lodOne ? std::string("lod_all") : "lod_" + cellName(c.gx, c.gy)].push_back(c.models[k]);
			}
		}
		for(auto &g : groups){
			TxdFile out; bool have = false; std::set<std::string> names;
			std::map<int, TxdFile> parsed;	// source TXD entry → parsed
			for(size_t k = 0; k < g.second.size(); k++){
				const ObjDef &d = gd.objs[(size_t)g.second[k]];
				int te = gd.txdSlots[(size_t)d.txdSlot].entry; if(te < 0) continue;
				auto it = parsed.find(te);
				if(it == parsed.end()){ std::vector<uint8_t> bytes; TxdFile f; if(!gd.readEntry(te, bytes) || !TxdParse(bytes, f)){ log += fmt(T("%s: TXD не прочитан — LOD «%s» остаётся на нём\n"), gd.entries[(size_t)te].name.c_str(), d.name.c_str()); continue; } it = parsed.insert(std::make_pair(te, f)).first; }
				const TxdFile &src = it->second;
				if(!have){ out = src; out.tex.clear(); have = true; }
				bool all = true;
				for(size_t t = 0; t < d.dffTex.size(); t++){
					if(names.count(d.dffTex[t])) continue;
					bool found = false;
					for(size_t q = 0; q < src.tex.size(); q++) if(lower(src.tex[q].name) == d.dffTex[t]){ out.tex.push_back(src.tex[q]); names.insert(d.dffTex[t]); found = true; break; }
					if(!found) all = false;	// a texture from a parent TXD (txdp) or generic: the model keeps its old TXD
				}
				if(all) txdOf[g.second[k]] = g.first;
			}
			if(!have || out.tex.empty()) continue;
			std::vector<uint8_t> bytes; std::string err;
			if(!TxdWrite(out, bytes, err)){ log += g.first + ".txd: " + err + "\n"; for(size_t k = 0; k < g.second.size(); k++) txdOf.erase(g.second[k]); continue; }
			std::string path = joinPath(imgDir, g.first + ".txd");
			if(!saveNew(gd, "img_" + g.first + ".txd", path, bytes, log)){ for(size_t k = 0; k < g.second.size(); k++) txdOf.erase(g.second[k]); continue; }
			newTxd++; movedTex += (int)out.tex.size();
			double mb = bytes.size() / 1048576.0;
			log += fmt(T("%s.txd: текстур %d, LOD-моделей %d, %.1f МБ → %s\n"), g.first.c_str(), (int)out.tex.size(), (int)g.second.size(), mb, path.c_str());
			if(mb > 16.0) log += fmt(T("  ! %s.txd тяжелее 16 МБ — возьми делений больше или отдельные TXD по районам\n"), g.first.c_str());
		}
	}
	// ---- IPL: every cell's lines into d<x>_<y>.ipl, the sources without them (their other sections stay)
	if(o.ipl){
		std::map<int, TextDoc> docs;	// IplFile index → text
		std::map<int, std::set<int> > removeLines;	// file → 1-based lines to drop
		for(size_t ci = 0; ci < p.cells.size(); ci++){
			const DistrictCell &c = p.cells[ci];
			if(c.insts.empty()) continue;
			std::string name = cellName(c.gx, c.gy);
			std::vector<std::string> out;
			out.push_back(fmt("# gta_check district %s: cell %d,%d of a %dx%d grid (step %.0f)%s", name.c_str(), c.gx, c.gy, p.div, p.div, p.cell, c.parts.size() > 1 ? fmt(" + %d merged squares", (int)c.parts.size() - 1).c_str() : ""));
			out.push_back("inst");
			std::map<int, int> ordinalOf;	// GameData inst → line ordinal in the new file
			std::vector<int> order = c.insts; std::sort(order.begin(), order.end());
			for(size_t k = 0; k < order.size(); k++) ordinalOf[order[k]] = (int)k;
			std::set<std::string> sources;
			for(size_t k = 0; k < order.size(); k++){
				const Inst &in = gd.insts[(size_t)order[k]];
				const IplFile &f = gd.ipls[(size_t)in.fileIdx];
				auto dit = docs.find(in.fileIdx);
				if(dit == docs.end()){ TextDoc d; if(!docRead(gd, f.logical, d)){ log += f.logical + ": " + T("не прочитан") + "\n"; continue; } dit = docs.insert(std::make_pair(in.fileIdx, d)).first; }
				if(in.line < 1 || in.line > (int)dit->second.lines.size()) continue;
				std::string l = dit->second.lines[(size_t)in.line - 1];
				int newLod = -1;
				if(in.lod >= 0 && in.lod < f.numInst){ auto q = ordinalOf.find(f.firstInst + in.lod); if(q != ordinalOf.end()) newLod = q->second; }
				std::vector<std::pair<size_t, size_t> > tok; lineTokens(l, tok);
				if(gd.isSA() && tok.size() >= 11) l = setToken(l, 10, fmt("%d", newLod));	// III/VC: no lod field (VC token 10 is the rotation)
				out.push_back(l);
				removeLines[in.fileIdx].insert(in.line);
				sources.insert(basename(f.logical));
				movedInst++;
			}
			out.push_back("end");
			std::string srcList; for(auto &s : sources){ if(!srcList.empty()) srcList += ", "; srcList += s; }
			out.insert(out.begin() + 1, "# from: " + srcList);
			TextDoc nd; nd.crlf = true; nd.trailing = true;
			std::string logical = "data/maps/district/" + name + ".ipl";
			if(!saveNew(gd, logical, joinPath(mapsDir, name + ".ipl"), docBytes(nd, out), log)) continue;
			newIpl++; datIpl.push_back("IPL DATA\\MAPS\\district\\" + name + ".ipl");
		}
		for(auto &kv : removeLines){
			const TextDoc &d = docs[kv.first];
			std::vector<std::string> keep;
			for(size_t i = 0; i < d.lines.size(); i++) if(!kv.second.count((int)i + 1)) keep.push_back(d.lines[i]);
			if(saveEdit(gd, gd.ipls[(size_t)kv.first].logical, d, keep, log)) log += fmt(T("%s: убрано строк inst: %d\n"), basename(gd.ipls[(size_t)kv.first].logical).c_str(), (int)kv.second.size());
		}
	}
	// ---- IDE: the lines of the models that live in one cell only → d<x>_<y>.ide; the TXD token patched where the model got a LOD TXD
	{
		std::map<std::string, TextDoc> docs;	// IDE logical → text
		std::map<std::string, std::map<int, std::string> > patchLines;	// file → line → new text (stays in place)
		std::map<std::string, std::set<int> > removeLines;
		static const char *secName[OT_NUM] = { "objs", "tobj", "anim", "hier", "weap", "cars", "peds" };
		for(size_t ci = 0; ci < p.cells.size(); ci++){
			const DistrictCell &c = p.cells[ci];
			std::string name = cellName(c.gx, c.gy);
			std::map<int, std::vector<std::string> > bySection;
			for(size_t k = 0; k < c.models.size(); k++){
				int oi = c.models[k];
				const ObjDef &d = gd.objs[(size_t)oi];
				if(d.line < 1 || (d.type != OT_OBJS && d.type != OT_TOBJ && d.type != OT_ANIM)) continue;
				auto dit = docs.find(d.file);
				if(dit == docs.end()){ TextDoc td; if(!docRead(gd, d.file, td)){ log += d.file + ": " + T("не прочитан") + "\n"; continue; } dit = docs.insert(std::make_pair(d.file, td)).first; }
				if(d.line > (int)dit->second.lines.size()) continue;
				std::string l = dit->second.lines[(size_t)d.line - 1];
				auto tx = txdOf.find(oi); if(tx != txdOf.end()) l = setToken(l, 2, tx->second);
				if(o.ide){ bySection[d.type].push_back(l); removeLines[d.file].insert(d.line); movedObj++; }
				else if(tx != txdOf.end()) patchLines[d.file][d.line] = l;
			}
			if(!o.ide || bySection.empty()) continue;
			std::vector<std::string> out;
			out.push_back(fmt("# gta_check district %s", name.c_str()));
			for(auto &sec : bySection){ out.push_back(secName[sec.first]); for(size_t k = 0; k < sec.second.size(); k++) out.push_back(sec.second[k]); out.push_back("end"); }
			TextDoc nd; nd.crlf = true; nd.trailing = true;
			std::string logical = "data/maps/district/" + name + ".ide";
			if(!saveNew(gd, logical, joinPath(mapsDir, name + ".ide"), docBytes(nd, out), log)) continue;
			newIde++; datIde.push_back("IDE DATA\\MAPS\\district\\" + name + ".ide");
		}
		// the models that stay (shared / LOD TXD only): the TXD token in place
		if(!o.ide) for(auto &tx : txdOf){
			const ObjDef &d = gd.objs[(size_t)tx.first];
			if(d.line < 1) continue;
			auto dit = docs.find(d.file);
			if(dit == docs.end()){ TextDoc td; if(!docRead(gd, d.file, td)) continue; dit = docs.insert(std::make_pair(d.file, td)).first; }
			if(d.line > (int)dit->second.lines.size()) continue;
			patchLines[d.file][d.line] = setToken(dit->second.lines[(size_t)d.line - 1], 2, tx.second);
		}
		std::set<std::string> files; for(auto &kv : removeLines) files.insert(kv.first); for(auto &kv : patchLines) files.insert(kv.first);
		for(auto &f : files){
			const TextDoc &d = docs[f];
			std::vector<std::string> keep;
			const std::set<int> *rm = removeLines.count(f) ? &removeLines[f] : nullptr;
			const std::map<int, std::string> *pt = patchLines.count(f) ? &patchLines[f] : nullptr;
			for(size_t i = 0; i < d.lines.size(); i++){
				if(rm && rm->count((int)i + 1)) continue;
				if(pt){ auto q = pt->find((int)i + 1); if(q != pt->end()){ keep.push_back(q->second); continue; } }
				keep.push_back(d.lines[i]);
			}
			if(saveEdit(gd, f, d, keep, log)) log += fmt(T("%s: строк убрано %d, с новым TXD %d\n"), basename(f).c_str(), rm ? (int)rm->size() : 0, pt ? (int)pt->size() : 0);
		}
	}
	// ---- COL: the entries of the cell's exclusive models → d<x>_<y>.col; the source .col files without them
	if(o.col && gd.isSA()){
		std::map<int, std::vector<uint8_t> > srcBytes; std::map<int, std::vector<ColListEntry> > srcList; std::map<int, std::set<int> > srcDrop;
		for(size_t ci = 0; ci < p.cells.size(); ci++){
			const DistrictCell &c = p.cells[ci];
			std::vector<uint8_t> out;
			for(size_t k = 0; k < c.models.size(); k++){
				const ObjDef &d = gd.objs[(size_t)c.models[k]];
				if(d.colEntry < 0 || d.colEntry >= (int)gd.colEntries.size()) continue;
				const ColEntry &ce = gd.colEntries[(size_t)d.colEntry];
				if(ce.entryIdx < 0) continue;
				auto bit = srcBytes.find(ce.entryIdx);
				if(bit == srcBytes.end()){ std::vector<uint8_t> b; if(!gd.readEntry(ce.entryIdx, b)){ log += gd.entries[(size_t)ce.entryIdx].name + ": " + T("не прочитан") + "\n"; continue; } bit = srcBytes.insert(std::make_pair(ce.entryIdx, b)).first; ListColData(bit->second, srcList[ce.entryIdx]); }
				const std::vector<ColListEntry> &lst = srcList[ce.entryIdx];
				if(ce.index < 0 || ce.index >= (int)lst.size()) continue;
				const ColListEntry &le = lst[(size_t)ce.index];
				if((size_t)le.offset + le.size > bit->second.size()) continue;
				out.insert(out.end(), bit->second.begin() + le.offset, bit->second.begin() + le.offset + le.size);
				srcDrop[ce.entryIdx].insert(ce.index);
				movedCol++;
			}
			if(out.empty()) continue;
			std::string name = cellName(c.gx, c.gy);
			if(saveNew(gd, "img_" + name + ".col", joinPath(imgDir, name + ".col"), out, log)) newCol++;
		}
		for(auto &kv : srcDrop){
			const std::vector<uint8_t> &b = srcBytes[kv.first]; const std::vector<ColListEntry> &lst = srcList[kv.first];
			std::vector<uint8_t> keep;
			for(size_t i = 0; i < lst.size(); i++){ if(kv.second.count((int)i)) continue; if((size_t)lst[i].offset + lst[i].size <= b.size()) keep.insert(keep.end(), b.begin() + lst[i].offset, b.begin() + lst[i].offset + lst[i].size); }
			std::string backup, err;
			const Entry &en = gd.entries[(size_t)kv.first];
			std::string rel = normSlashes(en.loose), rootN = normSlashes(gd.root);	// a loose file: its path under the game folder is the journal name
			if(rel.size() > rootN.size() && lower(rel.compare(0, rootN.size(), rootN) == 0 ? rel.substr(0, rootN.size()) : "") == lower(rootN)) rel = rel.substr(rootN.size() + 1);
			bool ok = en.img >= 0 && OutputDir().empty() ? WriteEntryFixed(gd, kv.first, b, keep, backup, err) : SaveTextFile(gd, en.img >= 0 ? "img_" + en.name : rel, en.img >= 0 ? joinPath(imgDir, en.name) : en.loose, b, keep, backup, err);
			if(ok) log += fmt(T("%s: записей COL убрано %d (осталось %d)\n"), en.name.c_str(), (int)kv.second.size(), (int)(lst.size() - kv.second.size())); else log += en.name + ": " + err + "\n";
		}
	}
	// ---- gta.dat: the new IDEs after the last IDE line, the new IPLs after the last IPL line
	if(!datIpl.empty() || !datIde.empty()){
		std::string datLogical = gd.isSA() ? "data/gta.dat" : gd.isVC() ? "data/gta_vc.dat" : "data/gta3.dat";
		TextDoc d;
		if(!docRead(gd, datLogical, d)) log += T("data/gta.dat не прочитан — добавь строки IDE/IPL сам\n");
		else{
			int lastIde = -1, lastIpl = -1;
			for(size_t i = 0; i < d.lines.size(); i++){ std::string u = lower(trim(d.lines[i])); if(u.compare(0, 4, "ide ") == 0) lastIde = (int)i; if(u.compare(0, 4, "ipl ") == 0) lastIpl = (int)i; }
			std::vector<std::string> out;
			for(size_t i = 0; i < d.lines.size(); i++){
				out.push_back(d.lines[i]);
				if((int)i == lastIde) for(size_t k = 0; k < datIde.size(); k++) out.push_back(datIde[k]);
				if((int)i == lastIpl) for(size_t k = 0; k < datIpl.size(); k++) out.push_back(datIpl[k]);
			}
			if(lastIde < 0) for(size_t k = 0; k < datIde.size(); k++) out.insert(out.begin(), datIde[k]);
			if(lastIpl < 0) for(size_t k = 0; k < datIpl.size(); k++) out.push_back(datIpl[k]);
			if(saveEdit(gd, datLogical, d, out, log)) log += fmt(T("gta.dat: добавлено строк IDE %d, IPL %d\n"), (int)datIde.size(), (int)datIpl.size());
		}
	}
	log += fmt(T("Итог: районов %d · IPL новых %d (копий перенесено %d) · IDE новых %d (моделей %d) · COL новых %d (записей %d) · TXD новых %d (текстур %d)\n"), (int)p.cells.size(), newIpl, movedInst, newIde, movedObj, newCol, movedCol, newTxd, movedTex);
	if(newCol || newTxd) log += dirExists(joinPath(gd.root, "modloader")) || !OutputDir().empty() ? T("Новые .col / .txd лежат в папке modloader (или вывода) — игра берёт их как записи IMG.\n") : T("Новые .col / .txd лежат в gta_check_out — добавь их в gta3.img (или поставь modloader).\n");
	if(!p.skippedIpl.empty()){ log += T("Пропущены IPL, на которые ссылаются бинарные стрим-IPL (их lod-индексы нельзя сдвигать): "); for(size_t i = 0; i < p.skippedIpl.size(); i++){ if(i) log += ", "; log += p.skippedIpl[i]; } log += "\n"; }
	return newIpl + newIde + newCol + newTxd > 0;
}

}
