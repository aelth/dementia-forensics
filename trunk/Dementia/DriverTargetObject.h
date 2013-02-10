#pragma once

#include "ITargetObject.h"

class DriverTargetObject : public ITargetObject
{
public:
	DriverTargetObject(const TCharString &objName);
	~DriverTargetObject(void);

	virtual TARGET_OBJECT LinearizeObject(void);
};
