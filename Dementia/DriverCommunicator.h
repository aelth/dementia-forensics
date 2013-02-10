#pragma once

#include "IDriverCommunicator.h"

class DriverCommunicator : public IDriverCommunicator
{
public:
	DriverCommunicator(const TCharString &driverName, const TCharString &driverPath, const TCharString &displayName, bool shouldUnloadDriver = true);
	~DriverCommunicator(void);

	virtual bool InstallDriver(void);
	virtual bool RemoveDriver(void);
	virtual bool StartHiding(const TargetObjectList &targetObjects);

private:
	TCharString m_driverName;
	TCharString m_driverPath;
	TCharString m_displayName;
	bool m_unloadDriver;
	SC_HANDLE m_serviceManager;
	SC_HANDLE m_service;
	HANDLE m_driver;
	HANDLE m_hidingFinishedEvent;

	virtual SymbolList GetDriverSymbols(void);
	virtual bool SendSymbolsToDriver(const SymbolList &symbolList);
	bool SendSynchronizationEventToDriver(void);
};
