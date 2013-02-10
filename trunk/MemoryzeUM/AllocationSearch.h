#pragma once
#include "../Common/SymbolUDT.h"
#include "IObjectSearch.h"

class AllocationSearch : public IObjectSearch
{
public:
	AllocationSearch(const std::string &targetProcessName);
	~AllocationSearch(void);
	
	// return symbols of all IObjectSearch objects and my allocator symbols
	SymbolList GetAllSymbols(void) const;

	// linearize (i.e. store in a map) all symbol offsets for myself and my IObjectSearch classes
	// symbol offsets are filled by SymbolHelper.dll
	bool LinearizeAllSymbols();

	virtual bool Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts);
private:
	SymbolList m_symbolList;
	SymbolList m_subObjectsSymbolList;

	std::vector<boost::shared_ptr<IObjectSearch>> m_searchObjects;

	virtual void SetDefaultOffsetsAndSizes(void);
	Artefact CreateArtefact(DWORD_PTR dwAddress, DWORD dwOffset, std::string szAllocationType);
};
