// gtacheck — CJ clothes: player.img / clothes.dat / shopping.dat prices (CLO-nn).
// Source: E:\RE\addon_check\clothes_cutscene_rules.md §1–6, §8 + GTACHECK_RULES.md §6 (gta_sa.exe 1.0 US).
#include "gtacheck.h"

#include <stdlib.h>
#include <algorithm>

namespace gc {

static const char *CLOTHES_COMPONENTS[10] = { "torso", "head", "hands", "legs", "feet", "necklace", "watch", "glasses", "hat", "extra1" };

// ------------------------------------------------- minimal clump parser ---

struct CGeo { int numVerts; bool normals, uv, skin; int maxBone; CGeo() : numVerts(0), normals(false), uv(false), skin(false), maxBone(-1) {} };
struct CClump {
	std::vector<std::string> frameNames;
	std::vector<int> frameParent;
	std::vector<int> frameNodes;		// HAnim numNodes on the frame, -1 = no HAnim
	std::vector<CGeo> geoms;
	std::vector<std::pair<int,int>> atomics;	// (frame, geom)
	bool ok;
	std::string err;
	CClump() : ok(false) {}
};

static bool parseClump(const uint8_t *p, size_t n, CClump &c)
{
	Buf b(p, n);
	Chunk clump;
	if(!readChunk(b, clump) || clump.type != 0x10){ c.err = "не CLUMP"; return false; }
	size_t end = clump.end;
	Chunk st;
	if(!findChunk(b, 0x1, st, end)){ c.err = "нет STRUCT клампа"; return false; }
	b.seek(st.end);
	Chunk fl;
	if(!findChunk(b, 0xE, fl, end)){ c.err = "нет FRAMELIST"; return false; }
	{
		Chunk fs;
		if(!findChunk(b, 0x1, fs, fl.end)){ c.err = "FRAMELIST без STRUCT"; return false; }
		Buf s(p + fs.start, fs.end - fs.start);
		uint32_t nf = s.u32();
		if(!s.ok || nf > 10000){ c.err = "число фреймов вне разумного"; return false; }
		for(uint32_t i = 0; i < nf; i++){
			s.skip(48);
			int parent = s.i32();
			s.u32();
			if(!s.ok){ c.err = "FRAMELIST STRUCT обрезан"; return false; }
			c.frameParent.push_back(parent);
			c.frameNames.push_back("");
			c.frameNodes.push_back(-1);
		}
		b.seek(fs.end);
		for(uint32_t i = 0; i < nf; i++){
			Chunk ext;
			if(!findChunk(b, 0x3, ext, fl.end)) break;
			size_t pos = ext.start;
			while(pos + 12 <= ext.end){
				Buf e(p, n); e.seek(pos);
				Chunk ch;
				if(!readChunk(e, ch)) break;
				if(ch.type == 0x253F2FE){
					std::string nm((const char *)p + ch.start, ch.end - ch.start);
					size_t z = nm.find('\0'); if(z != std::string::npos) nm.resize(z);
					c.frameNames[i] = nm;
				}else if(ch.type == 0x11E){
					Buf h(p + ch.start, ch.end - ch.start);
					h.u32(); h.u32();
					uint32_t nodes = h.u32();
					if(h.ok) c.frameNodes[i] = (int)nodes;
				}
				if(ch.end <= pos) break;
				pos = ch.end;
			}
			b.seek(ext.end);
		}
		b.seek(fl.end);
	}
	Chunk gl;
	if(!findChunk(b, 0x1A, gl, end)){ c.err = "нет GEOMETRYLIST"; return false; }
	{
		Chunk gs;
		if(!findChunk(b, 0x1, gs, gl.end)){ c.err = "GEOMETRYLIST без STRUCT"; return false; }
		Buf s(p + gs.start, gs.end - gs.start);
		uint32_t ng = s.u32();
		if(!s.ok || ng > 10000){ c.err = "число геометрий вне разумного"; return false; }
		b.seek(gs.end);
		for(uint32_t i = 0; i < ng; i++){
			Chunk g;
			if(!findChunk(b, 0xF, g, gl.end)){ c.err = fmt("GEOMETRY №%u не найдена", i); return false; }
			CGeo geo;
			Chunk hs;
			if(findChunk(b, 0x1, hs, g.end)){
				Buf h(p + hs.start, hs.end - hs.start);
				uint32_t flags = h.u32(); h.u32(); uint32_t nv = h.u32();
				if(h.ok){
					geo.numVerts = (int)nv;
					geo.normals = (flags & 0x10) != 0;
					int sets = (int)((flags >> 16) & 0xFF); if(sets == 0) sets = (flags & 0x80) ? 2 : ((flags & 4) ? 1 : 0);
					geo.uv = sets > 0;
				}
				b.seek(hs.end);
			}
			// skin plugin inside the geometry's EXTENSION (after MATERIALLIST)
			Chunk ml;
			if(findChunk(b, 0x8, ml, g.end)) b.seek(ml.end);
			Chunk ext;
			if(findChunk(b, 0x3, ext, g.end)){
				size_t pos = ext.start;
				while(pos + 12 <= ext.end){
					Buf e(p, n); e.seek(pos);
					Chunk ch;
					if(!readChunk(e, ch)) break;
					if(ch.type == 0x116){
						Buf sk(p + ch.start, ch.end - ch.start);
						int numBones = sk.u8(), numUsed = sk.u8(); sk.u8(); sk.u8();
						sk.skip((size_t)numUsed);
						geo.skin = sk.ok;
						(void)numBones;
						for(int v = 0; v < geo.numVerts && sk.ok; v++){
							for(int k = 0; k < 4; k++){ int bi = sk.u8(); if(sk.ok && bi > geo.maxBone) geo.maxBone = bi; }
						}
					}
					if(ch.end <= pos) break;
					pos = ch.end;
				}
			}
			c.geoms.push_back(geo);
			b.seek(g.end);
		}
		b.seek(gl.end);
	}
	// atomics
	while(true){
		Chunk a;
		if(!findChunk(b, 0x14, a, end)) break;
		Chunk as;
		if(findChunk(b, 0x1, as, a.end)){
			Buf s(p + as.start, as.end - as.start);
			int fr = s.i32(), ge = s.i32();
			if(s.ok) c.atomics.push_back(std::make_pair(fr, ge));
		}
		b.seek(a.end);
	}
	c.ok = true;
	return true;
}

// HAnim node count on the frame or below it (GetAnimHierarchyFromFrame 0x734AB0: own plugin, else first child carrying one)
static int hanimBelow(const CClump &c, int frame, int depth = 0)
{
	if(frame < 0 || frame >= (int)c.frameNodes.size() || depth > 64) return -1;
	if(c.frameNodes[(size_t)frame] >= 0) return c.frameNodes[(size_t)frame];
	for(size_t k = 0; k < c.frameParent.size(); k++)
		if(c.frameParent[k] == frame){ int r = hanimBelow(c, (int)k, depth + 1); if(r >= 0) return r; }
	return -1;
}

// ---------------------------------------------------------------- TXD peek ---

struct TexPeek { std::string name; int width, height, depth; bool compressed, palette; TexPeek() : width(0), height(0), depth(0), compressed(false), palette(false) {} };

static bool peekTxd(const std::vector<uint8_t> &data, std::vector<TexPeek> &out)
{
	Buf b(data.data(), data.size());
	Chunk dict, st;
	if(!findChunk(b, 0x16, dict, data.size())) return false;
	if(!findChunk(b, 0x1, st, dict.end)) return false;
	Buf s(data.data() + st.start, st.end - st.start);
	int numTex = s.u16();
	if(!s.ok) return false;
	b.seek(st.end);
	for(int i = 0; i < numTex; i++){
		Chunk tex;
		if(!findChunk(b, 0x15, tex, dict.end)) break;
		Chunk ts;
		if(!findChunk(b, 0x1, ts, tex.end)){ b.seek(tex.end); continue; }
		Buf t(data.data() + ts.start, ts.end - ts.start);
		TexPeek tp;
		uint32_t platform = t.u32(); t.u32();
		tp.name = t.str(32); t.str(32);
		uint32_t rasterFormat = t.u32();
		uint32_t d3dFormat = 0;
		if(platform == 9) d3dFormat = t.u32(); else t.u32();
		tp.width = t.u16(); tp.height = t.u16();
		tp.depth = t.u8(); t.u8(); t.u8(); int flags = t.u8();
		if(t.ok){
			tp.palette = (rasterFormat & 0x6000) != 0;
			tp.compressed = platform == 9 ? ((flags & 8) != 0 || (d3dFormat & 0xFFFFFF) == 0x545844 /* 'DXT' */) : flags != 0;
			out.push_back(tp);
		}
		b.seek(tex.end);
	}
	return true;
}

// ------------------------------------------------------------------- rules ---

void CheckClothes(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	ctx.prog->set("Одежда CJ: player.img / clothes.dat");
	// player.img directory
	int imgIdx = -1;
	for(size_t i = 0; i < gd.archives.size(); i++) if(ieq(basename(gd.archives[i].logical), "player.img")) imgIdx = (int)i;
	if(imgIdx < 0){
		ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-01", "models/player.img", -1, "", "models/player.img не найден — CDirectory::ReadDirFile 0x532350 не проверяет fopen: каталог пуст, любая часть одежды → CLO-03 (чтение по мусорному offset/size) → краш/зависание при первом ребилде CJ", "");
		return;
	}
	const Archive &ar = gd.archives[(size_t)imgIdx];
	ctx.rep->countFile(CAT_CLOTHES);
	if(!ar.ver2) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-01", ar.logical, -1, "", "player.img не VER2 — ReadDirFile читает 4 байта magic без проверки и число записей из следующих 4 байт → каталог из мусора", "");
	if(ar.numEntries > 550) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-01", ar.logical, -1, "", fmt("%d записей > 550 — CDirectory::Init(0x226): лишние записи выброшены («Too many objects without modelinfo structures»), обращение к ним → CLO-03/04 → краш", ar.numEntries), "Ваниль: 542 записи, свободно 8 слотов.");
	// largest streamed entry (streaming buffer size), rounded up to an even number of sectors
	uint32_t maxStream = 0;
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(e.img == imgIdx) continue;
		if(e.sizeSectors > maxStream) maxStream = e.sizeSectors;
	}
	if(maxStream & 1) maxStream++;
	std::map<std::string, int> dff, txd;	// lower stem → entry index
	std::vector<int> playerEntries;
	for(size_t i = 0; i < gd.entries.size(); i++){
		const Entry &e = gd.entries[i];
		if(e.img != imgIdx) continue;
		playerEntries.push_back((int)i);
		std::string where = "player.img/" + e.name;
		if(e.name.size() >= 24) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-02", where, e.dirIndex, e.name, "имя записи занимает все 24 байта без NUL — GetUppercaseKey 0x53CF30 читает до NUL в следующую запись: ключ никогда не совпадёт → CLO-03", "");
		if(e.sizeSectors == 0) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-02", where, e.dirIndex, e.name, "streaming size (байты 4-5) = 0 — FindItem вернёт 0 секторов: чтение клампа проваливается → BlendGeometry(NULL)", "");
		if(maxStream && e.sizeSectors > maxStream) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-02", where, e.dirIndex, e.name, fmt("%u секторов > %u (наибольшая запись стрим-IMG, чётно) — player.img не поднимает ms_streamingBufferSize (AddImageToList(…,0)), ConvertBufferToObject читает size<<11 из общего буфера → переполнение", e.sizeSectors, maxStream), "");
		if(e.kind == EK_DFF) dff[e.base] = (int)i;
		else if(e.kind == EK_TXD) txd[e.base] = (int)i;
	}
	// ----- names used as parts / textures
	struct Use { std::string name; std::string from; int line; bool model; };
	std::vector<Use> uses;
	for(const char *d : { "torso", "head", "hands", "legs", "feet" }) uses.push_back({ d, "defaults CreateSkinnedClump 0x5A69D0", -1, true });
	for(const char *t : { "player_torso", "player_legs", "player_face", "player_feet" }) uses.push_back({ t, "базовые TXD (0x8D0A4C)", -1, false });
	std::set<std::string> torsoTxd, legsTxd, tattooTxd;
	// clothes.dat
	{
		const std::string logical = "data/clothes.dat";
		std::vector<DataLine> lines;
		if(!ReadDataLines(gd, logical, lines))
			ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-12", logical, -1, "", "data/clothes.dat не найден — LoadClothesFile 0x5A7B30 не проверяет OpenFile → LoadLine(NULL) → краш при старте", "");
		else{
			ctx.rep->countFile(CAT_CLOTHES);
			int state = 0, words = 0, prevType = -1;
			std::vector<std::string> tags;	// active IGNORE/EXCLUSIVE tags (simulated local_20[8])
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				const std::vector<std::string> &t = l.tok;
				if(state == 0){
					if(l.norm.compare(0, 4, "rule") == 0) state = 1;
					else ctx.add(SEV_WARN, CAT_CLOTHES, "CLO-12", logical, l.number, t[0], fmt("строка «%.40s» вне блока rule … end — игнорируется", l.norm.c_str()), "");
					continue;
				}
				if(l.norm.compare(0, 3, "end") == 0){
					state = 0;
					if(l.norm.size() > 3 && l.norm[3] != ' ')
						ctx.add(SEV_WARN, CAT_CLOTHES, "CLO-12", logical, l.number, t[0], fmt("«%s» начинается со строчного «end» — strncmp(\"end\",3) закрывает блок правил: все правила до следующего «rule» игнорируются (ваниль пишет ENDIGNORE/ENDEXCLUSIVE прописными)", t[0].c_str()), "");
					continue;
				}
				std::string kw = lower(t[0]);
				int type = -1, need = 0, nw = 0;
				if(kw == "cuts"){ type = 0; need = 2; nw = 3; }
				else if(kw == "setc"){ type = 1; need = 4; nw = 5; }
				else if(kw == "tex"){ type = 2; need = 2; nw = 3; }
				else if(kw == "hide"){ type = 3; need = 2; nw = 3; }
				else if(kw == "ignore"){ type = 5; need = 1; nw = 2; }
				else if(kw == "endignore"){ type = 4; need = 1; nw = 2; }
				else if(kw == "exclusive"){ type = 7; need = 1; nw = 2; }
				else if(kw == "endexclusive"){ type = 6; need = 1; nw = 2; }
				if(type < 0){
					ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-13", logical, l.number, t[0], fmt("неизвестное ключевое слово «%s» — тип правила наследуется от предыдущей строки (%s), аргументы читаются по его схеме", t[0].c_str(), prevType < 0 ? "на первой строке — мусор" : "предыдущее правило"), "");
					type = prevType < 0 ? 1 : prevType;
					need = type == 1 ? 4 : (type == 0 || type == 2 || type == 3) ? 2 : 1;
					nw = type == 1 ? 5 : (type == 0 || type == 2 || type == 3) ? 3 : 2;
				}
				prevType = type;
				int args = (int)t.size() - 1;
				if(args < need){
					ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-13", logical, l.number, t[0], fmt("%s: %d аргументов вместо %d — strtok вернёт NULL → GetUppercaseKey(NULL) 0x53CF36 mov al,[edi] → краш при первом ребилде CJ", t[0].c_str(), args, need), "");
					words += nw;
					continue;
				}
				words += nw;
				if(type == 1 || type == 3){
					bool okc = false; for(int k = 0; k < 10; k++) if(t[2] == CLOTHES_COMPONENTS[k]) okc = true;
					if(!okc) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-14", logical, l.number, t[0], fmt("компонент «%s» не из torso head hands legs feet necklace watch glasses hat extra1 (строчные, точно) — GetClothesModelFromName → 0 = torso", t[2].c_str()), "");
				}
				if(type == 1){
					if(t[3] != "-") uses.push_back({ t[3], fmt("clothes.dat SETC (строка %d)", l.number), l.number, true });
					if(t[4] != "-"){
						uses.push_back({ t[4], fmt("clothes.dat SETC (строка %d)", l.number), l.number, false });
						if(t[2] == "torso") torsoTxd.insert(lower(t[4]));
						else if(t[2] == "legs") legsTxd.insert(lower(t[4]));
					}
				}else if(type == 0){
					uses.push_back({ t[2], fmt("clothes.dat CUTS (строка %d)", l.number), l.number, true });
				}else if(type == 2){
					uses.push_back({ t[2], fmt("clothes.dat TEX (строка %d)", l.number), l.number, false });
					torsoTxd.insert(lower(t[2]));
				}else if(type == 5 || type == 7){
					if(tags.size() >= 8) ctx.add(SEV_WARN, CAT_CLOTHES, "CLO-16", logical, l.number, t[1], fmt("9-й одновременно активный тег %s «%s» — PreprocessClothesDesc local_20[8]: тег молча не регистрируется", type == 5 ? "IGNORE" : "EXCLUSIVE", t[1].c_str()), "");
					tags.push_back(lower(t[1]));
				}else if(type == 4 || type == 6){
					auto it = std::find(tags.begin(), tags.end(), lower(t[1]));
					if(it != tags.end()) tags.erase(it);
				}
			}
			if(words > 600) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-15", logical, -1, "", fmt("%d слов правил > 600 — ms_clothesRules 0xBC1300 переполнен (с 607 затирается ms_lastClothesDesc, с 636 — g_bCutSceneFinishing и состояние катсцен)", words), "Ваниль — 587 слов (SETC 5, CUTS/TEX/HIDE 3, прочие 2).");
			else if(words > 580) ctx.add(SEV_INFO, CAT_CLOTHES, "CLO-15", logical, -1, "", fmt("%d слов правил из 600", words), "");
		}
	}
	// shopping.dat prices (Clothes/Haircuts/Tattoos) — CLO-17 + names
	{
		const std::string logical = "data/shopping.dat";
		std::vector<DataLine> lines;
		if(ReadDataLines(gd, logical, lines)){
			std::vector<std::string> stack; bool inPrices = false; std::string sub;
			for(size_t i = 0; i < lines.size(); i++){
				const DataLine &l = lines[i];
				if(l.norm.empty() || l.norm[0] == '#') continue;
				std::vector<std::string> t; for(size_t k = 0; k < l.tok.size(); k++){ if(l.tok[k][0] == '#') break; t.push_back(l.tok[k]); }
				if(t.empty()) continue;
				if(l.norm.compare(0, 7, "section") == 0){ std::string nm = t.size() > 1 ? t[1] : ""; stack.push_back(nm); if(stack.size() == 1) inPrices = ieq(nm, "prices"); else if(stack.size() == 2) sub = lower(nm); continue; }
				if(l.norm.compare(0, 3, "end") == 0){ if(!stack.empty()) stack.pop_back(); if(stack.empty()) inPrices = false; if(stack.size() < 2) sub.clear(); continue; }
				if(!inPrices || stack.size() != 2) continue;
				bool clothes = sub == "clothes", hair = sub == "haircuts", tattoo = sub == "tattoos";
				if(!clothes && !hair && !tattoo) continue;
				if(t.size() < 4) continue;	// PDD-77
				std::string from = fmt("shopping.dat %s (строка %d)", sub.c_str(), l.number);
				uses.push_back({ t[0], from, l.number, false });
				if(t[0].size() > 19) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-17", logical, l.number, t[0], fmt("texturename «%s» длиннее 19 — «%s.TXD» не влезает в 24-байтовое имя записи player.img", t[0].c_str(), t[0].c_str()), "");
				if(clothes || hair){
					uses.push_back({ t[2], from, l.number, true });
					if(t[2].size() > 19) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-17", logical, l.number, t[0], fmt("modelname «%s» длиннее 19", t[2].c_str()), "");
					if(IsIntToken(t[3])){
						int ty = (int)strtol(t[3].c_str(), nullptr, 10);
						bool ok = ty == 0 || ty == 1 || ty == 2 || ty == 3 || (ty >= 13 && ty <= 17);
						if(!ok) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-17", logical, l.number, t[0], fmt("type %d не из {0,1,2,3,13..17} — SetTextureAndModel 0x5A8050 пишет textures[type] за пределами CPedClothesDesc (18 слотов)", ty), "");
						if(ty == 0) torsoTxd.insert(lower(t[0]));
						else if(ty == 2) legsTxd.insert(lower(t[0]));
					}
				}else{
					tattooTxd.insert(lower(t[0]));
					if(t[2] != "-" && IsIntToken(t[2])){
						int ty = (int)strtol(t[2].c_str(), nullptr, 10);
						if(ty < 4 || ty > 12) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-17", logical, l.number, t[0], fmt("Tattoos type1 %d не в 4..12 — LoadPrices case 6 хэширует 4-й токен «%s» в models[GetTextureDependency(%d)]: ищется DFF с таким именем → CLO-03 → краш", ty, t[3].c_str(), ty), "");
					}else if(t[2] == "-")
						ctx.add(SEV_WARN, CAT_CLOTHES, "CLO-17", logical, l.number, t[0], "Tattoos type1 «-» = −1 — слот текстуры −1, татуировка не применяется", "");
				}
			}
		}
	}
	// CLO-03 / CLO-04
	{
		std::set<std::string> reported;
		for(size_t i = 0; i < uses.size(); i++){
			const Use &u = uses[i];
			std::string key = lower(u.name) + (u.model ? ".dff" : ".txd");
			if(reported.count(key)) continue;
			bool have = u.model ? dff.count(lower(u.name)) != 0 : txd.count(lower(u.name)) != 0;
			if(have) continue;
			reported.insert(key);
			if(u.model)
				ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-03", "models/player.img", -1, u.name, fmt("нет «%s.dff» в player.img (используется: %s) — FindItem промахивается, но ConstructGeometryArray 0x5A55A0 не проверяет результат: RequestFile с неинициализированным offset/size → чтение за EOF (зависание стриминга) или BlendGeometry(NULL) → краш при ребилде CJ", u.name.c_str(), u.from.c_str()), "");
			else
				ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-04", "models/player.img", -1, u.name, fmt("нет «%s.txd» в player.img (используется: %s) — RequestTexture 0x5A4220 с мусорным offset/size → словарь NULL → RwTexDictionaryFindNamedTexture/GetFirstTexture(NULL) → CopyTexture mov ebx,[eax] → краш", u.name.c_str(), u.from.c_str()), "");
		}
	}
	// CLO-05..08: every DFF of player.img
	int done = 0;
	for(auto it = dff.begin(); it != dff.end(); ++it, ++done){
		if(ctx.cancelled()) return;
		const Entry &e = gd.entries[(size_t)it->second];
		std::string where = "player.img/" + e.name;
		ctx.prog->step(e.name.c_str(), done, (int)(dff.size() + txd.size()));
		std::vector<uint8_t> data;
		if(!gd.readEntry(it->second, data)) continue;
		// split into top-level CLUMP chunks
		std::vector<CClump> clumps;
		Buf b(data.data(), data.size());
		int badClump = 0;
		while(b.pos + 12 <= b.n){
			size_t at = b.pos;
			Chunk c;
			if(!readChunk(b, c)) break;
			if(c.type == 0x10){
				CClump cl;
				size_t len = c.end > at ? c.end - at : 0;
				if(!parseClump(data.data() + at, len, cl)){ badClump++; ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-05", where, e.dirIndex, e.name, fmt("CLUMP №%d не разбирается (%s) — RpClumpStreamRead вернёт 0 → LoadClumpFile (флаг 2) прекращает чтение, часть не загружена → BlendGeometry(NULL) → краш", (int)clumps.size() + 1, cl.err.c_str()), ""); }
				clumps.push_back(cl);
			}
			if(c.truncated || c.end <= at) break;
			b.seek(c.end);
		}
		if(clumps.empty()){ ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-05", where, e.dirIndex, e.name, "в записи нет ни одного CLUMP — часть не загружается → BlendGeometry(NULL) → краш", ""); continue; }
		if(clumps.size() != 3) ctx.add(SEV_INFO, CAT_CLOTHES, "CLO-05", where, e.dirIndex, e.name, fmt("%d CLUMP в записи (ваниль всегда 3: normal / fat / ripped по одному атомику)", (int)clumps.size()), "");
		// union of atomics by frame name
		struct Part { int clump, geom; bool found; Part() : clump(-1), geom(-1), found(false) {} };
		Part parts[3]; const char *PN[3] = { "normal", "fat", "ripped" };
		int totalAtomics = 0;
		for(size_t ci = 0; ci < clumps.size(); ci++){
			const CClump &cl = clumps[ci];
			if(!cl.ok) continue;
			for(size_t ai = 0; ai < cl.atomics.size(); ai++){
				totalAtomics++;
				int fr = cl.atomics[ai].first;
				std::string fn = fr >= 0 && fr < (int)cl.frameNames.size() ? lower(cl.frameNames[(size_t)fr]) : "";
				for(int k = 0; k < 3; k++) if(!parts[k].found && fn == PN[k]){ parts[k].found = true; parts[k].clump = (int)ci; parts[k].geom = cl.atomics[ai].second; }
			}
		}
		if(totalAtomics > 3) ctx.add(SEV_INFO, CAT_CLOTHES, "CLO-05", where, e.dirIndex, e.name, fmt("%d атомиков — лишние сливаются в кламп, но не используются", totalAtomics), "");
		bool allFound = true;
		for(int k = 0; k < 3; k++) if(!parts[k].found){ allFound = false; ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-05", where, e.dirIndex, e.name, fmt("нет атомика на фрейме «%s» (stricmp по всем клампам записи) — GetAtomicWithName 0x5A4810 → 0, BlendGeometry читает [0+0x18] → краш при ребилде CJ", PN[k]), "Запись одежды = три склеенных DFF-клампа, атомики на фреймах Normal / Fat / Ripped."); }
		if(!allFound) continue;
		const CGeo *g[3];
		bool geomOk = true;
		for(int k = 0; k < 3; k++){
			const CClump &cl = clumps[(size_t)parts[k].clump];
			if(parts[k].geom < 0 || parts[k].geom >= (int)cl.geoms.size()){ geomOk = false; ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-06", where, e.dirIndex, e.name, fmt("атомик «%s» ссылается на несуществующую геометрию %d", PN[k], parts[k].geom), ""); continue; }
			g[k] = &cl.geoms[(size_t)parts[k].geom];
		}
		if(!geomOk) continue;
		for(int k = 0; k < 3; k++){
			if(!g[k]->normals) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-06", where, e.dirIndex, e.name, fmt("геометрия «%s» без нормалей (флаг 0x10) — BlendGeometry читает массив нормалей всех трёх без проверки → NULL → краш", PN[k]), "");
			if(!g[k]->uv) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-06", where, e.dirIndex, e.name, fmt("геометрия «%s» без UV0 — BlendGeometry читает texcoords[0] → NULL → краш", PN[k]), "");
			if(!g[k]->skin) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-06", where, e.dirIndex, e.name, fmt("геометрия «%s» без Skin PLG — RpSkinGetVertexBoneIndices(NULL) → краш", PN[k]), "");
		}
		if(g[1]->numVerts != g[0]->numVerts || g[2]->numVerts != g[0]->numVerts)
			ctx.add(g[1]->numVerts < g[0]->numVerts || g[2]->numVerts < g[0]->numVerts ? SEV_FATAL : SEV_ERROR, CAT_CLOTHES, "CLO-06", where, e.dirIndex, e.name, fmt("число вершин normal/fat/ripped = %d/%d/%d — цикл BlendGeometry идёт по normal и читает fat/ripped в ногу: меньшая геометрия → чтение за кучей", g[0]->numVerts, g[1]->numVerts, g[2]->numVerts), "");
		if(g[0]->numVerts > 6000) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-08", where, e.dirIndex, e.name, fmt("%d вершин в normal — сумма по 10 надетым частям может превысить 65535 (u16-индексы треугольников и short-смещение в ConstructGeometryAndSkinArrays 0x5A6530); ваниль макс 734", g[0]->numVerts), "");
		// CLO-07 HAnim
		for(int k = 0; k < 3; k++){
			const CClump &cl = clumps[(size_t)parts[k].clump];
			int fr = -1;
			for(size_t ai = 0; ai < cl.atomics.size(); ai++) if(cl.atomics[ai].second == parts[k].geom && fr < 0) fr = cl.atomics[ai].first;
			int nodes = hanimBelow(cl, fr);
			if(nodes < 0) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-07", where, e.dirIndex, e.name, fmt("у атомика «%s» нет HAnim PLG на его фрейме или ниже — GetAnimHierarchyFromFrame 0x734AB0 → NULL, StoreBoneArray 0x5A48B0 читает hierarchy+4 → краш", PN[k]), "");
			else{
				if(nodes > 64) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-07", where, e.dirIndex, e.name, fmt("HAnim «%s»: %d нод > 64 — gBoneIndices[i][64] переполняется (для extra1 — поверх каталога player.img)", PN[k], nodes), "");
				if(g[k]->maxBone >= nodes) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-07", where, e.dirIndex, e.name, fmt("«%s»: индекс кости %d ≥ числа нод HAnim %d — BuildBoneIndexConversionTable читает мусор стека, вершина следует за случайной костью", PN[k], g[k]->maxBone, nodes), "");
			}
		}
		// first atomic of the first clump must be skinned (SetClump flag 2 sets hierarchies only then)
		if(clumps[0].ok && !clumps[0].atomics.empty()){
			int ge = clumps[0].atomics[0].second;
			if(ge >= 0 && ge < (int)clumps[0].geoms.size() && !clumps[0].geoms[(size_t)ge].skin)
				ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-07", where, e.dirIndex, e.name, "первый атомик первого клампа не скинен — SetClump 0x4C4F70 (флаг 2) не назначает иерархии ни одному атомику → StoreBoneArray(NULL) → краш", "");
		}
	}
	// CLO-09..11: every TXD of player.img
	std::map<std::string, std::vector<TexPeek>> peeked;
	for(auto it = txd.begin(); it != txd.end(); ++it, ++done){
		if(ctx.cancelled()) return;
		const Entry &e = gd.entries[(size_t)it->second];
		std::string where = "player.img/" + e.name;
		ctx.prog->step(e.name.c_str(), done, (int)(dff.size() + txd.size()));
		std::vector<uint8_t> data;
		if(!gd.readEntry(it->second, data)) continue;
		std::vector<TexPeek> texs;
		if(!peekTxd(data, texs)){ ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-11", where, e.dirIndex, e.name, "TXD не разбирается — словарь NULL → CopyTexture → краш", ""); continue; }
		peeked[it->first] = texs;
		if(texs.empty()){ ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-11", where, e.dirIndex, e.name, "в TXD нет текстур — GetFirstTexture → NULL → CopyTexture mov ebx,[eax] → краш", ""); continue; }
		if(texs.size() > 1 && it->first.compare(0, 7, "player_") != 0) ctx.add(SEV_INFO, CAT_CLOTHES, "CLO-11", where, e.dirIndex, e.name, fmt("%d текстур — используется только первая («%s»)", (int)texs.size(), texs[0].name.c_str()), "");
		const TexPeek &t0 = texs[0];
		if(t0.depth != 32 || t0.compressed || t0.palette)
			ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-10", where, e.dirIndex, e.name, fmt("растр «%s»: %d bpp%s%s — CopyTexture/BlendTextures/PlaceTextureOnTopOfTexture читают и пишут 4 байта на пиксель через RwRasterLock: буфер сжатого растра в 4–8 раз меньше w×h×4 → переполнение кучи", t0.name.c_str(), t0.depth, t0.compressed ? ", DXT" : "", t0.palette ? ", палитра" : ""), "Все растры player.img — несжатые 32 bpp (ваниль: формат 0x500/0x600, 1 мип).");
		const char *role = nullptr; int rw = 0, rh = 0;
		if(torsoTxd.count(it->first)){ role = "torso-слот"; rw = 256; rh = 256; }
		else if(legsTxd.count(it->first)){ role = "legs-слот"; rw = 128; rh = 256; }
		else if(tattooTxd.count(it->first)){ role = "татуировка"; rw = 256; rh = 256; }
		if(role && (t0.width != rw || t0.height != rh))
			ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-10", where, e.dirIndex, e.name, fmt("%s: %d×%d вместо %d×%d — PlaceTextureOnTopOfTexture 0x5A57B0 идёт по w×h источника: больший overlay пишет за растр назначения (краш), меньший оставляет остаток нетронутым", role, t0.width, t0.height, rw, rh), "");
	}
	// CLO-09 base TXDs
	{
		struct { const char *txd; const char *base; const char *fat; const char *ripped; } B[2] = { { "player_torso", "torso", "torso_fat", "torso_ripped" }, { "player_legs", "legs", "legs_fat", "legs_ripped" } };
		for(int k = 0; k < 2; k++){
			auto it = peeked.find(B[k].txd);
			if(it == peeked.end()) continue;	// CLO-04 already
			const TexPeek *base = nullptr, *fat = nullptr, *rip = nullptr;
			for(size_t i = 0; i < it->second.size(); i++){
				const TexPeek &t = it->second[i];
				if(ieq(t.name, B[k].base)) base = &t; else if(ieq(t.name, B[k].fat)) fat = &t; else if(ieq(t.name, B[k].ripped)) rip = &t;
			}
			std::string where = std::string("player.img/") + B[k].txd + ".txd";
			for(int q = 0; q < 3; q++){
				const TexPeek *t = q == 0 ? base : q == 1 ? fat : rip;
				const char *nm = q == 0 ? B[k].base : q == 1 ? B[k].fat : B[k].ripped;
				if(!t) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-09", where, -1, nm, fmt("в %s.txd нет текстуры «%s» — RwTexDictionaryFindNamedTexture → NULL → RwRasterLock(NULL) в BlendTextures → краш", B[k].txd, nm), "");
			}
			if(base && fat && (fat->width != base->width || fat->height != base->height)) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-09", where, -1, B[k].fat, fmt("«%s» %d×%d ≠ «%s» %d×%d — цикл BlendTextures ограничен растром _fat, 4 Б/пиксель", B[k].fat, fat->width, fat->height, B[k].base, base->width, base->height), "");
			if(base && rip && (rip->width != base->width || rip->height != base->height)) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-09", where, -1, B[k].ripped, fmt("«%s» %d×%d ≠ «%s» %d×%d", B[k].ripped, rip->width, rip->height, B[k].base, base->width, base->height), "");
		}
		for(const char *nm : { "player_face", "player_feet" }){
			auto it = peeked.find(nm);
			if(it != peeked.end() && it->second.empty()) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-09", std::string("player.img/") + nm + ".txd", -1, nm, "пустой базовый TXD", "");
		}
	}
	// CLO-18: player.dff / csplay.dff / player.txd in the streamed IMGs
	{
		int pe = gd.findEntry("player.dff"), ce = gd.findEntry("csplay.dff"), te = gd.findEntry("player.txd");
		if(pe < 0) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-18", "gta3.img", -1, "player.dff", "нет player.dff (скинированный кламп CJ, модель 0) — CreateSkinnedClump 0x5A6C6A: GetFirstAtomic/RpSkinGeometryGetSkin по NULL → краш", "");
		if(ce < 0) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-18", "gta3.img", -1, "csplay.dff", "нет csplay.dff (катсценный скелет CJ, hier 1 в default.ide) — RebuildCutscenePlayer → CreateSkinnedClump(NULL) → краш при первой катсцене", "");
		if(te < 0) ctx.add(SEV_ERROR, CAT_CLOTHES, "CLO-18", "gta3.img", -1, "player.txd", "нет player.txd — RequestSpecialModel назначает модели 0 словарь «generic», и каждый ребилд CJ уничтожает ВСЕ текстуры generic.txd (RwTexDictionaryForAllTextures(destroy) 0x5A69D0)", "");
		const ObjDef *pd = gd.findObj(1);
		if(pd == nullptr || pd->type != OT_HIER || !ieq(pd->name, "csplay"))
			ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-18", "data/default.ide", -1, "csplay", "нет строки «hier 1, csplay, player» — LoadCutsceneData 0x4D5E80 делает RequestModel(1) для катсценного CJ", "");
		for(int idx : { pe, ce }){
			if(idx < 0) continue;
			const Entry &e = gd.entries[(size_t)idx];
			// skinned? (any geometry with Skin PLG in the first clump)
			std::vector<uint8_t> data;
			if(!gd.readEntry(idx, data)) continue;
			CClump cl;
			bool skinned = false;
			if(parseClump(data.data(), data.size(), cl)) for(size_t k = 0; k < cl.geoms.size(); k++) if(cl.geoms[k].skin) skinned = true;
			if(!skinned) ctx.add(SEV_FATAL, CAT_CLOTHES, "CLO-18", e.where(), e.dirIndex, e.name, fmt("%s не скинен — RpSkinGeometryGetSkin → NULL, RpSkinGetNumBones(NULL) → краш при ребилде CJ", e.name.c_str()), "");
		}
	}
	// CLO-19: fat / muscular IFP blocks
	for(const char *blk : { "fat", "muscular" }){
		if(gd.findEntry(std::string(blk) + ".ifp") < 0)
			ctx.add(SEV_WARN, CAT_CLOTHES, "CLO-19", "anim/anim.img", -1, blk, fmt("нет IFP-блока «%s» — RequestMotionGroupAnims 0x5A8120: GetAnimationBlockIndex = −1, запрос ложного id 25574; анимации толстого/качка не грузятся", blk), "");
	}
}

} // namespace gc
