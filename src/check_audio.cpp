// gtacheck — audio: audio/CONFIG/*.dat, audio/SFX bank headers, audio/streams, vehicles.ide audio ids (AUD-nn).
// Source: E:\RE\addon_check\audio_rules.md + GTACHECK_RULES.md §9 (gta_sa.exe 1.0 US).
#include "gtacheck.h"
#include "tables_audio.h"

#include <stdlib.h>
#include <algorithm>

namespace gc {

static bool fileSize(const std::string &phys, uint64_t &size)
{
	FILE *f = OpenReadUtf8(phys);
	if(!f) return false;
	_fseeki64(f, 0, SEEK_END); size = (uint64_t)_ftelli64(f); fclose(f);
	return true;
}

static bool readAt(FILE *f, uint64_t at, void *out, size_t n)
{
	return _fseeki64(f, (long long)at, SEEK_SET) == 0 && fread(out, 1, n, f) == n;
}

struct BankLkup { int pak; uint32_t offset, size; };
struct TrakLkup { int pak; uint32_t offset, length; };

void CheckAudio(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	ctx.prog->set("Аудио: CONFIG / SFX / streams");
	auto cfg = [&](const char *n){ return DataFilePath(gd, std::string("audio/CONFIG/") + n); };
	std::vector<uint8_t> d;
	// ------------------------------------------------------------ BankSlot.dat
	std::vector<uint32_t> slotSize;
	{
		std::string lg = "audio/CONFIG/BankSlot.dat";
		if(!readFile(cfg("BankSlot.dat"), d) || d.size() <= 2)
			ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-01", lg, -1, "", "BankSlot.dat отсутствует или ≤ 2 байт — LoadBankSlotFile 0x4E0590 → 0 → CAEAudioHardware::Initialise fail → GetTrackPlayTime 0x4F1537 разыменует m_pStreamingChannel = NULL → краш при первом CAudioEngine::Service", "");
		else{
			ctx.rep->countFile(CAT_AUDIO);
			int count = d[0] | (d[1] << 8);
			if(d.size() != 2 + (size_t)count * 4820)
				ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-02", lg, -1, "", fmt("длина %u ≠ 2 + %d × 4820 — %s", (unsigned)d.size(), count, d.size() > 2 + (size_t)count * 4820 ? "Read(len−2) переполняет new(count×0x12D4)" : "хвост блока не инициализирован → мусорные размеры слотов → Malloc(мусор)"), "");
			for(int i = 0; i < count && 2 + (size_t)i * 4820 + 8 <= d.size(); i++){
				uint32_t sz; memcpy(&sz, d.data() + 2 + (size_t)i * 4820 + 4, 4);
				slotSize.push_back(sz);
				if(sz == 0) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-23", lg, i, "", fmt("слот %d размера 0 — любая загрузка в него = переполнение следующего слота", i), "");
			}
			if(count <= 42 && count != 38 && count != 39)
				ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-11", lg, -1, "", fmt("%d слотов — exe запрашивает слоты до 42 (40 двигатель игрока, 41 generic feet, 42 bullet pass); запрос слота == count проходит (cmp di,[esi+0xC]; jg) → Service пишет таблицу звуков за массивом слотов → порча кучи", count), "");
			else if(count == 38 || count == 39)
				ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-11", lg, -1, "", fmt("%d слотов — слоты 40..42 молча выброшены: нет звука двигателя игрока / generic feet / bullet pass", count), "");
			else if(count == 43 || count == 44)
				ctx.add(SEV_INFO, CAT_AUDIO, "AUD-11", lg, -1, "", fmt("%d слотов (ваниль 45; слоты 43/44 exe не запрашивает)", count), "");
			uint64_t sum = 0; for(size_t i = 0; i < slotSize.size(); i++) sum += slotSize[i];
			if(sum != 8578144) ctx.add(SEV_INFO, CAT_AUDIO, "AUD-23", lg, -1, "", fmt("сумма размеров слотов %llu ≠ ванильных 8578144 (m_nBufferSize)", (unsigned long long)sum), "");
		}
	}
	auto slot = [&](int i) -> uint32_t { return i >= 0 && i < (int)slotSize.size() ? slotSize[(size_t)i] : 0xFFFFFFFFu; };
	// ------------------------------------------------------------ BankLkup.dat
	std::vector<BankLkup> banks;
	{
		std::string lg = "audio/CONFIG/BankLkup.dat";
		if(!readFile(cfg("BankLkup.dat"), d) || d.empty()) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-03", lg, -1, "", "BankLkup.dat отсутствует или пуст — LoadBankLookupFile 0x4DFBD0 → 0 → краш инициализации аудио (AUD-01)", "");
		else{
			ctx.rep->countFile(CAT_AUDIO);
			if(d.size() % 12) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-04", lg, -1, "", fmt("размер %u не кратен 12 — new((len/12)×12), Read(len) → переполнение кучи на %u байт", (unsigned)d.size(), (unsigned)(d.size() % 12)), "");
			size_t n = d.size() / 12;
			for(size_t i = 0; i < n; i++){ BankLkup b; b.pak = d[i * 12]; memcpy(&b.offset, d.data() + i * 12 + 4, 4); memcpy(&b.size, d.data() + i * 12 + 8, 4); banks.push_back(b); }
			if(n < 710){
				// banks loaded unconditionally at start → crash at boot; scanner / mission-table / vehicle banks → crash when first requested
				static const int STATIC_BANKS[] = { 0,1,2,3,4,5,6,13,27,28,29,30,31,39,44,51,52,59,60,74,82,105,128,138,143, -1 };
				static const int LAZY_BANKS[] = { 147,148,149,150,151,152,
					0,20,31,34,35,37,70,100,153,158,160,163,167,171,183,198,209,222,226,242,265,266,291,292,293,296,298,304,310,319,339,345,351,352, -1 };
				bool crash = false, lazy = false;
				for(int k = 0; STATIC_BANKS[k] >= 0; k++) if(STATIC_BANKS[k] == (int)n) crash = true;
				for(int k = 0; LAZY_BANKS[k] >= 0; k++) if(LAZY_BANKS[k] == (int)n) lazy = true;
				for(int i = 0; i < 231 && !lazy; i++){ if(VEH_AUDIO[i].playerBank == (int)n || VEH_AUDIO[i].dummyBank == (int)n) lazy = true; }
				std::string dead;
				if(n <= 709) dead += fmt("нет голосов педов %d..709", std::max((int)n, 365));
				if(n < 365) dead += fmt(", нет mission/script-аудио %d..364", std::max((int)n, 147));
				if(n < 147) dead += fmt(", нет GENRL-банков %d..143", (int)n);
				ctx.add(crash ? SEV_FATAL : SEV_ERROR, CAT_AUDIO, "AUD-12", lg, -1, "", fmt("%d записей < 710 — банки с id ≥ %d немы (%s)%s", (int)n, (int)n, dead.c_str(),
				        crash ? fmt("; банк %d грузится при старте: GetBankLookup 0x4E01C0 вернёт NULL при bankId == count → mov cl,[eax] → краш при запуске", (int)n).c_str()
				              : lazy ? fmt("; банк %d есть в таблице mission-аудио / сканера / машин: GetBankLookup → NULL при bankId == count → краш в момент первого запроса (сборка без этого события работает)", (int)n).c_str() : ""),
				        "Сохрани 710 записей BankLkup.dat (лишние банки могут указывать на любой пак), либо убедись, что ни один звук с id ≥ count не запрашивается.");
			}
		}
	}
	// ------------------------------------------------------------ PakFiles.dat
	std::vector<std::string> paks; std::vector<uint64_t> pakSize; std::vector<bool> pakOk;
	{
		std::string lg = "audio/CONFIG/PakFiles.dat";
		if(!readFile(cfg("PakFiles.dat"), d) || d.empty()) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-05", lg, -1, "", "PakFiles.dat отсутствует или пуст — LoadSFXPakLookupFile 0x4DFC70 → 0 → краш инициализации аудио", "");
		else{
			ctx.rep->countFile(CAT_AUDIO);
			if(d.size() % 52) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-06", lg, -1, "", fmt("размер %u не кратен 52 — переполнение кучи на %u байт", (unsigned)d.size(), (unsigned)(d.size() % 52)), "");
			size_t n = d.size() / 52;
			for(size_t i = 0; i < n; i++){
				size_t run = 0; while(i * 52 + run < d.size() && d[i * 52 + run] != 0) run++;
				std::string name((const char *)d.data() + i * 52, std::min(run, (size_t)52));
				if(run >= 52){
					if(run >= 118) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-15", lg, (int)i, name, fmt("запись %d без NUL: ран %u байт ≥ 118 — rep movsb 0x4DFD94 в буфер стека затирает сохранённые регистры/адрес возврата → краш", (int)i, (unsigned)run), "");
					else ctx.add(SEV_WARN, CAT_AUDIO, "AUD-15", lg, (int)i, name, fmt("запись %d без NUL в 52 байтах — strlen склеивает имя со следующей записью → файл не найден (AUD-14)", (int)i), "");
				}
				paks.push_back(name);
				uint64_t sz = 0;
				bool ok = fileSize(DataFilePath(gd, "audio/SFX/" + name), sz);
				pakSize.push_back(sz); pakOk.push_back(ok);
				if(!ok){
					int lo = -1, hi = -1; for(size_t b = 0; b < banks.size(); b++) if(banks[b].pak == (int)i){ if(lo < 0) lo = (int)b; hi = (int)b; }
					ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-14", lg, (int)i, name, fmt("audio/SFX/%s не найден — CdStreamOpen → 0: банки %d..%d читаются через handle 0 (%s) → не те сэмплы / тишина", name.c_str(), lo, hi, paks.empty() ? "?" : paks[0].c_str()), "");
				}
			}
		}
	}
	// ------------------------------------------------------------ bank headers (AUD-13, 16..22)
	{
		std::string lg = "audio/CONFIG/BankLkup.dat";
		uint32_t minDummy = 0xFFFFFFFFu; for(int s = 7; s <= 16; s++) minDummy = std::min(minDummy, slot(s));
		uint32_t minSpeech = 0xFFFFFFFFu; for(int s = 20; s <= 24; s++) minSpeech = std::min(minSpeech, slot(s));
		std::map<int, int> staticSlot = { {59,0},{60,1},{39,2},{27,3},{52,4},{143,5},{105,6},{74,17},{13,18},{138,19},{51,31},{128,32},{0,41},{1,30},{2,30},{3,30},{4,30},{5,30},{6,30},{28,42},{29,42},{30,42},{31,42},{44,40} };
		std::set<int> missionBanks = { 0,20,31,34,35,37,70,100,153,158,160,163,167,171,183,198,209,222,226,242,265,266,291,292,293,296,298,304,310,319,339,345,351,352 };
		std::set<int> dummyBanks, playerBanks;
		for(int i = 0; i < 231; i++){ if(VEH_AUDIO[i].dummyBank >= 0) dummyBanks.insert(VEH_AUDIO[i].dummyBank); if(VEH_AUDIO[i].playerBank >= 0) playerBanks.insert(VEH_AUDIO[i].playerBank); }
		static const int SCAN_MAX[6] = { 30134, 10080, 8992, 15906, 12866, 16578 };
		std::map<int, FILE*> open;
		for(size_t b = 0; b < banks.size(); b++){
			if(ctx.cancelled()) break;
			const BankLkup &bk = banks[b];
			if((size_t)b % 50 == 0) ctx.prog->step(fmt("bank %d", (int)b).c_str(), (int)b, (int)banks.size());
			if(bk.pak >= (int)paks.size()){ ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-13", lg, (int)b, "", fmt("банк %d: pakFileIndex %d ≥ числа паков %d — m_paStreamHandles[pak] за блоком → мусорный handle, банк не грузится (тишина)", (int)b, bk.pak, (int)paks.size()), ""); continue; }
			if(!pakOk[(size_t)bk.pak]) continue;
			if((uint64_t)bk.offset + 0x12C4 + bk.size > pakSize[(size_t)bk.pak]){ ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-16", lg, (int)b, paks[(size_t)bk.pak], fmt("банк %d: offset %u + 0x12C4 + size %u > размера %s (%llu) — CdStreamRead за EOF: устаревшие данные буфера / запрос не завершается", (int)b, bk.offset, bk.size, paks[(size_t)bk.pak].c_str(), (unsigned long long)pakSize[(size_t)bk.pak]), ""); continue; }
			FILE *f = nullptr;
			auto it = open.find(bk.pak);
			if(it == open.end()){ f = OpenReadUtf8(DataFilePath(gd, "audio/SFX/" + paks[(size_t)bk.pak])); open[bk.pak] = f; } else f = it->second;
			if(!f) continue;
			uint8_t hdr[0x12C4];
			if(!readAt(f, bk.offset, hdr, sizeof(hdr))) continue;
			int16_t numSounds; memcpy(&numSounds, hdr, 2);
			std::string who = fmt("%s банк %d", paks[(size_t)bk.pak].c_str(), (int)b);
			bool single = (b >= 144 && b <= 146) || (b >= 147 && b <= 364) || (b >= 365 && b <= 709);
			if(numSounds > 400) ctx.add(SEV_WARN, CAT_AUDIO, "AUD-17", lg, (int)b, who, fmt("numSounds %d > 400 — следующее смещение для звука 399 заворачивается через %%400 → отрицательная длина", numSounds), "");
			else if(numSounds < 0) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-17", lg, (int)b, who, fmt("numSounds %d < 0 — GetSoundBuffer всегда 0: немой банк", numSounds), "");
			int ns = std::max(0, std::min((int)numSounds, 400));
			std::vector<uint32_t> off((size_t)ns);
			for(int i = 0; i < ns; i++) memcpy(&off[(size_t)i], hdr + 4 + i * 12, 4);
			bool mono = true; for(int i = 1; i < ns; i++) if(off[(size_t)i] < off[(size_t)i - 1]) mono = false;
			if(!mono || (ns > 0 && off[(size_t)ns - 1] > bk.size))
				ctx.add(single ? SEV_FATAL : SEV_ERROR, CAT_AUDIO, "AUD-18", lg, (int)b, who, fmt("таблица звуков: смещения %s — soundSize = next − this заворачивается в огромное uint → %s", !mono ? "не неубывающие" : "последнее больше size", single ? "Malloc(huge) / копия state 3 в слот речи → краш" : "отрицательная длина каналу"), "");
			if((int)b < 710 && numSounds >= 0 && numSounds < VANILLA_NUMSOUNDS[b]){
				uint32_t target = (b >= 690) ? slot(25) : (b >= 365 || (b >= 144 && b <= 146)) ? minSpeech : 0xFFFFFFFFu;
				bool crash = bk.size > target;
				ctx.add(crash ? SEV_FATAL : SEV_WARN, CAT_AUDIO, "AUD-19", lg, (int)b, who, fmt("в банке %d звуков, в ванили %d — exe зашивает id звуков по банку: LoadSound с soundId ≥ numSounds берёт неинициализированную запись таблицы → soundSize = весь банк (%u байт)%s", numSounds, VANILLA_NUMSOUNDS[b], bk.size, crash ? fmt(" > слота %u → копия за слот → краш", target).c_str() : ""), "");
			}
			// sizes vs slots
			auto sizeVs = [&](uint32_t need, uint32_t cap, const char *what, Severity sev, const char *code){
				if(cap != 0xFFFFFFFFu && need > cap) ctx.add(sev, CAT_AUDIO, code, lg, (int)b, who, fmt("%s: %u байт > размера слота %u — Service копирует rep movsd без сравнения со slot.size → переполнение следующих слотов", what, need, cap), "");
			};
			auto st = staticSlot.find((int)b);
			if(st != staticSlot.end()) sizeVs(bk.size, slot(st->second), fmt("банк целиком в слот %d", st->second).c_str(), SEV_FATAL, "AUD-20");
			if(dummyBanks.count((int)b)) sizeVs(bk.size, minDummy, "dummy-банк машины в слоты 7..16", SEV_FATAL, "AUD-20");
			if(playerBanks.count((int)b)) sizeVs(bk.size, slot(40), "player-банк машины в слот 40", SEV_FATAL, "AUD-20");
			if(missionBanks.count((int)b)){
				if(bk.size > slot(29)) sizeVs(bk.size, slot(29), "mission-банк в слоты 26..29", SEV_FATAL, "AUD-20");
				else if(bk.size > slot(26)) ctx.add(SEV_INFO, CAT_AUDIO, "AUD-20", lg, (int)b, who, fmt("mission-банк %u байт влезает только в слот 29 (%u), слоты 26..28 = %u — слот выбирает скрипт (ваниль: банки 35, 209, 339)", bk.size, slot(29), slot(26)), "");
			}
			// single sounds
			if(ns > 0 && (single || b == 82)){
				uint32_t maxSound = 0;
				for(int i = 0; i < ns; i++){ uint32_t nx = i + 1 < ns ? off[(size_t)i + 1] : bk.size; if(nx >= off[(size_t)i]) maxSound = std::max(maxSound, nx - off[(size_t)i]); }
				if(b >= 690 && b <= 709) sizeVs(maxSound, slot(25), "самый большой звук PLY в слот 25", SEV_FATAL, "AUD-21");
				else if(b >= 365 || (b >= 144 && b <= 146)) sizeVs(maxSound, minSpeech, "самый большой звук речи в слоты 20..24", SEV_FATAL, "AUD-21");
				else if(b >= 147 && b <= 152){
					int k = (int)b - 147;
					if(maxSound > 30178) sizeVs(maxSound, 30178, "звук сканера полиции в слоты 33..37", SEV_FATAL, "AUD-21");
					else if((int)maxSound > SCAN_MAX[k]) ctx.add(SEV_WARN, CAT_AUDIO, "AUD-21", lg, (int)b, who, fmt("звук сканера %u байт больше ванильного максимума банка %d (%d) — пара банк→слот строится динамически в AddAudioEvent", maxSound, (int)b, SCAN_MAX[k]), "");
				}else if(b >= 153 && b <= 364){
					if(maxSound > slot(29)) sizeVs(maxSound, slot(29), "script speech в слоты 26..29", SEV_FATAL, "AUD-22");
					else if(maxSound > slot(26)) ctx.add(SEV_INFO, CAT_AUDIO, "AUD-22", lg, (int)b, who, fmt("script speech %u байт влезает только в слот 29 (%u), слоты 26..28 = %u — слот выбирает скрипт", maxSound, slot(29), slot(26)), "");
				}else if(b == 82) sizeVs(maxSound, std::min(slot(2), slot(5)), "loading tune (банк 82) в слоты 2/5", SEV_FATAL, "AUD-21");
			}
		}
		for(auto it = open.begin(); it != open.end(); ++it) if(it->second) fclose(it->second);
	}
	// ------------------------------------------------------------ streams
	{
		std::vector<std::string> strm; std::vector<bool> strmNoNul;
		std::string lg = "audio/CONFIG/StrmPaks.dat";
		if(!readFile(cfg("StrmPaks.dat"), d)) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-07", lg, -1, "", "StrmPaks.dat отсутствует — LoadStreamPackTable 0x4E0970 → 0 → краш инициализации аудио", "");
		else if(d.empty()) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-07", lg, -1, "", "StrmPaks.dat пуст — Initialise 0x4E0C76 делает lstrlen по Malloc(0): мусорный путь → OpenFile fail → return 0 → краш инициализации", "");
		else{
			ctx.rep->countFile(CAT_AUDIO);
			for(size_t i = 0; i * 16 < d.size(); i++){
				size_t run = 0; while(run < 16 && i * 16 + run < d.size() && d[i * 16 + run] != 0) run++;
				strm.push_back(std::string((const char *)d.data() + i * 16, run)); strmNoNul.push_back(run >= 16);
				if(run >= 16) ctx.add(SEV_WARN, CAT_AUDIO, "AUD-29", lg, (int)i, strm.back(), fmt("запись %d без NUL в 16 байтах — имя склеивается со следующим → файл не найден", (int)i), "");
			}
			if(!strm.empty() && !fileExists(DataFilePath(gd, "audio/streams/" + strm[0])))
				ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-09", lg, 0, strm[0], fmt("первый стрим-пак audio/streams/%s отсутствует — Initialise 0x4E0C50: OpenFile fail → DVD fail → return 0 → краш инициализации аудио", strm[0].c_str()), "");
		}
		std::vector<TrakLkup> traks;
		lg = "audio/CONFIG/TrakLkup.dat";
		if(!readFile(cfg("TrakLkup.dat"), d)) ctx.add(SEV_FATAL, CAT_AUDIO, "AUD-08", lg, -1, "", "TrakLkup.dat отсутствует — LoadTrackLookupTable 0x4E09F0 → 0 → краш инициализации аудио", "");
		else{
			ctx.rep->countFile(CAT_AUDIO);
			if(d.size() % 12) ctx.add(SEV_WARN, CAT_AUDIO, "AUD-24", lg, -1, "", fmt("размер %u не кратен 12", (unsigned)d.size()), "");
			size_t n = d.size() / 12;
			if(n < 1922) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-24", lg, -1, "", fmt("%d треков < 1922 — id треков радио/катсцен/эмбиента зашиты в exe: GetTrackInfo → NULL, треки ≥ %d немы", (int)n, (int)n), "");
			for(size_t i = 0; i < n; i++){ TrakLkup t; t.pak = d[i * 12]; memcpy(&t.offset, d.data() + i * 12 + 4, 4); memcpy(&t.length, d.data() + i * 12 + 8, 4); traks.push_back(t); }
			std::set<int> used;
			for(size_t i = 0; i < traks.size(); i++){
				if(traks[i].pak >= (int)strm.size()) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-25", lg, (int)i, "", fmt("трек %d: streamPakIndex %d ≥ числа стрим-паков %d — мусорное имя, CreateFileA fail, трек нем", (int)i, traks[i].pak, (int)strm.size()), "");
				else used.insert(traks[i].pak);
			}
			std::map<int, uint64_t> strmSize; std::map<int, FILE*> open;
			for(auto it = used.begin(); it != used.end(); ++it){
				uint64_t sz = 0;
				std::string phys = DataFilePath(gd, "audio/streams/" + strm[(size_t)*it]);
				if(!fileSize(phys, sz)){ if(*it != 0) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-26", "audio/CONFIG/StrmPaks.dat", *it, strm[(size_t)*it], fmt("audio/streams/%s отсутствует, а на него ссылаются треки — CAEDataStream::Initialise fail → декодер выброшен, треки немы", strm[(size_t)*it].c_str()), ""); continue; }
				strmSize[*it] = sz;
				open[*it] = OpenReadUtf8(phys);
			}
			static const uint8_t KEY[16] = { 0xEA,0x3A,0xC4,0xA1,0x9A,0xA8,0x14,0xF3,0x48,0xB0,0xD7,0x23,0x9D,0xE8,0xFF,0xF1 };
			int badOgg = 0, badRange = 0; std::string firstOgg, firstRange;
			for(size_t i = 0; i < traks.size(); i++){
				if(ctx.cancelled()) break;
				if((size_t)i % 100 == 0) ctx.prog->step(fmt("track %d", (int)i).c_str(), (int)i, (int)traks.size());
				const TrakLkup &t = traks[i];
				auto sz = strmSize.find(t.pak);
				if(sz == strmSize.end()) continue;
				uint64_t end = (uint64_t)t.offset + 0x1F84 + t.length;
				if(end > sz->second){ badRange++; if(firstRange.empty()) firstRange = fmt("трек %d в %s: offset %u + 0x1F84 + %u > %llu", (int)i, strm[(size_t)t.pak].c_str(), t.offset, t.length, (unsigned long long)sz->second); continue; }
				FILE *f = open[t.pak];
				if(!f) continue;
				uint8_t s4[4];
				uint64_t p = (uint64_t)t.offset + 0x1F84;
				if(!readAt(f, p, s4, 4)) continue;
				for(int k = 0; k < 4; k++) s4[k] ^= KEY[(p + (uint64_t)k) & 15];
				if(memcmp(s4, "OggS", 4) != 0){ badOgg++; if(firstOgg.empty()) firstOgg = fmt("трек %d в %s @%llu", (int)i, strm[(size_t)t.pak].c_str(), (unsigned long long)p); }
			}
			for(auto it = open.begin(); it != open.end(); ++it) if(it->second) fclose(it->second);
			if(badRange) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-27", lg, -1, "", fmt("%d треков выходят за размер своего стрим-пака (первый: %s) — ReadFile 0 байт → Ogg sync fail → трек нем", badRange, firstRange.c_str()), "На нестоковой установке (пустые стрим-паки, StrmPaks с перенаправлением) это ожидаемо.");
			if(badOgg) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-28", lg, -1, "", fmt("%d треков: расшифрованные байты по offset+0x1F84 ≠ «OggS» (первый: %s) — ov_open fail → трек нем", badOgg, firstOgg.c_str()), "Ключ XOR EA 3A C4 A1 9A A8 14 F3 48 B0 D7 23 9D E8 FF F1, индекс = смещение & 15. На установке с перенаправленным StrmPaks.dat (пустые стрим-паки) это ожидаемо.");
		}
	}
	// ------------------------------------------------------------ EventVol.dat
	{
		uint64_t sz = 0;
		if(!fileSize(cfg("EventVol.dat"), sz) || sz < 45401) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-10", "audio/CONFIG/EventVol.dat", -1, "", fmt("EventVol.dat %s — CAudioEngine::Initialise 0x5B9C60 выходит до инициализации сущностей: аудио сломано, возможный поздний краш", sz ? fmt("короче 45401 байт (%llu)", (unsigned long long)sz).c_str() : "отсутствует"), "");
		else ctx.rep->countFile(CAT_AUDIO);
	}
	// ------------------------------------------------------------ vehicles.ide ids (AUD-35/36)
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.type != OT_CARS) continue;
		if(o.id > 630) ctx.add(SEV_ERROR, CAT_AUDIO, "AUD-35", o.file, o.line, o.name, fmt("id %d > 630 — CAEVehicleAudioEntity::Initialise 0x4F7670 копирует 36 байт из 0x860AF0+(id−400)×36 без проверки: за 231-й строкой лежит таблица байтов 0/1 → мусорный тип звука/банки/радио", o.id), "", o.id);
		else if(o.id >= 612) ctx.add(SEV_WARN, CAT_AUDIO, "AUD-35", o.file, o.line, o.name, fmt("id %d в 612..630 — ванильная запись-заглушка: soundType 10 (немой двигатель, без гудка и дверей)", o.id), "", o.id);
		else if(o.id >= 400){
			const VehAudioRow &r = VEH_AUDIO[o.id - 400];
			const char *vn = VANILLA_VEH_NAME[o.id - 400];
			if(r.soundType == 10) ctx.add(ieq(o.name, vn) ? SEV_INFO : SEV_WARN, CAT_AUDIO, "AUD-35", o.file, o.line, o.name, fmt("id %d (ваниль %s): soundType 10 — немой двигатель, без гудка и дверей%s", o.id, vn, ieq(o.name, vn) ? "" : " — замена на этом id будет немой"), "", o.id);
			if(!ieq(o.name, vn)) ctx.add(SEV_INFO, CAT_AUDIO, "AUD-36", o.file, o.line, o.name, fmt("id %d занимает место ванильной «%s»: звук берётся по id — тип %d, банки player %d / dummy %d, гудок %d, двери %d, радио %d/%d", o.id, vn, r.soundType, r.playerBank, r.dummyBank, r.hornTone, r.doorSound, r.radioId, r.radioType), "", o.id);
		}
	}
}

} // namespace gc
