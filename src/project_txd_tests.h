// Hidden transaction regression fixtures. Never reuse the supplied game as a
// write target: the base directory must be new, and every game below is synthetic.
static void projectTxdTransactionTests(const std::string &base)
{
	if(dirExists(base) || !ProjectTxdBackupOutside(gData->root,base) || !EnsureDir(base)){
		mvChkWrite("project TXD transaction: fixture directory refused\n"); return;
	}
	auto hash=[](const std::vector<uint8_t> &b){return projectTxdHash(b);};
	auto text=[](const std::string &s){return std::vector<uint8_t>(s.begin(),s.end());};
	auto put32=[](uint8_t *p,uint32_t v){for(int i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));};
	TxdFile original; original.ok=true;
	std::vector<uint8_t> rgba(32*32*4,255);
	for(const char *name:{"one","two","dead"}){
		TxdTex tex; TxdFromRgba(name,rgba,32,32,true,tex); original.tex.push_back(tex);
	}
	std::vector<uint8_t> old,part1,part2; std::string error;
	TxdWrite(original,old,error); TxdFile part=original; part.tex={original.tex[0]}; TxdWrite(part,part1,error);
	part.tex={original.tex[1]}; TxdWrite(part,part2,error);
	std::vector<uint8_t> dff(1024*1024+37); for(size_t i=0;i<dff.size();i++) dff[i]=(uint8_t)(i*13+7);
	int totalFailures=0;
	for(int mode=0;mode<6;mode++){
		bool ver2=mode!=1;
		std::string label=mode==0 ? "ver2":mode==1 ? "ver1":mode==2 ? "rollback":mode==3 ? "stale":mode==4 ? "inside-backup":"integration";
		std::string root=joinPath(base,"game-"+label),backup=joinPath(base,"backup-"+label);
		EnsureDir(root); EnsureDir(backup);
		std::string img=joinPath(root,"test.img"),dir=joinPath(root,"test.dir"),ide=joinPath(root,"test.ide"),loose=joinPath(root,"old.txd"),newLoose=joinPath(root,"loose_new.txd");
		std::vector<uint8_t> catalog(3*32,0),archive;
		std::vector<std::vector<uint8_t>> payload={old,part1,dff};
		const char *names[]={"old.txd","unused.txd","keep.dff"}; uint32_t sector=4,keepOffset=0;
		for(int i=0;i<3;i++){
			uint32_t sectors=(uint32_t)((payload[(size_t)i].size()+2047)/2048); uint8_t *rec=catalog.data()+i*32;
			put32(rec,sector); if(ver2){rec[4]=(uint8_t)sectors;rec[5]=(uint8_t)(sectors>>8);}else put32(rec+4,sectors);
			memcpy(rec+8,names[i],strlen(names[i])); if(i==2) keepOffset=sector;
			archive.resize((size_t)(sector+sectors+3)*2048,0); memcpy(archive.data()+(size_t)sector*2048,payload[(size_t)i].data(),payload[(size_t)i].size()); sector+=sectors+3;
		}
		if(ver2){memcpy(archive.data(),"VER2",4);put32(archive.data()+4,3);memcpy(archive.data()+8,catalog.data(),catalog.size());}
		else projectTxdWrite(dir,catalog);
		projectTxdWrite(img,archive);
		std::vector<uint8_t> oldIde=text("objs\n1000, one, old, 1, 100, 0\n1001, two, old, 1, 100, 0\nend\n");
		projectTxdWrite(ide,oldIde); projectTxdWrite(loose,old);
		std::string stage=joinPath(backup,"staged"); EnsureDir(stage);
		std::string a=joinPath(stage,"old_1.txd"),b=joinPath(stage,"old_2.txd"),c=joinPath(stage,"loose_new.txd"),i=joinPath(stage,"test.ide");
		projectTxdWrite(a,part1);projectTxdWrite(b,part2);projectTxdWrite(c,part1);
		std::vector<uint8_t> newIde=text("objs\n1000, one, old_1, 1, 100, 0\n1001, two, old_2, 1, 100, 0\nend\n"); projectTxdWrite(i,newIde);
		ProjectTxdRequest request; request.root=root;request.backup=backup;request.newTxd=3;request.removedTxd=2;request.ideFiles=1;
		request.removedNames={"old.txd","unused.txd"};
		ProjectTxdArchive pack;pack.path=img;pack.ver2=ver2;pack.added={{"old_1.txd",a},{"old_2.txd",b}};request.archives.push_back(pack);
		ProjectTxdChange change;change.path=ide;change.staged=i;request.files.push_back(change);
		change=ProjectTxdChange();change.path=loose;change.remove=true;request.files.push_back(change);
		change=ProjectTxdChange();change.path=newLoose;change.staged=c;request.files.push_back(change);
		ProjectTxdCheck check;check.path=ide;check.size=oldIde.size();check.hash=hash(oldIde);request.checks.push_back(check);
		check.path=img;check.offset=(uint64_t)keepOffset*2048;check.size=dff.size();check.hash=hash(dff);request.checks.push_back(check);
		if(mode==3) request.checks[0].hash++;
		if(mode==4){request.backup=joinPath(root,"backup");EnsureDir(request.backup);}
		const char *previousFail=getenv("GTACHECK_PROJECTTXD_FAIL");std::string previous=previousFail ? previousFail:"";
		if(mode==2) _putenv_s("GTACHECK_PROJECTTXD_FAIL","3");
		ProjectTxdResult result;
		if(mode==5){
			GameData fixture; fixture.root=root; fixture.game=GAME_SA; fixture.ideFiles={"test.ide"}; int oldSlot=fixture.txdSlotOf("old"),unusedSlot=fixture.txdSlotOf("unused");
			Archive archiveInfo;archiveInfo.phys=img;archiveInfo.logical="test.img";archiveInfo.ver2=true;fixture.archives.push_back(archiveInfo);
			for(int index=0;index<3;index++){
				Entry entry;entry.name=names[index];entry.base=stem(entry.name);entry.kind=index<2 ? EK_TXD:EK_DFF;entry.img=0;entry.dirIndex=index;
				entry.offset=(uint32_t)catalog[index*32]|(uint32_t)catalog[index*32+1]<<8|(uint32_t)catalog[index*32+2]<<16|(uint32_t)catalog[index*32+3]<<24;
				entry.sizeSectors=(uint32_t)catalog[index*32+4]|(uint32_t)catalog[index*32+5]<<8;
				if(index==0){entry.img=-1;entry.loose=loose;}
				fixture.entryByName[entry.name]=index;fixture.entries.push_back(entry);
			}
			fixture.txdSlots[(size_t)oldSlot].entry=0;fixture.txdSlots[(size_t)oldSlot].fromIde=true;fixture.txdSlots[(size_t)oldSlot].users=2;fixture.txdSlots[(size_t)unusedSlot].entry=1;
			for(int index=0;index<2;index++){
				ObjDef obj;obj.id=1000+index;obj.name=index ? "two":"one";obj.txd="old";obj.txdSlot=oldSlot;obj.file=".\\test.ide";obj.line=2+index;obj.dffParsed=true;obj.dffEntry=2;obj.dffTex={obj.name};fixture.objs.push_back(obj);fixture.objByName[obj.name]=index;
			}
			GameData *previousData=gData;ProjectTxdState previousState=std::move(gProjectTxd);gData=&fixture;
			projectTxdOpen();while(gProjectTxd.scanning) projectTxdScanStep();
			if(projectTxdPrepareIde()){
				newIde=gProjectTxd.ide.begin()->second;gProjectTxd.dir=backup;gProjectTxd.exporting=true;gProjectTxd.cursor=0;
				while(gProjectTxd.exporting) projectTxdExportStep();
				if(gProjectTxd.worker.valid()) result=gProjectTxd.worker.get();else result.message=gProjectTxd.status;
			}
			gData=previousData;gProjectTxd=std::move(previousState);
		}else ApplyProjectTxd(request,result);
		_putenv_s("GTACHECK_PROJECTTXD_FAIL",previous.c_str());
		int failures=0;std::vector<uint8_t> got;
		if(mode==5){
			if(!result.ok || fileExists(loose) || !fileExists(joinPath(root,"old_1.txd")) || !fileExists(joinPath(root,"old_2.txd"))) failures++;
			if(!readFile(ide,got) || got!=newIde) failures++;
			if(!readFile(img,got) || got.size()<40 || memcmp(got.data()+16,"keep.dff",8)) failures++;
			if(!readFile(joinPath(backup,"original/test.img"),got) || got!=archive) failures++;
			if(!readFile(joinPath(backup,"original/old.txd"),got) || got!=old) failures++;
		}else if(mode<2){
			if(!result.ok || result.savedBytes<=0 || fileExists(loose) || !fileExists(newLoose)) failures++;
			if(!readFile(ide,got) || got!=newIde) failures++;
			if(!readFile(img,got) || got.size()>=archive.size()) failures++;
			std::vector<uint8_t> outputDir;
			if(ver2){if(got.size()<104 || memcmp(got.data(),"VER2",4)) failures++;else outputDir.assign(got.begin()+8,got.begin()+104);}
			else if(!readFile(dir,outputDir) || outputDir.size()!=96) failures++;
			auto get32=[](const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;};
			if(outputDir.size()==96){
				std::set<std::string> found;
				for(int rec=0;rec<3;rec++){
					uint8_t *r=outputDir.data()+rec*32;std::string name((char*)r+8);found.insert(name);size_t off=(size_t)get32(r)*2048;
					const std::vector<uint8_t> &expected=name=="keep.dff" ? dff:name=="old_1.txd" ? part1:part2;
					if(off+expected.size()>got.size() || memcmp(got.data()+off,expected.data(),expected.size())) failures++;
				}
				if(found!=std::set<std::string>{"keep.dff","old_1.txd","old_2.txd"}) failures++;
			}
			if(!readFile(joinPath(backup,"original/test.img"),got) || got!=archive) failures++;
			if(!readFile(joinPath(backup,"original/test.ide"),got) || got!=oldIde) failures++;
			if(!readFile(joinPath(backup,"original/old.txd"),got) || got!=old) failures++;
			if(!fileExists(joinPath(backup,"Restore.ps1"))) failures++;
			if(!readFile(joinPath(backup,"Restore-All.bat"),got) || std::string(got.begin(),got.end()).find("Restore.ps1")==std::string::npos) failures++;
			if(!ver2 && (!readFile(joinPath(backup,"original/test.dir"),got) || got!=catalog)) failures++;
		}else{
			if(result.ok || (mode==2 && !result.restored)) failures++;
			if(!readFile(img,got) || got!=archive || !readFile(ide,got) || got!=oldIde || !readFile(loose,got) || got!=old || fileExists(newLoose)) failures++;
		}
		totalFailures+=failures;mvChkWrite(fmt("project TXD transaction %s: ok %d restored %d saved %lld failures %d | %s\n",label.c_str(),result.ok,result.restored,(long long)result.savedBytes,failures,result.message.c_str()).c_str());
	}
	mvChkWrite(fmt("project TXD transaction total: failures %d\n",totalFailures).c_str());
}
