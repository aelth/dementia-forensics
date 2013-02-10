#pragma once

#include "IEvasionMethodPlugin.h"
#include "IDriverCommunicator.h"
#include "ITargetObject.h"

class KernelModeEvasion : public IEvasionMethodPlugin
{
public:
	KernelModeEvasion(int ID);
	~KernelModeEvasion(void);

	virtual bool ParseArguments(std::string args);
	virtual bool Execute(void);

private:
	TCharString m_driverName;
	TCharString m_driverPath;
	bool m_unloadDriver;
	boost::scoped_ptr<IDriverCommunicator> m_driverCommunicator;
	TargetObjectList m_targetObjects;
};