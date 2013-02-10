#pragma once
#include "../Common/SymbolUDT.h"
#include "IObjectSearch.h"
#include "AllocationSearch.h"
#include "KDBGSearch.h"
#include "../Common/SharedMemory.h"

/*
	Currently supported SearchTypes are:
		ALLOCATION - search for pool allocations with specific pool tags
*/
typedef enum _SearchType
{
	ALLOCATION,
} SearchType;

class SearchManager
{
public:
	SearchManager(SearchType type);
	~SearchManager(void);
	bool InitSymbols(void);
	inline bool AreSymbolsInitialized() { return m_bSymbolsInitialized; }

	// default values are set in constructor - just update/set the flag so AreSymbolsInitialized always returns true
	inline void UseDefaultSymbols() { m_bSymbolsInitialized = true; }

	bool Search(PBYTE pBuffer, DWORD dwSize, DWORD_PTR dwAddress);
private:
	SymbolList m_symbolList;
	SearchType m_searchType;
	std::string m_targetProcessName;
	bool m_bSymbolsInitialized;
	boost::scoped_ptr<AllocationSearch> m_allocationSearch;
	boost::scoped_ptr<KDBGSearch> m_KDBGSearch;
	boost::scoped_ptr<SharedMemory<SHARED_BLOCK>> m_sharedMemory;
	boost::scoped_ptr<SharedMemory<TARGET_PROCESS_BLOCK>> m_sharedMemoryTargetProc;
};
