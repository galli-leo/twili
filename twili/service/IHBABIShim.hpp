#pragma once

#include<libtransistor/cpp/types.hpp>
#include<libtransistor/cpp/ipcserver.hpp>
#include<libtransistor/loader_config.h>

namespace twili {

namespace process {
class MonitoredProcess;
}

namespace service {

class IHBABIShim : public trn::ipc::server::Object {
 public:
	IHBABIShim(trn::ipc::server::IPCServer *server, std::shared_ptr<process::MonitoredProcess> process);
	
	virtual trn::ResultCode Dispatch(trn::ipc::Message msg, uint32_t request_id);
	trn::ResultCode GetProcessHandle(trn::ipc::OutHandle<handle_t, trn::ipc::copy> out);
	trn::ResultCode GetLoaderConfigEntryCount(trn::ipc::OutRaw<uint32_t> out);
	trn::ResultCode GetLoaderConfigEntries(trn::ipc::Buffer<loader_config_entry_t, 0x6, 0> buffer);
	trn::ResultCode GetLoaderConfigHandle(trn::ipc::InRaw<uint32_t> placeholder, trn::ipc::OutHandle<handle_t, trn::ipc::copy> out);
	trn::ResultCode SetNextLoadPath(trn::ipc::Buffer<uint8_t, 0x5, 0> path, trn::ipc::Buffer<uint8_t, 0x5, 0> argv);
	trn::ResultCode GetTargetEntryPoint(trn::ipc::OutRaw<uint64_t> out);
	trn::ResultCode SetExitCode(trn::ipc::InRaw<uint32_t> code);
 private:
	std::shared_ptr<process::MonitoredProcess> process;
	std::vector<loader_config_entry_t> entries;
	std::vector<trn::KObject*> handles;
};

} // namespace service
} // namespace twili
