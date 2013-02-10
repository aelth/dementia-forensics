#pragma once

#include "Injector.h"

class ProcessMonitor
{
public:
	ProcessMonitor(TCharString targetProcessName, TCharString memoryzeProcessName, TCharString DLLName, int rescan, int scanInterval);
	~ProcessMonitor(void);

private:
	TCharString m_targetProcessName;
	TCharString m_memoryzeProcessName;
	TCharString m_DLLFullPath;
	boost::scoped_ptr<Injector> m_injector;
	bool m_shouldRescan;
	int m_scanInterval;
	ULONG m_processID;
	boost::thread m_monitorThread;
	std::vector<DWORD> m_injectedPIDs;

	void ScanProcessesThrdProc(void);
	std::vector<DWORD> GetMemoryzeProcesses(void);
};
