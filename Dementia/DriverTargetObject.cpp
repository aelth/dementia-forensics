#include "StdAfx.h"
#include "DriverTargetObject.h"

DriverTargetObject::DriverTargetObject(const TCharString &objName)
	: ITargetObject(objName, 0, true)
{
	m_type = DRIVER;
}

DriverTargetObject::~DriverTargetObject(void)
{
	// intentionally left empty!
}

TARGET_OBJECT DriverTargetObject::LinearizeObject(void)
{
	TARGET_OBJECT obj;
	LinearizeStandardMembers(obj);

	obj.type = m_type;

	return obj;
}