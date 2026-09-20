// gtacheck — in-place fixes for issues the tool can repair safely.
//
// FIX_DFF_FRAMENAME: rewrites every NodeName plugin in the FRAMELIST of a DFF
// so the chunk is exactly strlen(name) bytes (no zero padding) and the name is
// at most 23 characters; EXTENSION / FRAMELIST / CLUMP sizes are recomputed and
// the rest of the file is copied verbatim.  The result goes back to where the
// engine reads it (IMG entry or loose modloader file); the original bytes are
// saved under <game>\gta_check_backup\ first.
#include "gtacheck.h"

#include <algorithm>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gc {

static void put32(std::vector<uint8_t> &v, size_t at, uint32_t x)
{
	v[at] = (uint8_t)x; v[at+1] = (uint8_t)(x >> 8); v[at+2] = (uint8_t)(x >> 16); v[at+3] = (uint8_t)(x >> 24);
}
static uint32_t get32(const std::vector<uint8_t> &v, size_t at)
{
	return (uint32_t)v[at] | ((uint32_t)v[at+1] << 8) | ((uint32_t)v[at+2] << 16) | ((uint32_t)v[at+3] << 24);
}

// Frames of a DFF as the NodeName plugins store them (name up to the first NUL, chunk length).
bool ListDffFrames(const std::vector<uint8_t> &data, std::vector<FrameName> &out, std::string &err)
{
	out.clear();
	Buf b(data.data(), data.size());
	Chunk clump;
	if(!findChunk(b, 0x10, clump, data.size())){ err = T("нет чанка CLUMP"); return false; }
	Chunk fl;
	if(!findChunk(b, 0xE, fl, clump.end)){ err = T("нет FRAMELIST"); return false; }
	Chunk st;
	if(!findChunk(b, 0x1, st, fl.end)){ err = T("FRAMELIST без STRUCT"); return false; }
	int numFrames = (int)get32(data, st.start);
	if(numFrames < 0 || numFrames > 100000){ err = T("мусорное число фреймов"); return false; }
	size_t pos = st.end;
	for(int i = 0; i < numFrames; i++){
		b.seek(pos);
		Chunk ext;
		if(!findChunk(b, 0x3, ext, fl.end)){ err = fmt(T("фрейм %d без EXTENSION"), i); return false; }
		FrameName fn;
		fn.index = i;
		fn.chunkLen = -1;
		fn.isAtomic = false;
		size_t q = ext.start;
		while(q + 12 <= ext.end){
			b.seek(q);
			Chunk c;
			readChunk(b, c);
			if(c.end > ext.end || c.truncated) break;
			if(c.type == 0x253F2FE){
				std::string name((const char*)data.data() + c.start, c.end - c.start);
				size_t z = name.find('\0');
				if(z != std::string::npos) name.resize(z);
				fn.name = name;
				fn.chunkLen = (int)(c.end - c.start);
			}
			q = c.end;
		}
		out.push_back(fn);
		pos = ext.end;
	}
	// which frames carry an atomic: ATOMIC(0x14) STRUCT = { frameIndex, geometryIndex, flags, unused }
	b.seek(fl.end);
	Chunk gl;
	if(findChunk(b, 0x1A, gl, clump.end)){
		b.seek(gl.end);
		Chunk a;
		while(findChunk(b, 0x14, a, clump.end)){
			Chunk as;
			size_t save = a.end;
			if(findChunk(b, 0x1, as, a.end) && as.end - as.start >= 4){
				int fi = (int)get32(data, as.start);
				if(fi >= 0 && fi < (int)out.size()) out[(size_t)fi].isAtomic = true;
			}
			b.seek(save);
		}
	}
	return true;
}

// Rewrites the NodeName plugin of every frame with names[i] (exact length, no padding; a frame
// with no NodeName plugin and an empty name is left alone). `data` is replaced on success;
// `changed` counts frames whose chunk differs from the original.
bool RewriteDffFrameNames(std::vector<uint8_t> &data, const std::vector<std::string> &names, int &changed, std::string &err)
{
	changed = 0;
	Buf b(data.data(), data.size());
	Chunk clump;
	if(!findChunk(b, 0x10, clump, data.size())){ err = T("нет чанка CLUMP"); return false; }
	if(clump.truncated){ err = T("CLUMP обрезан"); return false; }
	size_t clumpHdr = clump.start - 12;
	Chunk fl;
	if(!findChunk(b, 0xE, fl, clump.end)){ err = T("нет FRAMELIST"); return false; }
	size_t flHdr = fl.start - 12;
	Chunk st;
	if(!findChunk(b, 0x1, st, fl.end)){ err = T("FRAMELIST без STRUCT"); return false; }
	int numFrames = (int)get32(data, st.start);
	if(numFrames < 0 || numFrames > 100000){ err = T("мусорное число фреймов"); return false; }
	if((int)names.size() != numFrames){ err = T("число имён не совпадает с числом фреймов"); return false; }
	for(int i = 0; i < numFrames; i++) if(names[(size_t)i].size() > 23){ err = fmt(T("фрейм %d: имя длиннее 23 символов"), i); return false; }

	std::vector<uint8_t> body(data.begin() + (long)fl.start, data.begin() + (long)st.end);
	size_t pos = st.end;
	for(int i = 0; i < numFrames; i++){
		b.seek(pos);
		Chunk ext;
		if(!findChunk(b, 0x3, ext, fl.end)){ err = fmt(T("фрейм %d без EXTENSION"), i); return false; }
		if(ext.truncated){ err = T("EXTENSION обрезан"); return false; }
		size_t extHdr = ext.start - 12;
		body.insert(body.end(), data.begin() + (long)pos, data.begin() + (long)extHdr);
		std::vector<uint8_t> extBody;
		size_t q = ext.start;
		bool hadName = false;
		while(q + 12 <= ext.end){
			b.seek(q);
			Chunk c;
			readChunk(b, c);
			if(c.end > ext.end || c.truncated){ err = fmt(T("фрейм %d: плагин выходит за EXTENSION"), i); return false; }
			if(c.type == 0x253F2FE){
				hadName = true;
				const std::string &name = names[(size_t)i];
				std::vector<uint8_t> hdr(data.begin() + (long)(c.start - 12), data.begin() + (long)c.start);
				put32(hdr, 4, (uint32_t)name.size());
				bool same = name.size() == c.end - c.start && memcmp(name.data(), data.data() + c.start, name.size()) == 0;
				if(!same) changed++;
				extBody.insert(extBody.end(), hdr.begin(), hdr.end());
				extBody.insert(extBody.end(), name.begin(), name.end());
			}else
				extBody.insert(extBody.end(), data.begin() + (long)(c.start - 12), data.begin() + (long)c.end);
			q = c.end;
		}
		if(!hadName && !names[(size_t)i].empty()){
			// frame had no NodeName plugin: add one (RW 3.6 header) so the name can be set
			std::vector<uint8_t> hdr(12, 0);
			put32(hdr, 0, 0x253F2FE);
			put32(hdr, 4, (uint32_t)names[(size_t)i].size());
			put32(hdr, 8, 0x1803FFFF);
			extBody.insert(extBody.end(), hdr.begin(), hdr.end());
			extBody.insert(extBody.end(), names[(size_t)i].begin(), names[(size_t)i].end());
			changed++;
		}
		std::vector<uint8_t> hdr(data.begin() + (long)extHdr, data.begin() + (long)ext.start);
		put32(hdr, 4, (uint32_t)extBody.size());
		body.insert(body.end(), hdr.begin(), hdr.end());
		body.insert(body.end(), extBody.begin(), extBody.end());
		pos = ext.end;
	}
	if(changed == 0) return true;
	body.insert(body.end(), data.begin() + (long)pos, data.begin() + (long)fl.end);

	std::vector<uint8_t> out;
	out.reserve(data.size() + 256);
	out.insert(out.end(), data.begin(), data.begin() + (long)fl.start);
	put32(out, flHdr + 4, (uint32_t)body.size());
	out.insert(out.end(), body.begin(), body.end());
	out.insert(out.end(), data.begin() + (long)fl.end, data.end());
	long delta = (long)body.size() - (long)(fl.end - fl.start);
	put32(out, clumpHdr + 4, (uint32_t)((long)clump.size + delta));
	data.swap(out);
	return true;
}

// Strip zero padding, cut to 23 characters.
std::string TrimFrameName(const std::string &name)
{
	std::string n = name;
	size_t z = n.find('\0');
	if(z != std::string::npos) n.resize(z);
	if(n.size() > 23) n.resize(23);
	return n;
}

static bool endsWithCI(const std::string &s, const char *suf)
{
	size_t n = strlen(suf);
	if(s.size() < n) return false;
	for(size_t i = 0; i < n; i++) if(tolower((unsigned char)s[s.size() - n + i]) != tolower((unsigned char)suf[i])) return false;
	return true;
}

// The automatic rule: an objs/tobj model's atomic frame is named after the model (IDE name),
// keeping a `_dam` suffix; everything else (helper frames, clump models = bones/dummies) is only trimmed.
std::string AutoFrameName(const FrameName &frame, const FixHint &hint)
{
	std::string orig = TrimFrameName(frame.name);
	if(!hint.atomicModel || hint.modelName.empty() || !frame.isAtomic) return orig;
	std::string base = hint.modelName;
	if(base.size() > 23) base.resize(23);
	if(endsWithCI(frame.name, "_dam")){
		if(base.size() > 19) base.resize(19);
		return base + "_dam";
	}
	return base;
}

FixHint HintForEntry(const GameData &gd, int entryIdx)
{
	FixHint h;
	if(entryIdx < 0 || entryIdx >= (int)gd.entries.size()) return h;
	for(size_t i = 0; i < gd.objs.size(); i++){
		const ObjDef &o = gd.objs[i];
		if(o.dffEntry != entryIdx) continue;
		h.modelName = o.name;
		h.atomicModel = o.type == OT_OBJS || o.type == OT_TOBJ;
		return h;
	}
	// no IDE definition: use the entry's own name, assume a map object
	h.modelName = stem(gd.entries[(size_t)entryIdx].name);
	h.atomicModel = true;
	return h;
}

bool FixDffFrameNames(std::vector<uint8_t> &data, const FixHint &hint, int &fixed, std::string &err)
{
	std::vector<FrameName> frames;
	if(!ListDffFrames(data, frames, err)) return false;
	std::vector<std::string> names;
	for(size_t i = 0; i < frames.size(); i++) names.push_back(AutoFrameName(frames[i], hint));
	return RewriteDffFrameNames(data, names, fixed, err);
}

#ifdef _WIN32
static std::wstring wide(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
	if(n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
	return w;
}
#endif

static bool writeWhole(const std::string &path, const std::vector<uint8_t> &data)
{
#ifdef _WIN32
	FILE *f = _wfopen(wide(path).c_str(), L"wb");
#else
	FILE *f = fopen(path.c_str(), "wb");
#endif
	if(!f) return false;
	size_t w = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
	fclose(f);
	return w == data.size();
}

static bool ensureDir(const std::string &p)
{
#ifdef _WIN32
	CreateDirectoryW(wide(p).c_str(), nullptr);
	return dirExists(p);
#else
	return false;
#endif
}

// Saves the original bytes of an entry under <root>/gta_check_backup/<img or "loose">/<name>.
static bool backupEntry(const GameData &gd, const Entry &e, const std::vector<uint8_t> &orig, std::string &backupPath)
{
	std::string dir = joinPath(gd.root, "gta_check_backup");
	if(!ensureDir(dir)) return false;
	std::string sub = e.img >= 0 ? stem(gd.archives[(size_t)e.img].logical) : "loose";
	dir = joinPath(dir, sub);
	if(!ensureDir(dir)) return false;
	backupPath = joinPath(dir, e.name);
	if(fileExists(backupPath)) return true;	// keep the first (true original)
	return writeWhole(backupPath, orig);
}

// Writes `data` back to the entry's real location. Shrinking or equal size only for IMG entries
// (the fixes here never grow a file); the directory's streaming size is updated.
static bool writeEntryBack(GameData &gd, int entryIdx, const std::vector<uint8_t> &data, std::string &err, uint32_t maxSectors = 0)
{
	Entry &e = gd.entries[(size_t)entryIdx];
	if(maxSectors == 0) maxSectors = e.sizeSectors;
	if(!e.loose.empty()){
		if(!writeWhole(e.loose, data)){ err = T("не удалось записать ") + e.loose; return false; }
		e.sizeSectors = (uint32_t)((data.size() + 2047) / 2048);
		return true;
	}
	if(e.img < 0){ err = T("запись без источника"); return false; }
	Archive &a = gd.archives[(size_t)e.img];
	uint32_t newSectors = (uint32_t)((data.size() + 2047) / 2048);
	if(newSectors > maxSectors){ err = T("новый файл больше исходной записи IMG — перепаковка архива не поддерживается"); return false; }
#ifdef _WIN32
	FILE *f = _wfopen(wide(a.phys).c_str(), L"r+b");
#else
	FILE *f = fopen(a.phys.c_str(), "r+b");
#endif
	if(!f){ err = T("не удалось открыть архив для записи: ") + a.phys; return false; }
	std::vector<uint8_t> padded(data);
	padded.resize((size_t)newSectors * 2048, 0);
#ifdef _WIN32
	_fseeki64(f, (long long)e.offset * 2048, SEEK_SET);
#else
	fseeko(f, (off_t)e.offset * 2048, SEEK_SET);
#endif
	bool ok = fwrite(padded.data(), 1, padded.size(), f) == padded.size();
	if(ok && a.ver2 && e.dirIndex >= 0){
		// VER2 directory: [4 "VER2"][4 count] then 32-byte entries {u32 offset, u16 streamingSize, u16 sizeInArchive, char name[24]}
		uint8_t sz[2] = { (uint8_t)newSectors, (uint8_t)(newSectors >> 8) };
		_fseeki64(f, 8 + (long long)e.dirIndex * 32 + 4, SEEK_SET);
		ok = fwrite(sz, 1, 2, f) == 2;
	}else if(ok && !a.ver2 && e.dirIndex >= 0){
		// III/VC: separate .dir with {u32 offset, u32 size, char name[24]}
		std::string dirPath = a.phys.substr(0, a.phys.size() - 4) + ".dir";
		FILE *d = fopen(dirPath.c_str(), "r+b");
		if(d){ uint8_t sz[4] = { (uint8_t)newSectors, (uint8_t)(newSectors >> 8), 0, 0 }; fseek(d, (long)e.dirIndex * 32 + 4, SEEK_SET); ok = fwrite(sz, 1, 4, d) == 4; fclose(d); }
	}
	fclose(f);
	if(!ok){ err = T("ошибка записи в ") + a.phys; return false; }
	e.sizeSectors = newSectors;
	return true;
}

// Reads an entry with the IMG sector padding trimmed to the real RW size.
bool ReadEntryTrimmed(const GameData &gd, int entryIdx, std::vector<uint8_t> &data, std::string &err)
{
	if(!gd.readEntry(entryIdx, data, &err)) return false;
	const Entry &e = gd.entries[(size_t)entryIdx];
	if(e.img >= 0 && data.size() >= 12){
		Buf b(data.data(), data.size());
		Chunk c;
		size_t endReal = 0;
		bool oldClump = false;	// III-era CLUMPs (0x310/0x32000) often understate their size and keep the EXTENSION past it
		while(b.pos + 12 <= b.n && readChunk(b, c) && !c.truncated && c.type != 0 && c.end >= c.start){ if(c.type == 0x10 && c.version < 0x33000) oldClump = true; endReal = c.end; b.seek(c.end); }
		if(oldClump) endReal = data.size();	// the chunk sizes cannot be trusted (the EXTENSION header may straddle the last non-zero byte): keep everything
		if(endReal > 0 && endReal <= data.size()) data.resize(endReal);
	}
	return true;
}

bool EnsureDir(const std::string &p) { return ensureDir(p); }
bool WriteBytes(const std::string &path, const std::vector<uint8_t> &data) { return writeWhole(path, data); }

static std::string gOutDir;
void SetOutputDir(const std::string &dir) { gOutDir = dir; }
const std::string &OutputDir() { return gOutDir; }

std::string OutputPathFor(const std::string &logicalOrName)
{
	std::string rel = normSlashes(logicalOrName);
	while(!rel.empty() && (rel[0] == '/' || rel[0] == '.')) rel.erase(0, 1);
	// "archive.img/entry" → a folder named after the archive, without the extension (the user's request: <outdir>\gta3\entry);
	// a deeper path before the archive is dropped; drive paths → basename
	size_t sl = rel.find(".img/"); if(sl == std::string::npos) sl = rel.find(".IMG/");
	if(sl != std::string::npos){ size_t st = rel.rfind('/', sl); std::string arch = rel.substr(st == std::string::npos ? 0 : st + 1, sl - (st == std::string::npos ? 0 : st + 1)); rel = arch + "/" + rel.substr(sl + 5); }
	else if(rel.size() > 1 && rel[1] == ':') rel = basename(rel);
	std::string cur = gOutDir;
	ensureDir(cur);
	size_t a = 0;
	for(;;){
		size_t b = rel.find('/', a);
		if(b == std::string::npos) break;
		cur = joinPath(cur, rel.substr(a, b - a));
		ensureDir(cur);
		a = b + 1;
	}
	std::string out = joinPath(cur, rel.substr(a));
#ifdef _WIN32
	for(size_t i = 0; i < out.size(); i++) if(out[i] == '/') out[i] = '\\';
#endif
	return out;
}

// ---- undo journal ----
// <root>\gta_check_backup\undo.txt: one line per write, tab-separated:
//   img  <archive phys>  <entry name>  <sectors before>  <undo copy>  <label>
//   file <phys>          <undo copy>   <label>
//   new  <phys>          <label>                                (a file that did not exist before: undo deletes it)
// The undo copies live in gta_check_backup\undo\ (one per write — unlike the first-original backups next to them).
static std::string undoDir(const std::string &root) { return joinPath(joinPath(root, "gta_check_backup"), "undo"); }
static std::string undoJournal(const std::string &root) { return joinPath(joinPath(root, "gta_check_backup"), "undo.txt"); }
static bool undoCopy(const std::string &root, const std::string &name, const std::vector<uint8_t> &bytes, std::string &path)
{
	if(!ensureDir(joinPath(root, "gta_check_backup")) || !ensureDir(undoDir(root))) return false;
	static unsigned seq = 0;
	std::string base = name; for(size_t i = 0; i < base.size(); i++) if(base[i] == '/' || base[i] == '\\' || base[i] == ':') base[i] = '_';
	path = joinPath(undoDir(root), fmt("%llu_%u_%s", (unsigned long long)time(nullptr), ++seq, base.c_str()));
	return writeWhole(path, bytes);
}
static FILE *openUtf8(const std::string &path, const char *mode)
{
#ifdef _WIN32
	std::wstring wm(mode, mode + strlen(mode));
	return _wfopen(wide(path).c_str(), wm.c_str());
#else
	return fopen(path.c_str(), mode);
#endif
}
static void undoAppend(const std::string &root, const std::string &line)
{
	FILE *f = openUtf8(undoJournal(root), "ab");
	if(!f) return;
	fprintf(f, "%s\n", line.c_str());
	fclose(f);
}
static std::vector<std::string> undoLines(const std::string &root)
{
	std::vector<std::string> out;
	std::vector<uint8_t> d;
	if(!readFile(undoJournal(root), d)) return out;
	std::string all(d.begin(), d.end()), cur;
	for(size_t i = 0; i < all.size(); i++){ if(all[i] == '\n'){ if(!cur.empty()) out.push_back(cur); cur.clear(); } else if(all[i] != '\r') cur += all[i]; }
	if(!cur.empty()) out.push_back(cur);
	return out;
}
static std::vector<std::string> splitTabs(const std::string &s)
{
	std::vector<std::string> f; std::string cur;
	for(size_t i = 0; i < s.size(); i++){ if(s[i] == '\t'){ f.push_back(cur); cur.clear(); } else cur += s[i]; }
	f.push_back(cur);
	return f;
}
int UndoCount(const std::string &root) { return (int)undoLines(root).size(); }
std::string UndoLastLabel(const std::string &root)
{
	std::vector<std::string> l = undoLines(root);
	if(l.empty()) return "";
	std::vector<std::string> f = splitTabs(l.back());
	return f.empty() ? "" : f.back();
}
bool UndoLastFix(GameData &gd, std::string &msg)
{
	std::vector<std::string> lines = undoLines(gd.root);
	if(lines.empty()){ msg = T("Откатывать нечего"); return false; }
	std::vector<std::string> f = splitTabs(lines.back());
	std::string err; bool ok = false;
	if(f.size() >= 6 && f[0] == "img"){
		int e = gd.findEntry(f[2]);
		if(e < 0 || gd.entries[(size_t)e].img < 0 || lower(normSlashes(gd.archives[(size_t)gd.entries[(size_t)e].img].phys)) != lower(normSlashes(f[1]))) err = T("запись «%s» больше не там, где была");
		else{
			std::vector<uint8_t> bytes;
			if(!readFile(f[4], bytes)) err = T("копия для отката не читается: ") + f[4];
			else ok = writeEntryBack(gd, e, bytes, err, (uint32_t)atoi(f[3].c_str()));
		}
		if(err.find("%s") != std::string::npos) err = fmt(err.c_str(), f[2].c_str());
	}else if(f.size() >= 4 && f[0] == "file"){
		std::vector<uint8_t> bytes;
		if(!readFile(f[2], bytes)) err = T("копия для отката не читается: ") + f[2];
		else if(!writeWhole(f[1], bytes)) err = T("не удалось записать ") + f[1];
		else ok = true;
	}else if(f.size() >= 3 && f[0] == "new"){
#ifdef _WIN32
		ok = DeleteFileW(wide(f[1]).c_str()) != 0 || !fileExists(f[1]);
#else
		ok = remove(f[1].c_str()) == 0;
#endif
		if(!ok) err = T("не удалось удалить ") + f[1];
	}else err = T("непонятная запись журнала");
	if(!ok){ msg = T("Откат не удался: ") + err; return false; }
	// drop the line (and its copy) from the journal
	if(f[0] != "new"){ const std::string &copy = f[0] == "img" ? f[4] : f[2];
#ifdef _WIN32
		DeleteFileW(wide(copy).c_str());
#else
		remove(copy.c_str());
#endif
	}
	lines.pop_back();
	if(lines.empty()){	// nothing left: no empty journal lying around
#ifdef _WIN32
		DeleteFileW(wide(undoJournal(gd.root)).c_str());
#else
		remove(undoJournal(gd.root).c_str());
#endif
	}else{
		FILE *w = openUtf8(undoJournal(gd.root), "wb");
		if(w){ for(size_t i = 0; i < lines.size(); i++) fprintf(w, "%s\n", lines[i].c_str()); fclose(w); }
	}
	msg = fmt(T("Откачено: %s"), f.back().c_str());
	return true;
}

bool SaveTextFile(const GameData &gd, const std::string &logical, const std::string &phys, const std::vector<uint8_t> &orig, const std::vector<uint8_t> &data, std::string &backup, std::string &err)
{
	// the output folder takes only what lived inside an IMG (phys empty: "archive.img/name"); loose files —
	// IPL/IDE/data, modloader TXD/DFF — are written in place with a backup (the user's rule, 16.09)
	if(!gOutDir.empty() && phys.empty()){
		backup = OutputPathFor(logical);
		bool existed = fileExists(backup);
		std::vector<uint8_t> before; if(existed) readFile(backup, before);
		if(!writeWhole(backup, data)){ err = T("не удалось записать ") + backup; return false; }
		std::string copy;
		if(!existed) undoAppend(gd.root, "new\t" + backup + "\t" + logical);
		else if(undoCopy(gd.root, basename(backup), before, copy)) undoAppend(gd.root, "file\t" + backup + "\t" + copy + "\t" + logical);
		return true;
	}
	std::string dir = joinPath(gd.root, "gta_check_backup");
	if(!ensureDir(dir)){ err = T("не удалось создать gta_check_backup"); return false; }
	dir = joinPath(dir, "text");
	if(!ensureDir(dir)){ err = T("не удалось создать gta_check_backup"); return false; }
	std::string name = normSlashes(logical);
	for(size_t i = 0; i < name.size(); i++) if(name[i] == '/' || name[i] == ':') name[i] = '_';
	backup = joinPath(dir, name);
	if(!fileExists(backup) && !writeWhole(backup, orig)){ err = T("не удалось сохранить копию оригинала — файл не тронут"); return false; }
	bool existed = fileExists(phys);
	if(!writeWhole(phys, data)){ err = T("не удалось записать ") + phys; return false; }
	std::string copy;
	if(!existed) undoAppend(gd.root, "new\t" + phys + "\t" + logical);
	else if(undoCopy(gd.root, basename(phys), orig, copy)) undoAppend(gd.root, "file\t" + phys + "\t" + copy + "\t" + logical);
	return true;
}

// Writes rewritten bytes back with a backup of the original entry. Returns false with `err`.
bool WriteEntryFixed(GameData &gd, int entryIdx, const std::vector<uint8_t> &orig, const std::vector<uint8_t> &data, std::string &backup, std::string &err)
{
	if(entryIdx < 0 || entryIdx >= (int)gd.entries.size()){ err = T("нет такой записи"); return false; }
	const Entry &en = gd.entries[(size_t)entryIdx];
	if(!gOutDir.empty() && en.img >= 0){	// an IMG entry goes to the output folder instead of the archive: `backup` carries the written path (<outdir>\<archive>\<entry>); loose files stay in place
		backup = OutputPathFor(en.img >= 0 ? basename(gd.archives[(size_t)en.img].logical) + "/" + en.name : en.name);
		bool existed = fileExists(backup);
		std::vector<uint8_t> before; if(existed) readFile(backup, before);
		if(!writeWhole(backup, data)){ err = T("не удалось записать ") + backup; return false; }
		std::string copy;
		if(!existed) undoAppend(gd.root, "new\t" + backup + "\t" + en.name);
		else if(undoCopy(gd.root, en.name, before, copy)) undoAppend(gd.root, "file\t" + backup + "\t" + copy + "\t" + en.name);
		return true;
	}
	if(!backupEntry(gd, en, orig, backup)){ err = T("не удалось сохранить копию оригинала — файл не тронут"); return false; }
	uint32_t sectorsBefore = en.sizeSectors;
	if(!writeEntryBack(gd, entryIdx, data, err)) return false;
	std::string copy;
	if(en.img >= 0){ if(undoCopy(gd.root, en.name, orig, copy)) undoAppend(gd.root, "img\t" + gd.archives[(size_t)en.img].phys + "\t" + en.name + "\t" + fmt("%u", sectorsBefore) + "\t" + copy + "\t" + en.name); }
	else if(undoCopy(gd.root, en.name, orig, copy)) undoAppend(gd.root, "file\t" + en.loose + "\t" + copy + "\t" + en.name);
	return true;
}

// Applies the fixes referenced by `issueIdx` (indices into rep.issues). One file is rewritten
// once even when several issues point at it. Returns the number of files changed; `summary`
// is a human-readable log.
// IDE text fixes: one line of an objs/tobj section rewritten in place (the rest of the file byte for byte).
// FIX_IDE_DRAWDIST: draw distance(s) := 2 × model radius (rounded up to 10); FIX_IDE_ALPHA: flags |= 4.
struct IdeLineFix { int line; int kind; int id; };
static int applyIdeFixes(GameData &gd, const std::string &logical, std::vector<IdeLineFix> &fixes, std::string &summary)
{
	std::string phys = DataFilePath(gd, logical);
	std::vector<uint8_t> orig;
	if(!readFile(phys, orig)){ summary += fmt(T("%s: не прочитан\n"), logical.c_str()); return 0; }
	std::string text(orig.begin(), orig.end());
	std::vector<std::string> lines; std::string cur; bool crlf = text.find("\r\n") != std::string::npos;
	for(size_t i = 0; i < text.size(); i++){ if(text[i] == '\n'){ lines.push_back(cur); cur.clear(); } else if(text[i] != '\r') cur += text[i]; }
	bool trailing = !text.empty() && text.back() == '\n'; if(!cur.empty() || !trailing) lines.push_back(cur);
	int done = 0;
	for(size_t f = 0; f < fixes.size(); f++){
		const IdeLineFix &fx = fixes[f];
		if(fx.line < 1 || fx.line > (int)lines.size()){ summary += fmt(T("%s:%d: строки нет\n"), logical.c_str(), fx.line); continue; }
		std::string &l = lines[(size_t)fx.line - 1];
		// tokens with their positions so the edit keeps the original spacing
		std::vector<std::pair<size_t, size_t> > tok;	// start, length
		for(size_t i = 0; i < l.size();){ while(i < l.size() && (l[i] == ' ' || l[i] == ',' || l[i] == '\t')) i++; size_t s = i; while(i < l.size() && l[i] != ' ' && l[i] != ',' && l[i] != '\t') i++; if(i > s) tok.push_back(std::make_pair(s, i - s)); }
		const ObjDef *o = gd.findObj(fx.id);
		if(!o || tok.size() < 5 || atoi(l.substr(tok[0].first, tok[0].second).c_str()) != fx.id){ summary += fmt(T("%s:%d: строка не похожа на объявление модели %d\n"), logical.c_str(), fx.line, fx.id); continue; }
		// objs: id name txd [count] dist... flags ; tobj: … flags timeOn timeOff
		bool tobj = o->type == OT_TOBJ;
		int flagsIdx = (int)tok.size() - 1 - (tobj ? 2 : 0);
		int distFirst = 3, distCount = flagsIdx - distFirst;	// 1 dist (4-token objs) or count + dists
		if(distCount >= 2){ distFirst = 4; distCount = flagsIdx - distFirst; }
		if(distCount < 1 || flagsIdx < 4){ summary += fmt(T("%s:%d: не разобрал поля строки\n"), logical.c_str(), fx.line); continue; }
		std::string out = l;
		if(fx.kind == FIX_IDE_DRAWDIST){
			float want = o->dffRadius > 0 ? ceilf(o->dffRadius * 2.0f / 10.0f) * 10.0f : 0;
			if(want <= 0){ summary += fmt(T("%s: радиус модели неизвестен\n"), o->name.c_str()); continue; }
			// rebuild from the last distance backwards so earlier positions stay valid
			for(int k = distCount - 1; k >= 0; k--){
				const std::pair<size_t, size_t> &t = tok[(size_t)(distFirst + k)];
				float cur = (float)atof(l.substr(t.first, t.second).c_str());
				if(cur >= want) continue;
				out = out.substr(0, t.first) + fmt("%g", want) + out.substr(t.first + t.second);
			}
			if(out == l){ summary += fmt(T("%s: дальность уже ≥ %g\n"), o->name.c_str(), want); continue; }
			summary += fmt(T("%s: дальность → %g (радиус %.1f)\n"), o->name.c_str(), want, o->dffRadius);
		}else if(fx.kind == FIX_IDE_ALPHA){
			const std::pair<size_t, size_t> &t = tok[(size_t)flagsIdx];
			int flags = atoi(l.substr(t.first, t.second).c_str());
			if(flags & 0xC){ summary += fmt(T("%s: флаг прозрачности уже есть\n"), o->name.c_str()); continue; }
			out = out.substr(0, t.first) + fmt("%d", flags | 4) + out.substr(t.first + t.second);
			summary += fmt(T("%s: флаги %d → %d\n"), o->name.c_str(), flags, flags | 4);
		}else continue;
		l = out; done++;
	}
	if(!done) return 0;
	std::string joined; const char *nl = crlf ? "\r\n" : "\n";
	for(size_t i = 0; i < lines.size(); i++){ joined += lines[i]; if(i + 1 < lines.size() || trailing) joined += nl; }
	std::vector<uint8_t> data(joined.begin(), joined.end());
	std::string backup, err;
	if(!SaveTextFile(gd, logical, phys, orig, data, backup, err)){ summary += fmt("%s: %s\n", logical.c_str(), err.c_str()); return 0; }
	summary += gOutDir.empty() ? fmt(T("%s: записан, строк изменено %d (оригинал: %s)\n"), logical.c_str(), done, backup.c_str()) : fmt(T("%s: записан в %s, строк изменено %d\n"), logical.c_str(), backup.c_str(), done);
	return 1;
}

bool SetIplLod(GameData &gd, const std::string &logical, int line, int newLod, std::string &msg)
{
	std::string phys = DataFilePath(gd, logical);
	std::vector<uint8_t> orig;
	if(!readFile(phys, orig)){ msg = fmt(T("%s: не прочитан"), logical.c_str()); return false; }
	std::string text(orig.begin(), orig.end());
	// the line's byte range (1-based line, CRLF or LF)
	size_t ls = 0; int cur = 1;
	while(cur < line){ size_t nl = text.find('\n', ls); if(nl == std::string::npos){ msg = fmt(T("%s:%d: строки нет"), logical.c_str(), line); return false; } ls = nl + 1; cur++; }
	size_t le = text.find('\n', ls); if(le == std::string::npos) le = text.size();
	size_t lend = le; while(lend > ls && (text[lend - 1] == '\r')) lend--;
	std::string l = text.substr(ls, lend - ls);
	size_t comma = l.find_last_of(',');
	int fields = 1; for(size_t k = 0; k < l.size(); k++) if(l[k] == ',') fields++;
	if(comma == std::string::npos || fields < 11){ msg = fmt(T("%s:%d: не похоже на inst-строку SA (нет поля lod)"), logical.c_str(), line); return false; }
	size_t ds = comma + 1; while(ds < l.size() && (l[ds] == ' ' || l[ds] == '\t')) ds++;
	size_t de = ds; while(de < l.size() && (l[de] == '-' || (l[de] >= '0' && l[de] <= '9'))) de++;
	std::string out = text.substr(0, ls) + l.substr(0, ds) + fmt("%d", newLod) + l.substr(de) + text.substr(lend);
	std::vector<uint8_t> data(out.begin(), out.end());
	std::string backup, err;
	if(!SaveTextFile(gd, logical, phys, orig, data, backup, err)){ msg = err; return false; }
	msg = gOutDir.empty() ? fmt(T("%s:%d: lod → %d (оригинал: %s)"), logical.c_str(), line, newLod, backup.c_str()) : fmt(T("%s:%d: lod → %d, записан в %s"), logical.c_str(), line, newLod, backup.c_str());
	return true;
}

bool LinkIplLods(GameData &gd, const std::string &logical, int modelId, int lodId, bool link, std::string &msg)
{
	std::string phys = DataFilePath(gd, logical);
	std::vector<uint8_t> orig;
	if(!readFile(phys, orig)){ msg = fmt(T("%s: не прочитан"), logical.c_str()); return false; }
	std::string text(orig.begin(), orig.end());
	bool crlf = text.find("\r\n") != std::string::npos;
	std::vector<std::string> lines; std::string cur;
	for(size_t i = 0; i < text.size(); i++){ if(text[i] == '\n'){ lines.push_back(cur); cur.clear(); } else if(text[i] != '\r') cur += text[i]; }
	bool trailing = !text.empty() && text.back() == '\n'; if(!cur.empty() || !trailing) lines.push_back(cur);
	// inst lines with ordinals; the last inst section's «end» is where new lines go (ordinals of nothing else move)
	struct L { int line, ordinal, id, lod; float x, y, z; };
	std::vector<L> inst; int lastEnd = -1; bool inInst = false;
	for(size_t i = 0; i < lines.size(); i++){
		std::string t = trim(lines[i]), tl = lower(t);
		if(!inInst){ if(tl == "inst") inInst = true; continue; }
		if(tl == "end"){ inInst = false; lastEnd = (int)i; continue; }
		if(t.empty() || t[0] == '#' || t[0] == ';') continue;
		L e; e.line = (int)i; e.ordinal = (int)inst.size(); e.id = -1; e.lod = -1; e.x = e.y = e.z = 0;
		std::vector<std::string> f; std::string c2; for(size_t k = 0; k <= t.size(); k++){ if(k == t.size() || t[k] == ','){ f.push_back(trim(c2)); c2.clear(); } else c2 += t[k]; }
		if(f.size() >= 11){ e.id = atoi(f[0].c_str()); e.x = (float)atof(f[3].c_str()); e.y = (float)atof(f[4].c_str()); e.z = (float)atof(f[5].c_str()); e.lod = atoi(f[10].c_str()); }
		inst.push_back(e);
	}
	if(lastEnd < 0){ msg = T("в файле нет секции inst"); return false; }
	auto setLod = [&](int line, int v){	// the last field, spacing kept
		std::string &l = lines[(size_t)line]; size_t comma = l.find_last_of(','); if(comma == std::string::npos) return;
		size_t ds = comma + 1; while(ds < l.size() && (l[ds] == ' ' || l[ds] == '\t')) ds++;
		size_t de = ds; while(de < l.size() && (l[de] == '-' || (l[de] >= '0' && l[de] <= '9'))) de++;
		l = l.substr(0, ds) + fmt("%d", v) + l.substr(de);
	};
	int linked = 0, added = 0, cleared = 0, kept = 0;
	std::vector<std::string> appended;
	// every copy gets a LOD of its own: a LOD line standing at the same spot (≤ 1 unit) that no other copy took;
	// otherwise a new LOD line at the copy's place. Copies sharing one far-away LOD (the «388 everywhere» case) are
	// what the game trips over.
	std::vector<char> taken(inst.size(), 0);
	if(link) for(size_t i = 0; i < inst.size(); i++){	// first pass: links that are already right stay (and claim their LOD)
		L &e = inst[i];
		if(e.id != modelId || e.lod < 0 || e.lod >= (int)inst.size()) continue;
		const L &l = inst[(size_t)e.lod];
		float dx = l.x - e.x, dy = l.y - e.y, dz = l.z - e.z;
		if(l.id == lodId && !taken[(size_t)e.lod] && dx * dx + dy * dy + dz * dz <= 1.0f){ taken[(size_t)e.lod] = 1; kept++; }
	}
	for(size_t i = 0; i < inst.size(); i++){
		L &e = inst[i];
		if(e.id != modelId) continue;
		if(!link){ if(e.lod != -1){ setLod(e.line, -1); cleared++; } else kept++; continue; }
		if(e.lod >= 0 && e.lod < (int)inst.size() && taken[(size_t)e.lod] && inst[(size_t)e.lod].id == lodId){ float dx = inst[(size_t)e.lod].x - e.x, dy = inst[(size_t)e.lod].y - e.y, dz = inst[(size_t)e.lod].z - e.z; if(dx * dx + dy * dy + dz * dz <= 1.0f) continue; }	// counted in the first pass
		int best = -1; float bestD = 1.0f;	// a free LOD at the same spot
		for(size_t k = 0; k < inst.size(); k++){ if(inst[k].id != lodId || taken[k]) continue; float dx = inst[k].x - e.x, dy = inst[k].y - e.y, dz = inst[k].z - e.z, d = dx * dx + dy * dy + dz * dz; if(d < bestD){ bestD = d; best = (int)k; } }
		if(best < 0){	// none: a LOD line at the model's own place (appended at the end of the last inst section → ordinal = count so far)
			const ObjDef *ld = gd.findObj(lodId);
			std::string t = trim(lines[(size_t)e.line]);
			std::vector<std::string> f; std::string c2; for(size_t k = 0; k <= t.size(); k++){ if(k == t.size() || t[k] == ','){ f.push_back(trim(c2)); c2.clear(); } else c2 += t[k]; }
			if(f.size() < 11 || !ld){ kept++; continue; }
			f[0] = fmt("%d", lodId); f[1] = ld->name; f[10] = "-1";
			std::string nl; for(size_t k = 0; k < f.size(); k++){ if(k) nl += ", "; nl += f[k]; }
			L ne = e; ne.id = lodId; ne.lod = -1; ne.ordinal = (int)inst.size(); ne.line = -1;
			inst.push_back(ne); appended.push_back(nl); taken.push_back(0); best = ne.ordinal; added++;
		}
		taken[(size_t)best] = 1;
		setLod(e.line, inst[(size_t)best].ordinal); e.lod = inst[(size_t)best].ordinal; linked++;
	}
	if(!linked && !added && !cleared){ msg = fmt(T("менять нечего (копий с нужной связью: %d)"), kept); return false; }
	for(size_t k = 0; k < appended.size(); k++) lines.insert(lines.begin() + lastEnd + (ptrdiff_t)k, appended[k]);
	std::string joined; const char *nl = crlf ? "\r\n" : "\n";
	for(size_t i = 0; i < lines.size(); i++){ joined += lines[i]; if(i + 1 < lines.size() || trailing) joined += nl; }
	std::vector<uint8_t> data(joined.begin(), joined.end());
	std::string backup, err;
	if(!SaveTextFile(gd, logical, phys, orig, data, backup, err)){ msg = err; return false; }
	msg = link ? fmt(T("%s: связано копий %d (добавлено LOD-строк %d, уже были верны %d) — оригинал: %s"), logical.c_str(), linked, added, kept, backup.c_str())
	           : fmt(T("%s: связь с LOD убрана у %d копий (без LOD уже были %d) — оригинал: %s"), logical.c_str(), cleared, kept, backup.c_str());
	return true;
}

bool SortIplLods(GameData &gd, const std::string &logical, std::string &msg)
{
	std::string phys = DataFilePath(gd, logical);
	std::vector<uint8_t> orig;
	if(!readFile(phys, orig)){ msg = fmt(T("%s: не прочитан"), logical.c_str()); return false; }
	std::string text(orig.begin(), orig.end());
	bool crlf = text.find("\r\n") != std::string::npos;
	std::vector<std::string> lines; std::string cur;
	for(size_t i = 0; i < text.size(); i++){ if(text[i] == '\n'){ lines.push_back(cur); cur.clear(); } else if(text[i] != '\r') cur += text[i]; }
	bool trailing = !text.empty() && text.back() == '\n'; if(!cur.empty() || !trailing) lines.push_back(cur);
	// inst lines: ordinal, line, section, lod
	struct L { int line, section, lod; };
	std::vector<L> inst; std::vector<int> lineOrdinal(lines.size(), -1);
	int section = -1; bool inInst = false;
	for(size_t i = 0; i < lines.size(); i++){
		std::string t = trim(lines[i]), tl = lower(t);
		if(!inInst){ if(tl == "inst"){ inInst = true; section++; } continue; }
		if(tl == "end"){ inInst = false; continue; }
		if(t.empty() || t[0] == '#' || t[0] == ';') continue;
		L e; e.line = (int)i; e.section = section; e.lod = -1;
		int fields = 1; for(size_t k = 0; k < t.size(); k++) if(t[k] == ',') fields++;
		size_t comma = t.find_last_of(',');
		if(fields >= 11 && comma != std::string::npos){ std::string tail = trim(t.substr(comma + 1)); char *ep = nullptr; long v = strtol(tail.c_str(), &ep, 10); if(ep != tail.c_str()) e.lod = (int)v; }
		lineOrdinal[i] = (int)inst.size(); inst.push_back(e);
	}
	if(inst.empty()){ msg = T("в файле нет секции inst"); return false; }
	// the new order: a model line, then its LOD (same section, not yet placed); everything else keeps its order
	std::vector<int> order; std::vector<char> placed(inst.size(), 0);
	for(size_t i = 0; i < inst.size(); i++){
		if(placed[i]) continue;
		order.push_back((int)i); placed[i] = 1;
		int l = inst[i].lod;
		if(l >= 0 && l < (int)inst.size() && !placed[(size_t)l] && inst[(size_t)l].section == inst[i].section){ order.push_back(l); placed[(size_t)l] = 1; }
	}
	std::vector<int> newOrdinal(inst.size(), -1);
	for(size_t k = 0; k < order.size(); k++) newOrdinal[(size_t)order[k]] = (int)k;
	bool same = true; for(size_t k = 0; k < order.size() && same; k++) if(order[k] != (int)k) same = false;
	if(same){ msg = T("порядок уже такой: за каждой моделью идёт её LOD"); return false; }
	// rebuild: inst lines are taken in the new order, the lod fields renumbered; other lines untouched
	std::vector<std::string> out; size_t next = 0; int moved = 0, renum = 0;
	for(size_t i = 0; i < lines.size(); i++){
		if(lineOrdinal[i] < 0){ out.push_back(lines[i]); continue; }
		int o = order[next++];
		std::string l = lines[(size_t)inst[(size_t)o].line];
		if(o != (int)lineOrdinal[i]) moved++;
		int lod = inst[(size_t)o].lod;
		if(lod >= 0 && lod < (int)inst.size()){
			int nv = newOrdinal[(size_t)lod];
			size_t comma = l.find_last_of(',');
			size_t ds = comma + 1; while(ds < l.size() && (l[ds] == ' ' || l[ds] == '\t')) ds++;
			size_t de = ds; while(de < l.size() && (l[de] == '-' || (l[de] >= '0' && l[de] <= '9'))) de++;
			if(nv != lod){ l = l.substr(0, ds) + fmt("%d", nv) + l.substr(de); renum++; }
		}
		out.push_back(l);
	}
	std::string joined; const char *nl = crlf ? "\r\n" : "\n";
	for(size_t i = 0; i < out.size(); i++){ joined += out[i]; if(i + 1 < out.size() || trailing) joined += nl; }
	std::vector<uint8_t> data(joined.begin(), joined.end());
	std::string backup, err;
	if(!SaveTextFile(gd, logical, phys, orig, data, backup, err)){ msg = err; return false; }
	msg = fmt(T("%s: строк переставлено %d, индексов пересчитано %d — оригинал: %s"), logical.c_str(), moved, renum, backup.c_str());
	return true;
}

int SetIdeTxdNames(GameData &gd, const std::vector<std::pair<int, std::string> > &modelTxd, std::string &summary)
{
	std::map<std::string, std::vector<std::pair<int, std::string> > > byFile;	// IDE logical → (line, txd)
	for(size_t i = 0; i < modelTxd.size(); i++){ const ObjDef *o = gd.findObj(modelTxd[i].first); if(o && o->line >= 1) byFile[o->file].push_back(std::make_pair(o->line, modelTxd[i].second)); }
	int files = 0;
	for(auto &kv : byFile){
		std::string phys = DataFilePath(gd, kv.first);
		std::vector<uint8_t> orig;
		if(!readFile(phys, orig)){ summary += fmt(T("%s: не прочитан\n"), kv.first.c_str()); continue; }
		std::string text(orig.begin(), orig.end());
		std::vector<std::string> lines; std::string cur; bool crlf = text.find("\r\n") != std::string::npos;
		for(size_t i = 0; i < text.size(); i++){ if(text[i] == '\n'){ lines.push_back(cur); cur.clear(); } else if(text[i] != '\r') cur += text[i]; }
		bool trailing = !text.empty() && text.back() == '\n'; if(!cur.empty() || !trailing) lines.push_back(cur);
		int done = 0;
		for(size_t k = 0; k < kv.second.size(); k++){
			int ln = kv.second[k].first; if(ln < 1 || ln > (int)lines.size()) continue;
			std::string &l = lines[(size_t)ln - 1];
			std::vector<std::pair<size_t, size_t> > tok;
			for(size_t i = 0; i < l.size();){ while(i < l.size() && (l[i] == ' ' || l[i] == ',' || l[i] == '\t')) i++; size_t s = i; while(i < l.size() && l[i] != ' ' && l[i] != ',' && l[i] != '\t') i++; if(i > s) tok.push_back(std::make_pair(s, i - s)); }
			if(tok.size() < 3) continue;
			l = l.substr(0, tok[2].first) + kv.second[k].second + l.substr(tok[2].first + tok[2].second);
			done++;
		}
		if(!done) continue;
		std::string joined; const char *nl = crlf ? "\r\n" : "\n";
		for(size_t i = 0; i < lines.size(); i++){ joined += lines[i]; if(i + 1 < lines.size() || trailing) joined += nl; }
		std::vector<uint8_t> data(joined.begin(), joined.end());
		std::string backup, err;
		if(!SaveTextFile(gd, kv.first, phys, orig, data, backup, err)){ summary += fmt("%s: %s\n", kv.first.c_str(), err.c_str()); continue; }
		summary += fmt(T("%s: строк с новым TXD: %d\n"), kv.first.c_str(), done);
		files++;
	}
	return files;
}

int ApplyFixes(GameData &gd, Report &rep, const std::vector<int> &issueIdx, std::string &summary)
{
	std::map<int, unsigned> byEntry;	// entry → set of fix kinds (1 << kind): a TXD row may need several
	std::map<std::string, std::vector<IdeLineFix> > byIde;	// IDE file → line fixes
	{
		std::lock_guard<std::mutex> lock(rep.mtx);
		for(size_t i = 0; i < issueIdx.size(); i++){
			int k = issueIdx[i];
			if(k < 0 || k >= (int)rep.issues.size()) continue;
			const Issue &is = rep.issues[(size_t)k];
			if(is.fix == FIX_NONE) continue;
			if(FixNeedsEntry(is.fix)){ if(is.entry >= 0) byEntry[is.entry] |= 1u << is.fix; continue; }
			if(is.line < 1 || is.id < 0) continue;
			IdeLineFix fx; fx.line = is.line; fx.kind = is.fix; fx.id = is.id;
			std::vector<IdeLineFix> &v = byIde[is.file];
			bool dup = false; for(size_t q = 0; q < v.size(); q++) if(v[q].line == fx.line && v[q].kind == fx.kind) dup = true;
			if(!dup) v.push_back(fx);
		}
	}
	int changed = 0;
	summary.clear();
	for(auto it = byIde.begin(); it != byIde.end(); ++it) changed += applyIdeFixes(gd, it->first, it->second, summary);
	for(auto it = byEntry.begin(); it != byEntry.end(); ++it){
		int entryIdx = it->first;
		if(entryIdx < 0 || entryIdx >= (int)gd.entries.size()) continue;
		const Entry &e = gd.entries[(size_t)entryIdx];
		std::vector<uint8_t> data;
		std::string err;
		if(!ReadEntryTrimmed(gd, entryIdx, data, err)){ summary += fmt(T("%s: не прочитан (%s)\n"), e.name.c_str(), err.c_str()); continue; }
		std::vector<uint8_t> orig(data);
		int fixed = 0;
		bool ok = false;
		unsigned kinds = it->second;
		std::string what;	// what the TXD path did, for the summary
		if(kinds & (1u << FIX_DFF_FRAMENAME)){ ok = FixDffFrameNames(data, HintForEntry(gd, entryIdx), fixed, err); what = fmt(T("исправлено фреймов %d"), fixed); }
		else{	// TXD: the codec with only the requested repairs, dead textures dropped from the list
			TxdFile f;
			if(!TxdParse(data, f) || !f.ok){ err = f.err.empty() ? T("TXD не разобран") : f.err; ok = false; }
			else{
				ok = true;
				TxdFixOptions o; o.toD3D9 = o.unpalettize = o.stripAutoMip = o.fullMips = o.pow2 = o.fixFilter = o.clearMask = o.dxt = o.fixDxtAlpha = false;
				if(kinds & (1u << FIX_TXD_POW2)) o.pow2 = true;
				if(kinds & (1u << FIX_TXD_DXTALPHA)) o.fixDxtAlpha = true;
				std::vector<std::string> log;
				if(o.pow2 || o.fixDxtAlpha){ int n = TxdApplyFixes(f, o, log); fixed += n; if(n) what += fmt(T("текстур перекодировано %d "), n); }
				if(kinds & (1u << FIX_TXD_DEADTEX)){
					std::vector<std::string> dead;
					if(DeadTextures(gd, e.base, dead)){
						int n = 0;
						for(size_t q = 0; q < f.tex.size(); ){
							bool isDead = false;
							for(size_t d = 0; d < dead.size(); d++) if(lower(f.tex[q].name) == lower(dead[d])){ isDead = true; break; }
							if(isDead){ f.tex.erase(f.tex.begin() + (ptrdiff_t)q); n++; } else q++;
						}
						fixed += n; if(n) what += fmt(T("удалено текстур %d "), n);
					}
				}
				if(fixed && !TxdWrite(f, data, err)) ok = false;
			}
		}
		if(!ok){ summary += fmt("%s: %s\n", e.name.c_str(), err.c_str()); continue; }
		if(fixed == 0){ summary += fmt(T("%s: нечего исправлять\n"), e.name.c_str()); continue; }
		std::string backup;
		if(!WriteEntryFixed(gd, entryIdx, orig, data, backup, err)){ summary += fmt("%s: %s\n", e.name.c_str(), err.c_str()); continue; }
		changed++;
		summary += gOutDir.empty() ? fmt(T("%s: %s (оригинал: %s)\n"), e.name.c_str(), what.c_str(), backup.c_str())
		                           : fmt(T("%s: %s → %s\n"), e.name.c_str(), what.c_str(), backup.c_str());
	}
	return changed;
}

} // namespace gc
