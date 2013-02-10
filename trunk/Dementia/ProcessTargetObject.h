#pragma once

#include "ITargetObject.h"

class ProcessTargetObject : public ITargetObject
{
public:
	ProcessTargetObject(const TCharString &objName, const DWORD_PTR ID, const bool shouldHideAllocs = true, 
						const bool shouldHideThreads = true, const bool shouldHideHandles = true, const bool shouldHideImageFile = true,
						const bool shouldHideJob = true, const bool shouldHideVad = true);
	~ProcessTargetObject(void);

	virtual TARGET_OBJECT LinearizeObject(void);

private:
	bool m_hideThreads;
	bool m_hideHandles;
	bool m_hideImageFileObj;
	bool m_hideJob;
	bool m_hideVad;
};
