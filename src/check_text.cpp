// gtacheck — GXT / main.scm / script.img / IMG list / stream.ini / UI TXDs / fonts.dat
// (GXT-nn, SCM-nn, IMG-nn, UI-nn).  Source: E:\RE\addon_check\text_script_img_rules.md + GTACHECK_RULES.md §8.
#include "gtacheck.h"

#include <stdlib.h>
#include <ctype.h>
#include <algorithm>

namespace gc {

// CKeyGen::GetUppercaseKey 0x53CF30: table CRC-32, init 0xFFFFFFFF, no final xor, ASCII toupper
static uint32_t jamcrcUpper(const std::string &s)
{
	static uint32_t table[256]; static bool init = false;
	if(!init){ for(uint32_t i = 0; i < 256; i++){ uint32_t c = i; for(int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
	uint32_t crc = 0xFFFFFFFFu;
	for(size_t i = 0; i < s.size(); i++){ unsigned char c = (unsigned char)s[i]; if(c >= 'a' && c <= 'z') c = (unsigned char)(c - 32); crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8); }
	return crc;
}

static std::string imgWhere(const GameData &gd, const Entry &e) { return (e.img >= 0 ? basename(gd.archives[(size_t)e.img].logical) + "/" : "") + e.name; }

// ------------------------------------------------------------------- GXT ---

struct GxtTable { std::string name; uint32_t offset; bool nameNoNul; };

// Parses TKEY/TDAT starting at `pos` (after an optional TABL); returns false on a structural hang.
static void checkGxtKeys(Context &ctx, const std::string &where, const std::string &table, const std::vector<uint8_t> &d, size_t pos, std::set<uint32_t> *hashes, bool isMain)
{
	Buf b(d.data(), d.size());
	b.seek(pos);
	bool gotKey = false, gotDat = false;
	std::vector<std::pair<uint32_t,uint32_t>> keys;	// offset, hash
	size_t datStart = 0, datSize = 0;
	int guard = 0;
	while(!(gotKey && gotDat) && guard++ < 100000){
		if(!b.can(8)){
			ctx.add(SEV_FATAL, CAT_GXT, isMain ? "GXT-03" : "GXT-09", where, -1, table, fmt("таблица %s: конец файла раньше, чем найдены TKEY и TDAT с size > 0 — цикл 0x6A0255 выходит только по обоим флагам, Read на EOF оставляет старый заголовок → бесконечный цикл / повторный new", table.c_str()), "");
			return;
		}
		const uint8_t *tag = b.ptr(4);
		uint32_t size = b.u32();
		std::string t((const char *)tag, 4);
		if(size == 0) continue;
		if(t == "TKEY"){
			if(size % 8) ctx.add(SEV_ERROR, CAT_GXT, "GXT-04", where, -1, table, fmt("TKEY size %u не кратен 8 — CKeyArray::Load читает все %u байт в блок new((size>>3)*8): переполнение кучи на %u байт", size, size, size % 8), "");
			uint32_t n = size >> 3;
			if(n > 32767) ctx.add(SEV_FATAL, CAT_GXT, "GXT-04", where, -1, table, fmt("TKEY: %u записей > 32767 — BinarySearch 0x69F570 держит lo/hi в int16: поиск не работает/уходит за массив", n), "");
			if(!b.can(size)){ ctx.add(SEV_FATAL, CAT_GXT, "GXT-03", where, -1, table, fmt("TKEY size %u выходит за конец файла — Read неполный, цикл не завершится", size), ""); return; }
			for(uint32_t i = 0; i < n; i++){ uint32_t off = b.u32(), h = b.u32(); keys.push_back(std::make_pair(off, h)); }
			b.skip(size - n * 8);	// the engine consumes exactly n*8 and misaligns on the remainder; we skip it to keep checking
			gotKey = true;
		}else if(t == "TDAT"){
			if(!b.can(size)){ ctx.add(SEV_FATAL, CAT_GXT, "GXT-03", where, -1, table, fmt("TDAT size %u выходит за конец файла", size), ""); return; }
			datStart = b.pos; datSize = size;
			b.skip(size);
			gotDat = true;
		}else if(t == "TABL"){
			if(!isMain) ctx.add(SEV_WARN, CAT_GXT, "GXT-09", where, -1, table, "TABL внутри mission-таблицы — пропускается как неизвестный чанк", "");
			if(!b.skip(size)) return;
		}else{
			if(size >= 0x10000) ctx.add(SEV_FATAL, CAT_GXT, "GXT-03b", where, -1, table, fmt("чанк «%.4s» size %u ≥ 65536 — цикл пропуска с 16-битным счётчиком (movzx ecx,bp) никогда не догоняет size → зависание", t.c_str(), size), "");
			if(!b.skip(size)){ ctx.add(SEV_FATAL, CAT_GXT, "GXT-03", where, -1, table, fmt("чанк «%.4s» size %u выходит за конец файла", t.c_str(), size), ""); return; }
		}
	}
	// key checks
	bool sorted = true; int dups = 0;
	for(size_t i = 1; i < keys.size(); i++){ if(keys[i].second < keys[i - 1].second) sorted = false; else if(keys[i].second == keys[i - 1].second) dups++; }
	if(!sorted) ctx.add(SEV_ERROR, CAT_GXT, "GXT-05", where, -1, table, fmt("таблица %s: TKEY не отсортирован по возрастанию беззнакового хэша — BinarySearch не найдёт часть ключей (пустой текст)", table.c_str()), "");
	if(dups) ctx.add(SEV_WARN, CAT_GXT, "GXT-05", where, -1, table, fmt("таблица %s: %d повторяющихся хэшей — бисекция вернёт случайную из копий", table.c_str(), dups), "");
	int badOff = 0, noNul = 0;
	for(size_t i = 0; i < keys.size(); i++){
		uint32_t off = keys[i].first;
		if(off >= datSize){ badOff++; continue; }
		bool nul = false; for(size_t k = datStart + off; k < datStart + datSize; k++) if(d[k] == 0){ nul = true; break; }
		if(!nul) noNul++;
		if(hashes) hashes->insert(keys[i].second);
	}
	if(badOff) ctx.add(SEV_FATAL, CAT_GXT, "GXT-06", where, -1, table, fmt("таблица %s: %d ключей со смещением ≥ размера TDAT (%u) — CKeyArray::Update прибавляет базу без проверки, Get отдаёт указатель за блоком кучи → краш при отрисовке", table.c_str(), badOff, (unsigned)datSize), "");
	if(noNul) ctx.add(SEV_FATAL, CAT_GXT, "GXT-06", where, -1, table, fmt("таблица %s: %d строк без NUL внутри TDAT — CFont читает за блоком кучи", table.c_str(), noNul), "");
}

static void checkGxt(Context &ctx, const std::string &logical, bool selected, std::set<uint32_t> *mainHashes, std::vector<GxtTable> *tables)
{
	GameData &gd = *ctx.gd;
	std::string phys = DataFilePath(gd, logical);
	std::vector<uint8_t> d;
	if(!readFile(phys, d)){
		if(selected) ctx.add(SEV_FATAL, CAT_GXT, "GXT-01", logical, -1, "", fmt("%s не найден — CText::Load 0x6A01A0 не проверяет OpenFile: fread(NULL) → _lock_str → краш при старте (файл выбранного языка)", logical.c_str()), "");
		else ctx.add(SEV_WARN, CAT_GXT, "GXT-01", logical, -1, "", fmt("%s не найден — краш при выборе этого языка в меню", logical.c_str()), "");
		return;
	}
	ctx.rep->countFile(CAT_GXT);
	if(d.size() < 12){ ctx.add(SEV_FATAL, CAT_GXT, "GXT-03", logical, -1, "", "файл короче 12 байт", ""); return; }
	if(!(d[0] == 4 && d[1] == 0 && d[2] == 8 && d[3] == 0))
		ctx.add(SEV_WARN, CAT_GXT, "GXT-02", logical, -1, "", fmt("первые 4 байта %02X %02X %02X %02X ≠ 04 00 08 00 — два Read(2) съедают их не сравнивая; формат III/VC (TABL/TKEY с байта 0) → обход чанков со смещения 4 рассинхронизирован", d[0], d[1], d[2], d[3]), "");
	// TABL at 4?
	std::set<uint32_t> hashes;
	std::vector<GxtTable> tabl;
	size_t pos = 4;
	if(d.size() >= 12 && memcmp(d.data() + 4, "TABL", 4) == 0){
		Buf b(d.data(), d.size()); b.seek(8);
		uint32_t size = b.u32();
		if(size % 12) ctx.add(SEV_WARN, CAT_GXT, "GXT-04", logical, -1, "", fmt("TABL size %u не кратен 12 — 0x69F670 съедает только (size/12)*12 = %u байт и читает следующий заголовок с позиции %u; если там не TKEY/TDAT — зависание (см. GXT-03)", size, (size / 12) * 12, 12 + (size / 12) * 12), "");
		uint32_t n = size / 12;
		if(n > 200) ctx.add(SEV_FATAL, CAT_GXT, "GXT-07", logical, -1, "", fmt("TABL: %u записей > 200 — CMissionTextOffsets 0x960 байт: запись 200 затирает счётчик +0xA8C, дальше — буферы конвертации", n), "");
		for(uint32_t i = 0; i < n && b.can(12); i++){
			GxtTable t;
			const uint8_t *np = b.ptr(8);
			t.nameNoNul = memchr(np, 0, 8) == nullptr;
			t.name.assign((const char *)np, t.nameNoNul ? 8 : strlen((const char *)np));
			t.offset = b.u32();
			tabl.push_back(t);
			if(t.nameNoNul) ctx.add(SEV_ERROR, CAT_GXT, "GXT-08", logical, (int)i, t.name, fmt("имя таблицы «%s» занимает все 8 байт — strlen 0x69FC3A уходит в offset: LoadMissionText никогда не совпадёт, все ключи таблицы = \"\"", t.name.c_str()), "");
		}
		pos = 8 + 4 + (size_t)n * 12;
	}
	checkGxtKeys(ctx, logical, "MAIN", d, pos, &hashes, true);
	if(!hashes.empty() && !hashes.count(0xEF128EC3u)) ctx.add(SEV_WARN, CAT_GXT, "GXT-13", logical, -1, "CDERROR", "в MAIN нет ключа CDERROR (0xEF128EC3) — пустой текст ошибки диска", "");
	// mission tables
	for(size_t i = 0; i < tabl.size(); i++){
		const GxtTable &t = tabl[i];
		if(t.name == "MAIN") continue;
		if((size_t)t.offset + 8 > d.size()){ ctx.add(SEV_FATAL, CAT_GXT, "GXT-09", logical, (int)i, t.name, fmt("таблица «%s»: смещение %u за концом файла — Seek/Read не проверены, цикл TKEY/TDAT не завершится", t.name.c_str(), t.offset), ""); continue; }
		checkGxtKeys(ctx, logical, t.name, d, (size_t)t.offset + 8, nullptr, false);
	}
	if(mainHashes) *mainHashes = hashes;
	if(selected) ctx.gd->gxtMainHashes = hashes;
	if(tables) *tables = tabl;
}

// ------------------------------------------------------------------- SCM ---

static void checkScm(Context &ctx, const std::vector<GxtTable> &tables, const std::set<uint32_t> &mainHashes)
{
	GameData &gd = *ctx.gd;
	const std::string logical = "data/script/main.scm";
	std::string phys = DataFilePath(gd, logical);
	std::vector<uint8_t> d;
	if(!readFile(phys, d)){ ctx.add(SEV_FATAL, CAT_SCM, "SCM-01", logical, -1, "", "data/script/main.scm не найден — CTheScripts::Init 0x468D50 не проверяет OpenFile: fread(NULL) → краш при старте", ""); return; }
	ctx.rep->countFile(CAT_SCM);
	Buf b(d.data(), d.size());
	auto seg = [&](size_t at, uint32_t &target) -> bool {
		if(at + 7 > d.size() || at + 7 > 200000) return false;
		b.seek(at + 3); target = b.u32(); return b.ok;
	};
	if(d.size() < 7 || !(d[0] == 2 && d[1] == 0 && d[2] == 1)) ctx.add(SEV_ERROR, CAT_SCM, "SCM-02", logical, 0, "", "байты 0..2 ≠ 02 00 01 (GOTO) — исполнение начинается с байта 0 без проверки: главный скрипт начнётся с мусорного опкода", "");
	uint32_t seg1 = 0, seg2 = 0, seg3 = 0, seg4 = 0;
	bool ok1 = seg(0, seg1);
	if(!ok1 || seg1 >= 200000){ ctx.add(SEV_FATAL, CAT_SCM, "SCM-02", logical, 0, "", fmt("seg0 target %u ≥ 200000 — заголовки читаются за ScriptSpace", seg1), ""); return; }
	ctx.add(SEV_INFO, CAT_SCM, "SCM-09", logical, 0, "", fmt("размер глобальных переменных (seg0 target) = %u байт — должен совпадать с сейвом", seg1), "");
	bool ok2 = seg(seg1, seg2);
	if(!ok2 || seg2 >= 200000){ ctx.add(SEV_FATAL, CAT_SCM, "SCM-02", logical, (int)seg1, "", fmt("seg1 target %u ≥ 200000", seg2), ""); return; }
	bool ok3 = seg(seg2, seg3);
	if(!ok3 || seg3 >= 200000){ ctx.add(SEV_FATAL, CAT_SCM, "SCM-02", logical, (int)seg2, "", fmt("seg2 target %u ≥ 200000", seg3), ""); return; }
	bool ok4 = seg(seg3, seg4);
	if(!ok4 || seg4 >= 200000){ ctx.add(SEV_FATAL, CAT_SCM, "SCM-02", logical, (int)seg3, "", fmt("seg3 target %u ≥ 200000", seg4), ""); return; }
	if(!(seg1 < seg2 && seg2 < seg3 && seg3 < seg4)) ctx.add(SEV_WARN, CAT_SCM, "SCM-02", logical, -1, "", fmt("targets не по возрастанию (%u, %u, %u, %u) — движку всё равно, но структура нестандартная", seg1, seg2, seg3, seg4), "");
	// seg1 objects
	b.seek(seg1 + 8);
	uint32_t numObjs = b.u16();
	if(seg1 + 12 + (uint64_t)numObjs * 24 > 200000) ctx.add(SEV_FATAL, CAT_SCM, "SCM-02", logical, (int)seg1, "", fmt("таблица объектов (%u × 24) выходит за 200000 байт ScriptSpace", numObjs), "");
	if(numObjs > 395) ctx.add(SEV_FATAL, CAT_SCM, "SCM-03", logical, (int)seg1, "", fmt("%u имён объектов > 395 — UsedObjectArray (0xA44B70 × 0x1C) переполняется в EntitiesWaitingForScriptBrain", numObjs), "");
	for(uint32_t i = 0; i < numObjs; i++){
		const uint8_t *np = b.ptr(24);
		if(!np) break;
		bool noNul = memchr(np, 0, 24) == nullptr;
		std::string name((const char *)np, noNul ? 24 : strlen((const char *)np));
		if(i == 0) continue;	// slot 0 is empty and skipped
		if(noNul){ ctx.add(SEV_ERROR, CAT_SCM, "SCM-03", logical, (int)seg1, name, fmt("имя объекта №%u без NUL в 24 байтах — хэш идёт до случайного NUL → индекс −1 → SCM-04", i), ""); continue; }
		if(name.empty()) continue;
		if(gd.findObjByName(name) == nullptr)
			ctx.add(SEV_FATAL, CAT_SCM, "SCM-04", logical, (int)seg1, name, fmt("объект №%u «%s» не модель ни в одном IDE — UpdateObjectIndices оставляет −1, при использовании RequestModel(−1) 0x4087E0 без границ → запись в ms_aInfoForModel[−1]", i, name.c_str()), "", -1);
	}
	// seg2 missions
	b.seek(seg2 + 8);
	uint32_t mainSize = b.u32(), largestMission = b.u32();
	int numMissions = (int16_t)b.u16(); b.u16();
	uint32_t largestLocals = b.u32();
	if(!b.ok) return;
	if(mainSize > 200000) ctx.add(SEV_FATAL, CAT_SCM, "SCM-05", logical, (int)seg2, "", fmt("MainScriptSize %u > 200000 — только первые 200000 байт копируются в ScriptSpace", mainSize), "");
	if(largestMission > 69000) ctx.add(SEV_FATAL, CAT_SCM, "SCM-05", logical, (int)seg2, "", fmt("LargestMissionScriptSize %u > 69000 — миссия читается ровно Read(69000): хвост обрезан, исполняется мусор", largestMission), "");
	if(numMissions > 200 || numMissions < 0) ctx.add(SEV_FATAL, CAT_SCM, "SCM-05", logical, (int)seg2, "", fmt("%d миссий > 200 — MultiScriptArray[200] переполняется в bAlreadyRunningAMissionScript", numMissions), "");
	if(seg2 + 24 + (uint64_t)std::max(numMissions, 0) * 4 > 200000) ctx.add(SEV_FATAL, CAT_SCM, "SCM-02", logical, (int)seg2, "", "таблица смещений миссий выходит за 200000 байт", "");
	if(largestLocals > 1024) ctx.add(SEV_WARN, CAT_SCM, "SCM-08", logical, (int)seg2, "", fmt("largest number of mission locals %u > 1024 — LocalVariablesForCurrentMission[1024] лежит прямо перед ScriptSpace: локалы ≥ 1024 алиасят глобалы", largestLocals), "");
	std::vector<uint32_t> missOff;
	for(int i = 0; i < numMissions && i < 200; i++){ uint32_t o = b.u32(); if(!b.ok) break; missOff.push_back(o); }
	{
		std::vector<uint32_t> sorted = missOff; sorted.push_back((uint32_t)d.size()); std::sort(sorted.begin(), sorted.end());
		for(size_t i = 0; i < missOff.size(); i++){
			if(missOff[i] >= d.size()){ ctx.add(SEV_FATAL, CAT_SCM, "SCM-05", logical, (int)seg2, "", fmt("миссия %d: смещение %u ≥ размера файла (%u)", (int)i, missOff[i], (unsigned)d.size()), ""); continue; }
			auto nx = std::upper_bound(sorted.begin(), sorted.end(), missOff[i]);
			if(nx != sorted.end()){ uint32_t len = *nx - missOff[i]; if(len > 69000) ctx.add(SEV_FATAL, CAT_SCM, "SCM-05", logical, (int)seg2, "", fmt("миссия %d: %u байт до следующего смещения > 69000 — обрезается при Read(69000)", (int)i, len), ""); }
		}
	}
	// seg3 streamed scripts vs script.img
	b.seek(seg3 + 8);
	uint32_t largestExt = b.u32(); b.u32();
	(void)largestExt;
	std::vector<std::pair<std::string,uint32_t>> seg3rows;
	// rows until seg4 (the GOTO at seg3 lands at seg4)
	size_t rows = seg4 > seg3 + 16 ? (seg4 - seg3 - 16) / 28 : 0;
	for(size_t i = 0; i < rows; i++){
		const uint8_t *np = b.ptr(20); if(!np) break;
		std::string name((const char *)np, memchr(np, 0, 20) ? strlen((const char *)np) : 20);
		b.u32(); uint32_t size = b.u32();
		if(!b.ok) break;
		seg3rows.push_back(std::make_pair(name, size));
	}
	int scmEntries = 0;
	std::map<std::string, int> scmByBase;
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(e.kind != EK_SCM || !gd.isWinner((int)i)) continue;
		scmEntries++;
		scmByBase[e.base] = (int)i;
		if(e.base.size() >= 20) ctx.add(SEV_ERROR, CAT_SCM, "SCM-06", imgWhere(gd, e), e.dirIndex, e.name, fmt("имя скрипта «%s» ≥ 20 символов — strcpy переполняет name[20] в size, и 20-байтовое поле seg3 без NUL никогда не совпадёт", e.base.c_str()), "");
	}
	if(scmEntries > 82) ctx.add(SEV_FATAL, CAT_SCM, "SCM-07", "script.img", -1, "", fmt("%d записей .scm во всех IMG > 82 — RegisterScript пишет 83-ю поверх CScriptResourceManager (0xA485A8)", scmEntries), "");
	if(scmEntries > (int)seg3rows.size()) ctx.add(SEV_ERROR, CAT_SCM, "SCM-06", logical, (int)seg3, "", fmt("в IMG %d .scm, а в seg3 только %d строк — ReadStreamedScriptData идёт по счётчику IMG и читает строки за таблицей (байты кода) как имена", scmEntries, (int)seg3rows.size()), "");
	for(size_t i = 0; i < seg3rows.size() && (int)i < scmEntries; i++){
		const std::string &nm = seg3rows[i].first;
		auto it = scmByBase.find(lower(nm));
		if(it == scmByBase.end()){ ctx.add(SEV_ERROR, CAT_SCM, "SCM-06", logical, (int)seg3, nm, fmt("seg3 строка %d «%s»: нет такого .scm в script.img (stricmp) — j = −1: size/index пишутся в EntitiesWaitingForScriptBrain[149]/[146]", (int)i, nm.c_str()), ""); continue; }
		const Entry &e = gd.entries[(size_t)it->second];
		uint32_t real = e.sizeSectors * 2048;
		if(seg3rows[i].second < real - 2047 - 0 && seg3rows[i].second + 2048 < real)
			ctx.add(SEV_ERROR, CAT_SCM, "SCM-06", logical, (int)seg3, nm, fmt("seg3 «%s»: size %u меньше записи в IMG (%u секторов) — LoadStreamedScript читает только size байт, скрипт обрезан", nm.c_str(), seg3rows[i].second, e.sizeSectors), "");
	}
	// heuristic opcode scans: 054C LOAD_MISSION_TEXT (GXT-10), 0390 LOAD_TXD_DICTIONARY (SCM-10) — opcode, type 9, 8-byte label
	{
		std::set<std::string> tnames; for(size_t i = 0; i < tables.size(); i++) tnames.insert(tables[i].name);
		std::set<std::string> seenT, seenX;
		for(size_t i = 0; i + 11 <= d.size(); i++){
			if(d[i + 2] != 9) continue;
			uint16_t op = (uint16_t)(d[i] | (d[i + 1] << 8));
			if(op != 0x054C && op != 0x0390) continue;
			std::string label((const char *)d.data() + i + 3, 8);
			size_t z = label.find('\0'); if(z != std::string::npos) label.resize(z);
			bool clean = !label.empty();
			for(size_t k = 0; k < label.size(); k++){ unsigned char c = (unsigned char)label[k]; if(!(isalnum(c) || c == '_')) clean = false; }
			if(!clean) continue;
			if(label.size() > 7) ctx.add(SEV_WARN, CAT_GXT, "GXT-12", logical, (int)i, label, fmt("метка «%s» длиной 8 без терминатора — ReadTextLabelFromScript копирует 8 байт без NUL", label.c_str()), "");
			if(op == 0x054C){
				if(seenT.count(label)) continue; seenT.insert(label);
				if(!tables.empty() && !tnames.count(label)) ctx.add(SEV_WARN, CAT_GXT, "GXT-10", logical, (int)i, label, fmt("LOAD_MISSION_TEXT «%s»: такой таблицы нет в TABL (точно, та же длина) — LoadMissionText выходит без загрузки, все ключи таблицы = \"\"", label.c_str()), "");
			}else{
				if(seenX.count(label)) continue; seenX.insert(label);
				std::string lg = "models/txd/" + label + ".txd";
				if(!fileExists(DataFilePath(gd, lg))) ctx.add(SEV_FATAL, CAT_SCM, "SCM-10", logical, (int)i, label, fmt("LOAD_TXD_DICTIONARY «%s»: нет %s — CTxdStore::LoadTxd 0x7320B0 крутит do{RwStreamOpen}while(!stream) → зависание при выполнении опкода", label.c_str(), lg.c_str()), "");
			}
		}
	}
	(void)mainHashes;
}

// --------------------------------------------------------------- IMG list ---

static void checkImgList(Context &ctx)
{
	GameData &gd = *ctx.gd;
	// IMG lines in default.dat + gta.dat (IMG-01..03)
	int imgLines = 0; bool seenIpl = false;
	for(const char *df : { "data/default.dat", "data/gta.dat" }){
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, df, lines)) continue;
		for(size_t i = 0; i < lines.size(); i++){
			const DataLine &l = lines[i];
			if(l.norm.empty() || l.norm[0] == '#' || l.tok.size() < 2) continue;
			std::string kw = l.tok[0];
			if(kw == "IPL") seenIpl = true;
			if(kw != "IMG") continue;
			std::string arg = l.norm.substr(3); arg = trim(arg);
			if(ieq(normSlashes(arg), "models/gta_int.img")) continue;
			imgLines++;
			if(arg.size() >= 40) ctx.add(SEV_ERROR, CAT_IMG, "IMG-03", df, l.number, arg, fmt("путь IMG длиной %d ≥ 40 — AddImageToList копирует в name[0x28] без границы: затирается имя следующего слота", (int)arg.size()), "");
			if(seenIpl) ctx.add(SEV_WARN, CAT_IMG, "IMG-02", df, l.number, arg, "строка IMG после первой строки IPL — LoadCdDirectory() вызывается один раз на первой IPL: архив открыт, но каталог не сканирован (его записи недоступны)", "");
		}
	}
	if(imgLines > 5) ctx.add(SEV_FATAL, CAT_IMG, "IMG-01", "data/gta.dat", -1, "", fmt("%d строк IMG (без gta_int.img) > 5 — ms_files на 8 слотов: 2 встроенных + строки + player.img; при переполнении AddImageToList возвращает 0 и PLAYER.IMG получает id 0 → одежда читает смещения player.img из gta3.img → краш", imgLines), "");
	// per-entry (IMG-05, 07, 08, 11) and buffer (IMG-09)
	uint32_t maxStream = 0; std::string maxName;
	int rrr = 0, txd = 0, col = 0, ifp = 0, ipl = 0;
	static const char *EXT[] = { "dff", "txd", "col", "ipl", "dat", "ifp", "rrr", "scm", nullptr };
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(e.img < 0) continue;
		const Archive &a = gd.archives[(size_t)e.img];
		if(ieq(basename(a.logical), "player.img")) continue;
		std::string where = imgWhere(gd, e);
		if(e.sizeSectors > maxStream){ maxStream = e.sizeSectors; maxName = e.name; }
		size_t dot = e.name.find('.');
		if(dot != std::string::npos && dot > 20) ctx.add(SEV_WARN, CAT_IMG, "IMG-05", where, e.dirIndex, e.name, fmt("первая точка на позиции %d > 20 — запись молча пропущена", (int)dot), "");
		if(dot != std::string::npos){
			std::string ext = lower(e.name.substr(dot + 1, 3));
			bool known = false; for(int k = 0; EXT[k]; k++) if(ext == EXT[k]) known = true;
			if(!known) ctx.add(SEV_WARN, CAT_IMG, "IMG-05", where, e.dirIndex, e.name, fmt("расширение «%s» (3 символа после первой точки) не DFF/TXD/COL/IPL/DAT/IFP/RRR/SCM — запись молча пропущена", ext.c_str()), "");
		}
		if(a.ver2){
			if(e.sizeSectors == 0 && e.sizeArchive == 0) ctx.add(SEV_WARN, CAT_IMG, "IMG-07", where, e.dirIndex, e.name, "streaming size и size-in-archive = 0 — m_nCdSize = 0: модель «не на диске»", "");
			if(e.sizeArchive != 0 && e.sizeArchive != e.sizeSectors) ctx.add(SEV_WARN, CAT_IMG, "IMG-08", where, e.dirIndex, e.name, fmt("байты 6..7 (size in archive) = %u ≠ 0 — переопределяют размер чтения (0x5B6465), буфер стриминга считается по байтам 4..5", e.sizeArchive), "");
		}
		if(!gd.isWinner((int)i)) continue;
		switch(e.kind){
		case EK_RRR: rrr++; if(e.base.compare(0, 6, "carrec") != 0) ctx.add(SEV_WARN, CAT_IMG, "IMG-11", where, e.dirIndex, e.name, fmt("файл .rrr «%s» не carrecN — номер записи 850 (недостижима/дубликат)", e.name.c_str()), ""); break;
		case EK_TXD: txd++; break;
		case EK_COL: col++; break;
		case EK_IFP: ifp++; break;
		case EK_IPL: ipl++; break;
		case EK_DAT: if(e.base.compare(0, 5, "nodes") != 0) ctx.add(SEV_ERROR, CAT_IMG, "IMG-11", where, e.dirIndex, e.name, fmt("файл .dat «%s» не nodesNN — sscanf(name+5) проваливается: остаётся id предыдущей записи → регистрация в чужой слот", e.name.c_str()), ""); break;
		default: break;
		}
	}
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(e.img < 0 || !gd.archives[(size_t)e.img].ver2 || ieq(basename(gd.archives[(size_t)e.img].logical), "player.img")) continue;
		if(e.sizeArchive != 0 && e.sizeArchive > maxStream)
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-08", imgWhere(gd, e), e.dirIndex, e.name, fmt("size in archive %u больше максимума streaming size по всем записям (%u) — CdStreamRead переполняет ms_pStreamingBuffer", e.sizeArchive, maxStream), "");
	}
	uint32_t even = maxStream + (maxStream & 1);
	ctx.add(SEV_INFO, CAT_IMG, "IMG-09", "IMG", -1, maxName, fmt("буфер стриминга = наибольшая запись %u секторов («%s») → %u секторов (%u КБ), по %u на канал", maxStream, maxName.c_str(), even, even * 2, even / 2), "");
	if(rrr > 475) ctx.add(SEV_FATAL, CAT_IMG, "IMG-11", "IMG", -1, "", fmt("%d .rrr > 475 — StreamingArray переполняется в NumPlayBackFiles 0x97F630", rrr), "");
	if(txd > 5000) ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IMG-11", "IMG", -1, "", fmt("%d TXD > 5000 (пул CTxdStore)", txd), "");
	if(col > 255) ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IMG-11", "IMG", -1, "", fmt("%d COL > 255 (пул CColStore)", col), "");
	if(ifp > 180) ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IMG-11", "IMG", -1, "", fmt("%d IFP > 180 (ms_aAnimBlocks)", ifp), "");
	// stream.ini (IMG-12)
	{
		const std::string logical = "stream.ini";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)) ctx.add(SEV_FATAL, CAT_IMG, "IMG-12", logical, -1, "", "stream.ini не найден — CStreaming::ReadIniFile 0x5BCCD0 не проверяет OpenFile → fgets(NULL) → краш при старте", "");
		else{
			ctx.rep->countFile(CAT_IMG);
			static const char *VK[] = { "memory", "devkit_memory", "vehicles", "pe_lightchangerate", "pe_lightingbasecap", "pe_lightingbasemult", "pe_leftx", "pe_rightx", "pe_topy", "pe_bottomy", "pe_bRadiosity", "def_brightness_pal", nullptr };
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				bool valueKey = false; for(int k = 0; VK[k]; k++) if(ieq(l.tok[0], VK[k])) valueKey = true;
				if(valueKey && l.tok.size() < 2) ctx.add(SEV_FATAL, CAT_IMG, "IMG-12", logical, l.number, l.tok[0], fmt("ключ «%s» без значения — второй strtok = NULL → atol/atof(NULL) → краш при старте", l.tok[0].c_str()), "");
				if(ieq(l.tok[0], "memory") || ieq(l.tok[0], "devkit_memory") || ieq(l.tok[0], "vehicles")) ctx.add(SEV_INFO, CAT_IMG, "IMG-12", logical, l.number, l.tok[0], fmt("«%s» переопределяется в Init2 (50 МБ / 22 машины) — значение из stream.ini не действует", l.tok[0].c_str()), "");
			}
		}
	}
}

// ------------------------------------------------------------------- UI ---

static void needTextures(Context &ctx, const std::string &logical, const std::vector<std::string> &names, const char *code, const char *func)
{
	GameData &gd = *ctx.gd;
	std::string phys = DataFilePath(gd, logical);
	if(!fileExists(phys)){ ctx.add(SEV_FATAL, CAT_UI, "UI-01", logical, -1, "", fmt("%s не найден — CTxdStore::LoadTxd 0x7320B0 крутит do{RwStreamOpen}while(!stream): зависание (%s)", logical.c_str(), func), ""); return; }
	std::vector<uint8_t> d;
	if(!readFile(phys, d)) return;
	TxdInfo info;
	std::string st = lower(stem(logical));
	// loose UI TXDs are not IMG entries, so the TXD pass never saw them — run the TXD rules here too
	CheckOneTxd(ctx, logical, d, st, &info);
	ctx.rep->countFile(CAT_UI);
	if(info.failed || !info.parsed){ ctx.add(SEV_ERROR, CAT_UI, "UI-02", logical, -1, "", fmt("%s не загружается как TXD (LoadTxd 0x731DD0 → false, никто не проверяет) — словарь NULL, все спрайты рисуются плоскими квадами", logical.c_str()), ""); return; }
	std::string miss; int n = 0;
	for(size_t i = 0; i < names.size(); i++) if(std::find(info.names.begin(), info.names.end(), lower(names[i])) == info.names.end()){ if(n < 10) miss += (n ? ", " : "") + names[i]; n++; }
	if(n) ctx.add(SEV_ERROR, CAT_UI, code, logical, -1, "", fmt("нет %d текстур: %s%s — SetTexture → NULL-растр, спрайт рисуется плоским цветом", n, miss.c_str(), n > 10 ? ", …" : ""), "");
}

static void checkUi(Context &ctx)
{
	GameData &gd = *ctx.gd;
	needTextures(ctx, "models/fonts.txd", { "font2", "font1" }, "UI-02", "CFont::Initialise 0x5BA690");
	needTextures(ctx, "models/pcbtns.txd", { "up", "down", "left", "right" }, "UI-02", "CFont::Initialise 0x5BA7D4");
	{
		std::vector<std::string> hud = { "fist", "siteM16", "siterocket", "radardisc", "radarRingPlane", "SkipIcon",
			"radar_centre", "arrow", "radar_north", "radar_airYard", "radar_ammugun", "radar_barbers", "radar_BIGSMOKE", "radar_boatyard", "radar_burgerShot", "radar_bulldozer",
			"radar_CATALINAPINK", "radar_CESARVIAPANDO", "radar_chicken", "radar_CJ", "radar_CRASH1", "radar_diner", "radar_emmetGun", "radar_enemyAttack", "radar_fire", "radar_girlfriend",
			"radar_hostpitaL", "radar_LocoSyndicate", "radar_MADDOG", "radar_mafiaCasino", "radar_MCSTRAP", "radar_modGarage", "radar_OGLOC", "radar_pizza", "radar_police", "radar_propertyG",
			"radar_propertyR", "radar_race", "radar_RYDER", "radar_saveGame", "radar_school", "radar_qmark", "radar_SWEET", "radar_tattoo", "radar_THETRUTH", "radar_waypoint", "radar_TorenoRanch",
			"radar_triads", "radar_triadsCasino", "radar_tshirt", "radar_WOOZIE", "radar_ZERO", "radar_dateDisco", "radar_dateDrink", "radar_dateFood", "radar_truck", "radar_cash", "radar_flag",
			"radar_gym", "radar_impound", "radar_light", "radar_runway", "radar_gangB", "radar_gangP", "radar_gangY", "radar_gangN", "radar_gangG", "radar_spray" };
		needTextures(ctx, "models/hud.txd", hud, "UI-03", "CHud::Initialise 0x5BA850");
	}
	{
		std::vector<std::string> ls = { "nvidia", "eax", "title_pc_US" };
		for(int i = 0; i <= 14; i++) ls.push_back(fmt("loadsc%d", i));
		needTextures(ctx, "models/txd/loadscs.txd", ls, "UI-06", "CLoadingScreen::LoadSplashes 0x5900B0");
	}
	needTextures(ctx, "models/fronten1.txd", { "arrow", "radio_playback", "radio_krose", "radio_KDST", "radio_bounce", "radio_SFUR", "radio_RLS", "radio_RADIOX", "radio_csr", "radio_kjah", "radio_mastersounds", "radio_WCTR", "radio_TPLAYER" }, "UI-06", "CMenuManager::LoadAllTextures 0x572EC0");
	needTextures(ctx, "models/fronten2.txd", { "back2", "back3", "back4", "back5", "back6", "back7", "back8", "map" }, "UI-06", "CMenuManager::LoadAllTextures");
	needTextures(ctx, "models/fronten3.txd", { "back8_top", "back8_right" }, "UI-06", "CMenuManager::LoadAllTextures");
	needTextures(ctx, "models/fronten_pc.txd", { "mouse", "crosshair" }, "UI-06", "CMenuManager::LoadAllTextures");
	for(const char *f : { "models/particle.txd", "models/grass/plant1.txd", "models/generic/vehicle.txd", "models/effectsPC.txd" })
		if(!fileExists(DataFilePath(gd, f))) ctx.add(SEV_FATAL, CAT_UI, "UI-01", f, -1, "", fmt("%s не найден — CTxdStore::LoadTxd 0x7320B0: do{RwStreamOpen}while(!stream) → зависание при старте", f), "");
	// UI-05 radar tiles
	{
		int miss = 0; std::string first;
		for(int i = 0; i < 144; i++){
			std::string nm = fmt("radar%02d.txd", i);
			if(gd.findEntry(nm) < 0){ if(!miss) first = nm; miss++; }
		}
		if(miss) ctx.add(SEV_WARN, CAT_UI, "UI-05", "IMG", -1, first, fmt("%d из 144 тайлов радара radar00..radar143.txd нет ни в одном IMG (первый: %s) — FindTxdSlot → −1, участок карты пустой", miss, first.c_str()), "");
	}
	// UI-04 fonts.dat
	{
		const std::string logical = "data/fonts.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines)){ ctx.add(SEV_FATAL, CAT_UI, "UI-04", logical, -1, "", "data/fonts.dat не найден — CFont::LoadFontValues 0x7187C0 не проверяет OpenFile → fgets(NULL) → краш", ""); return; }
		ctx.rep->countFile(CAT_UI);
		std::set<int> ids; int curId = -1; int propRows = -1; int propLine = 0;
		std::string pendingKey;
		for(size_t i = 0; i < lines.size(); i++){
			const DataLine &l = lines[i];
			if(l.norm.empty() || l.norm[0] == '#') continue;
			const std::vector<std::string> &t = l.tok;
			if(propRows >= 0){
				int nums = 0; for(size_t k = 0; k < t.size() && k < 8; k++){ if(IsIntToken(t[k])) nums++; else break; }
				if(nums < 8) ctx.add(SEV_ERROR, CAT_UI, "UI-04", logical, l.number, "", fmt("[PROP] строка %d: %d чисел вместо 8 — недостающие ширины = старые значения стека", propRows + 1, nums), "");
				propRows++;
				if(propRows == 26) propRows = -1;
				continue;
			}
			if(!pendingKey.empty()){
				std::string key = pendingKey; pendingKey.clear();
				if(!IsIntToken(t[0])) ctx.add(SEV_ERROR, CAT_UI, "UI-04", logical, l.number, key, fmt("%s: значение «%s» не число", key.c_str(), t[0].c_str()), "");
				else if(key == "[FONT_ID]"){
					curId = (int)strtol(t[0].c_str(), nullptr, 10);
					ids.insert(curId);
					if(curId < 0 || curId > 1) ctx.add(SEV_FATAL, CAT_UI, "UI-04", logical, l.number, key, fmt("[FONT_ID] %d не 0/1 — gFontData[2] (0xC718B0 × 0xD2): id 2 ложится на CFont::m_Color..Sprite 0xC71A54..0xC71B26 → краш при первой отрисовке текста", curId), "");
				}
				continue;
			}
			if(t[0] == "[TOTAL_FONTS]" || t[0] == "[FONT_ID]" || t[0] == "[REPLACEMENT_SPACE_CHAR]" || t[0] == "[UNPROP]"){ pendingKey = t[0]; continue; }
			if(t[0] == "[PROP]"){ propRows = 0; propLine = l.number; continue; }
		}
		if(propRows >= 0) ctx.add(SEV_FATAL, CAT_UI, "UI-04", logical, propLine, "[PROP]", fmt("EOF внутри блока [PROP] (%d строк из 26) — LoadLine NULL → sscanf(NULL) → краш", propRows), "");
		if(!pendingKey.empty()) ctx.add(SEV_FATAL, CAT_UI, "UI-04", logical, -1, pendingKey, fmt("EOF после %s — LoadLine NULL → sscanf(NULL)", pendingKey.c_str()), "");
		for(int id = 0; id <= 1; id++) if(!ids.count(id)) ctx.add(SEV_ERROR, CAT_UI, "UI-04", logical, -1, "", fmt("нет блока [FONT_ID] %d — ширины шрифта %d остаются нулями", id, id), "");
	}
}

void CheckTextScriptImg(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	ctx.prog->set("GXT / main.scm / IMG / UI");
	std::set<uint32_t> mainHashes; std::vector<GxtTable> tables;
	checkGxt(ctx, "text/american.gxt", true, &mainHashes, &tables);
	for(const char *f : { "text/french.gxt", "text/german.gxt", "text/italian.gxt", "text/spanish.gxt" }){
		if(ctx.cancelled()) return;
		checkGxt(ctx, f, false, nullptr, nullptr);
	}
	if(ctx.cancelled()) return;
	checkScm(ctx, tables, mainHashes);
	checkImgList(ctx);
	checkUi(ctx);
}

} // namespace gc
