#include "StdAfx.h"
#include "AllocationSearch.h"
#include "IEPROCESSListener.h"
#include "EPROCESSSearch.h"
#include "ETHREADSearch.h"
#include "UDPEndpoint.h"
#include "TCPEndpoint.h"
#include "TCPListener.h"
#include "../Common/SharedMemory.h"

AllocationSearch::AllocationSearch(const std::string &targetProcessName)
{
	// initialize object name and tag
	m_szObjName = "_POOL_HEADER";
	m_dwTag = 0;

	// set default values, if symbol retrieval fails
	SetDefaultOffsetsAndSizes();

	// initialize network objects
	boost::shared_ptr<UDPEndpoint> udpEndpoint = boost::shared_ptr<UDPEndpoint>(new UDPEndpoint(m_structSizeMap["_POOL_HEADER"]));
	boost::shared_ptr<TCPEndpoint> tcpEndpoint = boost::shared_ptr<TCPEndpoint>(new TCPEndpoint(m_structSizeMap["_POOL_HEADER"]));
	boost::shared_ptr<TCPListener> tcpListener = boost::shared_ptr<TCPListener>(new TCPListener(m_structSizeMap["_POOL_HEADER"]));

	// put all relevant objects that want to be informed about found target EPROCESS object into a listener list
	std::vector<boost::shared_ptr<IEPROCESSListener>> targetProcessListeners;
	targetProcessListeners.push_back(udpEndpoint);
	targetProcessListeners.push_back(tcpEndpoint);
	targetProcessListeners.push_back(tcpListener);

	// create other objects and put them into a search list
	m_searchObjects.push_back(boost::shared_ptr<IObjectSearch>(new EPROCESSSearch(targetProcessName, targetProcessListeners)));
	m_searchObjects.push_back(boost::shared_ptr<IObjectSearch>(new ETHREADSearch(targetProcessName)));
	m_searchObjects.push_back(udpEndpoint);
	m_searchObjects.push_back(tcpEndpoint);
	m_searchObjects.push_back(tcpListener);

	// obtain symbols from the objects
	std::vector<boost::shared_ptr<IObjectSearch>>::const_iterator iter;
	for(iter = m_searchObjects.begin(); iter != m_searchObjects.end(); ++iter)
	{
		// add list of symbols from the search object to symbols map
		SymbolList vect = (*iter)->GetSymbols();
		m_subObjectsSymbolList.insert(m_subObjectsSymbolList.end(), vect.begin(), vect.end());
	}

	// get POOL_HEADER member's offset - necessary for iterating through allocations
	Symbol poolHeader(new SymbolUDT(m_szObjName, true));
	// specify all relevant POOL_HEADER members
	poolHeader->AddWantedChildSymbol(Symbol(new SymbolUDT("PoolTag")));
	poolHeader->AddWantedChildSymbol(Symbol(new SymbolUDT("BlockSize", false, true)));
	poolHeader->AddWantedChildSymbol(Symbol(new SymbolUDT("PoolType", false, true)));

	m_symbolList.push_back(poolHeader);
}

AllocationSearch::~AllocationSearch(void)
{
	// intentionally left empty
}

SymbolList AllocationSearch::GetAllSymbols(void) const
{
	SymbolList fullSymList;
	
	// first add my own symbols
	fullSymList.insert(fullSymList.end(), m_symbolList.begin(), m_symbolList.end());

	// add symbols from other objects
	fullSymList.insert(fullSymList.end(), m_subObjectsSymbolList.begin(), m_subObjectsSymbolList.end());

	return fullSymList;
}

void AllocationSearch::SetDefaultOffsetsAndSizes(void)
{
	// bit fields, but just their offsets are here
	m_offsetMap["BlockSize"] = 0x2;
	m_offsetMap["PoolType"] = 0x2;

	// positions and lengths of bitfields
	m_bitFieldMap["BlockSize"] = std::make_pair(0, 9);
	m_bitFieldMap["PoolType"] = std::make_pair(9, 7);
	
	// PoolTag offset is 4 from Windows 2000 onwards - this is the most important field
	m_offsetMap["PoolTag"] = 0x4;

	// default size of the structure on all 32 bit systems is 8 bytes
	m_structSizeMap["_POOL_HEADER"] = 0x8;
}

bool AllocationSearch::Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts)
{
	if(pBuffer == NULL)
	{
		OutputDebugString(_T("Search buffer retrieved from the driver cannot be null"));
		return false;
	}

	DWORD dwPoolTagOffset = m_offsetMap["PoolTag"];
	DWORD dwBlockSizeOffset = m_offsetMap["BlockSize"];

	std::vector<boost::shared_ptr<IObjectSearch>>::iterator iter;

	// looking for allocations which are 8-byte aligned!
	for(DWORD i = dwObjectOffset; i < dwSize - 12; i += 8)
	{
		// get pool tag and compare it with our search objects
		DWORD dwPoolTag = *((PDWORD)(pBuffer + i + dwPoolTagOffset));
		for(iter = m_searchObjects.begin(); iter != m_searchObjects.end(); ++iter)
		{
			if(dwPoolTag == (*iter)->GetTag())
			{
				// get size of this allocation
				WORD wBlockSize = *((WORD *)(pBuffer + i + dwBlockSizeOffset));
				// compute size of the allocation - algorithm is basically (value >> bitPosition) & bitLength -- must multiply it with 8 (Windows related)
				DWORD dwAllocationSize = ((wBlockSize >> m_bitFieldMap["BlockSize"].first) & ((1 << m_bitFieldMap["BlockSize"].second) - 1)) * 8;

				// search returns true if target object (for example, process) was found
				// if found, "erase" this allocation immediately
				if((*iter)->Search(pBuffer, dwSize, dwAllocationSize, dwAddress, i + m_structSizeMap["_POOL_HEADER"], artefacts))
				{
					Artefact artefact = CreateArtefact(dwAddress, i, (*iter)->GetName());
					artefacts.push_back(artefact);
					memset((PBYTE) (pBuffer + i), 0, dwAllocationSize);
				}
			}
		}	
	}

	// for now:)
	return true;
}

bool AllocationSearch::LinearizeAllSymbols()
{
	// update symbols in IObjectSearch classes
	std::vector<boost::shared_ptr<IObjectSearch>>::iterator iter;
	for(iter = m_searchObjects.begin(); iter != m_searchObjects.end(); ++iter)
	{
		// errors are not checked, defaults will be used instead
		(*iter)->LinearizeSymbols();
	}

	// update my symbols - use defaults if everything fails and always return true (for now)
	LinearizeSymbols();

	return true;
}

Artefact AllocationSearch::CreateArtefact(DWORD_PTR dwAddress, DWORD dwOffset, std::string szAllocationType)
{
	Artefact artefact;
	artefact.dwOffset = dwOffset;
	artefact.dwHighlightLength = 10;
	std::string szAddress = boost::str(boost::format("%x") % (dwAddress + dwOffset));

	// ugly hacks
#ifdef _UNICODE
	std::wstring wszAddress;
	wszAddress.assign(szAddress.begin(), szAddress.end());
	
	std::wstring wszAllocationType;
	wszAllocationType.assign(szAllocationType.begin(), szAllocationType.end());

	artefact.szMessage = artefact.szMessage = _T("Erasing \"") + wszAllocationType + _T("\" allocation for target process @ phys. addr. ") + wszAddress;
#else
	artefact.szMessage = _T("Erasing \"") + szAllocationType + _T("\" allocation for target process @ phys. addr. ") + szAddress;
#endif // _UNICODE

	return artefact;
}