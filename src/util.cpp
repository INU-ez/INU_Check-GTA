// gtacheck — shared helpers: bounds-checked reader, RW chunks, paths, report.
#include "gtacheck.h"

#include <stdarg.h>
#include <ctype.h>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

namespace gc {

static std::string withVersion(const char *t){ std::string r = t; size_t p = r.find("gtacheck"); if(p != std::string::npos) r.insert(p + 8, " v" GC_VERSION); return r; }

// ---------------------------------------------------------------- Buf ----

uint8_t Buf::u8()
{
	if(!can(1)){ ok = false; return 0; }
	return p[pos++];
}

uint16_t Buf::u16()
{
	if(!can(2)){ ok = false; pos = n; return 0; }
	uint16_t v = (uint16_t)(p[pos] | (p[pos+1] << 8));
	pos += 2;
	return v;
}

uint32_t Buf::u32()
{
	if(!can(4)){ ok = false; pos = n; return 0; }
	uint32_t v = (uint32_t)p[pos] | ((uint32_t)p[pos+1] << 8) | ((uint32_t)p[pos+2] << 16) | ((uint32_t)p[pos+3] << 24);
	pos += 4;
	return v;
}

float Buf::f32()
{
	uint32_t v = u32();
	float f;
	memcpy(&f, &v, 4);
	return f;
}

bool Buf::skip(size_t k)
{
	if(!can(k)){ ok = false; pos = n; return false; }
	pos += k;
	return true;
}

bool Buf::seek(size_t at)
{
	if(at > n){ ok = false; pos = n; return false; }
	pos = at;
	return true;
}

const uint8_t *Buf::ptr(size_t k)
{
	if(!can(k)){ ok = false; pos = n; return nullptr; }
	const uint8_t *r = p + pos;
	pos += k;
	return r;
}

std::string Buf::str(size_t k)
{
	const uint8_t *s = ptr(k);
	if(s == nullptr) return "";
	size_t len = 0;
	while(len < k && s[len]) len++;
	return std::string((const char*)s, len);
}

// -------------------------------------------------------------- chunks ---

uint32_t rwUnpackVersion(uint32_t libid)
{
	if(libid & 0xFFFF0000)
		return (((libid >> 14) & 0x3FF00) + 0x30000) | ((libid >> 16) & 0x3F);
	return libid << 8;
}

bool readChunk(Buf &b, Chunk &c)
{
	memset(&c, 0, sizeof(c));
	if(!b.can(12)) return false;
	c.type = b.u32();
	c.size = b.u32();
	c.libid = b.u32();
	c.version = rwUnpackVersion(c.libid);
	c.build = c.libid & 0xFFFF;
	c.start = b.pos;
	uint64_t e = (uint64_t)c.start + c.size;
	if(e > b.n){ c.end = b.n; c.truncated = true; }
	else { c.end = (size_t)e; c.truncated = false; }
	return true;
}

bool findChunk(Buf &b, uint32_t type, Chunk &c, size_t limit)
{
	if(limit > b.n) limit = b.n;
	while(b.pos + 12 <= limit){
		size_t at = b.pos;
		if(!readChunk(b, c)) return false;
		if(c.type == type) return true;
		// skip this chunk; a truncated one ends the search
		if(c.truncated) return false;
		if(c.end <= at) return false;
		b.pos = c.end;
	}
	return false;
}

const char *rwChunkName(uint32_t type)
{
	switch(type){
	case 0x1: return "STRUCT";
	case 0x2: return "STRING";
	case 0x3: return "EXTENSION";
	case 0x5: return "CAMERA";
	case 0x6: return "TEXTURE";
	case 0x7: return "MATERIAL";
	case 0x8: return "MATERIALLIST";
	case 0xE: return "FRAMELIST";
	case 0xF: return "GEOMETRY";
	case 0x10: return "CLUMP";
	case 0x12: return "LIGHT";
	case 0x14: return "ATOMIC";
	case 0x15: return "TEXTURENATIVE";
	case 0x16: return "TEXDICTIONARY";
	case 0x1A: return "GEOMETRYLIST";
	case 0x1B: return "ANIMANIMATION";
	case 0x1F: return "RIGHTTORENDER";
	case 0x2B: return "UVANIMDICT";
	case 0x50E: return "BINMESH";
	case 0x510: return "NATIVEDATA";
	case 0x116: return "SKIN";
	case 0x11E: return "HANIM";
	case 0x120: return "MATFX";
	case 0x135: return "UVANIM";
	case 0x253F2F3: return "PIPELINESET";
	case 0x253F2F5: return "TXDPARENT";
	case 0x253F2F6: return "SPECULAR";
	case 0x253F2F8: return "2DFX";
	case 0x253F2F9: return "NIGHTCOLOURS";
	case 0x253F2FA: return "COLLISION";
	case 0x253F2FC: return "REFLECTION";
	case 0x253F2FD: return "BREAKABLE";
	case 0x253F2FE: return "NODENAME";
	}
	return "?";
}

// ------------------------------------------------------------- strings ---

std::string lower(const std::string &s)
{
	std::string r(s);
	for(size_t i = 0; i < r.size(); i++)
		if(r[i] >= 'A' && r[i] <= 'Z') r[i] = (char)(r[i] - 'A' + 'a');
	return r;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while(a < b && isspace((unsigned char)s[a])) a++;
	while(b > a && isspace((unsigned char)s[b-1])) b--;
	return s.substr(a, b - a);
}

int ieq(const std::string &a, const std::string &b)
{
	if(a.size() != b.size()) return 0;
	for(size_t i = 0; i < a.size(); i++)
		if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
	return 1;
}

std::string normSlashes(const std::string &s)
{
	std::string r(s);
	for(size_t i = 0; i < r.size(); i++) if(r[i] == '\\') r[i] = '/';
	return r;
}

std::string basename(const std::string &path)
{
	std::string p = normSlashes(path);
	size_t k = p.find_last_of('/');
	return k == std::string::npos ? p : p.substr(k + 1);
}

std::string stem(const std::string &path)
{
	std::string b = basename(path);
	size_t k = b.find_last_of('.');
	return k == std::string::npos ? b : b.substr(0, k);
}

std::string extOf(const std::string &path)
{
	std::string b = basename(path);
	size_t k = b.find_last_of('.');
	return k == std::string::npos ? "" : lower(b.substr(k + 1));
}

std::string joinPath(const std::string &a, const std::string &b)
{
	if(a.empty()) return b;
	if(b.empty()) return a;
	char last = a[a.size()-1];
	if(last == '/' || last == '\\') return a + b;
	return a + "/" + b;
}

std::string fmt(const char *f, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof(buf), f, ap);
	va_end(ap);
	buf[sizeof(buf)-1] = '\0';
	return buf;
}

double nowMs()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// --------------------------------------------------------------- files ---

#ifdef _WIN32
static std::wstring toWide(const std::string &utf8)
{
	if(utf8.empty()) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &w[0], n);
	return w;
}
static std::string fromWide(const std::wstring &w)
{
	if(w.empty()) return "";
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}
#endif

bool fileExists(const std::string &p)
{
#ifdef _WIN32
	DWORD a = GetFileAttributesW(toWide(p).c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool dirExists(const std::string &p)
{
#ifdef _WIN32
	DWORD a = GetFileAttributesW(toWide(p).c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

FILE *OpenReadUtf8(const std::string &p)
{
#ifdef _WIN32
	return _wfopen(toWide(p).c_str(), L"rb");
#else
	return fopen(p.c_str(), "rb");
#endif
}

FILE *openWriteUtf8(const std::string &p)
{
#ifdef _WIN32
	return _wfopen(toWide(p).c_str(), L"wb");
#else
	return fopen(p.c_str(), "wb");
#endif
}

bool readFile(const std::string &p, std::vector<uint8_t> &out)
{
	FILE *f = OpenReadUtf8(p);
	if(f == nullptr) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(n < 0){ fclose(f); return false; }
	out.resize((size_t)n);
	size_t r = n ? fread(out.data(), 1, (size_t)n, f) : 0;
	fclose(f);
	return r == (size_t)n;
}

bool readTextLines(const std::string &p, std::vector<std::string> &lines)
{
	std::vector<uint8_t> data;
	if(!readFile(p, data)) return false;
	lines.clear();
	std::string cur;
	for(size_t i = 0; i < data.size(); i++){
		char c = (char)data[i];
		if(c == '\n'){ lines.push_back(cur); cur.clear(); }
		else if(c != '\r') cur.push_back(c);
	}
	if(!cur.empty()) lines.push_back(cur);
	return true;
}

// Case-insensitive resolution: walks the path one component at a time and
// matches directory entries ignoring case (mods written on Windows often
// disagree with their own gta.dat about case; the engine does not care).
std::string resolvePath(const std::string &root, const std::string &logical)
{
	std::string rel = normSlashes(logical);
	// absolute / drive paths are used as-is
	if(rel.size() > 1 && (rel[1] == ':' || rel[0] == '/'))
		return rel;
	std::string cur = root;
	size_t a = 0;
	while(a < rel.size()){
		size_t b = rel.find('/', a);
		std::string comp = rel.substr(a, b == std::string::npos ? std::string::npos : b - a);
		a = b == std::string::npos ? rel.size() : b + 1;
		if(comp.empty() || comp == ".") continue;
		std::string direct = joinPath(cur, comp);
#ifdef _WIN32
		// exact (Windows is case-insensitive anyway, this also fixes separators)
		if(GetFileAttributesW(toWide(direct).c_str()) != INVALID_FILE_ATTRIBUTES){
			cur = direct;
			continue;
		}
		cur = direct;	// keep going; the caller checks existence
#else
		if(fileExists(direct) || dirExists(direct)){ cur = direct; continue; }
		DIR *d = opendir(cur.c_str());
		bool found = false;
		if(d){
			struct dirent *e;
			while((e = readdir(d)) != nullptr){
				if(ieq(e->d_name, comp)){ cur = joinPath(cur, e->d_name); found = true; break; }
			}
			closedir(d);
		}
		if(!found) cur = direct;
#endif
	}
	return cur;
}

// --------------------------------------------------------------- report --

void Report::add(const Issue &is)
{
	std::lock_guard<std::mutex> lock(mtx);
	issues.push_back(is);
	if(is.sev >= 0 && is.sev < SEV_NUM){ counters.bySev[is.sev]++; if(is.vanilla) counters.vanillaBySev[is.sev]++; }
	if(is.cat >= 0 && is.cat < CAT_NUM){ counters.byCat[is.cat]++; if(is.vanilla) counters.vanillaByCat[is.cat]++; }
	version++;
}

// ------------------------------------------------------ vanilla baseline ---

#include "vanilla_sa.h"
#include "vanilla_iii.h"
#include "vanilla_vc.h"

uint64_t IssueKey(const Issue &is)
{
	std::string f = is.file;
	size_t sl = f.find_last_of("/\\");
	if(sl != std::string::npos) f = f.substr(sl + 1);
	std::string key = is.code + "|" + lower(f) + "|" + (is.objectRu.empty() ? is.object : is.objectRu) + "|" + (is.msgRu.empty() ? is.msg : is.msgRu);
	uint64_t h = 1469598103934665603ull;
	for(size_t i = 0; i < key.size(); i++){ h ^= (unsigned char)key[i]; h *= 1099511628211ull; }
	return h;
}

bool IsVanillaKey(uint64_t key, int game)
{
	const uint64_t *tab = game == GAME_III ? VANILLA_III : game == GAME_VC ? VANILLA_VC : VANILLA_SA;
	size_t n = game == GAME_III ? VANILLA_III_COUNT : game == GAME_VC ? VANILLA_VC_COUNT : VANILLA_SA_COUNT;
	size_t lo = 0, hi = n;
	while(lo < hi){
		size_t mid = (lo + hi) / 2;
		if(tab[mid] < key) lo = mid + 1; else hi = mid;
	}
	return lo < n && tab[lo] == key;
}

void Report::countFile(Category c)
{
	std::lock_guard<std::mutex> lock(mtx);
	if(c >= 0 && c < CAT_NUM) counters.files[c]++;
	version++;
}

void Report::clear()
{
	std::lock_guard<std::mutex> lock(mtx);
	issues.clear();
	memset(&counters, 0, sizeof(counters));
	version++;
}

void Progress::set(const char *ph, int d, int t)
{
	std::lock_guard<std::mutex> lock(mtx);
	phase = T(ph);
	item.clear();
	done = d;
	total = t;
}

void Progress::step(const char *it, int d, int t)
{
	std::lock_guard<std::mutex> lock(mtx);
	item = it ? it : "";
	done = d;
	total = t;
}

void Context::add(Severity sev, Category cat, const char *code, const std::string &file, int line,
                  const std::string &object, const std::string &msg, const std::string &detail, int id,
                  int fix, int entry)
{
	if(sev == SEV_INFO && !opt.reportInfo) return;
	Issue is;
	is.sev = sev;
	is.cat = cat;
	is.code = code ? code : "";
	is.file = file;
	is.line = line;
	is.object = object;
	is.msg = msg;
	is.detail = detail;
	is.id = id;
	is.fix = fix;
	is.entry = entry;
	is.phase = phase;
	if(gd && gd->game != GAME_UNKNOWN) is.vanilla = IsVanillaKey(IssueKey(is), gd->game);	// keyed on the Russian text; per-game table
	is.msgRu = msg;
	is.detailRu = detail;
	is.objectRu = object;
	if(gLang != LANG_RU){ is.msg = TranslateMessage(msg); is.detail = TranslateMessage(detail); is.object = TranslateMessage(object); }
	rep->add(is);
}

// --------------------------------------------------------------- names ---

const char *severityName(int s)
{
	switch(s){
	case SEV_INFO: return T("Инфо");
	case SEV_WARN: return T("Предупреждение");
	case SEV_ERROR: return T("Ошибка");
	case SEV_FATAL: return T("Краш");
	}
	return "?";
}

const char *categoryName(int c)
{
	switch(c){
	case CAT_DAT: return "gta.dat";
	case CAT_IMG: return "IMG";
	case CAT_IDE: return "IDE";
	case CAT_IPL: return "IPL";
	case CAT_DFF: return "DFF";
	case CAT_TXD: return "TXD";
	case CAT_COL: return "COL";
	case CAT_IFP: return "IFP";
	case CAT_PED: return T("Педы");
	case CAT_WATER: return "water.dat";
	case CAT_TIMECYC: return "timecyc.dat";
	case CAT_PLANTS: return "plants.dat";
	case CAT_FXP: return "effects.fxp";
	case CAT_PATHS: return T("Пути");
	case CAT_XREF: return T("Связи");
	case CAT_LIMIT: return T("Лимиты");
	case CAT_VEH: return T("Машины");
	case CAT_HANDLING: return "handling/carcols";
	case CAT_WEAPON: return "weapon.dat";
	case CAT_PEDDATA: return T("Педы: данные");
	case CAT_CLOTHES: return T("Одежда CJ");
	case CAT_CUTSCENE: return T("Катсцены");
	case CAT_GXT: return "GXT";
	case CAT_SCM: return T("Скрипт");
	case CAT_UI: return T("UI-текстуры");
	case CAT_AUDIO: return T("Аудио");
	case CAT_QUALITY: return T("Качество");
	}
	return "?";
}

const char *gameName(GameVersion g)
{
	switch(g){
	case GAME_III: return "GTA III";
	case GAME_VC: return "Vice City";
	case GAME_SA: return "San Andreas";
	default: break;
	}
	return T("не определена");
}

const char *objTypeName(int t)
{
	switch(t){
	case OT_OBJS: return "objs";
	case OT_TOBJ: return "tobj";
	case OT_ANIM: return "anim";
	case OT_HIER: return "hier";
	case OT_WEAP: return "weap";
	case OT_CARS: return "cars";
	case OT_PEDS: return "peds";
	}
	return "?";
}

// ------------------------------------------------------------ GameData ---

std::string Entry::where() const
{
	if(!loose.empty()) return loose;
	return name;
}

int GameData::findEntry(const std::string &nameWithExt) const
{
	auto it = entryByName.find(lower(nameWithExt));
	return it == entryByName.end() ? -1 : it->second;
}

bool GameData::isWinner(int entryIdx) const
{
	if(entryIdx < 0 || entryIdx >= (int)entries.size()) return false;
	auto it = entryByName.find(lower(entries[(size_t)entryIdx].name));
	return it != entryByName.end() && it->second == entryIdx;
}

const ObjDef *GameData::findObj(int id) const
{
	auto it = objById.find(id);
	return it == objById.end() ? nullptr : &objs[it->second];
}

const ObjDef *GameData::findObjByName(const std::string &name) const
{
	auto it = objByName.find(lower(name));
	return it == objByName.end() ? nullptr : &objs[it->second];
}

int GameData::findTxdSlot(const std::string &name) const
{
	auto it = txdByName.find(lower(name));
	return it == txdByName.end() ? -1 : it->second;
}

int GameData::txdSlotOf(const std::string &name)
{
	std::string l = lower(name);
	auto it = txdByName.find(l);
	if(it != txdByName.end()) return it->second;
	TxdSlot s;
	s.name = l;
	s.shown = name;
	txdSlots.push_back(s);
	txdByName[l] = (int)txdSlots.size() - 1;
	return (int)txdSlots.size() - 1;
}

bool GameData::readEntry(int entryIdx, std::vector<uint8_t> &out, std::string *err) const
{
	if(entryIdx < 0 || entryIdx >= (int)entries.size()){ if(err) *err = "bad entry"; return false; }
	const Entry &e = entries[entryIdx];
	if(!e.loose.empty()){
		if(!readFile(e.loose, out)){ if(err) *err = T("не удалось прочитать ") + e.loose; return false; }
		return true;
	}
	if(e.img < 0 || e.img >= (int)archives.size()){ if(err) *err = "bad archive"; return false; }
	const Archive &a = archives[e.img];
	FILE *f = OpenReadUtf8(a.phys);
	if(f == nullptr){ if(err) *err = T("не удалось открыть ") + a.phys; return false; }
	uint64_t off = (uint64_t)e.offset * 2048;
	uint64_t len = (uint64_t)e.sizeSectors * 2048;
	if(len > 256u*1024*1024){ fclose(f); if(err) *err = T("запись больше 256 МБ"); return false; }
#ifdef _WIN32
	_fseeki64(f, (long long)off, SEEK_SET);
#else
	fseeko(f, (off_t)off, SEEK_SET);
#endif
	out.resize((size_t)len);
	size_t r = len ? fread(out.data(), 1, (size_t)len, f) : 0;
	fclose(f);
	if(r != len){
		out.resize(r);
		if(err) *err = fmt(T("запись выходит за конец архива (прочитано %u из %u байт)"), (unsigned)r, (unsigned)len);
		return false;
	}
	return true;
}

bool GameData::txdChainHasTexture(int slot, const std::string &texLower, int depth) const
{
	if(slot < 0 || slot >= (int)txdSlots.size() || depth > 8) return false;
	auto it = txdTextures.find(txdSlots[slot].name);
	if(it != txdTextures.end()){
		const std::vector<std::string> &names = it->second.names;
		if(std::find(names.begin(), names.end(), texLower) != names.end()) return true;
	}
	return txdChainHasTexture(txdSlots[slot].parent, texLower, depth + 1);
}

// ----------------------------------------------------- data-file access ---

std::string DataFilePath(const GameData &gd, const std::string &logical)
{
	if(gd.modloaderActive){
		auto it = gd.redirectByRel.find(lower(normSlashes(logical)));
		if(it == gd.redirectByRel.end()) it = gd.redirectByRel.find(lower(basename(logical)));
		if(it != gd.redirectByRel.end()) return gd.modFiles[(size_t)it->second].phys;
	}
	return resolvePath(gd.root, logical);
}

void SplitTokens(const std::string &s, std::vector<std::string> &out)
{
	out.clear();
	size_t i = 0, n = s.size();
	while(i < n){
		while(i < n && (s[i] == ' ' || s[i] == '\t')) i++;
		size_t a = i;
		while(i < n && s[i] != ' ' && s[i] != '\t') i++;
		if(i > a) out.push_back(s.substr(a, i - a));
	}
}

bool ReadDataLines(const GameData &gd, const std::string &logical, std::vector<DataLine> &out, std::string *physOut)
{
	std::string phys = DataFilePath(gd, logical);
	if(physOut) *physOut = phys;
	std::vector<std::string> lines;
	if(!readTextLines(phys, lines)) return false;
	out.clear();
	out.reserve(lines.size());
	for(size_t i = 0; i < lines.size(); i++){
		DataLine l;
		l.raw = lines[i];
		l.number = (int)i + 1;
		l.tooLong = lines[i].size() > 511;
		std::string s = lines[i];
		for(size_t k = 0; k < s.size(); k++){ unsigned char c = (unsigned char)s[k]; if(c < 0x20 || c == ',') s[k] = ' '; }
		l.norm = trim(s);
		SplitTokens(l.norm, l.tok);
		out.push_back(l);
	}
	return true;
}

bool IsIntToken(const std::string &t)
{
	if(t.empty()) return false;
	size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
	if(i >= t.size()) return false;
	for(; i < t.size(); i++) if(t[i] < '0' || t[i] > '9') return false;
	return true;
}

bool IsNumToken(const std::string &t)
{
	if(t.empty()) return false;
	char *e = nullptr;
	strtod(t.c_str(), &e);
	return e && *e == '\0';
}

bool IsHexToken(const std::string &t)
{
	if(t.empty()) return false;
	size_t i = 0;
	if(t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) i = 2;
	for(; i < t.size(); i++) if(!isxdigit((unsigned char)t[i])) return false;
	return true;
}

// -------------------------------------------------------------- export ---

static std::string csvEscape(const std::string &s)
{
	std::string r = "\"";
	for(size_t i = 0; i < s.size(); i++){
		if(s[i] == '"') r += "\"\"";
		else r.push_back(s[i]);
	}
	r += "\"";
	return r;
}

static std::string jsonEscape(const std::string &s)
{
	std::string r;
	for(size_t i = 0; i < s.size(); i++){
		unsigned char c = (unsigned char)s[i];
		switch(c){
		case '"': r += "\\\""; break;
		case '\\': r += "\\\\"; break;
		case '\n': r += "\\n"; break;
		case '\r': r += "\\r"; break;
		case '\t': r += "\\t"; break;
		default:
			if(c < 0x20) r += fmt("\\u%04x", c);
			else r.push_back((char)c);
		}
	}
	return r;
}

bool ExportTxt(const Report &rep, const GameData &gd, const std::string &path, bool hideVanilla)
{
	FILE *f = openWriteUtf8(path);
	if(f == nullptr) return false;
	const Counters &c = rep.counters;
	int v[SEV_NUM]; int vTotal = 0;
	for(int s = 0; s < SEV_NUM; s++){ v[s] = hideVanilla ? c.vanillaBySev[s] : 0; vTotal += v[s]; }
	fprintf(f, "\xEF\xBB\xBF");	// BOM so Notepad shows Cyrillic
	fprintf(f, "%s", withVersion(T("gtacheck — отчёт проверки\n")).c_str());
	fprintf(f, T("Папка: %s\nИгра: %s\n"), gd.root.c_str(), gameName(gd.game));
	fprintf(f, T("Проблем: %d (краш %d, ошибок %d, предупреждений %d, инфо %d)\n"),
	        (int)rep.issues.size() - vTotal, c.bySev[SEV_FATAL] - v[SEV_FATAL], c.bySev[SEV_ERROR] - v[SEV_ERROR],
	        c.bySev[SEV_WARN] - v[SEV_WARN], c.bySev[SEV_INFO] - v[SEV_INFO]);
	if(vTotal) fprintf(f, T("Скрыто ванильных: %d (те же сообщения есть у чистой игры (SA 1.0 US / III / VC))\n"), vTotal);
	fprintf(f, "\n");
	for(int s = SEV_FATAL; s >= SEV_INFO; s--){
		bool any = false;
		for(size_t i = 0; i < rep.issues.size(); i++){
			const Issue &is = rep.issues[i];
			if(is.sev != s) continue;
			if(hideVanilla && is.vanilla) continue;
			if(!any){ fprintf(f, "=== %s ===\n", severityName(s)); any = true; }
			fprintf(f, "[%s] %s", is.code.c_str(), is.file.c_str());
			if(is.line >= 0) fprintf(f, ":%d", is.line);
			if(!is.object.empty()) fprintf(f, "  (%s)", is.object.c_str());
			fprintf(f, "\n    %s\n", is.msg.c_str());
			if(!is.detail.empty()) fprintf(f, "    → %s\n", is.detail.c_str());
		}
		if(any) fprintf(f, "\n");
	}
	fclose(f);
	return true;
}

bool ExportCsv(const Report &rep, const std::string &path, bool hideVanilla)
{
	FILE *f = openWriteUtf8(path);
	if(f == nullptr) return false;
	fprintf(f, "\xEF\xBB\xBFseverity;category;code;file;line;id;object;message;detail\n");
	for(size_t i = 0; i < rep.issues.size(); i++){
		const Issue &is = rep.issues[i];
		if(hideVanilla && is.vanilla) continue;
		fprintf(f, "%s;%s;%s;%s;%d;%d;%s;%s;%s\n", severityName(is.sev), categoryName(is.cat),
		        is.code.c_str(), csvEscape(is.file).c_str(), is.line, is.id, csvEscape(is.object).c_str(),
		        csvEscape(is.msg).c_str(), csvEscape(is.detail).c_str());
	}
	fclose(f);
	return true;
}

bool ExportJson(const Report &rep, const GameData &gd, const std::string &path, bool hideVanilla)
{
	FILE *f = openWriteUtf8(path);
	if(f == nullptr) return false;
	fprintf(f, "{\n  \"root\": \"%s\",\n  \"game\": \"%s\",\n  \"issues\": [\n",
	        jsonEscape(gd.root).c_str(), gameName(gd.game));
	bool first = true;
	for(size_t i = 0; i < rep.issues.size(); i++){
		const Issue &is = rep.issues[i];
		if(hideVanilla && is.vanilla) continue;
		fprintf(f, "%s    {\"severity\": \"%s\", \"category\": \"%s\", \"code\": \"%s\", \"file\": \"%s\", \"line\": %d, \"id\": %d, \"object\": \"%s\", \"message\": \"%s\", \"detail\": \"%s\", \"vanilla\": %s}",
		        first ? "" : ",\n", severityName(is.sev), categoryName(is.cat), jsonEscape(is.code).c_str(),
		        jsonEscape(is.file).c_str(), is.line, is.id, jsonEscape(is.object).c_str(),
		        jsonEscape(is.msg).c_str(), jsonEscape(is.detail).c_str(), is.vanilla ? "true" : "false");
		first = false;
	}
	fprintf(f, "\n  ]\n}\n");
	fclose(f);
	return true;
}

static std::string htmlEscape(const std::string &s)
{
	std::string r;
	for(size_t i = 0; i < s.size(); i++){
		switch(s[i]){
		case '&': r += "&amp;"; break;
		case '<': r += "&lt;"; break;
		case '>': r += "&gt;"; break;
		case '"': r += "&quot;"; break;
		default: r.push_back(s[i]);
		}
	}
	return r;
}

// One self-contained page: the summary, every issue worst-first (code links to its rule), and the
// rule help for each code that occurs — the same texts the details pane and the reference show.
bool ExportHtml(const Report &rep, const GameData &gd, const std::string &path, bool hideVanilla, bool hideIgnored)
{
	FILE *f = openWriteUtf8(path);
	if(f == nullptr) return false;
	std::vector<size_t> order;
	int bySev[SEV_NUM] = {0};
	int vTotal = 0, iTotal = 0;
	std::set<std::string> codes;
	for(size_t i = 0; i < rep.issues.size(); i++){
		const Issue &is = rep.issues[i];
		if(hideVanilla && is.vanilla){ vTotal++; continue; }
		if(hideIgnored && is.ignored){ iTotal++; continue; }
		order.push_back(i);
		bySev[is.sev]++;
		codes.insert(is.code);
	}
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b){ return rep.issues[a].sev > rep.issues[b].sev; });
	static const char *sevClass[SEV_NUM] = { "info", "warn", "error", "fatal" };
	fprintf(f, "<!doctype html>\n<html><head><meta charset=\"utf-8\">\n<title>gtacheck v" GC_VERSION " — %s</title>\n", htmlEscape(gd.root).c_str());
	fprintf(f, "<style>\n"
	        "body{background:#1b1b1d;color:#e6e6e6;font:15px/1.45 Segoe UI,Arial,sans-serif;margin:0;padding:24px 32px}\n"
	        "h1{font-size:22px;margin:0 0 6px} h2{font-size:18px;margin:32px 0 10px;border-bottom:1px solid #3a3a3f;padding-bottom:4px}\n"
	        "h3{font-size:15px;margin:18px 0 4px;font-family:Consolas,monospace} p{margin:4px 0}\n"
	        ".sum{white-space:pre-line;color:#bdbdc2} .sum b{color:#fff}\n"
	        "table{border-collapse:collapse;width:100%%;margin-top:12px} th,td{text-align:left;vertical-align:top;padding:5px 8px;border-bottom:1px solid #2c2c30}\n"
	        "th{color:#9a9aa0;font-weight:600;position:sticky;top:0;background:#1b1b1d}\n"
	        "td.code{font-family:Consolas,monospace;white-space:nowrap} td.code a{text-decoration:none;font-weight:700}\n"
	        "tr.fatal td.code a{color:#ff5d62} tr.error td.code a{color:#ff9a3c} tr.warn td.code a{color:#f2d24b} tr.info td.code a{color:#7cb8ff}\n"
	        "tr.fatal td.lvl{color:#ff5d62} tr.error td.lvl{color:#ff9a3c} tr.warn td.lvl{color:#f2d24b} tr.info td.lvl{color:#7cb8ff}\n"
	        "td.file{font-family:Consolas,monospace;word-break:break-all} .det{color:#9a9aa0;display:block;margin-top:2px}\n"
	        "dl{margin:0 0 6px;padding-left:12px;border-left:3px solid #3a3a3f} dt{color:#9a9aa0;margin-top:4px} dd{margin:0 0 2px}\n"
	        "a{color:#7cb8ff} .top{font-size:13px;margin-left:10px}\n"
	        "</style></head><body>\n");
	fprintf(f, "<h1 id=\"top\">%s</h1>\n", htmlEscape(withVersion(T("gtacheck — отчёт проверки"))).c_str());
	fprintf(f, "<p class=\"sum\">%s", htmlEscape(fmt(T("Папка: %s\nИгра: %s\n"), gd.root.c_str(), gameName(gd.game))).c_str());
	fprintf(f, "%s", htmlEscape(fmt(T("Проблем: %d (краш %d, ошибок %d, предупреждений %d, инфо %d)\n"),
	        (int)order.size(), bySev[SEV_FATAL], bySev[SEV_ERROR], bySev[SEV_WARN], bySev[SEV_INFO])).c_str());
	if(vTotal) fprintf(f, "%s", htmlEscape(fmt(T("Скрыто ванильных: %d (те же сообщения есть у чистой игры (SA 1.0 US / III / VC))\n"), vTotal)).c_str());
	if(iTotal) fprintf(f, "%s", htmlEscape(fmt(T("Скрыто игнорируемых: %d (gta_check_ignore.txt)\n"), iTotal)).c_str());
	fprintf(f, "</p>\n<table>\n<tr><th>%s</th><th>%s</th><th>%s</th><th>%s</th><th>%s</th><th>%s</th></tr>\n",
	        htmlEscape(T("Уровень")).c_str(), htmlEscape(T("Код")).c_str(), htmlEscape(T("Категория")).c_str(),
	        htmlEscape(T("Файл")).c_str(), htmlEscape(T("Объект")).c_str(), htmlEscape(T("Сообщение")).c_str());
	for(size_t k = 0; k < order.size(); k++){
		const Issue &is = rep.issues[order[k]];
		std::string file = htmlEscape(is.file);
		if(is.line >= 0) file += fmt(":%d", is.line);
		std::string msg = htmlEscape(is.msg);
		if(!is.detail.empty()) msg += "<span class=\"det\">" + htmlEscape(is.detail) + "</span>";
		fprintf(f, "<tr class=\"%s\"><td class=\"lvl\">%s</td><td class=\"code\"><a href=\"#r-%s\">%s</a></td><td>%s</td><td class=\"file\">%s</td><td>%s</td><td>%s</td></tr>\n",
		        sevClass[is.sev], htmlEscape(severityName(is.sev)).c_str(), htmlEscape(is.code).c_str(), htmlEscape(is.code).c_str(),
		        htmlEscape(categoryName(is.cat)).c_str(), file.c_str(), htmlEscape(is.object).c_str(), msg.c_str());
	}
	fprintf(f, "</table>\n<h2>%s</h2>\n", htmlEscape(T("Справочник кодов")).c_str());
	for(std::set<std::string>::const_iterator it = codes.begin(); it != codes.end(); ++it){
		std::string what, risk, fix;
		fprintf(f, "<h3 id=\"r-%s\">%s <a class=\"top\" href=\"#top\">↑</a></h3>\n", htmlEscape(*it).c_str(), htmlEscape(*it).c_str());
		if(!RuleHelp(*it, what, risk, fix)){ fprintf(f, "<p>%s</p>\n", htmlEscape(T("Пояснения для этого кода пока нет")).c_str()); continue; }
		fprintf(f, "<dl><dt>%s</dt><dd>%s</dd><dt>%s</dt><dd>%s</dd><dt>%s</dt><dd>%s</dd></dl>\n",
		        htmlEscape(T("Что это")).c_str(), htmlEscape(what).c_str(), htmlEscape(T("Чем грозит")).c_str(), htmlEscape(risk).c_str(),
		        htmlEscape(T("Что делать")).c_str(), htmlEscape(fix).c_str());
	}
	fprintf(f, "</body></html>\n");
	fclose(f);
	return true;
}

} // namespace gc
