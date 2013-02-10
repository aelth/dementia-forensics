#pragma once

#include "IDriverCommunicator.h"

class DriverMinifilterCommunicator : public IDriverCommunicator
{
public:
	DriverMinifilterCommunicator(void);
	~DriverMinifilterCommunicator(void);

	virtual bool InstallDriver(void);
	virtual bool RemoveDriver(void);
	virtual bool StartHiding(const TargetObjectList &targetObjects);

private:
	HANDLE m_driverPort;

	virtual SymbolList GetDriverSymbols(void);
	virtual bool SendSymbolsToDriver(const SymbolList &symbolList);
};
