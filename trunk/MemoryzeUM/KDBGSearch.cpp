#include "StdAfx.h"
#include "KDBGSearch.h"

KDBGSearch::KDBGSearch(void)
{
	m_PsActiveProcessHead = -1;
	m_KDBGTag = 0x4742444b;
	m_addressesFound = false;

	SetDefaultOffsetsAndSizes();
}

KDBGSearch::~KDBGSearch(void)
{
	// clear all listeners
	m_listenerList.clear();
}

void KDBGSearch::AddListener(boost::shared_ptr<IKDBGListener> listener)
{
	m_listenerList.push_back(listener);
}

bool KDBGSearch::Search(PBYTE pBuffer, DWORD dwSize, DWORD_PTR dwAddress, std::vector<Artefact> &artefacts)
{
	// if all relevant addresses have already been found, just return
	if(m_addressesFound)
	{
		return m_addressesFound;
	}

	// read 4 bytes at a time, starting at the beginning of the buffer
	for(DWORD i = 0; i < dwSize; i += 4)
	{
		// KDBG tag
		DWORD dwKDBGTag = *((PDWORD)(pBuffer + i));
		// check if KDBG tag is encountered
		if(dwKDBGTag == m_KDBGTag)
		{
			DWORD_PTR dwPsActiveProcessHeadAddress = *((PDWORD_PTR)(pBuffer + i + m_offsetMap["PsActiveProcessHead"]));

#ifdef _WIN32
			// do some error checking
			if(dwPsActiveProcessHeadAddress <= 0x80000000)
			{
				continue;
			}
			else
#endif // _WIN32
			{
				m_addressesFound = true;
				NotifyListeners(dwPsActiveProcessHeadAddress);
				break;
			}
		}
	}

	return m_addressesFound;
}

void KDBGSearch::SetDefaultOffsetsAndSizes(void)
{
	// on all Windows operating systems, offsets are the same
	m_offsetMap["PsActiveProcessHead"] = 0x50;
}

void KDBGSearch::NotifyListeners(const DWORD_PTR dwPsActiveProcessHeadAddress)
{
	std::vector<boost::shared_ptr<IKDBGListener>>::const_iterator iter;
	for(iter = m_listenerList.begin(); iter != m_listenerList.end(); ++iter)
	{
		(*iter)->Notify(dwPsActiveProcessHeadAddress);
	}
}

