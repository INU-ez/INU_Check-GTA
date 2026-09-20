// gtacheck — phase orchestration + cross-reference checks that need every pass.
#include "gtacheck.h"

#include <math.h>

namespace gc {

static void limit(Context &ctx, int value, int cap, const char *code, const std::string &file, const std::string &what, const std::string &consequence)
{
	if(value > cap)
		ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_FATAL, CAT_LIMIT, code, file, -1, "",
		        fmt("%s: %d > %d — %s", what.c_str(), value, cap, consequence.c_str()),
		        ctx.opt.limitAdjuster ? "Лимит-аджастер включён — убедись, что этот лимит в нём поднят." : "Стандартный exe: массив фиксированного размера без проверки.");
	else if(value > cap * 0.9)
		ctx.add(SEV_INFO, CAT_LIMIT, code, file, -1, "", fmt("%s: %d из %d (запас меньше 10%%)", what.c_str(), value, cap), "");
}

void CheckCrossRefs(Context &ctx)
{
	GameData &gd = *ctx.gd;
	ctx.prog->set("Перекрёстные проверки");
	bool sa = gd.isSA();

	// model store capacities (DAT-07 / DFF-74)
	if(sa){
		int atomic = 0, damage = 0;
		for(size_t i = 0; i < gd.objs.size(); i++) if(gd.objs[i].type == OT_OBJS){ if(gd.objs[i].flags & 0x1000) damage++; else atomic++; }
		limit(ctx, atomic, 14000, "DAT-07", "IDE", "objs-моделей", "хранилище CAtomicModelInfo переполнено, следующее хранилище затирается");
		limit(ctx, damage, 70, "DAT-07", "IDE", "разрушаемых моделей (флаг 0x1000)", "хранилище CDamageAtomicModelInfo на 70");
		limit(ctx, gd.numDefs[OT_TOBJ], 169, "DAT-07", "IDE", "tobj-моделей", "хранилище CTimeModelInfo на 169");
		limit(ctx, gd.numDefs[OT_ANIM] + gd.numDefs[OT_HIER], 92, "DAT-07", "IDE", "anim+hier-моделей", "хранилище CClumpModelInfo на 92");
		limit(ctx, gd.numDefs[OT_CARS], 212, "DAT-07", "IDE", "машин", "хранилище CVehicleModelInfo на 212");
		limit(ctx, gd.numDefs[OT_PEDS], 278, "DAT-07", "IDE", "педов", "хранилище CPedModelInfo на 278");
		limit(ctx, gd.numDefs[OT_WEAP], 51, "DAT-07", "IDE", "оружия", "хранилище CWeaponModelInfo на 51");
		// IPL section capacities summed over all text IPLs
		limit(ctx, gd.totalNaviZones, 380, "DAT-27", "IPL", "навигационных зон (тип 0/1)", "381-я затирает m_CurrLevel");
		limit(ctx, gd.totalMapZones, 39, "DAT-27", "IPL", "map-зон (тип 3)", "40-я затирает счётчик zone info");
		limit(ctx, gd.totalCull, 1300, "DAT-28", "IPL", "cull-зон", "массив CAttributeZone на 1300");
		limit(ctx, gd.totalTunnel, 40, "DAT-28", "IPL", "туннельных cull-зон (0x80/0x800)", "массив на 40");
		limit(ctx, gd.totalMirror, 72, "DAT-28", "IPL", "зеркальных cull-зон", "массив на 72");
		if(gd.totalOccl > 1000) ctx.add(SEV_WARN, CAT_LIMIT, "DAT-29", "IPL", -1, "", fmt("окклюдеров %d > 1000 — лишние молча отброшены", gd.totalOccl), "");
		if(gd.totalOcclInt > 40) ctx.add(SEV_WARN, CAT_LIMIT, "DAT-29", "IPL", -1, "", fmt("интерьерных окклюдеров %d > 40 — лишние молча отброшены", gd.totalOcclInt), "");
		limit(ctx, gd.totalGrge, 50, "DAT-31", "IPL", "гаражей", "aGarages на 50 без проверки");
		if(gd.totalEnex > 400) ctx.add(ctx.opt.limitAdjuster ? SEV_INFO : SEV_ERROR, CAT_LIMIT, "DAT-30", "IPL", -1, "", fmt("enex %d > 400 — пул полон, флаги лишних входов OR-ятся в запись 0", gd.totalEnex), "");
		if(gd.totalCarGen > 500) ctx.add(SEV_WARN, CAT_LIMIT, "DAT-34", "IPL", -1, "", fmt("генераторов машин %d > 500 — лишние отброшены", gd.totalCarGen), "");
		limit(ctx, gd.totalTcyc, 20, "DAT-35", "IPL", "tcyc-боксов", "m_aBoxes на 20 без проверки");
		limit(ctx, gd.totalAuzoBox, 158, "DAT-36", "IPL", "audio-боксов", "таблица на 158");
		limit(ctx, gd.totalAuzoSphere, 3, "DAT-36", "IPL", "audio-сфер", "таблица на 3");
	}

	if(!sa){
		// re3 / reVC config.h: хранилища CModelInfo (CStore::alloc пишет за массив, когда allocPtr >= size) и массивы зон
		bool vc = gd.isVC();
		int simple = 0;
		for(size_t i = 0; i < gd.objs.size(); i++) if(gd.objs[i].type == OT_OBJS) simple++;
		limit(ctx, simple, vc ? 3885 : 5000, "DAT-07", "IDE", "objs-моделей", vc ? "хранилище CSimpleModelInfo на 3885 (VC)" : "хранилище CSimpleModelInfo на 5000 (III)");
		limit(ctx, gd.numDefs[OT_TOBJ], vc ? 385 : 30, "DAT-07", "IDE", "tobj-моделей", vc ? "хранилище CTimeModelInfo на 385 (VC)" : "хранилище CTimeModelInfo на 30 (III)");
		limit(ctx, gd.numDefs[OT_ANIM] + gd.numDefs[OT_HIER], 5, "DAT-07", "IDE", "hier-моделей", "хранилище CClumpModelInfo на 5");
		limit(ctx, gd.numDefs[OT_CARS], vc ? 110 : 120, "DAT-07", "IDE", "машин", vc ? "хранилище CVehicleModelInfo на 110 (VC)" : "хранилище CVehicleModelInfo на 120 (III)");
		limit(ctx, gd.numDefs[OT_PEDS], vc ? 130 : 90, "DAT-07", "IDE", "педов", vc ? "хранилище CPedModelInfo на 130 (VC)" : "хранилище CPedModelInfo на 90 (III)");
		if(vc) limit(ctx, gd.numDefs[OT_WEAP], 37, "DAT-07", "IDE", "оружия (weap)", "хранилище CWeaponModelInfo на 37");
		limit(ctx, gd.total2dfx, vc ? 1210 : 2000, "DAT-16", "IDE", "записей 2dfx", vc ? "хранилище C2dEffect на 1210 (VC)" : "хранилище C2dEffect на 2000 (III)");
		int txdSlots = (int)gd.txdSlots.size() + 2;	// + generic, particle
		limit(ctx, txdSlots, vc ? 1385 : 850, "TXD-27", "IDE/IMG", "слотов TXD", vc ? "пул CTxdStore на 1385 (VC) — CPool::New вернёт NULL, AddTxdSlot разыменует его" : "пул CTxdStore на 850 (III) — CPool::New вернёт NULL, AddTxdSlot разыменует его");
		if(vc){
			int colSlots = 1;	// generic
			for(size_t i = 0; i < gd.entries.size(); i++) if(gd.entries[i].kind == EK_COL && gd.entries[i].img >= 0 && gd.isWinner((int)i)) colSlots++;
			limit(ctx, colSlots, 31, "COL-26", "IMG", "стримовых .col", "пул CColStore на 31 (VC)");
			limit(ctx, gd.totalNaviZones, 20, "DAT-27", "IPL", "зон типа 0/1 (навигация)", "NavigationZoneArray на 20 (VC)");
			limit(ctx, gd.totalInfoZones, 169, "DAT-27", "IPL", "зон типа 2 (info)", "InfoZoneArray на 169 (VC) — ваниль 165");
			limit(ctx, gd.totalMapZones, 39, "DAT-27", "IPL", "map-зон (тип 3)", "MapZoneArray на 39 (VC)");
		}else{
			limit(ctx, gd.totalNaviZones + gd.totalInfoZones, 50, "DAT-27", "IPL", "зон (типы 0/1/2)", "ZoneArray на 50 (III)");
			limit(ctx, gd.totalMapZones, 25, "DAT-27", "IPL", "map-зон (тип 3)", "MapZoneArray на 25 (III)");
		}
	}

	// per model
	int noDff = 0, noTxd = 0, noCol = 0, orphans = 0;
	for(size_t i = 0; i < gd.objs.size(); i++){
		if(ctx.cancelled()) return;
		ObjDef &o = gd.objs[i];
		bool placed = o.numInstances > 0;
		bool fromModelFile = !sa && gd.modelFileClumps.count(lower(o.name)) != 0;	// wheels.dff / weapons.dff / air_vlo.dff (MODELFILE)
		// III/VC: special01..21 / cutobj01..05 / null — заглушки, в которые RequestSpecialModel подставляет другой DFF; сами не грузятся
		std::string ln = lower(o.name);
		bool placeholder = !sa && ((ln.size() == 9 && ln.compare(0, 7, "special") == 0 && isdigit((unsigned char)ln[7]) && isdigit((unsigned char)ln[8]))
		                        || (ln.size() == 8 && ln.compare(0, 6, "cutobj") == 0 && isdigit((unsigned char)ln[6]) && isdigit((unsigned char)ln[7]))
		                        || ln == "null");
		if(o.dffEntry < 0 && !fromModelFile && !placeholder){
			noDff++;
			if(noDff <= 300)
				ctx.add(placed ? SEV_ERROR : SEV_WARN, CAT_XREF, "DFF-71", o.file, o.line, o.name,
				        fmt("DFF «%s.dff» нет ни в одном IMG (и нет loose-файла) — модель никогда не загрузится%s", o.name.c_str(), placed ? fmt(", а она размещена %d раз", o.numInstances).c_str() : ""),
				        "Проверь имя в IMG (регистр не важен, но имя ≤ 20 символов) и что IMG перечислен в gta.dat.", o.id);
		}
		if(o.txdSlot >= 0){
			const TxdSlot &ts = gd.txdSlots[(size_t)o.txdSlot];
			bool loaded = ts.entry >= 0 || gd.txdTextures.count(ts.name) || ts.name == "generic";
			if(!loaded){
				noTxd++;
				if(noTxd <= 300)
					ctx.add(placed || o.type != OT_OBJS ? SEV_FATAL : SEV_ERROR, CAT_XREF, "DFF-71", o.file, o.line, o.name,
					        fmt("TXD «%s.txd» нет ни в одном IMG — ConvertBufferToObject будет запрашивать его вечно, модель не появится", o.txd.c_str()), "", o.id);
			}
		}
		if(sa && placed && o.colEntry < 0 && (o.type == OT_OBJS || o.type == OT_TOBJ || o.type == OT_ANIM || o.type == OT_HIER)){
			if(!o.isLod){
				noCol++;
				bool lodName = lower(o.name).compare(0, 3, "lod") == 0;	// named as a LOD, but no inst line points at it — the game treats it as an ordinary object
				if(noCol <= 300){
					if(lodName)
						ctx.add(SEV_FATAL, CAT_XREF, "COL-27", o.file, o.line, o.name,
						        fmt("«%s» — LOD-модель, но ни одна строка IPL не ссылается на неё как на LOD (поле lod). Игра считает её обычным объектом, а у неё нет COL → краш при загрузке зоны", o.name.c_str()),
						        fmt("Свяжи копии «%s» с этим LOD: строка модели в IPL → кнопка «Связать все копии с LOD». LOD с привязанной моделью COL не нужен.", o.name.size() > 3 ? o.name.substr(3).c_str() : o.name.c_str()), o.id);
					else
						ctx.add(SEV_FATAL, CAT_XREF, "COL-27", o.file, o.line, o.name,
						        fmt("«%s» расставлена в IPL (%d раз), но COL-записи с таким именем нет ни в одном .col — краш при загрузке зоны", o.name.c_str(), o.numInstances),
						        "Добавь COL-запись с именем модели (в .col архива или отдельный .col). Если это LOD-модель — назови её LOD<имя> и привяжи к модели.", o.id);
				}
			}
		}
		if(sa && o.colEntry >= 0 && o.dffRadius > 0){
			const ColEntry &ce = gd.colEntries[(size_t)o.colEntry];
			if(ce.radius > 0 && o.dffRadius > ce.radius * 1.15f + 0.5f)
				ctx.add(SEV_INFO, CAT_XREF, "COL-23a", ce.entryIdx >= 0 ? gd.entries[(size_t)ce.entryIdx].name : "COL", ce.index, o.name,
				        fmt("сфера DFF (r=%.1f) заметно больше сферы COL (r=%.1f) — визуальная модель отсекается по COL-сфере и будет пропадать у края экрана", o.dffRadius, ce.radius), "", o.id);
		}
		if(!placed && (o.type == OT_OBJS || o.type == OT_TOBJ || o.type == OT_ANIM) && !o.isLod){
			orphans++;
			if(orphans <= 200)
				ctx.add(SEV_INFO, CAT_XREF, "IDE-03", o.file, o.line, o.name, "объявлена в IDE, но нигде не размещена в IPL", "", o.id);
		}
	}
	if(noDff > 300) ctx.add(SEV_WARN, CAT_XREF, "DFF-71", "IDE", -1, "", fmt("…и ещё %d моделей без DFF", noDff - 300), "");
	if(noTxd > 300) ctx.add(SEV_WARN, CAT_XREF, "DFF-71", "IDE", -1, "", fmt("…и ещё %d моделей без TXD", noTxd - 300), "");
	if(noCol > 300) ctx.add(SEV_WARN, CAT_XREF, "COL-27", "IPL", -1, "", fmt("…и ещё %d размещённых моделей без COL", noCol - 300), "");
	if(orphans > 200) ctx.add(SEV_INFO, CAT_XREF, "IDE-03", "IDE", -1, "", fmt("…и ещё %d неразмещённых моделей", orphans - 200), "");

	// TXD slots created by IMG entries that no IDE uses
	int unused = 0;
	for(size_t i = 0; i < gd.txdSlots.size(); i++){
		const TxdSlot &t = gd.txdSlots[i];
		if(t.fromImg && !t.fromIde && t.name != "generic" && t.name != "particle" && t.name != "vehicle" && t.name != "effectspc" && t.name.compare(0, 4, "load") != 0 && t.name.compare(0, 5, "splash") != 0)
			unused++;
	}
	if(unused) ctx.add(SEV_INFO, CAT_XREF, "TXD-28", "IMG", -1, "", fmt("%d TXD в архивах не используются ни одной IDE-моделью (занимают слоты пула 5000)", unused), "");
}

void RunCheck(Context &ctx)
{
	Progress &pr = *ctx.prog;
	pr.running = true;
	pr.finished = false;
	pr.startMs = nowMs();
	ctx.phase = PH_LOAD;
	LoadGameData(ctx);
	if(!ctx.cancelled() && ctx.gd->game != GAME_UNKNOWN){
		ctx.phase = PH_TXD; if(ctx.opt.checkTxd) CheckTxds(ctx);
		ctx.phase = PH_DATA; if(!ctx.cancelled() && ctx.opt.checkData) CheckDataFiles(ctx);
		// COLs before DFFs: the vehicle rules need to know whether a .col entry named after the model exists (VEH-07)
		ctx.phase = PH_COL; if(!ctx.cancelled() && ctx.opt.checkCol) CheckCols(ctx);
		ctx.phase = PH_IFP; if(!ctx.cancelled() && ctx.opt.checkIfp) CheckIfps(ctx);
		ctx.phase = PH_DFF; if(!ctx.cancelled() && ctx.opt.checkDff) CheckDffs(ctx);
		ctx.phase = PH_DATA;
		if(!ctx.cancelled() && ctx.opt.checkData){
			// round-3 data-file modules (need IDE + IMG directories + TXD names + IFP anim lists)
			if(!ctx.cancelled()) CheckHandling(ctx);
			if(!ctx.cancelled()) CheckWeapons(ctx);
			if(!ctx.cancelled()) CheckPedData(ctx);
			if(!ctx.cancelled()) CheckClothes(ctx);
			if(!ctx.cancelled()) CheckCutscenes(ctx);
			if(!ctx.cancelled()) CheckTextScriptImg(ctx);
			if(!ctx.cancelled()) CheckAudio(ctx);
		}
		ctx.phase = PH_XREF; if(!ctx.cancelled() && !ctx.opt.partial) CheckCrossRefs(ctx);
		ctx.phase = PH_QUALITY; if(!ctx.cancelled() && !ctx.opt.partial) CheckQuality(ctx);
	}
	pr.endMs = nowMs();
	pr.set(ctx.cancelled() ? "Отменено" : "Готово");
	pr.running = false;
	pr.finished = true;
}

} // namespace gc
