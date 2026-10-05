// Included by main.cpp after the single-dictionary planner.
struct ProjectTxdRow {
	int slot = -1; uint64_t hash = 0; size_t bytes = 0;
	bool selected = false, removeOnly = false; TxdSplit plan;
	std::map<int,uint64_t> dffHash;
};
struct ProjectTxdState {
	bool open = false, scanning = false, exporting = false, applied = false, refresh = false;
	size_t cursor = 0; std::string root, dir, log, status;
	int writtenTxd = 0;
	std::vector<ProjectTxdRow> rows;
	std::set<std::string> reserved;
	std::map<std::string, std::vector<uint8_t>> ide;
	std::map<std::string,uint64_t> ideHash;
	std::set<std::string> unused, protectedNames;
	bool referencesRead = true;
	struct Work { std::atomic<int> phase{0},done{0},total{1}; };
	std::shared_ptr<Work> work;
	std::future<ProjectTxdResult> worker;
	int64_t savedBytes=0;
	uint64_t fingerprint=0;
};
static ProjectTxdState gProjectTxd;
static bool projectTxdBusy() { return gProjectTxd.exporting || gProjectTxd.worker.valid() || gProjectTxd.refresh; }
static bool projectTxdCan(const ProjectTxdRow &row)
{
	return row.removeOnly || (!row.plan.groups.empty() && row.plan.noTex.empty() && (row.plan.groups.size()>1 || !row.plan.dead.empty()));
}
static void projectTxdPoll()
{
	if(!gProjectTxd.worker.valid() || gProjectTxd.worker.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
	ProjectTxdResult result;
	try { result=gProjectTxd.worker.get(); }
	catch(const std::exception &){ result.message=T("Операция прервана. Проверь журнал и бэкап."); }
	gProjectTxd.status=result.message; gProjectTxd.savedBytes=result.savedBytes;
	gProjectTxd.exporting=false; gProjectTxd.applied=result.ok; gProjectTxd.refresh=result.ok;
}
static uint64_t projectTxdHash(const std::vector<uint8_t> &data)
{
	uint64_t h = 14695981039346656037ull;
	for(uint8_t b : data){ h ^= b; h *= 1099511628211ull; } return h;
}
static bool projectTxdWrite(const std::string &path, const std::vector<uint8_t> &data)
{
	size_t slash = path.find_last_of("/\\");
	if(slash != std::string::npos){
		for(size_t i=3;i<=slash;i++) if(path[i]=='/' || path[i]=='\\') if(!EnsureDir(path.substr(0,i))) return false;
		if(!EnsureDir(path.substr(0,slash))) return false;
	}
	int n = MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
	std::wstring wide((size_t)n,L'\0'); MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,&wide[0],n);
	FILE *f = _wfopen(wide.c_str(), L"wb"); if(!f) return false;
	bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
	return fclose(f) == 0 && ok;
}
static void projectTxdCleanStage()
{
	if(gProjectTxd.dir.empty()) return;
	std::string stagedDir=joinPath(gProjectTxd.dir,"staged");
	auto remove=[&](const std::string &name){
		std::string path=joinPath(stagedDir,name);
		DeleteFileW(std::filesystem::u8path(path).c_str());
	};
	for(const ProjectTxdRow &row:gProjectTxd.rows) if(row.selected)
		for(const TxdSplitGroup &group:row.plan.groups) remove(group.name+".txd");
	for(size_t i=0;i<gProjectTxd.ide.size();i++) remove("ide_"+std::to_string(i)+".bin");
	std::error_code ec; std::filesystem::remove(std::filesystem::u8path(stagedDir),ec); ec.clear();
	std::filesystem::remove(std::filesystem::u8path(gProjectTxd.dir),ec);
	gProjectTxd.dir.clear();
}
static void projectTxdReferences(const std::vector<uint8_t> &bytes)
{
	std::string token;
	for(size_t i=0;i<=bytes.size();i++){
		unsigned char c=i<bytes.size() ? bytes[i]:0;
		if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_'){ token.push_back((char)c); continue; }
		if(token.size()>=1 && token.size()<=24){ std::string name=lower(token); if(gProjectTxd.reserved.count(name)) gProjectTxd.protectedNames.insert(name); } token.clear();
	}
}
static void projectTxdReadReferences()
{
	std::set<std::string> paths;
	for(const ModFile &mf:gData->modFiles) paths.insert(mf.phys);
	std::string scripts=DataFilePath(*gData,"data/script/script.img");
	if(fileExists(scripts)){ std::vector<uint8_t> bytes; if(readFile(scripts,bytes)) projectTxdReferences(bytes); else gProjectTxd.referencesRead=false; }
	std::error_code ec; std::filesystem::recursive_directory_iterator it(std::filesystem::u8path(gData->root),ec),end;
	if(ec) gProjectTxd.referencesRead=false;
	for(;it!=end;it.increment(ec)){
		if(ec){ gProjectTxd.referencesRead=false; break; }
		std::string path=it->path().u8string(),name=lower(it->path().filename().u8string());
		if(it->is_directory(ec) && (name=="modloader" || name=="inu_check" || name=="gta_check_backup")){ it.disable_recursion_pending(); continue; }
		if(it->is_regular_file(ec)) paths.insert(path);
	}
	if(ec) gProjectTxd.referencesRead=false;
	for(const std::string &path:paths){
		std::string ext=lower(extOf(path));
		if(ext!="scm" && ext!="cs" && ext!="cs3" && ext!="lua" && ext!="ini" && ext!="cfg" && ext!="json" && ext!="xml" && ext!="txt" && ext!="asi" && ext!="dll" && ext!="exe") continue;
		std::vector<uint8_t> bytes;
		if(!readFile(path,bytes)) gProjectTxd.referencesRead=false; else projectTxdReferences(bytes);
	}
	for(size_t i=0;i<gData->entries.size();i++) if(gData->entries[i].kind==EK_SCM){
		std::vector<uint8_t> bytes;
		if(!gData->readEntry((int)i,bytes)) gProjectTxd.referencesRead=false; else projectTxdReferences(bytes);
	}
	for(const std::string &path:gData->datFiles) if(path.compare(0,11,"TEXDICTION:")==0) gProjectTxd.protectedNames.insert(lower(stem(basename(path.substr(11)))));
	std::vector<UnusedFile> unused; CollectUnused(*gData,unused);
	for(const UnusedFile &u:unused) if(gData->entries[(size_t)u.entry].kind==EK_TXD) gProjectTxd.unused.insert(gData->entries[(size_t)u.entry].base);
}
static void projectTxdOpen()
{
	if(!gData || gProgress.running || !gFlagsDone) return;
	gProjectTxd = ProjectTxdState(); gProjectTxd.open = gProjectTxd.scanning = true;
	gProjectTxd.root = gData->root;
	gProjectTxd.fingerprint=cacheFingerprint(gData->root,gData->opt);
	for(const TxdSlot &slot : gData->txdSlots) gProjectTxd.reserved.insert(slot.name);
	projectTxdReadReferences();
}
static void projectTxdScanStep()
{
	if(!gProjectTxd.scanning) return;
	if(gProjectTxd.cursor >= gData->txdSlots.size()){ gProjectTxd.scanning = false; gProjectTxd.cursor = 0; return; }
	ProjectTxdRow row; row.slot = (int)gProjectTxd.cursor++;
	const TxdSlot &slot = gData->txdSlots[(size_t)row.slot];
	std::vector<uint8_t> bytes; std::string err; TxdFile file;
	if(slot.entry < 0 || !ReadEntryTrimmed(*gData, slot.entry, bytes, err) || !TxdParse(bytes, file) || !file.ok){
		row.plan.note = T("TXD не прочитан — пропущен");
	}else{
		row.bytes = bytes.size(); row.hash = projectTxdHash(bytes);
		planTxdSplit(file, slot.name + ".txd", row.plan);
		bool chain=slot.parent>=0; for(const TxdSlot &other:gData->txdSlots) if(other.parent==row.slot) chain=true;
		bool special=false;
		if(slot.entry>=0 && gData->entries[(size_t)slot.entry].img>=0){
			std::string archive=lower(basename(gData->archives[(size_t)gData->entries[(size_t)slot.entry].img].logical));
			special=archive=="player.img" || archive=="cuts.img" || archive=="cutscene.img" || archive=="script.img";
		}
		static const char *system[]={"generic","particle","vehicle","effectspc","fonts","fronten","hud","radar","loadsc","splash","ld_"};
		for(const char *prefix:system) if(slot.name.compare(0,strlen(prefix),prefix)==0) special=true;
		if(chain || special || !gProjectTxd.referencesRead || gProjectTxd.protectedNames.count(slot.name)){
			row.plan.groups.clear(); row.plan.dead.clear(); row.plan.note=T("Системный TXD, txdp или внешние ссылки — сохранён");
		}else if(!row.plan.noTex.empty() && !row.plan.groups.empty()){
			row.plan.groups.clear(); row.plan.dead.clear(); row.plan.note=T("Остаются модели с исходным TXD — сохранён");
		}else if(gProjectTxd.unused.count(slot.name) && row.plan.groups.empty()){
			row.removeOnly=true; row.plan.note=T("Неиспользуемый словарь будет удалён");
		}
		row.selected = projectTxdCan(row);
		if(row.selected){
			for(const TxdSplitGroup &group : row.plan.groups) for(int mi : group.models){
				const ObjDef &o = gData->objs[(size_t)mi]; int e = o.dffEntry >= 0 ? o.dffEntry : gData->findEntry(o.name+".dff");
				std::vector<uint8_t> raw;
				if(e < 0 || !ReadEntryTrimmed(*gData,e,raw,err)){ row.selected=false; row.plan.note=T("TXD не прочитан — пропущен"); break; }
				row.dffHash[e]=projectTxdHash(raw);
			}
			if(!row.selected){ row.plan.groups.clear(); row.plan.dead.clear(); }
		}
		if(row.selected){
			for(TxdSplitGroup &group : row.plan.groups){
				int n = 1; std::string name;
				do { std::string suffix = "_" + std::to_string(n++); name = slot.name.substr(0, 19-suffix.size()) + suffix; }
				while(gProjectTxd.reserved.count(name) || gData->findEntry(name + ".txd") >= 0);
				group.name = name; gProjectTxd.reserved.insert(name);
			}
		}
	}
	gProjectTxd.rows.push_back(std::move(row));
}
// Validate and prepare every IDE edit before writing the export package.
static bool projectTxdPrepareIde()
{
	gProjectTxd.ide.clear();
	gProjectTxd.ideHash.clear();
	std::map<std::string, std::map<int, std::pair<int, std::string>>> edits;
	for(const ProjectTxdRow &row : gProjectTxd.rows) if(row.selected)
		for(const TxdSplitGroup &group : row.plan.groups) for(int mi : group.models){
			const ObjDef &o = gData->objs[(size_t)mi]; edits[o.file][o.line] = {mi, group.name};
		}
	for(const auto &entry : edits){
		std::string logical = entry.first; std::replace(logical.begin(), logical.end(), '\\', '/');
		if(logical.empty()){ gProjectTxd.status=T("Не удалось определить путь IDE. Сборка не изменена."); return false; } // Physical write paths are contained by the transaction.
		std::vector<uint8_t> orig; if(!readFile(DataFilePath(*gData, entry.first), orig)){ gProjectTxd.status=fmt(T("Не удалось прочитать IDE: %s. Сборка не изменена."),entry.first.c_str()); return false; }
		gProjectTxd.ideHash[DataFilePath(*gData,entry.first)] = projectTxdHash(orig);
		std::string text(orig.begin(), orig.end()), result; size_t start = 0; int ln = 1, changed = 0;
		while(start < text.size()){
			size_t end = text.find('\n', start); if(end == std::string::npos) end = text.size();
			std::string line = text.substr(start, end-start);
			auto edit = entry.second.find(ln);
			if(edit != entry.second.end()){
				std::vector<std::pair<size_t,size_t>> tokens;
				for(size_t p = 0; p < line.size();){
					while(p < line.size() && (line[p]==' ' || line[p]==',' || line[p]=='\t' || line[p]=='\r')) p++;
					size_t b=p; while(p < line.size() && line[p]!=' ' && line[p]!=',' && line[p]!='\t' && line[p]!='\r') p++;
					if(p>b) tokens.push_back({b,p-b});
				}
				const ObjDef &o = gData->objs[(size_t)edit->second.first];
				if(tokens.size()<3 || line.substr(tokens[0].first,tokens[0].second)!=std::to_string(o.id) ||
					lower(line.substr(tokens[1].first,tokens[1].second))!=lower(o.name) ||
					lower(line.substr(tokens[2].first,tokens[2].second))!=lower(o.txd)) { gProjectTxd.status=fmt(T("Строка %d файла IDE %s уже отличается от плана. Сборка не изменена; построй план заново."),ln,entry.first.c_str()); return false; }
				line.replace(tokens[2].first,tokens[2].second,edit->second.second); changed++;
			}
			result += line; if(end < text.size()) result += '\n'; start = end+1; ln++;
		}
		if(changed != (int)entry.second.size()){ gProjectTxd.status=fmt(T("Не все строки IDE найдены в %s. Сборка не изменена; построй план заново."),entry.first.c_str()); return false; }
		gProjectTxd.ide[logical] = std::vector<uint8_t>(result.begin(), result.end());
	}
	return true;
}
static bool projectTxdInstallRequest(ProjectTxdRequest &request, std::string &why)
{
	auto fail=[&](const char *message){ why=T(message); return false; };
	if(gProjectTxd.fingerprint!=cacheFingerprint(gData->root,gData->opt)) return fail("Сборка или настройки проверки изменились после построения плана.");
	request.root=gProjectTxd.root; request.backup=gProjectTxd.dir;
	for(const Archive &a:gData->archives){ ProjectTxdArchive archive; archive.path=a.phys; archive.ver2=a.ver2; request.archives.push_back(archive); }
	std::set<int> inputs;
	for(const ProjectTxdRow &row:gProjectTxd.rows) if(row.selected){
		const TxdSlot &slot=gData->txdSlots[(size_t)row.slot]; const Entry &entry=gData->entries[(size_t)slot.entry];
		request.removedNames.insert(lower(entry.name)); request.removedTxd++;
		inputs.insert(slot.entry); for(const auto &dff:row.dffHash) inputs.insert(dff.first);
		for(const TxdSplitGroup &group:row.plan.groups){
			std::string name=group.name+".txd",staged=joinPath(joinPath(gProjectTxd.dir,"staged"),name);
			if(!entry.loose.empty()){
				ProjectTxdChange change; change.path=joinPath(std::filesystem::u8path(entry.loose).parent_path().u8string(),name); change.staged=staged;
				if(fileExists(change.path)) return fail("Имя нового TXD уже занято loose-файлом."); request.files.push_back(change);
			}else if(entry.img>=0) request.archives[(size_t)entry.img].added[name]=staged;
			else return fail("Для выбранного TXD не найден исходный IMG или loose-файл.");
			request.newTxd++;
		}
	}
	std::set<std::string> loose;
	for(const Entry &entry:gData->entries) if(!entry.loose.empty() && request.removedNames.count(lower(entry.name))) loose.insert(entry.loose);
	for(const ModFile &file:gData->modFiles) if(request.removedNames.count(lower(basename(file.phys)))) loose.insert(file.phys);
	for(const std::string &path:loose){ ProjectTxdChange change; change.path=path; change.remove=true; request.files.push_back(change); }
	for(int index:inputs){
		const Entry &entry=gData->entries[(size_t)index]; std::vector<uint8_t> raw; std::string error;
		if(!ReadEntryTrimmed(*gData,index,raw,error)) return fail("Не удалось повторно прочитать TXD/DFF для проверки плана.");
		uint64_t hash=projectTxdHash(raw); bool match=false;
		for(const ProjectTxdRow &row:gProjectTxd.rows) if(row.selected){
			if(gData->txdSlots[(size_t)row.slot].entry==index) match=row.hash==hash;
			auto dff=row.dffHash.find(index); if(dff!=row.dffHash.end()) match=dff->second==hash;
		}
		if(!match) return fail("TXD или DFF изменился после анализа. Построй план заново.");
		ProjectTxdCheck check; check.path=!entry.loose.empty() ? entry.loose:gData->archives[(size_t)entry.img].phys;
		check.offset=entry.loose.empty() ? (uint64_t)entry.offset*2048:0; check.size=raw.size(); check.hash=hash; request.checks.push_back(check);
		if(entry.loose.empty()){
			const Archive &archive=gData->archives[(size_t)entry.img];
			std::string catalog=archive.ver2 ? archive.phys:archive.phys.substr(0,archive.phys.size()-4)+".dir";
			FILE *file=OpenReadUtf8(catalog); if(!file) return fail("Не удалось открыть каталог IMG для проверки.");
			uint8_t record[32]; uint64_t offset=(archive.ver2 ? 8:0)+(uint64_t)entry.dirIndex*32;
			bool ok=_fseeki64(file,(long long)offset,SEEK_SET)==0 && fread(record,1,32,file)==32; fclose(file);
			auto u32=[](const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;};
			if(!ok || !memchr(record+8,0,24) || lower((const char*)record+8)!=lower(entry.name) || u32(record)!=entry.offset) return fail("Каталог IMG изменился после анализа. Построй план заново.");
			uint32_t sectors=archive.ver2 ? (uint32_t)record[4]|(uint32_t)record[5]<<8:u32(record+4);
			if(archive.ver2 && !sectors) sectors=(uint32_t)record[6]|(uint32_t)record[7]<<8;
			if(sectors!=entry.sizeSectors) return fail("Размер записи IMG изменился после анализа. Построй план заново.");
			check=ProjectTxdCheck();check.path=catalog;check.offset=offset;check.size=32;check.hash=projectTxdHash(std::vector<uint8_t>(record,record+32));request.checks.push_back(check);
		}
	}
	// Check the resulting IDE text, including unchanged files. A retained reference
	// must block removal even when the cached model list missed it.
	std::set<std::string> logicalIde(gData->ideFiles.begin(),gData->ideFiles.end());
	std::map<std::string,const std::vector<uint8_t>*> editedByPhysical;
	for(const auto &ide:gProjectTxd.ide){
		logicalIde.insert(ide.first);std::string key=lower(normSlashes(DataFilePath(*gData,ide.first)));
		auto other=editedByPhysical.find(key);if(other!=editedByPhysical.end() && *other->second!=ide.second) return fail("Один физический IDE-файл подключён разными путями с несовместимыми правками.");
		editedByPhysical[key]=&ide.second;
	}
	for(const std::string &logical:logicalIde){
		std::string phys=DataFilePath(*gData,logical);std::vector<uint8_t> before;if(!readFile(phys,before)) return fail("Не удалось прочитать IDE при проверке ссылок на удаляемые TXD.");
		ProjectTxdCheck check;check.path=phys;check.size=before.size();check.hash=projectTxdHash(before);request.checks.push_back(check);
		auto edited=editedByPhysical.find(lower(normSlashes(phys)));const std::vector<uint8_t> &bytes=edited==editedByPhysical.end() ? before:*edited->second;
		std::string contents(bytes.begin(),bytes.end()),section;size_t start=0;
		while(start<contents.size()){
			size_t end=contents.find('\n',start);if(end==std::string::npos) end=contents.size();
			std::string line=lower(trim(contents.substr(start,end-start)));
			// GTA IDE lines prefixed with # are disabled; they must not prevent
			// removal of a dictionary referenced only by a commented-out object.
			if(!line.empty() && (line[0]=='#' || line[0]==';' || line.compare(0,2,"//")==0)){ start=end+1; continue; }
			std::replace(line.begin(),line.end(),',',' ');
			std::vector<std::string> tokens;SplitTokens(line,tokens);
			if(tokens.size()==1) section=tokens[0]=="end" ? "":tokens[0];
			else if(section=="txdp"){
				for(const std::string &token:tokens) if(request.removedNames.count(token+".txd")){ why=fmt(T("В IDE %s осталась ссылка на удаляемый TXD %s."),logical.c_str(),token.c_str()); return false; }
			}else if(section=="objs" || section=="tobj" || section=="cars" || section=="peds" || section=="weap" || section=="hier" || section=="anim"){
				if(tokens.size()>=3 && request.removedNames.count(tokens[2]+".txd")){ why=fmt(T("В IDE %s осталась ссылка на удаляемый TXD %s."),logical.c_str(),tokens[2].c_str()); return false; }
			}
			start=end+1;
		}
	}
	std::set<std::string> writtenIde;
	for(const auto &ide:gProjectTxd.ide){
		std::string phys=DataFilePath(*gData,ide.first); std::vector<uint8_t> before;
		if(!writtenIde.insert(lower(normSlashes(phys))).second) continue;
		if(!readFile(phys,before) || projectTxdHash(before)!=gProjectTxd.ideHash[phys]){ why=fmt(T("IDE %s изменился после построения плана. Построй план заново."),ide.first.c_str()); return false; }
		ProjectTxdCheck check; check.path=phys; check.size=before.size(); check.hash=projectTxdHash(before); request.checks.push_back(check);
		ProjectTxdChange change; change.path=phys; change.staged=joinPath(joinPath(gProjectTxd.dir,"staged"),"ide_"+std::to_string(request.ideFiles++)+".bin");
		if(!projectTxdWrite(change.staged,ide.second)){ why=T("Не удалось подготовить изменённый IDE в папке бэкапа."); return false; } request.files.push_back(change);
	}
	return true;
}
static void projectTxdExportStep()
{
	if(!gProjectTxd.exporting) return;
	while(gProjectTxd.cursor < gProjectTxd.rows.size() && !gProjectTxd.rows[gProjectTxd.cursor].selected) gProjectTxd.cursor++;
	bool ok = true; std::string why;
	if(gProjectTxd.cursor < gProjectTxd.rows.size()){
		const ProjectTxdRow &row = gProjectTxd.rows[gProjectTxd.cursor++];
		std::vector<uint8_t> bytes; std::string err; TxdFile file;
		int e = gData->txdSlots[(size_t)row.slot].entry;
		ok = ReadEntryTrimmed(*gData, e, bytes, err) && projectTxdHash(bytes)==row.hash && TxdParse(bytes,file) && file.ok;
		for(const auto &dff : row.dffHash){ std::vector<uint8_t> raw; if(!ReadEntryTrimmed(*gData,dff.first,raw,err) || projectTxdHash(raw)!=dff.second) ok=false; }
		if(ok) for(const TxdSplitGroup &group : row.plan.groups){
			TxdFile part = file; part.tex.clear();
			for(int ti : group.texs) part.tex.push_back(file.tex[(size_t)ti]);
			std::vector<uint8_t> out;
			TxdFile verify;
			if(!TxdWrite(part,out,err) || !TxdParse(out,verify) || verify.tex.size()!=part.tex.size()){ ok=false; break; }
			for(size_t i=0;i<part.tex.size();i++){
				const TxdTex &a=part.tex[i],&b=verify.tex[i];
				if(std::tie(a.name,a.mask,a.platform,a.filterAddr,a.rasterFormat,a.d3dFormat,a.width,a.height,a.depth,a.mips,a.flags,a.palette,a.levels)!=
					std::tie(b.name,b.mask,b.platform,b.filterAddr,b.rasterFormat,b.d3dFormat,b.width,b.height,b.depth,b.mips,b.flags,b.palette,b.levels)) ok=false;
			}
			if(!ok || !projectTxdWrite(joinPath(joinPath(gProjectTxd.dir,"staged"),group.name+".txd"),out)){ ok=false; break; }
			gProjectTxd.log += group.name+".txd\n";
			gProjectTxd.writtenTxd++;
		}
	}else{
		ProjectTxdRequest request; ok=projectTxdInstallRequest(request,why);
		if(ok){
			gProjectTxd.work=std::make_shared<ProjectTxdState::Work>(); auto work=gProjectTxd.work;
			try {
				gProjectTxd.worker=std::async(std::launch::async,[request,work](){
					ProjectTxdResult result;
					ApplyProjectTxd(request,result,[work](int phase,int done,int total){work->phase=phase;work->done=done;work->total=(std::max)(1,total);});
					return result;
				});
				gProjectTxd.exporting=false;
			}catch(const std::exception &){ok=false;}
		}
	}
	if(!ok){ projectTxdCleanStage(); gProjectTxd.exporting=false; if(!why.empty()) gProjectTxd.status=why; else if(gProjectTxd.status.empty()) gProjectTxd.status=T("Подготовка не завершена. Сборка не изменена; построй план заново."); }
}
static void drawProjectTxd()
{
	if(!gProjectTxd.open) return;
	if(!gProjectTxd.applied && (!gData || gData->root!=gProjectTxd.root || gProgress.running)){ gProjectTxd=ProjectTxdState(); return; }
	float width=(std::min)(700.0f,(std::max)(320.0f,ImGui::GetIO().DisplaySize.x-32.0f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(width,0),ImVec2(width,ImGui::GetIO().DisplaySize.y*0.9f));
	if(gProjectTxd.applied){
		if(ImGui::Begin(T("Распределение TXD проекта"),nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize)){
			ImGui::TextWrapped("%s",gProjectTxd.status.c_str());
			if(gProjectTxd.savedBytes>=0) ImGui::Text(T("Освобождено в сборке: %.2f МБ"),gProjectTxd.savedBytes/1048576.0);
			else ImGui::Text(T("Размер сборки вырос на %.2f МБ"),-gProjectTxd.savedBytes/1048576.0);
			ImGui::TextWrapped("%s",T("Оригиналы сохранены в бэкапе. Для полного отката закрой игру и INU Check, затем запусти Restore-All.bat из его папки."));
			if(btn(T("Открыть бэкап"))) openInExplorer(gProjectTxd.dir); ImGui::SameLine();
			if(btn(T("Закрыть"))) gProjectTxd.open=false;
		} ImGui::End(); return;
	}
	projectTxdScanStep(); projectTxdExportStep();
	int candidates=0,selected=0,groups=0,models=0,dead=0; double textureMb=0;
	for(const ProjectTxdRow &row:gProjectTxd.rows){
		bool can=projectTxdCan(row);
		if(can) candidates++;
		if(!row.selected) continue;
		selected++; dead+=(int)row.plan.dead.size();
		for(const TxdSplitGroup &group:row.plan.groups){ groups++; models+=(int)group.models.size(); textureMb+=group.mb; }
	}
	if(ImGui::Begin(T("Распределение TXD проекта"),nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize)){
		ImGui::TextWrapped("%s",T("Изменения применяются прямо в сборке: новые TXD, обновлённые IDE и удаление лишних словарей. Выбранная папка нужна для бэкапа."));
		ImGui::Text(T("Проверено TXD: %d из %d"),(int)gProjectTxd.rows.size(),(int)gData->txdSlots.size());
		if(gProjectTxd.scanning) ImGui::ProgressBar((float)gProjectTxd.cursor / std::max<size_t>(1,gData->txdSlots.size()));
		else ImGui::Text(T("Без изменений или пропущено: %d TXD"),(int)gProjectTxd.rows.size()-candidates);
		if(!projectTxdBusy() && ImGui::CollapsingHeader(T("Подробности и выбор TXD")) && ImGui::BeginTable("projectTxd",4,ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollY|ImGuiTableFlags_RowBg,ImVec2(0,(std::min)(260.0f,ImGui::GetIO().DisplaySize.y*0.4f)))){
			ImGui::TableSetupColumn("TXD"); ImGui::TableSetupColumn(T("Новый TXD")); ImGui::TableSetupColumn(T("МБ")); ImGui::TableSetupColumn(T("Результат")); ImGui::TableHeadersRow();
			ImGuiListClipper clip; clip.Begin((int)gProjectTxd.rows.size());
			while(clip.Step()) for(int i=clip.DisplayStart;i<clip.DisplayEnd;i++){
				ProjectTxdRow &row=gProjectTxd.rows[(size_t)i]; const TxdSlot &slot=gData->txdSlots[(size_t)row.slot];
				ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
				bool can=projectTxdCan(row);
				ImGui::BeginDisabled(!can || gProjectTxd.exporting); ImGui::Checkbox(slot.name.c_str(),&row.selected); ImGui::EndDisabled();
				ImGui::TableSetColumnIndex(1); ImGui::Text("%d",can ? (int)row.plan.groups.size() : 0);
				if(ImGui::IsItemHovered()){
					ImGui::BeginTooltip();
					for(const TxdSplitGroup &group:row.plan.groups){
						ImGui::Text("%s: %d, %.2f MB",group.name.c_str(),(int)group.models.size(),group.mb);
						for(size_t m=0;m<group.models.size() && m<8;m++) ImGui::TextUnformatted(gData->objs[(size_t)group.models[m]].name.c_str());
					} ImGui::EndTooltip();
				}
				ImGui::TableSetColumnIndex(2); float largest=0; for(const TxdSplitGroup &group:row.plan.groups) largest=(std::max)(largest,(float)group.mb);
				ImGui::Text("%.2f -> %.2f",row.bytes/1048576.0,can ? largest : row.bytes/1048576.0);
				ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(can ? (row.removeOnly ? T("Удалить") : row.plan.groups.size()>1 ? T("Разделить") : T("Убрать лишние текстуры")) : T("Без изменений"));
				if(ImGui::IsItemHovered() && !row.plan.note.empty()) uiTip("%s",row.plan.note.c_str()); ImGui::PopID();
			} ImGui::EndTable();
		}
		ImGui::Separator();
		ImGui::Text(T("К обработке: %d TXD · моделей: %d"),selected,models);
		ImGui::Text(T("Новых TXD: %d · данных текстур: ≈ %.2f МБ"),groups,textureMb);
		ImGui::Text(T("Будет удалено исходных словарей: %d"),selected);
		if(dead) ImGui::Text(T("Будет удалено неиспользуемых текстур: %d"),dead);
		if(gProjectTxd.scanning) ImGui::TextUnformatted(T("Предварительный итог — анализ ещё идёт."));
		if(gProjectTxd.exporting){
			ImGui::Text(T("Подготовлено TXD: %d из %d"),gProjectTxd.writtenTxd,groups);
			ImGui::ProgressBar(groups ? (float)gProjectTxd.writtenTxd/groups : 0);
		}
		if(gProjectTxd.worker.valid()){
			static const char *phases[]={"Подготовка", "Бэкап", "Перепаковка IMG", "Применение в сборке", "Восстановление"};
			int phase=gProjectTxd.work->phase.load(); phase=(std::max)(0,(std::min)(4,phase));
			ImGui::TextUnformatted(T(phases[phase]));
			ImGui::ProgressBar((float)gProjectTxd.work->done.load()/gProjectTxd.work->total.load());
		}
		if(!gProjectTxd.status.empty()) ImGui::TextWrapped("%s",gProjectTxd.status.c_str());
		ImGui::BeginDisabled(gProjectTxd.scanning || projectTxdBusy() || !selected);
		if(btn(T("Применить в сборке…"))){
			std::string dir;
			if(pickFolder(dir)){
				if(gTv.dirty) gProjectTxd.status=T("Сначала сохрани несохранённые правки TXD и построй план заново.");
				else if(!ProjectTxdBackupOutside(gProjectTxd.root,dir)) gProjectTxd.status=T("Папка бэкапа должна находиться вне сборки.");
				else if(projectTxdPrepareIde()){
					gProjectTxd.dir=joinPath(dir,"inu_check_txd_backup_"+std::to_string((unsigned long long)nowMs()));
					if(!dirExists(gProjectTxd.dir) && EnsureDir(gProjectTxd.dir)){ gProjectTxd.cursor=0; gProjectTxd.exporting=true; gProjectTxd.log.clear(); gProjectTxd.status.clear(); gProjectTxd.writtenTxd=0; }
					else gProjectTxd.status=T("Не удалось создать папку бэкапа.");
				}else if(gProjectTxd.status.empty()) gProjectTxd.status=T("IDE изменились или не прочитаны. Построй план заново.");
			}
		} ImGui::EndDisabled(); ImGui::SameLine();
		ImGui::BeginDisabled(projectTxdBusy()); if(btn(T("Закрыть"))) gProjectTxd.open=false; ImGui::EndDisabled();
		if(!gProjectTxd.dir.empty()){ ImGui::SameLine(); if(btn(T("Открыть бэкап"))) openInExplorer(gProjectTxd.dir); }
	} ImGui::End();
}

#include "project_txd_tests.h"
static void projectTxdTest()
{
	// Graph regression: shared texture, unused texture, reserved name, unknown DFF, txdp.
	GameData fixture; int slot=fixture.txdSlotOf("test"); fixture.txdSlotOf("test_1");
	TxdFile file; file.ok=true;
	for(const char *name : {"a","b","dead"}){ TxdTex tex; tex.name=name; file.tex.push_back(tex); }
	for(int i=0;i<3;i++){ ObjDef o; o.txdSlot=slot; o.dffParsed=true; o.dffTex={i<2 ? "a":"b"}; fixture.objs.push_back(o); }
	GameData *source=gData; gData=&fixture; TxdSplit plan;
	planTxdSplit(file,"test.txd",plan);
	int graphFailures=plan.groups.size()!=2 || plan.dead.size()!=1;
	for(const TxdSplitGroup &group:plan.groups) if(group.name=="test_1") graphFailures++;
	fixture.objs[0].dffParsed=false; planTxdSplit(file,"test.txd",plan);
	if(!plan.groups.empty() || !plan.dead.empty() || plan.note.empty()) graphFailures++;
	fixture.objs[0].dffParsed=true; fixture.txdSlots[(size_t)slot].parent=slot;
	planTxdSplit(file,"test.txd",plan); if(!plan.groups.empty() || !plan.dead.empty()) graphFailures++;
	gData=source; mvChkWrite(fmt("project TXD graph: failures %d\n",graphFailures).c_str());
	GDff maskFixture; GGeom maskGeom; GMat maskMaterial; maskMaterial.textured=true; maskMaterial.tex="window"; maskMaterial.mask="window_alpha";
	maskGeom.mats.push_back(maskMaterial); GTri maskTri; maskTri.mat=0; maskGeom.tri.push_back(maskTri);
	GMat unusedSlot; unusedSlot.textured=true; unusedSlot.tex="unused_material_slot"; maskGeom.mats.push_back(unusedSlot); maskFixture.geoms.push_back(maskGeom);
	std::set<std::string> maskNames; txdSplitCollectUsedTextures(maskFixture,maskNames);
	int maskFailures=maskNames!=std::set<std::string>{"window","window_alpha","unused_material_slot"};
	mvChkWrite(fmt("project TXD material references: found %d/3 failures %d\n",(int)maskNames.size(),maskFailures).c_str());
	// Reproduce cache hydration: no DFF/material metadata, but entries and IDE are present.
	for(ObjDef &o : gData->objs){ o.dffParsed=false; o.dffTex.clear(); }
	projectTxdOpen();
	while(gProjectTxd.scanning){
		projectTxdScanStep();
		if(gProjectTxd.rows.size()%100==0) mvChkWrite(fmt("project TXD scan: %d\n",(int)gProjectTxd.rows.size()).c_str());
	}
	int selected=0,groups=0; size_t models=0; std::set<std::string> names; int failures=0;
	for(const ProjectTxdRow &row:gProjectTxd.rows){
		if(!row.selected) continue; selected++; groups+=(int)row.plan.groups.size();
		mvChkWrite(fmt("project TXD %s: groups %d dead %d stays %d\n",gData->txdSlots[(size_t)row.slot].name.c_str(),(int)row.plan.groups.size(),(int)row.plan.dead.size(),(int)row.plan.noTex.size()).c_str());
		for(const TxdSplitGroup &group:row.plan.groups){
			models+=group.models.size(); if(group.name.size()>19 || !names.insert(group.name).second || gData->findTxdSlot(group.name)>=0) failures++;
		}
	}
	bool ide = selected>0 && projectTxdPrepareIde();
	mvChkWrite(fmt("project TXD: slots %d selected %d groups %d models %d IDE %d failures %d\n",(int)gProjectTxd.rows.size(),selected,groups,(int)models,(int)gProjectTxd.ide.size(),failures+(selected>0 && !ide ? 1:0)).c_str());
	// Read-only scan tests must never apply to the supplied user game. Transaction
	// tests below create fresh fixtures outside it, rather than reusing PROJECTTXDOUT.
	if(const char *base=getenv("GTACHECK_PROJECTTXDFIXTURE")) projectTxdTransactionTests(base);
	gProjectTxd.open=getenv("GTACHECK_PROJECTTXD") && strcmp(getenv("GTACHECK_PROJECTTXD"),"ui")==0;
}
