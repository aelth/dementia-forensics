#include "StdAfx.h"
#include "EPROCESSSearch.h"

EPROCESSSearch::EPROCESSSearch(const std::string &targetProcessName, const std::vector<boost::shared_ptr<IEPROCESSListener>> &targetProcessListeners)
{
	// initialize object name and tag
	m_szObjName = "_EPROCESS";
	m_dwTag = 0xe36f7250;

	// create new symbol that represents "abstract" EPROCESS symbol
	Symbol eProc(new SymbolUDT(m_szObjName, true));
	// specify all relevant EPROCESS members
	eProc->AddWantedChildSymbol(Symbol(new SymbolUDT("ActiveProcessLinks")));
	eProc->AddWantedChildSymbol(Symbol(new SymbolUDT("UniqueProcessId")));
	eProc->AddWantedChildSymbol(Symbol(new SymbolUDT("ImageFileName")));

	// "child" structure of _EPROCESS is a _KPROCESS structure that also contains some interesting objects
	Symbol kProc(new SymbolUDT("_KPROCESS", true));
	// specify members of _KPROCESS - these are used to determine EPROCESS address
	kProc->AddWantedChildSymbol(Symbol(new SymbolUDT("ProfileListHead")));
	kProc->AddWantedChildSymbol(Symbol(new SymbolUDT("ReadyListHead")));

	// _KPROCESS is a child of _EPROCESS
	eProc->AddWantedChildSymbol(kProc);

	m_symbolList.push_back(eProc);

	// initialize default EPROCESS offsets
	SetDefaultOffsetsAndSizes();

	// set target (wanted) process name
	m_targetProcessName = targetProcessName;

	// set process listeners
	m_listenerList = targetProcessListeners;
}

EPROCESSSearch::~EPROCESSSearch(void)
{
	// intentionally left empty
}

bool EPROCESSSearch::Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts)
{
	bool bFound = false;

	// search loop could be optimized even further - EPROCESS is usually 0x20, 0x30 or 0x40 bytes away from the
	// _POOL_HEADER - however, speed increase would be completely invisible
	for(DWORD i = dwObjectOffset; i < dwSize - 12; i += 8)
	{
		// process tag
		DWORD dwEprocessStart = *((PDWORD)(pBuffer + i));
		// start of the EPROCESS block is equal to dispatcher header pattern
		if(dwEprocessStart == m_dwDispatcherHeaderPattern)
		{
			PBYTE pEPROCESS = (PBYTE) (pBuffer + i);
			
			// try to obtain all relevant EPROCESS fields
			DWORD_PTR dwPID = *((PDWORD_PTR)(pEPROCESS + m_offsetMap["UniqueProcessId"]));
			char *szProcName = (char *) (pEPROCESS + m_offsetMap["ImageFileName"]);

			// get EPROCESS virtual address
			DWORD_PTR dwEPROCESSAddr = GetEPROCESSAddress(pEPROCESS);

			OS_OBJECT process;
			//memcpy_s(&process.buffer, 4096, pBuffer, dwSize);
			process.dwAddress = dwAddress;
			process.dwObjectOffset = i;
			//process.dwObjectSize = m_structSizeMap["_EPROCESS"];
			// same as for threads - use passed allocation size for deleting
			process.dwObjectSize = dwAllocationSize;
			process.bFlinkModified = process.bBlinkModified = false;
			process.dwPID = dwPID;
			strncpy_s(process.szImageName, 15, szProcName, _TRUNCATE);

			// ImageFileName is only 15 characters long, so do such comparison
			// theoretically, there can be a lot of processes with the same "short" (15 char) name that are completely different
			// this situation is only theoretical and will not be considered further
			if(!_strnicmp(szProcName, m_targetProcessName.c_str(), 15))
			{
				// set flink/blink pointers of the target process
				FLINK_BLINK flinkBlink;
				flinkBlink.dwTargetFlink = *((PDWORD_PTR)(pEPROCESS + m_offsetMap["ActiveProcessLinks"]));
				flinkBlink.dwTargetBlink = *((PDWORD_PTR)(pEPROCESS + m_offsetMap["ActiveProcessLinks"] + sizeof(DWORD_PTR)));
				m_targetObjectFlinkBlinkList.push_back(flinkBlink);

				// notify all listeners about the discovery of target process
				NotifyListeners(dwPID, dwEPROCESSAddr);
				
				// process block will be erased with the entire allocation
				bFound = true;
			}
			else
			{
				m_objectMap.insert(std::make_pair(dwEPROCESSAddr + m_offsetMap["ActiveProcessLinks"], process));
			}

			FixAndWriteFlinkBlinkLinks(pBuffer, m_offsetMap["ActiveProcessLinks"]);
			
			Artefact artefact = CreateArtefact(i + m_offsetMap["ImageFileName"], 15, dwPID, dwEPROCESSAddr, szProcName);
			artefacts.push_back(artefact);

			// should break? no more EPROCESS blocks here?
			break;
		}
	}

	return bFound;
}
DWORD_PTR EPROCESSSearch::GetEPROCESSAddress(PBYTE pEPROCESS)
{
	// ProfileListHead and ReadyListHead are rarely used members inside KPROCESS - when they are empty, they point to
	// themselves - by subtracting the offset from the beginning of EPROCESS, we get the EPROCESS address.
	// ProfileListHead is used for profiling processes - profile object is inserted in this list - this list is almost never used.
	// ReadyListHead seems to be used by KiAttachProcess when thread is initially attached to a process in memory.
	// It is also used in KiReadyThread when the process is not in memory - thread is inserted in the ReadyListHead
	DWORD_PTR dwEPROCESSAddress = 0;
	DWORD_PTR dwEPROCESSAddress1 = dwEPROCESSAddress = (*((PDWORD_PTR)(pEPROCESS + m_offsetMap["ProfileListHead"]))) - m_offsetMap["ProfileListHead"];
	DWORD_PTR dwEPROCESSAddress2 = (*((PDWORD_PTR)(pEPROCESS + m_offsetMap["ReadyListHead"]))) - m_offsetMap["ReadyListHead"];

	// if the addresses differ, take the first one, since profiling is extremely rarely used
	if(dwEPROCESSAddress1 != dwEPROCESSAddress2)
	{
#ifdef _WIN64
		// prefer first address
		dwEPROCESSAddress = dwEPROCESSAddress1;
#else // _WIN32
		// perform additional checking, but prefer the first address
		if(dwEPROCESSAddress1 < 0x80000000)
		{
			dwEPROCESSAddress = dwEPROCESSAddress2;
		}
#endif // _WIN64
	}

	return dwEPROCESSAddress;
}

void EPROCESSSearch::NotifyListeners(const DWORD_PTR dwPID, const DWORD_PTR dwEPROCESSAddress)
{
	std::vector<boost::shared_ptr<IEPROCESSListener>>::const_iterator iter;
	for(iter = m_listenerList.begin(); iter != m_listenerList.end(); ++iter)
	{
		(*iter)->Notify(dwPID, dwEPROCESSAddress);
	}
}

Artefact EPROCESSSearch::CreateArtefact(DWORD dwOffset, DWORD dwHighlightLength, DWORD_PTR dwPID, DWORD_PTR dwEPROCESSAddress, PCHAR szProcessName)
{
	Artefact artefact;
	artefact.dwOffset = dwOffset;
	artefact.dwHighlightLength = dwHighlightLength;

	std::string procName(szProcessName);
	std::string szAddress = boost::str(boost::format("%x") % dwEPROCESSAddress);

	// ugly hacks
#ifdef _UNICODE
	std::wstring ws;
	ws.assign(procName.begin(), procName.end());

	std::wstring wszAddress;
	wszAddress.assign(szAddress.begin(), szAddress.end());
	artefact.szMessage = _T("Found potential EPROCESS block - process \"") + ws + _T("\" with PID ") + boost::lexical_cast<TCharString>(dwPID) + _T(" -- EPROCESS @ ") + wszAddress;
#else
	artefact.szMessage = _T("Found potential EPROCESS block - process \"") + procname + _T("\" with PID ") + boost::lexical_cast<std::string>(dwPID) + _T(". EPROCESS @ ") + szAddress;
#endif // _UNICODE

	return artefact;
}

void EPROCESSSearch::SetDefaultOffsetsAndSizes(void)
{
	// first obtain OS version
	OSVERSIONINFO verInfo;
	ZeroMemory(&verInfo, sizeof(OSVERSIONINFO));
	verInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	GetVersionEx(&verInfo);

	// THE CODE THAT FOLLOWS DOES NOT COVER ALL CASES
	// assumption is made that major version = 5 (Windows XP) covers all similar OS-es (Windows 2003, Windows 2000, etc.)
	// THAT IS NOT TRUE, since majorver = 5 covers Windows XP 64-bit as well, which has widely different offsets.
	// situation is even more complicated between different service packs, etc.
	// however, since this is used as a fallback mechanism, if symbol retrieval does not work, it is satisfactory
	if(verInfo.dwMajorVersion == 5)
	{
		// using Windows XP x86
		m_offsetMap["UniqueProcessId"] = 0x84;
		m_offsetMap["ActiveProcessLinks"] = 0x88;
		m_offsetMap["ImageFileName"] = 0x174;
		m_offsetMap["ProfileListHead"] = 0x10;
		m_offsetMap["ReadyListHead"] = 0x40;

		m_structSizeMap["_EPROCESS"] = 0x25c;

		// do not obtain _DISPATCHER_HEADER offsets - _DISPATCHER_HEADER is the first element of the _EPROCESS block
		// this DWORD consists of type (0x3) and size - size is 0x1b until Windows XP (32-bit!)
#ifdef _WIN64
		m_dwDispatcherHeaderPattern = 0x002e0003;
#else // _WIN32
		m_dwDispatcherHeaderPattern = 0x001b0003;
#endif // _WIN64
	}
	else if(verInfo.dwMajorVersion == 6)
	{
		// using Windows 7 x86 defaults
		m_offsetMap["UniqueProcessId"] = 0xb4;
		m_offsetMap["ActiveProcessLinks"] = 0xb8;
		m_offsetMap["ImageFileName"] = 0x16c;

		m_offsetMap["ProfileListHead"] = 0x10;
		m_offsetMap["ReadyListHead"] = 0x44;

		m_structSizeMap["_EPROCESS"] = 0x2c0;

#ifdef _WIN64
		// x64 Windows 7 has different dispatcher header pattern
		m_dwDispatcherHeaderPattern = 0x00580003;
#else // _WIN32
		m_dwDispatcherHeaderPattern = 0x00260003;
#endif // _WIN32
	}
}

void EPROCESSSearch::ProcessNotification(void)
{

}