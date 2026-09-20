// gtacheck — TXD read / decode / repair / write at the byte level (independent of librw, so
// palettized and D3D8 textures can be shown and converted even where the D3D9 device refuses them).
//
// Repairs: D3D8 → D3D9 native layout, PAL4/PAL8 → 32-bit (or DXT), AUTOMIPMAP → explicit levels,
// non-power-of-two → nearest power of two (bilinear), filter mode out of 1..6 → LINEARMIPLINEAR,
// mask name cleared. Every converted texture is decoded to RGBA8, optionally resampled, mip chain
// regenerated (box filter) when the original had one, then encoded as A8R8G8B8 or DXT1/DXT5
// (range-fit encoder — fine for a repair tool). Untouched textures are re-serialised bit-exact.
#include "gtacheck.h"

#include <math.h>

namespace gc {

static const uint32_t FCC_DXT1 = 0x31545844, FCC_DXT2 = 0x32545844, FCC_DXT3 = 0x33545844, FCC_DXT4 = 0x34545844, FCC_DXT5 = 0x35545844;
enum { D3DFMT_A8R8G8B8 = 21, D3DFMT_X8R8G8B8 = 22, D3DFMT_R5G6B5 = 23, D3DFMT_X1R5G5B5 = 24, D3DFMT_A1R5G5B5 = 25, D3DFMT_A4R4G4B4 = 26, D3DFMT_P8 = 41, D3DFMT_L8 = 50 };

static bool isDxt(uint32_t f) { return f == FCC_DXT1 || f == FCC_DXT2 || f == FCC_DXT3 || f == FCC_DXT4 || f == FCC_DXT5; }
static int dxtBlock(uint32_t f) { return f == FCC_DXT1 ? 8 : isDxt(f) ? 16 : 0; }

const char *TxdFormatName(const TxdTex &t)
{
	if(t.rasterFormat & 0x2000) return "PAL8";
	if(t.rasterFormat & 0x4000) return "PAL4";
	switch(t.d3dFormat){
	case FCC_DXT1: return "DXT1"; case FCC_DXT2: return "DXT2"; case FCC_DXT3: return "DXT3"; case FCC_DXT4: return "DXT4"; case FCC_DXT5: return "DXT5";
	case D3DFMT_A8R8G8B8: return "8888"; case D3DFMT_X8R8G8B8: return "888"; case D3DFMT_R5G6B5: return "565";
	case D3DFMT_X1R5G5B5: return "555"; case D3DFMT_A1R5G5B5: return "1555"; case D3DFMT_A4R4G4B4: return "4444"; case D3DFMT_L8: return "LUM8"; case D3DFMT_P8: return "P8";
	}
	static char s[16]; snprintf(s, sizeof(s), "0x%X", t.d3dFormat); return s;
}

static uint32_t d3dFromRaster(uint32_t rf)
{
	switch(rf & 0x0F00){
	case 0x0100: return D3DFMT_A1R5G5B5; case 0x0200: return D3DFMT_R5G6B5; case 0x0300: return D3DFMT_A4R4G4B4; case 0x0400: return D3DFMT_L8;
	case 0x0500: return D3DFMT_A8R8G8B8; case 0x0600: return D3DFMT_X8R8G8B8; case 0x0A00: return D3DFMT_X1R5G5B5;
	}
	return 0;
}

static size_t levelBytes(const TxdTex &t, int w, int h)
{
	if(w < 1) w = 1; if(h < 1) h = 1;
	if(t.compressed) return (size_t)((w + 3) / 4) * ((h + 3) / 4) * dxtBlock(t.d3dFormat);
	return (size_t)w * h * (t.depth / 8);
}

// ------------------------------------------------------------------ parse ---

bool TxdParse(const std::vector<uint8_t> &data, TxdFile &out)
{
	out = TxdFile();
	Buf b(data.data(), data.size());
	Chunk dict, st;
	if(!findChunk(b, 0x16, dict, data.size())){ out.err = T("не найден чанк TEXDICTIONARY"); return false; }
	out.libid = dict.libid; out.version = dict.version;
	b.seek(dict.start);
	if(!findChunk(b, 0x1, st, dict.end)){ out.err = T("нет STRUCT словаря"); return false; }
	int numTex = b.u16();
	out.deviceId = b.u16();
	b.seek(st.end);
	for(int i = 0; i < numTex; i++){
		Chunk tex;
		if(!findChunk(b, 0x15, tex, dict.end)){ out.err = fmt(T("в заголовке %d текстур, найдено %d"), numTex, i); break; }
		TxdTex t;
		t.version = tex.version;
		Chunk ts;
		if(!findChunk(b, 0x1, ts, tex.end)){ t.ok = false; t.err = T("нет STRUCT"); out.tex.push_back(t); b.seek(tex.end); continue; }
		Buf s(data.data() + ts.start, ts.end - ts.start);
		t.platform = s.u32();
		t.filterAddr = s.u32();
		t.name = s.str(32);
		t.mask = s.str(32);
		t.rasterFormat = s.u32();
		if(t.platform == 8){
			t.hasAlpha = s.u32() != 0;
			t.width = s.u16(); t.height = s.u16(); t.depth = s.u8(); t.mips = s.u8(); t.type = s.u8();
			t.compression = s.u8();
			t.compressed = t.compression != 0;
			t.d3dFormat = t.compressed ? (t.compression == 1 ? FCC_DXT1 : t.compression == 2 ? FCC_DXT2 : t.compression == 3 ? FCC_DXT3 : t.compression == 4 ? FCC_DXT4 : FCC_DXT5)
			            : (t.rasterFormat & 0x6000) ? D3DFMT_P8 : d3dFromRaster(t.rasterFormat);
			t.flags = (uint8_t)((t.hasAlpha ? 1 : 0) | (t.compressed ? 8 : 0));
		}else{
			t.d3dFormat = s.u32();
			t.width = s.u16(); t.height = s.u16(); t.depth = s.u8(); t.mips = s.u8(); t.type = s.u8();
			t.flags = s.u8();
			t.hasAlpha = (t.flags & 1) != 0;
			t.compressed = (t.flags & 8) != 0;
			t.compression = 0;
		}
		if(!s.ok){ t.ok = false; t.err = T("STRUCT короче заголовка"); out.tex.push_back(t); b.seek(tex.end); continue; }
		if(t.rasterFormat & 0x4000){ const uint8_t *p = s.ptr(4 * 32); if(p) t.palette.assign(p, p + 4 * 32); }
		else if(t.rasterFormat & 0x2000){ const uint8_t *p = s.ptr(4 * 256); if(p) t.palette.assign(p, p + 4 * 256); }
		int n = t.mips < 1 ? 1 : t.mips;
		for(int L = 0; L < n; L++){
			uint32_t sz = s.u32();
			if(!s.ok || sz > s.left()){ t.ok = false; t.err = fmt(T("уровень %d выходит за STRUCT"), L); break; }
			const uint8_t *p = s.ptr(sz);
			t.levels.push_back(std::vector<uint8_t>(p, p + sz));
		}
		// the texture's EXTENSION chunk, copied verbatim
		b.seek(ts.end);
		Chunk ex;
		if(findChunk(b, 0x3, ex, tex.end)){
			size_t hdr = ex.start - 12;
			t.extension.assign(data.begin() + (ptrdiff_t)hdr, data.begin() + (ptrdiff_t)ex.end);
		}
		out.tex.push_back(t);
		b.seek(tex.end);
	}
	// dictionary EXTENSION (after the textures)
	{
		Chunk ex;
		if(findChunk(b, 0x3, ex, dict.end)){ size_t hdr = ex.start - 12; out.extension.assign(data.begin() + (ptrdiff_t)hdr, data.begin() + (ptrdiff_t)ex.end); }
	}
	out.ok = true;
	return true;
}

// ----------------------------------------------------------------- decode ---

static inline void put(uint8_t *o, int r, int g, int b, int a) { o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)b; o[3] = (uint8_t)a; }
static inline void c565(uint16_t c, int &r, int &g, int &b) { r = ((c >> 11) & 31) * 255 / 31; g = ((c >> 5) & 63) * 255 / 63; b = (c & 31) * 255 / 31; }

static void decodeDxtBlock(uint32_t fmt, const uint8_t *blk, bool oneBitAlpha, uint8_t *out, int w, int h, int bx, int by)
{
	uint8_t alpha[16];
	const uint8_t *col = blk;
	if(fmt == FCC_DXT1){ for(int i = 0; i < 16; i++) alpha[i] = 255; }
	else if(fmt == FCC_DXT2 || fmt == FCC_DXT3){
		for(int i = 0; i < 16; i++){ int v = (blk[i / 2] >> ((i & 1) * 4)) & 15; alpha[i] = (uint8_t)(v * 17); }
		col = blk + 8;
	}else{
		int a0 = blk[0], a1 = blk[1];
		int tab[8]; tab[0] = a0; tab[1] = a1;
		if(a0 > a1){ for(int i = 1; i < 7; i++) tab[i + 1] = ((7 - i) * a0 + i * a1) / 7; }
		else{ for(int i = 1; i < 5; i++) tab[i + 1] = ((5 - i) * a0 + i * a1) / 5; tab[6] = 0; tab[7] = 255; }
		uint64_t bits = 0; for(int i = 0; i < 6; i++) bits |= (uint64_t)blk[2 + i] << (8 * i);
		for(int i = 0; i < 16; i++) alpha[i] = (uint8_t)tab[(bits >> (3 * i)) & 7];
		col = blk + 8;
	}
	uint16_t c0 = (uint16_t)(col[0] | (col[1] << 8)), c1 = (uint16_t)(col[2] | (col[3] << 8));
	int r[4], g[4], b[4], a[4] = { 255, 255, 255, 255 };
	c565(c0, r[0], g[0], b[0]); c565(c1, r[1], g[1], b[1]);
	if(c0 > c1 || fmt != FCC_DXT1){
		r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
		r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
	}else{
		r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
		r[3] = g[3] = b[3] = 0; a[3] = 0;
	}
	(void)oneBitAlpha;
	uint32_t idx = (uint32_t)(col[4] | (col[5] << 8) | (col[6] << 16) | ((uint32_t)col[7] << 24));
	for(int py = 0; py < 4; py++) for(int px = 0; px < 4; px++){
		int x = bx * 4 + px, y = by * 4 + py;
		if(x >= w || y >= h) continue;
		int i = py * 4 + px, k = (idx >> (2 * i)) & 3;
		put(out + ((size_t)y * w + x) * 4, r[k], g[k], b[k], a[k] == 0 ? 0 : alpha[i]);
	}
}

bool TxdDecode(const TxdTex &t, std::vector<uint8_t> &rgba)
{
	if(!t.ok || t.levels.empty() || t.width <= 0 || t.height <= 0) return false;
	int w = t.width, h = t.height;
	const std::vector<uint8_t> &L = t.levels[0];
	rgba.assign((size_t)w * h * 4, 0);
	if(t.compressed){
		if(!isDxt(t.d3dFormat)) return false;
		int bw = (w + 3) / 4, bh = (h + 3) / 4, bs = dxtBlock(t.d3dFormat);
		if(L.size() < (size_t)bw * bh * bs) return false;
		for(int by = 0; by < bh; by++) for(int bx = 0; bx < bw; bx++) decodeDxtBlock(t.d3dFormat, L.data() + ((size_t)by * bw + bx) * bs, t.hasAlpha, rgba.data(), w, h, bx, by);
		return true;
	}
	size_t n = (size_t)w * h;
	if(t.rasterFormat & 0x6000){
		bool p4 = (t.rasterFormat & 0x4000) != 0;
		size_t need = p4 ? (n + 1) / 2 : n;
		if(t.palette.size() < (size_t)(p4 ? 32 : 256) * 4 || L.size() < need) return false;
		for(size_t i = 0; i < n; i++){
			int k = p4 ? ((L[i / 2] >> ((i & 1) * 4)) & 15) : L[i];	// the RW/D3D palette is R,G,B,A
			const uint8_t *c = t.palette.data() + (size_t)k * 4;
			put(rgba.data() + i * 4, c[0], c[1], c[2], c[3]);
		}
		return true;
	}
	switch(t.d3dFormat){
	case D3DFMT_A8R8G8B8: if(L.size() < n * 4) return false; for(size_t i = 0; i < n; i++) put(rgba.data() + i * 4, L[i * 4 + 2], L[i * 4 + 1], L[i * 4], L[i * 4 + 3]); return true;
	case D3DFMT_X8R8G8B8: if(L.size() < n * 4) return false; for(size_t i = 0; i < n; i++) put(rgba.data() + i * 4, L[i * 4 + 2], L[i * 4 + 1], L[i * 4], 255); return true;
	case D3DFMT_L8: if(L.size() < n) return false; for(size_t i = 0; i < n; i++) put(rgba.data() + i * 4, L[i], L[i], L[i], 255); return true;
	case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: {
		if(L.size() < n * 2) return false;
		for(size_t i = 0; i < n; i++){
			uint16_t c = (uint16_t)(L[i * 2] | (L[i * 2 + 1] << 8));
			int r, g, b, a = 255;
			if(t.d3dFormat == D3DFMT_R5G6B5) c565(c, r, g, b);
			else if(t.d3dFormat == D3DFMT_A4R4G4B4){ a = ((c >> 12) & 15) * 17; r = ((c >> 8) & 15) * 17; g = ((c >> 4) & 15) * 17; b = (c & 15) * 17; }
			else{ r = ((c >> 10) & 31) * 255 / 31; g = ((c >> 5) & 31) * 255 / 31; b = (c & 31) * 255 / 31; if(t.d3dFormat == D3DFMT_A1R5G5B5) a = (c & 0x8000) ? 255 : 0; }
			put(rgba.data() + i * 4, r, g, b, a);
		}
		return true;
	}
	}
	return false;
}

// ----------------------------------------------------------------- encode ---

static void resampleBilinear(const std::vector<uint8_t> &src, int sw, int sh, std::vector<uint8_t> &dst, int dw, int dh)
{
	dst.assign((size_t)dw * dh * 4, 0);
	for(int y = 0; y < dh; y++){
		float fy = (y + 0.5f) * sh / dh - 0.5f; int y0 = (int)floorf(fy); float ty = fy - y0; int y1 = y0 + 1;
		if(y0 < 0) y0 = 0; if(y1 < 0) y1 = 0; if(y0 >= sh) y0 = sh - 1; if(y1 >= sh) y1 = sh - 1;
		for(int x = 0; x < dw; x++){
			float fx = (x + 0.5f) * sw / dw - 0.5f; int x0 = (int)floorf(fx); float tx = fx - x0; int x1 = x0 + 1;
			if(x0 < 0) x0 = 0; if(x1 < 0) x1 = 0; if(x0 >= sw) x0 = sw - 1; if(x1 >= sw) x1 = sw - 1;
			const uint8_t *a = &src[((size_t)y0 * sw + x0) * 4], *b = &src[((size_t)y0 * sw + x1) * 4], *c = &src[((size_t)y1 * sw + x0) * 4], *d = &src[((size_t)y1 * sw + x1) * 4];
			uint8_t *o = &dst[((size_t)y * dw + x) * 4];
			for(int k = 0; k < 4; k++){
				float v = (a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
				o[k] = (uint8_t)(v + 0.5f);
			}
		}
	}
}

static void halve(const std::vector<uint8_t> &src, int sw, int sh, std::vector<uint8_t> &dst, int &dw, int &dh)
{
	dw = sw > 1 ? sw / 2 : 1; dh = sh > 1 ? sh / 2 : 1;
	dst.assign((size_t)dw * dh * 4, 0);
	for(int y = 0; y < dh; y++) for(int x = 0; x < dw; x++){
		int sx = sw > 1 ? x * 2 : 0, sy = sh > 1 ? y * 2 : 0, sx1 = sw > 1 ? sx + 1 : sx, sy1 = sh > 1 ? sy + 1 : sy;
		const uint8_t *a = &src[((size_t)sy * sw + sx) * 4], *b = &src[((size_t)sy * sw + sx1) * 4], *c = &src[((size_t)sy1 * sw + sx) * 4], *d = &src[((size_t)sy1 * sw + sx1) * 4];
		uint8_t *o = &dst[((size_t)y * dw + x) * 4];
		for(int k = 0; k < 4; k++) o[k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2) / 4);
	}
}

static int nearestPow2(int n)
{
	if(n < 1) return 1;
	int lo = 1; while(lo * 2 <= n) lo *= 2;
	int hi = lo * 2;
	return (n - lo) < (hi - n) ? lo : hi;
}

static uint16_t to565(int r, int g, int b) { return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255)); }

// range-fit DXT1 / DXT5 block encoder
static void encodeDxtBlock(int kind, const std::vector<uint8_t> &rgba, int w, int h, int bx, int by, uint8_t *out)	// kind: 1 DXT1, 3 DXT3, 5 DXT5
{
	int px[16][4]; int n = 0;
	for(int py = 0; py < 4; py++) for(int qx = 0; qx < 4; qx++){
		int x = bx * 4 + qx, y = by * 4 + py; if(x >= w) x = w - 1; if(y >= h) y = h - 1;
		const uint8_t *p = &rgba[((size_t)y * w + x) * 4];
		px[n][0] = p[0]; px[n][1] = p[1]; px[n][2] = p[2]; px[n][3] = p[3]; n++;
	}
	if(kind == 3){	// explicit alpha: 16 × 4 bits
		for(int i = 0; i < 16; i += 2) out[i / 2] = (uint8_t)(((px[i][3] + 8) / 17) | (((px[i + 1][3] + 8) / 17) << 4));
		out += 8;
	}else if(kind == 5){
		int a0 = 255, a1 = 0;
		for(int i = 0; i < 16; i++){ if(px[i][3] < a0) a0 = px[i][3]; if(px[i][3] > a1) a1 = px[i][3]; }
		// a0 = max, a1 = min → 8-value ramp
		int amax = a1, amin = a0;
		out[0] = (uint8_t)amax; out[1] = (uint8_t)amin;
		int tab[8]; tab[0] = amax; tab[1] = amin;
		for(int i = 1; i < 7; i++) tab[i + 1] = ((7 - i) * amax + i * amin) / 7;
		uint64_t bits = 0;
		for(int i = 0; i < 16; i++){
			int best = 0, bd = 1 << 20;
			if(amax != amin) for(int k = 0; k < 8; k++){ int d = abs(tab[k] - px[i][3]); if(d < bd){ bd = d; best = k; } }
			bits |= (uint64_t)best << (3 * i);
		}
		for(int i = 0; i < 6; i++) out[2 + i] = (uint8_t)(bits >> (8 * i));
		out += 8;
	}
	// colour: extremes along the axis of largest spread
	int mn[3] = { 255, 255, 255 }, mx[3] = { 0, 0, 0 };
	for(int i = 0; i < 16; i++) for(int k = 0; k < 3; k++){ if(px[i][k] < mn[k]) mn[k] = px[i][k]; if(px[i][k] > mx[k]) mx[k] = px[i][k]; }
	// inset slightly (reduces banding on flat blocks)
	int ins[3]; for(int k = 0; k < 3; k++){ ins[k] = (mx[k] - mn[k]) / 16; mn[k] += ins[k]; mx[k] -= ins[k]; if(mx[k] < mn[k]) mx[k] = mn[k]; }
	uint16_t c0 = to565(mx[0], mx[1], mx[2]), c1 = to565(mn[0], mn[1], mn[2]);
	if(c0 < c1){ uint16_t t = c0; c0 = c1; c1 = t; }
	int r[4], g[4], b[4];
	c565(c0, r[0], g[0], b[0]); c565(c1, r[1], g[1], b[1]);
	r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
	r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
	uint32_t idx = 0;
	for(int i = 0; i < 16; i++){
		int best = 0, bd = 1 << 30;
		if(c0 != c1) for(int k = 0; k < 4; k++){
			int dr = r[k] - px[i][0], dg = g[k] - px[i][1], db = b[k] - px[i][2];
			int d = dr * dr * 2 + dg * dg * 4 + db * db;
			if(d < bd){ bd = d; best = k; }
		}
		idx |= (uint32_t)best << (2 * i);
	}
	out[0] = (uint8_t)c0; out[1] = (uint8_t)(c0 >> 8); out[2] = (uint8_t)c1; out[3] = (uint8_t)(c1 >> 8);
	out[4] = (uint8_t)idx; out[5] = (uint8_t)(idx >> 8); out[6] = (uint8_t)(idx >> 16); out[7] = (uint8_t)(idx >> 24);
}

static void encodeLevel(const std::vector<uint8_t> &rgba, int w, int h, bool dxt, bool alpha, std::vector<uint8_t> &out, int kind = 0)	// kind 0: DXT1 / DXT5 by alpha
{
	if(dxt){
		if(kind == 0) kind = alpha ? 5 : 1;
		int bw = (w + 3) / 4, bh = (h + 3) / 4, bs = kind == 1 ? 8 : 16;
		out.assign((size_t)bw * bh * bs, 0);
		for(int by = 0; by < bh; by++) for(int bx = 0; bx < bw; bx++) encodeDxtBlock(kind, rgba, w, h, bx, by, out.data() + ((size_t)by * bw + bx) * bs);
	}else{
		size_t n = (size_t)w * h;
		out.resize(n * 4);
		for(size_t i = 0; i < n; i++){ out[i * 4] = rgba[i * 4 + 2]; out[i * 4 + 1] = rgba[i * 4 + 1]; out[i * 4 + 2] = rgba[i * 4]; out[i * 4 + 3] = rgba[i * 4 + 3]; }
	}
}

// A 16-byte-block DXT (DXT2/3 explicit, DXT4/5 interpolated alpha) is a candidate for DXT1: half the
// block bytes, same colour endpoints. Whether the alpha is really unused can only be told by decoding,
// so this is just the cheap pre-filter TxdNeedsFix / the UI counter use.
static bool dxtAlphaCandidate(const TxdTex &t) { return t.compressed && dxtBlock(t.d3dFormat) == 16; }

// Rebuilds a texture from RGBA (level 0): resampled to `w`×`h`, mip chain when `mips`, 8888 or DXT.
static void rebuild(TxdTex &t, std::vector<uint8_t> rgba, int sw, int sh, int w, int h, bool mips, bool dxt, int forceAlpha = -1, int dxtKind = 0)
{
	if(w != sw || h != sh){ std::vector<uint8_t> r; resampleBilinear(rgba, sw, sh, r, w, h); rgba.swap(r); }
	bool alpha = false;
	for(size_t i = 3; i < rgba.size(); i += 4) if(rgba[i] != 255){ alpha = true; break; }
	if(forceAlpha >= 0) alpha = forceAlpha != 0;	// the caller decides (DXT5 asked for an opaque image, DXT1 for one with alpha)
	if(dxt && dxtKind == 0) dxtKind = alpha ? 5 : 1;
	if(dxt && (dxtKind == 2 || dxtKind == 4)) for(size_t i = 0; i + 3 < rgba.size(); i += 4){ int a = rgba[i + 3]; rgba[i] = (uint8_t)(rgba[i] * a / 255); rgba[i + 1] = (uint8_t)(rgba[i + 1] * a / 255); rgba[i + 2] = (uint8_t)(rgba[i + 2] * a / 255); }	// DXT2 / DXT4: premultiplied colour
	int encKind = dxtKind == 2 ? 3 : dxtKind == 4 ? 5 : dxtKind;
	t.platform = 9;
	t.width = w; t.height = h;
	t.hasAlpha = alpha;
	t.compressed = dxt; t.compression = 0;
	t.d3dFormat = dxt ? (dxtKind == 1 ? FCC_DXT1 : dxtKind == 2 ? FCC_DXT2 : dxtKind == 3 ? FCC_DXT3 : dxtKind == 4 ? FCC_DXT4 : FCC_DXT5) : D3DFMT_A8R8G8B8;
	t.depth = dxt ? (dxtKind == 1 ? 16 : 32) : 32;	// what SA's own DXT textures carry
	t.rasterFormat = (t.rasterFormat & 0x0000) | (dxt ? (alpha ? 0x0500 : 0x0200) : 0x0500);
	t.palette.clear();
	t.levels.clear();
	int cw = w, ch = h;
	std::vector<uint8_t> cur = rgba;
	for(;;){
		std::vector<uint8_t> enc; encodeLevel(cur, cw, ch, dxt, alpha, enc, encKind); t.levels.push_back(enc);
		if(!mips || (cw <= 1 && ch <= 1)) break;
		std::vector<uint8_t> nxt; int nw, nh; halve(cur, cw, ch, nxt, nw, nh); cur.swap(nxt); cw = nw; ch = nh;
	}
	t.mips = (int)t.levels.size();
	if(t.mips > 1) t.rasterFormat |= 0x8000; else t.rasterFormat &= ~0x8000u;
	t.rasterFormat &= ~0x1000u;	// never AUTOMIPMAP
	t.flags = (uint8_t)((alpha ? 1 : 0) | (dxt ? 8 : 0));
	t.ok = true;
}

// ------------------------------------------------------------------- fixes ---

static bool pow2(int n) { return n > 0 && (n & (n - 1)) == 0; }
static int fullChainLevels(int w, int h) { int m = w > h ? w : h, n = 1; while(m > 1){ m >>= 1; n++; } return n; }
// TXD-15 (error case): MIPMAP bit, fewer levels than the chain D3D creates, and the missing
// ones are not just the < 4×4 tail (which SA itself leaves out)
static bool missingMips(const TxdTex &t)
{
	if(!(t.rasterFormat & 0x8000) || t.mips < 1 || t.mips >= fullChainLevels(t.width, t.height)) return false;
	if(t.compressed && t.mips == 1) return false;
	int lastW = t.width >> (t.mips - 1), lastH = t.height >> (t.mips - 1);
	return (lastW > lastH ? lastW : lastH) > 4;
}

// Stock TXDs store the tail of a mip chain (levels of 4×4 and smaller) with size 0: the game (and librw) creates
// the level and reads nothing into it, so it stays whatever the driver had there — usually zeros, i.e. black.
// In-game the draw distances never reach those levels; a viewer looking from far above samples exactly them
// («модели издалека чёрные»). Each empty level gets the first block / pixel of the level above — one texel of
// roughly the right colour is all a ≤ 4×4 level shows anyway.
int TxdFillEmptyMips(TxdFile &f)
{
	int filled = 0;
	for(size_t i = 0; i < f.tex.size(); i++){
		TxdTex &t = f.tex[i];
		if(!t.ok || t.levels.empty() || t.levels[0].empty()) continue;
		size_t unit = t.compressed ? dxtBlock(t.d3dFormat) : (size_t)(t.depth / 8);
		if(unit == 0) continue;
		for(size_t L = 1; L < t.levels.size(); L++){
			if(!t.levels[L].empty()) continue;
			const std::vector<uint8_t> &prev = t.levels[L - 1];
			if(prev.size() < unit) break;
			size_t want = levelBytes(t, t.width >> L, t.height >> L);
			std::vector<uint8_t> lv; lv.reserve(want);
			while(lv.size() < want) lv.insert(lv.end(), prev.begin(), prev.begin() + unit);
			t.levels[L].swap(lv);
			filled++;
		}
	}
	return filled;
}

bool TxdNeedsFix(const TxdTex &t, const TxdFixOptions &o)
{
	if(!t.ok) return false;
	if(o.toD3D9 && t.platform == 8) return true;
	if(o.unpalettize && (t.rasterFormat & 0x6000)) return true;
	if(o.stripAutoMip && (t.rasterFormat & 0x1000)) return true;
	if(o.fullMips && missingMips(t)) return true;
	if(o.pow2 && (!pow2(t.width) || !pow2(t.height))) return true;
	if(o.fixFilter && ((t.filterAddr & 0xFF) < 1 || (t.filterAddr & 0xFF) > 6)) return true;
	if(o.clearMask && !t.mask.empty()) return true;
	if(o.dxt && !t.compressed && !(t.rasterFormat & 0x6000) && t.width >= 4 && t.height >= 4) return true;
	if(o.maxSide > 0 && (t.width > o.maxSide || t.height > o.maxSide)) return true;
	if(o.fixDxtAlpha && dxtAlphaCandidate(t)) return true;	// candidate only — TxdApplyFixes decodes and checks the alpha
	return false;
}

int TxdApplyFixes(TxdFile &f, const TxdFixOptions &o, std::vector<std::string> &log)
{
	int changed = 0;
	for(size_t i = 0; i < f.tex.size(); i++){
		TxdTex &t = f.tex[i];
		if(!TxdNeedsFix(t, o)) continue;
		std::string what;
		bool needPixels = (o.toD3D9 && t.platform == 8 && (t.rasterFormat & 0x6000) && o.unpalettize)
		               || (o.unpalettize && (t.rasterFormat & 0x6000))
		               || (o.stripAutoMip && (t.rasterFormat & 0x1000))
		               || (o.fullMips && missingMips(t))
		               || (o.pow2 && (!pow2(t.width) || !pow2(t.height)))
		               || (o.dxt && !t.compressed && !(t.rasterFormat & 0x6000) && t.width >= 4 && t.height >= 4)
		               || (o.maxSide > 0 && (t.width > o.maxSide || t.height > o.maxSide));
		if(o.fixFilter && ((t.filterAddr & 0xFF) < 1 || (t.filterAddr & 0xFF) > 6)){ t.filterAddr = (t.filterAddr & 0xFFFFFF00u) | 6; what += T("фильтр→LINEARMIPLINEAR "); }
		if(o.clearMask && !t.mask.empty()){ t.mask.clear(); what += T("маска снята "); }
		// DXT2/3/4/5 → DXT1: only worth it when every decoded pixel is opaque. Nothing else asked for a
		// re-encode, so a texture that does use its alpha is left bit-exact instead of being recompressed.
		std::vector<uint8_t> rgba;
		bool haveRgba = false;
		if(o.fixDxtAlpha && dxtAlphaCandidate(t) && !needPixels && TxdDecode(t, rgba)){
			bool realAlpha = false;
			for(size_t j = 3; j < rgba.size(); j += 4) if(rgba[j] != 255){ realAlpha = true; break; }
			if(realAlpha){
				rgba.clear();
				if(!t.hasAlpha){ t.hasAlpha = true; t.flags |= 1; what += T("флаг alpha "); }	// TXD-20 with a real alpha channel: the flag, not the format, was wrong
			}else{ needPixels = true; haveRgba = true; }
		}
		if(needPixels){
			if(!haveRgba && !TxdDecode(t, rgba)){ log.push_back(fmt(T("%s: формат %s не декодируется — оставлено как есть"), t.name.c_str(), TxdFormatName(t))); }
			else{
				int w = t.width, h = t.height;
				if(o.pow2){ w = nearestPow2(w); h = nearestPow2(h); }
				while(o.maxSide > 0 && (w > o.maxSide || h > o.maxSide) && w > 4 && h > 4){ w /= 2; h /= 2; }
				bool mips = t.mips > 1 || (t.rasterFormat & 0x1000) != 0;
				bool dxt = (o.dxt || t.compressed) && w >= 4 && h >= 4;	// a DXT source stays DXT
				std::string from = fmt("%s %dx%d/%d", TxdFormatName(t), t.width, t.height, t.mips);
				rebuild(t, rgba, t.width, t.height, w, h, mips, dxt);
				what += fmt("%s → %s %dx%d/%d ", from.c_str(), TxdFormatName(t), w, h, t.mips);
			}
		}else if(o.toD3D9 && t.platform == 8){
			t.platform = 9;	// layout only: the pixel data is identical between D3D8 and D3D9 natives
			what += T("D3D8→D3D9 ");
		}
		if(!what.empty()){ changed++; log.push_back(t.name + ": " + what); }
	}
	return changed;
}

bool TxdFromRgba(const std::string &name, const std::vector<uint8_t> &rgba, int w, int h, bool dxt, TxdTex &out)
{
	if(w < 1 || h < 1 || rgba.size() < (size_t)w * (size_t)h * 4) return false;
	TxdTex t;
	t.name = name.substr(0, 31);
	t.platform = 9; t.filterAddr = 0x1106; t.rasterFormat = 0; t.type = 4;
	int pw = nearestPow2(w), ph = nearestPow2(h);
	std::vector<uint8_t> px(rgba.begin(), rgba.begin() + (ptrdiff_t)((size_t)w * (size_t)h * 4));
	rebuild(t, px, w, h, pw, ph, true, dxt && pw >= 4 && ph >= 4);
	out = t;
	return true;
}

bool TxdHasAlphaFormat(const TxdTex &t)
{
	if(t.hasAlpha) return true;
	if(t.compressed) return dxtBlock(t.d3dFormat) == 16;
	return t.d3dFormat == D3DFMT_A8R8G8B8 || t.d3dFormat == D3DFMT_A1R5G5B5 || t.d3dFormat == D3DFMT_A4R4G4B4;
}
bool TxdRebuildTexture(TxdTex &t, int w, int h, int mode, std::string &err) { return TxdRebuildTexture(t, w, h, mode, -1, err); }
bool TxdRebuildTexture(TxdTex &t, int w, int h, int mode, int mipsWant, std::string &err)
{
	std::vector<uint8_t> rgba;
	if(!TxdDecode(t, rgba)){ err = fmt(T("%s: формат %s не декодируется"), t.name.c_str(), TxdFormatName(t)); return false; }
	if(w <= 0) w = t.width; if(h <= 0) h = t.height;
	// mode: 0 keep, 1..5 DXT1..DXT5, 6 = 32-bit uncompressed
	if(mode == 1) for(size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;	// DXT1: no alpha channel
	bool mips = mipsWant < 0 ? (t.mips > 1 || (t.rasterFormat & 0x1000) != 0) : mipsWant != 0;
	bool dxt = mode == 0 ? t.compressed : mode != 6;
	if(dxt && (w < 4 || h < 4)) dxt = false;
	int kind = mode == 0 ? (t.compressed ? (t.d3dFormat == FCC_DXT1 ? 1 : t.d3dFormat == FCC_DXT2 ? 2 : t.d3dFormat == FCC_DXT3 ? 3 : t.d3dFormat == FCC_DXT4 ? 4 : 5) : 0) : mode <= 5 ? mode : 0;
	rebuild(t, rgba, t.width, t.height, w, h, mips, dxt, mode == 1 ? 0 : (mode >= 2 && mode <= 5) ? 1 : -1, kind);
	return true;
}

// ------------------------------------------------------------------- write ---

static void w32(std::vector<uint8_t> &o, uint32_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)(v >> 16)); o.push_back((uint8_t)(v >> 24)); }
static void w16(std::vector<uint8_t> &o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
static void wstr(std::vector<uint8_t> &o, const std::string &s, size_t n) { for(size_t i = 0; i < n; i++) o.push_back(i < s.size() ? (uint8_t)s[i] : 0); }
static void chunk(std::vector<uint8_t> &o, uint32_t id, const std::vector<uint8_t> &body, uint32_t libid) { w32(o, id); w32(o, (uint32_t)body.size()); w32(o, libid); o.insert(o.end(), body.begin(), body.end()); }

bool TxdWrite(const TxdFile &f, std::vector<uint8_t> &out, std::string &err)
{
	std::vector<uint8_t> dict, st;
	w16(st, (uint16_t)f.tex.size()); w16(st, f.deviceId);
	chunk(dict, 0x1, st, f.libid);
	for(size_t i = 0; i < f.tex.size(); i++){
		const TxdTex &t = f.tex[i];
		if(!t.ok){ err = fmt(T("текстура №%d битая (%s) — файл не пересобрать"), (int)i, t.err.c_str()); return false; }
		std::vector<uint8_t> ts, tn;
		w32(ts, t.platform); w32(ts, t.filterAddr); wstr(ts, t.name, 32); wstr(ts, t.mask, 32); w32(ts, t.rasterFormat);
		if(t.platform == 8){
			w32(ts, t.hasAlpha ? 1 : 0);
			w16(ts, (uint16_t)t.width); w16(ts, (uint16_t)t.height); ts.push_back((uint8_t)t.depth); ts.push_back((uint8_t)t.mips); ts.push_back((uint8_t)t.type);
			int comp = t.compressed ? (t.d3dFormat == FCC_DXT1 ? 1 : t.d3dFormat == FCC_DXT2 ? 2 : t.d3dFormat == FCC_DXT3 ? 3 : t.d3dFormat == FCC_DXT4 ? 4 : 5) : 0;
			ts.push_back((uint8_t)comp);
		}else{
			w32(ts, t.d3dFormat);
			w16(ts, (uint16_t)t.width); w16(ts, (uint16_t)t.height); ts.push_back((uint8_t)t.depth); ts.push_back((uint8_t)t.mips); ts.push_back((uint8_t)t.type);
			ts.push_back(t.flags);
		}
		ts.insert(ts.end(), t.palette.begin(), t.palette.end());
		for(size_t L = 0; L < t.levels.size(); L++){ w32(ts, (uint32_t)t.levels[L].size()); ts.insert(ts.end(), t.levels[L].begin(), t.levels[L].end()); }
		chunk(tn, 0x1, ts, f.libid);
		if(!t.extension.empty()) tn.insert(tn.end(), t.extension.begin(), t.extension.end());
		else chunk(tn, 0x3, std::vector<uint8_t>(), f.libid);
		chunk(dict, 0x15, tn, f.libid);
	}
	if(!f.extension.empty()) dict.insert(dict.end(), f.extension.begin(), f.extension.end());
	else chunk(dict, 0x3, std::vector<uint8_t>(), f.libid);
	out.clear();
	chunk(out, 0x16, dict, f.libid);
	return true;
}

} // namespace gc
