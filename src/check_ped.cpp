// gtacheck — ped data files PDD-nn: ped.dat, pedstats.dat, pedgrp.dat, popcycle.dat, animgrp.dat grammar,
// peds.ide cross-references, decision makers, surface*.dat, shopping.dat, statdisp.dat, ar_stats.dat.
// Source: E:\RE\addon_check\weapon_ped_rules.md §2–11 + GTACHECK_RULES.md §5 (gta_sa.exe 1.0 US).
#include "gtacheck.h"
#include "tables_ped.h"

#include <stdlib.h>
#include <ctype.h>
#include <algorithm>

namespace gc {

static int ti(const std::string &t) { return (int)strtol(t.c_str(), nullptr, 10); }
static long tl(const std::string &t) { return strtol(t.c_str(), nullptr, 10); }
static float tf(const std::string &t) { return (float)strtod(t.c_str(), nullptr); }

static int findPedType(const std::string &s)
{
	for(int i = 0; i < 32; i++) if(s == PED_TYPE_NAMES[i]) return i;
	return 32;
}

static bool inTable(const char **tab, int n, const std::string &s)
{
	for(int i = 0; i < n; i++) if(s == tab[i]) return true;
	return false;
}

static void missing(Context &ctx, const char *code, const std::string &logical, const char *func)
{
	ctx.add(SEV_FATAL, CAT_PEDDATA, code, logical, -1, "", fmt("%s не найден — %s не проверяет FILE* → LoadLine/ReadLine(NULL) → краш при старте", logical.c_str(), func), "");
}

// ------------------------------------------------------------------ ped.dat ---

static void checkPedDat(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/ped.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){ missing(ctx, "PDD-01", logical, "CPedType::LoadPedData 0x608B30"); return; }
	ctx.rep->countFile(CAT_PEDDATA);
	int cur = 32;			// iVar3 starts at 0x20
	std::string curName;
	std::set<int> seen;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '#') continue;
		const std::vector<std::string> &t = l.tok;
		if(t[0].size() >= 32){ ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-04", logical, l.number, t[0], fmt("первый токен «%.40s…» длиной %d ≥ 32 — sscanf %%s в local_20[32] 0x608B5A → переполнение стека", t[0].c_str(), (int)t[0].size()), ""); continue; }
		bool acq = t[0] == "Hate" || t[0] == "Dislike" || t[0] == "Like" || t[0] == "Respect";
		if(acq){
			if(cur == 32)
				ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-02", logical, l.number, t[0], curName.empty()
				        ? fmt("строка %s до первой строки типа — текущий индекс = 32: запись маски по array+32*0x14 = за концом блока new(0x284) → порча заголовка следующего блока кучи → краш при позднем malloc/free", t[0].c_str())
				        : fmt("строка %s после строки типа «%s», которого нет среди 32 имён (регистр важен) — индекс 32: запись за концом блока new(0x284) → порча кучи → краш", t[0].c_str(), curName.c_str()),
				        "Имена типов: PLAYER1 PLAYER2 PLAYER_NETWORK PLAYER_UNUSED CIVMALE CIVFEMALE COP GANG1..GANG10 DEALER MEDIC FIREMAN CRIMINAL BUM PROSTITUTE SPECIAL MISSION1..MISSION8.");
			for(size_t k = 1; k < t.size(); k++)
				if(findPedType(t[k]) == 32)
					ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-03", logical, l.number, t[k], fmt("«%s» в списке %s не из 32 имён типов (регистр важен) — FindPedType → 32 → вклад в маску 0, молча игнорируется", t[k].c_str(), t[0].c_str()), "");
		}else{
			cur = findPedType(t[0]);
			curName = t[0];
			if(cur == 32){
				// not yet fatal — fatal only when an acquaintance line follows (PDD-02); note it
				ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-02", logical, l.number, t[0], fmt("«%s» не из 32 имён типов и не ключевое слово (Hate/Dislike/Like/Respect с большой буквы) — следующие строки отношений уйдут за конец массива", t[0].c_str()), "");
			}else{
				if(cur <= 3) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-05", logical, l.number, t[0], fmt("блок для player-типа %s", t[0].c_str()), "");
				if(seen.count(cur)) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-05", logical, l.number, t[0], fmt("блок %s повторяется — каждая строка отношений заменяет маску целиком", t[0].c_str()), "");
				seen.insert(cur);
			}
			if(t.size() > 1) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-05", logical, l.number, t[0], "лишние токены после имени типа игнорируются", "");
		}
	}
}

// ------------------------------------------------------------- pedstats.dat ---

static void checkPedStats(Context &ctx, std::vector<std::string> &statNames)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/pedstats.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){ missing(ctx, "PDD-06", logical, "CPedStats::LoadPedStats 0x5BB890"); return; }
	ctx.rep->countFile(CAT_PEDDATA);
	int n = 0;
	bool orderDiff = false;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '#') continue;
		const std::vector<std::string> &t = l.tok;
		n++;
		if(n == 44) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-07", logical, l.number, t[0], "44-я строка данных — блок new(0x8BC) = 43×0x34, iVar4 += 0x34 без проверки → запись за концом блока кучи → краш", "В pedstats.dat ровно 43 строки (позиционные индексы STAT_PLAYER 0 … STAT_COWARD 42).");
		if(t[0].size() >= 32) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-08", logical, l.number, t[0], fmt("имя длиной %d ≥ 32 — sscanf %%s в local_20[32] → переполнение стека", (int)t[0].size()), "");
		else if(t[0].size() >= 24) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-09", logical, l.number, t[0], fmt("имя длиной %d (24..31) — strcpy в 24-байтовое поле, NUL уезжает в fleeDist и затирается: GetPedStatType 0x6088D0 никогда не найдёт это имя", (int)t[0].size()), "");
		if(n <= 43){
			statNames.push_back(t[0]);
			if(t[0] != PEDSTAT_NAMES[n - 1]) orderDiff = true;
		}
		if(t.size() < 11) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-12", logical, l.number, t[0], fmt("%d полей вместо 11 (name fleeDist headingChangeRate fear temper lawfulness sexiness attack defend shootingRate defaultDecisionMaker) — недостающие = мусор со стека", (int)t.size()), "");
		const char *f = "sffddddffdd";
		for(size_t k = 1; k < t.size() && k < 11; k++){
			bool ok = f[k] == 'd' ? IsIntToken(t[k]) : IsNumToken(t[k]);
			if(!ok){ ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-12", logical, l.number, t[0], fmt("поле %d «%s» не число — sscanf останавливается", (int)k, t[k].c_str()), ""); break; }
		}
		static const char *F4[4] = { "fear", "temper", "lawfulness", "sexiness" };
		for(int k = 3; k <= 6; k++) if((int)t.size() > k && IsIntToken(t[(size_t)k]) && (tl(t[(size_t)k]) < 0 || tl(t[(size_t)k]) > 255)) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-11", logical, l.number, t[0], fmt("%s = %s вне 0..255 (байт, по замыслу 0..100)", F4[k - 3], t[(size_t)k].c_str()), "");
		if(t.size() > 9 && IsIntToken(t[9]) && (tl(t[9]) < -32768 || tl(t[9]) > 32767)) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-11", logical, l.number, t[0], fmt("shootingRate %s вне int16", t[9].c_str()), "");
		if(t.size() > 10 && IsIntToken(t[10])){ long dm = tl(t[10]); if(dm < 0 || dm > 7) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-11", logical, l.number, t[0], fmt("defaultDecisionMaker %ld вне 0..7 (GangMbr 0, Cop 1, R_Norm 2, R_Tough 3, R_Weak 4, Fireman 5, m_empty 6, Indoors 7) — +0x32 читается как signed char: 8..19 = групповые/миссионные DM, ≥ 20 — за m_DecisionMakers, 128..255 — отрицательные", dm), ""); }
	}
	if(n < 43) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-10", logical, -1, "", fmt("%d строк вместо 43 — движок адресует статы по позиции (STAT_PLAYER 0, STAT_COP 1 …); недостающие остаются по умолчанию Initialise", n), "");
	else if(orderDiff){
		std::string diff;
		int nd = 0;
		for(size_t k = 0; k < statNames.size() && k < 43; k++) if(statNames[k] != PEDSTAT_NAMES[k]){ if(nd < 5) diff += (nd ? ", " : "") + fmt("%d: %s ≠ %s", (int)k, statNames[k].c_str(), PEDSTAT_NAMES[k]); nd++; }
		ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-10", logical, -1, "", fmt("имена/порядок отличаются от ванильных 43 (%d позиций: %s%s) — движок индексирует по позиции, неизвестное имя в peds.ide → 16", nd, diff.c_str(), nd > 5 ? ", …" : ""), "");
	}
}

// --------------------------------------------------------------- pedgrp.dat ---

static void checkPedGrp(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/pedgrp.dat";
	std::vector<DataLine> lines;
	std::string phys;
	if(!ReadDataLines(gd, logical, lines, &phys)){ missing(ctx, "PDD-13", logical, "CPopulation::LoadPedGroups 0x5BCFE0"); return; }
	ctx.rep->countFile(CAT_PEDDATA);
	{
		std::vector<uint8_t> raw;
		if(readFile(phys, raw) && !raw.empty() && raw.back() != '\n')
			ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-20", logical, (int)lines.size(), "", "последняя строка без перевода строки — сканер 0x5BD02A ищет 0x0A мимо NUL за буфером (обычно упирается в старый 0x0A; порча стека только если строка самая длинная)", "Заверши файл переводом строки (ваниль — CRLF).");
	}
	int groups = 0;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.raw.size() >= 1024) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-19", logical, l.number, "", "строка ≥ 1024 байт — fgets(0x400) режет её на две записи: лишняя группа, сдвиг индексов", "");
		const std::vector<std::string> &t = l.tok;	// ',' and '\r' → space like the engine's scan loop
		// engine: tokens until '#' or 21
		int seen = 0, accepted = 0, resolved = 0;
		for(size_t k = 0; k < t.size(); k++){
			if(t[k][0] == '#') break;
			seen++;
			if(seen == 22){ ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-18", logical, l.number, "", fmt("больше 21 имени в группе (%d) — читаются первые 21 (цикл < 0x15)", (int)t.size()), ""); break; }
			if(t[k].size() >= 256) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-19", logical, l.number, t[k], fmt("токен длиной %d ≥ 256 — strncpy в cStack_500[256] переполняет (внутри того же фрейма)", (int)t[k].size()), "");
			accepted++;
			const ObjDef *d = gd.findObjByName(t[k]);
			if(d == nullptr){ ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-16", logical, l.number, t[k], fmt("«%s» не модель ни в одном IDE — GetModelInfo NULL, молча пропущено", t[k].c_str()), ""); continue; }
			resolved++;
			if(d->type != OT_PEDS)
				ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-17", logical, l.number, t[k], fmt("«%s» — модель типа %s, не педа: id попадёт в группу населения, CPopulation::AddPed прочитает CPedModelInfo (m_pHitColModel +0x34 …) из чужой записи → краш при спавне", t[k].c_str(), objTypeName(d->type)), "");
		}
		if(accepted == 0) continue;
		groups++;
		if(groups == 58) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-14", logical, l.number, "", "58-я группа — m_PedGroups 57×21 (0xC0F358..0xC0FCB2): дальше затираются m_bDontCreateRandomGangMembers … CurrentWorldZone, m_nNumPedsInGroup[60] ложится на m_CarGroups", "");
		if(groups >= 43 && groups <= 50 && resolved == 0) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-21", logical, l.number, "", fmt("группа банды №%d (ePopcyclePedGroup %d) без ни одной найденной модели — ChooseGangOccupation не спавнит банду", groups, groups - 1), "");
	}
	if(groups < 57) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-15", logical, -1, "", fmt("%d групп вместо 57 — поздние ePopcyclePedGroup (DEALERS 52, SHOPKEEPERS 53, OFFICE_WORKERS 54, HUSBANDS 55, WIVES 56 …) пусты", groups), "");
}

// ------------------------------------------------------------- popcycle.dat ---

static void checkPopcycle(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/popcycle.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){ missing(ctx, "PDD-22", logical, "CPopCycle::Initialise 0x5BC090"); return; }
	ctx.rep->countFile(CAT_PEDDATA);
	int rows = 0;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '/') continue;
		rows++;
		if(rows > 480) continue;
		const std::vector<std::string> &t = l.tok;
		int nums = 0; bool bad = false; int sum = 0;
		for(size_t k = 0; k < t.size() && nums < 24; k++){
			if(!IsIntToken(t[k])){ bad = true; break; }
			long v = tl(t[k]);
			if(v < 0 || v > 255) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-26", logical, l.number, "", fmt("значение %ld (поле %d) вне 0..255 — хранится байтом", v, nums + 1), "");
			if(nums >= 6) sum += (int)v;
			nums++;
		}
		if(nums < 24) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-25", logical, l.number, "", fmt("%d чисел вместо 24%s — недостающие остаются от предыдущей строки/мусор", nums, bad ? " (нечисловой токен среди первых 24)" : ""), "");
		else if(sum == 0) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-27", logical, l.number, "", "все 18 процентов групп = 0 — 100/0 → INF, 0·INF = NaN, ftol → 0 у всех, +100 достаётся последней группе: 100 % Aircrew_runway в этом слоте", "");
	}
	if(rows < 480) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-23", logical, -1, "", fmt("%d строк данных вместо 480 (20 типов зон × будни/выходные × 12 слотов) — LoadLine вернёт NULL на EOF, sscanf(NULL) 0x5BC1E4 → strlen(NULL) → краш при старте", rows), "");
	else if(rows > 480) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-24", logical, -1, "", fmt("%d строк данных — читаются первые 480", rows), "");
}

// --------------------------------------------------------------- animgrp.dat ---

static void checkAnimGrpGrammar(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/animgrp.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)) return;	// DAT-03 already reported
	bool in = false;
	int count = 0, got = 0, hdrLine = 0;
	std::string gname;
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '#') continue;
		const std::vector<std::string> &t = l.tok;
		if(!in){
			if(t.size() < 4 || !IsIntToken(t[3])){
				ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-30", logical, l.number, t[0], fmt("заголовок группы «%s»: %s — count не инициализирован: AddAnimAssocDefinition 0x4D3BA0 делает new(count*4)/new(count*0x18) с мусором со стека → bad_alloc/NULL → запись через NULL", l.norm.c_str(), t.size() < 4 ? fmt("%d токенов вместо 4 (name block type count)", (int)t.size()).c_str() : fmt("count «%s» не целое", t[3].c_str()).c_str()), "");
				// still treat as opening a group so the state machine stays in sync
			}
			for(size_t k = 0; k < t.size() && k < 3; k++)
				if(t[k].size() >= 32) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-32", logical, l.number, t[k], fmt("токен заголовка «%.40s…» длиной %d ≥ 32 — sscanf %%s в 32-байтный буфер стека", t[k].c_str(), (int)t[k].size()), "");
			if(t[0].size() >= 16) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-31", logical, l.number, t[0], fmt("имя группы «%s» длиной %d ≥ 16 — strcpy в 16-байтовое поле: NUL теряется, имя не совпадёт с weapon.dat/peds.ide → далее краш по IFP-12/DAT-12", t[0].c_str(), (int)t[0].size()), "");
			if(t.size() > 1 && t[1].size() >= 16) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-31", logical, l.number, t[0], fmt("имя блока «%s» длиной %d ≥ 16 — strcpy в 16-байтовое поле +0x10", t[1].c_str(), (int)t[1].size()), "");
			in = true; gname = t[0]; hdrLine = l.number; got = 0;
			count = (t.size() > 3 && IsIntToken(t[3])) ? ti(t[3]) : -1;
			continue;
		}
		if(t[0] == "end"){
			if(count == 0 && got == 0) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-36", logical, hdrLine, gname, fmt("группа «%s»: count 0 и сразу end — new(0), пустая группа", gname.c_str()), "");
			else if(count > 0 && got < count) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-34", logical, hdrLine, gname, fmt("группа «%s»: %d строк анимаций при count %d — пустые слоты, позиционные AnimId сдвигаются", gname.c_str(), got, count), "");
			in = false;
			continue;
		}
		if(t[0].size() >= 32) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-32", logical, l.number, gname, fmt("имя анимации «%.40s…» длиной %d ≥ 32 — переполнение 32-байтного буфера стека", t[0].c_str(), (int)t[0].size()), "");
		got++;
		if(count == 0 && got == 1) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-36", logical, l.number, gname, fmt("группа «%s»: count 0, но есть строка анимации — AddAnimToAssocDefinition читает указатель из new(0) (мусор кучи) и strcpy через него → краш", gname.c_str()), "");
		else if(count > 0 && got == count + 1) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-33", logical, l.number, gname, fmt("группа «%s»: строк анимаций больше объявленного count %d — AddAnimToAssocDefinition 0x4D3C80 ищет пустой слот без границы → запись за массивом указателей → порча кучи/краш", gname.c_str(), count), "");
		// a header-looking line inside an open group = missing end
		if(t.size() >= 4 && IsIntToken(t[3]) && !IsIntToken(t[0]))
			ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-37", logical, l.number, gname, fmt("строка «%s» похожа на заголовок группы, но группа «%s» не закрыта «end» — она съедается как имя анимации", l.norm.c_str(), gname.c_str()), "");
	}
	if(in) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-37", logical, hdrLine, gname, fmt("группа «%s» не закрыта «end» до конца файла", gname.c_str()), "");
}

// -------------------------------------------------------------- peds.ide ---

static void checkPedsIde(Context &ctx, const std::vector<std::string> &statNames)
{
	GameData &gd = *ctx.gd;
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.type != OT_PEDS) continue;
		const std::vector<std::string> &t = o.tok;
		// t: 0 id 1 model 2 txd 3 pedType 4 stat 5 animGroup 6 carsCanDrive 7 flags 8 animFile 9 radio1 10 radio2 11 voiceType 12 voice1 13 voice2
		// PDD-45 lengths (frame 0x128: slots animFile +0x24[16] modelName +0x34[24] voiceType +0x4C[20] statName +0x60[24] animGroup +0x78[24] pedType +0x90[24] txd +0xA8[24] voice1 +0xC0[60] voice2 +0xFC[60])
		struct { int idx; const char *what; int slot; int crash; } F[] = {
			{ 1, "modelName", 24, 260 }, { 2, "txd", 24, 144 }, { 3, "pedType", 24, 168 }, { 4, "statName", 24, 216 },
			{ 5, "animGroup", 24, 192 }, { 8, "animFile", 16, 276 }, { 11, "voiceType", 20, 236 }, { 12, "voice1", 60, 120 }, { 13, "voice2", 60, 60 },
		};
		for(size_t k = 0; k < sizeof(F) / sizeof(F[0]); k++){
			if((int)t.size() <= F[k].idx) continue;
			int len = (int)t[(size_t)F[k].idx].size();
			if(len >= F[k].crash) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-45", o.file, o.line, o.name, fmt("%s длиной %d ≥ %d — sscanf %%s в LoadPedObject 0x5B7420 достаёт до адреса возврата → краш при загрузке IDE", F[k].what, len, F[k].crash), "", o.id);
			else if(len >= F[k].slot) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-45", o.file, o.line, o.name, fmt("%s длиной %d ≥ %d — переполняет свой слот на стеке, затирает соседние поля строки", F[k].what, len, F[k].slot), "", o.id);
		}
		if(t.size() > 3 && findPedType(t[3]) == 32)
			ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-40", o.file, o.line, o.name, fmt("pedType «%s» не из 32 имён (регистр важен) — FindPedType → 32: GetPedFlag(32) = 0, ms_apPedTypes[32] читается за концом массива (маски отношений из мусора кучи)", t[3].c_str()), "", o.id);
		if(t.size() > 4 && !statNames.empty() && std::find(statNames.begin(), statNames.end(), t[4]) == statNames.end())
			ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-41", o.file, o.line, o.name, fmt("statName «%s» нет среди первых 43 строк pedstats.dat (регистр важен) — GetPedStatType → 16 (STAT_SENSIBLE_GUY)", t[4].c_str()), "", o.id);
		for(int r = 9; r <= 10; r++)
			if((int)t.size() > r && IsIntToken(t[(size_t)r])){ long v = tl(t[(size_t)r]); if(v < -1 || v > 254) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-44", o.file, o.line, o.name, fmt("radio%d = %ld вне −1..254 — хранится как u8 после +1", r - 8, v), "", o.id); }
		if(t.size() > 11){
			static const char *AT[6] = { "PED_TYPE_GEN", "PED_TYPE_EMG", "PED_TYPE_PLAYER", "PED_TYPE_GANG", "PED_TYPE_GFD", "PED_TYPE_SPC" };
			int at = -1;
			for(int k = 0; k < 6; k++) if(t[11] == AT[k]) at = k;
			if(at < 0) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-42", o.file, o.line, o.name, fmt("voiceType «%s» не PED_TYPE_GEN/EMG/PLAYER/GANG/GFD/SPC — GetAudioPedType 0x4E3C60 → −1: оба голоса −1, пед немой", t[11].c_str()), "", o.id);
			else if(at < 5){
				const char **tab = at == 0 ? VOICE_GEN : at == 1 ? VOICE_EMG : at == 2 ? VOICE_PLY : at == 3 ? VOICE_GNG : VOICE_GFD;
				int n = at == 0 ? 209 : at == 1 ? 46 : at == 2 ? 20 : at == 3 ? 52 : 18;
				for(int v = 12; v <= 13; v++)
					if((int)t.size() > v && !inTable(tab, n, t[(size_t)v]))
						ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-43", o.file, o.line, o.name, fmt("voice%d «%s» нет в таблице %s (%d имён, регистр важен) — GetVoice 0x4E3CD0 → −1, пед немой (в ванили так у WMYSGRD, BMYMIB, BMYPIMP)", v - 11, t[(size_t)v].c_str(), AT[at], n), "", o.id);
			}
		}
	}
}

// ------------------------------------------------------ decision makers ---

static void checkDecision(Context &ctx)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/decision/PedEvent.txt";
	std::vector<std::string> raw;
	std::string phys = DataFilePath(gd, logical);
	std::set<int> events;
	if(!readTextLines(phys, raw)){ missing(ctx, "PDD-47", logical, "CDecisionMakerTypesFileLoader::LoadEventIndices 0x5BB9F0"); }
	else{
		ctx.rep->countFile(CAT_PEDDATA);
		int n = 0;
		for(size_t i = 0; i < raw.size(); i++){
			std::string s = raw[i];
			if(!s.empty() && s.back() == '\r') s.pop_back();
			if(s.empty()) continue;	// only lines whose first byte is 0 / '\n' are skipped
			if(s.size() > 255) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-50", logical, (int)i + 1, "", "строка длиннее 255 символов — ReadLine(0x100) режет её, хвост читается как ложная запись", "");
			std::vector<std::string> t; SplitTokens(s, t);
			bool hasNum = t.size() >= 2 && (isdigit((unsigned char)t[1][0]) || ((t[1][0] == '-' || t[1][0] == '+') && t[1].size() > 1 && isdigit((unsigned char)t[1][1])));
			if(t.empty() || !hasNum){
				ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-50", logical, (int)i + 1, t.empty() ? "" : t[0], fmt("строка «%.60s» без номера события (пробельная строка или комментарий тоже) — sscanf(\"%%s %%d\") не заполняет ev: m_EventIndices[мусор со стека] = n → дикая запись → краш", s.c_str()),
				        "PedEvent.txt: только строки «EVENT_NAME число» и полностью пустые строки; комментариев формат не знает.");
				n++;
				continue;
			}
			if(t[0].size() >= 256) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-50", logical, (int)i + 1, t[0], fmt("имя события длиной %d ≥ 256 — переполнение буфера стека", (int)t[0].size()), "");
			long ev = tl(t[1]);
			if(ev < 0 || ev > 95) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-49", logical, (int)i + 1, t[0], fmt("номер события %ld вне 0..95 — 0x5BBA85 mov [ebx+edx*4],esi в m_EventIndices[96] без проверки: отрицательный → внутрь m_DecisionMakers, ≥ 96 → в default-DM, большой → AV", ev), "");
			else events.insert((int)ev);
			n++;
			if(n == 42) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-48", logical, (int)i + 1, t[0], "42-е событие — CDecisionMaker вмещает 41 CDecision (0x99C = 41×0x3C): загрузчик .ped пишет по dm+n*0x3C в следующий DM того же объекта", "");
		}
	}
	// the 15 default files
	static const char *DEF[] = { "RANDOM.ped", "m_norm.ped", "m_plyr.ped", "RANDOM.grp", "MISSION.grp", "GangMbr.ped", "Cop.ped", "R_Norm.ped", "R_Tough.ped", "R_Weak.ped", "Fireman.ped", "m_empty.ped", "Indoors.ped", "RANDOM2.grp", nullptr };
	for(int k = 0; DEF[k]; k++){
		std::string lg = std::string("data/decision/allowed/") + DEF[k];
		std::string ph = DataFilePath(gd, lg);
		std::vector<std::string> ls;
		if(!readTextLines(ph, ls)){ ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-51", lg, -1, "", fmt("%s не найден — LoadDecisionMaker 0x6076B0 вернёт false, этот decision maker пустой: педы этого типа не реагируют на события", lg.c_str()), ""); continue; }
		ctx.rep->countFile(CAT_PEDDATA);
		for(size_t i = 1; i < ls.size(); i++){	// first line skipped by the engine
			std::string s = ls[i];
			if(!s.empty() && s.back() == '\r') s.pop_back();
			if(trim(s).empty()) continue;
			if(s.size() > 511) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-52", lg, (int)i + 1, "", "строка длиннее 511 символов — ReadLine(0x200) режет её на две записи", "");
			// 80 comma-separated numbers
			std::vector<std::string> parts;
			{ std::string cur; for(size_t c = 0; c < s.size(); c++){ if(s[c] == ','){ parts.push_back(trim(cur)); cur.clear(); } else cur += s[c]; } parts.push_back(trim(cur)); }
			int nums = 0;
			for(size_t c = 0; c < parts.size(); c++){ if(IsNumToken(parts[c])) nums++; else break; }
			if(nums != 80) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-52", lg, (int)i + 1, "", fmt("%d чисел через запятую вместо 80 — один sscanf на 80 конверсий 0x6079B4: остальное = мусор со стека (случайные task id / вероятности)", nums), "");
			if(nums >= 1){
				long ev = tl(parts[0]);
				if(ev < 0 || ev > 95) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-53", lg, (int)i + 1, "", fmt("id события %ld вне 0..95 — 0x607A1D читает m_EventIndices[%ld] за массивом, ×0x3C → CDecision::Set куда угодно → краш", ev, ev), "");
				else if(!events.empty() && !events.count((int)ev)) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-54", lg, (int)i + 1, "", fmt("событие %ld не перечислено в PedEvent.txt — индекс 0: перезаписывается решение первого события (ваниль: 19 таких строк)", ev), "");
			}
		}
	}
}

// ---------------------------------------------------------------- surfaces ---

static void checkSurfaces(Context &ctx)
{
	GameData &gd = *ctx.gd;
	// surface.dat
	{
		const std::string logical = "data/surface.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) missing(ctx, "PDD-55", logical, "SurfaceInfos_c::LoadAdhesiveLimits 0x55D0E0");
		else{
			ctx.rep->countFile(CAT_PEDDATA);
			int row = 0;
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == ';') continue;
				const std::vector<std::string> &t = l.tok;
				if(row == 6) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-56", logical, l.number, t[0], "7-я строка данных — матрица 6×6 (RUBBER HARD ROAD LOOSE SAND WET) полна, запись уходит в surface-записи this+0x90..", "");
				int vals = (int)t.size() - 1;
				if(row < 6 && vals > row + 1) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-56", logical, l.number, t[0], fmt("строка %d: %d значений, читается ровно %d (лишние игнорируются)", row, vals, row + 1), "");
				if(row < 6 && vals < row + 1) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-57", logical, l.number, t[0], fmt("строка %d: %d значений вместо %d — недостающие = 0.0 (нулевое трение для пары групп)", row, vals, row + 1), "");
				if(l.raw.size() > 0 && t.size() >= 1 && l.raw.find_first_of(" \t", 0) == std::string::npos) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-56", logical, l.number, t[0], "строка с именем без пробела после него — значения не разбираются", "");
				row++;
			}
			if(row < 6) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-57", logical, -1, "", fmt("%d строк вместо 6 — остальные лимиты сцепления 0", row), "");
		}
	}
	std::set<std::string> infoNames, audNames;
	// surfinfo.dat
	{
		const std::string logical = "data/surfinfo.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) missing(ctx, "PDD-55", logical, "SurfaceInfos_c::LoadSurfaceInfos 0x55EB90");
		else{
			ctx.rep->countFile(CAT_PEDDATA);
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				const std::vector<std::string> &t = l.tok;
				const std::string &name = t[0];
				struct { int idx; int cap; int crash; const char *what; } S[] = { {0, 64, 64, "name"}, {1, 32, 192, "adhesionGroup"}, {4, 32, 128, "skidmark"}, {5, 32, 96, "frictionEffect"}, {35, 32, 160, "bulletFx"} };
				for(size_t k = 0; k < 5; k++){
					if((int)t.size() <= S[k].idx) continue;
					int len = (int)t[(size_t)S[k].idx].size();
					if(len >= S[k].crash) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-62", logical, l.number, name, fmt("%s длиной %d ≥ %d — sscanf %%s достаёт до адреса возврата 0x55EB90 → краш", S[k].what, len, S[k].crash), "");
					else if(len >= S[k].cap) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-62", logical, l.number, name, fmt("%s длиной %d ≥ %d — переполняет буфер стека", S[k].what, len, S[k].cap), "");
				}
				if(!inTable(SURFACE_NAMES, 179, name)) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-58", logical, l.number, name, fmt("поверхность «%s» не из 179 встроенных (регистр важен) — GetSurfaceIdFromName 0x55D220 → 0: свойства строки достаются DEFAULT", name.c_str()), "Своих поверхностей нет — список имён зашит в exe (DEFAULT … RAILTRACK).");
				else infoNames.insert(name);
				if(t.size() < 36) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-59", logical, l.number, name, fmt("%d полей вместо 36 — недостающие = мусор со стека", (int)t.size()), "");
				else{
					bool bad = false;
					for(size_t k = 6; k < 35; k++) if(!IsIntToken(t[k])){ bad = true; ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-59", logical, l.number, name, fmt("поле %d «%s» не целое — sscanf останавливается", (int)k + 1, t[k].c_str()), ""); break; }
					if(!bad && (!IsNumToken(t[2]) || !IsNumToken(t[3]))) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-59", logical, l.number, name, "tyreGrip/wetGrip не число", "");
					static const char *ADH[] = { "RUBBER", "HARD", "ROAD", "LOOSE", "SAND", "WET", nullptr };
					static const char *SKD[] = { "DEFAULT", "SANDY", "MUDDY", nullptr };
					static const char *FRC[] = { "NONE", "SPARKS", nullptr };
					static const char *BUL[] = { "NONE", "SPARKS", "SAND", "WOOD", "DUST", nullptr };
					auto inl = [](const char **L, const std::string &s){ for(int k = 0; L[k]; k++) if(s == L[k]) return true; return false; };
					if(!inl(ADH, t[1])) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-60", logical, l.number, name, fmt("adhesionGroup «%s» не RUBBER/HARD/ROAD/LOOSE/SAND/WET — биты не меняются", t[1].c_str()), "");
					if(!inl(SKD, t[4])) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-60", logical, l.number, name, fmt("skidmark «%s» не DEFAULT/SANDY/MUDDY", t[4].c_str()), "");
					if(!inl(FRC, t[5])) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-60", logical, l.number, name, fmt("frictionEffect «%s» не NONE/SPARKS", t[5].c_str()), "");
					if(!inl(BUL, t[35])) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-60", logical, l.number, name, fmt("bulletFx «%s» не NONE/SPARKS/SAND/WOOD/DUST", t[35].c_str()), "");
					if(IsNumToken(t[2])){ float g = tf(t[2]) * 10; if(g < 0 || g > 255) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-61", logical, l.number, name, fmt("tyreGrip×10 = %.0f вне байта после ftol", g), ""); }
				}
			}
		}
	}
	// surfaud.dat
	{
		const std::string logical = "data/surfaud.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) missing(ctx, "PDD-55", logical, "SurfaceInfos_c::LoadSurfaceAudioInfos 0x55F2B0");
		else{
			ctx.rep->countFile(CAT_PEDDATA);
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				const std::vector<std::string> &t = l.tok;
				if(t[0].size() >= 64) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-62", logical, l.number, t[0], fmt("имя длиной %d ≥ 64 — sscanf %%s в local_40[64] 0x55F2B0 → краш", (int)t[0].size()), "");
				if(!inTable(SURFACE_NAMES, 179, t[0])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-58", logical, l.number, t[0], fmt("поверхность «%s» не из 179 встроенных (регистр важен) — id 0: флаги звука достаются DEFAULT", t[0].c_str()), "");
				else audNames.insert(t[0]);
				if(t.size() < 10) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-59", logical, l.number, t[0], fmt("%d полей вместо 10 (имя + 9 флагов CON GRS SND GRV WOD WTR MTL LGS TIL)", (int)t.size()), "");
			}
		}
	}
	if(!infoNames.empty() || !audNames.empty()){
		std::string mi, ma; int ni = 0, na = 0;
		for(int k = 0; k < 179; k++){
			if(!infoNames.empty() && !infoNames.count(SURFACE_NAMES[k])){ if(ni < 8) mi += (ni ? ", " : "") + std::string(SURFACE_NAMES[k]); ni++; }
			if(!audNames.empty() && !audNames.count(SURFACE_NAMES[k])){ if(na < 8) ma += (na ? ", " : "") + std::string(SURFACE_NAMES[k]); na++; }
		}
		if(ni) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-63", "data/surfinfo.dat", -1, "", fmt("%d встроенных поверхностей без строки в surfinfo.dat (%s%s) — свойства нули", ni, mi.c_str(), ni > 8 ? ", …" : ""), "");
		if(na) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-63", "data/surfaud.dat", -1, "", fmt("%d встроенных поверхностей без строки в surfaud.dat (%s%s) — без класса звука", na, ma.c_str(), na > 8 ? ", …" : ""), "");
	}
}

// -------------------------------------------------------------- shopping.dat ---

static const char *PRICE_SECTIONS[11] = { "None", "CarMods", "CarPaintJobs", "Furniture", "Clothes", "Haircuts", "Tattoos", "Gifts", "Food", "Weapons", "Property" };
static int priceSection(const std::string &s) { for(int i = 0; i < 11; i++) if(ieq(s, PRICE_SECTIONS[i])) return i; return -1; }

static int weaponTypeCI(const std::string &n)
{
	static const char *W[49] = { "UNARMED","BRASSKNUCKLE","GOLFCLUB","NIGHTSTICK","KNIFE","BASEBALLBAT","SHOVEL","POOLCUE","KATANA","CHAINSAW","DILDO1","DILDO2","VIBE1","VIBE2","FLOWERS","CANE","GRENADE","TEARGAS","MOLOTOV","ROCKET","ROCKET_HS","FREEFALL_BOMB","PISTOL","PISTOL_SILENCED","DESERT_EAGLE","SHOTGUN","SAWNOFF","SPAS12","MICRO_UZI","MP5","AK47","M4","TEC9","COUNTRYRIFLE","SNIPERRIFLE","RLAUNCHER","RLAUNCHER_HS","FTHROWER","MINIGUN","SATCHEL_CHARGE","DETONATOR","SPRAYCAN","EXTINGUISHER","CAMERA","NIGHTVISION","INFRARED","PARACHUTE","","ARMOUR" };
	for(int i = 0; i < 49; i++) if(i != 47 && ieq(n, W[i])) return i;
	return -1;
}

struct ShopItem { std::string name; int line; int section; };

static void checkShopping(Context &ctx, const std::set<std::string> &playerDff, const std::set<std::string> &playerTxd)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/shopping.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){ missing(ctx, "PDD-64", logical, "CShopping::LoadStats 0x49B6A0 → FindSection"); return; }
	ctx.rep->countFile(CAT_PEDDATA);
	// tokens for shopping: strtok(" \t,") — commas already spaces in norm; stop at a '#' token
	auto toks = [](const DataLine &l, std::vector<std::string> &t){ t.clear(); for(size_t k = 0; k < l.tok.size(); k++){ if(l.tok[k][0] == '#') break; t.push_back(l.tok[k]); } };
	std::vector<std::string> stack;		// section names by depth
	bool inPrices = false, sawPrices = false, inShops = false;
	int curPrice = -1;			// price subsection index while in prices
	std::string curPriceName;
	std::map<std::string, std::vector<ShopItem>> pricesBySection;	// lower name → items
	std::map<std::string, int> priceCount;
	int totalItems = 0;
	// shops
	std::string curShop; int shopItems = 0; int shopType = -2; std::string shopTypeName; bool shopHasType = false;
	static const char *STATS[] = { "fat", "respect", "sexy", "health", "stamina", "calories", "-", nullptr };
	auto statOk = [&](const std::string &s){ for(int k = 0; STATS[k]; k++) if(s == STATS[k]) return true; return false; };
	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '#') continue;
		std::vector<std::string> t; toks(l, t);
		if(t.empty()) continue;
		if(l.norm.compare(0, 7, "section") == 0){
			std::string name = t.size() > 1 ? t[1] : "";
			stack.push_back(name);
			if(stack.size() == 1){
				if(ieq(name, "prices")){ inPrices = true; sawPrices = true; }
				else if(ieq(name, "shops")) inShops = true;
			}else if(stack.size() == 2 && inPrices){
				curPrice = priceSection(name); curPriceName = name;
				if(curPrice < 0) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-73", logical, l.number, name, fmt("подсекция prices «%s» не из 11 имён (None CarMods CarPaintJobs Furniture Clothes Haircuts Tattoos Gifts Food Weapons Property) — LoadStats ведёт её как model-секцию, LoadPrices по ней не найдёт индекс", name.c_str()), "");
				if(curPrice == 2) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-78", logical, l.number, name, "подсекция CarPaintJobs: ключи не присваиваются (0x49B72B), GetKey возвращает константу 2", "");
			}else if(stack.size() == 2 && inShops){
				curShop = name; shopItems = 0; shopType = -2; shopHasType = false;
				if(name.size() >= 24) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-79", logical, l.number, name, fmt("имя магазина «%s» длиной %d ≥ 24 — не совпадёт с FindSection из скрипта (ms_shopLoaded[24]): магазин пустой", name.c_str(), (int)name.size()), "");
			}
			continue;
		}
		if(l.norm.compare(0, 3, "end") == 0){
			if(stack.empty()){ ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-81", logical, l.number, "", "«end» без открытой секции — FindSection теряет глубину: следующие строки относятся не к той секции", ""); continue; }
			stack.pop_back();
			if(stack.empty()){ inPrices = false; inShops = false; }
			if(stack.size() == 1){ curPrice = -1; curPriceName.clear(); curShop.clear(); }
			continue;
		}
		if(inPrices && stack.size() == 2){
			// item line
			totalItems++;
			priceCount[lower(curPriceName)]++;
			int cnt = priceCount[lower(curPriceName)];
			if(totalItems == 561) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-66", logical, l.number, t[0], "561-й предмет под prices — ms_keys[560] 0xA97D90 полон, дальше затираются ms_priceModifiers/ms_bHasBought/ms_statModifiers", "");
			if(cnt == 301) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-67", logical, l.number, t[0], fmt("301-й предмет в подсекции %s — ms_prices[300] = ms_numBuyableItems: LoadPrices затирает счётчик", curPriceName.c_str()), "");
			int need = (curPrice == 4 || curPrice == 5 || curPrice == 6) ? 9 : curPrice == 9 ? 8 : 7;
			int needStats = (curPrice == 4 || curPrice == 5 || curPrice == 6) ? 8 : curPrice == 9 ? 7 : 6;
			if((int)t.size() < need){
				ctx.add((int)t.size() < needStats ? SEV_FATAL : SEV_ERROR, CAT_PEDDATA, "PDD-77", logical, l.number, t[0], fmt("%d токенов вместо %d для подсекции %s — strtok вернёт NULL → atol(NULL)/GetUppercaseKey(NULL) → краш (%s)", (int)t.size(), need, curPriceName.c_str(), (int)t.size() < needStats ? "уже в LoadStats при старте" : "в LoadPrices при открытии магазина"), "");
				continue;
			}
			const std::string &name = t[0];
			pricesBySection[lower(curPriceName)].push_back({ name, l.number, curPrice });
			if(t[1].size() > 7) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-72", logical, l.number, name, fmt("nametag «%s» длиннее 7 — strncpy(…,8) без NUL: GXT-метка читается в следующее поле", t[1].c_str()), "");
			size_t st = curPrice == 4 || curPrice == 5 || curPrice == 6 ? 4 : curPrice == 9 ? 3 : 2;
			for(size_t k = st; k < st + 4 && k < t.size(); k += 2){
				if(!statOk(t[k])) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-75", logical, l.number, name, fmt("стат «%s» не fat/respect/sexy/health/stamina/calories/- (точно) — −1, без изменения", t[k].c_str()), "");
				if(k + 1 < t.size()){ if(!IsIntToken(t[k + 1]) && t[k + 1] != "-") ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-76", logical, l.number, name, fmt("изменение стата «%s» не целое (atol → 0)", t[k + 1].c_str()), ""); else { long v = tl(t[k + 1]); if(v < -128 || v > 127) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-76", logical, l.number, name, fmt("изменение стата %ld вне −128..127 (int8)", v), ""); } }
			}
			if(st + 4 < t.size() && !IsIntToken(t[st + 4])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-76", logical, l.number, name, fmt("цена «%s» не целое", t[st + 4].c_str()), "");
			if(curPrice == 9){
				if(weaponTypeCI(name) < 0) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-74", logical, l.number, name, fmt("«%s» не имя типа оружия — FindWeaponType → 0 UNARMED: покупка ничего не даёт, все неизвестные делят ключ 0", name.c_str()), "");
				if(!IsIntToken(t[2])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-76", logical, l.number, name, fmt("ammo «%s» не целое", t[2].c_str()), "");
			}else if(curPrice == 4 || curPrice == 5){
				if(!IsIntToken(t[3])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-76", logical, l.number, name, fmt("type «%s» не целое", t[3].c_str()), "");
				// player.img presence of modelname / texturename: CLO-03 / CLO-04 (check_clothes.cpp) report it as a crash
			}else if(curPrice == 6){
				if(t[2] != "-" && !IsIntToken(t[2])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-76", logical, l.number, name, fmt("type1 «%s» не целое и не «-»", t[2].c_str()), "");
			}else if(curPrice != 2){
				if(gd.findObjByName(name) == nullptr) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-73", logical, l.number, name, fmt("«%s» (подсекция %s) не имя модели ни в одном IDE — GetModelInfo → ключ −1; все нерезолвленные делят ключ −1", name.c_str(), curPriceName.c_str()), "");
			}
			continue;
		}
		if(inShops && stack.size() == 2){
			if(t[0] == "type"){
				if(t.size() < 2){ ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-70", logical, l.number, curShop, "«type» без имени — strcpy(NULL) → краш при открытии магазина", ""); continue; }
				if(t[1].size() >= 32) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-79", logical, l.number, curShop, fmt("токен type «%s» длиной %d ≥ 32 — strcpy в local_20[32] → краш", t[1].c_str(), (int)t[1].size()), "");
				shopType = priceSection(t[1]); shopTypeName = t[1]; shopHasType = true;
				if(shopType < 0) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-68", logical, l.number, curShop, fmt("type «%s» не из 11 имён price-секций — GetPriceSectionFromName → −1 → ms_sectionNames[−1] = 0xFFFF → _stricmp по адресу 0xFFFF в GetAnimationBlock → краш при открытии магазина", t[1].c_str()), "");
				else if(!pricesBySection.count(lower(t[1])) && !priceCount.count(lower(t[1]))){
					ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-69", logical, l.number, curShop, fmt("type «%s»: такой подсекции нет под prices — FindSection доходит до end блока prices и LoadPrices читает следующие строки (section shops …) как предметы: strtok(NULL) → atol(NULL) → краш при открытии магазина", t[1].c_str()), "Добавь под prices пустую «section <имя>» … «end» или поправь type.");
				}
			}else if(t[0] == "item"){
				shopItems++;
				if(shopItems == 301) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-67", logical, l.number, curShop, "301-й item в магазине — ms_shopContents[300] = ms_priceSectionLoaded", "");
				if(t.size() < 2) continue;
				if(!shopHasType) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-70", logical, l.number, curShop, fmt("item «%s» до строки type — ключ считается по предыдущей секции (или None): цена никогда не совпадёт", t[1].c_str()), "");
				else if(shopType >= 0){
					auto it = pricesBySection.find(lower(shopTypeName));
					bool found = false;
					if(it != pricesBySection.end()) for(size_t k = 0; k < it->second.size(); k++) if(ieq(it->second[k].name, t[1])) found = true;
					if(!found) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-71", logical, l.number, curShop, fmt("item «%s» нет в подсекции prices/%s — предмет без цены", t[1].c_str(), shopTypeName.c_str()), "");
				}
			}
			continue;
		}
	}
	if(!stack.empty()) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-81", logical, -1, "", fmt("%d секций не закрыто «end» до конца файла (последняя: %s)", (int)stack.size(), stack.back().c_str()), "");
	if(!sawPrices) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-65", logical, -1, "", "нет верхней «section prices» — LoadStats ничего не загружает, все магазины пусты", "");
}

// ------------------------------------------------------ statdisp / ar_stats ---

static void checkStats(Context &ctx)
{
	GameData &gd = *ctx.gd;
	{
		const std::string logical = "data/statdisp.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) missing(ctx, "PDD-82", logical, "CStats::LoadStatUpdateConditions 0x559860");
		else{
			ctx.rep->countFile(CAT_PEDDATA);
			int n = 0;
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				const std::vector<std::string> &t = l.tok;
				n++;
				if(n == 129) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-83", logical, l.number, "", "129-я строка — StatMessage[128] (0xB78200..0xB78A00) полон: затирается LastMissionPassedName и далее статистика сейва", "");
				if(t.size() < 5){ ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-86", logical, l.number, "", fmt("%d токенов вместо 5 (statId statName condition value gxt)", (int)t.size()), ""); continue; }
				if(!IsIntToken(t[0])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-86", logical, l.number, t[1], fmt("statId «%s» не целое", t[0].c_str()), "");
				else{ long id = tl(t[0]); if(!((id >= 0 && id <= 81) || (id >= 120 && id <= 343))) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-86", logical, l.number, t[1], fmt("statId %ld не в 0..81 / 120..343 — GetStatValue 0x558E40 читает вне таблиц статов", id), ""); }
				if(t[1].size() >= 80) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-87", logical, l.number, t[1], fmt("statName длиной %d ≥ 80 — до EBP/ret → краш", (int)t[1].size()), "");
				if(t[2].size() >= 92) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-87", logical, l.number, t[1], fmt("condition длиной %d ≥ 92 — краш", (int)t[2].size()), "");
				else if(t[2].size() >= 12) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-87", logical, l.number, t[1], fmt("condition длиной %d ≥ 12 — переполняет слот (уходит в statName, который не используется)", (int)t[2].size()), "");
				else if(t[2] != "lessthan" && t[2] != "morethan") ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-85", logical, l.number, t[1], fmt("condition «%s» не lessthan/morethan (точно) — считается lessthan", t[2].c_str()), "");
				if(!IsNumToken(t[3])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-86", logical, l.number, t[1], fmt("value «%s» не число", t[3].c_str()), "");
				if(t[4].size() > 7) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-84", logical, l.number, t[1], fmt("GXT-метка «%s» длиннее 7 — strcpy в 8 байт entry+8 затирает statId следующей записи", t[4].c_str()), "");
			}
		}
	}
	{
		const std::string logical = "data/ar_stats.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) missing(ctx, "PDD-82", logical, "CStats::LoadActionReactionStats 0x5599B0");
		else{
			ctx.rep->countFile(CAT_PEDDATA);
			std::set<long> ids;
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				const std::vector<std::string> &t = l.tok;
				if(t.size() < 3){ ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-88", logical, l.number, "", fmt("%d токенов вместо 3 (id name value)", (int)t.size()), ""); continue; }
				if(!IsIntToken(t[0])) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-88", logical, l.number, t[1], fmt("id «%s» не целое", t[0].c_str()), "");
				else{ long id = tl(t[0]); if(id < 0 || id > 59) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-88", logical, l.number, t[1], fmt("id %ld вне 0..59 — StatReactionValue[id] (0xB78F10..0xB79000): 60..123 попадают в StatTypesInt, больше — куда угодно", id), ""); else ids.insert(id); }
				if(t[1].size() >= 80) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-89", logical, l.number, t[1], fmt("имя длиной %d ≥ 80 — 0x5599B0: name E+0xC, ret E+0x5C → краш", (int)t[1].size()), "");
			}
			int miss = 0; for(long k = 0; k <= 58; k++) if(!ids.count(k)) miss++;
			if(miss) ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-90", logical, -1, "", fmt("%d id из 0..58 отсутствуют — значение реакции 0, действие не обновляет стат", miss), "");
		}
	}
}


// ------------------------------------------------ III / VC (re3 / reVC): pedstats.dat, pedgrp.dat, peds в IDE ---
// CPedType::FindPedType — 21 имя (иначе NUM_PEDTYPES: флаги читаются за массивом), CPedStats::GetPedStatType ищет имя
// среди NUM_PEDSTATS (III 35, VC 40) статов, загруженных по порядку из pedstats.dat (иначе NUM_PEDSTATS →
// ms_apPedStats[N] за массивом → краш при спавне); группа анимаций — по имени из ms_aAnimAssocDefinitions
// (иначе NUM_ANIM_ASSOC_GROUPS → чтение за таблицей); pedgrp.dat: NUMPEDGROUPS × NUMMODELSPERPEDGROUP (III 31×8, VC 67×16),
// группа засчитывается только при полном наборе имён, неизвестное имя оставляет в слоте предыдущее значение.
static const char *LEGACY_PED_TYPES[] = { "PLAYER1","PLAYER2","PLAYER3","PLAYER4","CIVMALE","CIVFEMALE","COP","GANG1","GANG2","GANG3","GANG4","GANG5","GANG6","GANG7","GANG8","GANG9","EMERGENCY","FIREMAN","CRIMINAL","SPECIAL","PROSTITUTE" };
static const char *III_ANIM_GROUPS[] = { "man","player","playerrocket","player1armed","player2armed","shuffle","oldman","gang1","gang2","fatman","oldfatman","woman","shopping","busywoman","sexywoman","oldwoman","fatwoman","panicchunky","playerback","playerleft","playerright","rocketback","rocketleft","rocketright" };
static const char *VC_ANIM_GROUPS[] = { "man","van","coach","bikes","bikev","bikeh","biked","unarmed","screwdrv","knife","baseball","golfclub","chainsaw","python","colt45","shotgun","buddy","tec","uzi","rifle","m60","sniper","grenade","flame","medic","sunbathe","playidles","riot","strip","lance","player","playerrocket","player1armed","player2armed","playercsaw","shuffle","oldman","gang1","gang2","fatman","oldfatman","jogger","woman","shopping","busywoman","sexywoman","fatwoman","oldwoman","jogwoman","panicchunky","skate","playerback","playerleft","playerright","rocketback","rocketleft","rocketright","csawback","csawleft","csawright" };

static void checkPedDataLegacy(Context &ctx)
{
	GameData &gd = *ctx.gd;
	bool vc = gd.isVC();
	const int numStats = vc ? 40 : 35, numGroups = vc ? 67 : 31, perGroup = vc ? 16 : 8;
	// pedstats.dat
	std::vector<std::string> statNames;
	{
		const std::string logical = "data/pedstats.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) missing(ctx, "PDD-06", logical, "CPedStats::LoadPedStats");
		else{
			ctx.rep->countFile(CAT_PEDDATA);
			int n = 0;
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				const std::vector<std::string> &t = l.tok;
				n++;
				if(n == numStats + 1) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-07", logical, l.number, t[0], fmt("%d-я строка данных — ms_apPedStats[NUM_PEDSTATS = %d] заполняется по порядку, лишняя пишется за массивом", n, numStats), "");
				if(t[0].size() >= 24) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-09", logical, l.number, t[0], fmt("имя длиной %d ≥ 24 — strcpy в m_name[24] затирает соседние поля", (int)t[0].size()), "");
				if(n <= numStats) statNames.push_back(t[0]);
				if(t.size() < 10) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-12", logical, l.number, t[0], fmt("%d полей вместо 10 (name fleeDist headingChangeRate fear temper lawfulness sexiness attack defend shootingRate) — недостающие = мусор", (int)t.size()), "");
			}
			if(n < numStats) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-10", logical, -1, "", fmt("%d строк вместо %d — статы адресуются по позиции, недостающие остаются нулями", n, numStats), "");
		}
	}
	// pedgrp.dat
	{
		const std::string logical = "data/pedgrp.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) missing(ctx, "PDD-13", logical, "CPopulation::LoadPedGroups");
		else{
			ctx.rep->countFile(CAT_PEDDATA);
			int groups = 0;
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				const std::vector<std::string> &t = l.tok;
				int accepted = 0;
				for(size_t k = 0; k < t.size() && accepted < perGroup; k++){
					if(t[k][0] == '#') break;
					accepted++;
					const ObjDef *d = gd.findObjByName(t[k]);
					if(d == nullptr) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-16", logical, l.number, t[k], fmt("«%s» не модель ни в одном IDE — GetModelInfo не трогает слот, в группе остаётся id предыдущей загрузки (0 = %s) → спавн не-педа → краш", t[k].c_str(), vc ? "null" : "player"), "");
					else if(d->type != OT_PEDS) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-17", logical, l.number, t[k], fmt("«%s» — модель типа %s, не педа: CPopulation::AddPed прочитает CPedModelInfo из чужой записи → краш при спавне", t[k].c_str(), objTypeName(d->type)), "");
				}
				if(accepted == 0) continue;
				if(accepted < perGroup){ ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-18", logical, l.number, "", fmt("%d имён вместо %d — группа не засчитывается (nextPedGroup не растёт), следующая строка ложится поверх неё: все группы ниже сдвигаются", accepted, perGroup), ""); continue; }
				groups++;
				if(groups == numGroups + 1) ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-14", logical, l.number, "", fmt("%d-я группа — ms_pPedGroups[NUMPEDGROUPS = %d] переполняется", groups, numGroups), "");
			}
			if(groups < numGroups) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-15", logical, -1, "", fmt("%d групп вместо %d — поздние группы населения пусты (модель 0)", groups, numGroups), "");
		}
	}
	// peds в IDE
	std::set<std::string> ifpBlocks;
	for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].kind == EK_IFP && gd.isWinner((int)i)) ifpBlocks.insert(gd.entries[i].base);
	const char **groupsTab = vc ? VC_ANIM_GROUPS : III_ANIM_GROUPS;
	int nGroups = vc ? (int)(sizeof(VC_ANIM_GROUPS) / sizeof(*VC_ANIM_GROUPS)) : (int)(sizeof(III_ANIM_GROUPS) / sizeof(*III_ANIM_GROUPS));
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.type != OT_PEDS) continue;
		const std::vector<std::string> &t = o.tok;
		// III: id model txd pedType stat animGroup carsCanDrive; VC: + animFile radio1 radio2
		if(t.size() > 3 && !inTable(LEGACY_PED_TYPES, (int)(sizeof(LEGACY_PED_TYPES) / sizeof(*LEGACY_PED_TYPES)), t[3]))
			ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-40", o.file, o.line, o.name, fmt("pedType «%s» не из 21 имени (регистр важен) — FindPedType → NUM_PEDTYPES, флаги отношений читаются за массивом ms_apPedType", t[3].c_str()), "", o.id);
		if(t.size() > 4 && !statNames.empty() && std::find(statNames.begin(), statNames.end(), t[4]) == statNames.end())
			ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-41", o.file, o.line, o.name, fmt("statName «%s» нет среди %d строк pedstats.dat (регистр важен) — GetPedStatType → NUM_PEDSTATS, ms_apPedStats[%d] за массивом → краш при спавне педа", t[4].c_str(), numStats, numStats), "", o.id);
		if(t.size() > 5 && !inTable(groupsTab, nGroups, t[5]))
			ctx.add(SEV_FATAL, CAT_PEDDATA, "PDD-91", o.file, o.line, o.name, fmt("animGroup «%s» не из %d встроенных (регистр важен) — m_animGroup = NUM_ANIM_ASSOC_GROUPS, CAnimManager читает ассоциации за таблицей → краш при спавне", t[5].c_str(), nGroups), "", o.id);
		if(t.size() > 6 && !IsHexToken(t[6]))
			ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-44", o.file, o.line, o.name, fmt("carsCanDrive «%s» — читается как hex (%%x)", t[6].c_str()), "", o.id);
		if(vc){
			if(t.size() < 10) ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-44", o.file, o.line, o.name, fmt("peds: %d полей вместо 10 (id model txd pedType stat animGroup cars animFile radio1 radio2) — хвост со стека", (int)t.size()), "", o.id);
			if(t.size() > 7 && lower(t[7]) != "null" && !ifpBlocks.count(lower(t[7])))
				ctx.add(SEV_INFO, CAT_PEDDATA, "PDD-92", o.file, o.line, o.name, fmt("animFile «%s» — такого IFP нет в IMG: GetAnimationBlockIndex → −1, блок анимаций не запрашивается (ваниль: «man» у обычных педов)", t[7].c_str()), "", o.id);
			if(t.size() > 7 && t[7].size() >= 16) ctx.add(SEV_ERROR, CAT_PEDDATA, "PDD-45", o.file, o.line, o.name, "animFile ≥ 16 символов — переполняет буфер LoadPedObject", "", o.id);
		}else if(t.size() < 7)
			ctx.add(SEV_WARN, CAT_PEDDATA, "PDD-44", o.file, o.line, o.name, fmt("peds: %d полей вместо 7 (id model txd pedType stat animGroup carsCanDrive) — хвост со стека", (int)t.size()), "", o.id);
	}
}

void CheckPedData(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()){ ctx.prog->set("Педы: pedstats / pedgrp / IDE"); checkPedDataLegacy(ctx); return; }
	ctx.prog->set("Педы: ped.dat / pedstats / pedgrp / popcycle / decision / surfaces / shopping / stats");
	checkPedDat(ctx);
	std::vector<std::string> statNames;
	checkPedStats(ctx, statNames);
	checkPedGrp(ctx);
	checkPopcycle(ctx);
	checkAnimGrpGrammar(ctx);
	checkPedsIde(ctx, statNames);
	if(ctx.cancelled()) return;
	checkDecision(ctx);
	checkSurfaces(ctx);
	// player.img directory for the clothes cross-references
	std::set<std::string> playerDff, playerTxd;
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(e.img < 0 || e.img >= (int)gd.archives.size()) continue;
		if(!ieq(basename(gd.archives[(size_t)e.img].logical), "player.img")) continue;
		if(e.kind == EK_DFF) playerDff.insert(e.base);
		else if(e.kind == EK_TXD) playerTxd.insert(e.base);
	}
	checkShopping(ctx, playerDff, playerTxd);
	checkStats(ctx);
}

} // namespace gc
