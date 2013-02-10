#pragma once
#include "IObjectSearchLinked.h"

class ETHREADSearch : public IObjectSearchLinked
{
public:
	ETHREADSearch(const std::string &targetProcessName);
	~ETHREADSearch(void);

	virtual bool Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts);

private:
	DWORD m_dwDispatcherHeaderPattern;
	std::string m_targetProcessName;
	std::vector<DWORD_PTR> m_targetPIDs;

	bool IsTargetProcessThread(PBYTE pETHREAD, DWORD_PTR dwPID);
	void GetTargetProcessPIDs(void);
	DWORD_PTR GetETHREADAddress(PBYTE pETHREAD);
	virtual void SetDefaultOffsetsAndSizes(void);
	Artefact CreateArtefact(DWORD dwOffset, DWORD dwHighlightLength, DWORD_PTR dwPID, DWORD_PTR dwTID, DWORD_PTR dwETHREADAddress);

};
