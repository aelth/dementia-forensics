#include "StdAfx.h"
#include "ProcessMonitor.h"
#include "Logger.h"

ProcessMonitor::ProcessMonitor(TCharString targetProcessName, TCharString memoryzeProcessName, TCharString DLLName, int rescan, int scanInterval)
: m_targetProcessName(targetProcessName), m_memoryzeProcessName(memoryzeProcessName)
{
	m_shouldRescan = (bool) rescan;
	m_scanInterval = scanInterval;

	// passed DLLName is not a full path, but just a DLL name - convert it to full path
	TCHAR szDLLFullPath[MAX_PATH + 1];

	// THIS CALL IS NOT THREAD-SAFE!
	// since GetFullPathName uses current directory, another thread could theoretically change current directory and thus "tamper"
	// our results - as no such thread exists currently, this code is more or less safe, but BE CAUTIOUS!
	if(!GetFullPathNameW(DLLName.c_str(), MAX_PATH + 1, szDLLFullPath, NULL))
	{
		Logger::Instance().Log(_T("Could not obtain full path name to MemoryzeUM.dll -- exiting program"), CRITICAL_ERROR);
		exit(EXIT_FAILURE);
	}

	//m_DLLFullPath = TCharString(szDLLFullPath);

	// create our DLL injector
	m_injector.reset(new Injector(szDLLFullPath));

	m_monitorThread = boost::thread(&ProcessMonitor::ScanProcessesThrdProc, this);
	// don't wait for this thread exit, continue with execution
	//m_monitorThread.join();
}
ProcessMonitor::~ProcessMonitor(void)
{
	
}

void ProcessMonitor::ScanProcessesThrdProc(void)
{
	std::vector<DWORD> listOfPIDs;

	while(true)
	{
		listOfPIDs = GetMemoryzeProcesses();

		// check if our target Memoryze process was found (single or multiple instances/PIDs - inject them all)
		if(!listOfPIDs.empty())
		{
			std::vector<DWORD>::iterator PIDIterator;

			// for each discovered PID, check if injection was already made for this process
			for(PIDIterator = listOfPIDs.begin(); PIDIterator != listOfPIDs.end(); ++PIDIterator)
			{
				DWORD dwMemoryzePID = *PIDIterator;
				// no injection was made inside the process with this PID - do injection
				if(std::find(m_injectedPIDs.begin(), m_injectedPIDs.end(), dwMemoryzePID) == m_injectedPIDs.end()) 
				{	
					// inject and add to the list of injected processes if successful
					Logger::Instance().Log(_T("Process \"") + m_memoryzeProcessName + _T("\" found! -- PID = ") + boost::lexical_cast<TCharString>(dwMemoryzePID), SUCCESS);
					if(m_injector->InjectToProcess(dwMemoryzePID))
					{
						m_injectedPIDs.push_back(dwMemoryzePID);
					}
				}
			}
			
			listOfPIDs.clear();
		}

		boost::posix_time::seconds sleepTime(m_scanInterval);
		boost::this_thread::sleep(sleepTime);

		// if no rescan specified, exit
		if(!m_shouldRescan)
		{
			break;
		}
	}
}

std::vector<DWORD> ProcessMonitor::GetMemoryzeProcesses(void)
{
	DWORD PIDArray[1024];
	DWORD dwSizeNeeded = 0;

	// return value of this function is list (vector) of process IDs where the name of the process with
	// this PID is equal to the name of the target Memoryze process
	std::vector<DWORD> listOfMemoryzePIDs;

	if(!EnumProcesses(PIDArray, sizeof(PIDArray), &dwSizeNeeded))
	{
		Logger::Instance().Log(_T("Process enumeration failed, will try again..."), WARNING);
		return listOfMemoryzePIDs;
	}
	
	// get number of processes that are currently active
	DWORD dwProcessCount = dwSizeNeeded / sizeof(DWORD);

	// get the name of each process and see if our target process is active
	for (unsigned int i = 0; i < dwProcessCount; i++)
	{
		DWORD dwProcessID = PIDArray[i];
		// skip "Idle" and "System" processes
		if(dwProcessID != 0 && dwProcessID != 4)
		{
			
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwProcessID);
			if (hProcess != NULL)
			{
				HMODULE hMod;
				// if you're running this code on a 64-bit Windows, you will get errors, since this (32-bit) program cannot enumerate the modules
				// of the 64-bit process
				if(EnumProcessModules(hProcess, &hMod, sizeof(hMod), &dwSizeNeeded))
				{
					TCHAR szProcessName[MAX_PATH] = _T("");
					if(GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName)/sizeof(TCHAR)))
					{
						// access to private variable from the separate thread is safe - this variable will never be modified once it is initialized in constructor
						if(!_tcsicmp(szProcessName, m_memoryzeProcessName.c_str()))
						{
							listOfMemoryzePIDs.push_back(dwProcessID);
						}
					}
					else if(Logger::Instance().IsDebug())
					{
						Logger::Instance().Log(_T("Could not get module base name for process with PID = ") + boost::lexical_cast<TCharString>(dwProcessID), DEBUG);
					}
				}
				else if(Logger::Instance().IsDebug())
				{
					Logger::Instance().Log(_T("Could not enumerate modules for process with PID = ") + boost::lexical_cast<TCharString>(dwProcessID), DEBUG);
				}
			}
			else if(Logger::Instance().IsDebug())
			{
				Logger::Instance().Log(_T("Could not open process with PID = ") + boost::lexical_cast<TCharString>(dwProcessID), DEBUG);
			}

			CloseHandle(hProcess);
		}
	}

	return listOfMemoryzePIDs;
}