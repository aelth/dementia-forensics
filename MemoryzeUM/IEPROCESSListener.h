#pragma once

class IEPROCESSListener
{
public:
	IEPROCESSListener(void)
	{
		m_processPID = -1;
		m_EPROCESSAddress = -1;
	}

	virtual void Notify(const DWORD_PTR dwPID, const DWORD_PTR dwEPROCESSAddress)
	{
		m_processPID = dwPID;
		m_EPROCESSAddress = dwEPROCESSAddress;

		ProcessNotification();
	}
	
protected:
	DWORD_PTR m_processPID;
	DWORD_PTR m_EPROCESSAddress;

	// this function will be used by concrete implementation in order to process given data
	// the function can just return without doing any real processing
	virtual void ProcessNotification(void) = 0;
};