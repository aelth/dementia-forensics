#pragma once

class IKDBGListener
{
public:
	IKDBGListener(void)
	{
		m_PsActiveProcessHeadAddr = -1;
	}

	virtual void Notify(const DWORD_PTR dwPsActiveProcessHeadAddress)
	{
		m_PsActiveProcessHeadAddr = dwPsActiveProcessHeadAddress;
		ProcessNotification();
	}

protected:
	DWORD_PTR m_PsActiveProcessHeadAddr;

	// this function will be used by concrete implementation in order to process given data
	// the function can just return without doing any real processing
	virtual void ProcessNotification(void) = 0;
};