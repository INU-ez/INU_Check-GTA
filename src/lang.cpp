// gtacheck — localisation.  Every message in the checker is written in Russian at the point where
// it is produced (often through fmt() with the values already substituted).  Instead of touching
// ~1500 call sites, translation happens centrally: lang.tsv maps each Russian source pattern
// ("%s: %d полей вместо %d …") to English and Spanish patterns with the same printf specs in the
// same order.  TranslateMessage() matches a formatted Russian message against those patterns,
// captures the substituted values and re-inserts them into the target pattern; captured %s
// fragments are translated recursively (nested fmt()s).  T() is the exact-match variant for UI
// labels and format strings that are translated before formatting.
#include "gtacheck.h"
#include "lang_data.h"

#include <ctype.h>
#include <algorithm>

namespace gc {

Lang gLang = LANG_RU;

// ------------------------------------------------------------ patterns ---

struct Seg { bool spec; char kind; std::string lit; };	// spec: kind d/u/x/f/s/c
struct Pattern {
	const char *ru, *en, *es;
	std::vector<Seg> segs;		// Russian pattern split into literals and specs
	std::vector<Seg> segsTr[2];	// en / es
	int nspec;
	size_t litChars;
};
static std::vector<Pattern> gPatterns;			// with specs, longest literal part first
static std::unordered_map<std::string, int> gExact;	// Russian text (no specs) → LANG_TABLE row
static std::vector<int> gPatByFirst[256];		// patterns indexed by the first byte of their first literal (0 = starts with a spec)
static bool gBuilt;
static std::set<std::string> gMissing;
static std::mutex gMissingMtx;

// printf spec at s[i] ("%d", "%.2f", "%08X", "%-6s", "%.40s", "%llu", "%zu", "%c"); "%%" is a literal '%'
static int parseSpec(const char *s, char *kind)
{
	if(s[0] != '%') return 0;
	int i = 1;
	if(s[i] == '%'){ *kind = '%'; return 2; }
	while(s[i] == '-' || s[i] == '+' || s[i] == ' ' || s[i] == '0' || s[i] == '#') i++;
	while(isdigit((unsigned char)s[i])) i++;
	if(s[i] == '.'){ i++; while(isdigit((unsigned char)s[i])) i++; }
	while(s[i] == 'l' || s[i] == 'z' || s[i] == 'h' || s[i] == 'I') i++;
	char c = s[i];
	if(c == 'd' || c == 'i'){ *kind = 'd'; return i + 1; }
	if(c == 'u'){ *kind = 'u'; return i + 1; }
	if(c == 'x' || c == 'X'){ *kind = 'x'; return i + 1; }
	if(c == 'f' || c == 'g' || c == 'e'){ *kind = 'f'; return i + 1; }
	if(c == 's'){ *kind = 's'; return i + 1; }
	if(c == 'c'){ *kind = 'c'; return i + 1; }
	return 0;
}

static void split(const char *s, std::vector<Seg> &segs, int *nspec, size_t *litChars)
{
	segs.clear(); if(nspec) *nspec = 0; if(litChars) *litChars = 0;
	std::string lit;
	for(size_t i = 0; s[i]; ){
		char kind = 0;
		int n = parseSpec(s + i, &kind);
		if(n == 0){ lit += s[i]; i++; continue; }
		if(kind == '%'){ lit += '%'; i += (size_t)n; continue; }
		if(!lit.empty()){ Seg l; l.spec = false; l.kind = 0; l.lit = lit; segs.push_back(l); if(litChars) *litChars += lit.size(); lit.clear(); }
		Seg sp; sp.spec = true; sp.kind = kind; segs.push_back(sp);
		if(nspec) (*nspec)++;
		i += (size_t)n;
	}
	if(!lit.empty()){ Seg l; l.spec = false; l.kind = 0; l.lit = lit; segs.push_back(l); if(litChars) *litChars += lit.size(); }
}

static void build()
{
	if(gBuilt) return;
	gBuilt = true;
	for(size_t r = 0; r < LANG_TABLE_COUNT; r++){
		Pattern p;
		p.ru = LANG_TABLE[r][0]; p.en = LANG_TABLE[r][1]; p.es = LANG_TABLE[r][2];
		split(p.ru, p.segs, &p.nspec, &p.litChars);
		if(p.nspec == 0){ gExact[p.ru] = (int)r; continue; }
		split(p.en, p.segsTr[0], nullptr, nullptr);
		split(p.es, p.segsTr[1], nullptr, nullptr);
		gPatterns.push_back(p);
	}
	std::stable_sort(gPatterns.begin(), gPatterns.end(), [](const Pattern &a, const Pattern &b){ return a.litChars > b.litChars; });
	for(size_t i = 0; i < gPatterns.size(); i++){
		const Pattern &p = gPatterns[i];
		unsigned char first = (!p.segs.empty() && !p.segs[0].spec) ? (unsigned char)p.segs[0].lit[0] : 0;
		gPatByFirst[first].push_back((int)i);
	}
}

// value matchers ------------------------------------------------------------
static size_t matchNumber(const char *s, char kind)
{
	size_t i = 0;
	if(kind == 'd' || kind == 'f'){ if(s[i] == '-' || s[i] == '+') i++; }
	if(kind == 'f'){
		// nan / inf / -nan(ind)
		if(strncmp(s + i, "nan", 3) == 0 || strncmp(s + i, "inf", 3) == 0){ i += 3; if(strncmp(s + i, "(ind)", 5) == 0) i += 5; return i; }
	}
	size_t d = i;
	if(kind == 'x'){ while(isxdigit((unsigned char)s[i])) i++; }
	else{ while(isdigit((unsigned char)s[i])) i++; }
	if(i == d) return 0;
	if(kind == 'f'){
		if(s[i] == '.'){ i++; while(isdigit((unsigned char)s[i])) i++; }
		if((s[i] == 'e' || s[i] == 'E') && (isdigit((unsigned char)s[i + 1]) || ((s[i + 1] == '-' || s[i + 1] == '+') && isdigit((unsigned char)s[i + 2])))){ i += 2; while(isdigit((unsigned char)s[i])) i++; }
	}
	return i;
}

static size_t utf8Len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }

// Matches segs[si..] against s, filling caps; %s tries every position where the following literal reappears.
static bool matchSegs(const std::vector<Seg> &segs, size_t si, const char *s, std::vector<std::string> &caps)
{
	if(si == segs.size()) return *s == 0;
	const Seg &g = segs[si];
	if(!g.spec){
		if(strncmp(s, g.lit.c_str(), g.lit.size()) != 0) return false;
		return matchSegs(segs, si + 1, s + g.lit.size(), caps);
	}
	if(g.kind == 's'){
		if(si + 1 == segs.size()){ caps.push_back(s); return true; }
		const Seg &next = segs[si + 1];
		if(!next.spec){
			const char *p = s;
			while((p = strstr(p, next.lit.c_str())) != nullptr){
				caps.push_back(std::string(s, (size_t)(p - s)));
				if(matchSegs(segs, si + 1, p, caps)) return true;
				caps.pop_back();
				p += utf8Len((unsigned char)*p);
			}
			return false;
		}
		// two specs in a row (%s%d …): let the string be empty first, then grow
		for(const char *p = s; ; p += utf8Len((unsigned char)*p)){
			caps.push_back(std::string(s, (size_t)(p - s)));
			if(matchSegs(segs, si + 1, p, caps)) return true;
			caps.pop_back();
			if(*p == 0) return false;
		}
	}
	if(g.kind == 'c'){
		if(*s == 0) return false;
		size_t n = utf8Len((unsigned char)*s);
		caps.push_back(std::string(s, n));
		if(matchSegs(segs, si + 1, s + n, caps)) return true;
		caps.pop_back();
		return false;
	}
	size_t n = matchNumber(s, g.kind);
	if(n == 0) return false;
	caps.push_back(std::string(s, n));
	if(matchSegs(segs, si + 1, s + n, caps)) return true;
	caps.pop_back();
	return false;
}

static bool hasCyrillic(const std::string &s)
{
	for(size_t i = 0; i + 1 < s.size(); i++){ unsigned char c = (unsigned char)s[i]; if(c == 0xD0 || c == 0xD1) return true; }
	return false;
}

static std::string translateInner(const std::string &ru, int depth);

static std::string render(const std::vector<Seg> &segs, const std::vector<std::string> &caps, int depth)
{
	std::string out;
	size_t ci = 0;
	for(size_t i = 0; i < segs.size(); i++){
		if(!segs[i].spec){ out += segs[i].lit; continue; }
		std::string v = ci < caps.size() ? caps[ci] : "";
		ci++;
		if(segs[i].kind == 's' && depth < 4 && hasCyrillic(v)) v = translateInner(v, depth + 1);
		out += v;
	}
	return out;
}

static std::string translateInner(const std::string &ru, int depth)
{
	if(gLang == LANG_RU || ru.empty()) return ru;
	build();
	int col = gLang == LANG_EN ? 1 : 2;
	auto ex = gExact.find(ru);
	if(ex != gExact.end()){
		const char *t = LANG_TABLE[(size_t)ex->second][col];
		return *t ? std::string(t) : ru;
	}
	unsigned char first = (unsigned char)ru[0];
	std::vector<std::string> caps;
	for(int pass = 0; pass < 2; pass++){
		const std::vector<int> &cand = gPatByFirst[pass == 0 ? first : 0];
		for(size_t k = 0; k < cand.size(); k++){
			const Pattern &p = gPatterns[(size_t)cand[k]];
			caps.clear();
			if(!matchSegs(p.segs, 0, ru.c_str(), caps)) continue;
			const char *t = col == 1 ? p.en : p.es;
			if(!*t) return ru;
			return render(p.segsTr[col - 1], caps, depth);
		}
	}
	// nested fragment assembled from several messages ("a, b" / "a; b" / "a / b"): translate the pieces
	if(depth > 0){
		static const char *SEPS[] = { ", ", "; ", " / ", nullptr };
		for(int k = 0; SEPS[k]; k++){
			size_t at = ru.find(SEPS[k]);
			if(at == std::string::npos) continue;
			std::string left = ru.substr(0, at);
			std::string tl = translateInner(left, depth + 1);
			if(hasCyrillic(tl) && hasCyrillic(left)) continue;
			// the right part either stands alone or carries the separator itself (", нет …" patterns)
			std::string right = ru.substr(at + strlen(SEPS[k]));
			std::string tr = translateInner(right, depth + 1);
			if(!hasCyrillic(tr) || !hasCyrillic(right)) return tl + SEPS[k] + tr;
			right = ru.substr(at);
			tr = translateInner(right, depth + 1);
			if(!hasCyrillic(tr)) return tl + tr;
		}
	}
	if(depth == 0 && hasCyrillic(ru)){ std::lock_guard<std::mutex> lock(gMissingMtx); gMissing.insert(ru); }
	return ru;
}

// ---------------------------------------------------------------- API ---

std::string TranslateMessage(const std::string &ru)
{
	return translateInner(ru, 0);
}

const char *T(const char *ru)
{
	if(gLang == LANG_RU || !ru || !*ru) return ru;
	build();
	auto ex = gExact.find(ru);
	if(ex == gExact.end()){
		// a format string with specs is stored as a pattern — look it up by its Russian text
		for(size_t i = 0; i < gPatterns.size(); i++) if(strcmp(gPatterns[i].ru, ru) == 0){
			const char *t = gLang == LANG_EN ? gPatterns[i].en : gPatterns[i].es;
			return *t ? t : ru;
		}
		if(hasCyrillic(ru)){ std::lock_guard<std::mutex> lock(gMissingMtx); gMissing.insert(ru); }
		return ru;
	}
	const char *t = LANG_TABLE[(size_t)ex->second][gLang == LANG_EN ? 1 : 2];
	return *t ? t : ru;
}

void SetLanguage(Lang l) { gLang = l; }

const char *LangCode(Lang l) { return l == LANG_EN ? "en" : l == LANG_ES ? "es" : "ru"; }

Lang LangFromCode(const std::string &c)
{
	std::string l = lower(trim(c));
	if(l == "en" || l == "eng" || l == "english") return LANG_EN;
	if(l == "es" || l == "spa" || l == "español" || l == "espanol" || l == "spanish") return LANG_ES;
	return LANG_RU;
}

void LangMissing(std::vector<std::string> &out)
{
	std::lock_guard<std::mutex> lock(gMissingMtx);
	out.assign(gMissing.begin(), gMissing.end());
}

// Re-translates every issue of a report from its Russian originals (after a language switch).
void RetranslateReport(Report &rep)
{
	std::lock_guard<std::mutex> lock(rep.mtx);
	for(size_t i = 0; i < rep.issues.size(); i++){
		Issue &is = rep.issues[i];
		is.msg = TranslateMessage(is.msgRu);
		is.detail = TranslateMessage(is.detailRu);
		is.object = TranslateMessage(is.objectRu);
	}
	rep.version++;
}

} // namespace gc
