// gtacheck — cutscenes: anim/cuts.img (.cut / .ifp / .dat) and models/cutscene.img (CUT-nn).
// Source: E:\RE\addon_check\clothes_cutscene_rules.md §7–8 + GTACHECK_RULES.md §7 (gta_sa.exe 1.0 US).
#include "gtacheck.h"

#include <stdlib.h>
#include <ctype.h>
#include <algorithm>

namespace gc {

bool ReadCutsDir(const std::string &phys, std::vector<CutEntry> &out, bool &ver2, int &count)
{
	FILE *f = OpenReadUtf8(phys);
	if(!f) return false;
	uint8_t hdr[8];
	if(fread(hdr, 1, 8, f) != 8){ fclose(f); return false; }
	ver2 = memcmp(hdr, "VER2", 4) == 0;
	count = (int)(hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24));
	if(count < 0 || count > 100000){ fclose(f); return false; }
	std::vector<uint8_t> dir((size_t)count * 32);
	size_t got = dir.empty() ? 0 : fread(dir.data(), 1, dir.size(), f);
	fclose(f);
	Buf b(dir.data(), got);
	for(int i = 0; i < count; i++){
		CutEntry e;
		e.offset = b.u32(); e.size = b.u16(); b.u16();
		const uint8_t *np = b.ptr(24);
		if(!np || !b.ok) break;
		e.nameNoNul = memchr(np, 0, 24) == nullptr;
		e.name.assign((const char *)np, e.nameNoNul ? 24 : strlen((const char *)np));
		e.index = i;
		std::string l = lower(e.name);
		size_t dot = l.rfind('.');
		e.base = dot == std::string::npos ? l : l.substr(0, dot);
		e.ext = dot == std::string::npos ? "" : l.substr(dot + 1);
		out.push_back(e);
	}
	return true;
}

bool ReadCutsEntry(const std::string &phys, const CutEntry &e, std::vector<uint8_t> &out)
{
	FILE *f = OpenReadUtf8(phys);
	if(!f) return false;
	out.assign((size_t)e.size * 2048, 0);
	bool ok = _fseeki64(f, (long long)e.offset * 2048, SEEK_SET) == 0 && (out.empty() || fread(out.data(), 1, out.size(), f) == out.size());
	fclose(f);
	return ok;
}

// .cut line reader (0x5B0830): stop at NUL, split on \r / \n, no trimming
static void cutLines(const std::vector<uint8_t> &d, std::vector<std::string> &lines, bool &hasNul, size_t &longest)
{
	lines.clear(); longest = 0;
	size_t z = 0; while(z < d.size() && d[z] != 0) z++;
	hasNul = z < d.size();
	std::string cur;
	for(size_t i = 0; i < z; i++){
		char c = (char)d[i];
		if(c == '\r' || c == '\n'){ if(!cur.empty()){ lines.push_back(cur); if(cur.size() > longest) longest = cur.size(); cur.clear(); } continue; }
		cur += c;
	}
	if(!cur.empty()){ lines.push_back(cur); if(cur.size() > longest) longest = cur.size(); }
}

static void strtokSplit(const std::string &s, const char *delims, std::vector<std::string> &out)
{
	out.clear(); std::string cur;
	for(size_t i = 0; i < s.size(); i++){
		if(strchr(delims, s[i])){ if(!cur.empty()){ out.push_back(cur); cur.clear(); } }
		else cur += s[i];
	}
	if(!cur.empty()) out.push_back(cur);
}

static bool parseIntPrefix(const char *&p, long &v)
{
	while(*p == ' ' || *p == '\t') p++;
	char *e = nullptr; v = strtol(p, &e, 10);
	if(e == p) return false;
	p = e; return true;
}
static bool parseFloatPrefix(const char *&p, float &v)
{
	while(*p == ' ' || *p == '\t') p++;
	char *e = nullptr; v = (float)strtod(p, &e);
	if(e == p) return false;
	p = e; return true;
}

void CheckCutscenes(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	ctx.prog->set("Катсцены: cuts.img / cutscene.img");
	const std::string logical = "anim/cuts.img";
	std::string phys = DataFilePath(gd, logical);
	std::vector<CutEntry> ents;
	bool ver2 = false; int count = 0;
	if(!ReadCutsDir(phys, ents, ver2, count)){
		ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-01", logical, -1, "", "anim/cuts.img не найден или не читается — ReadDirFile не проверяет fopen: каталог катсцен пуст, каждая катсцена стартует без объектов", "");
	}else{
		ctx.rep->countFile(CAT_CUTSCENE);
		if(!ver2) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-01", logical, -1, "", "cuts.img не VER2 — ReadDirFile читает magic без проверки, каталог из мусора", "");
		if(count > 512) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-01", logical, -1, "", fmt("%d записей > 512 — CDirectory(0x200): лишние выброшены, катсцены с этими файлами стартуют без объектов/анимаций", count), "Ваниль: 444 записи (148 × .cut/.ifp/.dat).");
		std::map<std::string, std::map<std::string, const CutEntry*>> byBase;
		for(size_t i = 0; i < ents.size(); i++){
			const CutEntry &e = ents[i];
			std::string where = "cuts.img/" + e.name;
			if(e.nameNoNul) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-01", where, e.index, e.name, "имя записи занимает все 24 байта без NUL — FindItem (stricmp) никогда не совпадёт", "");
			if(e.size == 0) ctx.add(e.ext == "cut" ? SEV_ERROR : SEV_INFO, CAT_CUTSCENE, "CUT-01", where, e.index, e.name, e.ext == "cut" ? "streaming size 0 у .cut — new(0) и разбор мусора кучи до первого NUL" : "streaming size 0 — файл пуст", "");
			if(e.ext == "cut" || e.ext == "ifp" || e.ext == "dat") byBase[e.base][e.ext] = &e;
		}
		// count of IDE-less DFFs across streamed IMGs (CUT-07 / ms_pExtraObjectsDir 550)
		int extraDffs = 0;
		std::set<std::string> extraNames;
		for(size_t i = 0; i < gd.entries.size(); i++){
			const Entry &e = gd.entries[i];
			if(e.kind != EK_DFF || !gd.isWinner((int)i)) continue;
			if(e.img >= 0 && ieq(basename(gd.archives[(size_t)e.img].logical), "player.img")) continue;
			if(gd.findObjByName(e.base) == nullptr){ extraDffs++; extraNames.insert(e.base); }
		}
		if(extraDffs > 550) ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_CUTSCENE, "CUT-07", "IMG", -1, "", fmt("%d DFF без IDE-строки во всех стрим-IMG > 550 — ms_pExtraObjectsDir (CDirectory 0x226) переполнен: лишние special-модели выброшены → RequestSpecialModel с мусорным offset/size", extraDffs), "Ваниль: 392 (317 cutscene.img + 75 gta3.img).");
		int done = 0;
		for(auto it = byBase.begin(); it != byBase.end(); ++it, ++done){
			if(ctx.cancelled()) return;
			const std::string &base = it->first;
			ctx.prog->step(base.c_str(), done, (int)byBase.size());
			const CutEntry *cut = it->second.count("cut") ? it->second["cut"] : nullptr;
			const CutEntry *ifp = it->second.count("ifp") ? it->second["ifp"] : nullptr;
			const CutEntry *dat = it->second.count("dat") ? it->second["dat"] : nullptr;
			std::string whereBase = "cuts.img/" + base;
			if(base.size() > 11) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-02", whereBase, -1, base, fmt("имя катсцены «%s» длиной %d > 11 — LoadCutsceneData_overlay 0x5B13F0 копирует его в ms_cutsceneName[8] без границы: затирается ms_pCutsceneObjects[0] (0xBC3F18) → краш", base.c_str(), (int)base.size()), "Имена катсцен ≤ 7 символов (ваниль).");
			else if(base.size() >= 8) ctx.add(SEV_INFO, CAT_CUTSCENE, "CUT-02", whereBase, -1, base, fmt("имя катсцены «%s» длиной %d ≥ 8 — вылезает за ms_cutsceneName[8] в неиспользуемые байты 0xBC3F10..17", base.c_str(), (int)base.size()), "");
			if(cut && !ifp) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-03", whereBase, -1, base, fmt("%s.cut без %s.ifp — объекты не анимируются; остаются ассоциации предыдущей катсцены (возможный use-after-free)", base.c_str(), base.c_str()), "");
			if(cut && !dat) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-03", whereBase, -1, base, fmt("%s.cut без %s.dat — HasCutsceneFinished вернёт true сразу: катсцена мгновенно заканчивается", base.c_str(), base.c_str()), "");
			if(!cut && (ifp || dat)) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-03", whereBase, -1, base, fmt("%s.%s без %s.cut — катсцена без объектов", base.c_str(), ifp ? "ifp" : "dat", base.c_str()), "");
			// ---------------------------------------------------------------- .cut
			std::vector<std::string> cutAnims;	// anim tokens (lower)
			int slots = 0;
			if(cut && cut->size){
				std::vector<uint8_t> d;
				if(ReadCutsEntry(phys, *cut, d)){
					std::string where = "cuts.img/" + cut->name;
					std::vector<std::string> lines; bool hasNul = false; size_t longest = 0;
					cutLines(d, lines, hasNul, longest);
					if(!hasNul) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-04", where, cut->index, cut->name, "в записи нет NUL — читатель 0x5B0830 идёт за конец буфера new(size<<11) до первого нуля в куче", "Дополни файл нулями до границы сектора (ваниль: пробелы, затем нули).");
					if(longest >= 1024) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-04", where, cut->index, cut->name, fmt("строка длиной %u ≥ 1024 — 1024-байтовый буфер стека без границы → краш", (unsigned)longest), "");
					static const char *SECT[] = { "info", "model", "text", "uncompress", "attach", "remove", "peffect", "extracol", nullptr };
					int sect = 0; bool ignoredBlock = false;
					bool haveOffset = false; int texts = 0, uncompress = 0, attaches = 0, removes = 0, peffects = 0, extracols = 0;
					int specials = 0; std::set<std::string> specialNames;
					for(size_t li = 0; li < lines.size(); li++){
						const std::string &L = lines[li];
						if(sect == 0 && !ignoredBlock){
							if(L == "end") continue;
							int s = 0; for(int k = 0; SECT[k]; k++) if(L == SECT[k]) s = k + 1;
							if(s){ sect = s; continue; }
							std::string tl = lower(trim(L)); size_t cm = tl.find(','); if(cm != std::string::npos) tl = trim(tl.substr(0, cm));
							bool nearHeader = false; for(int k = 0; SECT[k]; k++) if(tl == SECT[k]) nearHeader = true;
							if(nearHeader) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-04", where, (int)li + 1, cut->name, fmt("заголовок «%s» с пробелами/запятой — repe cmpsb сравнивает строку целиком: блок молча пропущен", L.c_str()), "");
							else if(tl != "motion") ctx.add(SEV_INFO, CAT_CUTSCENE, "CUT-04", where, (int)li + 1, cut->name, fmt("неизвестная секция «%s» — пропущена до «end»", L.c_str()), "");
							ignoredBlock = true;
							continue;
						}
						if(ignoredBlock){ if(L == "end") ignoredBlock = false; continue; }
						if(L == "end"){ sect = 0; continue; }
						std::vector<std::string> t;
						switch(sect){
						case 1: {	// info
							if(L.compare(0, 6, "offset") == 0){
								const char *p = L.c_str() + 6; float x, y, z;
								if(parseFloatPrefix(p, x) && parseFloatPrefix(p, y) && parseFloatPrefix(p, z)) haveOffset = true;
								else ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-05", where, (int)li + 1, cut->name, fmt("«%s»: sscanf %%f %%f %%f не разобрал все три числа — LoadScene по мусору со стека", L.c_str()), "");
							}
							break; }
						case 2: {	// model
							strtokSplit(L, " ,", t);
							if(t.size() < 2){ ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-06", where, (int)li + 1, cut->name, fmt("строка model «%s» без имени модели — strcpy из strtok(NULL) 0x5B0B40 → краш", L.c_str()), ""); break; }
							std::string model = lower(t[1]);
							if(model.size() >= 64) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-06", where, (int)li + 1, cut->name, fmt("имя модели «%s» длиной %d ≥ 64 — переполнение 64-байтного локала", t[1].c_str(), (int)model.size()), "");
							else if(model.size() >= 32) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-06", where, (int)li + 1, cut->name, fmt("имя модели «%s» длиной %d ≥ 32 — strcpy в ms_cLoadObjectName[32] уезжает в следующий слот", t[1].c_str(), (int)model.size()), "");
							if(t.size() < 3){ ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-06", where, (int)li + 1, cut->name, fmt("строка model «%s» без токена анимации — молча выброшена, объект не создаётся", L.c_str()), ""); break; }
							for(size_t k = 2; k < t.size(); k++){
								slots++;
								if(slots == 51) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-06", where, (int)li + 1, cut->name, "51-й слот объекта — массивы [50] (0xBC38C8/0xBC3288/0xBC31C0): затираются ms_cutsceneTimer и ms_cutsceneName → краш", "");
								if(t[k].size() >= 32) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-06", where, (int)li + 1, cut->name, fmt("имя анимации «%s» длиной %d ≥ 32 — уезжает в следующий слот", t[k].c_str(), (int)t[k].size()), "");
								cutAnims.push_back(lower(t[k]));
							}
							// CUT-07 / CUT-08: model resolution
							if(model != "csplay" && gd.findObjByName(model) == nullptr){
								if(!specialNames.count(model)){
									specialNames.insert(model); specials++;
									if(gd.findEntry(model + ".dff") < 0)
										ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-07", where, (int)li + 1, model, fmt("модель «%s» не в IDE и нет «%s.dff» ни в одном стрим-IMG — RequestSpecialModel 0x409F76 игнорирует промах FindItem: RequestFile с мусорным offset/size → чтение за EOF (зависание стриминга) или CreateCutsceneObject(NULL) → краш", model.c_str(), model.c_str()), "");
									else if(gd.findEntry(model + ".txd") < 0)
										ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-08", where, (int)li + 1, model, fmt("special-модель «%s» без одноимённого «%s.txd» — FindTxdSlot → «generic»: текстуры модели не найдутся", model.c_str(), model.c_str()), "");
									if(specials == 21) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-07", where, (int)li + 1, cut->name, "21-я special-модель в катсцене — ids 300..319 исчерпаны, цикл IsModelLoaded без границы грузит её поверх IDE-модели 320+ → порча хранилища моделей", "");
								}
							}
							break; }
						case 3: {	// text
							texts++;
							if(texts == 65) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-09", where, (int)li + 1, cut->name, "65-я строка text — ms_cTextOutput[64] полон, дальше затирается ms_iModelIndex", "");
							const char *p = L.c_str(); long a, b2;
							bool ok = parseIntPrefix(p, a) && *p == ',' && (p++, parseIntPrefix(p, b2)) && *p == ',';
							std::string label;
							if(ok){ p++; while(*p == ' ' || *p == '\t') p++; while(*p && *p != ' ' && *p != '\t') label += *p++; ok = !label.empty(); }
							if(!ok) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-09", where, (int)li + 1, cut->name, fmt("«%s» не по форме start,duration,LABEL (sscanf %%d,%%d,%%s) — время/метка из мусора стека", L.c_str()), "");
							else if(label.size() >= 24) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-09", where, (int)li + 1, cut->name, fmt("метка «%s» длиной %d ≥ 24 — 20-байтный локал → курсор строки esp+0x34 → краш", label.c_str(), (int)label.size()), "");
							else if(label.size() >= 20) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-09", where, (int)li + 1, cut->name, fmt("метка «%s» длиной %d — переполняет 20-байтный локал (портит extracol)", label.c_str(), (int)label.size()), "");
							else if(label.size() > 7) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-09", where, (int)li + 1, cut->name, fmt("GXT-метка «%s» длиннее 7 — strcpy в ms_cTextOutput[n][8] затирает следующий слот", label.c_str()), "");
							break; }
						case 4: {	// uncompress
							strtokSplit(L, " ,", t);
							if(t.empty()){ ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-10", where, (int)li + 1, cut->name, "строка uncompress только из пробелов — strtok → NULL → чтение байта по адресу 0 → краш", ""); break; }
							uncompress++;
							if(uncompress == 8) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-10", where, (int)li + 1, cut->name, "8-е имя uncompress — ms_aUncompressedCutsceneAnims[8][32]: терминатор слота 8 обнуляет младший байт ms_iTextDuration[0]", "");
							if(t[0].size() >= 32) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-10", where, (int)li + 1, cut->name, fmt("имя «%s» длиной %d ≥ 32 — копия без границы в [8][32]", t[0].c_str(), (int)t[0].size()), "");
							break; }
						case 5: {	// attach
							attaches++;
							if(attaches == 51) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-11", where, (int)li + 1, cut->name, "51-я строка attach — ms_iAttachObjectToBone[50] переполнен", "");
							const char *p = L.c_str(); long a, b2, c;
							bool ok = parseIntPrefix(p, a) && *p == ',' && (p++, parseIntPrefix(p, b2)) && *p == ',' && (p++, parseIntPrefix(p, c));
							if(!ok) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-11", where, (int)li + 1, cut->name, fmt("«%s» не по форме objA,objB,boneId — индексы из мусора → ms_pCutsceneObjects[мусор] → краш", L.c_str()), "");
							else{
								if(a < 0 || b2 < 0 || a >= slots || b2 >= slots) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-11", where, (int)li + 1, cut->name, fmt("attach %ld,%ld,%ld: индекс объекта вне 0..%d (0-based, без проверки) → мусорный указатель → краш", a, b2, c, slots - 1), "");
							}
							break; }
						case 6: {	// remove
							removes++;
							if(removes == 51) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-12", where, (int)li + 1, cut->name, "51-я строка remove — ms_crToHideItems[50] переполнен", "");
							const char *p = L.c_str(); float x, y, z;
							bool ok = parseFloatPrefix(p, x) && *p == ',' && (p++, parseFloatPrefix(p, y)) && *p == ',' && (p++, parseFloatPrefix(p, z)) && *p == ',';
							std::string name;
							if(ok){ p++; while(*p == ' ' || *p == '\t') p++; while(*p && *p != ' ' && *p != '\t') name += *p++; ok = !name.empty(); }
							if(!ok) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-12", where, (int)li + 1, cut->name, fmt("«%s» не по форме x,y,z,modelname", L.c_str()), "");
							else{
								if(name.size() >= 32) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-12", where, (int)li + 1, cut->name, fmt("имя «%s» длиной %d ≥ 32", name.c_str(), (int)name.size()), "");
								if(gd.findObjByName(name) == nullptr) ctx.add(SEV_INFO, CAT_CUTSCENE, "CUT-12", where, (int)li + 1, cut->name, fmt("remove: модель «%s» не в IDE — пропускается в рантайме (ваниль: bcesa5w, synd_7)", name.c_str()), "");
							}
							break; }
						case 7: {	// peffect
							peffects++;
							if(peffects == 9) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-13", where, (int)li + 1, cut->name, "9-я строка peffect — ms_pParticleEffects[8] переполнен (9-й = ms_crToHideItems)", "");
							strtokSplit(L, ",", t);
							if(t.size() < 11){ ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-13", where, (int)li + 1, cut->name, fmt("peffect: %d полей вместо 11 (name,start,end,objId,part,x,y,z,dx,dy,dz) — strncpy/atoi/atof по NULL → краш", (int)t.size()), ""); break; }
							{
								std::string fx = lower(trim(t[0]));
								if(!gd.fxSystems.empty() && !gd.fxSystems.count(fx)) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-13", where, (int)li + 1, cut->name, fmt("эффект «%s» нет в effects.fxp — CreateFxSystem → NULL, эффект пропускается", trim(t[0]).c_str()), "");
								long objId = strtol(t[3].c_str(), nullptr, 10);
								if(objId < 1 || objId > slots) ctx.add(SEV_INFO, CAT_CUTSCENE, "CUT-13", where, (int)li + 1, cut->name, fmt("peffect objId %ld вне 1..%d (1-based) — проверка 0 < id ≤ numObjs не проходит, эффект пропускается (ваниль: crashv1, ryder3a)", objId, slots), "");
							}
							break; }
						case 8: {	// extracol
							extracols++;
							if(extracols == 2) ctx.add(SEV_INFO, CAT_CUTSCENE, "CUT-14", where, (int)li + 1, cut->name, "вторая строка extracol — читается только первая", "");
							const char *p = L.c_str(); long v;
							if(!parseIntPrefix(p, v)) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-14", where, (int)li + 1, cut->name, fmt("extracol «%s» не число", L.c_str()), "");
							else if(v < 0 || v > 16) ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-14", where, (int)li + 1, cut->name, fmt("extracol %ld вне 0..16 — StartExtraColour: строка timecyc (v−1)/8+21 за пределами 23 строк → мусорные цвета", v), "");
							break; }
						}
					}
					if(!haveOffset) ctx.add(SEV_FATAL, CAT_CUTSCENE, "CUT-05", where, -1, cut->name, "нет корректной строки «offset X Y Z» в секции info — три локала не инициализированы → CStreaming::LoadScene по мусору → краш", "");
				}
			}
			// ---------------------------------------------------------------- .ifp
			if(ifp && ifp->size){
				std::vector<uint8_t> d;
				if(ReadCutsEntry(phys, *ifp, d)){
					std::string where = "cuts.img/" + ifp->name;
					int n = 0; std::vector<std::string> names;
					CheckOneIfp(ctx, where, d, base, &n, &names, true);
					// block name vs cutscene name is enforced by the IFP checker (file stem = cutscene name)
					std::set<std::string> have(names.begin(), names.end());
					std::set<std::string> want(cutAnims.begin(), cutAnims.end());
					for(auto a = want.begin(); a != want.end(); ++a)
						if(!have.count(*a)) ctx.add(SEV_WARN, CAT_CUTSCENE, "CUT-15", where, -1, *a, fmt("анимация «%s» из %s.cut отсутствует в %s.ifp — объект остаётся статичным", a->c_str(), base.c_str(), base.c_str()), "");
					int unref = 0; for(auto a = have.begin(); a != have.end(); ++a) if(!want.count(*a)) unref++;
					if(unref) ctx.add(SEV_INFO, CAT_CUTSCENE, "CUT-15", where, -1, base, fmt("%d анимаций IFP не упомянуты в %s.cut (ваниль: 15 таких IFP)", unref, base.c_str()), "");
					// uncompress names must be in the IFP — checked above only by count; cross-check here
				}
			}
			// ---------------------------------------------------------------- .dat
			if(dat && dat->size){
				std::vector<uint8_t> d;
				if(ReadCutsEntry(phys, *dat, d)){
					std::string where = "cuts.img/" + dat->name;
					size_t z = 0; while(z < d.size() && d[z] != 0) z++;
					std::string txt((const char *)d.data(), z);
					std::vector<std::string> lines; { std::string cur; for(size_t i = 0; i < txt.size(); i++){ if(txt[i] == '\n'){ lines.push_back(cur); cur.clear(); } else if(txt[i] != '\r') cur += txt[i]; } if(!cur.empty()) lines.push_back(cur); }
					int block = -1; long remaining = 0; bool expectCount = true; bool bad = false;
					for(size_t li = 0; li < lines.size() && !bad; li++){
						std::string L = lines[li];
						// LoadLine: control chars/commas → spaces, leading blanks trimmed
						for(size_t k = 0; k < L.size(); k++) if((unsigned char)L[k] < 0x20 || L[k] == ',') L[k] = ' ';
						L = trim(L);
						if(L.empty() || L[0] == '#') continue;
						if(remaining == 0){
							if(expectCount){
								block++;
								if(block > 3){ ctx.add(SEV_INFO, CAT_CUTSCENE, "CUT-16", where, (int)li + 1, dat->name, "данные после 4-го блока — не читаются", ""); break; }
								const char *p = L.c_str(); long n;
								if(!parseIntPrefix(p, n)){ ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-16", where, (int)li + 1, dat->name, fmt("блок %d: count «%s» не число — n = 0, пустой сплайн", block, L.c_str()), ""); n = 0; }
								remaining = n; expectCount = false;
							}else if(L[0] == ';') expectCount = true;
						}else{
							remaining--;
							std::vector<std::string> t; strtokSplit(L, " \t", t);
							int need = block < 2 ? 4 : 10;
							if((int)t.size() != need){
								ctx.add((int)t.size() > need ? SEV_FATAL : SEV_ERROR, CAT_CUTSCENE, "CUT-16", where, (int)li + 1, dat->name, fmt("блок %d: %d токенов вместо %d — %s", block, (int)t.size(), need, (int)t.size() > need ? "atof-цикл без границы пишет за malloc(n*размер+4) → переполнение кучи" : "следующие записи сдвигаются"), "");
								if((int)t.size() > need) bad = true;
							}
						}
					}
					if(!bad && (block < 3 || remaining != 0 || !expectCount))
						ctx.add(SEV_ERROR, CAT_CUTSCENE, "CUT-16", where, -1, dat->name, fmt("не ровно 4 блока «N / N строк / ;» (прочитано блоков: %d%s) — LoadPathSplines читает дальше в следующую запись IMG как текст", block + 1, remaining ? fmt(", в последнем не хватает %ld строк", remaining).c_str() : ""), "");
				}
			}
		}
	}
	// ---------------------------------------------------------------- cutscene.img DFFs (CUT-17)
	{
		int imgIdx = -1;
		for(size_t i = 0; i < gd.archives.size(); i++) if(ieq(basename(gd.archives[i].logical), "cutscene.img")) imgIdx = (int)i;
		if(imgIdx >= 0){
			int total = 0; for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].img == imgIdx && gd.entries[i].kind == EK_DFF) total++;
			int done = 0;
			for(size_t i = 0; i < gd.entries.size(); i++){
				if(ctx.cancelled()) return;
				const Entry &e = gd.entries[i];
				if(e.img != imgIdx || e.kind != EK_DFF || !gd.isWinner((int)i)) continue;
				if(ctx.opt.skipVanillaImgs && gd.archives[(size_t)imgIdx].vanilla) break;
				done++;
				ctx.prog->step(e.name.c_str(), done, total);
				if(gd.findObjByName(e.base) != nullptr) continue;	// IDE model — the DFF pass already covered it
				std::vector<uint8_t> data;
				if(!gd.readEntry((int)i, data)) continue;
				ObjDef pseudo; pseudo.name = e.base; pseudo.type = OT_HIER; pseudo.id = -1; pseudo.dffEntry = (int)i;
				CheckOneDff(ctx, "cutscene.img/" + e.name, data, &pseudo, -1);
			}
		}
	}
}

} // namespace gc
