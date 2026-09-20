// gtacheck — game data loading: default.dat / gta.dat (+ modloader overlay),
// IMG directories, IDE and IPL (text + bnry).  Emits the DAT-nn checks from
// E:\RE\addon_check\textdata_path.md while it parses.
#include "gtacheck.h"

#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gc {

// ------------------------------------------------------------ tokenizer ---

// The engine's LoadLine: every byte < 0x20 and every ',' becomes a space,
// leading whitespace is skipped, and the line buffer is 512 bytes.
struct Line {
	std::string raw;	// after the character replacement, trimmed
	int number;		// 1-based
	bool tooLong;		// > 511 chars (DAT-01)
	bool comment;		// first non-blank is '#'
	std::vector<std::string> tok;
};

static void tokenize(Line &l)
{
	l.tok.clear();
	size_t i = 0, n = l.raw.size();
	while(i < n){
		while(i < n && l.raw[i] == ' ') i++;
		size_t s = i;
		while(i < n && l.raw[i] != ' ') i++;
		if(i > s) l.tok.push_back(l.raw.substr(s, i - s));
	}
}

static bool loadLines(const std::string &phys, std::vector<Line> &out)
{
	std::vector<std::string> lines;
	if(!readTextLines(phys, lines)) return false;
	out.clear();
	out.reserve(lines.size());
	for(size_t i = 0; i < lines.size(); i++){
		Line l;
		l.number = (int)i + 1;
		l.tooLong = lines[i].size() > 511;
		std::string s = lines[i];
		for(size_t k = 0; k < s.size(); k++){
			unsigned char c = (unsigned char)s[k];
			if(c < 0x20 || c == ',') s[k] = ' ';
		}
		l.raw = trim(s);
		l.comment = !l.raw.empty() && l.raw[0] == '#';
		if(l.raw.empty()) continue;
		tokenize(l);
		out.push_back(l);
	}
	return true;
}

static bool isInt(const std::string &t)
{
	if(t.empty()) return false;
	size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
	if(i >= t.size()) return false;
	for(; i < t.size(); i++) if(!isdigit((unsigned char)t[i])) return false;
	return true;
}

static bool isHex(const std::string &t)
{
	if(t.empty()) return false;
	size_t i = 0;
	if(t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) i = 2;
	for(; i < t.size(); i++) if(!isxdigit((unsigned char)t[i])) return false;
	return true;
}

static bool isNum(const std::string &t)
{
	if(t.empty()) return false;
	char *end = nullptr;
	strtod(t.c_str(), &end);
	return end && *end == '\0';
}

static int toInt(const std::string &t) { return (int)strtol(t.c_str(), nullptr, 10); }
static float toF(const std::string &t) { return (float)strtod(t.c_str(), nullptr); }

// ----------------------------------------------------------- modloader ---

static void scanDirRec(const std::string &dir, const std::string &rel, std::vector<std::pair<std::string,std::string>> &files, int depth)
{
	if(depth > 24) return;
#ifdef _WIN32
	std::string pat = joinPath(dir, "*");
	int wn = MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, nullptr, 0);
	std::wstring wpat((size_t)wn, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, &wpat[0], wn);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(wpat.c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE) return;
	do{
		int n8 = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nullptr, 0, nullptr, nullptr);
		std::string name((size_t)n8, '\0');
		WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, &name[0], n8, nullptr, nullptr);
		name.resize(strlen(name.c_str()));
		if(name == "." || name == "..") continue;
		std::string phys = joinPath(dir, name);
		std::string r = rel.empty() ? name : rel + "/" + name;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			scanDirRec(phys, r, files, depth + 1);
		else
			files.push_back(std::make_pair(phys, r));
	}while(FindNextFileW(h, &fd));
	FindClose(h);
#endif
}

static void listDirs(const std::string &dir, std::vector<std::string> &dirs)
{
#ifdef _WIN32
	std::string pat = joinPath(dir, "*");
	int wn = MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, nullptr, 0);
	std::wstring wpat((size_t)wn, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, pat.c_str(), -1, &wpat[0], wn);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(wpat.c_str(), &fd);
	if(h == INVALID_HANDLE_VALUE) return;
	do{
		if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		int n8 = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nullptr, 0, nullptr, nullptr);
		std::string name((size_t)n8, '\0');
		WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, &name[0], n8, nullptr, nullptr);
		name.resize(strlen(name.c_str()));
		if(name == "." || name == ".." || name[0] == '.') continue;
		dirs.push_back(name);
	}while(FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(dirs.begin(), dirs.end());
#endif
}

struct ModAddition { std::string type, path, mod; int priority; };

static void parseModloaderIni(const std::string &ini, std::map<std::string,int> &prio)
{
	std::vector<std::string> lines;
	if(!readTextLines(ini, lines)) return;
	std::string profile = "Default";
	bool inCfg = false;
	for(size_t i = 0; i < lines.size(); i++){
		std::string s = trim(lines[i]);
		if(s.empty()) continue;
		if(s[0] == '['){ inCfg = ieq(s, "[Folder.Config]"); continue; }
		if(inCfg && s.compare(0, 7, "Profile") == 0){
			size_t eq = s.find('=');
			if(eq != std::string::npos) profile = trim(s.substr(eq + 1));
		}
	}
	std::string sect = "[Profiles." + profile + ".Priority]";
	bool inPrio = false;
	for(size_t i = 0; i < lines.size(); i++){
		std::string s = trim(lines[i]);
		if(s.empty()) continue;
		if(s[0] == '['){ inPrio = ieq(s, sect); continue; }
		if(!inPrio || s[0] == '#' || s[0] == ';') continue;
		size_t eq = s.find('=');
		if(eq == std::string::npos) continue;
		prio[lower(trim(s.substr(0, eq)))] = toInt(trim(s.substr(eq + 1)));
	}
}

static bool isLooseKind(const std::string &ext)
{
	return ext == "dff" || ext == "txd" || ext == "col" || ext == "ifp" || ext == "rrr" || ext == "scm";
}

static bool isBnryFile(const std::string &phys)
{
	std::vector<uint8_t> d;
	FILE *f = nullptr;
#ifdef _WIN32
	int wn = MultiByteToWideChar(CP_UTF8, 0, phys.c_str(), -1, nullptr, 0);
	std::wstring w((size_t)wn, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, phys.c_str(), -1, &w[0], wn);
	f = _wfopen(w.c_str(), L"rb");
#else
	f = fopen(phys.c_str(), "rb");
#endif
	if(!f) return false;
	char m[4] = {0,0,0,0};
	size_t r = fread(m, 1, 4, f);
	fclose(f);
	return r == 4 && memcmp(m, "bnry", 4) == 0;
}

// Collects loose overrides, path redirects and gta.dat-style additions from
// modloader/.  Semantics follow SA Mod Loader closely enough for a checker:
//   * dff/txd/col/ifp/rrr/scm and binary ipl → override the IMG entry with the same name
//   * ide/ipl/zon/dat/cfg… whose basename matches a stock data file → replace it
//   * other ide/ipl → added after gta.dat (IDE first, then IPL)
//   * gta.dat / default.dat / *.txt in a mod → their IDE/IPL/IMG/COLFILE lines are additions
//   * *.img → added archives (replace a stock one with the same basename)
// Priority: modloader.ini [Profiles.X.Priority] (0 = disabled), default 50; higher wins.
static void scanModloader(Context &ctx, std::vector<ModAddition> &adds, std::vector<std::pair<std::string,std::string>> &addedImgs)
{
	GameData &gd = *ctx.gd;
	std::string mlDir = joinPath(gd.root, "modloader");
	if(!dirExists(mlDir)) return;
	std::map<std::string,int> prio;
	parseModloaderIni(joinPath(mlDir, "modloader.ini"), prio);
	std::vector<std::string> mods;
	listDirs(mlDir, mods);
	if(mods.empty()) return;
	gd.modloaderActive = true;

	for(size_t m = 0; m < mods.size(); m++){
		int p = 50;
		auto it = prio.find(lower(mods[m]));
		if(it != prio.end()) p = it->second;
		if(p <= 0) continue;	// disabled in modloader.ini
		std::vector<std::pair<std::string,std::string>> files;
		scanDirRec(joinPath(mlDir, mods[m]), "", files, 0);
		for(size_t i = 0; i < files.size(); i++){
			ModFile mf;
			mf.phys = files[i].first;
			mf.rel = lower(files[i].second);
			mf.mod = mods[m];
			mf.priority = p;
			gd.modFiles.push_back(mf);
			int idx = (int)gd.modFiles.size() - 1;
			std::string ext = extOf(mf.rel);
			std::string base = basename(mf.rel);
			if(isLooseKind(ext) || (ext == "ipl" && isBnryFile(mf.phys))){
				auto e = gd.looseByName.find(base);
				if(e == gd.looseByName.end() || gd.modFiles[e->second].priority < p)
					gd.looseByName[base] = idx;
				continue;
			}
			if(ext == "img"){
				addedImgs.push_back(std::make_pair(mf.phys, mods[m]));
				continue;
			}
			if(ext == "txt" || base == "gta.dat" || base == "default.dat" || base == "gta3.dat" || base == "gta_vc.dat"){
				// readme / dat additions
				std::vector<std::string> lines;
				if(readTextLines(mf.phys, lines)){
					for(size_t k = 0; k < lines.size(); k++){
						std::string s = trim(lines[k]);
						if(s.empty() || s[0] == '#') continue;
						std::string type, path;
						if(s.compare(0, 4, "IDE ") == 0){ type = "IDE"; path = s.substr(4); }
						else if(s.compare(0, 4, "IPL ") == 0){ type = "IPL"; path = s.substr(4); }
						else if(s.compare(0, 4, "IMG ") == 0){ type = "IMG"; path = s.substr(4); }
						else if(s.compare(0, 8, "CDIMAGE ") == 0){ type = "IMG"; path = s.substr(8); }
						else if(s.compare(0, 8, "COLFILE ") == 0){
							type = "COLFILE";
							path = trim(s.substr(8));
							size_t sp = path.find(' ');
							if(sp != std::string::npos) path = trim(path.substr(sp + 1));
						}else if(s.compare(0, 11, "TEXDICTION ") == 0){ type = "TEXDICTION"; path = s.substr(11); }
						if(type.empty()) continue;
						ModAddition a;
						a.type = type;
						a.path = trim(path);
						a.mod = mods[m];
						a.priority = p;
						adds.push_back(a);
					}
				}
				if(ext == "txt") continue;
			}
			// data redirect by basename (also registers the relative path)
			if(ext == "ide" || ext == "ipl" || ext == "zon" || ext == "dat" || ext == "cfg" || ext == "fxp" || ext == "ifp"){
				auto e = gd.redirectByRel.find(base);
				if(e == gd.redirectByRel.end() || gd.modFiles[e->second].priority < p)
					gd.redirectByRel[base] = idx;
				gd.redirectByRel[mf.rel] = idx;
			}
		}
	}
}

// Physical path for a logical game path, honouring modloader redirects.
static std::string physFor(GameData &gd, const std::string &logical, std::string *mod)
{
	if(mod) mod->clear();
	if(gd.modloaderActive){
		std::string rel = lower(normSlashes(logical));
		auto it = gd.redirectByRel.find(rel);
		if(it == gd.redirectByRel.end()) it = gd.redirectByRel.find(basename(rel));
		if(it != gd.redirectByRel.end()){
			if(mod) *mod = gd.modFiles[it->second].mod;
			return gd.modFiles[it->second].phys;
		}
	}
	return resolvePath(gd.root, logical);
}

// ----------------------------------------------------------------- IMG ---

static int entryKind(const std::string &ext)
{
	if(ext == "dff") return EK_DFF;
	if(ext == "txd") return EK_TXD;
	if(ext == "col") return EK_COL;
	if(ext == "ipl") return EK_IPL;
	if(ext == "dat") return EK_DAT;
	if(ext == "ifp") return EK_IFP;
	if(ext == "rrr") return EK_RRR;
	if(ext == "scm") return EK_SCM;
	return EK_OTHER;
}

static void registerEntry(Context &ctx, Entry &e)
{
	GameData &gd = *ctx.gd;
	std::string key = lower(e.name);
	auto it = gd.entryByName.find(key);
	if(it != gd.entryByName.end()){
		e.duplicate = true;
		const Entry &prev = gd.entries[it->second];
		std::string a = prev.img >= 0 ? basename(gd.archives[prev.img].logical) : prev.loose;
		std::string b = e.img >= 0 ? basename(gd.archives[e.img].logical) : e.loose;
		// all three games: CStreaming::LoadCdDirectory keeps the first position («appears more than once» → skip)
		ctx.add(SEV_INFO, CAT_IMG, "IMG-21", b, e.dirIndex, e.name,
		        fmt("«%s» встречается ещё раз (первый — в %s); %s", e.name.c_str(), a.c_str(),
		            gd.isSA() ? "SA берёт ПЕРВУЮ зарегистрированную запись, эта копия мёртвая" : "III/VC тоже берут ПЕРВУЮ запись, эта копия мёртвая"),
		        "CStreaming::LoadCdDirectory пропускает запись, если у модели/TXD уже есть позиция в IMG.");
		gd.entries.push_back(e);
		return;
	}
	gd.entries.push_back(e);
	gd.entryByName[key] = (int)gd.entries.size() - 1;
}

static void loadArchive(Context &ctx, const std::string &logical, const std::string &phys, const std::string &mod)
{
	GameData &gd = *ctx.gd;
	Archive a;
	a.logical = logical;
	a.phys = phys;
	a.mod = mod;
	std::string bl = lower(basename(logical));
	a.vanilla = bl == "gta3.img" || bl == "gta_int.img" || bl == "player.img" || bl == "cutscene.img";
	if(!fileExists(phys)){
		ctx.add(SEV_FATAL, CAT_IMG, "IMG-22", logical, -1, "", "IMG-архив не найден",
		        "CStreaming::AddImageToList открывает файл при старте; отсутствующий архив = игра не запускается.");
		return;
	}
	std::vector<uint8_t> hdr;
	FILE *f = nullptr;
#ifdef _WIN32
	int wn = MultiByteToWideChar(CP_UTF8, 0, phys.c_str(), -1, nullptr, 0);
	std::wstring w((size_t)wn, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, phys.c_str(), -1, &w[0], wn);
	f = _wfopen(w.c_str(), L"rb");
#else
	f = fopen(phys.c_str(), "rb");
#endif
	if(!f){
		ctx.add(SEV_FATAL, CAT_IMG, "IMG-22", logical, -1, "", "IMG-архив не открывается", "");
		return;
	}
	uint8_t h[8] = {0};
	size_t hr = fread(h, 1, 8, f);
	std::vector<uint8_t> dir;
	int n = 0;
	if(hr == 8 && memcmp(h, "VER2", 4) == 0){
		a.ver2 = true;
		n = (int)(h[4] | (h[5] << 8) | (h[6] << 16) | (h[7] << 24));
		if(n < 0 || n > 1000000){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-20", logical, -1, "", fmt("VER2: число записей %d — мусор в заголовке", n), "");
			fclose(f);
			return;
		}
		dir.resize((size_t)n * 32);
		size_t r = fread(dir.data(), 1, dir.size(), f);
		fclose(f);
		if(r != dir.size()){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-20", logical, -1, "", fmt("VER2: каталог на %d записей обрезан (прочитано %u байт)", n, (unsigned)r),
			        "Каталог читается целиком при старте; короткий каталог = мусорные записи.");
			dir.resize(r - r % 32);
			n = (int)(dir.size() / 32);
		}
	}else{
		fclose(f);
		// III / VC: .dir next to the .img
		std::string dirPath = phys.substr(0, phys.size() - 4) + ".dir";
		if(!fileExists(dirPath)){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-20", logical, -1, "", "не VER2 и нет .dir рядом — каталог архива не найден", "");
			return;
		}
		readFile(dirPath, dir);
		n = (int)(dir.size() / 32);
	}
	gd.archives.push_back(a);
	int imgIdx = (int)gd.archives.size() - 1;
	gd.archives[imgIdx].numEntries = n;
	ctx.rep->countFile(CAT_IMG);

	// archive size for bounds checks
	uint64_t imgSize = 0;
	{
		FILE *g = nullptr;
#ifdef _WIN32
		g = _wfopen(w.c_str(), L"rb");
#else
		g = fopen(phys.c_str(), "rb");
#endif
		if(g){
#ifdef _WIN32
			_fseeki64(g, 0, SEEK_END); imgSize = (uint64_t)_ftelli64(g);
#else
			fseeko(g, 0, SEEK_END); imgSize = (uint64_t)ftello(g);
#endif
			fclose(g);
		}
	}

	Buf b(dir.data(), dir.size());
	int numIpl = 0;
	for(int i = 0; i < n; i++){
		Entry e;
		e.img = imgIdx;
		e.dirIndex = i;
		if(a.ver2){
			e.offset = b.u32();
			e.sizeSectors = b.u16();
			e.sizeArchive = b.u16();
			if(e.sizeSectors == 0) e.sizeSectors = e.sizeArchive;
		}else{
			e.offset = b.u32();
			e.sizeSectors = b.u32();
		}
		const uint8_t *nm = b.ptr(24);
		if(nm == nullptr) break;
		size_t len = 0;
		while(len < 24 && nm[len]) len++;
		e.name.assign((const char*)nm, len);
		std::string where = basename(logical);
		if(len == 24){
			ctx.add(SEV_ERROR, CAT_IMG, "IMG-23", where, i, e.name,
			        fmt("имя записи №%d без NUL в 24 байтах («%s…») — игра прочитает мусор за полем", i, e.name.c_str()),
			        "Поле имени 24 байта, максимум 23 символа + NUL.");
		}
		if(e.name.empty()){
			ctx.add(SEV_WARN, CAT_IMG, "IMG-23", where, i, "", fmt("запись №%d с пустым именем", i), "");
			continue;
		}
		size_t dot = e.name.find_last_of('.');
		if(dot == std::string::npos){
			ctx.add(SEV_WARN, CAT_IMG, "IMG-23", where, i, e.name, fmt("запись «%s» без расширения — игра её пропустит", e.name.c_str()), "");
			continue;
		}
		if(dot > 20 && gd.isSA()){
			ctx.add(SEV_ERROR, CAT_IMG, "TXD-24", where, i, e.name,
			        fmt("имя «%s»: базовая часть длиннее 20 символов — CStreaming::LoadCdDirectory молча пропускает такую запись", e.name.c_str()),
			        "Ограничение 0x5B6222 (`cmp edx,0x14; jg skip`): модель/TXD с таким именем никогда не загрузится. Переименуй файл (≤ 20 символов до точки).");
		}
		e.base = lower(e.name.substr(0, dot));
		e.kind = entryKind(lower(e.name.substr(dot + 1)));
		if(e.sizeSectors == 0)
			ctx.add(SEV_ERROR, CAT_IMG, "IMG-25", where, i, e.name, fmt("запись «%s» нулевого размера", e.name.c_str()), "");
		else if(imgSize && (uint64_t)e.offset * 2048 + (uint64_t)e.sizeSectors * 2048 > imgSize)
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-25", where, i, e.name,
			        fmt("запись «%s» выходит за конец архива (смещение %u, %u секторов, размер архива %llu)", e.name.c_str(), e.offset, e.sizeSectors, (unsigned long long)imgSize),
			        "Чтение за концом файла возвращает мусор в буфер стриминга → краш при разборе.");
		if(e.kind == EK_IPL) numIpl++;
		registerEntry(ctx, e);
	}
	if(gd.isSA() && numIpl > 255)
		ctx.add(SEV_FATAL, CAT_LIMIT, "DAT-39", basename(logical), -1, "", fmt("%d streamed-IPL записей в одном архиве — пул CIplStore на 256", numIpl), "");
}

// Modloader loose files that override / add IMG entries.
static void applyLooseOverrides(Context &ctx)
{
	GameData &gd = *ctx.gd;
	for(auto it = gd.looseByName.begin(); it != gd.looseByName.end(); ++it){
		const ModFile &mf = gd.modFiles[it->second];
		std::string name = basename(mf.phys);
		Entry e;
		e.name = name;
		size_t dot = name.find_last_of('.');
		e.base = lower(dot == std::string::npos ? name : name.substr(0, dot));
		e.kind = entryKind(extOf(name));
		e.img = -1;
		e.loose = mf.phys;
		std::vector<uint8_t> tmp;
		// size: sectors from the file length
		FILE *f = nullptr;
#ifdef _WIN32
		int wn = MultiByteToWideChar(CP_UTF8, 0, mf.phys.c_str(), -1, nullptr, 0);
		std::wstring w((size_t)wn, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, mf.phys.c_str(), -1, &w[0], wn);
		f = _wfopen(w.c_str(), L"rb");
#else
		f = fopen(mf.phys.c_str(), "rb");
#endif
		if(f){
			fseek(f, 0, SEEK_END);
			long sz = ftell(f);
			fclose(f);
			e.sizeSectors = (uint32_t)((sz + 2047) / 2048);
		}
		std::string key = lower(name);
		auto prev = gd.entryByName.find(key);
		if(prev != gd.entryByName.end()){
			// override: replace the winning entry in place, keep the original for the note
			Entry &old = gd.entries[prev->second];
			e.dirIndex = old.dirIndex;
			ctx.add(SEV_INFO, CAT_IMG, "MOD-01", mf.mod, -1, name,
			        fmt("modloader: «%s» подменяет запись из %s", name.c_str(), old.img >= 0 ? basename(gd.archives[old.img].logical).c_str() : "?"), "");
			old = e;
		}else{
			if(e.base.size() > 20 && gd.isSA() && (e.kind == EK_DFF || e.kind == EK_TXD || e.kind == EK_COL || e.kind == EK_IPL || e.kind == EK_IFP))
				ctx.add(SEV_ERROR, CAT_IMG, "TXD-24", mf.mod, -1, name,
				        fmt("loose-файл «%s»: имя длиннее 20 символов — по правилам IMG игра его не зарегистрирует", name.c_str()), "");
			gd.entries.push_back(e);
			gd.entryByName[key] = (int)gd.entries.size() - 1;
		}
	}
}

// ----------------------------------------------------------------- IDE ---

static const int NUM_MODELINFO = 20000;

static void checkNameLen(Context &ctx, const std::string &file, int line, const std::string &name, int maxChars, const char *what, bool crash, int id)
{
	if((int)name.size() >= maxChars){
		ctx.add(crash ? SEV_FATAL : SEV_ERROR, CAT_IDE, "DAT-09", file, line, name,
		        fmt("%s «%s» длиннее %d символов (%d) — %s", what, name.c_str(), maxChars - 1, (int)name.size(),
		            crash ? "sscanf переполняет стековый буфер, краш при загрузке IDE" : "имя ломает соседний буфер, модель никогда не загрузится"),
		        "Буферы имён в CFileLoader — 24 байта без ограничения ширины (§3.11 textdata_path.md). Переименуй.", id);
	}
}

static void addObj(Context &ctx, ObjDef &o)
{
	GameData &gd = *ctx.gd;
	if(o.id < 0 || o.id >= gd.modelInfoSize()){
		ctx.add(SEV_FATAL, CAT_IDE, "DAT-06", o.file, o.line, o.name,
		        fmt("id %d вне диапазона 0..%d — запись мимо ms_modelInfoPtrs", o.id, gd.modelInfoSize() - 1),
		        gd.isSA() ? "CModelInfo::Add*Model пишет ms_modelInfoPtrs[id] без проверки; id ≥ 20000 к тому же совпадает с id TXD/COL/IPL стриминга."
		                  : "CModelInfo::Add*Model пишет ms_modelInfoPtrs[id] без проверки (MODELINFOSIZE: III 5500, VC 6500); id ≥ размера к тому же совпадает с id TXD стриминга.", o.id);
	}
	auto it = gd.objById.find(o.id);
	if(it != gd.objById.end()){
		const ObjDef &p = gd.objs[it->second];
		ctx.add(SEV_ERROR, CAT_IDE, "DAT-07", o.file, o.line, o.name,
		        fmt("id %d уже определён как «%s» (%s:%d) — второе определение молча заменит первое", o.id, p.name.c_str(), basename(p.file).c_str(), p.line),
		        "Первая modelinfo утекает, а её слот в хранилище всё равно потрачен. Оставь одно определение.", o.id);
	}
	if(o.type < OT_NUM) gd.numDefs[o.type]++;
	gd.objs.push_back(o);
	int idx = (int)gd.objs.size() - 1;
	gd.objById[o.id] = idx;
	std::string ln = lower(o.name);
	if(gd.objByName.find(ln) == gd.objByName.end()) gd.objByName[ln] = idx;
	else{
		const ObjDef &p = gd.objs[gd.objByName[ln]];
		if(p.id != o.id)
			ctx.add(SEV_INFO, CAT_IDE, "IDE-02", o.file, o.line, o.name,
			        fmt("имя «%s» уже используется id %d (%s:%d) — COL и IMG ищут модель по имени, попадёт первая", o.name.c_str(), p.id, basename(p.file).c_str(), p.line), "", o.id);
	}
}

static const char *carTypes[] = { "car","mtruck","quad","heli","plane","boat","train","f_heli","f_plane","bike","bmx","trailer", nullptr };
static const char *carClasses[] = { "normal","poorfamily","richfamily","executive","worker","big","taxi","moped","motorbike","leisureboat","workerboat","bicycle","ignore", nullptr };

static bool inList(const char **list, const std::string &s)
{
	for(int i = 0; list[i]; i++) if(s == list[i]) return true;
	return false;
}

static void loadIde(Context &ctx, const std::string &logical, const std::string &phys)
{
	GameData &gd = *ctx.gd;
	std::vector<Line> lines;
	if(!loadLines(phys, lines)){
		ctx.add(SEV_FATAL, CAT_DAT, "DAT-03", logical, -1, "", "IDE-файл не найден или не открывается",
		        "LoadObjectTypes не проверяет результат OpenFile и делает fgets(NULL) → краш CRT при старте.");
		return;
	}
	ctx.rep->countFile(CAT_IDE);
	gd.ideFiles.push_back(logical);
	std::string sect;
	int fxCount = 0;
	int lastFxModel = -1;
	std::set<int> fxModelsSeen;
	bool sa = gd.isSA();
	for(size_t i = 0; i < lines.size(); i++){
		Line &l = lines[i];
		if(l.tooLong)
			ctx.add(SEV_ERROR, CAT_IDE, "DAT-01", logical, l.number, "", "строка длиннее 511 символов — движок разрежет её на две записи", "");
		if(l.comment) continue;
		if(l.raw.compare(0, 2, "//") == 0 || l.raw[0] == ';'){
			ctx.add(SEV_WARN, CAT_IDE, "DAT-02", logical, l.number, "", "строка начинается с «//» или «;» — для IDE это не комментарий, она будет разобрана как данные",
			        "Комментарий только «#» первым непробельным символом.");
		}
		const std::vector<std::string> &t = l.tok;
		if(t.empty()) continue;
		if(sect.empty()){
			std::string h = t[0].size() >= 4 ? t[0].substr(0, 4) : t[0];
			if(h == "objs" || h == "tobj" || h == "weap" || h == "hier" || h == "anim" || h == "cars" || h == "peds" || h == "path" || h == "2dfx" || h == "txdp"){
				sect = h;
				if(sect == "path" && sa)
					ctx.add(SEV_INFO, CAT_IDE, "DAT-17", logical, l.number, "", "секция path в IDE — SA её разбирает и выбрасывает (пути только из nodes*.dat)", "");
				continue;
			}
			if(t[0] == "end") continue;
			ctx.add(SEV_WARN, CAT_IDE, "DAT-18", logical, l.number, "", fmt("строка «%s» вне секции — игнорируется (неизвестный заголовок?)", l.raw.substr(0, 40).c_str()), "");
			continue;
		}
		if(t[0] == "end" && t.size() == 1){ sect.clear(); continue; }

		if(sect == "objs" || sect == "tobj"){
			ObjDef o;
			o.file = logical;
			o.line = l.number;
			o.type = sect == "objs" ? OT_OBJS : OT_TOBJ;
			bool timed = o.type == OT_TOBJ;
			int extra = timed ? 2 : 0;
			if(t.size() < 4 || !isInt(t[0])){
				ctx.add(SEV_ERROR, CAT_IDE, "DAT-08b", logical, l.number, "", "меньше 4 полей или id не число — строка молча отброшена", "", -1);
				continue;
			}
			o.id = toInt(t[0]);
			o.name = t[1];
			o.txd = t[2];
			bool legacy = false;
			// SA native: id name txd dd flags [timeOn timeOff]
			if(sa && t.size() >= (size_t)(5 + extra) && isNum(t[3]) && toF(t[3]) >= 4.0f){
				o.numAtomics = 1;
				o.drawDist[0] = toF(t[3]);
				if(!isInt(t[4])) ctx.add(SEV_ERROR, CAT_IDE, "DAT-08b", logical, l.number, o.name, "флаги не число", "", o.id);
				o.flags = toInt(t[4]);
				if(timed){ o.timeOn = toInt(t[5]); o.timeOff = toInt(t[6]); }
			}else{
				// legacy III/VC form: id name txd count dd[count] flags [timeOn timeOff]
				legacy = true;
				if(!isInt(t[3])){
					if(sa && isNum(t[3]) && toF(t[3]) < 4.0f)
						ctx.add(SEV_ERROR, CAT_IDE, "DAT-08", logical, l.number, o.name,
						        fmt("дистанция прорисовки %s < 4.0 — SA перечитает строку как старый формат с числом мешей и получит мусор", t[3].c_str()),
						        "LoadObject: `n != 5 || dd < 4.0` → sscanf(\"%d %s %s %d\") читает целую часть дистанции как count. Ставь ≥ 4.", o.id);
					else
						ctx.add(SEV_ERROR, CAT_IDE, "DAT-08b", logical, l.number, o.name, "4-е поле не число — строка отброшена", "", o.id);
					continue;
				}
				int count = toInt(t[3]);
				if(count < 1 || count > 3){
					if(sa && t.size() == (size_t)(4 + extra)){
						ctx.add(SEV_ERROR, CAT_IDE, "DAT-08b", logical, l.number, o.name,
						        fmt("только %d полей (нет флагов): SA берёт count=%d, дистанция и флаги остаются мусором со стека", (int)t.size(), count),
						        "Допиши поле флагов (обычно 0).", o.id);
						o.numAtomics = 1; o.drawDist[0] = (float)count; o.flags = 0;
						addObj(ctx, o);
					}else
						ctx.add(SEV_ERROR, CAT_IDE, "DAT-08b", logical, l.number, o.name, fmt("число мешей %d вне 1..3 — строка отброшена", count), "", o.id);
					continue;
				}
				if(t.size() < (size_t)(5 + count + extra)){
					ctx.add(SEV_WARN, CAT_IDE, "DAT-08b", logical, l.number, o.name,
					        fmt("ожидалось %d полей для %d мешей, найдено %d — недостающие поля (флаги/часы) остаются мусором со стека", 5 + count + extra, count, (int)t.size()), "Допиши флаги (обычно 0).", o.id);
				}
				o.numAtomics = count;
				for(int k = 0; k < count && 4 + k < (int)t.size(); k++) o.drawDist[k] = toF(t[(size_t)(4 + k)]);
				o.flags = 4 + count < (int)t.size() ? toInt(t[(size_t)(4 + count)]) : 0;
				if(timed && 6 + count < (int)t.size()){ o.timeOn = toInt(t[(size_t)(5 + count)]); o.timeOff = toInt(t[(size_t)(6 + count)]); }
				if(sa && count > 1)
					ctx.add(SEV_INFO, CAT_IDE, "DAT-08", logical, l.number, o.name, "старый формат с несколькими дистанциями — SA хранит только первую", "", o.id);
			}
			(void)legacy;
			checkNameLen(ctx, logical, l.number, o.name, 24, "имя модели", false, o.id);
			checkNameLen(ctx, logical, l.number, o.txd, 24, "имя TXD", true, o.id);
			if(sa){
				unsigned known = 0x1|0x4|0x8|0x40|0x80|0x200|0x400|0x800|0x1000|0x2000|0x4000|0x8000|0x20|0x80000|0x100000|0x200000|0x400000|0x2|0x10|0x100|0x10000|0x20000|0x40000;
				if((unsigned)o.flags & ~known)
					ctx.add(SEV_INFO, CAT_IDE, "DAT-14", logical, l.number, o.name, fmt("флаги 0x%X содержат биты, которые SA не читает", o.flags), "", o.id);
				if(o.drawDist[0] < 2.0f)
					ctx.add(SEV_WARN, CAT_IDE, "DAT-25b", logical, l.number, o.name, fmt("дистанция прорисовки %.1f < 2 — все экземпляры создаются невидимыми", o.drawDist[0]), "", o.id);
			}
			if(timed && (o.timeOn < 0 || o.timeOn > 24 || o.timeOff < 0 || o.timeOff > 24))
				ctx.add(SEV_WARN, CAT_IDE, "IDE-04", logical, l.number, o.name, fmt("часы %d..%d вне 0..24", o.timeOn, o.timeOff), "", o.id);
			addObj(ctx, o);
		}else if(sect == "hier"){
			if(t.size() < 3 || !isInt(t[0])){
				ctx.add(SEV_ERROR, CAT_IDE, "DAT-08b", logical, l.number, "", fmt("hier: нужно минимум 3 поля, найдено %d — строка отброшена", (int)t.size()), "");
				continue;
			}
			ObjDef o; o.file = logical; o.line = l.number; o.type = OT_HIER;
			o.id = toInt(t[0]); o.name = t[1]; o.txd = t[2];
			o.drawDist[0] = 2000.0f;
			checkNameLen(ctx, logical, l.number, o.name, 24, "имя модели", false, o.id);
			checkNameLen(ctx, logical, l.number, o.txd, 24, "имя TXD", true, o.id);
			addObj(ctx, o);
		}else if(sect == "anim"){
			if(t.size() < 6 || !isInt(t[0])){
				ctx.add(SEV_ERROR, CAT_IDE, "DAT-08b", logical, l.number, "", fmt("anim: нужно 6 полей, найдено %d — строка отброшена", (int)t.size()), "");
				continue;
			}
			ObjDef o; o.file = logical; o.line = l.number; o.type = OT_ANIM;
			o.id = toInt(t[0]); o.name = t[1]; o.txd = t[2]; o.anim = t[3];
			o.drawDist[0] = toF(t[4]); o.flags = toInt(t[5]);
			checkNameLen(ctx, logical, l.number, o.name, 24, "имя модели", false, o.id);
			checkNameLen(ctx, logical, l.number, o.txd, 24, "имя TXD", true, o.id);
			if(o.anim.size() >= 16)
				ctx.add(SEV_ERROR, CAT_IDE, "DAT-10", logical, l.number, o.name, fmt("имя анимации «%s» ≥ 16 символов — затирает имя модели на стеке, модель не загрузится", o.anim.c_str()), "", o.id);
			addObj(ctx, o);
		}else if(sect == "weap"){
			if(t.size() < 6 || !isInt(t[0])){
				ctx.add(SEV_FATAL, CAT_IDE, "DAT-11", logical, l.number, "", "weap: некорректная строка — LoadWeaponObject не проверяет sscanf, AddWeaponModel(мусор) → краш", "");
				continue;
			}
			ObjDef o; o.file = logical; o.line = l.number; o.type = OT_WEAP;
			o.id = toInt(t[0]); o.name = t[1]; o.txd = t[2]; o.anim = t[3];
			o.drawDist[0] = t.size() > 5 ? toF(t[5]) : 100.0f;
			checkNameLen(ctx, logical, l.number, o.name, 24, "имя модели", false, o.id);
			checkNameLen(ctx, logical, l.number, o.txd, 24, "имя TXD", false, o.id);
			addObj(ctx, o);
		}else if(sect == "cars"){
			size_t need = sa ? 15 : (gd.game == GAME_VC ? 12 : 11);
			if(t.size() < (sa ? 11u : 8u) || !isInt(t[0])){
				ctx.add(SEV_FATAL, CAT_IDE, "DAT-11", logical, l.number, "", fmt("cars: некорректная строка (%d полей, нужно %d) — id остаётся -1, запись в ms_modelInfoPtrs[-1]", (int)t.size(), (int)need), "");
				continue;
			}
			ObjDef o; o.file = logical; o.line = l.number; o.type = OT_CARS;
			o.id = toInt(t[0]); o.name = t[1]; o.txd = t[2]; o.carType = t[3]; o.carHandling = t[4];
			o.tok = t;
			o.drawDist[0] = 300.0f;
			if(sa){
				if(t.size() < 15)
					ctx.add(SEV_WARN, CAT_IDE, "DAT-11", logical, l.number, o.name, fmt("cars: %d полей вместо 15 — масштаб колёс (и прочие недостающие поля) = мусор со стека: LoadVehicleObject пишет их из неинициализированных локалов, предустановлены только id и upgradeClass (−1)", (int)t.size()),
					        "Ванильный skimmer (plane) имеет 11 полей — уровень Предупреждение.", o.id);
				o.anim = t.size() > 6 ? t[6] : "";
				o.carClass = t.size() > 7 ? t[7] : "";
				if(!inList(carTypes, o.carType))
					ctx.add(SEV_ERROR, CAT_IDE, "DAT-13", logical, l.number, o.name, fmt("тип машины «%s» неизвестен — останется значение по умолчанию", o.carType.c_str()), "car mtruck quad heli plane boat train f_heli f_plane bike bmx trailer", o.id);
				if(!inList(carClasses, o.carClass))
					ctx.add(SEV_WARN, CAT_IDE, "DAT-13", logical, l.number, o.name, fmt("класс машины «%s» неизвестен", o.carClass.c_str()), "normal poorfamily richfamily executive worker big taxi moped motorbike leisureboat workerboat bicycle ignore", o.id);
				if(t.size() > 10 && !isHex(t[10]))
					ctx.add(SEV_WARN, CAT_IDE, "DAT-11", logical, l.number, o.name, "поле compRules должно быть hex", "", o.id);
			}
			checkNameLen(ctx, logical, l.number, o.name, 24, "имя модели", false, o.id);
			checkNameLen(ctx, logical, l.number, o.txd, 24, "имя TXD", true, o.id);
			addObj(ctx, o);
		}else if(sect == "peds"){
			if(t.size() < (sa ? 8u : 6u) || !isInt(t[0])){
				ctx.add(SEV_FATAL, CAT_IDE, "DAT-11", logical, l.number, "", "peds: некорректная строка — id остаётся -1, запись в ms_modelInfoPtrs[-1]", "");
				continue;
			}
			ObjDef o; o.file = logical; o.line = l.number; o.type = OT_PEDS;
			o.id = toInt(t[0]); o.name = t[1]; o.txd = t[2]; o.pedType = t[3]; o.pedStats = t[4]; o.pedAnimGroup = t[5];
			o.tok = t;
			o.drawDist[0] = 300.0f;
			if(sa){
				if(t.size() != 14)
					ctx.add(SEV_WARN, CAT_IDE, "DAT-11", logical, l.number, o.name, fmt("peds: %d полей вместо 14 — хвостовые поля получат мусор", (int)t.size()), "", o.id);
				o.anim = t.size() > 8 ? t[8] : "";
				if(t.size() > 6 && !isHex(t[6])) ctx.add(SEV_WARN, CAT_IDE, "DAT-11", logical, l.number, o.name, "carsCanDrive должно быть hex", "", o.id);
				if(t.size() > 7 && !isHex(t[7])) ctx.add(SEV_WARN, CAT_IDE, "DAT-11", logical, l.number, o.name, "flags должно быть hex", "", o.id);
				if(o.anim.size() >= 16) ctx.add(SEV_ERROR, CAT_IDE, "DAT-09", logical, l.number, o.name, "animFile ≥ 16 символов — переполнение буфера", "", o.id);
				if(o.pedAnimGroup.size() >= 24) ctx.add(SEV_ERROR, CAT_IDE, "DAT-09", logical, l.number, o.name, "animGroup ≥ 24 символов — переполнение буфера", "", o.id);
			}
			checkNameLen(ctx, logical, l.number, o.name, 24, "имя модели", false, o.id);
			checkNameLen(ctx, logical, l.number, o.txd, 24, "имя TXD", true, o.id);
			addObj(ctx, o);
		}else if(sect == "txdp"){
			if(t.size() < 2){
				ctx.add(SEV_WARN, CAT_IDE, "DAT-08b", logical, l.number, "", "txdp: нужно 2 поля", "");
				continue;
			}
			if(t[0].size() >= 32 || t[1].size() >= 32)
				ctx.add(SEV_FATAL, CAT_IDE, "DAT-09", logical, l.number, t[0], "txdp: имя ≥ 32 символов — переполнение стека", "");
			int child = gd.txdSlotOf(t[0]);
			int parent = gd.txdSlotOf(t[1]);
			gd.txdSlots[child].fromIde = true;
			gd.txdSlots[parent].fromIde = true;
			if(child == parent)
				ctx.add(SEV_FATAL, CAT_IDE, "TXD-26", logical, l.number, t[0], "txdp: TXD назначен родителем самому себе — никогда не загрузится", "");
			else{
				if(gd.txdSlots[child].parent >= 0 && gd.txdSlots[child].parent != parent)
					ctx.add(SEV_WARN, CAT_IDE, "TXD-25", logical, l.number, t[0], fmt("txdp: у «%s» уже был родитель «%s», заменён на «%s»", t[0].c_str(), gd.txdSlots[gd.txdSlots[child].parent].shown.c_str(), t[1].c_str()), "");
				gd.txdSlots[child].parent = parent;
			}
		}else if(sect == "2dfx"){
			gd.total2dfx++;
			if(!sa){ continue; }
			if(t.size() < 5 || !isInt(t[0])){
				ctx.add(SEV_FATAL, CAT_IDE, "DAT-16", logical, l.number, "", "2dfx: некорректная строка — modelId со стека, Add2dEffect по мусорному указателю", "");
				continue;
			}
			int mid = toInt(t[0]);
			fxCount++;
			if(gd.objById.find(mid) == gd.objById.end())
				ctx.add(SEV_FATAL, CAT_IDE, "DAT-16", logical, l.number, "", fmt("2dfx для модели %d, которая не определена (или определена позже) — ms_modelInfoPtrs[id] == NULL → краш", mid), "", mid);
			if(mid != lastFxModel){
				if(fxModelsSeen.count(mid))
					ctx.add(SEV_ERROR, CAT_IDE, "DAT-16", logical, l.number, "", fmt("2dfx модели %d идут не подряд — Add2dEffect хранит только первый индекс и счётчик", mid), "", mid);
				fxModelsSeen.insert(mid);
				lastFxModel = mid;
			}
			if(fxCount > 100)
				ctx.add(SEV_FATAL, CAT_LIMIT, "DAT-16", logical, l.number, "", fmt("больше 100 эффектов в IDE-секциях 2dfx (%d) — CStore<C2dEffect,100> без проверки", fxCount), "");
			if(t.size() > 8 && toInt(t[8]) == 0){
				// light: two quoted names must be closed
				int q = 0;
				for(size_t k = 0; k < l.raw.size(); k++) if(l.raw[k] == '"') q++;
				if(q % 2)
					ctx.add(SEV_FATAL, CAT_IDE, "DAT-16", logical, l.number, "", "2dfx light: незакрытая кавычка — поиск «\"» уходит за буфер строки", "", mid);
			}
		}
		// path: ignored
	}
	if(!sect.empty())
		ctx.add(SEV_WARN, CAT_IDE, "IDE-01", logical, (int)lines.size(), "", fmt("секция «%s» не закрыта строкой end", sect.c_str()), "");
}

// ----------------------------------------------------------------- IPL ---

static const int PICKUP_OK_MIN = 4, PICKUP_OK_MAX = 0x37;
static bool pickupIdOk(int id)
{
	if(id < PICKUP_OK_MIN || id > PICKUP_OK_MAX) return false;
	if(id == 7 || id == 8 || id == 0x1E || id == 0x2A) return false;
	return true;
}

static void checkInstCommon(Context &ctx, IplFile &ipl, Inst &in, const std::string &logical, int line, int thisFileInstCount)
{
	GameData &gd = *ctx.gd;
	bool sa = gd.isSA();
	std::string who = in.name.empty() ? fmt("id %d", in.id) : in.name;
	if(in.id < 0 || in.id >= gd.modelInfoSize()){
		ctx.add(SEV_FATAL, CAT_IPL, "DAT-21", logical, line, who, fmt("id модели %d вне 0..%d — чтение мимо ms_modelInfoPtrs, вызов по мусорному указателю", in.id, gd.modelInfoSize() - 1), "", in.id);
		return;
	}
	const ObjDef *def = gd.findObj(in.id);
	if(def == nullptr){
		ctx.add(SEV_FATAL, CAT_IPL, "DAT-21", logical, line, who,
		        fmt("модель %d не определена ни в одном IDE — LoadObjectInstance вернёт NULL, LinkLods/LoadIpl разыменуют его", in.id),
		        "Проверь, что IDE с этой моделью перечислен в gta.dat ДО IPL и что id совпадает.", in.id);
		return;
	}
	ObjDef &d = gd.objs[gd.objById[in.id]];
	d.numInstances++;
	if(sa){
		if(!in.name.empty() && !ieq(in.name, d.name))
			ctx.add(SEV_WARN, CAT_IPL, "IPL-02", logical, line, in.name, fmt("имя «%s» не совпадает с IDE («%s», id %d) — SA смотрит только на id", in.name.c_str(), d.name.c_str(), in.id), "", in.id);
		float n2 = in.rot[0]*in.rot[0] + in.rot[1]*in.rot[1] + in.rot[2]*in.rot[2] + in.rot[3]*in.rot[3];
		if(!(n2 == n2) || fabsf(in.rot[3]) > 1.0001f || n2 < 0.5f || n2 > 1.5f)
			ctx.add(SEV_ERROR, CAT_IPL, "DAT-23", logical, line, d.name, fmt("кватернион (%.3f %.3f %.3f %.3f) не единичный (|q|²=%.3f) — acos(rw) даст NaN-матрицу", in.rot[0], in.rot[1], in.rot[2], in.rot[3], n2), "", in.id);
		else if(fabsf(n2 - 1.0f) > 0.01f)
			ctx.add(SEV_WARN, CAT_IPL, "DAT-23", logical, line, d.name, fmt("кватернион не нормирован (|q|²=%.3f)", n2), "", in.id);
		if(in.interior > 0xFFFF || in.interior < 0)
			ctx.add(SEV_WARN, CAT_IPL, "DAT-24", logical, line, d.name, fmt("interior %d: код зоны хранится в байте, старшие биты вне 0x100..0x1000 игнорируются", in.interior), "", in.id);
		else if((in.interior & 0xFF) > 18 && (in.interior & 0xFF) != 0)
			ctx.add(SEV_INFO, CAT_IPL, "DAT-24", logical, line, d.name, fmt("interior %d — нестандартный код интерьера (ваниль 0..18)", in.interior & 0xFF), "", in.id);
		if(fabsf(in.pos[0]) > 3000.0f || fabsf(in.pos[1]) > 3000.0f)
			ctx.add(SEV_WARN, CAT_IPL, "DAT-25", logical, line, d.name, fmt("позиция (%.1f, %.1f) вне ±3000 — CEntity::Add зажмёт её в крайний сектор (стриминг/коллизия сломаны)", in.pos[0], in.pos[1]), "", in.id);
		if(!(in.pos[0] == in.pos[0]) || !(in.pos[1] == in.pos[1]) || !(in.pos[2] == in.pos[2]))
			ctx.add(SEV_FATAL, CAT_IPL, "DAT-25", logical, line, d.name, "позиция NaN", "", in.id);
		if(in.lod != -1){
			if(in.lod < -1 || in.lod >= thisFileInstCount){
				ctx.add(SEV_FATAL, CAT_IPL, "DAT-22", logical, line, d.name,
				        fmt("lod = %d, а в файле%s только %d inst-строк — LinkLods прочитает указатель мимо массива", in.lod, ipl.streamed ? " (текстовом IPL с тем же именем)" : "", thisFileInstCount),
				        "Индекс LOD считается по активным inst-строкам того же текстового IPL (комментарии не считаются).", in.id);
			}else if(!ipl.streamed && in.lod == in.ordinal)
				ctx.add(SEV_ERROR, CAT_IPL, "DAT-22", logical, line, d.name, "объект назначен LOD-ом самому себе", "", in.id);
		}
	}
}

// DAT-22 plausibility of an in-range lod index: the engine only follows the number, so a wrong but
// valid index silently links the object to some unrelated line. Vanilla: 1418 of 1419 LOD targets sit
// within 1 unit of the object (the exception is a shared train-track LOD at 52); mod maps share one
// LOD between several objects up to ~120 apart. A LOD whose draw distance is below the object's own
// never shows: the object streams out first and nothing replaces it.
static void checkLodTarget(Context &ctx, GameData &gd, const Inst &in, const Inst &lod, const std::string &logical)
{
	const ObjDef *co = gd.findObj(in.id), *lo = gd.findObj(lod.id);
	if(lo == nullptr) return;
	float dx = in.pos[0] - lod.pos[0], dy = in.pos[1] - lod.pos[1], dz = in.pos[2] - lod.pos[2];
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	if(dist > 200.0f)
		ctx.add(SEV_WARN, CAT_IPL, "DAT-22", logical, in.line, in.name,
		        fmt("lod %d → «%s» стоит в %.0f ед. от объекта — LOD обычно в той же точке; похоже, индекс указывает не на ту строку", in.lod, lo->name.c_str(), dist),
		        "Индекс lod — номер inst-строки (с 0, без комментариев) в этом же текстовом IPL.", in.id);
	if(co && lo->maxDrawDist() > 0 && lo->maxDrawDist() < co->maxDrawDist())
		ctx.add(SEV_WARN, CAT_IPL, "DAT-22", logical, in.line, in.name,
		        fmt("lod %d → «%s»: дистанция прорисовки LOD %.0f меньше, чем у объекта (%.0f) — объект исчезнет раньше, чем появится LOD", in.lod, lo->name.c_str(), lo->maxDrawDist(), co->maxDrawDist()),
		        "", in.id);
}

static void finishTextIplLods(Context &ctx, IplFile &ipl, const std::string &logical)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	// LOD targets must resolve to an instance whose model is defined
	std::map<int,int> children;
	for(int i = 0; i < ipl.numInst; i++){
		Inst &in = gd.insts[ipl.firstInst + i];
		if(in.lod < 0 || in.lod >= ipl.numInst) continue;
		Inst &lod = gd.insts[ipl.firstInst + in.lod];
		if(gd.findObj(lod.id) == nullptr)
			ctx.add(SEV_FATAL, CAT_IPL, "DAT-22", logical, in.line, in.name, fmt("lod %d указывает на строку, чья модель %d не определена (NULL) — краш в LinkLods", in.lod, lod.id), "", in.id);
		else{
			gd.objs[gd.objById[lod.id]].isLod = true;
			gd.objs[gd.objById[lod.id]].lodChildren++;
			checkLodTarget(ctx, gd, in, lod, logical);
		}
		children[in.lod]++;
	}
	for(auto it = children.begin(); it != children.end(); ++it){
		if(it->second > 255){
			Inst &lod = gd.insts[ipl.firstInst + it->first];
			ctx.add(SEV_ERROR, CAT_IPL, "DAT-22", logical, lod.line, lod.name, fmt("у LOD-объекта %d детей — счётчик m_nNumLodChildren байтовый, переполнится", it->second), "", lod.id);
		}
	}
}

static int findTextIplByBase(GameData &gd, const std::string &baseLower)
{
	for(size_t i = 0; i < gd.ipls.size(); i++)
		if(!gd.ipls[i].streamed && !gd.ipls[i].binary && lower(stem(gd.ipls[i].logical)) == baseLower)
			return (int)i;
	return -1;
}

static void loadTextIplLines(Context &ctx, const std::string &logical, const std::string &phys, std::vector<Line> &lines, bool streamed, int entryIdx)
{
	GameData &gd = *ctx.gd;
	ctx.rep->countFile(CAT_IPL);
	IplFile ipl;
	ipl.logical = logical;
	ipl.phys = phys;
	ipl.streamed = streamed;
	ipl.entry = entryIdx;
	ipl.firstInst = (int)gd.insts.size();
	bool sa = gd.isSA();
	bool isInterior = lower(logical).size() >= 7 && lower(logical).compare(lower(logical).size() - 7, 7, "int.ipl") == 0;
	std::string sect;
	std::vector<Inst> pending;	// text inst lines in order
	bool aborted = false;
	int textParentCount = -1;
	if(streamed){
		std::string b = lower(stem(logical));
		size_t s = b.find("_stream");
		if(s != std::string::npos) b = b.substr(0, s);
		ipl.textParent = findTextIplByBase(gd, b);
		if(ipl.textParent >= 0) textParentCount = gd.ipls[ipl.textParent].numInst;
	}
	for(size_t i = 0; i < lines.size() && !aborted; i++){
		Line &l = lines[i];
		if(l.tooLong)
			ctx.add(SEV_ERROR, CAT_IPL, "DAT-01", logical, l.number, "", "строка длиннее 511 символов — движок разрежет её на две записи", "");
		if(l.comment) continue;
		if(l.raw.compare(0, 2, "//") == 0 || l.raw[0] == ';')
			ctx.add(SEV_WARN, CAT_IPL, "DAT-02", logical, l.number, "", "строка начинается с «//» или «;» — для IPL это не комментарий", "");
		const std::vector<std::string> &t = l.tok;
		if(t.empty()) continue;
		if(sect.empty()){
			std::string h = t[0].size() >= 4 ? t[0].substr(0, 4) : t[0];
			static const char *hdrs[] = { "inst","mult","zone","cull","path","occl","grge","enex","pick","cars","jump","tcyc","auzo", nullptr };
			if(inList(hdrs, h)){
				sect = h;
				if(sa && streamed && sect != "inst")
					ctx.add(SEV_WARN, CAT_IPL, "DAT-38", logical, l.number, "", fmt("секция %s в streamed-IPL — CIplStore::LoadIpl читает только inst, секция игнорируется", sect.c_str()), "");
				if(sa && !streamed && sect == "mult")
					ctx.add(SEV_INFO, CAT_IPL, "DAT-19", logical, l.number, "", "секция mult принимается, но не разбирается", "");
				continue;
			}
			if(t[0] == "end") continue;
			ctx.add(SEV_WARN, CAT_IPL, "DAT-18", logical, l.number, "", fmt("строка «%s» вне секции — игнорируется", l.raw.substr(0, 40).c_str()), "");
			continue;
		}
		if(t[0] == "end" && t.size() == 1){ sect.clear(); continue; }
		if(sect == "path" && sa && !streamed){
			// does any non-path section with data follow? then it is lost
			bool lostData = false;
			{
				std::string cs = "path";
				for(size_t k = i + 1; k < lines.size() && !lostData; k++){
					const std::vector<std::string> &tt = lines[k].tok;
					if(lines[k].comment || tt.empty()) continue;
					std::string hh = tt[0].size() >= 4 ? tt[0].substr(0, 4) : tt[0];
					if(tt[0] == "end" && tt.size() == 1){ cs.clear(); continue; }
					if(cs.empty()){
						if(hh == "inst" || hh == "zone" || hh == "cull" || hh == "occl" || hh == "grge" || hh == "enex" || hh == "pick" || hh == "cars" || hh == "jump" || hh == "tcyc" || hh == "auzo" || hh == "path" || hh == "mult") cs = hh;
						continue;
					}
					if(cs != "path" && cs != "mult") lostData = true;
				}
			}
			ctx.add(lostData ? SEV_ERROR : SEV_INFO, CAT_IPL, "DAT-19", logical, l.number, "",
			        lostData ? "секция path с данными в SA IPL: первая же строка данных прерывает разбор ОСТАЛЬНОГО файла — секции ниже потеряны"
			                 : "секция path с данными — SA прекращает разбор файла на ней (пути берутся из nodes*.dat; ниже ничего полезного нет)",
			        "LoadScene: if (state == 1) break; — всё после этой строки не будет прочитано.");
			aborted = true; break;
		}

		if(sect == "inst"){
			Inst in;
			in.line = l.number;
			in.fileIdx = (int)gd.ipls.size();
			in.ordinal = (int)pending.size();
			if(sa){
				if(t.size() < 11 || !isInt(t[0])){
					ctx.add(SEV_FATAL, CAT_IPL, "DAT-20", logical, l.number, t.size() > 1 ? t[1] : "",
					        fmt("inst: %d полей вместо 11 — незаполненные поля (lod!) остаются мусором со стека, LinkLods упадёт", (int)t.size()),
					        "Формат SA: id name interior x y z rx ry rz rw lod.");
					in.id = t.empty() ? -1 : toInt(t[0]);
					pending.push_back(in);
					continue;
				}
				in.id = toInt(t[0]); in.name = t[1]; in.interior = toInt(t[2]);
				for(int k = 0; k < 3; k++) in.pos[k] = toF(t[3 + k]);
				for(int k = 0; k < 4; k++) in.rot[k] = toF(t[6 + k]);
				in.lod = toInt(t[10]);
				if(t.size() > 11)
					ctx.add(SEV_INFO, CAT_IPL, "DAT-20", logical, l.number, in.name, fmt("inst: %d полей, лишние игнорируются", (int)t.size()), "", in.id);
				for(size_t k = 2; k < 11; k++) if(!isNum(t[k])){
					ctx.add(SEV_ERROR, CAT_IPL, "DAT-20", logical, l.number, in.name, fmt("inst: поле %d («%s») не число", (int)k + 1, t[k].c_str()), "", in.id);
					break;
				}
			}else{
				if(t.size() == 13){
					in.id = toInt(t[0]); in.name = t[1]; in.interior = toInt(t[2]);
					if(in.interior == 13) in.interior = 0;	// VC: the field is the area; 13 = AREA_EVERYWHERE (IsAreaVisible → always) — outdoor land/roads use it
					for(int k = 0; k < 3; k++) in.pos[k] = toF(t[3 + k]);
					for(int k = 0; k < 3; k++) in.scale[k] = toF(t[6 + k]);
					for(int k = 0; k < 4; k++) in.rot[k] = toF(t[9 + k]);
				}else if(t.size() == 12){
					in.id = toInt(t[0]); in.name = t[1];
					for(int k = 0; k < 3; k++) in.pos[k] = toF(t[2 + k]);
					for(int k = 0; k < 3; k++) in.scale[k] = toF(t[5 + k]);
					for(int k = 0; k < 4; k++) in.rot[k] = toF(t[8 + k]);
				}else{
					ctx.add(SEV_ERROR, CAT_IPL, "DAT-20", logical, l.number, "", fmt("inst: %d полей (ожидалось 12 или 13)", (int)t.size()), "");
					continue;
				}
			}
			pending.push_back(in);
		}else if(!sa || streamed){
			// III/VC: только счётчики зон (CTheZones::CreateZone пишет в массивы фиксированного размера без проверки)
			if(!sa && sect == "zone" && t.size() >= 9){
				int type = toInt(t[1]);
				if(type == 3){
					gd.totalMapZones++;
					GameData::LevelZone z; z.x0 = toF(t[2]); z.y0 = toF(t[3]); z.x1 = toF(t[5]); z.y1 = toF(t[6]); z.level = toInt(t[8]);
					if(z.x0 > z.x1) std::swap(z.x0, z.x1); if(z.y0 > z.y1) std::swap(z.y0, z.y1);
					gd.mapZones.push_back(z);
				}
				else if(type == 2) gd.totalInfoZones++;
				else gd.totalNaviZones++;
			}
			continue;	// only SA text IPL sections are rule-checked
		}else if(sect == "zone"){
			if(t.size() < 10){
				ctx.add(SEV_ERROR, CAT_IPL, "DAT-27", logical, l.number, t.empty() ? "" : t[0], fmt("zone: %d полей вместо 10 — SA вызывает CreateZone только когда sscanf вернул 10, зона отброшена", (int)t.size()),
				        "name type x1 y1 z1 x2 y2 z2 level gxt-name");
				continue;
			}
			ipl.numZone++;
			{ ZoneRef z; z.name = t[0]; z.gxt = t[9]; z.file = logical; z.line = l.number; gd.zones.push_back(z); }
			int type = toInt(t[1]);
			if(t[0].size() > 7) ctx.add(SEV_WARN, CAT_IPL, "DAT-27", logical, l.number, t[0], fmt("имя зоны «%s» длиннее 7 символов — обрежется (strncpy 7)", t[0].c_str()), "");
			if(t[9].size() >= 12) ctx.add(SEV_FATAL, CAT_IPL, "DAT-27b", logical, l.number, t[0], fmt("gxt-имя «%s» ≥ 12 символов — переполняет буфер (границы зоны/адрес возврата)", t[9].c_str()), "");
			else if(t[9].size() > 7) ctx.add(SEV_WARN, CAT_IPL, "DAT-27", logical, l.number, t[0], fmt("gxt-имя «%s» длиннее 7 символов — обрежется", t[9].c_str()), "");
			if(type == 0 || type == 1){ gd.totalNaviZones++; }
			else if(type == 3){ gd.totalMapZones++; }
			else ctx.add(SEV_WARN, CAT_IPL, "DAT-27", logical, l.number, t[0], fmt("тип зоны %d — SA принимает только 0, 1 (навигация) и 3 (карта); зона отброшена", type), "");
			for(int k = 2; k < 8; k++) if(fabs(strtod(t[k].c_str(), nullptr)) > 32767)
				ctx.add(SEV_ERROR, CAT_IPL, "DAT-27", logical, l.number, t[0], "координата зоны вне int16", "");
		}else if(sect == "cull"){
			if(t.size() >= 14){ gd.totalMirror++; ipl.numCull++; }
			else if(t.size() >= 11){
				ipl.numCull++;
				int flags = toInt(t[9]);
				if(flags & 0x880) gd.totalTunnel++;
				int rest = flags & 0xF77F;
				if(rest) gd.totalCull++;
				else ctx.add(SEV_INFO, CAT_IPL, "DAT-28", logical, l.number, "", fmt("cull с флагами 0x%X: после маскирования флагов нет — зона отброшена", flags), "");
			}else
				ctx.add(SEV_ERROR, CAT_IPL, "DAT-28", logical, l.number, "", fmt("cull: %d полей (нужно 11 или 14 для зеркала)", (int)t.size()), "");
		}else if(sect == "occl"){
			if(t.size() < 6){ ctx.add(SEV_ERROR, CAT_IPL, "DAT-29", logical, l.number, "", "occl: меньше 6 полей", ""); continue; }
			ipl.numOccl++;
			int zero = 0;
			for(int k = 3; k < 6; k++) if((int)toF(t[k]) == 0) zero++;
			if(zero >= 2) ctx.add(SEV_WARN, CAT_IPL, "DAT-29", logical, l.number, "", "occl: два нулевых размера — окклюдер отброшен", "");
			if(isInterior) gd.totalOcclInt++; else gd.totalOccl++;
		}else if(sect == "grge"){
			if(t.size() < 11){ ctx.add(SEV_WARN, CAT_IPL, "DAT-31", logical, l.number, "", fmt("grge: %d полей вместо 11 — гараж молча отброшен (в ванили тоже есть такие)", (int)t.size()), ""); continue; }
			ipl.numGrge++; gd.totalGrge++;
			if(t[10].size() >= 8) ctx.add(SEV_FATAL, CAT_IPL, "DAT-31", logical, l.number, t[10], fmt("имя гаража «%s» ≥ 8 символов — буфер 8 байт лежит под адресом возврата, краш", t[10].c_str()), "");
		}else if(sect == "enex"){
			if(t.size() < 14){ ctx.add(SEV_ERROR, CAT_IPL, "DAT-30", logical, l.number, "", fmt("enex: %d полей (нужно 18)", (int)t.size()), ""); continue; }
			ipl.numEnex++; gd.totalEnex++;
			const std::string &nm = t[13];
			if(nm.size() < 2 || nm[0] != '"' || nm[nm.size()-1] != '"')
				ctx.add(SEV_ERROR, CAT_IPL, "DAT-30", logical, l.number, nm, "enex: имя должно быть в кавычках без пробелов («\"NAME\"»), иначе поля после него не разберутся", "");
			if(nm.size() >= 32) ctx.add(SEV_FATAL, CAT_IPL, "DAT-30", logical, l.number, nm, "enex: имя ≥ 32 символов — переполнение стека", "");
			if(t.size() < 18) ctx.add(SEV_WARN, CAT_IPL, "DAT-30", logical, l.number, nm, fmt("enex: %d полей вместо 18 — хвост по умолчанию", (int)t.size()), "");
		}else if(sect == "pick"){
			if(t.size() < 4){ ctx.add(SEV_ERROR, CAT_IPL, "DAT-33", logical, l.number, "", fmt("pick: %d полей вместо 4 — отброшен", (int)t.size()), ""); continue; }
			ipl.numPick++; gd.totalPickups++;
			if(!pickupIdOk(toInt(t[0]))) ctx.add(SEV_WARN, CAT_IPL, "DAT-33", logical, l.number, t[0], fmt("pick: id %s нет в таблице движка (4..6, 9..55 без 30 и 42) — отброшен", t[0].c_str()), "");
		}else if(sect == "cars"){
			if(t.size() < 12){ ctx.add(SEV_ERROR, CAT_IPL, "DAT-34", logical, l.number, "", fmt("cars: %d полей вместо 12 — генератор отброшен", (int)t.size()), ""); continue; }
			ipl.numCars++; gd.totalCarGen++;
			int model = toInt(t[4]);
			if(!(model == -1 || (model >= 400 && model <= 630)))
				ctx.add(SEV_ERROR, CAT_IPL, "DAT-34", logical, l.number, t[4], fmt("cars: модель %d не в -1 / 400..630 — CreateCarGenerator отбросит генератор", model), "");
		}else if(sect == "jump"){
			if(t.size() < 16){ ctx.add(SEV_ERROR, CAT_IPL, "DAT-32", logical, l.number, "", fmt("jump: %d полей вместо 16 — отброшен", (int)t.size()), ""); continue; }
			ipl.numJump++; gd.totalJumps++;
		}else if(sect == "tcyc"){
			if(t.size() < 11) ctx.add(SEV_WARN, CAT_IPL, "DAT-35", logical, l.number, "", fmt("tcyc: %d полей вместо 12 — недостающие поля останутся со стека", (int)t.size()), "");
			else if(t.size() == 11) ctx.add(SEV_INFO, CAT_IPL, "DAT-35", logical, l.number, "", "tcyc: 11 полей — lodDistMult берётся из 11-го (ванильный вариант)", "");
			ipl.numTcyc++; gd.totalTcyc++;
		}else if(sect == "auzo"){
			if(t.size() >= 9){ gd.totalAuzoBox++; }
			else if(t.size() >= 7){ gd.totalAuzoSphere++; }
			else { ctx.add(SEV_ERROR, CAT_IPL, "DAT-36", logical, l.number, t.empty() ? "" : t[0], fmt("auzo: %d полей (нужно 9 для box или 7 для sphere)", (int)t.size()), ""); continue; }
			ipl.numAuzo++;
			if(t[0].size() >= 16) ctx.add(SEV_FATAL, CAT_IPL, "DAT-36", logical, l.number, t[0], "auzo: имя ≥ 16 символов — переполнение стека", "");
			else if(t[0].size() >= 8) ctx.add(SEV_ERROR, CAT_IPL, "DAT-36", logical, l.number, t[0], "auzo: имя ≥ 8 символов — теряет NUL в 8-байтовом поле", "");
		}
	}
	if(!sect.empty() && !aborted)
		ctx.add(SEV_WARN, CAT_IPL, "IPL-01", logical, (int)lines.size(), "", fmt("секция «%s» не закрыта строкой end", sect.c_str()), "");
	if(sa && !streamed && pending.size() > 4096)
		ctx.add(SEV_FATAL, CAT_LIMIT, "DAT-26", logical, -1, "", fmt("%d inst-строк в одном текстовом IPL — gCurrIplInstances на 4096 без проверки", (int)pending.size()), "");
	if(sa && streamed && ipl.textParent < 0){
		for(size_t i = 0; i < pending.size(); i++) if(pending[i].lod != -1){
			ctx.add(SEV_FATAL, CAT_IPL, "DAT-22", logical, pending[i].line, pending[i].name,
			        fmt("lod = %d в streamed-IPL, а текстового IPL «%s» с тем же именем нет в gta.dat — движок читает адрес lod*4", pending[i].lod, stem(logical).c_str()), "", pending[i].id);
			break;
		}
	}
	gd.ipls.push_back(ipl);
	int ii = (int)gd.ipls.size() - 1;
	int countForLod = streamed ? textParentCount : (int)pending.size();
	for(size_t i = 0; i < pending.size(); i++){
		pending[i].fileIdx = ii;
		gd.insts.push_back(pending[i]);
		checkInstCommon(ctx, gd.ipls[ii], gd.insts.back(), logical, pending[i].line, countForLod < 0 ? 0 : countForLod);
	}
	gd.ipls[ii].numInst = (int)pending.size();
	if(!streamed) finishTextIplLods(ctx, gd.ipls[ii], logical);
	else if(ipl.textParent >= 0){
		IplFile &tp = gd.ipls[ipl.textParent];
		for(size_t i = 0; i < pending.size(); i++){
			Inst &in = gd.insts[gd.ipls[ii].firstInst + i];
			if(in.lod >= 0 && in.lod < tp.numInst){
				Inst &lod = gd.insts[tp.firstInst + in.lod];
				if(gd.findObj(lod.id)) { gd.objs[gd.objById[lod.id]].isLod = true; gd.objs[gd.objById[lod.id]].lodChildren++; }
			}
		}
	}
}

static void loadTextIpl(Context &ctx, const std::string &logical, const std::string &phys, bool streamed, int entryIdx)
{
	std::vector<Line> lines;
	if(!loadLines(phys, lines)){
		ctx.add(SEV_FATAL, CAT_DAT, "DAT-03", logical, -1, "", "IPL-файл не найден или не открывается",
		        "LoadScene не проверяет результат OpenFile → fgets(NULL) → краш при старте.");
		return;
	}
	loadTextIplLines(ctx, logical, phys, lines, streamed, entryIdx);
}

static void linesFromMemory(const uint8_t *data, size_t n, std::vector<Line> &out)
{
	out.clear();
	std::string cur;
	int number = 0;
	auto flush = [&](){
		number++;
		Line l;
		l.number = number;
		l.tooLong = cur.size() > 511;
		for(size_t k = 0; k < cur.size(); k++){
			unsigned char c = (unsigned char)cur[k];
			if(c < 0x20 || c == ',') cur[k] = ' ';
		}
		l.raw = trim(cur);
		l.comment = !l.raw.empty() && l.raw[0] == '#';
		if(!l.raw.empty()){ tokenize(l); out.push_back(l); }
		cur.clear();
	};
	for(size_t i = 0; i < n; i++){
		char c = (char)data[i];
		if(c == '\0') break;
		if(c == '\n') flush();
		else if(c != '\r') cur.push_back(c);
	}
	if(!cur.empty()) flush();
}

static void loadBinaryIpl(Context &ctx, const std::string &logical, const std::vector<uint8_t> &data, int entryIdx)
{
	GameData &gd = *ctx.gd;
	ctx.rep->countFile(CAT_IPL);
	IplFile ipl;
	ipl.logical = logical;
	ipl.binary = true;
	ipl.streamed = true;
	ipl.entry = entryIdx;
	ipl.firstInst = (int)gd.insts.size();
	std::string b = lower(stem(logical));
	size_t s = b.find("_stream");
	if(s != std::string::npos) b = b.substr(0, s);
	ipl.textParent = findTextIplByBase(gd, b);
	int textParentCount = ipl.textParent >= 0 ? gd.ipls[ipl.textParent].numInst : -1;

	Buf bf(data.data(), data.size());
	bf.skip(4);
	uint32_t numInst = bf.u32(), numCull = bf.u32(), numGrge = bf.u32(), numEnex = bf.u32(), numCars = bf.u32(), numPick = bf.u32();
	uint32_t offInst = bf.u32(), sizeInst = bf.u32();
	bf.skip(16);	// cull / garage offsets+sizes
	bf.skip(8);	// enex
	uint32_t offCars = bf.u32(), sizeCars = bf.u32();
	(void)sizeInst; (void)sizeCars; (void)numCull; (void)numGrge; (void)numEnex; (void)numPick;
	if(!bf.ok || data.size() < 0x4C){
		ctx.add(SEV_FATAL, CAT_IPL, "DAT-37", logical, -1, "", "bnry: заголовок короче 76 байт", "");
		gd.ipls.push_back(ipl);
		return;
	}
	if((int16_t)numInst < 0)
		ctx.add(SEV_ERROR, CAT_IPL, "DAT-37", logical, -1, "", fmt("bnry: numInst = %u — читается как int16, отрицательно → секция пропущена", numInst), "");
	if(numInst > 0 && ((uint64_t)offInst + (uint64_t)numInst * 40 > data.size()))
		ctx.add(SEV_FATAL, CAT_IPL, "DAT-37", logical, -1, "", fmt("bnry: inst-таблица (%u записей @%u) выходит за размер файла %u — чтение буфера стриминга за концом", numInst, offInst, (unsigned)data.size()), "");
	if(numCars > 0 && ((int16_t)numCars < 0))
		ctx.add(SEV_ERROR, CAT_IPL, "DAT-37", logical, -1, "", fmt("bnry: numCarGen = %u читается как int16 — секция пропущена", numCars), "");
	if(numCars > 0 && ((uint64_t)offCars + (uint64_t)numCars * 48 > data.size()))
		ctx.add(SEV_FATAL, CAT_IPL, "DAT-37", logical, -1, "", "bnry: cars-таблица выходит за размер файла", "");
	if(numCull || numGrge || numEnex || numPick)
		ctx.add(SEV_INFO, CAT_IPL, "DAT-37", logical, -1, "", "bnry: секции cull/grge/enex/pick движок не читает", "");
	gd.ipls.push_back(ipl);
	int ii = (int)gd.ipls.size() - 1;
	int cnt = (int16_t)numInst < 0 ? 0 : (int)numInst;
	if((uint64_t)offInst + (uint64_t)cnt * 40 > data.size()) cnt = 0;
	bf.seek(offInst);
	for(int i = 0; i < cnt && bf.ok; i++){
		Inst in;
		for(int k = 0; k < 3; k++) in.pos[k] = bf.f32();
		for(int k = 0; k < 4; k++) in.rot[k] = bf.f32();
		in.id = bf.i32();
		in.interior = bf.i32();
		in.lod = bf.i32();
		in.line = i;
		in.fileIdx = ii;
		in.ordinal = i;
		if(in.id == 0) continue;	// deleted (editor convention)
		gd.insts.push_back(in);
		checkInstCommon(ctx, gd.ipls[ii], gd.insts.back(), logical, i, textParentCount < 0 ? 0 : textParentCount);
		if(gd.isSA() && in.lod != -1 && ipl.textParent < 0)
			ctx.add(SEV_FATAL, CAT_IPL, "DAT-22", logical, i, "", fmt("lod = %d, а текстового IPL «%s» нет в gta.dat — движок читает адрес lod*4", in.lod, b.c_str()), "", in.id);
	}
	gd.ipls[ii].numInst = (int)gd.insts.size() - gd.ipls[ii].firstInst;
	if(ipl.textParent >= 0){
		IplFile &tp = gd.ipls[ipl.textParent];
		for(int i = 0; i < gd.ipls[ii].numInst; i++){
			Inst &in = gd.insts[gd.ipls[ii].firstInst + i];
			if(in.lod >= 0 && in.lod < tp.numInst){
				Inst &lod = gd.insts[tp.firstInst + in.lod];
				if(gd.findObj(lod.id)) { gd.objs[gd.objById[lod.id]].isLod = true; gd.objs[gd.objById[lod.id]].lodChildren++; checkLodTarget(ctx, gd, in, lod, logical); }
			}
		}
	}
}

// ------------------------------------------------------------- gta.dat ---

struct DatLine { std::string kw, arg, file; int line; };

static void readDat(Context &ctx, const std::string &logical, std::vector<DatLine> &out, bool required)
{
	GameData &gd = *ctx.gd;
	std::string mod;
	std::string phys = physFor(gd, logical, &mod);
	std::vector<Line> lines;
	if(!loadLines(phys, lines)){
		if(required) ctx.add(SEV_FATAL, CAT_DAT, "DAT-03", logical, -1, "", "файл не найден", "");
		return;
	}
	gd.datFiles.push_back(phys);
	ctx.rep->countFile(CAT_DAT);
	for(size_t i = 0; i < lines.size(); i++){
		Line &l = lines[i];
		if(l.comment) continue;
		if(l.tooLong) ctx.add(SEV_ERROR, CAT_DAT, "DAT-01", logical, l.number, "", "строка длиннее 511 символов", "");
		DatLine d;
		d.file = logical;
		d.line = l.number;
		const std::string &r = l.raw;
		// keyword = prefix match, argument at a fixed offset (exactly one separator)
		struct KW { const char *kw; int off; };
		static const KW kws[] = { {"EXIT",0}, {"TEXDICTION",11}, {"IMG",4}, {"CDIMAGE",8}, {"COLFILE",10}, {"MODELFILE",10}, {"HIERFILE",9}, {"IDE",4}, {"IPL",4}, {"MAPZONE",8}, {"SPLASH",0}, {nullptr,0} };
		bool matched = false;
		for(int k = 0; kws[k].kw; k++){
			size_t kl = strlen(kws[k].kw);
			if(r.compare(0, kl, kws[k].kw) == 0){
				d.kw = kws[k].kw;
				if(kws[k].off){
					if(r.size() > (size_t)kws[k].off) d.arg = trim(r.substr(kws[k].off));
					// the engine takes the argument at a FIXED offset: two separators = leading space in the path
					std::string tail = r.size() > kl ? r.substr(kl) : "";
					size_t sep = 0;
					while(sep < tail.size() && tail[sep] == ' ') sep++;
					int expectSep = kws[k].off - (int)kl;
					if(d.kw == "COLFILE"){
						// "COLFILE 0 path": arg offset 10 skips "COLFILE 0 "
						std::vector<std::string> &t = l.tok;
						if(t.size() >= 3) d.arg = t[2];
						else { ctx.add(SEV_ERROR, CAT_DAT, "DAT-03", logical, l.number, "", "COLFILE без уровня/пути", ""); matched = true; break; }
						if(t.size() > 3) ctx.add(SEV_WARN, CAT_DAT, "DAT-03", logical, l.number, "", "COLFILE: лишние поля / пробелы в пути", "");
					}else if((int)sep != expectSep){
						ctx.add(SEV_FATAL, CAT_DAT, "DAT-03", logical, l.number, d.arg,
						        fmt("%s: между ключевым словом и путём %d пробелов вместо 1 — движок берёт путь с фиксированного смещения и не откроет файл", d.kw.c_str(), (int)sep),
						        "LoadLevel: strncmp по ключевому слову, аргумент = line+N. Один пробел.");
						d.arg = tail.substr(sep);
					}
				}
				matched = true;
				break;
			}
		}
		if(!matched){
			ctx.add(SEV_WARN, CAT_DAT, "DAT-02", logical, l.number, "", fmt("строка «%s» не распознана (ключевые слова регистрозависимы) — игнорируется", r.substr(0, 40).c_str()), "");
			continue;
		}
		if(d.kw == "EXIT") break;
		if(d.kw == "SPLASH") continue;
		if((d.kw == "TEXDICTION" || d.kw == "IPL") && d.arg.size() >= 64)
			ctx.add(SEV_FATAL, CAT_DAT, "DAT-05", logical, l.number, d.arg, fmt("%s: путь ≥ 64 символов переполняет буфер LoadLevel", d.kw.c_str()), "");
		if(d.kw == "IDE" && d.arg.size() >= 256)
			ctx.add(SEV_FATAL, CAT_DAT, "DAT-05", logical, l.number, d.arg, "IDE: путь ≥ 256 символов переполняет буфер LoadObjectTypes", "");
		out.push_back(d);
	}
}

// ----------------------------------------------------------- top level ---

static GameVersion detectGame(const std::string &root)
{
	if(fileExists(joinPath(root, "data/gta.dat")) && fileExists(joinPath(root, "models/gta3.img"))){
		std::vector<uint8_t> h;
		FILE *f = nullptr;
#ifdef _WIN32
		std::string p = joinPath(root, "models/gta3.img");
		int wn = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
		std::wstring w((size_t)wn, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], wn);
		f = _wfopen(w.c_str(), L"rb");
#endif
		if(f){ char m[4] = {0}; size_t r = fread(m, 1, 4, f); fclose(f); if(r == 4 && memcmp(m, "VER2", 4) == 0) return GAME_SA; }
		return GAME_SA;
	}
	if(fileExists(joinPath(root, "data/gta_vc.dat"))) return GAME_VC;
	if(fileExists(joinPath(root, "data/gta3.dat"))) return GAME_III;
	if(fileExists(joinPath(root, "data/gta.dat"))) return GAME_SA;
	return GAME_UNKNOWN;
}

void LoadGameData(Context &ctx)
{
	GameData &gd = *ctx.gd;
	gd.root = ctx.opt.root;
	gd.opt = ctx.opt;
	gd.game = detectGame(gd.root);
	if(gd.game == GAME_UNKNOWN){
		ctx.add(SEV_FATAL, CAT_DAT, "ROOT", gd.root, -1, "", "в папке нет data/gta.dat (SA), data/gta_vc.dat (VC) или data/gta3.dat (III)", "Укажи корневую папку игры (где лежит gta_sa.exe).");
		return;
	}
	bool sa = gd.isSA();
	// fastman92 limit adjuster present → pool/array limits are most likely raised
	if(!ctx.opt.limitAdjuster && (fileExists(joinPath(gd.root, "$fastman92limitAdjuster.asi")) || fileExists(joinPath(gd.root, "scripts/$fastman92limitAdjuster.asi")))){
		ctx.opt.limitAdjuster = true;
		gd.opt.limitAdjuster = true;
		ctx.add(SEV_INFO, CAT_LIMIT, "LIMITADJ", "$fastman92limitAdjuster.asi", -1, "", "найден fastman92 limit adjuster — превышения стандартных пулов показаны как заметки, а не как краш", "");
	}

	ctx.prog->set("modloader");
	std::vector<ModAddition> adds;
	std::vector<std::pair<std::string,std::string>> addedImgs;
	if(ctx.opt.useModloader) scanModloader(ctx, adds, addedImgs);

	ctx.prog->set("gta.dat");
	std::vector<DatLine> dat;
	readDat(ctx, "data/default.dat", dat, true);
	readDat(ctx, sa ? "data/gta.dat" : (gd.game == GAME_VC ? "data/gta_vc.dat" : "data/gta3.dat"), dat, true);
	// modloader additions (readme / mod gta.dat): IDE first, then everything else, IPL last
	for(int pass = 0; pass < 3; pass++)
		for(size_t i = 0; i < adds.size(); i++){
			const ModAddition &a = adds[i];
			int want = a.type == "IDE" ? 0 : (a.type == "IPL" ? 2 : 1);
			if(want != pass) continue;
			DatLine d; d.kw = a.type; d.arg = a.path; d.file = "modloader/" + a.mod; d.line = -1;
			dat.push_back(d);
		}
	// loose IDE / IPL in mods not matching any stock file → additions
	if(gd.modloaderActive){
		std::set<std::string> stockBases;
		for(size_t i = 0; i < dat.size(); i++) stockBases.insert(lower(basename(dat[i].arg)));
		std::vector<DatLine> extraIde, extraIpl;
		std::set<std::string> seen;
		for(size_t i = 0; i < gd.modFiles.size(); i++){
			const ModFile &mf = gd.modFiles[i];
			std::string ext = extOf(mf.rel);
			if(ext != "ide" && ext != "ipl") continue;
			std::string b = basename(mf.rel);
			if(stockBases.count(b) || seen.count(b)) continue;
			if(ext == "ipl" && isBnryFile(mf.phys)) continue;	// loose bnry = stream override
			auto w = gd.redirectByRel.find(b);
			if(w == gd.redirectByRel.end() || w->second != (int)i) continue;	// not the winner
			seen.insert(b);
			DatLine d; d.kw = ext == "ide" ? "IDE" : "IPL"; d.arg = mf.phys; d.file = "modloader/" + mf.mod; d.line = -1;
			(ext == "ide" ? extraIde : extraIpl).push_back(d);
		}
		for(size_t i = 0; i < extraIde.size(); i++) dat.push_back(extraIde[i]);
		for(size_t i = 0; i < extraIpl.size(); i++) dat.push_back(extraIpl[i]);
	}

	// pass 1: IMGs (SA: explicit IMG lines + stock models/gta3.img, gta_int.img, player.img are implicit;
	// III/VC: models/gta3.img + CDIMAGE lines)
	ctx.prog->set("IMG-архивы");
	std::vector<std::string> imgList;
	if(sa){
		imgList.push_back("models/gta3.img");
		imgList.push_back("models/gta_int.img");
		imgList.push_back("models/player.img");
	}else{
		imgList.push_back("models/gta3.img");
	}
	bool seenIpl = false;
	for(size_t i = 0; i < dat.size(); i++){
		if(dat[i].kw == "IMG" || dat[i].kw == "CDIMAGE"){
			bool dup = false;
			for(size_t k = 0; k < imgList.size(); k++) if(ieq(normSlashes(imgList[k]), normSlashes(dat[i].arg))) dup = true;
			if(!dup) imgList.push_back(dat[i].arg);
		}
	}
	for(size_t i = 0; i < addedImgs.size(); i++){
		bool replaced = false;
		for(size_t k = 0; k < imgList.size(); k++)
			if(ieq(basename(imgList[k]), basename(addedImgs[i].first))){ imgList[k] = addedImgs[i].first; replaced = true; }
		if(!replaced) imgList.push_back(addedImgs[i].first);
	}
	for(size_t i = 0; i < imgList.size(); i++){
		if(ctx.cancelled()) return;
		ctx.prog->step(basename(imgList[i]).c_str(), (int)i, (int)imgList.size());
		std::string mod;
		std::string phys = physFor(gd, imgList[i], &mod);
		for(size_t k = 0; k < addedImgs.size(); k++) if(addedImgs[k].first == imgList[i]){ phys = imgList[i]; mod = addedImgs[k].second; }
		loadArchive(ctx, imgList[i], phys, mod);
	}
	if(ctx.opt.useModloader) applyLooseOverrides(ctx);

	// TXD slots from IMG entries
	for(size_t i = 0; i < gd.entries.size(); i++){
		Entry &e = gd.entries[i];
		if(e.kind == EK_TXD){
			int s = gd.txdSlotOf(e.base);
			gd.txdSlots[s].fromImg = true;
			gd.txdSlots[s].entry = (int)i;
		}
	}

	// pass 2: IDE / COLFILE / TEXDICTION / IPL in gta.dat order
	ctx.prog->set("IDE / IPL");
	int nIde = 0, nIpl = 0;
	for(size_t i = 0; i < dat.size(); i++) if(dat[i].kw == "IDE") nIde++; else if(dat[i].kw == "IPL") nIpl++;
	int done = 0;
	for(size_t i = 0; i < dat.size(); i++){
		if(ctx.cancelled()) return;
		const DatLine &d = dat[i];
		if(d.kw == "IDE"){
			if(seenIpl && sa)
				ctx.add(SEV_ERROR, CAT_DAT, "DAT-04", d.file, d.line, d.arg, "IDE указан после первого IPL — его модели не получат позиций в IMG (CStreaming::Init2 выполняется один раз) и никогда не загрузятся",
				        "Перенеси все строки IDE выше первой строки IPL.");
			ctx.prog->step(basename(d.arg).c_str(), done++, nIde + nIpl);
			std::string mod;
			std::string phys = d.line < 0 ? d.arg : physFor(gd, d.arg, &mod);
			loadIde(ctx, d.arg, phys);
		}else if(d.kw == "IPL"){
			seenIpl = true;
			ctx.prog->step(basename(d.arg).c_str(), done++, nIde + nIpl);
			std::string mod;
			std::string phys = d.line < 0 ? d.arg : physFor(gd, d.arg, &mod);
			bool dupe = false;
			for(size_t k = 0; k < gd.ipls.size(); k++) if(ieq(normSlashes(gd.ipls[k].logical), normSlashes(d.arg))) dupe = true;
			if(dupe){ ctx.add(SEV_WARN, CAT_DAT, "DAT-50", d.file, d.line, d.arg, "IPL перечислен дважды — все объекты будут продублированы", ""); continue; }
			loadTextIpl(ctx, d.arg, phys, false, -1);
		}else if(d.kw == "MAPZONE"){
			std::string mod;
			loadTextIpl(ctx, d.arg, physFor(gd, d.arg, &mod), false, -1);
		}
	}
	// model → IMG entry, TXD slot (vehicle TXDs get the engine's "vehicle" parent)
	int vehicleSlot = sa ? gd.txdSlotOf("vehicle") : -1;
	for(size_t i = 0; i < gd.objs.size(); i++){
		ObjDef &o = gd.objs[i];
		o.dffEntry = gd.findEntry(o.name + ".dff");
		o.txdSlot = gd.txdSlotOf(o.txd);
		gd.txdSlots[o.txdSlot].fromIde = true;
		gd.txdSlots[o.txdSlot].users++;
		if(o.type == OT_CARS && vehicleSlot >= 0 && o.txdSlot != vehicleSlot && gd.txdSlots[o.txdSlot].parent < 0)
			gd.txdSlots[o.txdSlot].parent = vehicleSlot;
	}
	if(!sa){
		// III/VC: LOD связь только по имени — CSimpleModelInfo::FindRelatedModel сравнивает имена БЕЗ первых трёх букв с обеих
		// сторон (LOD_land034 ↔ ind_land034, LODhaiblockc3 ↔ haiblockc3 не пара!). LOD = модель с префиксом LOD, у которой
		// есть такая пара; карта суффикс → id LOD для 3D-вида
		std::unordered_map<std::string, std::vector<size_t>> bySuffix;
		for(size_t i = 0; i < gd.objs.size(); i++){
			const ObjDef &o = gd.objs[i];
			if(o.type != OT_OBJS && o.type != OT_TOBJ) continue;
			std::string ln = lower(o.name);
			if(ln.size() > 3) bySuffix[ln.substr(3)].push_back(i);
		}
		for(size_t i = 0; i < gd.objs.size(); i++){
			ObjDef &o = gd.objs[i];
			std::string ln = lower(o.name);
			if(ln.size() <= 3 || ln.compare(0, 3, "lod") != 0) continue;
			auto it = bySuffix.find(ln.substr(3));
			if(it == bySuffix.end()) continue;
			for(size_t k = 0; k < it->second.size(); k++){
				size_t j = it->second[k];
				if(j == i) continue;
				o.isLod = true;
				gd.objs[j].lodChildren++;
				gd.lodBySuffix[ln.substr(3)] = o.id;
			}
		}
	}

	// pass 3: streamed IPLs from IMG (bnry or text)
	ctx.prog->set("Streamed IPL (bnry)");
	int nStream = 0;
	for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].kind == EK_IPL) nStream++;
	if(sa && nStream > 255)
		ctx.add(SEV_FATAL, CAT_LIMIT, "DAT-39", "IMG", -1, "", fmt("%d streamed-IPL файлов во всех IMG — пул CIplStore на 256 слотов (0 = generic), AddIplSlot пишет по NULL", nStream), "");
	done = 0;
	for(size_t i = 0; i < gd.entries.size(); i++){
		if(ctx.cancelled()) return;
		Entry &e = gd.entries[i];
		if(e.kind != EK_IPL || !gd.isWinner((int)i)) continue;
		std::string where = (e.img >= 0 ? basename(gd.archives[e.img].logical) + "/" : "") + e.name;
		ctx.prog->step(e.name.c_str(), done++, nStream);
		if(sa && e.base.size() >= 18)
			ctx.add(SEV_FATAL, CAT_IPL, "DAT-39", where, e.dirIndex, e.name, fmt("имя streamed-IPL «%s» ≥ 18 символов — AddIplSlot затирает поля IplDef за именем (индексы LOD)", e.base.c_str()), "Держи имя ≤ 17 символов.");
		std::vector<uint8_t> data;
		std::string err;
		if(!gd.readEntry((int)i, data, &err)){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-24", where, e.dirIndex, e.name, "не удалось прочитать запись: " + err, "");
			continue;
		}
		if(data.size() >= 4 && memcmp(data.data(), "bnry", 4) == 0)
			loadBinaryIpl(ctx, where, data, (int)i);
		else{
			std::vector<Line> lines;
			linesFromMemory(data.data(), data.size(), lines);
			loadTextIplLines(ctx, where, where, lines, true, (int)i);
		}
	}

	// IFP entries
	for(size_t i = 0; i < gd.entries.size(); i++)
		if(gd.entries[i].kind == EK_IFP && gd.isWinner((int)i)) gd.ifpEntries.push_back(gd.entries[i].name);
	for(size_t i = 0; i < gd.entries.size(); i++)
		if(gd.entries[i].kind == EK_COL && gd.isWinner((int)i)) gd.numColFiles++;

	// COLFILE / TEXDICTION lines are consumed by the COL / TXD passes (they read `dat` again),
	// so keep them in the data for those passes.
	for(size_t i = 0; i < dat.size(); i++){
		if(dat[i].kw == "COLFILE") gd.datFiles.push_back("COLFILE:" + dat[i].arg);
		else if(dat[i].kw == "TEXDICTION") gd.datFiles.push_back("TEXDICTION:" + dat[i].arg);
		else if(dat[i].kw == "MODELFILE" || dat[i].kw == "HIERFILE"){
			// CFileLoader::LoadClumpFile: каждый CLUMP файла → модель по имени корневого фрейма (GetNameAndLOD)
			std::vector<uint8_t> f;
			std::string phys = DataFilePath(gd, dat[i].arg);
			if(!readFile(phys, f)){
				ctx.add(SEV_ERROR, CAT_DAT, "DAT-03", dat[i].file, dat[i].line, "", fmt("%s «%s» не найден — RwStreamOpen вернёт NULL, RwStreamFindChunk(NULL) → краш при старте", dat[i].kw.c_str(), dat[i].arg.c_str()), "");
				continue;
			}
			// MODELFILE → LoadModelFile: каждый атомик → модель по имени его фрейма; HIERFILE → LoadClumpFile: по корневому
			// фрейму каждого клампа. Берём имена всех фреймов всех клампов (без LOD-суффикса _lN) — надмножество обоих
			Buf b(f.data(), f.size());
			Chunk cl;
			while(findChunk(b, 0x10, cl, f.size())){
				Buf c(f.data() + cl.start, cl.end - cl.start);
				Chunk fl, st;
				if(findChunk(c, 0xE, fl, c.n) && findChunk(c, 0x1, st, fl.end)){
					c.seek(st.end);
					Chunk ext;
					while(findChunk(c, 0x3, ext, fl.end)){	// один EXTENSION на фрейм, внутри NodeName (0x253F2FE)
						Chunk nn;
						if(findChunk(c, 0x253F2FE, nn, ext.end)){
							std::string name((const char*)f.data() + cl.start + nn.start, nn.end - nn.start);
							size_t z = name.find('\0'); if(z != std::string::npos) name.resize(z);
							size_t us = name.rfind("_l"); if(us != std::string::npos && us + 2 < name.size() && isdigit((unsigned char)name[us + 2])) name.resize(us);
							if(!name.empty()) gd.modelFileClumps.insert(lower(name));
						}
						c.seek(ext.end);
					}
				}
				b.seek(cl.end);
			}
		}
	}
}

} // namespace gc
