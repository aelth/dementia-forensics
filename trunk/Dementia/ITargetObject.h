#pragma once

// don't like this include because of the driver connection -- only DriverCommunicator should know some driver structures
// ITargetObject is also related to a driver somehow, so it is acceptable at the moment
#include "../Common/CommonTypesDrv.h"

class ITargetObject;
typedef boost::shared_ptr<ITargetObject> TargetObjectPtr;
typedef std::vector<TargetObjectPtr> TargetObjectList;

class ITargetObject
{
public:
	ITargetObject(const TCharString &objName, const DWORD_PTR ID, const bool shouldHideAllocs)
		: m_objectName(objName)
	{
		m_ID = ID;
		m_hideAllocations = shouldHideAllocs;
		m_type = ABSTRACT;
	}

	ITargetObject(const ITargetObject &obj)
		: m_objectName(obj.m_objectName)
	{
		m_ID = obj.m_ID;
		m_hideAllocations = obj.m_hideAllocations;
		m_type = obj.m_type;
	}

	virtual ~ITargetObject(void)
	{
		// intentionally left empty
	}

	TCharString GetName(void) const { return m_objectName; }
	void SetName(const TCharString &objName) { m_objectName = objName; }

	TARGET_OBJECT_TYPE GetType(void) const { return m_type; }
	void SetType(const TARGET_OBJECT_TYPE type) { m_type = type; }

	DWORD_PTR GetID(void) const { return m_ID; }
	void SetID(const DWORD_PTR dwID) { m_ID = dwID; }

	bool ShouldHideAllocations(void) const { return m_hideAllocations; }
	void SetHideAllocations(void) { m_hideAllocations = true; }

	virtual TARGET_OBJECT LinearizeObject(void)
	{
		TARGET_OBJECT obj;
		LinearizeStandardMembers(obj);
		return obj;
	}

protected:
	TCharString m_objectName;
	TARGET_OBJECT_TYPE m_type;
	// ID represents process ID, thread ID, or any other ID
	// use DWORD_PTR type, as PIDs on 64 bit systems are actually 64-bit long
	DWORD_PTR m_ID;
	bool m_hideAllocations;

	virtual void LinearizeStandardMembers(TARGET_OBJECT &obj)
	{
#ifdef _UNICODE
		std::string objName;
		objName.assign(m_objectName.begin(), m_objectName.end());
		strcpy_s(obj.szObjectName, MAX_OBJECT_NAME, objName.c_str());
#else
		strcpy_s(obj.szObjectName, MAX_OBJECT_NAME, m_objectName.c_str());
#endif // _UNICODE
		
		obj.uPID = m_ID;
		obj.bHideAllocs = m_hideAllocations;
	}
};
