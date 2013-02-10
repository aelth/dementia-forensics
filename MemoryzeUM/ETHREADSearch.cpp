#include "StdAfx.h"
#include "ETHREADSearch.h"

#define PROCNUM	1024

ETHREADSearch::ETHREADSearch(const std::string &targetProcessName)
{
	// initialize object name and tag
	m_szObjName = "_ETHREAD";
	m_dwTag = 0xe5726854;

	// create new symbol that represents "abstract" ETHREAD symbol
	Symbol eThread(new SymbolUDT(m_szObjName, true));
	// specify all relevant ETHREAD members
	eThread->AddWantedChildSymbol(Symbol(new SymbolUDT("Cid")));
	eThread->AddWantedChildSymbol(Symbol(new SymbolUDT("ThreadListEntry")));
	// these members will be used for obtaining ETHREAD address - only a heuristic based approach
	eThread->AddWantedChildSymbol(Symbol(new SymbolUDT("KeyedWaitChain")));
	eThread->AddWantedChildSymbol(Symbol(new SymbolUDT("ActiveTimerListHead")));

	// "child" structure of _ETHREAD is a _KTHREAD structure that also contains some interesting objects
	Symbol kThread(new SymbolUDT("_KTHREAD", true));

	// WaitBlock has ETHREAD member!
	kThread->AddWantedChildSymbol(Symbol(new SymbolUDT("WaitBlock")));

	// "child" of _KTHREAD is _KWAIT_BLOCK
	Symbol kWaitBlock(new SymbolUDT("_KWAIT_BLOCK", true));
	kWaitBlock->AddWantedChildSymbol(Symbol(new SymbolUDT("Thread")));

	// _KWAIT_BLOCK is a child of _KTHREAD
	kThread->AddWantedChildSymbol(kWaitBlock);
	// _KTHREAD is a child of _ETHREAD
	eThread->AddWantedChildSymbol(kThread);

	m_symbolList.push_back(eThread);

	// initialize default ETHREAD offsets
	SetDefaultOffsetsAndSizes();

	// set target (wanted) process name
	m_targetProcessName = targetProcessName;

	// obtain target process PIDs and fill out the PID vector
	GetTargetProcessPIDs();
}

ETHREADSearch::~ETHREADSearch(void)
{
	// intentionally left empty
}

bool ETHREADSearch::Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts)
{
	bool bFound = false;

	// search loop could be optimized even further - ETHREAD (same as EPROCESS) is usually 0x20, 0x30 or 0x40 bytes away from the
	// _POOL_HEADER - however, speed increase would be completely invisible
	for(DWORD i = dwObjectOffset; i < dwSize - 12; i += 8)
	{
		// process tag
		DWORD dwEthreadStart = *((PDWORD)(pBuffer + i));
		// start of the EPROCESS block is equal to dispatcher header pattern
		if(dwEthreadStart == m_dwDispatcherHeaderPattern)
		{
			PBYTE pETHREAD = (PBYTE) (pBuffer + i);

			// PID is the first member of the CLIENT_ID structure inside ETHREAD
			// Thread ID is the second
			DWORD_PTR dwPID = *((PDWORD_PTR)(pETHREAD + m_offsetMap["Cid"]));
			DWORD_PTR dwTID = *((PDWORD_PTR)(pETHREAD + m_offsetMap["Cid"] + sizeof(DWORD_PTR)));

			// get EPROCESS virtual address
			DWORD_PTR dwETHREADAddr = GetETHREADAddress(pETHREAD);

			OS_OBJECT thread;
			//memcpy_s(&thread.buffer, 4096, pBuffer, dwSize);
			thread.dwAddress = dwAddress;
			thread.dwObjectOffset = i;
			// thread.dwObjectSize = m_structSizeMap["_ETHREAD"];
			// will use passed allocation size as object size 
			thread.dwObjectSize = dwAllocationSize;
			thread.bFlinkModified = thread.bBlinkModified = false;
			thread.dwPID = dwPID;

			if(IsTargetProcessThread(pETHREAD, dwPID))
			{
				// set flink/blink pointers of the thread that belongs to a target process
				FLINK_BLINK flinkBlink;
				flinkBlink.dwTargetFlink = *((PDWORD_PTR)(pETHREAD + m_offsetMap["ThreadListEntry"]));
				flinkBlink.dwTargetBlink = *((PDWORD_PTR)(pETHREAD + m_offsetMap["ThreadListEntry"] + sizeof(DWORD_PTR)));
				m_targetObjectFlinkBlinkList.push_back(flinkBlink);

				// thread block will be deleted with the entire allocation
				bFound = true;
			}
			else
			{
				m_objectMap.insert(std::make_pair(dwETHREADAddr + m_offsetMap["ThreadListEntry"], thread));
			}

			FixAndWriteFlinkBlinkLinks(pBuffer, m_offsetMap["ThreadListEntry"]);

			Artefact artefact = CreateArtefact(i, 15, dwPID, dwTID, dwETHREADAddr);
			artefacts.push_back(artefact);

			// should break? no more ETHREAD blocks here?
			break;
		}
	}

	return bFound;
}
bool ETHREADSearch::IsTargetProcessThread(PBYTE pETHREAD, DWORD_PTR dwPID)
{
	// see if this thread belongs to a target process
	std::vector<DWORD_PTR>::const_iterator iter;
	for(iter = m_targetPIDs.begin(); iter != m_targetPIDs.end(); ++iter)
	{
		if(*iter == dwPID)
		{
			return true;
		}
	}

	return false;
}

void ETHREADSearch::GetTargetProcessPIDs(void)
{
	// don't expect more than 1000 processes running on the system - it's easy to increase it
	DWORD PIDArray[PROCNUM];
	DWORD dwSizeNeeded = 0;

	if(!EnumProcesses(PIDArray, sizeof(PIDArray), &dwSizeNeeded))
	{
		OutputDebugString(_T("Process enumeration failed -- cannot obtain list of PIDs on the system!"));
		return;
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
				// if you're running this code on a 64-bit Windows, you will get errors, since this (32-bit) program cannot enumerate the modules of the 64-bit process
				if(EnumProcessModules(hProcess, &hMod, sizeof(hMod), &dwSizeNeeded))
				{
					CHAR szProcessName[MAX_PATH] = "";
					if(GetModuleBaseNameA(hProcess, hMod, szProcessName, sizeof(szProcessName)/sizeof(CHAR)))
					{
						if(!_stricmp(szProcessName, m_targetProcessName.c_str()))
						{
							m_targetPIDs.push_back(dwProcessID);
						}
					}
					else
					{
						OutputDebugString(_T("Could not get process name"));
						// don't exit -- this will break the loop!
					}
				}
				else
				{
					OutputDebugString(_T("Could not enumerate modules for process -- obtaining process name failed"));
					// don't exit -- this will break the loop!
				}
			}
			else
			{
				OutputDebugString(_T("Could not open process -- obtaining process name failed"));
				// don't exit -- this will break the loop!
			}

			
			CloseHandle(hProcess);
		}
	}
}

DWORD_PTR ETHREADSearch::GetETHREADAddress(PBYTE pETHREAD)
{
	// The easiest way to find ETHREAD address is to use ETHREAD.Tcb.WaitBlock.Thread member, which is a pointer to current ETHREAD
	// Another way is similar to the one used for EPROCESS objects - address of various list members is used (ActiveTimerListHead and KeyedWaitChain).
	// KeyedWaitChain is a list for a given thread - when it is empty, they point to themselves - by subtracting the offset from the beginning of ETHREAD
	// we get the ETHRAD address. Similar situation is for ActiveTimerListHead
	// When both flink and blink pointers of these lists point to the same address, they are empty and point to themselves

	DWORD_PTR dwETHREADAddress = 0;
	DWORD_PTR dwETHREADAddress1 = dwETHREADAddress = *((PDWORD_PTR)(pETHREAD + m_offsetMap["WaitBlock"] + m_offsetMap["Thread"]));

	// check if the pointers of the list are the same
	DWORD_PTR dwActiveTimerListFlink = *((PDWORD_PTR)(pETHREAD + m_offsetMap["ActiveTimerListHead"]));
	DWORD_PTR dwActiveTimerListBlink = *((PDWORD_PTR)(pETHREAD + m_offsetMap["ActiveTimerListHead"] + sizeof(DWORD_PTR)));

	DWORD_PTR dwKeyedWaitChainFlink = *((PDWORD_PTR)(pETHREAD + m_offsetMap["KeyedWaitChain"]));
	DWORD_PTR dwKeyedWaitChainBlink = *((PDWORD_PTR)(pETHREAD + m_offsetMap["KeyedWaitChain"] + sizeof(DWORD_PTR)));

	DWORD_PTR dwETHREADAddress2 = 0;
	DWORD_PTR dwETHREADAddress3 = 0;

	if(dwActiveTimerListFlink == dwActiveTimerListBlink)
	{
		dwETHREADAddress2 = dwActiveTimerListFlink - m_offsetMap["ActiveTimerListHead"];
	}

	if(dwKeyedWaitChainFlink == dwKeyedWaitChainBlink)
	{
		dwETHREADAddress3 = dwKeyedWaitChainFlink - m_offsetMap["KeyedWaitChain"];
	}

	// first check if all addresses are the same
	if(dwETHREADAddress1 == dwETHREADAddress2 == dwETHREADAddress3)
	{
		return dwETHREADAddress;
	}
	if(dwETHREADAddress2 == 0)
	{
		if(dwETHREADAddress1 == dwETHREADAddress3)
		{
			return dwETHREADAddress;
		}
	}
	else if(dwETHREADAddress3 == 0)
	{
		if(dwETHREADAddress1 == dwETHREADAddress2)
		{
			return dwETHREADAddress;
		}
	}

	// now we have the situation where the pointers are either both not null and different, prefer Address 1, but perform additional checking 
	// perform additional checking, but prefer the first address
#ifdef _WIN32
	if(dwETHREADAddress1 >= 0x80000000 && dwETHREADAddress1 < 0xFFFFFFFF)
	{
		return dwETHREADAddress;
	}
#else // _WIN64
	return dwETHREADAddress1;
#endif // _WIN32

	return 0;
}

void ETHREADSearch::SetDefaultOffsetsAndSizes(void)
{
	// get current Windows version number - this must be used in order to identify offsets of the specific parameters 
	OSVERSIONINFO verInfo;
	ZeroMemory(&verInfo, sizeof(OSVERSIONINFO));
	verInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	GetVersionEx(&verInfo);

	// THE CODE THAT FOLLOWS DOES NOT COVER ALL CASES -- see EPROCESSSearch for explanation
	if(verInfo.dwMajorVersion == 5)
	{
		// using Windows XP x86
		m_offsetMap["KeyedWaitChain"] = 0x18c;
		m_offsetMap["ActiveTimerListHead"] = 0x1e4;
		m_offsetMap["Cid"] = 0x1ec;
		m_offsetMap["ThreadListEntry"] = 0x22c;

		// KTHREAD fields
		m_offsetMap["WaitBlock"] = 0x70;

		// KWAIT_BLOCK fields
		m_offsetMap["Thread"] = 0x8;

		m_dwDispatcherHeaderPattern = 0x00700006;

		m_structSizeMap["_ETHREAD"] = 0x258;
	}
	else if(verInfo.dwMajorVersion == 6)
	{
		// using Windows 7 x86 defaults
		m_offsetMap["KeyedWaitChain"] = 0x208;
		m_offsetMap["ActiveTimerListHead"] = 0x224;
		m_offsetMap["Cid"] = 0x22c;
		m_offsetMap["ThreadListEntry"] = 0x268;

		// KTHREAD fields
		m_offsetMap["WaitBlock"] = 0xc0;

		// KWAIT_BLOCK fields
		m_offsetMap["Thread"] = 0x8;

		m_dwDispatcherHeaderPattern = 0x00000006;

		m_structSizeMap["_ETHREAD"] = 0x2b8;
	}
}

Artefact ETHREADSearch::CreateArtefact(DWORD dwOffset, DWORD dwHighlightLength, DWORD_PTR dwPID, DWORD_PTR dwTID, DWORD_PTR dwETHREADAddress)
{
	Artefact artefact;
	artefact.dwOffset = dwOffset;
	artefact.dwHighlightLength = dwHighlightLength;

	std::string szAddress = boost::str(boost::format("%x") % dwETHREADAddress);

	// ugly hacks
#ifdef _UNICODE
	std::wstring wszAddress;
	wszAddress.assign(szAddress.begin(), szAddress.end());
	artefact.szMessage = _T("Found potential ETHREAD block - thread with TID ") + boost::lexical_cast<TCharString>(dwTID) + _T(" belongs to a PROCESS with PID ") + boost::lexical_cast<TCharString>(dwPID) + _T(" -- ETHREAD @ ") + wszAddress;
#else
	artefact.szMessage = _T("Found potential ETHREAD block - thread with TID ") + boost::lexical_cast<std::string>(dwTID) + _T(" belongs to a PROCESS with PID ") + boost::lexical_cast<std::string>(dwPID) + _T(" -- ETHREAD @ ") + szAddress;
#endif // _UNICODE

	return artefact;
}
