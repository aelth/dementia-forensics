#include "StdAfx.h"
#include "ProcessTargetObject.h"

ProcessTargetObject::ProcessTargetObject(	const TCharString &objName, const DWORD_PTR ID, const bool shouldHideAllocs, const bool shouldHideThreads,
											const bool shouldHideHandles, const bool shouldHideImageFile, const bool shouldHideJob, const bool shouldHideVad)
: ITargetObject(objName, ID, shouldHideAllocs)
{
	m_hideThreads = shouldHideThreads;
	m_hideHandles = shouldHideHandles;
	m_hideImageFileObj = shouldHideImageFile;
	m_hideJob = shouldHideJob;
	m_hideVad = shouldHideVad;
	m_type = PROCESS;
}
ProcessTargetObject::~ProcessTargetObject(void)
{
	// intentionally left empty
}

TARGET_OBJECT ProcessTargetObject::LinearizeObject(void)
{
	TARGET_OBJECT obj;
	LinearizeStandardMembers(obj);

	obj.bHideThreads = m_hideThreads;
	obj.bHideHandles = m_hideHandles;
	obj.bHideImageFileObj = m_hideImageFileObj;
	obj.bHideJob = m_hideJob;
	obj.bHideVad = m_hideVad;
	obj.type = m_type;

	return obj;
}

