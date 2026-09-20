// gtacheck — TXD parser + the TXD-* rules from E:\RE\addon_check\txd_path.md
// (what RwTexDictionaryGtaStreamRead / _rwD3D9NativeTextureRead accept).
#include "gtacheck.h"
#include <sys/stat.h>

#include <math.h>
#include <algorithm>

namespace gc {

static const uint32_t FOURCC_DXT1 = 0x31545844, FOURCC_DXT2 = 0x32545844, FOURCC_DXT3 = 0x33545844,
                      FOURCC_DXT4 = 0x34545844, FOURCC_DXT5 = 0x35545844;

struct FmtEntry { int d3d; int bpp; bool alpha; const char *name; };
// rasterFormat nibble → (D3DFORMAT, depth, alpha) — table 0x85C670
static const FmtEntry fmtTable[16] = {
	{ 0, 0, false, "none" },
	{ 25, 16, true, "1555" }, { 23, 16, false, "565" }, { 26, 16, true, "4444" }, { 50, 8, false, "LUM8" },
	{ 21, 32, true, "8888" }, { 22, 32, false, "888" }, { 80, 16, false, "D16" }, { 77, 32, false, "D24" },
	{ 71, 32, false, "D32" }, { 24, 16, false, "555" },
	{ -1, 0, false, "?" }, { -1, 0, false, "?" }, { -1, 0, false, "?" }, { -1, 0, false, "?" }, { -1, 0, false, "?" },
};


static int dxtBlockBytes(uint32_t fourcc)
{
	if(fourcc == FOURCC_DXT1) return 8;
	if(fourcc == FOURCC_DXT2 || fourcc == FOURCC_DXT3 || fourcc == FOURCC_DXT4 || fourcc == FOURCC_DXT5) return 16;
	return 0;
}

static const char *fourccStr(uint32_t f)
{
	static char s[8];
	s[0] = (char)(f & 0xFF); s[1] = (char)((f >> 8) & 0xFF); s[2] = (char)((f >> 16) & 0xFF); s[3] = (char)((f >> 24) & 0xFF); s[4] = 0;
	for(int i = 0; i < 4; i++) if(s[i] < 0x20 || s[i] > 0x7E) return "";
	return s;
}

static int fullChain(int w, int h)
{
	int m = w > h ? w : h;
	int n = 1;
	while(m > 1){ m >>= 1; n++; }
	return n;
}

void CheckOneTxd(Context &ctx, const std::string &where, const std::vector<uint8_t> &data,
                 const std::string &stemLower, TxdInfo *outInfo, int entry)
{
	GameData &gd = *ctx.gd;
	bool sa = gd.isSA();
	ctx.rep->countFile(CAT_TXD);
	TxdInfo info;
	info.parsed = true;
	Buf b(data.data(), data.size());
	Chunk dict, st;
	if(!findChunk(b, 0x16, dict, data.size())){
		ctx.add(SEV_FATAL, CAT_TXD, "TXD-01", where, -1, "", "не найден чанк TEXDICTIONARY (0x16) — файл не TXD или мусор",
		        "RwStreamFindChunk(0x16) не находит чанк → LoadTxd возвращает false → TXD запрашивается заново вечно, все модели с ним невидимы.");
		info.failed = true;
		if(outInfo) *outInfo = info;
		return;
	}
	if(!gd.rwVersionOk(dict.version)){
		ctx.add(SEV_FATAL, CAT_TXD, "TXD-01", where, -1, "", fmt("версия RW чанка 0x%X вне %s (libid 0x%08X) — LOAD-FAIL", dict.version, gd.rwRange(), dict.libid),
		        sa ? "SA принимает только 0x34000..0x36003 (libid 0x1803FFFF безопасен). Файл из III/VC/RW 3.7 нужно пересохранить."
		        : gd.isVC() ? "VC принимает 0x31000..0x34003 (libid 0x0C02FFFF безопасен). Файл из SA нужно пересохранить."
		        : "III принимает 0x31000..0x33002 (libid 0x0800FFFF безопасен). Файл из VC/SA нужно пересохранить.");
		info.failed = true;
	}
	b.seek(dict.start);
	if(!findChunk(b, 0x1, st, dict.end)){
		ctx.add(SEV_FATAL, CAT_TXD, "TXD-02", where, -1, "", "нет STRUCT после TEXDICTIONARY", "");
		info.failed = true;
		if(outInfo) *outInfo = info;
		return;
	}
	if(sa && st.size != 4){
		ctx.add(SEV_FATAL, CAT_TXD, "TXD-02", where, -1, "",
		        fmt("STRUCT словаря длиной %u байт вместо 4 — читается в 4-байтовый слот на стеке (%s)", st.size,
		            st.size >= 13 ? "перезапись адреса возврата → краш" : "LOAD-FAIL"),
		        "RwTexDictionaryGtaStreamRead 0x730FC0: RwStreamRead(stream, frame+4, len). Пересохрани TXD.");
		info.failed = true;
	}
	if(!gd.rwVersionOk(st.version)) info.failed = true;
	int numTex = b.u16();
	int deviceId = b.u16();
	(void)deviceId;
	if(numTex == 0)
		ctx.add(SEV_INFO, CAT_TXD, "TXD-04", where, -1, "", "TXD без текстур", "");
	b.seek(st.end);

	std::set<std::string> seen;
	int found = 0;
	size_t limit = dict.end;
	for(int i = 0; i < numTex; i++){
		if(ctx.cancelled()) break;
		Chunk tex;
		if(!findChunk(b, 0x15, tex, limit)){
			ctx.add(SEV_FATAL, CAT_TXD, "TXD-04", where, i, "",
			        fmt("в заголовке %d текстур, найдено только %d чанков TEXTURENATIVE — FindChunk упирается в конец → LOAD-FAIL", numTex, found),
			        "Либо счётчик завышен, либо предыдущая текстура оставила лишние байты (см. TXD-18).");
			info.failed = true;
			break;
		}
		found++;
		if(!gd.rwVersionOk(tex.version)){
			ctx.add(SEV_FATAL, CAT_TXD, "TXD-01", where, i, "", fmt("текстура №%d: версия чанка 0x%X вне диапазона — LOAD-FAIL", i, tex.version), "");
			info.failed = true;
		}
		size_t texEnd = tex.end;
		Chunk ts;
		if(!findChunk(b, 0x1, ts, texEnd)){
			ctx.add(SEV_FATAL, CAT_TXD, "TXD-02", where, i, "", fmt("текстура №%d: нет STRUCT", i), "");
			info.failed = true;
			b.seek(texEnd);
			continue;
		}
		if(!gd.rwVersionOk(ts.version)) info.failed = true;
		Buf s(data.data() + ts.start, ts.end - ts.start);
		uint32_t platform = s.u32();
		if(s.ok && platform != 8 && platform != 9){
			// the native reader checks the platform id first (rwID_PCD3D8/9 != id → FALSE): the rest of the
			// STRUCT is another platform's layout, so one line for the file instead of TXD-03 per texture
			const char *pn = platform == 0x00325350 ? "PS2" : platform == 5 ? "Xbox" : platform == 0x00504350 ? "PSP" : "?";
			ctx.add(SEV_FATAL, CAT_TXD, "TXD-05", where, i, fmt("текстура №%d", i), fmt("platformId 0x%X (%s) — %s принимает только %s → LOAD-FAIL", platform, pn, sa ? "SA" : "PC", sa ? "9 (D3D9)" : "8 (D3D8)"), "Пересохрани TXD для PC.");
			info.failed = true;
			break;
		}
		uint32_t filterAddr = s.u32();
		std::string name = s.str(32);
		std::string mask = s.str(32);
		bool nameNoNul = false;
		{
			const uint8_t *np = data.data() + ts.start + 8;
			nameNoNul = ts.end - ts.start >= 40 && memchr(np, 0, 32) == nullptr;
		}
		std::string who = name.empty() ? fmt("текстура №%d", i) : name;
		if(!s.ok){
			ctx.add(SEV_FATAL, CAT_TXD, "TXD-03", where, i, who, "STRUCT текстуры короче заголовка (72 байта)", "");
			info.failed = true;
			b.seek(texEnd);
			continue;
		}
		// --- names
		std::string nl = lower(name);
		if(name.empty())
			ctx.add(SEV_ERROR, CAT_TXD, "TXD-22", where, i, who, "пустое имя текстуры — материал никогда её не найдёт", "");
		if(nameNoNul)
			ctx.add(SEV_ERROR, CAT_TXD, "TXD-22", where, i, who, fmt("имя «%s» занимает все 32 байта без NUL — RwTextureSetName обрежет до 31, материал с полным именем не совпадёт", name.c_str()),
			        "Имя 1..31 символ, NUL-паддинг до 32; в DFF должно быть то же имя.");
		if(!nl.empty()){
			if(seen.count(nl))
				ctx.add(SEV_WARN, CAT_TXD, "TXD-23", where, i, who, fmt("имя «%s» повторяется в этом TXD (без учёта регистра) — победит последняя, обе остаются в памяти", name.c_str()), "");
			seen.insert(nl);
			info.names.push_back(nl);
		}
		{ TxdInfo::Tex tx; tx.name = nl; info.texs.push_back(tx); }
		if(!mask.empty() && sa)	// III/VC: маска записана почти у каждой текстуры, RwTextureSetMaskName её только хранит
			ctx.add(SEV_INFO, CAT_TXD, "TXD-13", where, i, who, fmt("маска «%s» — на D3D9 не используется", mask.c_str()), "");
		if(mask.size() >= 32)
			ctx.add(SEV_WARN, CAT_TXD, "TXD-13", where, i, who, "имя маски ≥ 32 символов (обрежется, RwErrorSet)", "");
		// --- filter / addressing
		int filter = filterAddr & 0xFF, addrU = (filterAddr >> 8) & 0xF, addrV = (filterAddr >> 12) & 0xF;
		if(filter < 1 || filter > 6)
			ctx.add(SEV_WARN, CAT_TXD, "TXD-12", where, i, who, fmt("фильтр %d вне 1..6 — индекс в таблицу без проверки, мусорный SetSamplerState", filter), "Ваниль: 0x1106 / 0x1102 / 0x1101.");
		if(addrU < 1 || addrU > 4 || addrV < 1 || addrV > 4)
			ctx.add(SEV_WARN, CAT_TXD, "TXD-12", where, i, who, fmt("адресация U=%d V=%d вне 1..4", addrU, addrV), "");

		if(platform == 9 || (platform == 8 && !sa)){
			// D3D8 (III/VC) or D3D9 (SA) header
			uint32_t rasterFormat = s.u32();
			uint32_t d3dFormat = 0;
			bool hasAlphaField = false;
			int dxt8 = 0;
			if(platform == 9) d3dFormat = s.u32();
			else { hasAlphaField = s.u32() != 0; }
			int width = s.u16(), height = s.u16();
			int depth = s.u8(), numLevels = s.u8(), rasterType = s.u8(), flags = s.u8();
			if(platform == 8){ dxt8 = flags; flags = (dxt8 ? 8 : 0) | (hasAlphaField ? 1 : 0); }
			if(!info.texs.empty()){ info.texs.back().w = width; info.texs.back().h = height; info.texs.back().alpha = (flags & 1) != 0; }
			if(!s.ok){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-03", where, i, who, "STRUCT короче заголовка растра", "");
				info.failed = true; b.seek(texEnd); continue;
			}
			if(platform == 8 && sa){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-05", where, i, who, "platformId 8 (D3D8, III/VC) — SA принимает только 9, RwTextureGtaStreamRead вернёт NULL → LOAD-FAIL", "Пересохрани TXD как D3D9.");
				info.failed = true;
			}
			int nib = (rasterFormat >> 8) & 0xF;
			bool mip = (rasterFormat & 0x8000) != 0;
			bool autoMip = (rasterFormat & 0x1000) != 0;
			bool pal8 = (rasterFormat & 0x2000) != 0, pal4 = (rasterFormat & 0x4000) != 0;
			bool compressed = (flags & 8) != 0;
			bool cube = (flags & 2) != 0;
			bool flagAutoMip = (flags & 4) != 0;
			bool hasAlpha = (flags & 1) != 0;
			int blockBytes = platform == 9 ? dxtBlockBytes(d3dFormat) : (dxt8 == 1 ? 8 : (dxt8 >= 2 && dxt8 <= 5 ? 16 : 0));
			bool fatalFmt = false;

			if(rasterType != 4 && rasterType != 0){
				ctx.add(rasterType == 1 || rasterType == 3 || rasterType == 2 ? SEV_FATAL : SEV_ERROR, CAT_TXD, "TXD-06", where, i, who,
				        fmt("rasterType %d вместо 4 — %s", rasterType, rasterType == 1 || rasterType == 3 ? "lock не удаётся, RwStreamRead(NULL) → краш" : rasterType == 2 ? "запись в back-buffer камеры" : "лишние биты попадают в cFlags"), "");
			}
			if(cube){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-11", where, i, who, compressed ? "cube-map (flags bit1) — требует caps, ваниль не использует" : "несжатый cube-map никогда не загружается (ext->d3dFormat не записывается) — LOAD-FAIL", "");
				fatalFmt = true;
			}
			if((flagAutoMip || autoMip) && platform == 9){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-10", where, i, who, "AUTOMIPMAP (rasterFormat 0x1000 / flags bit2) — читается один уровень, остальные байты ломают поток → LOAD-FAIL (или отказ устройства)", "Сними бит и запиши полную цепочку мипов.");
				fatalFmt = true;
			}
			if(platform == 8 && dxt8 > 5){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-19", where, i, who, fmt("dxtFormat %d — _rwD3D8NativeTextureRead знает только 1..5 → LOAD-FAIL", dxt8), "");
				fatalFmt = true;
			}
			// D3D8 (III/VC): CTxdStore включает _rwD3D8TexDictionaryEnableRasterFormatConversion — PAL8/PAL4 и прочие
			// неподдерживаемые форматы конвертируются через RwImage, AUTOMIPMAP снимается на время чтения, а
			// rasterFormat не сверяется с cFormat — TXD-07/10/19 только для D3D9
			if((pal4 || pal8) && platform == 9){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-07", where, i, who, pal4 ? "PAL4 — rwD3D9SetRasterFormat возвращает 0 → LOAD-FAIL" : "PAL8 — D3DFMT_P8 не поддерживается современными драйверами → LOAD-FAIL (с compressed → краш)", "Конвертируй в DXT/8888.");
				fatalFmt = true;
			}
			if((rasterFormat & 0xFF || rasterFormat & 0xFFFF0000) && platform == 9){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-19", where, i, who, fmt("rasterFormat 0x%X содержит биты вне 0x0F00/0x8000 — проверка rasterFormat == cFormat<<8 не пройдёт → LOAD-FAIL", rasterFormat), "");
				fatalFmt = true;
			}
			if(platform == 9){
				if(compressed){
					if(blockBytes == 0){
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-19", where, i, who, fmt("flags bit3 (compressed), но d3dFormat 0x%08X «%s» не DXT", d3dFormat, fourccStr(d3dFormat)), "");
						fatalFmt = true;
					}
					if(nib == 0 || fmtTable[nib].d3d < 0){
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-19", where, i, who, fmt("rasterFormat-нибл 0x%X с DXT — %s → LOAD-FAIL", nib, nib == 0 ? "подставится формат дисплея и не совпадёт" : "за пределами таблицы"), "DXT1 → 0x200/0x100, DXT3 → 0x300, DXT5 → 0x300/0x500.");
						fatalFmt = true;
					}
				}else{
					if(blockBytes){
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-19", where, i, who, fmt("d3dFormat «%s» (DXT) без flags bit3 — несовпадение форматов → LOAD-FAIL", fourccStr(d3dFormat)), "");
						fatalFmt = true;
					}else if(nib == 0 || fmtTable[nib].d3d < 0){
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-19", where, i, who, fmt("rasterFormat-нибл 0x%X вне таблицы 1..6/0xA → LOAD-FAIL", nib), "");
						fatalFmt = true;
					}else if((int)d3dFormat != fmtTable[nib].d3d){
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-19", where, i, who, fmt("несжатый растр: нибл 0x%X (%s) требует D3DFMT %d, в файле %u → LOAD-FAIL", nib, fmtTable[nib].name, fmtTable[nib].d3d, d3dFormat), "");
						fatalFmt = true;
					}
				}
			}
			if(width == 0 || height == 0){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-09", where, i, who, fmt("размер %dx%d — нулевая сторона (краш при lock или LOAD-FAIL)", width, height), "");
				fatalFmt = true;
			}else{
				bool pow2 = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
				if(!pow2)
					ctx.add(SEV_WARN, CAT_TXD, "TXD-08", where, i, who, fmt("размер %dx%d не степень двойки — на видеокартах с POW2-caps LOAD-FAIL", width, height), "", -1, FIX_TXD_POW2, entry);
				if(width > 4096 || height > 4096)
					ctx.add(SEV_ERROR, CAT_TXD, "TXD-08", where, i, who, fmt("размер %dx%d больше 4096 — превышает MaxTextureWidth многих устройств → LOAD-FAIL", width, height), "");
				else if(width > 2048 || height > 2048)
					ctx.add(SEV_WARN, CAT_TXD, "TXD-08", where, i, who, fmt("размер %dx%d больше 2048 — старые видеокарты откажут", width, height), "");
			}
			// alpha flag vs format
			if(platform == 9 && compressed){
				if(blockBytes == 16 && !hasAlpha){
					ctx.add(SEV_WARN, CAT_TXD, "TXD-20", where, i, who, fmt("%s без флага alpha — отрисуется непрозрачной", fourccStr(d3dFormat)), "", -1, FIX_TXD_DXTALPHA, entry);
					// the alpha block is written, paid for in memory, and then never sampled
					ctx.add(SEV_INFO, CAT_TXD, "TXD-21", where, i, who,
					        fmt("%s без альфы — 16 байт на блок вместо 8 у DXT1, текстура занимает вдвое больше без пользы", fourccStr(d3dFormat)),
					        "Пересохрани в DXT1 (просмотрщик TXD → «Пересохранить»): цветные блоки те же, теряется только неиспользуемый канал альфы.");
				}
			}
			// --- levels
			int chain = fullChain(width, height);
			int expectLevels = mip ? chain : 1;
			if(numLevels == 0)
				ctx.add(SEV_ERROR, CAT_TXD, "TXD-15", where, i, who, "numLevels = 0 — ни один уровень не читается, текстура из мусора", "");
			else if(numLevels > expectLevels)
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-14", where, i, who, fmt("numLevels %d > %d (%s) — GetSurfaceLevel для лишнего уровня даёт NULL, RwStreamRead(NULL) → краш", numLevels, expectLevels, mip ? "полная цепочка" : "без бита MIPMAP один уровень"), "");
			else if(mip && numLevels < expectLevels){
				// compressed: D3DResourceSystem::CreateTexture keeps levels==1 as a single level (only >1 becomes "full chain");
				// uncompressed: rwD3D9CreateTexture asks for the full chain whenever the MIPMAP bit is set
				int lastW = width >> (numLevels - 1), lastH = height >> (numLevels - 1);
				int lastMax = lastW > lastH ? lastW : lastH;
				if(compressed && numLevels == 1)
					ctx.add(SEV_INFO, CAT_TXD, "TXD-15", where, i, who, "бит MIPMAP (0x8000) при одном уровне — DXT-текстура создаётся одноуровневой, флаг лишний", "");
				else if(lastMax <= 4)
					ctx.add(SEV_INFO, CAT_TXD, "TXD-15", where, i, who, fmt("numLevels %d < цепочки %d — не записаны только уровни меньше 4×4 (ваниль хранит их с размером 0, тот же мусор)", numLevels, expectLevels), "");
				else
					ctx.add(SEV_ERROR, CAT_TXD, "TXD-15", where, i, who, fmt("numLevels %d < полной цепочки %d — D3D создаёт всю цепочку, непрочитанные уровни остаются мусором (видно вдали)", numLevels, expectLevels), "");
			}
			// palette
			if(pal4) s.skip(0x80); else if(pal8) s.skip(0x400);
			int faces = cube ? 6 : 1;
			int levelsToRead = numLevels;
			int lw = width, lh = height;
			bool levelFail = false;
			for(int f = 0; f < faces && s.ok && !levelFail; f++){
				lw = width; lh = height;
				for(int lv = 0; lv < levelsToRead; lv++){
					uint32_t size = s.u32();
					if(!s.ok){
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-18", where, i, who, fmt("уровень %d: данные обрываются внутри STRUCT — короткое чтение → LOAD-FAIL", lv), "");
						info.failed = true; levelFail = true; break;
					}
					uint32_t expect;
					if(blockBytes) expect = (uint32_t)((lw + 3) / 4 < 1 ? 1 : (lw + 3) / 4) * (uint32_t)((lh + 3) / 4 < 1 ? 1 : (lh + 3) / 4) * blockBytes;
					else if(platform == 8) expect = ((uint32_t)lw * (uint32_t)lh * (uint32_t)(depth ? depth : 32) + 7) / 8;	// D3D8 header carries the real depth (8 for PAL8)
					else expect = (uint32_t)lw * (uint32_t)(fmtTable[nib].bpp ? fmtTable[nib].bpp : 32) / 8 * (uint32_t)lh;
					info.bytes += expect;	// what the raster takes in memory (QLT-04: heavy TXD)
					if(!info.texs.empty()) info.texs.back().bytes += expect;
					if(lv == 0 && f == 0 && (width >= 1024 || height >= 1024)) info.big++;
					if(size > expect)
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-17", where, i, who, fmt("уровень %d (%dx%d): размер %u > %u байт поверхности — запись за пределы системной копии текстуры (порча кучи)", lv, lw, lh, size, expect), "");
					else if(size == 0){
						if(lv == 0) ctx.add(SEV_ERROR, CAT_TXD, "TXD-16", where, i, who, "верхний уровень с размером 0 — текстура из мусора", "");
						else if(lw >= 4 && lh >= 4) ctx.add(SEV_WARN, CAT_TXD, "TXD-16", where, i, who, fmt("уровень %d (%dx%d) с размером 0 — останется неинициализированным", lv, lw, lh), "Ваниль пишет 0 только для уровней меньше 4×4.");
					}else if(size < expect)
						ctx.add(SEV_WARN, CAT_TXD, "TXD-16", where, i, who, fmt("уровень %d (%dx%d): %u из %u байт — хвост уровня мусор", lv, lw, lh, size, expect), "");
					if(!s.skip(size)){
						ctx.add(SEV_FATAL, CAT_TXD, "TXD-18", where, i, who, fmt("уровень %d: заявлено %u байт, в STRUCT осталось меньше → короткое чтение → LOAD-FAIL", lv, size), "");
						info.failed = true; levelFail = true; break;
					}
					lw = lw > 1 ? lw >> 1 : 1; lh = lh > 1 ? lh >> 1 : 1;
				}
			}
			if(!levelFail && s.ok && s.left() > 0){
				ctx.add(SEV_FATAL, CAT_TXD, "TXD-18", where, i, who, fmt("после последнего уровня в STRUCT остаётся %u байт — следующий FindChunk прочитает их как заголовок → LOAD-FAIL", (unsigned)s.left()),
				        "Читатель не использует длину STRUCT; каждый байт должен быть съеден уровнями.");
				info.failed = true;
			}
			if(fatalFmt) info.failed = true;
		}else{
			const char *pn = platform == 8 ? "D3D8" : platform == 0x00325350 ? "PS2" : platform == 5 ? "Xbox" : "?";
			ctx.add(SEV_FATAL, CAT_TXD, "TXD-05", where, i, who, fmt("platformId %u (%s) — %s принимает только %s → LOAD-FAIL", platform, pn, sa ? "SA" : "PC", sa ? "9 (D3D9)" : "8 (D3D8)"), "Пересохрани TXD для PC.");
			info.failed = true;
		}
		b.seek(texEnd);
	}
	if(found == numTex){
		// extra textures beyond the count are silently ignored
		Chunk extra;
		size_t save = b.pos;
		if(findChunk(b, 0x15, extra, limit))
			ctx.add(SEV_WARN, CAT_TXD, "TXD-04", where, numTex, "", fmt("в файле больше текстур, чем %d в заголовке — лишние не загрузятся", numTex), "");
		b.seek(save);
	}
	if(outInfo) *outInfo = info;
}

// ------------------------------------------------------------- driver ----

// III: models/txd.img is a cache the game builds for the video card (CStreaming::Init → CreateTxdImageForVideoCard) and,
// while its saved caps match, ADDS FIRST — every texture comes from it, not from gta3.img. A modded TXD in gta3.img is
// invisible until the cache is deleted.
static void checkTxdImgCache(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isIII()) return;
	std::string cache = resolvePath(gd.root, "models/txd.img"), img = resolvePath(gd.root, "models/gta3.img");
	if(!fileExists(cache)) return;
	struct _stat64 a, b;
	bool stale = _stat64(cache.c_str(), &a) == 0 && _stat64(img.c_str(), &b) == 0 && a.st_mtime < b.st_mtime;
	ctx.add(stale ? SEV_WARN : SEV_INFO, CAT_TXD, "TXD-29", "models/txd.img", -1, "",
	        stale ? "кэш текстур видеокарты старше gta3.img — игра берёт TXD из кэша, правки текстур в gta3.img не видны" : "кэш текстур видеокарты (CreateTxdImageForVideoCard) — пока он есть, TXD читаются из него, а не из gta3.img",
	        "После правки любого TXD удалить models/txd.img и txd.dir — игра пересоберёт их при запуске.");
}

void CheckTxds(Context &ctx)
{
	GameData &gd = *ctx.gd;
	ctx.prog->set("TXD");
	checkTxdImgCache(ctx);
	int total = 0;
	for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].kind == EK_TXD) total++;
	int done = 0;
	for(size_t i = 0; i < gd.entries.size(); i++){
		if(ctx.cancelled()) return;
		Entry &e = gd.entries[i];
		if(e.kind != EK_TXD || !gd.isWinner((int)i)) continue;
		done++;
		if(ctx.opt.skipVanillaImgs && e.img >= 0 && gd.archives[e.img].vanilla){
			// still need texture names for the cross reference → parse silently
		}
		std::string where = (e.img >= 0 ? basename(gd.archives[e.img].logical) + "/" : "") + e.name;
		ctx.prog->step(e.name.c_str(), done, total);
		std::vector<uint8_t> data;
		std::string err;
		if(!gd.readEntry((int)i, data, &err)){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-24", where, e.dirIndex, e.name, "не удалось прочитать запись: " + err, "");
			continue;
		}
		TxdInfo info;
		bool quiet = ctx.opt.skipVanillaImgs && e.img >= 0 && gd.archives[e.img].vanilla;
		if(quiet){
			// parse names only, without emitting issues
			Report tmp; Context q = ctx; q.rep = &tmp;
			CheckOneTxd(q, where, data, e.base, &info, (int)i);
		}else
			CheckOneTxd(ctx, where, data, e.base, &info, (int)i);
		gd.txdTextures[e.base] = info;
		if(e.base == "particle")
			for(size_t k = 0; k < info.names.size(); k++) gd.particleTextures.insert(info.names[k]);
	}
	// TXDs the engine loads itself from loose files (not through the streamer)
	{
		struct Loose { const char *path; const char *slot; };
		static const Loose looseTxds[] = {
			{ "models/generic.txd", "generic" }, { "models/generic/vehicle.txd", "vehicle" },
			{ "models/particle.txd", "particle" }, { "models/effectsPC.txd", "effectspc" }, { nullptr, nullptr } };
		for(int k = 0; looseTxds[k].path; k++){
			std::string logical = looseTxds[k].path;
			std::string phys = resolvePath(gd.root, logical);
			if(gd.modloaderActive){
				auto it = gd.looseByName.find(lower(basename(logical)));
				if(it != gd.looseByName.end()) phys = gd.modFiles[(size_t)it->second].phys;
			}
			std::vector<uint8_t> data;
			if(!readFile(phys, data)) continue;
			TxdInfo info;
			CheckOneTxd(ctx, logical, data, looseTxds[k].slot, &info);
			gd.txdTextures[looseTxds[k].slot] = info;
			gd.txdSlotOf(looseTxds[k].slot);
			if(std::string(looseTxds[k].slot) == "particle")
				for(size_t n = 0; n < info.names.size(); n++) gd.particleTextures.insert(info.names[n]);
		}
	}
	// TEXDICTION lines (generic.txd etc.)
	for(size_t i = 0; i < gd.datFiles.size(); i++){
		if(gd.datFiles[i].compare(0, 11, "TEXDICTION:") != 0) continue;
		std::string logical = gd.datFiles[i].substr(11);
		std::string phys = resolvePath(gd.root, logical);
		std::vector<uint8_t> data;
		if(!readFile(phys, data)){
			ctx.add(SEV_ERROR, CAT_DAT, "DAT-03", logical, -1, "", "TEXDICTION: файл не найден (LoadTexDictionary вернёт пустой словарь)", "");
			continue;
		}
		TxdInfo info;
		CheckOneTxd(ctx, logical, data, lower(stem(logical)), &info);
		gd.txdTextures[lower(stem(logical))] = info;
		// LoadLevel: AddTexDictionaries(savedTxd, txd) — the textures land in the current dictionary = the «generic» slot
		// (III: misc.txd, VC: wheels.txd), so the cross-reference must see them there; a duplicate name keeps the first
		TxdInfo &gen = gd.txdTextures["generic"];
		gen.parsed = true;
		for(size_t k = 0; k < info.names.size(); k++)
			if(std::find(gen.names.begin(), gen.names.end(), info.names[k]) == gen.names.end()){
				gen.names.push_back(info.names[k]);
				if(k < info.texs.size()) gen.texs.push_back(info.texs[k]);
			}
	}
	// limits
	int slots = (int)gd.txdSlots.size();
	if(gd.isSA() && slots + 20 > 5000)
		ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "TXD-27", "IDE/IMG", -1, "",
		        fmt("%d различных имён TXD (+~20 системных) — пул CTxdStore на 5000, AddTxdSlot пишет по NULL", slots), ctx.opt.limitAdjuster ? "Лимит-аджастер включён — проверь его настройку." : "");
	else if(gd.isSA() && slots > 4500)
		ctx.add(SEV_WARN, CAT_LIMIT, "TXD-27", "IDE/IMG", -1, "", fmt("%d имён TXD из 5000 доступных — запас меньше 500", slots), "");
	// parent chain cycles
	for(size_t i = 0; i < gd.txdSlots.size(); i++){
		int p = gd.txdSlots[i].parent, depth = 0;
		while(p >= 0 && depth < 64){ if(p == (int)i){ ctx.add(SEV_FATAL, CAT_XREF, "TXD-26", "txdp", -1, gd.txdSlots[i].shown, "цикл в цепочке txdp — ни один TXD цепочки не загрузится", ""); break; } p = gd.txdSlots[p].parent; depth++; }
		if(gd.txdSlots[i].parent >= 0){
			const TxdSlot &par = gd.txdSlots[gd.txdSlots[i].parent];
			if(par.entry < 0 && gd.txdTextures.find(par.name) == gd.txdTextures.end())
				ctx.add(SEV_FATAL, CAT_XREF, "TXD-26", "txdp", -1, gd.txdSlots[i].shown, fmt("родительский TXD «%s» нет ни в одном IMG — дочерний «%s» никогда не загрузится", par.shown.c_str(), gd.txdSlots[i].shown.c_str()), "");
		}
	}
}

} // namespace gc
