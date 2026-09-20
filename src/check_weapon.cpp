// gtacheck — data/weapon.dat rules WPN-nn.  Source: E:\RE\addon_check\weapon_ped_rules.md §1
// (CWeaponInfo::LoadWeaponData 0x5BE670, gta_sa.exe 1.0 US) + GTACHECK_RULES.md §4.
#include "gtacheck.h"

#include <stdlib.h>
#include <algorithm>

namespace gc {

// FindWeaponType 0x743D10, table 0x8D6150 — _stricmp, unknown → 0 UNARMED.  Index 47 is "".
static const char *WEAPON_NAMES[49] = {
	"UNARMED","BRASSKNUCKLE","GOLFCLUB","NIGHTSTICK","KNIFE","BASEBALLBAT","SHOVEL","POOLCUE","KATANA","CHAINSAW",
	"DILDO1","DILDO2","VIBE1","VIBE2","FLOWERS","CANE","GRENADE","TEARGAS","MOLOTOV","ROCKET","ROCKET_HS",
	"FREEFALL_BOMB","PISTOL","PISTOL_SILENCED","DESERT_EAGLE","SHOTGUN","SAWNOFF","SPAS12","MICRO_UZI","MP5",
	"AK47","M4","TEC9","COUNTRYRIFLE","SNIPERRIFLE","RLAUNCHER","RLAUNCHER_HS","FTHROWER","MINIGUN","SATCHEL_CHARGE",
	"DETONATOR","SPRAYCAN","EXTINGUISHER","CAMERA","NIGHTVISION","INFRARED","PARACHUTE","","ARMOUR",
};

// ms_aAnimAssocDefinitions 0x8AA5A8 — the 118 built-in groups, exact case (WPN-10 compares with strcmp).
static const char *BUILTIN_GROUPS[118] = {
	"default","door","bikes","bikev","bikeh","biked","wayfarer","bmx","mtb","choppa","quad",
	"python","pythonbad","colt45","colt_cop","colt45pro","sawnoff","sawnoffpro","silenced","shotgun","shotgunbad",
	"buddy","buddybad","uzi","uzibad","rifle","riflebad","sniper","grenade","flame","rocket","spraycan",
	"goggles","melee_1","melee_2","melee_3","melee_4","bbbat_1","gclub_1","knife_1","sword_1","dildo_1","flowers_1","csaw_1",
	"kick_std","pistlwhp","medic","beach","sunbathe","playidles","riot","strip","gangs","attractors","player","fat","muscular",
	"playerrocket","playerrocketf","playerrocketm","player2armed","player2armedf","player2armedm","playerBBBat","playerBBBatf","playerBBBatm",
	"playercsaw","playercsawf","playercsawm","playersneak","playerjetpack","swim","drivebys","bike_dbz","cop_dbz","quad_dbz","fat_tired",
	"handsignal","handsignalL","lhand","rhand","carry","carry05","carry105","int_house","int_office","int_shop","stealth_kn",
	"stdcaramims","lowcaramims","trkcaranims","stdbikeanims","sportbikeanims","vespabikeanims","harleybikeanims","dirtbikeanims",
	"wayfbikeanims","bmxbikeanims","mtbbikeanims","choppabikeanims","quadbikeanims","vancaranims","rustplaneanims","coachcaranims",
	"buscaranims","dozercaranims","kartcaranims","convcaranims","mtrkcaranims","traincarranims","stdtallcaramims","hovercaranims",
	"tankcaranims","bfinjcaramims","learplaneanims","harrplaneanims","stdcarupright","nvadaplaneanims",
};

static int findWeaponType(const std::string &name)
{
	for(int i = 0; i < 49; i++) if(i != 47 && ieq(name, WEAPON_NAMES[i])) return i;
	return -1;
}

// index over built-ins + animgrp.dat groups (118..), exact case; -1 = not found
static int findAnimGroup(const GameData &gd, const std::string &name)
{
	for(int i = 0; i < 118; i++) if(name == BUILTIN_GROUPS[i]) return i;
	for(size_t g = 0; g < gd.animGroups.size(); g++) if(name == gd.animGroups[g].name) return 118 + (int)g;
	return -1;
}

static bool isMeleeType(int t) { return (t >= 0 && t <= 15) || t == 46; }
static int ti(const std::string &t) { return (int)strtol(t.c_str(), nullptr, 10); }
static float tf(const std::string &t) { return (float)strtod(t.c_str(), nullptr); }

void CheckWeapons(Context &ctx)
{
	GameData &gd = *ctx.gd;
	if(!gd.isSA()) return;
	ctx.prog->set("weapon.dat");
	const std::string logical = "data/weapon.dat";
	std::vector<DataLine> lines;
	if(!ReadDataLines(gd, logical, lines)){
		ctx.add(SEV_FATAL, CAT_WEAPON, "WPN-01", logical, -1, "", "data/weapon.dat не найден — LoadWeaponData 0x5BE670 не проверяет OpenFile → LoadLine(NULL) → fgets(NULL) → краш при старте", "");
		return;
	}
	ctx.rep->countFile(CAT_WEAPON);

	// WPN-14: hard-coded initial weapons
	{
		struct { int id; const char *what; } need[3] = { {334, "nitestick"}, {345, "missile"}, {346, "colt45"} };
		for(int k = 0; k < 3; k++)
			if(gd.findObj(need[k].id) == nullptr)
				ctx.add(SEV_FATAL, CAT_WEAPON, "WPN-14", "IDE", -1, need[k].what, fmt("модель id %d (%s в ванильном default.ide, секция weap) не определена ни в одном IDE — CStreaming::LoadInitialWeapons 0x40A120 запрашивает ровно ids 334/345/346, RequestModel 0x4087E0 разыменует ms_modelInfoPtrs[id] без NULL → краш при старте", need[k].id, need[k].what), "", need[k].id);
	}

	bool ended = false;
	std::map<std::pair<int,int>, int> dollarRows;	// (type, skill) → line
	std::map<int, int> poundRows;			// type → line
	std::set<int> skill1Types;
	std::map<int, std::set<int>> skillsPerType;
	int prevRow = -1;

	auto tokLenCheck = [&](const DataLine &l, const std::string &name, const std::string &tok, int cap, const char *field, const char *stackNote) {
		if((int)tok.size() >= cap)
			ctx.add(SEV_FATAL, CAT_WEAPON, "WPN-03", logical, l.number, name, fmt("%s «%.40s…» длиной %d ≥ %d — sscanf %%s без ширины пишет за буфер на стеке (%s) → адрес возврата затёрт → краш при старте", field, tok.c_str(), (int)tok.size(), cap, stackNote), "");
		else if((int)tok.size() >= 32)
			ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-03", logical, l.number, name, fmt("%s «%.40s…» длиной %d ≥ 32 — переполняет 32-байтный буфер, затирает соседние локалы (имя/группу)", field, tok.c_str(), (int)tok.size()), "");
	};

	for(size_t i = 0; i < lines.size(); i++){
		const DataLine &l = lines[i];
		if(l.norm.empty() || l.norm[0] == '#') continue;
		const std::vector<std::string> &t = l.tok;
		unsigned char pfx = (unsigned char)l.norm[0];
		if(ended){
			ctx.add(SEV_INFO, CAT_WEAPON, "WPN-20", logical, l.number, "", "строка после ENDWEAPONDATA — не читается", "");
			continue;
		}
		if(pfx != '$' && pfx != 0xA3 && pfx != '%'){
			if(t[0].compare(0, 13, "ENDWEAPONDATA") == 0){ ended = true; continue; }
			if(pfx == 0xC2 && l.norm.size() > 1 && (unsigned char)l.norm[1] == 0xA3)
				ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-20", logical, l.number, "", "строка начинается с «£» в UTF-8 (C2 A3) — движок ждёт один байт 0xA3 (cp1252); строка молча игнорируется, melee-оружие остаётся без строки", "Сохрани weapon.dat в ANSI/cp1252, не в UTF-8.");
			else
				ctx.add(SEV_INFO, CAT_WEAPON, "WPN-20", logical, l.number, "", fmt("строка с неизвестным первым байтом 0x%02X — молча игнорируется", pfx), "");
			continue;
		}
		if(t[0].size() >= 8)
			ctx.add(SEV_FATAL, CAT_WEAPON, "WPN-03", logical, l.number, "", fmt("префиксный токен «%s» длиной %d ≥ 8 — 8-байтный буфер префикса на стеке переполнен", t[0].c_str(), (int)t[0].size()), "");
		// ---------------------------------------------------------------- % aim offsets
		if(pfx == '%'){
			if(t.size() < 10) ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-02", logical, l.number, t.size() > 1 ? t[1] : "", fmt("%%-строка: %d полей вместо 10 — недостающие смещения прицела = мусор со стека", (int)t.size()), "");
			if(t.size() < 2) continue;
			const std::string &grp = t[1];
			tokLenCheck(l, grp, grp, 128, "animGroup", "+0xA0[32] … EBP/ret");
			int gi = findAnimGroup(gd, grp);
			if(gi < 11 || gi > 31){
				int n = 118 + (int)gd.animGroups.size();
				int e = (gi < 0 ? n : gi) - 11;	// aGunAimingOffsets entry actually written
				std::string where = gi < 0 ? fmt("группа не найдена → индекс = число определений (%d) − 11 = %d → запись внутрь aWeaponInfo (оружие №%d, байт +0x%X)", n, e, (e - 21) * 0x18 / 0x70, (e - 21) * 0x18 % 0x70)
				                  : gi < 11 ? fmt("индекс %d − 11 < 0 → запись перед aGunAimingOffsets: gpCrossHairTex / gCrossHair 0xC8A810..0xC8A838 (указатели текстур прицела → краш при отрисовке)", gi)
				                            : fmt("индекс %d − 11 = %d ≥ 21 → запись внутрь aWeaponInfo (оружие №%d)", gi, e, (e - 21) * 0x18 / 0x70);
				ctx.add(gi >= 0 && gi < 11 ? SEV_FATAL : SEV_ERROR, CAT_WEAPON, "WPN-11", logical, l.number, grp, fmt("%%-строка для группы «%s» не из 21 стрелковой (python … spraycan, индексы 11..31) — 0x5BED14 пишет 24 байта по aGunAimingOffsets[индекс−11] без границ: %s", grp.c_str(), where.c_str()),
				        "Смещения прицела бывают только у 21 группы: python pythonbad colt45 colt_cop colt45pro sawnoff sawnoffpro silenced shotgun shotgunbad buddy buddybad uzi uzibad rifle riflebad sniper grenade flame rocket spraycan.");
			}
			for(size_t k = 2; k < t.size() && k < 10; k++){
				bool ok = k < 6 ? IsNumToken(t[k]) : IsIntToken(t[k]);
				if(!ok){ ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-02", logical, l.number, grp, fmt("%%-строка: поле %d «%s» не разбирается (sscanf останавливается; остальное — мусор)", (int)k, t[k].c_str()), ""); break; }
			}
			continue;
		}
		// ---------------------------------------------------------------- $ / £ rows
		bool gun = pfx == '$';
		int expect = gun ? 26 : 12;
		if((int)t.size() < expect){
			ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-02", logical, l.number, t.size() > 1 ? t[1] : "", fmt("%s-строка: %d полей вместо %d — недостающие поля (slot/skill/modelId/…) = мусор со стека", gun ? "$" : "£", (int)t.size(), expect), "");
		}
		if(t.size() < 2) continue;
		const std::string &name = t[1];
		int type = findWeaponType(name);
		tokLenCheck(l, name, name, 96, "имя оружия", "+0xC0[32] … EBP/ret");
		if(type < 0)
			ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-04", logical, l.number, name, fmt("имя «%s» не из 49 встроенных (FindWeaponType 0x743D10, без регистра) — вернёт 0: строка записывается поверх UNARMED", name.c_str()),
			        "Своих типов оружия нет; используй одно из имён UNARMED … PARACHUTE, ARMOUR.");
		if(t.size() > 2){
			const std::string &ft = t[2];
			tokLenCheck(l, name, ft, 64, "fireType", "+0xE0[32] … EBP/ret");
			static const char *FT[] = { "MELEE", "INSTANT_HIT", "PROJECTILE", "AREA_EFFECT", "CAMERA", "USE", nullptr };
			bool ok = false; for(int k = 0; FT[k]; k++) if(ft == FT[k]) ok = true;
			if(!ok) ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-05", logical, l.number, name, fmt("fireType «%s» не MELEE/INSTANT_HIT/PROJECTILE/AREA_EFFECT/CAMERA/USE (точно, с регистром) — FindWeaponFireType 0x5BCF30 вернёт INSTANT_HIT: оружие стреляет пулями", ft.c_str()), "");
		}
		// numeric fields
		{
			// $: 3f 4f 5d 6d 7d 8s 9d 10d 11f 12f 13f 14d 15d 16f 17f 18d 19d 20d 21d 22d 23d 24d 25x 26f 27f 28f 29f
			// £: 3f 4f 5d 6d 7d 8s 9d 10x 11s
			const char *fmtD = "ssffdddsddfffddffdddddddxffff";	// index 1..29 for $ (name fireType tR wR m1 m2 slot grp ammo dmg x y z skill stat acc move 6×loop breakout flags 4×float)
			const char *fmtP = "ssffdddsdxs";			// index 1..11 for £
			const char *f = gun ? fmtD : fmtP;
			size_t nf = strlen(f);
			for(size_t k = 1; k < t.size() && k - 1 < nf; k++){
				char c = f[k - 1];
				bool ok = c == 's' ? true : c == 'd' ? IsIntToken(t[k]) : c == 'f' ? IsNumToken(t[k]) : IsHexToken(t[k]);
				if(!ok){
					if(c == 'x') ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-18", logical, l.number, name, fmt("flags «%s» не hex — %%x останавливается на первом не-hex символе: флаги частичные, дальнейшие поля не разобраны", t[k].c_str()), "");
					else ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-02", logical, l.number, name, fmt("поле %d «%s» не %s — sscanf останавливается, остальные поля = мусор со стека", (int)k, t[k].c_str(), c == 'd' ? "целое" : "число"), "");
					break;
				}
			}
		}
		int modelId1 = t.size() > 5 && IsIntToken(t[5]) ? ti(t[5]) : -1;
		int modelId2 = t.size() > 6 && IsIntToken(t[6]) ? ti(t[6]) : -1;
		int slot = t.size() > 7 && IsIntToken(t[7]) ? ti(t[7]) : 0;
		int skill = 1;
		if(gun && t.size() > 14 && IsIntToken(t[14])) skill = ti(t[14]);
		bool skillType = type >= 22 && type <= 32;
		if(!skillType) skill = 1;	// the loader overwrites it
		if(!gun) skill = 1;
		// WPN-06
		int row = -1;
		if(gun && skillType && (skill < 0 || skill > 3)){
			ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-06", logical, l.number, name, fmt("skill %d вне 0..3 для типа %s — switch 0x5BE968 не сработает, индекс строки берётся из предыдущей (%s): переписывается чужая запись", skill, name.c_str(), prevRow >= 0 ? fmt("строка %d", prevRow).c_str() : "неинициализирован на первой строке"), "");
			row = prevRow;
		}else if(type >= 0){
			row = skillType ? (skill == 0 ? type + 25 : skill == 2 ? type + 36 : skill == 3 ? type + 47 : type) : type;
		}
		prevRow = row;
		// WPN-09
		if(slot < 0 || slot > 12) ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-09", logical, l.number, name, fmt("slot %d вне 0..12 — CPed::GiveWeapon 0x5E6080: ped+0x5A0+slot*0x1C без границ (m_aWeapons[13]) → порча CPed", slot), "");
		// WPN-07 / WPN-08
		bool storesModel = !gun || (skill == 1 && type != 40);
		for(int m = 0; m < 2; m++){
			int id = m == 0 ? modelId1 : modelId2;
			if(id < 0) continue;
			const ObjDef *d = gd.findObj(id);
			if(id >= 20000)
				ctx.add(SEV_FATAL, CAT_WEAPON, "WPN-07", logical, l.number, name, fmt("modelId%d = %d ≥ 20000 — не id модели (TXD/COL/IPL-слоты стриминга); RequestModel 0x4087E0 читает ms_modelInfoPtrs за концом массива → краш", m + 1, id), "");
			else if(d == nullptr)
				ctx.add(SEV_FATAL, CAT_WEAPON, "WPN-07", logical, l.number, name, fmt("modelId%d = %d не определён ни в одном IDE — %s", m + 1, id,
				        (m == 0 && storesModel) ? "загрузчик пишет ms_modelInfoPtrs[id]+0x24 (0x5BEC49) без NULL-проверки → краш при старте"
				                                 : "CPed::AddWeaponModel → RequestModel 0x4087E0 разыменует ms_modelInfoPtrs[id]+0xA (0x40890A) без NULL → краш при выдаче оружия"), "", id);
			else if(m == 0 && storesModel && d->type != OT_WEAP){
				bool crash = d->type == OT_TOBJ || (d->type == OT_OBJS && (d->flags & 0x1000));
				ctx.add(crash ? SEV_FATAL : SEV_ERROR, CAT_WEAPON, "WPN-08", logical, l.number, name,
				        fmt("modelId1 = %d («%s») определён секцией %s, не weap — запись weaponType по +0x24 (CWeaponModelInfo::m_weaponInfo) уходит за конец modelinfo размером %s: %s", id, d->name.c_str(), objTypeName(d->type),
				            d->type == OT_OBJS ? "0x20" : "0x24",
				            crash ? "затирает vtable следующего элемента хранилища → краш" : d->type == OT_OBJS ? "затирает m_nKey следующего objs-элемента (модель перестаёт находиться по имени)" : d->type == OT_PEDS ? "затирает anim group педа" : "затирает clump-info машины"), "", id);
			}
		}
		// WPN-10 / WPN-13 anim group
		if(gun && t.size() > 8){
			const std::string &g = t[8];
			tokLenCheck(l, name, g, 128, "animGroup", "+0xA0[32] … EBP/ret");
			if(g.compare(0, 4, "null") != 0){
				int gi = findAnimGroup(gd, g);
				if(gi < 0) ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-10", logical, l.number, name, fmt("animGroup «%s» не «null» и не точное имя из 118 встроенных групп / animgrp.dat (strcmp с регистром) — группа = 0 «default»: нет анимаций стрельбы и перезарядки", g.c_str()), "");
				else if(gi < 11 || gi > 31) ctx.add(SEV_INFO, CAT_WEAPON, "WPN-13", logical, l.number, name, fmt("animGroup «%s» (индекс %d) не в 11..31 — aimOffsetIndex остаётся 0 (смещения python); в ванили так у goggles", g.c_str(), gi), "");
			}
		}
		if(!gun && t.size() > 11){
			const std::string &g = t[11];
			tokLenCheck(l, name, g, 128, "stealthAnimGroup", "+0xA0[32] … EBP/ret");
			if(g.compare(0, 4, "null") != 0 && findAnimGroup(gd, g) < 0)
				ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-10", logical, l.number, name, fmt("stealthAnimGroup «%s» не «null» и не точное имя группы — группа = 0 «default»", g.c_str()), "");
		}
		// WPN-12 base combo
		if(!gun && t.size() > 8){
			const std::string &c = t[8];
			tokLenCheck(l, name, c, 32, "baseCombo", "+0x100[32] — сразу за ним локалы и EBP/ret");
			static const char *CB[] = { "UNARMED", "BBALLBAT", "KNIFE", "GOLFCLUB", "SWORD", "CHAINSAW", "DILDO", "FLOWERS", nullptr };
			bool ok = false; for(int k = 0; CB[k]; k++) if(c == CB[k]) ok = true;
			if(!ok) ctx.add(SEV_WARN, CAT_WEAPON, "WPN-12", logical, l.number, name, fmt("baseCombo «%s» не UNARMED/BBALLBAT/KNIFE/GOLFCLUB/SWORD/CHAINSAW/DILDO/FLOWERS (точно) — 0x61DB30 вернёт 4 (комбо UNARMED)", c.c_str()), "");
		}
		// WPN-17 ranges
		if(gun){
			if(t.size() > 9 && IsIntToken(t[9])){ long v = strtol(t[9].c_str(), nullptr, 10); if(v < -32768 || v > 32767) ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-17", logical, l.number, name, fmt("ammoClip %ld вне int16 — усекается при записи", v), ""); else if(v < 0) ctx.add(SEV_INFO, CAT_WEAPON, "WPN-18", logical, l.number, name, fmt("ammoClip %ld отрицательный", v), ""); }
			if(t.size() > 10 && IsIntToken(t[10])){ long v = strtol(t[10].c_str(), nullptr, 10); if(v < -32768 || v > 32767) ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-17", logical, l.number, name, fmt("damage %ld вне int16 — усекается при записи", v), ""); }
			// WPN-21
			if(t.size() > 4 && IsNumToken(t[3]) && IsNumToken(t[4]) && t.size() > 2 && t[2] != "MELEE" && t[2] != "CAMERA" && t[2] != "USE"){
				if(tf(t[3]) <= 0 || tf(t[4]) <= 0) ctx.add(SEV_WARN, CAT_WEAPON, "WPN-21", logical, l.number, name, fmt("targetRange %s / weaponRange %s ≤ 0 для %s", t[3].c_str(), t[4].c_str(), t[2].c_str()), "");
			}
			if(t.size() > 17 && IsNumToken(t[16]) && IsNumToken(t[17]) && (tf(t[16]) <= 0 || tf(t[17]) <= 0))
				ctx.add(SEV_WARN, CAT_WEAPON, "WPN-21", logical, l.number, name, fmt("accuracy %s / moveSpeed %s ≤ 0 — множители в рантайме (заголовок файла: 0.5..2.0 / 0.5..1.5)", t[16].c_str(), t[17].c_str()), "");
			for(size_t k = 18; k < t.size() && k <= 23; k++) if(IsIntToken(t[k]) && ti(t[k]) < 0){ ctx.add(SEV_INFO, CAT_WEAPON, "WPN-18", logical, l.number, name, fmt("кадр анимации (поле %d) %s отрицательный", (int)k, t[k].c_str()), ""); break; }
		}else{
			if(t.size() > 9 && IsIntToken(t[9])){ long v = strtol(t[9].c_str(), nullptr, 10); if(v < 0 || v > 255) ctx.add(SEV_ERROR, CAT_WEAPON, "WPN-17", logical, l.number, name, fmt("numCombos %ld вне 0..255 — хранится байтом", v), ""); }
		}
		// WPN-19 bookkeeping
		if(type >= 0){
			if(gun){
				if(isMeleeType(type)) ctx.add(SEV_INFO, CAT_WEAPON, "WPN-19", logical, l.number, name, fmt("$-строка для melee-типа %s — skill принудительно 1, комбо остаются по умолчанию", name.c_str()), "");
				std::pair<int,int> key(type, skillType ? skill : 1);
				if(dollarRows.count(key)) ctx.add(SEV_INFO, CAT_WEAPON, "WPN-19", logical, l.number, name, fmt("повторная $-строка для %s skill %d (первая — строка %d) — побеждает последняя", name.c_str(), key.second, dollarRows[key]), "");
				dollarRows[key] = l.number;
				if(key.second == 1) skill1Types.insert(type);
				if(skillType) skillsPerType[type].insert(key.second);
			}else{
				if(!isMeleeType(type)) ctx.add(SEV_INFO, CAT_WEAPON, "WPN-19", logical, l.number, name, fmt("£-строка для стрелкового типа %s", name.c_str()), "");
				if(poundRows.count(type)) ctx.add(SEV_INFO, CAT_WEAPON, "WPN-19", logical, l.number, name, fmt("повторная £-строка для %s (первая — строка %d) — побеждает последняя", name.c_str(), poundRows[type]), "");
				poundRows[type] = l.number;
			}
		}
	}
	// WPN-15 / WPN-16
	{
		struct { int type; const char *n; } gang[4] = { {22, "PISTOL"}, {24, "DESERT_EAGLE"}, {28, "MICRO_UZI"}, {30, "AK47"} };
		for(int k = 0; k < 4; k++)
			if(!skill1Types.count(gang[k].type))
				ctx.add(SEV_WARN, CAT_WEAPON, "WPN-15", logical, -1, gang[k].n, fmt("нет $-строки со skill 1 для %s — CGangs::Initialise 0x5DE680 зашивает этот тип как оружие банд; остаются defaults (MELEE, модель −1): бандиты «стреляют» невидимым melee", gang[k].n), "");
		for(int type = 22; type <= 32; type++){
			if(!dollarRows.count(std::make_pair(type, 1)) && !skillsPerType.count(type)) continue;	// type absent entirely — WPN-15 covers the gang ones
			for(int sk = 0; sk <= 2; sk += 2)
				if(!skillsPerType[type].count(sk))
					ctx.add(SEV_INFO, CAT_WEAPON, "WPN-16", logical, -1, WEAPON_NAMES[type], fmt("нет $-строки со skill %d для %s — GetWeaponInfo 0x743C60 вернёт пустую запись (MELEE, модель −1) для педов с этим уровнем", sk, WEAPON_NAMES[type]), "");
		}
	}
	if(!ended) ctx.add(SEV_INFO, CAT_WEAPON, "WPN-20", logical, -1, "", "нет строки ENDWEAPONDATA — читается до конца файла", "");
}

} // namespace gc
