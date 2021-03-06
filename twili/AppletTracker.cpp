//
// Twili - Homebrew debug monitor for the Nintendo Switch
// Copyright (C) 2018 misson20000 <xenotoad@xenotoad.net>
//
// This file is part of Twili.
//
// Twili is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Twili is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Twili.  If not, see <http://www.gnu.org/licenses/>.
//

#include "AppletTracker.hpp"

#include "twili.hpp"
#include "process/AppletProcess.hpp"
#include "process/fs/ActualFile.hpp"

#include "err.hpp"
#include "applet_shim.hpp"

namespace twili {

AppletTracker::AppletTracker(Twili &twili) :
	twili(twili),
	process_queued_wevent(process_queued_event),
	monitor(*this) {
	printf("building AppletTracker\n");
	hbmenu_nro = std::make_shared<process::fs::ActualFile>("/sd/hbmenu.nro");
}

bool AppletTracker::HasControlProcess() {
	return has_control_process;
}

void AppletTracker::AttachControlProcess() {
	if(has_control_process) {
		throw trn::ResultError(TWILI_ERR_APPLET_TRACKER_INVALID_STATE);
	}
	process_queued_wevent.Signal(); // for hbmenu launch
	has_control_process = true;
}

void AppletTracker::ReleaseControlProcess() {
	if(!has_control_process) {
		throw trn::ResultError(TWILI_ERR_APPLET_TRACKER_INVALID_STATE);
	}
	has_control_process = false;

	printf("lost control applet. invalidating created processes...\n");
	for(std::shared_ptr<process::AppletProcess> process : created) {
		process->ChangeState(process::MonitoredProcess::State::Exited);
	}
	if(hbmenu) {
		std::shared_ptr<process::AppletProcess> hbmenu_copy = hbmenu;
		hbmenu_copy->Terminate();
		hbmenu_copy->ChangeState(process::MonitoredProcess::State::Exited);
	}
	created.clear();
}

const trn::KEvent &AppletTracker::GetProcessQueuedEvent() {
	return process_queued_event;
}

bool AppletTracker::ReadyToLaunch() {
	return
		created.size() == 0 && // there are no processes created but not yet attached
		(!monitor.process || // either there is no applet currently running
		 monitor.process->GetState() == process::MonitoredProcess::State::Exited); // or it has exited
}

std::shared_ptr<process::AppletProcess> AppletTracker::PopQueuedProcess() {
	while(queued.size() > 0) {
		std::shared_ptr<process::AppletProcess> proc = queued.front();
		created.push_back(proc);
		queued.pop_front();

		// returns true if process wasn't cancelled,
		// but if it returns false, attempt to dequeue
		// another process
		if(proc->PrepareForLaunch()) {
			return proc;
		}
	}
	
	// launch hbmenu if there's nothing else to launch
	printf("launching hbmenu\n");
	hbmenu = CreateHbmenu();
	created.push_back(hbmenu);
	if(!hbmenu->PrepareForLaunch()) {
		// hbmenu shouldn't be able to be cancelled yet...
		throw trn::ResultError(TWILI_ERR_APPLET_TRACKER_INVALID_STATE);
	}
	
	return hbmenu;
}

std::shared_ptr<process::AppletProcess> AppletTracker::AttachHostProcess(trn::KProcess &&process) {
	printf("attaching new host process\n");
	if(created.size() == 0) {
		printf("  no processes created\n");
		throw trn::ResultError(TWILI_ERR_APPLET_TRACKER_NO_PROCESS);
	}
	std::shared_ptr<process::AppletProcess> proc = created.front();
	proc->Attach(std::make_shared<trn::KProcess>(std::move(process)));
	printf("  attached\n");
	created.pop_front();
	monitor.Reattach(proc);
	if(ReadyToLaunch()) {
		process_queued_wevent.Signal();
	}
	return proc;
}

void AppletTracker::QueueLaunch(std::shared_ptr<process::AppletProcess> process) {
	if(hbmenu) {
		// exit out of hbmenu, if it's running, before launching
		hbmenu->Kill();
	}
	queued.push_back(process);
	if(ReadyToLaunch()) {
		process_queued_wevent.Signal();
	}
}

std::shared_ptr<process::AppletProcess> AppletTracker::CreateHbmenu() {
	std::shared_ptr<process::AppletProcess> proc = std::make_shared<process::AppletProcess>(twili);
	// note: we skip over the Started state through this non-standard launch procedure
	proc->AppendCode(hbmenu_nro);
	proc->argv = "sdmc:/hbmenu.nro";
	return proc;
}

void AppletTracker::HBLLoad(std::string path, std::string argv) {
	// transmute sdmc:/switch/application.nro path to /sd/switch/application.nro
	const std::string *prefix = nullptr;
	const std::string sdmc_prefix("sdmc:/");
	if(!path.compare(0, sdmc_prefix.size(), sdmc_prefix)) {
		prefix = &sdmc_prefix;
	}
	char transmute_path[0x301];
	snprintf(transmute_path, sizeof(transmute_path), "/sd/%s", path.c_str() + (prefix == nullptr ? 0 : prefix->size()));

	FILE *file = fopen(transmute_path, "rb");
	if(!file) {
		printf("  failed to open\n");
		return;
	}
	
	std::shared_ptr<process::AppletProcess> next_proc = std::make_shared<process::AppletProcess>(twili);
	next_proc->AppendCode(std::make_shared<process::fs::ActualFile>(file));
	next_proc->argv = argv;
	
	printf("prepared hbl next load process. queueing...\n");
	QueueLaunch(next_proc);
}

void AppletTracker::PrintDebugInfo() {
	printf("AppletTracker debug:\n");
	printf("  has_control_process: %d\n", has_control_process);
	printf("  hbmenu: %p\n", hbmenu.get());
	if(hbmenu) {
		printf("    pid: 0x%lx\n", hbmenu->GetPid());
	}
	printf("  monitor.process: %p\n", monitor.process.get());
	if(monitor.process) {
		printf("    pid: 0x%lx\n", monitor.process->GetPid());
	}

	printf("  queued:\n");
	for(auto proc : queued) {
		printf("    - %p\n", proc.get());
		printf("      type: %s\n", typeid(*proc.get()).name());
		printf("      pid: 0x%lx\n", proc->GetPid());
		printf("      state: %d\n", proc->GetState());
		printf("      result: 0x%x\n", proc->GetResult().code);
		printf("      target entry: 0x%lx\n", proc->GetTargetEntry());
	}
	printf("  created:\n");
	for(auto proc : created) {
		printf("    - %p\n", proc.get());
		printf("      type: %s\n", typeid(*proc.get()).name());
		printf("      pid: 0x%lx\n", proc->GetPid());
		printf("      state: %d\n", proc->GetState());
		printf("      result: 0x%x\n", proc->GetResult().code);
		printf("      target entry: 0x%lx\n", proc->GetTargetEntry());
	}

}


AppletTracker::Monitor::Monitor(AppletTracker &tracker) : process::ProcessMonitor(std::shared_ptr<process::MonitoredProcess>()), tracker(tracker) {
}

void AppletTracker::Monitor::StateChanged(process::MonitoredProcess::State new_state) {
	if(tracker.ReadyToLaunch()) {
		tracker.process_queued_wevent.Signal();
	}
	if(new_state == process::MonitoredProcess::State::Exited) {
		if(process == tracker.hbmenu) {
			printf("AppletTracker: hbmenu exited\n");
			tracker.hbmenu.reset();
		}
		if(!process->next_load_path.empty()) {
			printf("AppletTracker: application requested next load: %s[%s]\n", process->next_load_path.c_str(), process->next_load_argv.c_str());
			tracker.HBLLoad(process->next_load_path, process->next_load_argv);
		}
		// we are no longer interested in monitoring the applet after it exits,
		// and we need to clear this reference pretty fast anyway since libnx
		// applets like to hog memory.
		Reattach(std::shared_ptr<process::MonitoredProcess>());
	}
}

} // namespace twili
