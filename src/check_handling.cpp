// gtacheck — handling.cfg / carcols.dat / carmods.dat / cargrp.dat (HND-nn) and the
// vehicles.ide-side vehicle rules VEH-13..20.  Source: E:\RE\addon_check\vehicle_rules.md
// + GTACHECK_RULES.md §2–3 (gta_sa.exe 1.0 US decompile, adversarially verified).
#include "gtacheck.h"

#include <stdlib.h>
#include <ctype.h>
#include <algorithm>

namespace gc {

// The 210 built-in handling names (0x8D3978, 14-byte cells, index = position).  Custom names are
// impossible: FindExactWord / GetHandlingId return 210 for anything else.
static const char *HANDLING_NAMES[210] = {
	"LANDSTAL","BRAVURA","BUFFALO","LINERUN","PEREN","SENTINEL","DUMPER","FIRETRUK","TRASH","STRETCH","MANANA",
	"INFERNUS","VOODOO","PONY","MULE","CHEETAH","AMBULAN","MOONBEAM","ESPERANT","TAXI","WASHING",
	"BOBCAT","MRWHOOP","BFINJECT","PREMIER","ENFORCER","SECURICA","BANSHEE","BUS","RHINO","BARRACKS",
	"HOTKNIFE","ARTICT1","PREVION","COACH","CABBIE","STALLION","RUMPO","RCBANDIT","ROMERO","PACKER",
	"MONSTER","ADMIRAL","TRAM","AIRTRAIN","ARTICT2","TURISMO","FLATBED","YANKEE","GOLFCART","SOLAIR",
	"TOPFUN","GLENDALE","OCEANIC","PATRIOT","HERMES","SABRE","ZR350","WALTON","REGINA","COMET",
	"BURRITO","CAMPER","BAGGAGE","DOZER","RANCHER","FBIRANCH","VIRGO","GREENWOO","HOTRING","SANDKING",
	"BLISTAC","BOXVILLE","BENSON","MESA","BLOODRA","BLOODRB","SUPERGT","ELEGANT","JOURNEY","PETROL",
	"RDTRAIN","NEBULA","MAJESTIC","BUCCANEE","CEMENT","TOWTRUCK","FORTUNE","CADRONA","FBITRUCK",
	"WILLARD","FORKLIFT","TRACTOR","COMBINE","FELTZER","REMINGTN","SLAMVAN","BLADE","FREIGHT","STREAK",
	"VINCENT","BULLET","CLOVER","SADLER","RANGER","HUSTLER","INTRUDER","PRIMO","TAMPA",
	"SUNRISE","MERIT","UTILITY","YOSEMITE","WINDSOR","MTRUCK_A","MTRUCK_B","URANUS","JESTER",
	"SULTAN","STRATUM","ELEGY","RCTIGER","FLASH","TAHOMA","SAVANNA","BANDITO","FREIFLAT",
	"CSTREAK","KART","MOWER","DUNE","SWEEPER","BROADWAY","TORNADO","DFT30","HUNTLEY",
	"STAFFORD","NEWSVAN","TUG","PETROTR","EMPEROR","FLOAT","EUROS","HOTDOG","CLUB","ARTICT3",
	"RCCAM","POLICE_LA","POLICE_SF","POLICE_VG","POLRANGER","PICADOR","SWATVAN","ALPHA",
	"PHOENIX","BAGBOXA","BAGBOXB","STAIRS","BOXBURG","FARM_TR1","UTIL_TR1","ROLLER",
	// bikes 162..174
	"BIKE","MOPED","DIRTBIKE","FCR900","NRG500","HPV1000","BF400","WAYFARER","QUADBIKE","BMX","CHOPPERB","MTB","FREEWAY",
	// boats 175..186 (186 SEAPLANE is also the first flying slot)
	"PREDATOR","SPEEDER","REEFER","RIO","SQUALO","TROPIC","COASTGRD","DINGHY","MARQUIS","CUPBOAT","LAUNCH","SEAPLANE",
	// flying 187..209
	"VORTEX","RUSTLER","BEAGLE","CROPDUST","STUNT","SHAMAL","HYDRA","NEVADA","AT400","ANDROM",
	"DODO","SPARROW","SEASPAR","MAVERICK","COASTMAV","POLMAV","HUNTER","LEVIATHN","CARGOBOB",
	"RAINDANC","RCBARON","RCGOBLIN","RCRAIDER",
};

// FindExactWord 0x6F4F30: strncmp(token, cell, strlen(cell)) — prefix match on the cell, case-sensitive,
// first hit wins; 210 = not found.  `exact` tells whether the token equals the cell.
static int findHandlingPrefix(const std::string &tok, bool *exact)
{
	for(int i = 0; i < 210; i++){
		size_t n = strlen(HANDLING_NAMES[i]);
		if(tok.compare(0, n, HANDLING_NAMES[i]) == 0){
			if(exact) *exact = tok.size() == n;
			return i;
		}
	}
	if(exact) *exact = false;
	return 210;
}

// GetHandlingId 0x6F4FD0: strncmp(name, cell, 14) over the cells — exact (cells are < 14 chars + NUL).
static int findHandlingExact(const std::string &tok)
{
	if(tok.size() >= 14) return 210;
	for(int i = 0; i < 210; i++) if(tok == HANDLING_NAMES[i]) return i;
	return 210;
}

static float tf(const std::string &t) { return (float)strtod(t.c_str(), nullptr); }
// handling.cfg is read with strtok + atof/atoi: a token counts as a number when it has a numeric prefix
static bool numPrefix(const std::string &t, bool *trailing = nullptr)
{
	if(t.empty()) return false;
	char *e = nullptr; strtod(t.c_str(), &e);
	if(e == t.c_str()) return false;
	if(trailing) *trailing = *e != '\0';
	return true;
}
static int ti(const std::string &t) { return (int)strtol(t.c_str(), nullptr, 10); }

static bool isCarFamily(const std::string &type)
{
	std::string t = lower(type);
	return t == "car" || t == "mtruck" || t == "quad" || t == "heli" || t == "plane" || t == "trailer" || t == "f_heli";
}

// ------------------------------------------------------------ handling.cfg ---

static void checkHandlingCfg(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/handling.cfg";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){
		ctx.add(SEV_FATAL, CAT_HANDLING, "HND-01", logical, -1, "", "data/handling.cfg не найден — LoadHandlingData не проверяет FILE*, fgets(NULL) → краш при старте", "");
		return;
	}
	ctx.rep->countFile(CAT_HANDLING);
	bool sawEnd = false;
	int animGroupsMax = -1;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.tooLong) ctx.add(SEV_INFO, CAT_HANDLING, "HND-17", logical, l.number, "", "строка длиннее 511 символов — LoadLine разрежет её на две", "");
		if(l.raw.compare(0, 8, ";the end") == 0){ sawEnd = true; break; }
		if(!l.norm.empty() && l.norm[0] == ';') continue;
		if(l.norm.empty()){
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-02", logical, l.number, "", "пустая (или пробельная) строка до «;the end» — car-ветка делает strtok(\"\") → NULL и без проверки идёт в FindExactWord(NULL) → strncmp(NULL) → краш",
			        "В handling.cfg не должно быть пустых строк; комментарий — только «;» первым символом. «#» комментарием НЕ является.");
			continue;
		}
		char pfx = l.norm[0];
		const std::vector<std::string> &t = l.tok;
		if(pfx == '!' || pfx == '$' || pfx == '%'){
			// prefixed line: "! NAME fields…" — the prefix is its own token; "!NAME" glues them
			std::string name;
			size_t first = 1;
			if(t[0].size() > 1){ name = t[0].substr(1); first = 1; }
			else if(t.size() > 1){ name = t[1]; first = 2; }
			if(name.empty()){
				if(pfx == '!') ctx.add(SEV_FATAL, CAT_HANDLING, "HND-02", logical, l.number, "", "строка из одного «!» — цикл кончается сразу и ConvertBikeDataToGameUnits(предыдущий указатель) — NULL для первой байк-строки → краш", "");
				else ctx.add(SEV_INFO, CAT_HANDLING, "HND-02", logical, l.number, "", fmt("строка из одного «%c» — ничего не записано", pfx), "");
				continue;
			}
			bool exact = false;
			int id = findHandlingPrefix(name, &exact);
			int nfields = (int)t.size() - (int)first;
			const char *kind = pfx == '!' ? "байк" : pfx == '$' ? "летающая" : "лодка";
			int expect = pfx == '!' ? 15 : pfx == '$' ? 21 : 14;
			if(id == 210){
				ctx.add(SEV_ERROR, CAT_HANDLING, "HND-03", logical, l.number, name, fmt("%c %s: имя не из 210 встроенных — FindExactWord вернёт 210: запись уйдёт в чужой массив (%s)", pfx, name.c_str(),
				        pfx == '!' ? "внутрь m_aBoatHandling[2..3] REEFER/RIO" : pfx == '$' ? "m_aBoatHandling[0..1] PREDATOR/SPEEDER" : "boat[0] PREDATOR"),
				        "Своих имён в handling.cfg быть не может — таблица имён вшита в exe; переиспользуй одно из 210.");
			}else{
				if(!exact) ctx.add(SEV_WARN, CAT_HANDLING, "HND-03b", logical, l.number, name, fmt("имя «%s» совпало с «%s» только по префиксу (strncmp по длине ячейки) — данные лягут в %s", name.c_str(), HANDLING_NAMES[id], HANDLING_NAMES[id]), "");
				bool ok = pfx == '!' ? (id >= 162 && id <= 174) : pfx == '$' ? (id >= 186 && id <= 209) : (id >= 175 && id <= 186);
				if(!ok)
					ctx.add(SEV_ERROR, CAT_HANDLING, "HND-04", logical, l.number, name, fmt("%c %s: индекс %d не из диапазона %s (%s) — запись ложится в чужую структуру (%s)", pfx, name.c_str(), id,
					        pfx == '!' ? "162..174" : pfx == '$' ? "186..209" : "175..186", kind,
					        pfx == '!' ? "0x8F54+id*0x40 внутри m_aVehicleHandling" : pfx == '$' ? "0x7B24+id*0x58 внутри m_aVehicleHandling" : "GetBoatPointer → PREDATOR"), "");
			}
			if(nfields != expect)
				ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("%c-строка: %d полей после имени вместо %d — недостающие остаются старыми/нулевыми, лишние игнорируются", pfx, nfields, expect), "");
			for(size_t k = first; k < t.size(); k++){
				bool trailing = false;
				if(!numPrefix(t[k], &trailing)){ ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("поле %d «%s» не число (atof → 0)", (int)(k - first + 1), t[k].c_str()), ""); break; }
				if(trailing) ctx.add(SEV_INFO, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("поле %d «%s»: atof читает числовой префикс, хвост игнорируется (ваниль: RCRAIDER «0.1s»)", (int)(k - first + 1), t[k].c_str()), "");
			}
		}else if(pfx == '^'){
			if(t.size() != 36) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-07", logical, l.number, "", fmt("^-строка: %d токенов вместо 36", (int)t.size()), "");
			if(t.size() > 1 && (!IsIntToken(t[1]) || ti(t[1]) < 0 || ti(t[1]) > 29))
				ctx.add(SEV_ERROR, CAT_HANDLING, "HND-07", logical, l.number, "", fmt("^-строка: индекс группы «%s» вне 0..29 — CopyAnimGroup пишет по 0xC1CDC0+idx*0x94 без границы", t.size() > 1 ? t[1].c_str() : ""), "");
			else if(t.size() > 1) animGroupsMax = std::max(animGroupsMax, ti(t[1]));
		}else{
			// car line: NAME + 35 fields
			const std::string &name = t[0];
			bool exact = false;
			int id = findHandlingPrefix(name, &exact);
			if(id == 210)
				ctx.add(SEV_ERROR, CAT_HANDLING, "HND-03", logical, l.number, name, fmt("«%s» не из 210 встроенных имён%s — FindExactWord вернёт 210: 0xE0 байт лягут в m_aBikeHandling[0..3] (BIKE, MOPED, DIRTBIKE, FCR900)", name.c_str(),
				        name[0] == '#' ? " («#» — не комментарий, строка разбирается как машина)" : (islower((unsigned char)name[0]) ? " (регистр важен: имена в верхнем регистре)" : "")),
				        "Своих имён быть не может; для мода переиспользуй имя из 210.");
			else if(!exact)
				ctx.add(SEV_WARN, CAT_HANDLING, "HND-03b", logical, l.number, name, fmt("«%s» совпало с «%s» только по префиксу — данные лягут в %s", name.c_str(), HANDLING_NAMES[id], HANDLING_NAMES[id]), "");
			if(t.size() != 36)
				ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("%d токенов вместо 36 — недостающие поля остаются старыми/нулевыми, лишние игнорируются", (int)t.size()), "");
			// numeric fields: all except 15 driveType, 16 engineType, 31/32 hex flags
			for(size_t k = 1; k < t.size() && k <= 35; k++){
				if(k == 15 || k == 16) continue;
				if(k == 31 || k == 32){ if(!IsHexToken(t[k])) ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("поле %d «%s» должно быть hex", (int)k, t[k].c_str()), ""); continue; }
				bool trailing = false;
				if(!numPrefix(t[k], &trailing)){ ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("поле %d «%s» не число (atof → 0)", (int)k, t[k].c_str()), ""); break; }
				if(trailing) ctx.add(SEV_INFO, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("поле %d «%s»: atof читает числовой префикс, хвост игнорируется", (int)k, t[k].c_str()), "");
			}
			if(t.size() > 15 && !(t[15][0] == 'F' || t[15][0] == 'R' || t[15][0] == '4')) ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("driveType «%s» не F/R/4", t[15].c_str()), "");
			if(t.size() > 16 && !(t[16][0] == 'P' || t[16][0] == 'D' || t[16][0] == 'E')) ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("engineType «%s» не P/D/E", t[16].c_str()), "");
			if(t.size() > 1 && tf(t[1]) <= 0) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-06", logical, l.number, name, "mass ≤ 0 — 1/mass = INF, физика взрывается", "");
			if(t.size() > 7 && (tf(t[7]) <= 0 || tf(t[7]) > 255)) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-06", logical, l.number, name, fmt("percentSubmerged %s вне 1..255 (байт; 0 = деление на ноль в плавучести)", t[7].c_str()), "");
			if(t.size() > 11){ int g = ti(t[11]); if(g == 0 || g >= 6) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-06", logical, l.number, name, fmt("numGears %d — 0: машина не едет (NaN-передачи); ≥6: InitGearRatios затирает driveType/engineType/numGears/флаги (+0x74..+0x80)", g), ""); }
			if(t.size() > 12 && tf(t[12]) <= 0) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-06", logical, l.number, name, "maxVel ≤ 0", "");
			if(t.size() > 20 && tf(t[20]) <= 0) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-06", logical, l.number, name, "steeringLock ≤ 0", "");
			if(t.size() > 33 && (ti(t[33]) < 0 || ti(t[33]) > 3)) ctx.add(SEV_INFO, CAT_HANDLING, "HND-06b", logical, l.number, name, fmt("frontLights %s вне 0..3", t[33].c_str()), "");
			if(t.size() > 34 && (ti(t[34]) < 0 || ti(t[34]) > 3)) ctx.add(SEV_INFO, CAT_HANDLING, "HND-06b", logical, l.number, name, fmt("rearLights %s вне 0..3", t[34].c_str()), "");
			if(t.size() > 35 && (ti(t[35]) < 0 || ti(t[35]) >= 30)) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-07", logical, l.number, name, fmt("animGroup %s ≥ 30 — индекс m_vehicleAnimGroups[30] без границы (анимации посадки из мусора)", t[35].c_str()), "");
		}
	}
	if(!sawEnd) ctx.add(SEV_INFO, CAT_HANDLING, "HND-17", logical, -1, "", "нет строки «;the end» — читается до конца файла", "");
	(void)animGroupsMax;
}

// ------------------------------------------------------------- carcols.dat ---

static void checkCarcols(Context &ctx, std::set<std::string> &carsWithColours, int &colCount)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/carcols.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){
		ctx.add(SEV_FATAL, CAT_HANDLING, "HND-01", logical, -1, "", "data/carcols.dat не найден — LoadVehicleColours не проверяет FILE* → краш при старте", "");
		return;
	}
	ctx.rep->countFile(CAT_HANDLING);
	int sect = 0;	// 1 col, 2 car, 3 car4
	colCount = 0;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.raw.size() >= 1024) ctx.add(SEV_WARN, CAT_HANDLING, "HND-14", logical, l.number, "", "строка ≥ 1024 байт — ReadLine(0x400) режет её на две записи", "");
		if(l.norm.empty() || l.norm[0] == '#') continue;
		const std::vector<std::string> &t = l.tok;
		if(sect == 0){
			if(l.norm.compare(0, 4, "car4") == 0) sect = 3;
			else if(l.norm.compare(0, 3, "col") == 0) sect = 1;
			else if(l.norm.compare(0, 3, "car") == 0) sect = 2;
			continue;
		}
		if(l.norm.compare(0, 3, "end") == 0){ sect = 0; continue; }
		if(sect == 1){
			// sscanf("%d %d %d"): a token like "77.93" yields 77 and stops the scan at ".93"
			int ints = 0;
			for(size_t k = 0; k < t.size() && k < 3; k++){
				char *e = nullptr; strtol(t[k].c_str(), &e, 10);
				if(e == t[k].c_str()) break;
				ints++;
				if(*e != '\0') break;
			}
			colCount++;
			if(ints < 3) ctx.add(SEV_INFO, CAT_HANDLING, "HND-09c", logical, l.number, "", fmt("цвет №%d: sscanf(\"%%d %%d %%d\") разобрал только %d значений (например «77.93,96») — остальные каналы берутся со стека предыдущей строки", colCount - 1, ints), "");
			for(size_t k = 0; k < t.size() && k < 3; k++) if(IsIntToken(t[k]) && (ti(t[k]) < 0 || ti(t[k]) > 255)) { ctx.add(SEV_WARN, CAT_HANDLING, "HND-09b", logical, l.number, "", fmt("цвет №%d: канал %s вне 0..255 (хранится байтом)", colCount - 1, t[k].c_str()), ""); break; }
			if(l.raw.find('#') == std::string::npos) ctx.add(SEV_INFO, CAT_HANDLING, "HND-10", logical, l.number, "", fmt("цвет №%d без «#»-комментария — движок сканирует строку за буфер до ближайшего «#» (только чтение)", colCount - 1), "");
			if(colCount == 129) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-09", logical, l.number, "", "129-й цвет — таблица ms_vehicleColourTable на 128, дальше затираются ms_pVehicleStructurePool/TXD-указатели и статики до CAnimManager", "");
		}else{
			if(t.empty()) continue;
			const std::string &name = t[0];
			const ObjDef *def = gd.findObjByName(name);
			int maxNums = sect == 2 ? 16 : 32;
			int nums = 0;
			for(size_t k = 1; k < t.size() && nums < maxNums; k++){ if(IsIntToken(t[k])) nums++; else break; }
			if(def == nullptr){
				ctx.add(SEV_FATAL, CAT_HANDLING, "HND-08", logical, l.number, name, fmt("%s «%s»: имя не является моделью ни в одном IDE — GetModelInfo → NULL, запись numVariations по адресу 0x2D0 → краш", sect == 2 ? "car" : "car4", name.c_str()), "");
				continue;
			}
			if(def->type != OT_CARS)
				ctx.add(SEV_ERROR, CAT_HANDLING, "HND-08", logical, l.number, name, fmt("«%s» — модель типа %s, не машина: запись цветов +0x2B0..+0x2D0 уходит за пределы modelinfo (порча соседних записей хранилища)", name.c_str(), objTypeName(def->type)), "");
			carsWithColours.insert(lower(name));
			if((int)t.size() - 1 > maxNums) ctx.add(SEV_INFO, CAT_HANDLING, "HND-10", logical, l.number, name, fmt("%d чисел, sscanf читает максимум %d (ваниль stafford — 18)", (int)t.size() - 1, maxNums), "");
			for(size_t k = 1; k < t.size() && (int)k <= maxNums; k++){
				if(!IsIntToken(t[k])) break;
				int idx = ti(t[k]);
				if(idx < 0 || idx > 255) { ctx.add(SEV_WARN, CAT_HANDLING, "HND-09b", logical, l.number, name, fmt("индекс цвета %d вне байта", idx), ""); break; }
			}
			// index vs colour count is checked after the col section is complete (below)
		}
	}
	// second pass for index-range warnings (col section precedes car in vanilla, but be safe)
	sect = 0;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '#') continue;
		const std::vector<std::string> &t = l.tok;
		if(sect == 0){
			if(l.norm.compare(0, 4, "car4") == 0) sect = 3; else if(l.norm.compare(0, 3, "col") == 0) sect = 1; else if(l.norm.compare(0, 3, "car") == 0) sect = 2;
			continue;
		}
		if(l.norm.compare(0, 3, "end") == 0){ sect = 0; continue; }
		if(sect < 2 || t.empty()) continue;
		int maxNums = sect == 2 ? 16 : 32;
		std::string bad;
		for(size_t k = 1; k < t.size() && (int)k <= maxNums; k++){
			if(!IsIntToken(t[k])) break;
			int idx = ti(t[k]);
			if(idx >= colCount && idx >= 0 && idx <= 255) bad += (bad.empty() ? "" : ", ") + t[k];
		}
		if(!bad.empty())
			ctx.add(SEV_WARN, CAT_HANDLING, "HND-09b", logical, l.number, t[0], fmt("индексы цветов %s ≥ числа цветов в col (%d) — ChooseVehicleColour читает за таблицей (мусорный цвет; ваниль: moonbeam 227)", bad.c_str(), colCount), "");
	}
}

// ------------------------------------------------------------- carmods.dat ---

static const char *MOD_PREFIXES[] = { "chss_", "wheel_", "exh_", "fbmp_", "rbmp_", "misc_a_", "misc_b_", "misc_c_",
	"bnt_", "bntl_", "bntr_", "spl_", "wg_l_", "wg_r_", "fbb_", "bbb_", "lgt_", "rf_", "nto_", "hydralics", "stereo", nullptr };

static void checkCarmods(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/carmods.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){
		ctx.add(SEV_FATAL, CAT_HANDLING, "HND-01", logical, -1, "", "data/carmods.dat не найден — LoadVehicleUpgrades не проверяет FILE* → краш при старте", "");
		return;
	}
	ctx.rep->countFile(CAT_HANDLING);
	std::string sect;
	int links = 0;
	int wheelsPerGroup[4] = {0, 0, 0, 0};
	auto modelCheck = [&](const DataLine &l, const std::string &name, const char *ctxName) -> const ObjDef* {
		const ObjDef *d = gd.findObjByName(name);
		if(d == nullptr)
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-11", logical, l.number, name, fmt("%s: «%s» не определена ни в одном IDE — SetupVehicleUpgradeFlags вызывается с this = NULL (mov ax,[ecx+0x12]) → краш при старте", ctxName, name.c_str()),
			        "Все имена carmods.dat (машины, детали, link, wheel, hydralics/stereo) должны быть моделями IDE (ваниль: veh_mods.ide).");
		return d;
	};
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '#') continue;
		const std::vector<std::string> &t = l.tok;
		if(sect.empty()){
			if(l.norm.compare(0, 4, "link") == 0) sect = "link";
			else if(l.norm.compare(0, 4, "mods") == 0) sect = "mods";
			else if(l.norm.compare(0, 5, "wheel") == 0) sect = "wheel";
			continue;
		}
		if(l.norm.compare(0, 3, "end") == 0){ sect.clear(); continue; }
		if(sect == "link"){
			links++;
			if(t.size() < 2){ ctx.add(SEV_WARN, CAT_HANDLING, "HND-12", logical, l.number, "", "link: меньше двух имён", ""); continue; }
			modelCheck(l, t[0], "link"); modelCheck(l, t[1], "link");
			if(links == 31) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-12", logical, l.number, "", "31-я link-строка — ms_linkedUpgrades на 30, AddUpgradeLink затирает собственный счётчик", "");
		}else if(sect == "mods"){
			if(t.empty()) continue;
			const ObjDef *veh = modelCheck(l, t[0], "mods");
			if(veh && veh->type != OT_CARS)
				ctx.add(SEV_ERROR, CAT_HANDLING, "HND-11", logical, l.number, t[0], fmt("mods: «%s» — модель типа %s, не машина: слоты апгрейдов пишутся по +0x2D6 за пределами modelinfo", t[0].c_str(), objTypeName(veh->type)), "");
			int parts = (int)t.size() - 1;
			if(parts > 16) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-12", logical, l.number, t[0], fmt("%d деталей в строке mods — m_anUpgrades[18] с hydralics/stereo: 17-я затирает m_anRemapTxds (+0x2FA), 18-я — m_nAnimBlockIndex (+0x304) → краш стриминга", parts), "");
			for(size_t k = 1; k < t.size(); k++){
				modelCheck(l, t[k], "mods");
				bool known = false;
				for(int p = 0; MOD_PREFIXES[p]; p++) if(t[k].compare(0, strlen(MOD_PREFIXES[p]), MOD_PREFIXES[p]) == 0) known = true;
				if(!known) ctx.add(SEV_WARN, CAT_HANDLING, "HND-13", logical, l.number, t[k], fmt("деталь «%s» без известного префикса (chss_ wheel_ exh_ fbmp_ rbmp_ misc_a_/b_/c_ bnt_ bntl_ bntr_ spl_ wg_l_ wg_r_ fbb_ bbb_ lgt_ rf_ nto_ hydralics stereo) — слот в мод-шопе не назначится", t[k].c_str()), "");
			}
		}else if(sect == "wheel"){
			if(t.empty() || !IsIntToken(t[0])){ ctx.add(SEV_WARN, CAT_HANDLING, "HND-12", logical, l.number, "", "wheel: первый токен должен быть номером группы", ""); continue; }
			int g = ti(t[0]);
			if(g < 0 || g > 3){ ctx.add(SEV_ERROR, CAT_HANDLING, "HND-12", logical, l.number, "", fmt("wheel: группа %d вне 0..3 — AddWheelUpgrade пишет мимо ms_upgradeWheels[4][15] (счётчики, ms_compsUsed, таблица цветов)", g), ""); continue; }
			for(size_t k = 1; k < t.size(); k++){
				modelCheck(l, t[k], "wheel");
				wheelsPerGroup[g]++;
				if(wheelsPerGroup[g] == 16) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-12", logical, l.number, t[k], fmt("16-я модель в группе колёс %d — по 15 на группу, дальше затираются счётчики", g), "");
			}
		}
	}
	// hydralics / stereo must exist as models (vanilla veh_mods.ide 1086/1087)
	for(const char *nm : { "hydralics", "stereo" })
		if(gd.findObjByName(nm) == nullptr)
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-11", logical, -1, nm, fmt("встроенное имя «%s» не определено ни в одном IDE — LoadVehicleUpgrades ищет его для каждой mods-строки → SetupVehicleUpgradeFlags(NULL)", nm), "");
}

// -------------------------------------------------------------- cargrp.dat ---

static void checkCargrp(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/cargrp.dat";
	std::vector<DataLine> lines;
	std::string phys;
	if(!ReadDataLines(gd, logical, lines, &phys)){
		ctx.add(SEV_FATAL, CAT_HANDLING, "HND-01", logical, -1, "", "data/cargrp.dat не найден — LoadCarGroups не проверяет FILE* → краш при старте", "");
		return;
	}
	ctx.rep->countFile(CAT_HANDLING);
	// trailing newline (HND-14)
	{
		std::vector<uint8_t> raw;
		if(readFile(phys, raw) && !raw.empty() && raw.back() != '\n')
			ctx.add(SEV_WARN, CAT_HANDLING, "HND-14", logical, (int)lines.size(), "", "последняя строка без перевода строки — сканер ищет 0x0A мимо NUL за буфером (обычно упирается в старый 0x0A; порча стека только если строка самая длинная в файле)", "");
	}
	int groups = 0;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.raw.size() >= 1024) ctx.add(SEV_WARN, CAT_HANDLING, "HND-14", logical, l.number, "", "строка ≥ 1024 байт — ReadLine(0x400) режет её на две группы", "");
		if(l.norm.empty() || l.norm[0] == '#') continue;
		int accepted = 0, seen = 0;
		for(size_t k = 0; k < l.tok.size(); k++){
			const std::string &name = l.tok[k];
			if(name[0] == '#') break;
			seen++;
			if(seen > 23){ if(seen == 24) ctx.add(SEV_INFO, CAT_HANDLING, "HND-16b", logical, l.number, "", fmt("больше 23 имён в строке (%d) — движок читает первые 23", (int)l.tok.size()), ""); break; }
			const ObjDef *d = gd.findObjByName(name);
			if(d == nullptr){ ctx.add(SEV_WARN, CAT_HANDLING, "HND-15", logical, l.number, name, fmt("«%s» не модель ни в одном IDE — молча пропущено", name.c_str()), ""); continue; }
			accepted++;
			if(d->type != OT_CARS)
				ctx.add(SEV_FATAL, CAT_HANDLING, "HND-15", logical, l.number, name, fmt("«%s» — модель типа %s, не машина: id попадёт в группу трафика, CCarCtrl построит машину из чужой modelinfo → краш", name.c_str(), objTypeName(d->type)), "");
		}
		if(accepted){
			groups++;
			if(groups == 35) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-16", logical, l.number, "", "35-я группа — m_nNumCarsInGroup на 34; счётчик и модели уходят в паддинг и далее в m_PedGroups (педы со id машин → краш при спавне)", "");
		}
	}
}

// ---------------------------------------------- vehicles.ide side (VEH-13..20) ---

static void checkVehiclesIde(Context &ctx, const std::set<std::string> &carsWithColours)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	// IFP block names (lower) for VEH-16
	std::set<std::string> ifpBlocks;
	for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].kind == EK_IFP && gd.isWinner((int)i)) ifpBlocks.insert(gd.entries[i].base);
	// remap TXD scan (VEH-20): "<carname><digits>.txd" → strip ALL trailing digits
	std::map<std::string, std::vector<std::string>> remaps;
	std::map<std::string, int> carsByLower;
	for(size_t i = 0; i < gd.objs.size(); i++) if(gd.objs[i].type == OT_CARS) carsByLower[lower(gd.objs[i].name)] = (int)i;
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(e.kind != EK_TXD || !gd.isWinner((int)i)) continue;
		std::string b = e.base;
		size_t n = b.size();
		while(n > 0 && isdigit((unsigned char)b[n - 1])) n--;
		if(n == b.size() || n == 0) continue;
		std::string stripped = b.substr(0, n);
		auto it = carsByLower.find(stripped);
		if(it == carsByLower.end()) continue;
		const ObjDef &car = gd.objs[(size_t)it->second];
		if(car.id < 400 || car.id > 630) continue;
		remaps[stripped].push_back(e.name);
	}
	for(auto it = remaps.begin(); it != remaps.end(); ++it){
		if(it->second.size() > 4){
			std::string list; for(size_t k = 0; k < it->second.size(); k++) list += (k ? ", " : "") + it->second[k];
			ctx.add(SEV_INFO, CAT_VEH, "VEH-20", "IMG", -1, it->first, fmt("%d remap-TXD для машины «%s» (%s) — m_anRemapTxds на 4, 5-й и далее уходят в паддинг и молча теряются", (int)it->second.size(), it->first.c_str(), list.c_str()), "");
		}
	}
	// VEH-20b: a vehicle name ending in digits that after stripping names another vehicle
	for(auto it = carsByLower.begin(); it != carsByLower.end(); ++it){
		std::string b = it->first; size_t n = b.size();
		while(n > 0 && isdigit((unsigned char)b[n - 1])) n--;
		if(n == b.size() || n == 0) continue;
		auto other = carsByLower.find(b.substr(0, n));
		if(other != carsByLower.end() && other != it){
			const ObjDef &o = gd.objs[(size_t)other->second];
			if(o.id >= 400 && o.id <= 630)
				ctx.add(SEV_INFO, CAT_VEH, "VEH-20b", gd.objs[(size_t)it->second].file, gd.objs[(size_t)it->second].line, gd.objs[(size_t)it->second].name,
				        fmt("имя «%s» после среза цифр = «%s» (другая машина, id %d) — её TXD «%s.txd» зарегистрируется как remap чужой машины (AssignRemapTxd срезает все хвостовые цифры)", gd.objs[(size_t)it->second].name.c_str(), o.name.c_str(), o.id, gd.objs[(size_t)it->second].name.c_str()), "", gd.objs[(size_t)it->second].id);
		}
	}
	// per vehicle
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.type != OT_CARS) continue;
		const std::vector<std::string> &t = o.tok;
		std::string type = lower(o.carType);
		// VEH-13 / VEH-14
		int hid = findHandlingExact(o.carHandling);
		if(hid == 210)
			ctx.add(SEV_ERROR, CAT_VEH, "VEH-13", o.file, o.line, o.name, fmt("handling «%s» не из 210 встроенных имён (точно, верхний регистр, ≤ 13 символов) — GetHandlingId вернёт 210 = m_aBikeHandling[0] (BIKE) как tHandlingData: mass 0.35, turnMass 0.34 → физика-мусор", o.carHandling.c_str()),
			        "Своих имён handling нет; выбери одно из 210 (см. handling.cfg).", o.id);
		else{
			bool bad = false; const char *range = "";
			if(type == "bike" || type == "bmx"){ bad = !(hid >= 162 && hid <= 174); range = "162..174 (байки)"; }
			else if(type == "boat"){ bad = !(hid >= 175 && hid <= 186); range = "175..186 (лодки)"; }
			else if(type == "heli" || type == "plane" || type == "f_heli" || type == "f_plane"){ bad = !(hid >= 186 && hid <= 209); range = "186..209 (летающие)"; }
			if(bad) ctx.add(SEV_WARN, CAT_VEH, "VEH-14", o.file, o.line, o.name, fmt("тип %s с handling «%s» (индекс %d) не из %s — CBike/GetBoatPointer/GetFlyingPointer возьмут чужие данные (PREDATOR/SEAPLANE/индекс за массивом)", o.carType.c_str(), o.carHandling.c_str(), hid, range), "", o.id);
		}
		// VEH-16 anims
		if(t.size() > 6){
			std::string anims = lower(t[6]);
			if(anims != "null" && !ifpBlocks.count(anims))
				ctx.add(SEV_WARN, CAT_VEH, "VEH-16", o.file, o.line, o.name, fmt("anims «%s»: файла «%s.ifp» нет ни в одном IMG — GetAnimationBlockIndex → −1, блок анимаций не стримится с машиной", t[6].c_str(), t[6].c_str()), "", o.id);
		}
		// VEH-17 wheel scale / bike steer angle
		if(isCarFamily(type)){
			if(t.size() > 12 && IsNumToken(t[12]) && tf(t[12]) <= 0) ctx.add(SEV_ERROR, CAT_VEH, "VEH-17", o.file, o.line, o.name, fmt("wheelScaleFront %s ≤ 0 — радиус колёсной сферы = scale/2 в SetupSuspensionLines", t[12].c_str()), "", o.id);
			if(t.size() > 13 && IsNumToken(t[13]) && tf(t[13]) <= 0) ctx.add(SEV_ERROR, CAT_VEH, "VEH-17", o.file, o.line, o.name, fmt("wheelScaleRear %s ≤ 0", t[13].c_str()), "", o.id);
		}else if(type == "bike" || type == "bmx"){
			if(t.size() > 11 && IsNumToken(t[11])){ float a = tf(t[11]); if(a <= 0 || a >= 90) ctx.add(SEV_ERROR, CAT_VEH, "VEH-17", o.file, o.line, o.name, fmt("поле 12 для байка = угол руля %s° — должен быть в (0, 90): CBike ctor берёт tan(deg2rad) → NaN/∞", t[11].c_str()), "", o.id); }
		}
		// VEH-18 upgrade class
		if(t.size() > 14 && IsIntToken(t[14])){ int u = ti(t[14]); if(u < -1 || u > 3) ctx.add(SEV_ERROR, CAT_VEH, "VEH-18", o.file, o.line, o.name, fmt("wheelUpgradeClass %d не в {−1,0,1,2,3} — GetNumWheelUpgrades/GetWheelUpgrade читают за таблицей колёс в мод-шопе", u), "", o.id); }
		// HND-10 absent from carcols
		if(!carsWithColours.empty() && !carsWithColours.count(lower(o.name)))
			ctx.add(SEV_INFO, CAT_HANDLING, "HND-10", o.file, o.line, o.name, fmt("«%s» нет в carcols.dat — m_nNumColorVariations = 0, цвет 0/0/0", o.name.c_str()), "", o.id);
	}
	// VEH-19 shared textures
	auto veh = gd.txdTextures.find("vehicle");
	if(veh != gd.txdTextures.end()){
		auto has = [&](const char *n){ return std::find(veh->second.names.begin(), veh->second.names.end(), n) != veh->second.names.end(); };
		for(const char *n : { "platecharset", "plateback1", "plateback2", "plateback3" })
			if(!has(n)) ctx.add(SEV_FATAL, CAT_VEH, "VEH-19", "models/generic/vehicle.txd", -1, n, fmt("в vehicle.txd нет текстуры «%s» — CCustomCarPlateMgr::Initialise делает mov byte [tex+0x50],1 без NULL-проверки → краш при старте", n), "");
		for(const char *n : { "vehiclelights128", "vehiclelightson128" })
			if(!has(n)) ctx.add(SEV_ERROR, CAT_VEH, "VEH-19", "models/generic/vehicle.txd", -1, n, fmt("в vehicle.txd нет текстуры «%s» — NULL-текстура фар, краш при отрисовке света машины", n), "");
	}
	auto part = gd.txdTextures.find("particle");
	if(part != gd.txdTextures.end() && std::find(part->second.names.begin(), part->second.names.end(), "white") == part->second.names.end())
		ctx.add(SEV_FATAL, CAT_VEH, "VEH-19", "models/particle.txd", -1, "white", "в particle.txd нет текстуры «white» — LoadEnvironmentMaps пишет *(tex+0x10) без NULL-проверки → краш при старте", "");
}


// ------------------------------------------ III / VC: handling.cfg (re3 / reVC cHandlingDataMgr::LoadHandlingData) ---
// Имена вшиты в exe (VehicleNames[NUMHANDLINGS][14]); файл целиком читается в work_buff[55000], строки режутся по '\n',
// конец — строка ровно «;the end» (иначе цикл уходит за буфер). Неизвестное имя → FindExactWord = −1 → запись в
// HandlingData[−1]. VC: «!» байки (GetBikePointer без проверки), «$» летающие, «%» лодки (вне диапазона → элемент 0).

static const char *III_HANDLING_NAMES[] = {
	"LANDSTAL","IDAHO","STINGER","LINERUN","PEREN","SENTINEL","PATRIOT","FIRETRUK","TRASH","STRETCH","MANANA","INFERNUS","BLISTA","PONY","MULE",
	"CHEETAH","AMBULAN","FBICAR","MOONBEAM","ESPERANT","TAXI","KURUMA","BOBCAT","MRWHOOP","BFINJECT","POLICE","ENFORCER","SECURICA","BANSHEE",
	"PREDATOR","BUS","RHINO","BARRACKS","TRAIN","HELI","DODO","COACH","CABBIE","STALLION","RUMPO","RCBANDIT","BELLYUP","MRWONGS","MAFIA","YARDIE",
	"YAKUZA","DIABLOS","COLUMB","HOODS","AIRTRAIN","DEADDODO","SPEEDER","REEFER","PANLANT","FLATBED","YANKEE","BORGNINE" };
static const char *VC_HANDLING_NAMES[] = {
	"LANDSTAL","IDAHO","STINGER","LINERUN","PEREN","SENTINEL","PATRIOT","FIRETRUK","TRASH","STRETCH","MANANA","INFERNUS","PONY","MULE","CHEETAH",
	"AMBULAN","FBICAR","MOONBEAM","ESPERANT","TAXI","KURUMA","BOBCAT","MRWHOOP","BFINJECT","POLICE","ENFORCER","SECURICA","BANSHEE","BUS","RHINO",
	"BARRACKS","TRAIN","HELI","DODO","COACH","CABBIE","STALLION","RUMPO","RCBANDIT","MAFIA","AIRTRAIN","DEADDODO","FLATBED","YANKEE","GOLFCART",
	"VOODOO","WASHING","CUBAN","ROMERO","PACKER","ADMIRAL","GANGBUR","ZEBRA","TOPFUN","GLENDALE","OCEANIC","HERMES","SABRE1","SABRETUR","PHEONIX",
	"WALTON","REGINA","COMET","DELUXO","BURRITO","SPAND","BAGGAGE","KAUFMAN","RANCHER","FBIRANCH","VIRGO","GREENWOO","HOTRING","SANDKING","BLISTAC",
	"BOXVILLE","BENSON","DESPERAD","LOVEFIST","BLOODRA","BLOODRB","BIKE","MOPED","DIRTBIKE","ANGEL","FREEWAY","PREDATOR","SPEEDER","REEFER","RIO",
	"SQUALO","TROPIC","COASTGRD","DINGHY","MARQUIS","CUPBOAT","SEAPLANE","SPARROW","SEASPAR","MAVERICK","COASTMAV","POLMAV","HUNTER","RCBARON",
	"RCGOBLIN","RCCOPTER" };
enum { VC_H_BIKE = 81, VC_H_FREEWAY = 85, VC_H_PREDATOR = 86, VC_H_SEAPLANE = 96, VC_H_RCCOPTER = 105 };

// FindExactWord: strncmp(word, name, 14) по всем именам — точное совпадение (имена < 14 символов)
static int legacyHandlingId(const GameData &gd, const std::string &name)
{
	const char **tab = gd.isVC() ? VC_HANDLING_NAMES : III_HANDLING_NAMES;
	int n = gd.isVC() ? (int)(sizeof(VC_HANDLING_NAMES) / sizeof(*VC_HANDLING_NAMES)) : (int)(sizeof(III_HANDLING_NAMES) / sizeof(*III_HANDLING_NAMES));
	for(int i = 0; i < n; i++) if(strncmp(tab[i], name.c_str(), 14) == 0) return i;
	return -1;
}

static void checkHandlingLegacy(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/handling.cfg";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){
		ctx.add(SEV_FATAL, CAT_HANDLING, "HND-01", logical, -1, "", "data/handling.cfg не найден — LoadHandlingData разбирает work_buff с прошлым содержимым, «;the end» не найден → чтение за буфером", "");
		return;
	}
	ctx.rep->countFile(CAT_HANDLING);
	{
		std::vector<uint8_t> raw;
		if(readFile(DataFilePath(gd, logical), raw) && raw.size() > 55000)
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-17", logical, -1, "", fmt("файл %u байт > work_buff[55000] — LoadFile обрезает его, «;the end» теряется, цикл разбора уходит за буфер", (unsigned)raw.size()), "");
	}
	bool sawEnd = false;
	int cars = 0;
	std::set<int> seen;
	int carFields = gd.isVC() ? 33 : 32;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		std::string t0 = l.raw; while(!t0.empty() && (t0.back() == '\r' || t0.back() == '\n')) t0.pop_back();
		if(t0 == ";the end"){ sawEnd = true; break; }
		if(t0.compare(0, 8, ";the end") == 0)
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-02", logical, l.number, "", "строка «;the end» с хвостом — сравнение strcmp точное, конец файла не распознан → разбор идёт за буфер", "Строка должна быть ровно «;the end».");
		if(!t0.empty() && t0[0] == ';') continue;
		const std::vector<std::string> &t = l.tok;
		if(t.empty()){
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-02", logical, l.number, "", "пустая строка до «;the end» — strtok даёт NULL, FindExactWord(NULL) → краш при старте", "Пустых строк быть не должно; комментарий — «;» первым символом.");
			continue;
		}
		char pfx = t[0][0];
		bool prefixed = gd.isVC() && (pfx == '!' || pfx == '$' || pfx == '%');
		if(prefixed){
			// поле 0 — сам префикс (отдельное слово), поле 1 — имя; «!BIKE» слитно → имя ищется во втором слове
			if(t[0].size() > 1){
				ctx.add(SEV_FATAL, CAT_HANDLING, "HND-03", logical, l.number, t[0], fmt("«%s» — префикс «%c» должен быть отдельным словом: поле 1 (%s) уходит в FindExactWord → −1", t[0].c_str(), pfx, t.size() > 1 ? t[1].c_str() : "пусто"), "");
				continue;
			}
			std::string name = t.size() > 1 ? t[1] : "";
			if(name.empty()){
				ctx.add(pfx == '!' ? SEV_FATAL : SEV_ERROR, CAT_HANDLING, "HND-02", logical, l.number, "", fmt("строка из одного «%c» — %s", pfx, pfx == '!' ? "ConvertBikeDataToGameUnits(NULL) → краш" : "ничего не записано"), "");
				continue;
			}
			int id = legacyHandlingId(gd, name);
			int nfields = (int)t.size();	// с префиксом и именем: «!» 17, «$» 20, «%» 16 (ваниль)
			int expect = pfx == '!' ? 17 : pfx == '$' ? 20 : 16;
			if(id < 0){
				ctx.add(SEV_FATAL, CAT_HANDLING, "HND-03", logical, l.number, name, fmt("%c %s: имени нет среди %d встроенных — FindExactWord = −1, %s", pfx, name.c_str(), 106,
				        pfx == '!' ? "GetBikePointer(−1) пишет за пределами BikeHandlingData" : "запись уходит в элемент 0 (SEAPLANE/PREDATOR)"),
				        "Таблица имён вшита в exe: свои имена в handling.cfg невозможны, переиспользуй одно из встроенных.");
			}else if(pfx == '!' && (id < VC_H_BIKE || id > VC_H_FREEWAY))
				ctx.add(SEV_FATAL, CAT_HANDLING, "HND-04", logical, l.number, name, fmt("! %s: не байк (BIKE..FREEWAY) — BikeHandlingData[%d] вне массива из 5", name.c_str(), id - VC_H_BIKE), "");
			else if(pfx == '$' && (id < VC_H_SEAPLANE || id > VC_H_RCCOPTER))
				ctx.add(SEV_ERROR, CAT_HANDLING, "HND-04", logical, l.number, name, fmt("$ %s: не летающая (SEAPLANE..RCCOPTER) — GetFlyingPointer отдаёт элемент 0, перезаписывается SEAPLANE", name.c_str()), "");
			else if(pfx == '%' && (id < VC_H_PREDATOR || id > VC_H_SEAPLANE))
				ctx.add(SEV_ERROR, CAT_HANDLING, "HND-04", logical, l.number, name, fmt("%% %s: не лодка (PREDATOR..SEAPLANE) — GetBoatPointer отдаёт элемент 0, перезаписывается PREDATOR", name.c_str()), "");
			if(nfields < expect)
				ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, name, fmt("%c %s: %d полей вместо %d — недостающие остаются нулями", pfx, name.c_str(), nfields, expect), "");
			continue;
		}
		if(!gd.isVC() && (pfx == '!' || pfx == '$' || pfx == '%')){
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-03", logical, l.number, t[0], fmt("строка «%s…» — III не знает префиксов байков/лодок, «%c» ищется как имя машины → FindExactWord = −1 → HandlingData[−1]", t[0].c_str(), pfx), "");
			continue;
		}
		int id = legacyHandlingId(gd, t[0]);
		if(id < 0){
			ctx.add(SEV_FATAL, CAT_HANDLING, "HND-03", logical, l.number, t[0], fmt("имя «%s» не из %d встроенных — FindExactWord = −1, вся строка пишется в HandlingData[−1] (память перед таблицей)", t[0].c_str(), gd.isVC() ? 106 : 57),
			        "Таблица имён вшита в exe (III 57, VC 106): свои имена невозможны, машина в IDE должна ссылаться на одно из них.");
			continue;
		}
		cars++;
		if(seen.count(id)) ctx.add(SEV_INFO, CAT_HANDLING, "HND-06", logical, l.number, t[0], fmt("«%s» встречается второй раз — победит последняя строка", t[0].c_str()), "");
		seen.insert(id);
		if((int)t.size() < carFields)
			ctx.add(SEV_WARN, CAT_HANDLING, "HND-05", logical, l.number, t[0], fmt("%d полей вместо %d — недостающие (%s) остаются нулями", (int)t.size(), carFields, (int)t.size() <= carFields - 3 ? "флаги, фары и далее" : "фары"), "");
		else if((int)t.size() > carFields)
			ctx.add(SEV_INFO, CAT_HANDLING, "HND-05", logical, l.number, t[0], fmt("%d полей — лишние после %d игнорируются", (int)t.size(), carFields), "");
		// поля 1..7: масса и габариты — нули дают деление на 0 в ConvertDataToGameUnits / физике
		if(t.size() > 4){
			float mass = tf(t[1]);
			if(mass <= 0) ctx.add(SEV_FATAL, CAT_HANDLING, "HND-07", logical, l.number, t[0], fmt("масса %s ≤ 0 — ConvertDataToGameUnits делит на массу (1/m = inf) → NaN в физике", t[1].c_str()), "");
			for(int k = 2; k <= 4; k++) if(tf(t[(size_t)k]) <= 0){ ctx.add(SEV_ERROR, CAT_HANDLING, "HND-07", logical, l.number, t[0], fmt("габарит %d = %s ≤ 0 — нулевой момент инерции", k - 1, t[(size_t)k].c_str()), ""); break; }
		}
		if(t.size() > 12){
			int gears = atoi(t[12].c_str());
			if(gears < 1 || gears > 5) ctx.add(SEV_ERROR, CAT_HANDLING, "HND-08", logical, l.number, t[0], fmt("%d передач — таблица передач cTransmission на 5 (1..5)", gears), "");
		}
		if(t.size() > 16 && !t[15].empty() && !strchr("FR4", toupper((unsigned char)t[15][0])))
			ctx.add(SEV_WARN, CAT_HANDLING, "HND-09", logical, l.number, t[0], fmt("привод «%s» — ожидается F/R/4", t[15].c_str()), "");
		if(t.size() > 17 && !t[16].empty() && !strchr("PDE", toupper((unsigned char)t[16][0])))
			ctx.add(SEV_WARN, CAT_HANDLING, "HND-09", logical, l.number, t[0], fmt("двигатель «%s» — ожидается P/D/E", t[16].c_str()), "");
	}
	if(!sawEnd)
		ctx.add(SEV_FATAL, CAT_HANDLING, "HND-02", logical, -1, "", "нет строки «;the end» — цикл LoadHandlingData не останавливается и читает за концом work_buff", "Последняя строка файла должна быть ровно «;the end».");
	// cars в IDE: handling по имени, GetHandlingId возвращает NUMHANDLINGS для неизвестного → HandlingData[NUMHANDLINGS] за массивом
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.type != OT_CARS || o.carHandling.empty()) continue;
		int id = legacyHandlingId(gd, o.carHandling);
		if(id < 0)
			ctx.add(SEV_FATAL, CAT_VEH, "VEH-13", o.file, o.line, o.name, fmt("handling «%s» не из встроенной таблицы — GetHandlingId = %d, GetHandlingData читает за концом массива (масса/габариты — мусор)", o.carHandling.c_str(), gd.isVC() ? 106 : 57),
			        "Имена handling вшиты в exe; выбери подходящее из handling.cfg.", o.id);
		else if(!seen.count(id))
			ctx.add(SEV_ERROR, CAT_VEH, "VEH-13", o.file, o.line, o.name, fmt("handling «%s» есть в exe, но строки в handling.cfg нет — все параметры нули (масса 0 → NaN)", o.carHandling.c_str()), "", o.id);
	}
	(void)cars;
}

void CheckHandling(Context &ctx)
{
	ctx.prog->set("handling / carcols / carmods / cargrp");
	if(!ctx.gd->isSA()){ checkHandlingLegacy(ctx); return; }
	checkHandlingCfg(ctx);
	if(ctx.cancelled()) return;
	std::set<std::string> carsWithColours;
	int colCount = 0;
	checkCarcols(ctx, carsWithColours, colCount);
	checkCarmods(ctx);
	checkCargrp(ctx);
	checkVehiclesIde(ctx, carsWithColours);
}

} // namespace gc
