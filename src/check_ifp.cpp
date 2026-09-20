// gtacheck — IFP parser (ANPK / ANP2 / ANP3) + the IFP-nn rules from
// E:\RE\addon_check\ifp_path.md (mirrors INU_tools/core/ifp_lint.py).
#include "gtacheck.h"

#include <math.h>
#include <algorithm>

namespace gc {

struct KF { float q[4]; float t[3]; double time; bool hasT; };

struct Seq {
	std::string name;
	int frameType;		// 1..4 (ANP3) ; ANPK: 1 KR00, 2 KRT0, 5 KRTS (scale)
	int boneId;		// -1 = by name
	std::vector<KF> kf;
	bool compressed;
	bool hasTrans;
	bool hasScale;
	Seq() : frameType(0), boneId(-1), compressed(false), hasTrans(false), hasScale(false) {}
};

struct Anim {
	std::string name;
	std::vector<Seq> seqs;
	uint32_t frameDataSize;
	uint32_t flags;
	Anim() : frameDataSize(0), flags(0) {}
};

struct IP {
	Context *ctx;
	const std::string *where;
	std::string obj;
	bool fatal;
	bool cutscene;	// cuts.img animation: played once (no loop) and loaded uncompressed
	void add(Severity s, const char *code, const std::string &msg, const std::string &detail = "")
	{ ctx->add(s, CAT_IFP, code, *where, -1, obj, msg, detail, -1); if(s == SEV_FATAL) fatal = true; }
};

static bool nameNoNul(const uint8_t *p, size_t n)
{
	return memchr(p, 0, n) == nullptr;
}

static void checkSequence(IP &p, const Anim &a, const Seq &s)
{
	const std::string who = a.name + " / " + s.name;
	if(s.kf.empty()){
		// III: CalcTotalTime суммирует по numFrames — пустая секвенция безвредна (ваниль ped.ifp: 11 штук); VC/SA читают кадр −1
		if(!p.ctx->gd->isIII()) p.add(SEV_FATAL, "IFP-01", fmt("%s: 0 ключей — CalcTotalTime читает кадр −1", who.c_str()));
		return;
	}
	if(s.kf.size() > 32767) p.add(SEV_FATAL, "IFP-10", fmt("%s: %d ключей — счётчик int16, максимум 32767", who.c_str(), (int)s.kf.size()));
	// times
	std::vector<double> ticks(s.kf.size());
	for(size_t i = 0; i < s.kf.size(); i++) ticks[i] = s.compressed ? s.kf[i].time : floor(s.kf[i].time * 60.0 + 0.5);
	if(ticks[0] != 0)
		p.add(SEV_INFO, "IFP-06", fmt("%s: первый ключ на %.3f с, а не на 0 — нулевой кадр не конвертируется, его время добавляется на каждом витке цикла", who.c_str(), s.compressed ? s.kf[0].time / 60.0 : s.kf[0].time));
	int equal = 0;
	for(size_t i = 1; i < ticks.size(); i++){
		if(ticks[i] < ticks[i-1]){
			if(p.cutscene) p.add(SEV_INFO, "IFP-07", fmt("%s: время ключей убывает (ключ %d) — для катсцены (без цикла) безвредно, кадр проигрывается скачком (ваниль: bcesa4w и др.)", who.c_str(), (int)i));
			else p.add(SEV_FATAL, "IFP-07", fmt("%s: время ключей убывает (ключ %d) — отрицательная дельта, зацикленная анимация повесит игру", who.c_str(), (int)i));
			return;
		}
		if(ticks[i] == ticks[i-1]) equal++;
	}
	if(ticks.size() >= 2 && ticks.back() == ticks[0]){
		p.add(p.cutscene ? SEV_INFO : SEV_FATAL, "IFP-07", fmt("%s: все %d ключей на одном времени — нулевая длительность, зацикленная анимация повесит игру", who.c_str(), (int)ticks.size()));
		return;
	}
	if(equal) p.add(SEV_INFO, "IFP-08", fmt("%s: %d пар ключей на одном тике — схлопнутся в скачок", who.c_str(), equal));
	double lastAbs = s.compressed ? s.kf.back().time : s.kf.back().time * 60.0 + 0.5;
	if(lastAbs > 32767)
		p.add(SEV_FATAL, "IFP-09", fmt("%s: последний ключ на %.1f с — больше 546 с, тик int16 переполнится", who.c_str(), s.compressed ? s.kf.back().time / 60.0 : s.kf.back().time));
	// values
	int nonUnit = 0, badTrans = 0, badQuat = 0;
	for(size_t i = 0; i < s.kf.size(); i++){
		const KF &k = s.kf[i];
		bool nan = false;
		for(int c = 0; c < 4; c++) if(!(k.q[c] == k.q[c]) || fabsf(k.q[c]) > 1e30f) nan = true;
		if(nan){ p.add(p.cutscene ? SEV_WARN : SEV_FATAL, "IFP-16", fmt("%s: кватернион с NaN/inf — матрица кости станет NaN, меш исчезнет%s", who.c_str(), p.cutscene ? " (ваниль: bcras1 cscopcarla92)" : "")); return; }
		float mag = sqrtf(k.q[0]*k.q[0] + k.q[1]*k.q[1] + k.q[2]*k.q[2] + k.q[3]*k.q[3]);
		if(fabsf(mag - 1.0f) > 5e-3f) nonUnit++;
		for(int c = 0; c < 4; c++) if(fabsf(k.q[c]) >= 8.0f) { badQuat++; break; }
		if(k.hasT){
			for(int c = 0; c < 3; c++) if(!(k.t[c] == k.t[c])){ p.add(SEV_FATAL, "IFP-16", fmt("%s: смещение с NaN", who.c_str())); return; }
			for(int c = 0; c < 3; c++) if(fabsf(k.t[c]) >= 32.0f){ badTrans++; break; }
		}
	}
	if(badQuat) p.add(p.cutscene ? SEV_INFO : SEV_FATAL, "IFP-09", fmt("%s: %d ключей с компонентом кватерниона ≥ 8 — int16/4096 переполнится%s", who.c_str(), badQuat, p.cutscene ? " (катсцена грузится без сжатия — безвредно)" : ""));
	if(badTrans) p.add(p.cutscene ? SEV_INFO : (s.compressed || s.frameType == 0 ? SEV_FATAL : SEV_WARN), "IFP-09", fmt("%s: %d ключей со смещением ≥ 32 — int16/1024 переполнится при %s, кость прыгнет", who.c_str(), badTrans, s.compressed || s.frameType == 0 ? "загрузке" : "пережатии (вытеснение из кэша анимаций)"));
	if(nonUnit) p.add(p.cutscene ? SEV_INFO : SEV_WARN, "IFP-16", fmt("%s: %d ненормированных кватернионов (|q| ≠ 1 ± 0.005) — slerp смешает с неверными весами", who.c_str(), nonUnit));
}

static bool isRootBone(const Seq &s)
{
	if(s.boneId == 0) return true;
	std::string n = lower(trim(s.name));
	return s.boneId == -1 && (n == "root" || n == "normal");
}

static void checkAnims(IP &p, std::vector<Anim> &anims, const std::string &blockName, const std::string &fileStem, bool anp3)
{
	if(blockName.size() > 15)
		p.add(SEV_FATAL, "IFP-11", fmt("имя пакета «%s» длиннее 15 символов — strncpy(16) обрежет его, блок не совпадёт с записью IMG", blockName.c_str()));
	if(!fileStem.empty() && !ieq(blockName, fileStem))
		p.add(SEV_FATAL, "IFP-20", fmt("имя пакета «%s» не совпадает с именем файла «%s» — игра заведёт второй блок, зарегистрированный по IMG останется пустым, AddAnimation упадёт", blockName.c_str(), fileStem.c_str()),
		        "Внутреннее имя блока должно равняться имени файла без расширения.");
	std::map<std::string, std::string> seen;
	for(size_t ai = 0; ai < anims.size(); ai++){
		Anim &a = anims[ai];
		std::string key = lower(a.name);
		if(seen.count(key)) p.add(SEV_WARN, "IFP-18", fmt("анимация «%s» повторяет «%s» (регистр не учитывается) — вторая недостижима", a.name.c_str(), seen[key].c_str()));
		else seen[key] = a.name;
		if(a.name.size() > 23) p.add(SEV_ERROR, "IFP-11", fmt("анимация «%s»: имя длиннее 23 символов — хэш пойдёт по мусору", a.name.c_str()));
		if(a.seqs.empty()){ p.add(SEV_FATAL, "IFP-02", fmt("анимация «%s» без единой кости — new[0] и чтение мимо массива при выгрузке", a.name.c_str())); continue; }
		if(a.seqs.size() > 32767) p.add(SEV_FATAL, "IFP-10", fmt("анимация «%s»: %d костей — счётчик int16", a.name.c_str(), (int)a.seqs.size()));
		bool cls0 = a.seqs[0].compressed;
		for(size_t si = 1; si < a.seqs.size(); si++) if(a.seqs[si].compressed != cls0){
			p.add(SEV_FATAL, "IFP-05", fmt("анимация «%s»: смешаны сжатые и несжатые кости — класс берётся по первой, остальные читаются с неверным шагом", a.name.c_str()));
			break;
		}
		if(anp3){
			uint64_t sum = 0;
			for(size_t si = 0; si < a.seqs.size(); si++){
				int sz = a.seqs[si].frameType == 1 ? 20 : a.seqs[si].frameType == 2 ? 32 : a.seqs[si].frameType == 3 ? 10 : a.seqs[si].frameType == 4 ? 16 : 0;
				sum += (uint64_t)a.seqs[si].kf.size() * sz;
			}
			if(a.frameDataSize < sum) p.add(SEV_FATAL, "IFP-03", fmt("анимация «%s»: frameDataSize %u меньше суммы ключей %llu — запись за выделенный блок (порча кучи)", a.name.c_str(), a.frameDataSize, (unsigned long long)sum));
			else if(a.frameDataSize == 0 && sum) p.add(SEV_WARN, "IFP-03", fmt("анимация «%s»: frameDataSize 0 — Malloc(0), утечка и Free чужого буфера при распаковке", a.name.c_str()));
		}
		std::map<std::string, int> bones;
		std::string transNonRoot, scaleBones;
		for(size_t si = 0; si < a.seqs.size(); si++){
			Seq &s = a.seqs[si];
			std::string bk = s.boneId != -1 ? fmt("id:%d", s.boneId) : "name:" + lower(trim(s.name));
			if(bones.count(bk)) p.add(SEV_WARN, "IFP-18", fmt("анимация «%s»: кость «%s» (id %d) задана дважды — играть будет последняя", a.name.c_str(), s.name.c_str(), s.boneId));
			bones[bk] = 1;
			if(s.name.size() > 23) p.add(SEV_WARN, "IFP-11", fmt("анимация «%s»: имя кости «%s» длиннее 23 символов", a.name.c_str(), s.name.c_str()));
			if(s.boneId != -1 && (s.boneId < -32768 || s.boneId > 32767)) p.add(SEV_ERROR, "IFP-17", fmt("анимация «%s» / «%s»: boneId %d вне int16 — никогда не совпадёт с nodeID", a.name.c_str(), s.name.c_str(), s.boneId));
			checkSequence(p, a, s);
			if(s.hasTrans && !isRootBone(s)) transNonRoot += (transNonRoot.empty() ? "" : ", ") + s.name;
			if(s.hasScale) scaleBones += (scaleBones.empty() ? "" : ", ") + s.name;
		}
		if(!transNonRoot.empty()) p.add(SEV_INFO, "IFP-19", fmt("анимация «%s»: ключи смещения на не-корневых костях (%s) — наличие смещения заменяет bind-позицию кости", a.name.c_str(), transNonRoot.c_str()));
		if(!scaleBones.empty()) p.add(SEV_INFO, "IFP-21", fmt("анимация «%s»: ключи масштаба (%s) — движок читает и выбрасывает", a.name.c_str(), scaleBones.c_str()));
	}
}

static bool parseAnp3(IP &p, Buf &b, bool anp3, std::vector<Anim> &anims, std::string &blockName)
{
	b.skip(8);	// tag + fileSize
	const uint8_t *bn = b.ptr(24);
	if(!bn) return false;
	blockName.assign((const char*)bn, strnlen((const char*)bn, 24));
	if(nameNoNul(bn, 24)) p.add(SEV_ERROR, "IFP-11", "имя пакета без NUL в 24 байтах");
	uint32_t numAnims = b.u32();
	if(numAnims > 100000){ p.add(SEV_FATAL, "IFP-10", fmt("numAnims = %u — мусор", numAnims)); return false; }
	for(uint32_t ai = 0; ai < numAnims; ai++){
		Anim a;
		const uint8_t *an = b.ptr(24);
		if(!an){ p.add(SEV_FATAL, "IFP-19b", fmt("файл обрывается на анимации %u из %u", ai, numAnims)); return false; }
		a.name.assign((const char*)an, strnlen((const char*)an, 24));
		if(nameNoNul(an, 24)) p.add(SEV_ERROR, "IFP-11", fmt("анимация №%u: имя без NUL в 24 байтах — хэш по мусору", ai));
		uint32_t numSeq = b.u32();
		if(anp3){ a.frameDataSize = b.u32(); a.flags = b.u32(); }
		if(numSeq > 100000 || !b.ok){ p.add(SEV_FATAL, "IFP-10", fmt("анимация «%s»: numSequences = %u — мусор (рассинхрон потока?)", a.name.c_str(), numSeq)); anims.push_back(a); return false; }
		for(uint32_t si = 0; si < numSeq; si++){
			Seq s;
			const uint8_t *sn = b.ptr(24);
			if(!sn){ p.add(SEV_FATAL, "IFP-19b", fmt("анимация «%s»: файл обрывается на кости %u", a.name.c_str(), si)); anims.push_back(a); return false; }
			s.name.assign((const char*)sn, strnlen((const char*)sn, 24));
			s.frameType = b.i32();
			uint32_t numFrames = b.u32();
			s.boneId = b.i32();
			if(!b.ok){ anims.push_back(a); return false; }
			if(s.frameType < 1 || s.frameType > 4){
				p.add(SEV_FATAL, "IFP-04", fmt("анимация «%s» / «%s»: frameType %d вне 1..4 — движок не читает ключи, поток рассинхронизируется (дальше мусор)", a.name.c_str(), s.name.c_str(), s.frameType));
				a.seqs.push_back(s);
				anims.push_back(a);
				return false;
			}
			s.compressed = s.frameType >= 3;
			s.hasTrans = s.frameType == 2 || s.frameType == 4;
			int sz = s.frameType == 1 ? 20 : s.frameType == 2 ? 32 : s.frameType == 3 ? 10 : 16;
			if(numFrames > 32767) p.add(SEV_FATAL, "IFP-10", fmt("анимация «%s» / «%s»: %u ключей — счётчик int16", a.name.c_str(), s.name.c_str(), numFrames));
			if((uint64_t)numFrames * sz > b.left()){
				p.add(SEV_FATAL, "IFP-19b", fmt("анимация «%s» / «%s»: %u ключей × %d байт не помещаются в файл — RwStreamRead вернёт меньше, ключи из мусора", a.name.c_str(), s.name.c_str(), numFrames, sz));
				a.seqs.push_back(s); anims.push_back(a); return false;
			}
			s.kf.reserve(numFrames < 100000 ? numFrames : 0);
			for(uint32_t f = 0; f < numFrames; f++){
				KF k; k.hasT = s.hasTrans; k.t[0] = k.t[1] = k.t[2] = 0;
				if(s.compressed){
					for(int c = 0; c < 4; c++) k.q[c] = b.i16() / 4096.0f;
					k.time = (double)b.i16();	// ticks
					if(s.hasTrans) for(int c = 0; c < 3; c++) k.t[c] = b.i16() / 1024.0f;
				}else{
					for(int c = 0; c < 4; c++) k.q[c] = b.f32();
					k.time = (double)b.f32();
					if(s.hasTrans) for(int c = 0; c < 3; c++) k.t[c] = b.f32();
				}
				s.kf.push_back(k);
			}
			a.seqs.push_back(s);
		}
		anims.push_back(a);
	}
	return true;
}

static bool parseAnpk(IP &p, Buf &b, std::vector<Anim> &anims, std::string &blockName)
{
	auto section = [&](const char *expectTag, uint32_t &size, std::string &tag) -> bool {
		const uint8_t *t = b.ptr(4);
		if(!t) return false;
		tag.assign((const char*)t, 4);
		size = b.u32();
		size = (size + 3) & ~3u;
		if(expectTag && tag != expectTag) p.add(SEV_WARN, "IFP-30", fmt("ожидалась секция %s, найдена «%s» — движок тег не проверяет и читает тело как %s", expectTag, tag.c_str(), expectTag));
		return b.ok;
	};
	uint32_t size; std::string tag;
	b.skip(8);	// ANPK hdr
	if(!section("INFO", size, tag)) return false;
	if(size > 260) p.add(SEV_FATAL, "IFP-13", fmt("INFO длиной %u > 260 байт — переполнение стека LoadAnimFile", size));
	const uint8_t *info = b.ptr(size);
	if(!info) return false;
	uint32_t numAnims = size >= 4 ? (uint32_t)(info[0] | (info[1] << 8) | (info[2] << 16) | (info[3] << 24)) : 0;
	blockName.assign((const char*)info + 4, size > 4 ? strnlen((const char*)info + 4, size - 4) : 0);
	if(numAnims > 100000){ p.add(SEV_FATAL, "IFP-10", fmt("numAnims = %u — мусор", numAnims)); return false; }
	for(uint32_t ai = 0; ai < numAnims; ai++){
		Anim a;
		if(!section("NAME", size, tag)) { p.add(SEV_FATAL, "IFP-19b", "файл обрывается"); return false; }
		if(size > 260) p.add(SEV_FATAL, "IFP-13", fmt("NAME длиной %u > 260 байт — переполнение стека", size));
		const uint8_t *nm = b.ptr(size);
		if(!nm) return false;
		a.name.assign((const char*)nm, strnlen((const char*)nm, size));
		if(size && nameNoNul(nm, size)) p.add(SEV_ERROR, "IFP-11", fmt("анимация №%u: NAME без NUL — хэш по мусору", ai));
		if(!section("DGAN", size, tag)) return false;
		if(!section("INFO", size, tag)) return false;
		if(size > 260) p.add(SEV_FATAL, "IFP-13", fmt("INFO анимации «%s» длиной %u > 260", a.name.c_str(), size));
		const uint8_t *in2 = b.ptr(size);
		if(!in2) return false;
		uint32_t numSeq = size >= 4 ? (uint32_t)(in2[0] | (in2[1] << 8) | (in2[2] << 16) | (in2[3] << 24)) : 0;
		if(numSeq > 100000){ p.add(SEV_FATAL, "IFP-10", fmt("анимация «%s»: numSequences = %u", a.name.c_str(), numSeq)); return false; }
		for(uint32_t si = 0; si < numSeq; si++){
			Seq s;
			if(!section("CPAN", size, tag)) return false;
			if(!section("ANIM", size, tag)) return false;
			if(size > 260) p.add(SEV_FATAL, "IFP-13", fmt("ANIM длиной %u > 260", size));
			if(p.ctx->gd->isSA()){
				if(size != 40 && size != 44) p.add(SEV_WARN, "IFP-30", fmt("анимация «%s»: ANIM длиной %u (ожидается 40 или 44 с boneId)", a.name.c_str(), size));
			}else if(size < 32)	// III/VC читают ANIM целиком в buf[256]: имя 28 байт + numFrames по смещению 28 (ваниль пишет 48)
				p.add(SEV_FATAL, "IFP-30", fmt("анимация «%s»: ANIM длиной %u < 32 — numFrames читается за концом секции", a.name.c_str(), size));
			const uint8_t *an = b.ptr(size);
			if(!an) return false;
			s.name.assign((const char*)an, strnlen((const char*)an, size < 28 ? size : 28));
			uint32_t numFrames = size >= 32 ? (uint32_t)(an[28] | (an[29] << 8) | (an[30] << 16) | (an[31] << 24)) : 0;
			if(size == 44) s.boneId = (int32_t)(an[40] | (an[41] << 8) | (an[42] << 16) | (an[43] << 24));
			if(numFrames){
				if(!section(nullptr, size, tag)) return false;
				int stride;
				if(tag == "KR00"){ stride = 20; s.frameType = 1; }
				else if(tag == "KRT0"){ stride = 32; s.frameType = 2; s.hasTrans = true; }
				else if(tag == "KRTS"){ stride = 44; s.frameType = 2; s.hasTrans = true; s.hasScale = true; }
				else{
					p.add(SEV_FATAL, "IFP-30", fmt("анимация «%s» / «%s»: KFRM-тег «%s» не KRTS/KRT0/KR00 — ключи не читаются, поток рассинхронизируется", a.name.c_str(), s.name.c_str(), tag.c_str()));
					a.seqs.push_back(s); anims.push_back(a); return false;
				}
				if((uint64_t)numFrames * stride > b.left()){
					p.add(SEV_FATAL, "IFP-19b", fmt("анимация «%s» / «%s»: %u ключей не помещаются в файл", a.name.c_str(), s.name.c_str(), numFrames));
					a.seqs.push_back(s); anims.push_back(a); return false;
				}
				if(numFrames > 32767) p.add(SEV_FATAL, "IFP-10", fmt("анимация «%s» / «%s»: %u ключей — счётчик int16", a.name.c_str(), s.name.c_str(), numFrames));
				for(uint32_t f = 0; f < numFrames; f++){
					KF k; k.hasT = s.hasTrans; k.t[0] = k.t[1] = k.t[2] = 0;
					for(int c = 0; c < 4; c++) k.q[c] = b.f32();
					if(s.hasTrans) for(int c = 0; c < 3; c++) k.t[c] = b.f32();
					if(s.hasScale) b.skip(12);
					k.time = (double)b.f32();
					s.kf.push_back(k);
				}
				(void)size;
			}
			s.compressed = false;	// engine quantises on load; class is uniform for ANPK
			a.seqs.push_back(s);
		}
		anims.push_back(a);
	}
	return true;
}

void CheckOneIfp(Context &ctx, const std::string &where, const std::vector<uint8_t> &data, const std::string &fileStem, int *outNumAnims, std::vector<std::string> *outAnimNames, bool cutscene)
{
	ctx.rep->countFile(CAT_IFP);
	IP p; p.ctx = &ctx; p.where = &where; p.obj = fileStem; p.fatal = false;
	p.cutscene = cutscene;
	Buf b(data.data(), data.size());
	if(data.size() < 8){ p.add(SEV_FATAL, "IFP-19b", "файл короче 8 байт"); return; }
	std::string tag((const char*)data.data(), 4);
	std::vector<Anim> anims;
	std::string blockName;
	bool anp3 = tag == "ANP3", anp2 = tag == "ANP2";
	bool ok;
	if(anp3 || anp2) ok = parseAnp3(p, b, anp3, anims, blockName);
	else{
		if(tag != "ANPK") p.add(SEV_WARN, "IFP-30", fmt("тег «%s» — не ANPK/ANP2/ANP3; движок разбирает всё прочее как ANPK", tag.c_str()));
		ok = parseAnpk(p, b, anims, blockName);
	}
	if(!ok) p.add(SEV_FATAL, "IFP-19b", "файл обрывается / рассинхронизирован — ключи останутся неинициализированными");
	checkAnims(p, anims, blockName, fileStem, anp3);
	if(ctx.gd->isSA() && !anp3 && !anp2 && !anims.empty())
		p.add(SEV_INFO, "IFP-30", "формат ANPK в SA: кватернионы сопрягаются при загрузке, все ключи квантуются в int16", "");
	if(outNumAnims) *outNumAnims = (int)anims.size();
	if(outAnimNames){ outAnimNames->clear(); for(size_t i = 0; i < anims.size(); i++) outAnimNames->push_back(lower(anims[i].name)); }
}

void CheckIfps(Context &ctx)
{
	GameData &gd = *ctx.gd;
	ctx.prog->set("IFP");
	int totalAnims = 0;
	int blocks = 0;
	std::map<std::string, std::vector<std::string>> animsByBlock;	// block name lower → anim names
	// anim/ped.ifp (always loaded)
	{
		std::string logical = "anim/ped.ifp";
		std::string phys = resolvePath(gd.root, logical);
		if(gd.modloaderActive){
			auto it = gd.looseByName.find("ped.ifp");
			if(it != gd.looseByName.end()) phys = gd.modFiles[(size_t)it->second].phys;
		}
		std::vector<uint8_t> data;
		if(readFile(phys, data)){
			int n = 0; std::vector<std::string> names;
			ctx.prog->step("ped.ifp", 0, 1);
			CheckOneIfp(ctx, logical, data, "ped", &n, &names);
			totalAnims += n; blocks++;
			animsByBlock["ped"] = names;
			gd.ifpAnims["ped"] = names;
		}else
			ctx.add(SEV_FATAL, CAT_IFP, "IFP-31", logical, -1, "", "anim/ped.ifp не найден — CAnimManager::LoadAnimFiles при старте", "");
	}
	int total = 0;
	for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].kind == EK_IFP) total++;
	int done = 0;
	for(size_t i = 0; i < gd.entries.size(); i++){
		if(ctx.cancelled()) return;
		Entry &e = gd.entries[i];
		if(e.kind != EK_IFP || !gd.isWinner((int)i)) continue;
		done++;
		blocks++;
		std::string where = (e.img >= 0 ? basename(gd.archives[e.img].logical) + "/" : "") + e.name;
		ctx.prog->step(e.name.c_str(), done, total);
		bool quiet = ctx.opt.skipVanillaImgs && e.img >= 0 && gd.archives[e.img].vanilla;
		std::vector<uint8_t> data;
		std::string err;
		if(!gd.readEntry((int)i, data, &err)){
			ctx.add(SEV_FATAL, CAT_IMG, "IMG-24", where, e.dirIndex, e.name, "не удалось прочитать запись: " + err, "");
			continue;
		}
		int n = 0; std::vector<std::string> names;
		if(quiet){ Report tmp; Context q = ctx; q.rep = &tmp; CheckOneIfp(q, where, data, e.base, &n, &names); }
		else CheckOneIfp(ctx, where, data, e.base, &n, &names);
		totalAnims += n;
		animsByBlock[e.base] = names;
		gd.ifpAnims[e.base] = names;
	}
	if(!gd.isSA()){
		// re3/reVC config.h: NUMANIMATIONS 250 / 450, NUMANIMBLOCKS 2 / 35; LoadAnimFile пишет в ms_aAnimations[firstIndex + j] без проверки
		int maxAnims = gd.isIII() ? 250 : 450, maxBlocks = gd.isIII() ? 2 : 35;
		if(totalAnims > maxAnims)
			ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IFP-22", "IMG", -1, "", fmt("всего анимаций во всех IFP: %d > %d — ms_aAnimations переполнится (LoadAnimFile пишет дальше массива)", totalAnims, maxAnims), "");
		else if(totalAnims > maxAnims - 8) ctx.add(SEV_WARN, CAT_LIMIT, "IFP-22", "IMG", -1, "", fmt("всего анимаций %d из %d", totalAnims, maxAnims), "");	// ваниль III: 236
		if(blocks > maxBlocks)
			ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IFP-11b", "IMG", -1, "", fmt("%d IFP-блоков > %d — ms_aAnimBlocks переполняется", blocks, maxBlocks), "");
	}
	if(gd.isSA()){
		if(totalAnims > 2500)
			ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IFP-22", "IMG", -1, "", fmt("всего анимаций во всех IFP: %d > 2500 — ms_aAnimations переполнится, анимация №2500 затрёт заголовок блока ped", totalAnims), "");
		else if(totalAnims > 2300) ctx.add(SEV_WARN, CAT_LIMIT, "IFP-22", "IMG", -1, "", fmt("всего анимаций %d из 2500", totalAnims), "");
		if(blocks > 180)
			ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IFP-11b", "IMG", -1, "", fmt("%d IFP-блоков > 180 — ms_aAnimBlocks переполняется, и id стриминга 25575..25754 кончаются", blocks), "");
		// animgrp.dat vs IFP contents
		if(gd.animGroups.size() > 27)
			ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, "IFP-12", "data/animgrp.dat", -1, "", fmt("%d групп в animgrp.dat — при 118 встроенных максимум 27 (145 определений)", (int)gd.animGroups.size()), "");
		for(size_t g = 0; g < gd.animGroups.size(); g++){
			const AnimGroup &ag = gd.animGroups[g];
			auto it = animsByBlock.find(lower(ag.block));
			if(it == animsByBlock.end()){
				ctx.add(SEV_ERROR, CAT_XREF, "IFP-12", "data/animgrp.dat", ag.line, ag.name, fmt("группа «%s» ссылается на блок «%s», которого нет ни в одном IMG (и это не ped)", ag.name.c_str(), ag.block.c_str()), "");
				continue;
			}
			for(size_t k = 0; k < ag.anims.size(); k++){
				if(ag.anims[k].size() > 23) ctx.add(SEV_FATAL, CAT_IFP, "IFP-12", "data/animgrp.dat", ag.line, ag.name, fmt("имя анимации «%s» длиннее 23 символов — переполняет соседний слот имени", ag.anims[k].c_str()), "");
				if(std::find(it->second.begin(), it->second.end(), lower(ag.anims[k])) == it->second.end())
					ctx.add(SEV_FATAL, CAT_XREF, "IFP-12", "data/animgrp.dat", ag.line, ag.name, fmt("группа «%s»: анимации «%s» нет в блоке «%s» — CAnimBlendStaticAssociation::Init(NULL) → краш сразу после загрузки IFP", ag.name.c_str(), ag.anims[k].c_str(), ag.block.c_str()), "");
			}
		}
	}
}

} // namespace gc
