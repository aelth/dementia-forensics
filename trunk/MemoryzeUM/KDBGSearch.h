#pragma once

#include "IObjectSearch.h"
#include "IKDBGListener.h"

// KDBGSearch does not inherit IObjectSearch because it does not use symbols -- all structures are actually WinDBG related
class KDBGSearch
{
public:
	KDBGSearch(void);
	~KDBGSearch(void);

	void AddListener(boost::shared_ptr<IKDBGListener> listener);

	// use similar search function as for IObjectSearch class/interface -- no allocation size and object offset
	bool Search(PBYTE pBuffer, DWORD dwSize, DWORD_PTR dwAddress, std::vector<Artefact> &artefacts);
private:
	DWORD_PTR m_PsActiveProcessHead;
	DWORD m_KDBGTag;
	bool m_addressesFound;
	std::map<std::string, DWORD> m_offsetMap;
	std::vector<boost::shared_ptr<IKDBGListener>> m_listenerList;

	void NotifyListeners(const DWORD_PTR dwPsActiveProcessHeadAddress);
	// use similar function as for IObjectSearch class/interface
	void SetDefaultOffsetsAndSizes(void);
};
