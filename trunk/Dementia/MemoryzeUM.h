#pragma once

#include "IEvasionMethodPlugin.h"
#include "../common/SharedMemory.h"

class MemoryzeUM : public IEvasionMethodPlugin
{
public:
	MemoryzeUM(int ID);
	~MemoryzeUM(void);

	virtual bool ParseArguments(std::string args);
	virtual bool Execute(void);

private:
	// name of the process that should be hidden
	TCharString m_targetProcessName;
	TCharString m_memoryzeProcessName;
	TCharString m_DLLName;
	boost::scoped_ptr<SharedMemory<SHARED_BLOCK>> m_sharedMemory;
	boost::scoped_ptr<SharedMemory<TARGET_PROCESS_BLOCK>> m_sharedMemoryTarget;
	int m_processScanInterval;
	int m_rescan;
};
