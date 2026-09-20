// gtacheck — text data files besides IDE/IPL: water.dat, timecyc.dat, plants.dat,
// animgrp.dat, object.dat, effects.fxp, nodesNN.dat (DAT-41..48 in textdata_path.md).
#include "gtacheck.h"

#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <algorithm>

namespace gc {

static std::string dataPath(GameData &gd, const std::string &logical)
{
	if(gd.modloaderActive){
		auto it = gd.redirectByRel.find(lower(normSlashes(logical)));
		if(it == gd.redirectByRel.end()) it = gd.redirectByRel.find(lower(basename(logical)));
		if(it != gd.redirectByRel.end()) return gd.modFiles[(size_t)it->second].phys;
	}
	return resolvePath(gd.root, logical);
}

// LoadLine semantics: control chars and ',' → ' ', trimmed
static std::string engineLine(const std::string &raw, bool *tooLong)
{
	if(tooLong) *tooLong = raw.size() > 511;
	std::string s = raw;
	for(size_t k = 0; k < s.size(); k++){ unsigned char c = (unsigned char)s[k]; if(c < 0x20 || c == ',') s[k] = ' '; }
	return trim(s);
}

static void split(const std::string &s, std::vector<std::string> &t)
{
	t.clear();
	size_t i = 0, n = s.size();
	while(i < n){
		while(i < n && s[i] == ' ') i++;
		size_t a = i;
		while(i < n && s[i] != ' ') i++;
		if(i > a) t.push_back(s.substr(a, i - a));
	}
}

static bool isNum(const std::string &t){ if(t.empty()) return false; char *e = nullptr; strtod(t.c_str(), &e); return e && *e == '\0'; }
static bool isInt(const std::string &t){ if(t.empty()) return false; size_t i = (t[0]=='-'||t[0]=='+') ? 1 : 0; if(i >= t.size()) return false; for(; i < t.size(); i++) if(!isdigit((unsigned char)t[i])) return false; return true; }

// ------------------------------------------------------------ water.dat ---

// III/VC: CWaterLevel::Initialise читает бинарный data/waterpro.dat (water.dat — только отладочный текст, в MASTER не читается)
static void checkWaterPro(Context &ctx)
{
	GameData &gd = *ctx.gd;
	std::string logical = "data/waterpro.dat";
	std::vector<uint8_t> d;
	if(!readFile(dataPath(gd, logical), d)){
		ctx.add(SEV_FATAL, CAT_WATER, "DAT-03", logical, -1, "", "data/waterpro.dat не найден — CWaterLevel::Initialise крутится в цикле открытия файла (зависание при старте)", "");
		return;
	}
	ctx.rep->countFile(CAT_WATER);
	const size_t need = 4 + 48 * 4 + 48 * 16 + 64 * 64 + 128 * 128;	// numLevels, Z[48], CRect[48], блоки 64×64 и 128×128 (int8)
	if(d.size() < need)
		ctx.add(SEV_ERROR, CAT_WATER, "DAT-43", logical, -1, "", fmt("файл %u байт вместо %u — недочитанные таблицы уровней/секторов остаются нулями (вода на нулевой высоте или её нет)", (unsigned)d.size(), (unsigned)need), "");
	else if(d.size() > need)
		ctx.add(SEV_INFO, CAT_WATER, "DAT-43", logical, -1, "", fmt("файл %u байт, читается %u — хвост игнорируется", (unsigned)d.size(), (unsigned)need), "");
	if(d.size() < 4) return;
	int32_t n; memcpy(&n, d.data(), 4);
	if(n < 0 || n > 48)
		ctx.add(SEV_ERROR, CAT_WATER, "DAT-43", logical, -1, "", fmt("число уровней воды %d вне 0..48 — циклы по ms_aWaterZs/ms_aWaterRects выходят за массивы", n), "");
	else{
		int bad = 0;
		for(size_t i = 4 + 48 * 4 + 48 * 16; i < d.size() && i < need; i++){
			int8_t v = (int8_t)d[i];
			if(v != -128 && (v < 0 || v >= n)) bad++;
		}
		if(bad)
			ctx.add(SEV_WARN, CAT_WATER, "DAT-43", logical, -1, "", fmt("%d секторов ссылаются на уровень воды вне 0..%d (−128 = суши) — высота берётся из-за массива", bad, n - 1), "");
		if(n == 0) ctx.add(SEV_WARN, CAT_WATER, "DAT-43", logical, -1, "", "0 уровней воды — воды в мире нет", "");
	}
}

static void checkWater(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()){ checkWaterPro(ctx); return; }
	std::string logical = "data/water.dat";
	std::vector<std::string> lines;
	if(!readTextLines(dataPath(gd, logical), lines)){ ctx.add(SEV_FATAL, CAT_WATER, "DAT-03", logical, -1, "", "data/water.dat не найден — WaterLevelInitialise крутится в цикле открытия файла (зависание)", ""); return; }
	ctx.rep->countFile(CAT_WATER);
	int quads = 0, tris = 0;
	std::set<std::string> verts;
	int outOfWorld = 0, nonInt = 0, badFlow = 0, nonRect = 0, big = 0, degenerate = 0;
	for(size_t i = 0; i < lines.size(); i++){
		bool tooLong;
		std::string s = engineLine(lines[i], &tooLong);
		if(s.empty() || s[0] == ';' || s[0] == '*' || s[0] == 'p') continue;
		if(tooLong) ctx.add(SEV_ERROR, CAT_WATER, "DAT-01", logical, (int)i + 1, "", "строка длиннее 511 символов", "");
		std::vector<std::string> t;
		split(s, t);
		int n = (int)t.size();
		int nv;
		if(n == 29 || n == 28) nv = 4;
		else if(n == 22 || n == 21) nv = 3;
		else{
			ctx.add(SEV_ERROR, CAT_WATER, "DAT-43", logical, (int)i + 1, "", fmt("%d полей — не 28/29 (квад) и не 21/22 (треугольник): будет разобрано как треугольник с мусором в незаполненных полях", n), "x y z flowX flowY bigWaves smallWaves × 4 вершины + флаг.");
			continue;
		}
		for(int k = 0; k < n; k++) if(!isNum(t[(size_t)k])){ ctx.add(SEV_ERROR, CAT_WATER, "DAT-43", logical, (int)i + 1, "", fmt("поле %d «%s» не число", k + 1, t[(size_t)k].c_str()), ""); break; }
		float x[4], y[4];
		for(int v = 0; v < nv; v++){
			x[v] = (float)atof(t[(size_t)v * 7].c_str()); y[v] = (float)atof(t[(size_t)v * 7 + 1].c_str());
			float fx = (float)atof(t[(size_t)v * 7 + 3].c_str()), fy = (float)atof(t[(size_t)v * 7 + 4].c_str());
			if(fabsf(x[v]) > 3000 || fabsf(y[v]) > 3000) outOfWorld++;
			if(x[v] != floorf(x[v]) || y[v] != floorf(y[v])) nonInt++;
			if(fabsf(fx) >= 2.0f || fabsf(fy) >= 2.0f) badFlow++;
			verts.insert(fmt("%d,%d,%s", (int)x[v], (int)y[v], t[(size_t)v * 7 + 2].c_str()));
		}
		if(nv == 4){
			quads++;
			std::set<float> xs(x, x + 4), ys(y, y + 4);
			if(xs.size() != 2 || ys.size() != 2) nonRect++;
			else{
				float w = *xs.rbegin() - *xs.begin(), h = *ys.rbegin() - *ys.begin();
				if(w > 500 || h > 500) big++;
				WaterQuad q; q.x0 = *xs.begin(); q.x1 = *xs.rbegin(); q.y0 = *ys.begin(); q.y1 = *ys.rbegin();
				q.z = 0; for(int v = 0; v < 4; v++) q.z += (float)atof(t[(size_t)v * 7 + 2].c_str()) * 0.25f;
				gd.water.push_back(q);
			}
			bool allX = x[0] == x[1] && x[1] == x[2] && x[2] == x[3], allY = y[0] == y[1] && y[1] == y[2] && y[2] == y[3];
			if(allX || allY) degenerate++;
		}else tris++;
	}
	if(quads > 301) ctx.add(SEV_FATAL, CAT_LIMIT, "DAT-44", logical, -1, "", fmt("%d квадов > 301 — 302-й затирает таблицу треугольников", quads), "");
	if(tris > 6) ctx.add(SEV_FATAL, CAT_LIMIT, "DAT-44", logical, -1, "", fmt("%d треугольников > 6 — 7-й затирает собственный счётчик", tris), "");
	if(verts.size() > 1021) ctx.add(SEV_FATAL, CAT_LIMIT, "DAT-44", logical, -1, "", fmt("%d уникальных вершин > 1021 — переполнение m_aVertices", (int)verts.size()), "");
	if(outOfWorld) ctx.add(SEV_ERROR, CAT_WATER, "DAT-45", logical, -1, "", fmt("%d вершин вне ±3000 — AddWaterLevelVertex зажимает координату и СБРАСЫВАЕТ z/волны/поток в 0", outOfWorld), "");
	if(nonInt) ctx.add(SEV_WARN, CAT_WATER, "DAT-45", logical, -1, "", fmt("%d вершин с нецелыми X/Y — движок обрезает до целого (int16)", nonInt), "");
	if(badFlow) ctx.add(SEV_ERROR, CAT_WATER, "DAT-45", logical, -1, "", fmt("%d вершин с |flow| ≥ 2 — flow×64 не влезает в int8", badFlow), "");
	if(nonRect) ctx.add(SEV_ERROR, CAT_WATER, "DAT-45", logical, -1, "", fmt("%d квадов не являются осевыми прямоугольниками — вершины сортируются как углы прямоугольника, отрисуется мусор", nonRect), "");
	if(degenerate) ctx.add(SEV_WARN, CAT_WATER, "DAT-45b", logical, -1, "", fmt("%d вырожденных квадов (все X или все Y равны) — отброшены", degenerate), "");
	if(big) ctx.add(SEV_INFO, CAT_WATER, "DAT-45", logical, -1, "", fmt("%d квадов больше 500×500 (в ванили 21 такой) — рендер идёт по блокам 500 м", big), "");
}

// ---------------------------------------------------------- timecyc.dat ---

static void checkTimecyc(Context &ctx)
{
	GameData &gd = *ctx.gd;
	std::string logical = "data/timecyc.dat";
	std::vector<std::string> lines;
	if(!readTextLines(dataPath(gd, logical), lines)){ ctx.add(SEV_FATAL, CAT_TIMECYC, "DAT-03", logical, -1, "", "data/timecyc.dat не найден", ""); return; }
	ctx.rep->countFile(CAT_TIMECYC);
	int dataLines = 0;
	int need = gd.isSA() ? 184 : (gd.game == GAME_VC ? 7 * 24 : 4 * 24);	// re3/reVC CTimeCycle::Initialise: NUMWEATHERS (4 / 7) × NUMHOURS 24
	int cols = gd.isSA() ? 51 : 0;
	for(size_t i = 0; i < lines.size(); i++){
		std::string s = engineLine(lines[i], nullptr);
		if(s.empty() || s[0] == '/') continue;
		if(s[0] == '#' || !isdigit((unsigned char)s[0]) && s[0] != '-'){
			ctx.add(SEV_ERROR, CAT_TIMECYC, "DAT-42b", logical, (int)i + 1, "", fmt("строка «%s» не начинается с числа и не с «/» — попадёт в sscanf, счёт строк сдвинется, все следующие семплы лягут не в свои слоты", s.substr(0, 30).c_str()), "В timecyc.dat комментарии только «//».");
			dataLines++;
			continue;
		}
		dataLines++;
		if(dataLines > need) continue;
		std::vector<std::string> t; split(s, t);
		if(cols && (int)t.size() < cols - 3)
			ctx.add(SEV_ERROR, CAT_TIMECYC, "DAT-42c", logical, (int)i + 1, "", fmt("%d полей вместо %d — хвостовые колонки останутся от предыдущей строки", (int)t.size(), cols), "");
		for(size_t k = 0; k < t.size() && k < 21; k++){
			if(!isInt(t[k])) continue;
			int v = atoi(t[k].c_str());
			if(v < 0 || v > 255){ ctx.add(SEV_WARN, CAT_TIMECYC, "DAT-42d", logical, (int)i + 1, "", fmt("цвет %d вне 0..255 — обрежется до байта", v), ""); break; }
		}
	}
	if(dataLines < need)
		ctx.add(SEV_FATAL, CAT_TIMECYC, "DAT-42a", logical, -1, "", fmt("%d строк данных, нужно ровно %d (%s) — LoadLine вернёт NULL, sscanf(NULL) → краш при старте", dataLines, need, gd.isSA() ? "23 погоды × 8 часов" : (gd.isVC() ? "7 погод × 24 часа" : "4 погоды × 24 часа")), "");
	else if(dataLines > need)
		ctx.add(SEV_INFO, CAT_TIMECYC, "DAT-42a", logical, -1, "", fmt("%d строк данных, читаются первые %d", dataLines, need), "");
}

// ----------------------------------------------------------- plants.dat ---

static void checkPlants(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	std::string logical = "data/plants.dat";
	std::vector<std::string> lines;
	if(!readTextLines(dataPath(gd, logical), lines)){ ctx.add(SEV_ERROR, CAT_PLANTS, "DAT-46", logical, -1, "", "data/plants.dat не найден — травы не будет", ""); return; }
	ctx.rep->countFile(CAT_PLANTS);
	// surface names from surface.dat (first column)
	std::set<std::string> surfaces;
	std::vector<std::string> sl;
	if(readTextLines(dataPath(gd, "data/surfinfo.dat"), sl)){
		for(size_t i = 0; i < sl.size(); i++){
			std::string s = trim(sl[i]);
			if(s.empty() || s[0] == ';' || s[0] == '#') continue;
			std::vector<std::string> t; split(engineLine(s, nullptr), t);
			if(!t.empty()) surfaces.insert(lower(t[0]));
		}
	}
	std::set<std::string> used;
	for(size_t i = 0; i < lines.size(); i++){
		std::string s = engineLine(lines[i], nullptr);
		if(s.compare(0, 8, ";the end") == 0) break;
		if(s.empty() || s[0] == ';') continue;
		std::vector<std::string> t; split(s, t);
		if(t.size() < 18){
			ctx.add(SEV_ERROR, CAT_PLANTS, "DAT-46a", logical, (int)i + 1, "", fmt("%d полей вместо 18 — LoadPlantsDat вернёт false: ВСЯ трава отключается, остальные строки не читаются", (int)t.size()), "");
			return;
		}
		if(!surfaces.empty() && !surfaces.count(lower(t[0]))){
			ctx.add(SEV_ERROR, CAT_PLANTS, "DAT-46a", logical, (int)i + 1, t[0], fmt("поверхность «%s» нет в surfinfo.dat — «Unknown surface name … See Andrzej», вся трава отключается", t[0].c_str()), "");
			return;
		}
		used.insert(lower(t[0]));
		if(used.size() > 57){ ctx.add(SEV_ERROR, CAT_PLANTS, "DAT-46a", logical, (int)i + 1, t[0], "больше 57 различных поверхностей — таблица переполнена, трава отключается", ""); return; }
		int slot = atoi(t[2].c_str()), model = atoi(t[3].c_str()), uv = atoi(t[4].c_str());
		if(slot < 0 || slot > 3 || model < 0 || model > 3 || uv < 0 || uv > 3)
			ctx.add(SEV_ERROR, CAT_PLANTS, "DAT-46b", logical, (int)i + 1, t[0], fmt("SlotID %d / ModelID %d / UVoff %d вне 0..3 — индекс в мусорные атомики при рендере", slot, model, uv), "");
	}
}

// ---------------------------------------------------------- animgrp.dat ---

static void loadAnimGrp(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	std::string logical = "data/animgrp.dat";
	std::vector<std::string> lines;
	if(!readTextLines(dataPath(gd, logical), lines)){ ctx.add(SEV_FATAL, CAT_IFP, "DAT-03", logical, -1, "", "data/animgrp.dat не найден", ""); return; }
	AnimGroup cur; bool in = false;
	for(size_t i = 0; i < lines.size(); i++){
		std::string s = engineLine(lines[i], nullptr);
		if(s.empty() || s[0] == '#') continue;
		std::vector<std::string> t; split(s, t);
		if(!in){
			if(t.size() < 4){ ctx.add(SEV_WARN, CAT_IFP, "ANIMGRP", logical, (int)i + 1, "", "заголовок группы: нужно name, filename, animtype, numanims", ""); continue; }
			cur = AnimGroup(); cur.name = t[0]; cur.block = t[1]; cur.model = t[2]; cur.line = (int)i + 1;
			in = true;
			continue;
		}
		if(t[0] == "end"){
			gd.animGroups.push_back(cur);
			in = false;
			continue;
		}
		cur.anims.push_back(t[0]);
	}
	// peds IDE anim groups must exist (DAT-12)
	std::set<std::string> groups;
	// the 118 definitions built into gta_sa.exe (ms_aAnimAssocDefinitions @0x8AA5A8, read from the binary)
	static const char *builtin[] = { "default","door","bikes","bikev","bikeh","biked","wayfarer","bmx","mtb","choppa","quad","python","pythonbad","colt45","colt_cop","colt45pro","sawnoff","sawnoffpro","silenced","shotgun","shotgunbad","buddy","buddybad","uzi","uzibad","rifle","riflebad","sniper","grenade","flame","rocket","spraycan","goggles","melee_1","melee_2","melee_3","melee_4","bbbat_1","gclub_1","knife_1","sword_1","dildo_1","flowers_1","csaw_1","kick_std","pistlwhp","medic","beach","sunbathe","playidles","riot","strip","gangs","attractors","player","fat","muscular","playerrocket","playerrocketf","playerrocketm","player2armed","player2armedf","player2armedm","playerbbbat","playerbbbatf","playerbbbatm","playercsaw","playercsawf","playercsawm","playersneak","playerjetpack","swim","drivebys","bike_dbz","cop_dbz","quad_dbz","fat_tired","handsignal","handsignall","lhand","rhand","carry","carry05","carry105","int_house","int_office","int_shop","stealth_kn","stdcaramims","lowcaramims","trkcaranims","stdbikeanims","sportbikeanims","vespabikeanims","harleybikeanims","dirtbikeanims","wayfbikeanims","bmxbikeanims","mtbbikeanims","choppabikeanims","quadbikeanims","vancaranims","rustplaneanims","coachcaranims","buscaranims","dozercaranims","kartcaranims","convcaranims","mtrkcaranims","traincarranims","stdtallcaramims","hovercaranims","tankcaranims","bfinjcaramims","learplaneanims","harrplaneanims","stdcarupright","nvadaplaneanims", nullptr };
	for(int k = 0; builtin[k]; k++) groups.insert(builtin[k]);
	for(size_t g = 0; g < gd.animGroups.size(); g++) groups.insert(lower(gd.animGroups[g].name));
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.type != OT_PEDS || o.pedAnimGroup.empty()) continue;
		if(!groups.count(lower(o.pedAnimGroup)))
			ctx.add(SEV_FATAL, CAT_XREF, "DAT-12", o.file, o.line, o.name, fmt("группа анимаций «%s» нет в animgrp.dat — m_nAnimType = ms_numAnimAssocDefinitions (за концом массива), краш при спавне педа", o.pedAnimGroup.c_str()), "", o.id);
	}
}

// ----------------------------------------------------------- object.dat ---

static void checkObjectDat(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	std::string logical = "data/object.dat";
	std::vector<std::string> lines;
	if(!readTextLines(dataPath(gd, logical), lines)) return;
	std::set<std::string> distinct;
	int unknown = 0;
	for(size_t i = 0; i < lines.size(); i++){
		std::string s = engineLine(lines[i], nullptr);
		if(s.empty() || s[0] == ';' || s[0] == '#' || s[0] == '*') continue;
		std::vector<std::string> t; split(s, t);
		if(t.size() < 2) continue;
		gd.objectDatNames.insert(lower(t[0]));
		// distinct parameter lines = everything after the name
		std::string params; for(size_t k = 1; k < t.size(); k++) params += t[k] + " ";
		distinct.insert(params);
		if(!gd.objs.empty() && gd.findObjByName(t[0]) == nullptr && unknown++ < 20)
			ctx.add(SEV_INFO, CAT_XREF, "COL-35", logical, (int)i + 1, t[0], fmt("object.dat: модель «%s» не объявлена в IDE — строка игнорируется", t[0].c_str()), "");
	}
	if(distinct.size() + 5 > 400)
		ctx.add(SEV_FATAL, CAT_LIMIT, "COL-35", logical, -1, "", fmt("%d различных наборов параметров в object.dat (+5 стандартных) > 400 — ms_aObjectInfo переполняется", (int)distinct.size()), "");
}

// ---------------------------------------------------------- effects.fxp ---

static void checkFxp(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	std::string logical = "models/effects.fxp";
	std::string phys = resolvePath(gd.root, logical);
	if(gd.modloaderActive){ auto it = gd.looseByName.find("effects.fxp"); if(it != gd.looseByName.end()) phys = gd.modFiles[(size_t)it->second].phys; auto r = gd.redirectByRel.find("effects.fxp"); if(r != gd.redirectByRel.end()) phys = gd.modFiles[(size_t)r->second].phys; }
	std::vector<std::string> lines;
	if(!readTextLines(phys, lines)){ ctx.add(SEV_ERROR, CAT_FXP, "DAT-47", logical, -1, "", "models/effects.fxp не найден", ""); return; }
	ctx.rep->countFile(CAT_FXP);
	static const char *infoKw[] = { "EMRATE","EMSIZE","EMSPEED","EMDIR","EMANGLE","EMLIFE","EMPOS","EMWEATHER","EMROTATION","NOISE","FORCE","FRICTION","ATTRACTPT","ATTRACTLINE","GROUNDCOLLIDE","WIND","JITTER","ROTSPEED","FLOAT","UNDERWATER","COLOUR","SIZE","SPRITERECT","HEATHAZE","TRAIL","FLAT","DIR","ANIMTEX","COLOURRANGE","SELFLIT","COLOURBRIGHT","SMOKE", nullptr };
	std::string curSys;
	int numPrims = 0, prims = 0, numInfos = -1, infos = 0;
	auto effTx = gd.txdTextures.find("effectspc");
	std::set<std::string> texUsed;
	for(size_t i = 0; i < lines.size(); i++){
		std::string s = trim(lines[i]);
		if(s.empty()) continue;
		std::vector<std::string> t; split(engineLine(s, nullptr), t);
		if(t.empty()) continue;
		if(t[0] == "NAME:" && t.size() > 1 && curSys.empty()){ curSys = t[1]; gd.fxSystems.insert(lower(t[1])); }
		else if(t[0] == "FX_SYSTEM_DATA:"){
			if(numInfos >= 0 && infos != numInfos) ctx.add(SEV_FATAL, CAT_FXP, "DAT-47b", logical, (int)i + 1, curSys, fmt("NUM_INFOS %d, а блоков FX_INFO_* %d", numInfos, infos), "");
			curSys.clear(); numPrims = 0; prims = 0; numInfos = -1; infos = 0;
		}
		else if(t[0] == "NUM_PRIMS:" && t.size() > 1){
			numPrims = atoi(t[1].c_str());
			if(numPrims > 8) ctx.add(SEV_FATAL, CAT_FXP, "DAT-47a", logical, (int)i + 1, curSys, fmt("NUM_PRIMS %d > 8 — стековый буфер имён текстур 8×128 переполняется", numPrims), "");
		}
		else if(t[0] == "FX_PRIM_EMITTER_DATA:") prims++;
		else if(t[0] == "NUM_INFOS:" && t.size() > 1){ if(numInfos >= 0 && infos != numInfos) ctx.add(SEV_FATAL, CAT_FXP, "DAT-47b", logical, (int)i + 1, curSys, fmt("NUM_INFOS %d, а блоков FX_INFO_* %d", numInfos, infos), ""); numInfos = atoi(t[1].c_str()); infos = 0; }
		else if(t[0].compare(0, 8, "FX_INFO_") == 0){
			infos++;
			std::string kw = t[0].substr(8);
			size_t d = kw.find("_DATA:");
			if(d != std::string::npos) kw = kw.substr(0, d);
			bool known = false;
			for(int k = 0; infoKw[k]; k++) if(kw == infoKw[k]) known = true;
			if(!known) ctx.add(SEV_FATAL, CAT_FXP, "DAT-47b", logical, (int)i + 1, curSys, fmt("неизвестное ключевое слово %s — FxInfoManager_c::Load пишет по NULL → краш", t[0].c_str()), "");
		}
		else if(t[0] == "NUM_KEYS:" && t.size() > 1){ int nk = atoi(t[1].c_str()); if(nk > 127) ctx.add(SEV_FATAL, CAT_FXP, "DAT-47c", logical, (int)i + 1, curSys, fmt("NUM_KEYS %d > 127 (int8)", nk), ""); }
		else if((t[0] == "TEXTURE:" || t[0] == "TEXTURE2:" || t[0] == "TEXTURE3:" || t[0] == "TEXTURE4:") && t.size() > 1){
			if(t[1] != "NULL" && effTx != gd.txdTextures.end() && std::find(effTx->second.names.begin(), effTx->second.names.end(), lower(t[1])) == effTx->second.names.end() && !texUsed.count(lower(t[1]))){
				texUsed.insert(lower(t[1]));
				ctx.add(SEV_ERROR, CAT_FXP, "DAT-48", logical, (int)i + 1, curSys, fmt("текстура «%s» отсутствует в effectsPC.txd — частицы без текстуры", t[1].c_str()), "");
			}
		}
	}
	(void)prims;
}

// ------------------------------------------------------------- nodes ---

static void checkNodes(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	for(size_t i = 0; i < gd.entries.size(); i++){
		Entry &e = gd.entries[i];
		if(e.kind != EK_DAT) continue;
		if(e.base.compare(0, 5, "nodes") != 0) continue;
		std::string where = (e.img >= 0 ? basename(gd.archives[e.img].logical) + "/" : "") + e.name;
		int area = atoi(e.base.c_str() + 5);
		if(area < 0 || area > 63 || e.base.size() < 6 || !isdigit((unsigned char)e.base[5])){
			ctx.add(SEV_ERROR, CAT_PATHS, "DAT-41", where, e.dirIndex, e.name, fmt("nodes-файл с номером области %d вне 0..63 — id попадает в диапазон IFP и трактуется как блок анимаций", area), "");
			continue;
		}
		std::vector<uint8_t> data;
		if(!gd.readEntry((int)i, data, nullptr)) continue;
		ctx.rep->countFile(CAT_PATHS);
		Buf b(data.data(), data.size());
		uint32_t numNodes = b.u32(), numVeh = b.u32(), numPed = b.u32(), numNavi = b.u32(), numLinks = b.u32();
		if(!b.ok){ ctx.add(SEV_FATAL, CAT_PATHS, "DAT-41", where, -1, "", "заголовок nodes короче 20 байт", ""); continue; }
		if(numVeh + numPed != numNodes) ctx.add(SEV_WARN, CAT_PATHS, "DAT-41", where, -1, "", fmt("numNodes %u ≠ veh %u + ped %u", numNodes, numVeh, numPed), "");
		uint64_t need = 20 + (uint64_t)numNodes * 28 + (uint64_t)numNavi * 14 + ((uint64_t)numLinks + 192) * 4 + (uint64_t)numLinks * 2 + ((uint64_t)numLinks + 192) * 1 + ((uint64_t)numLinks + 192) * 1;
		if(need > data.size())
			ctx.add(SEV_FATAL, CAT_PATHS, "DAT-41", where, -1, "", fmt("объявленные секции требуют %llu байт, в файле %u — массивы останутся частично неинициализированными", (unsigned long long)need, (unsigned)data.size()), "");
		else{
			// per-node area id / link range sanity
			b.seek(20);
			int badArea = 0, badLink = 0;
			for(uint32_t n = 0; n < numNodes && b.ok; n++){
				b.skip(8);	// mem addr + unk
				b.skip(6);	// pos int16 x3
				b.u16();	// link id / heuristic
				uint16_t linkId = b.u16(); uint16_t areaId = b.u16(); uint16_t nodeId = b.u16();
				b.u8(); b.u8();	// pathWidth, floodFill
				uint32_t fl = b.u32(); uint8_t numLinksNode = (uint8_t)(fl & 0xF);
				(void)nodeId;
				if(areaId != (uint16_t)area) badArea++;
				if((uint32_t)linkId + numLinksNode > numLinks) badLink++;
			}
			if(badArea) ctx.add(SEV_WARN, CAT_PATHS, "DAT-41", where, -1, "", fmt("%d узлов с areaId ≠ %d", badArea, area), "");
			if(badLink) ctx.add(SEV_ERROR, CAT_PATHS, "DAT-41", where, -1, "", fmt("%d узлов, чьи linkId + numLinks выходят за таблицу связей (%u) — OOB при поиске пути", badLink, numLinks), "");
		}
	}
}

void CheckDataFiles(Context &ctx)
{
	ctx.prog->set("Данные (water / timecyc / plants / animgrp / fxp / nodes)");
	loadAnimGrp(ctx);
	if(ctx.cancelled()) return;
	checkWater(ctx);
	checkTimecyc(ctx);
	checkPlants(ctx);
	checkObjectDat(ctx);
	checkFxp(ctx);
	checkNodes(ctx);
}

} // namespace gc
