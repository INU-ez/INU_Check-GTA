#pragma once
#include "gtacheck.h"
#include <functional>

namespace gc {
struct ProjectTxdChange { std::string path, staged; bool remove = false; };
struct ProjectTxdArchive {
	std::string path; bool ver2 = false;
	std::map<std::string,std::string> added; // entry name -> staged TXD
};
struct ProjectTxdCheck { std::string path; uint64_t offset=0, size=0, hash=0; };
struct ProjectTxdRequest {
	std::string root, backup;
	std::set<std::string> removedNames;
	std::vector<ProjectTxdArchive> archives;
	std::vector<ProjectTxdChange> files;
	std::vector<ProjectTxdCheck> checks;
	int newTxd=0, removedTxd=0, ideFiles=0;
};
struct ProjectTxdResult {
	bool ok=false, restored=false;
	std::string message;
	int64_t savedBytes=0;
};
// Progress phases: 0 prepare, 1 backup, 2 repack, 3 apply, 4 restore.
bool ApplyProjectTxd(const ProjectTxdRequest &request, ProjectTxdResult &result,
	const std::function<void(int,int,int)> &progress = {});
bool ProjectTxdBackupOutside(const std::string &root, const std::string &backup);
}
