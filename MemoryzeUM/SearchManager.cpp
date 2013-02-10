#include "StdAfx.h"
#include "SearchManager.h"
#include "../Common/MemoryzeUMEvents.h"

// define function pointer types to functions imported dynamically
typedef bool (*INITDBGSYMS) (VOID);
typedef bool (*GETSYMINFO)(SymbolList &);

SearchManager::SearchManager(SearchType type)
{
	m_sharedMemory.reset(new SharedMemory<SHARED_BLOCK>(MEMORYZEREADEVENTNAME, MEMORYZEWRITEEVENTNAME, MEMORYZESHAREDMEMORYNAME));
	m_sharedMemoryTargetProc.reset(new SharedMemory<TARGET_PROCESS_BLOCK>(MEMORYZETARGETREADEVENTNAME, MEMORYZETARGETWRITEEVENTNAME, MEMORYZETARGETSHAREDMEMORYNAME));

	// get target process name
	TARGET_PROCESS_BLOCK procBlock = m_sharedMemoryTargetProc->Read();
	m_targetProcessName.assign(procBlock.szProcessName);

	switch(type)
	{
	case ALLOCATION:
		// first create KDBG search class because this class finds some interesting addresses
		m_KDBGSearch.reset(new KDBGSearch());
		m_allocationSearch.reset(new AllocationSearch(m_targetProcessName));
		m_symbolList = m_allocationSearch->GetAllSymbols();
		break;
	default:
		OutputDebugString(_T("Invalid type passed to SearchManager!"));
		// crash the Memoryze process?
		exit(EXIT_FAILURE);
	}

	// get amount of memory available on the system - this information will be used for triggering clean/hide operation
	/*PERFORMACE_INFORMATION perfInfo;
	perfInfo.cb = sizeof(PERFORMACE_INFORMATION);
	GetPerformanceInfo(&perfInfo, sizeof(PERFORMACE_INFORMATION));*/
	
	m_bSymbolsInitialized = false;
	m_searchType = type;
}

SearchManager::~SearchManager(void)
{
}

bool SearchManager::InitSymbols(void)
{
	// do nothing if symbols are already initialized
	if(m_bSymbolsInitialized)
	{
		return true;
	}

	// else load library dynamically.
	// this is the main reason why this function cannot be called from DLLMain
	// according to Microsoft documentation LoadLibrary should not be called from DllMain because of unforeseen consequences.
	// although test have shown no problems whatsoever, library will be loaded from this function that will be called from the hook
	// disadvantage is that we will have many comparisons during the memory dump which could impact performance
	// first obtain my full path (since this DLL can be loaded in any process,
	// current directory cannot be used for this purpose)

	// first obtain my path -- MemoryzeUM.dll is in the same directory as SymbolHelper.dll
	// CurrentDirectory points to Memoryze.exe current dir!
	// similar code is used inside SymbolHelper as well
	TCHAR szDLLSelfName[MAX_PATH + 1];
	HMODULE hMyHandle = GetModuleHandle(_T("MemoryzeUM.dll"));
	if(hMyHandle == NULL)
	{
		OutputDebugString(_T("Cannot get handle of MemoryzeUM.dll module"));
		return false;
	}

	if(!GetModuleFileName(hMyHandle, szDLLSelfName, MAX_PATH + 1))
	{
		OutputDebugString(_T("Full path of MemoryzeUM.dll module could not be obtained"));
		return false;
	}

	boost::filesystem::path basePath = boost::filesystem::path(szDLLSelfName).parent_path();
	// again, ugly conversions
#ifdef _UNICODE
	TCharString szSymbolHelperPath = basePath.wstring() + _T("\\SymbolHelper.dll");
#else
	TCharString szSymbolHelperPath = basePath.string() + _T("\\SymbolHelper.dll");
#endif // _UNICODE

	// get SymbolHelper module used in GetProcAddress calls
	HMODULE hSymHelper = LoadLibrary(szSymbolHelperPath.c_str());
	if(hSymHelper == NULL)
	{
		OutputDebugString(_T("Hhandle to SymbolHelper.dll could not be obtained, LoadLibrary failed"));
		return false;
	}

	// initialize all function pointers
	INITDBGSYMS pfnInitializeDebugSymbols = (INITDBGSYMS) GetProcAddress(hSymHelper, "InitializeDebugSymbols");
	GETSYMINFO pfnGetSymbolsInformation = (GETSYMINFO) GetProcAddress(hSymHelper, "GetSymbolsInformation");

	if(pfnInitializeDebugSymbols == NULL || pfnGetSymbolsInformation == NULL)
	{
		OutputDebugString(_T("Pointers to SymbolHelper functions could not be obtained"));
		FreeLibrary(hSymHelper);
		return false;
	}

	if(!pfnInitializeDebugSymbols())
	{
		OutputDebugString(_T("Failed to initialize debug symbols"));
		FreeLibrary(hSymHelper);
		return false;
	}

	if(!pfnGetSymbolsInformation(m_symbolList))
	{
		OutputDebugString(_T("Failed to retrieve information about symbols"));
		FreeLibrary(hSymHelper);
		return false;
	}


	// this is actually not necessary, since we're using pointers to symbols everywhere
	// but linearize is necessary
	if(m_searchType == ALLOCATION)
	{
		m_allocationSearch->LinearizeAllSymbols();
	}
	
	m_bSymbolsInitialized = true;
	FreeLibrary(hSymHelper);
	return true;
}

bool SearchManager::Search(PBYTE pBuffer, DWORD dwSize, DWORD_PTR dwAddress)
{
	bool ret = false;
	std::vector<Artefact> artefacts;
	std::vector<Artefact>::const_iterator artefactIter;
	switch(m_searchType)
	{
		case ALLOCATION:
			ret = m_allocationSearch->Search(pBuffer, dwSize, 0, dwAddress, 0, artefacts);
			break;
	}

	for(artefactIter = artefacts.begin(); artefactIter != artefacts.end(); ++artefactIter)
	{
		SHARED_BLOCK sharedBlock;
		sharedBlock.dwAddress =  dwAddress;
		sharedBlock.dwHighlightOffset = (*artefactIter).dwOffset;
		sharedBlock.dwHighlightLength = (*artefactIter).dwHighlightLength;
		_stprintf_s(sharedBlock.szInfoMessage, 512, (*artefactIter).szMessage.c_str());
		memcpy_s(sharedBlock.bCapturedBuffer, 4096, pBuffer, dwSize);
		m_sharedMemory->Write(&sharedBlock);
	}

	return ret;
}