#pragma once
#include "IObjectSearchLinked.h"
#include "IEPROCESSListener.h"
#include "IKDBGListener.h"

class EPROCESSSearch : public IObjectSearchLinked, public IKDBGListener
{
public:
	EPROCESSSearch(const std::string &targetProcessName, const std::vector<boost::shared_ptr<IEPROCESSListener>> &targetProcessListeners);
	~EPROCESSSearch(void);

	virtual bool Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts);
private:
	DWORD m_dwDispatcherHeaderPattern;
	std::string m_targetProcessName;
	std::vector<boost::shared_ptr<IEPROCESSListener>> m_listenerList;

	virtual void SetDefaultOffsetsAndSizes(void);
	DWORD_PTR GetEPROCESSAddress(PBYTE pEPROCESS);
	void NotifyListeners(const DWORD_PTR dwPID, const DWORD_PTR dwEPROCESSAddress);
	Artefact CreateArtefact(DWORD dwOffset, DWORD dwHighlightLength, DWORD_PTR dwPID, DWORD_PTR dwEPROCESSAddress, PCHAR szProcessName);

	// implement pure virtual function from IKDBGListener
	virtual void ProcessNotification(void);
};
