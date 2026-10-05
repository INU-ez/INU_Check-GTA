// Project TXD transaction: stage and verify complete archives, back up every
// changed file, then replace. No GameData or renderer access on this worker.
#include "project_txd_apply.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <io.h>

namespace gc {
namespace {
namespace fs = std::filesystem;
static std::string canonical(const std::string &path)
{
	std::error_code ec; auto p=fs::weakly_canonical(fs::absolute(fs::u8path(path),ec),ec);
	return ec ? std::string() : p.u8string();
}
static std::string pathKey(const std::string &path)
{
	std::string p=lower(normSlashes(path)); while(p.size()>3 && p.back()=='/') p.pop_back(); return p;
}
static bool inside(const std::string &root,const std::string &path)
{
	std::string r=pathKey(root),p=pathKey(path); if(!r.empty() && r.back()!='/') r+='/';
	return !r.empty() && p.size()>r.size() && p.compare(0,r.size(),r)==0;
}
static std::string relativeTo(const std::string &root,const std::string &path)
{
	// Windows path comparisons ignore case; filesystem::lexically_relative does not.
	std::string r=normSlashes(root); while(r.size()>3 && r.back()=='/') r.pop_back(); if(r.back()!='/') r+='/';
	return inside(root,path) ? normSlashes(path).substr(r.size()):std::string();
}
static bool dirs(const std::string &path)
{
	std::error_code ec; fs::create_directories(fs::u8path(path),ec); return !ec && fs::is_directory(fs::u8path(path),ec);
}
static bool copy(const std::string &from,const std::string &to,bool fresh=false)
{
	if(!dirs(fs::u8path(to).parent_path().u8string())) return false;
	return CopyFileW(fs::u8path(from).c_str(),fs::u8path(to).c_str(),fresh ? TRUE:FALSE)!=0;
}
static bool erase(const std::string &path) { return DeleteFileW(fs::u8path(path).c_str())!=0; }
static uint64_t updateHash(uint64_t h,const uint8_t *p,size_t n)
{
	for(size_t i=0;i<n;i++){ h^=p[i]; h*=1099511628211ull; } return h;
}
static bool hashRange(const std::string &path,uint64_t offset,uint64_t size,uint64_t &h)
{
	FILE *f=OpenReadUtf8(path); if(!f) return false;
	bool ok=_fseeki64(f,(long long)offset,SEEK_SET)==0; h=14695981039346656037ull;
	std::vector<uint8_t> buf(1024*1024);
	while(ok && size){ size_t n=(size_t)(std::min)(size,(uint64_t)buf.size()); ok=fread(buf.data(),1,n,f)==n; if(ok) h=updateHash(h,buf.data(),n); size-=n; }
	fclose(f); return ok;
}
static bool fileHash(const std::string &path,uint64_t &h,uint64_t &size)
{
	std::error_code ec; size=fs::file_size(fs::u8path(path),ec); return !ec && hashRange(path,0,size,h);
}
static bool sameFile(const std::string &path,uint64_t hash,uint64_t size)
{
	uint64_t h=0,n=0; return fileHash(path,h,n) && h==hash && n==size;
}
static bool write(const std::string &path,const std::vector<uint8_t> &data)
{
	if(!dirs(fs::u8path(path).parent_path().u8string())) return false;
	FILE *f=_wfopen(fs::u8path(path).c_str(),L"wb"); if(!f) return false;
	bool ok=data.empty() || fwrite(data.data(),1,data.size(),f)==data.size();
	if(fflush(f)!=0 || _commit(_fileno(f))!=0) ok=false; if(fclose(f)!=0) ok=false; return ok;
}
static bool writeText(const std::string &path,const std::string &text) { return write(path,std::vector<uint8_t>(text.begin(),text.end())); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p,uint32_t v) { for(int i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void put16(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
struct Record { uint8_t raw[32]={}; uint64_t from=0,sectors=0,to=0; std::string staged; };
struct Change {
	std::string path,relative,backup,staged;
	bool existed=false,remove=false,touched=false;
	uint64_t hash=0,size=0,newHash=0,newSize=0;
};
static bool readDirectory(const ProjectTxdArchive &a,std::vector<uint8_t> &dir,uint64_t &imgSize)
{
	std::error_code ec; imgSize=fs::file_size(fs::u8path(a.path),ec); if(ec) return false;
	if(a.ver2){
		FILE *f=OpenReadUtf8(a.path); if(!f) return false; uint8_t head[8];
		bool ok=fread(head,1,8,f)==8 && memcmp(head,"VER2",4)==0;
		uint32_t n=ok ? u32(head+4):0; if(n>1000000 || 8ull+32ull*n>imgSize) ok=false;
		if(ok){ dir.resize((size_t)n*32); ok=dir.empty() || fread(dir.data(),1,dir.size(),f)==dir.size(); }
		fclose(f); if(!ok) return false;
	}else if(!readFile(a.path.substr(0,a.path.size()-4)+".dir",dir) || dir.size()%32 || dir.size()>32000000) return false;
	return true;
}
static bool records(const ProjectTxdArchive &a,std::vector<Record> &out,uint64_t &imgSize,std::string &err)
{
	std::vector<uint8_t> dir;if(!readDirectory(a,dir,imgSize)) return false;
	uint64_t header=a.ver2 ? (8+dir.size()+2047)/2048:0;
	std::vector<std::pair<uint64_t,uint64_t>> ranges;
	for(size_t i=0;i<dir.size();i+=32){
		Record r; memcpy(r.raw,dir.data()+i,32); r.from=u32(r.raw);
		uint32_t stream=a.ver2 ? (uint32_t)r.raw[4]|(uint32_t)r.raw[5]<<8:u32(r.raw+4);
		uint32_t stored=a.ver2 ? (uint32_t)r.raw[6]|(uint32_t)r.raw[7]<<8:0;
		if(stored && stream && stored!=stream){ err=T("Сжатые записи IMG не поддерживаются — архив не изменён."); return false; }
		r.sectors=stream ? stream:stored;
		if(!memchr(r.raw+8,0,24) || !r.raw[8] || (r.sectors && r.from<header) || (r.from+r.sectors)*2048>imgSize) return false;
		if(r.sectors) ranges.push_back({r.from,r.from+r.sectors}); out.push_back(r);
	}
	std::sort(ranges.begin(),ranges.end());
	for(size_t i=1;i<ranges.size();i++) if(ranges[i].first<ranges[i-1].second){ err=T("Перекрывающиеся записи IMG — сначала исправь архив."); return false; }
	return true;
}
static bool repack(const ProjectTxdArchive &a,const std::set<std::string> &removed,
	const std::string &output,const std::string &dirOutput,bool &changed,std::string &err)
{
	std::vector<Record> original,kept; uint64_t size=0;
	if(!records(a,original,size,err)){ if(err.empty()) err=T("Каталог IMG повреждён или не прочитан."); return false; }
	for(const Record &r:original){
		std::string name((const char*)r.raw+8);
		if(removed.count(lower(name))) changed=true; else kept.push_back(r);
	}
	for(const auto &add:a.added){
		for(const Record &r:kept) if(lower((const char*)r.raw+8)==lower(add.first)) return false;
		std::vector<uint8_t> bytes; TxdFile txd;
		if(add.first.size()>23 || !readFile(add.second,bytes) || !TxdParse(bytes,txd) || !txd.ok) return false;
		Record r; r.staged=add.second; r.sectors=(bytes.size()+2047)/2048;
		if(a.ver2 && r.sectors>65535) return false;
		if(a.ver2) put16(r.raw+4,(uint32_t)r.sectors); else put32(r.raw+4,(uint32_t)r.sectors);
		memcpy(r.raw+8,add.first.c_str(),add.first.size()); kept.push_back(r); changed=true;
	}
	if(!changed) return true;
	if(kept.size()>1000000) return false;
	uint64_t sector=a.ver2 ? (8+kept.size()*32+2047)/2048:0;
	std::vector<uint8_t> dir(kept.size()*32);
	for(size_t i=0;i<kept.size();i++){
		Record &r=kept[i]; if(sector+r.sectors>UINT32_MAX) return false;
		r.to=sector; put32(r.raw,(uint32_t)sector); memcpy(dir.data()+i*32,r.raw,32); sector+=r.sectors;
	}
	if(!dirs(fs::u8path(output).parent_path().u8string())) return false;
	FILE *src=OpenReadUtf8(a.path),*dst=_wfopen(fs::u8path(output).c_str(),L"wb");
	if(!src || !dst){ if(src) fclose(src); if(dst) fclose(dst); return false; }
	bool ok=true; std::vector<uint8_t> buf(1024*1024),zero(2048,0);
	if(a.ver2){
		std::vector<uint8_t> head((size_t)(kept.empty() ? 1:kept.front().to)*2048,0);
		memcpy(head.data(),"VER2",4); put32(head.data()+4,(uint32_t)kept.size());
		if(!dir.empty()) memcpy(head.data()+8,dir.data(),dir.size()); ok=fwrite(head.data(),1,head.size(),dst)==head.size();
	}
	std::vector<uint64_t> hashes;
	for(const Record &r:kept){
		FILE *input=r.staged.empty() ? src:OpenReadUtf8(r.staged);
		uint64_t bytes=r.sectors*2048,h=14695981039346656037ull;
		if(!input){ ok=false; break; }
		uint64_t content=bytes;
		if(!r.staged.empty()){ std::error_code ec; content=fs::file_size(fs::u8path(r.staged),ec); if(ec || content>bytes) ok=false; }
		else if(_fseeki64(input,(long long)r.from*2048,SEEK_SET)!=0) ok=false;
		while(ok && content){ size_t n=(size_t)(std::min)(content,(uint64_t)buf.size());
			ok=fread(buf.data(),1,n,input)==n && fwrite(buf.data(),1,n,dst)==n;
			if(ok) h=updateHash(h,buf.data(),n); content-=n; bytes-=n;
		}
		while(ok && bytes){ size_t n=(size_t)(std::min)(bytes,(uint64_t)zero.size()); ok=fwrite(zero.data(),1,n,dst)==n; if(ok) h=updateHash(h,zero.data(),n); bytes-=n; }
		if(input!=src) fclose(input); hashes.push_back(h); if(!ok) break;
	}
	fclose(src); if(fflush(dst)!=0 || _commit(_fileno(dst))!=0) ok=false; if(fclose(dst)!=0) ok=false;
	if(ok && !a.ver2) ok=write(dirOutput,dir);
	// Re-read every copied block, including sector padding; do not trust a successful fwrite.
	for(size_t i=0;ok && i<kept.size();i++){ uint64_t h=0; ok=hashRange(output,kept[i].to*2048,kept[i].sectors*2048,h) && h==hashes[i]; }
	if(ok){ ProjectTxdArchive check; check.path=output; check.ver2=a.ver2;
		if(!a.ver2 && dirOutput!=output.substr(0,output.size()-4)+".dir") ok=false;
		std::vector<Record> reread; uint64_t n=0; std::string message;
		ok=ok && records(check,reread,n,message) && reread.size()==kept.size() && n==sector*2048;
		for(size_t i=0;ok && i<kept.size();i++) ok=memcmp(reread[i].raw,kept[i].raw,32)==0;
	}
	return ok;
}
static bool replace(const std::string &from,const std::string &to,bool existed)
{
	// Copy across volumes first; rename a complete file on the target volume.
	std::string temp=to+".inu_txd_"+std::to_string(GetCurrentProcessId())+".tmp";
	if(fileExists(temp)) return false;
	if(!copy(from,temp,true)){ DWORD error=GetLastError(); if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS && fileExists(temp)) erase(temp); return false; }
	bool ok=MoveFileExW(fs::u8path(temp).c_str(),fs::u8path(to).c_str(),MOVEFILE_WRITE_THROUGH|(existed ? MOVEFILE_REPLACE_EXISTING:0))!=0;
	if(!ok) erase(temp); return ok;
}
static bool restore(std::vector<Change> &changes)
{
	bool ok=true;
	for(auto it=changes.rbegin();it!=changes.rend();++it) if(it->touched){
		if(it->existed){ if(!replace(it->backup,it->path,true)) ok=false; }
		else if(fileExists(it->path) && !erase(it->path)) ok=false;
	} return ok;
}
}
bool ProjectTxdBackupOutside(const std::string &root,const std::string &backup)
{
	std::string r=canonical(root),b=canonical(backup);
	return !r.empty() && !b.empty() && pathKey(r)!=pathKey(b) && !inside(r,b);
}
bool ApplyProjectTxd(const ProjectTxdRequest &request,ProjectTxdResult &result,const std::function<void(int,int,int)> &progress)
{
	std::vector<Change> changes; bool committing=false;
	auto report=[&](int phase,int done,int total){ if(progress) progress(phase,done,total); };
	try {
		std::string root=canonical(request.root),backup=canonical(request.backup);
		if(!ProjectTxdBackupOutside(root,backup)){ result.message=T("Папка бэкапа должна находиться вне сборки."); return false; }
		auto addChange=[&](const std::string &path,const std::string &staged,bool remove)->bool{
			std::string actual=canonical(path); if(!inside(root,actual) || actual.find_first_of("\t\r\n")!=std::string::npos) return false;
			if(!staged.empty() && !inside(backup,canonical(staged))) return false;
			for(const Change &c:changes) if(pathKey(c.path)==pathKey(actual)) return c.remove==remove && c.staged==staged;
			Change c; c.path=actual; c.staged=staged; c.remove=remove;
			c.relative=relativeTo(root,actual);
			if(c.relative.empty() || c.relative.find("..")!=std::string::npos) return false;
			c.backup=joinPath(joinPath(backup,"original"),c.relative); c.existed=fileExists(actual);
			if(remove && !c.existed) return false;
			if(c.existed && !fileHash(actual,c.hash,c.size)) return false;
			changes.push_back(c); return true;
		};
		for(const ProjectTxdCheck &c:request.checks){ uint64_t h=0;
			if(!hashRange(c.path,c.offset,c.size,h) || h!=c.hash){ result.message=T("Файлы сборки изменились. Построй план заново."); return false; }
		}
		for(const auto &f:request.files) if(!addChange(f.path,f.staged,f.remove)) throw std::runtime_error("file plan");
		// Determine changed archives from the raw directories, including shadowed TXD copies.
		std::vector<ProjectTxdArchive> archives;
		for(const auto &a:request.archives){
			std::vector<uint8_t> raw;uint64_t n=0;std::string error;
			if(!readDirectory(a,raw,n)){result.message=T("Каталог IMG повреждён или не прочитан.");return false;}
			bool changed=!a.added.empty();
			for(size_t i=0;i<raw.size();i+=32){const char *name=(const char*)raw.data()+i+8;size_t len=0;while(len<24 && name[len]) len++;if(request.removedNames.count(lower(std::string(name,len)))) changed=true;}
			if(!changed) continue;
			std::vector<Record> rec;
			if(!inside(root,canonical(a.path)) || !records(a,rec,n,error)){ result.message=error.empty() ? T("Каталог IMG повреждён или не прочитан."):error; return false; }
			std::string actual=canonical(a.path),relative=relativeTo(root,actual); bool merged=false;
			for(auto &previous:archives) if(pathKey(canonical(previous.path))==pathKey(actual)){
				if(previous.ver2!=a.ver2) throw std::runtime_error("conflicting archive format");
				for(const auto &add:a.added){ auto found=previous.added.find(add.first); if(found!=previous.added.end() && found->second!=add.second) throw std::runtime_error("conflicting archive entry"); previous.added[add.first]=add.second; }
				merged=true; break;
			}
			if(merged) continue; archives.push_back(a);
			if(!addChange(actual,joinPath(joinPath(backup,"prepared"),relative),false)) throw std::runtime_error("archive plan");
			if(!a.ver2){ std::string path=actual.substr(0,actual.size()-4)+".dir";
				if(!addChange(path,joinPath(joinPath(backup,"prepared"),relative.substr(0,relative.size()-4)+".dir"),false)) throw std::runtime_error("directory plan"); }
		}
		if(changes.empty()){ result.message=T("Нет изменений для применения."); return false; }
		for(size_t i=0;i<changes.size();i++){
			report(1,(int)i,(int)changes.size()); Change &c=changes[i];
			if(c.existed && (!copy(c.path,c.backup,true) || !sameFile(c.backup,c.hash,c.size) || !sameFile(c.path,c.hash,c.size))) throw std::runtime_error("backup");
		}
		for(size_t i=0;i<archives.size();i++){
			report(2,(int)i,(int)archives.size()); ProjectTxdArchive a=archives[i];
			std::string relative=relativeTo(root,canonical(a.path));
			a.path=joinPath(joinPath(backup,"original"),relative);
			std::string out=joinPath(joinPath(backup,"prepared"),relative),err; bool changed=false;
			if(!repack(a,request.removedNames,out,out.substr(0,out.size()-4)+".dir",changed,err)){
				if(!err.empty()) result.message=err; throw std::runtime_error("repack");
			}
		}
		std::string manifest;
		for(Change &c:changes){
			if(!c.remove && !fileHash(c.staged,c.newHash,c.newSize)) throw std::runtime_error("staged file");
			result.savedBytes+=(int64_t)c.size-(int64_t)c.newSize;
			manifest+=(c.existed ? "file\t":"new\t")+c.relative+"\t"+(c.existed ? "original/"+c.relative:"-")+"\n";
		}
		const char *restoreScript=R"PS($ErrorActionPreference='Stop'
$root=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'root.txt'),[Text.Encoding]::UTF8).TrimEnd()
$root=[IO.Path]::GetFullPath($root).TrimEnd('\','/')
foreach($line in [IO.File]::ReadAllLines((Join-Path $PSScriptRoot 'restore.tsv'),[Text.Encoding]::UTF8)) {
  if(!$line){continue}
  $parts=$line.Split([char]9)
  if($parts.Count -ne 3){throw 'Invalid backup manifest'}
  $target=[IO.Path]::GetFullPath((Join-Path $root $parts[1]))
  if(!$target.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside the game folder'}
  if($parts[0] -eq 'file') {
    $source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $parts[2]))
    if(!$source.StartsWith($PSScriptRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside the backup'}
    Copy-Item -LiteralPath $source -Destination $target -Force
  } elseif($parts[0] -eq 'new') {
    if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
  } else {throw 'Invalid backup operation'}
}
Write-Output 'TXD distribution backup restored. Run the checker again.'
)PS";
		const char *restoreBatch="@echo off\r\nsetlocal\r\nset \"SCRIPT=%~dp0Restore.ps1\"\r\nif not exist \"%SCRIPT%\" (\r\n  echo Restore.ps1 was not found beside this batch file.\r\n  exit /b 2\r\n)\r\npowershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File \"%SCRIPT%\"\r\nset \"RESULT=%ERRORLEVEL%\"\r\nif not \"%RESULT%\"==\"0\" (\r\n  echo Restore failed with exit code %RESULT%.\r\n  exit /b %RESULT%\r\n)\r\necho All files listed in restore.tsv have been restored.\r\nexit /b 0\r\n";
		if(!writeText(joinPath(backup,"root.txt"),root) || !writeText(joinPath(backup,"restore.tsv"),manifest) ||
			!writeText(joinPath(backup,"Restore.ps1"),restoreScript) ||
			!writeText(joinPath(backup,"Restore-All.bat"),restoreBatch) ||
			!writeText(joinPath(backup,"README.txt"),"Original files are in original/. Close the game and INU Check, then run Restore-All.bat (or Restore.ps1) to restore all changed files and remove new TXDs.\n")) throw std::runtime_error("recovery manifest");
		// Revalidate the entire original set and model/material inputs immediately before the first write.
		for(const Change &c:changes) if(c.existed ? !sameFile(c.path,c.hash,c.size):fileExists(c.path)) throw std::runtime_error("source changed");
		for(const ProjectTxdCheck &c:request.checks){ uint64_t h=0; if(!hashRange(c.path,c.offset,c.size,h) || h!=c.hash) throw std::runtime_error("model changed"); }
		committing=true;
		for(size_t i=0;i<changes.size();i++){
			report(3,(int)i,(int)changes.size()); Change &c=changes[i];
			if(c.existed ? !sameFile(c.path,c.hash,c.size):fileExists(c.path)) throw std::runtime_error("source changed during apply");
			// A fixture-only failure point exercises rollback after some files have been installed.
			if(const char *fail=getenv("GTACHECK_PROJECTTXD_FAIL")) if(getenv("GTACHECK_HIDDEN") && (int)i==atoi(fail)) throw std::runtime_error("test failure");
			if(!(c.remove ? erase(c.path):replace(c.staged,c.path,c.existed))) throw std::runtime_error("install");
			c.touched=true; if(!c.remove && !sameFile(c.path,c.newHash,c.newSize)) throw std::runtime_error("verify installed");
		}
		result.ok=true;
		result.message=fmt(T("Готово: новых TXD — %d, удалено словарей — %d, обновлено IDE — %d."),request.newTxd,request.removedTxd,request.ideFiles);
		writeText(joinPath(backup,"result.txt"),result.message+"\n");
		// Successful staging files are disposable; retain only original files and recovery instructions.
		for(const Change &c:changes) if(!c.staged.empty()) erase(c.staged);
		for(const auto &a:request.archives) for(const auto &add:a.added) if(inside(backup,canonical(add.second))) erase(add.second);
		return true;
	}catch(const std::exception &){
		if(committing){ report(4,0,1); result.restored=restore(changes);
			result.message=result.restored ? T("Применение не завершено. Изменения отменены, сборка восстановлена."):T("Не удалось восстановить все файлы. Закрой программу и запусти Restore-All.bat из бэкапа.");
		}else if(result.message.empty()) result.message=T("Подготовка или бэкап не завершены. Сборка не изменена.");
		if(!request.backup.empty()) writeText(joinPath(request.backup,"result.txt"),result.message+"\n"); return false;
	}
}
}
