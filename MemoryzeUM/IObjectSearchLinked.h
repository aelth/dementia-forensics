#pragma once

#include "IObjectSearch.h"
#include "DumpOverwriter.h"

class IObjectSearchLinked : public IObjectSearch
{
public:
	virtual bool Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts) = 0;

protected:
	// structure that describes raw ABSTRACT OS structure in memory -- any linked structure can be described by this opaque structure
	// some of the members are redundant (everything is inside the buffer), but this is for faster retrieval only
	typedef std::vector<std::pair<DWORD_PTR, DWORD_PTR>> ChangeList;
	typedef struct _OS_OBJECT
	{
		BYTE buffer[4096];
		DWORD_PTR dwAddress;
		DWORD dwObjectOffset;
		DWORD dwObjectSize;
		CHAR szImageName[15];
		ChangeList listOfChanges;
		DWORD_PTR dwPID;
		bool bFlinkModified;
		bool bBlinkModified;
	} OS_OBJECT , *POS_OBJECT;
	typedef std::map<DWORD_PTR, OS_OBJECT> ObjectMap;

	// map of object - first parameter will be address of the list with which objects are connected (for example, address of ActiveProcessLinks member in memory) 
	ObjectMap m_objectMap;

	// Flink and Blink pointers of the doubly linked list must be changed in order to "bypass" the target object (process, thread, etc)
	// these are target object Flink/Blink pointers
	// using list of flink/blink pointers because multiple such target objects can exist! (for example, multiple target processes with the same name,
	// multiple threads of the same target process, etc.)
	typedef struct _FLINK_BLINK
	{
		DWORD_PTR dwTargetFlink;
		DWORD_PTR dwTargetBlink;
	} FLINK_BLINK, *PFLINK_BLINK;
	std::vector<FLINK_BLINK> m_targetObjectFlinkBlinkList;

	bool FixAndWriteFlinkBlinkLinks(PBYTE pBuffer, DWORD dwListMemberOffset)
	{
		bool bRet = false;

		std::vector<FLINK_BLINK>::iterator iter;
		for(iter = m_targetObjectFlinkBlinkList.begin(); iter != m_targetObjectFlinkBlinkList.end(); ++iter)
		{
			DWORD_PTR dwTargetFlink = (*iter).dwTargetFlink;
			DWORD_PTR dwTargetBlink = (*iter).dwTargetBlink;

			OS_OBJECT object;
			if(FindAndModifyLinks(dwTargetFlink, dwListMemberOffset, sizeof(DWORD_PTR), dwTargetBlink, object) || FindAndModifyLinks(dwTargetBlink, dwListMemberOffset, 0, dwTargetFlink, object))
			{
				// proper pointer was changed - object can be written to disk
				bRet = DumpOverwriter::Instance().WriteDump(object.dwAddress, pBuffer, 4096, object.listOfChanges);
			}
		}

		return bRet;
	}

	bool FindAndModifyLinks(DWORD_PTR dwNextOrPrev, DWORD dwListMemberOffset, DWORD dwListEntryOffset, DWORD_PTR dwNewPointer, OS_OBJECT &object)
	{
		bool bRet = false;

		// found flink or blink object in the list
		if(m_objectMap.find(dwNextOrPrev) != m_objectMap.end())
		{
			object = m_objectMap[dwNextOrPrev];

			// if flink and blink have not yet been modified, modify it
			if((dwListEntryOffset == 0 && object.bFlinkModified == false) ||
				(dwListEntryOffset == sizeof(DWORD_PTR) && object.bBlinkModified == false))
			{
				// fix flink/blink pointers
				// if FLINK object, then set object->blink = targetobject->blink
				// if BLINK object, then set object->flink = targetobject->flink
				DWORD_PTR dwChangeAddress = object.dwAddress + object.dwObjectOffset + dwListMemberOffset + dwListEntryOffset;
				object.listOfChanges.push_back(std::make_pair(dwChangeAddress, dwNewPointer));
				bRet = true;

				if(dwListEntryOffset == 0)
				{
					object.bFlinkModified = true;
				}
				else
				{
					object.bBlinkModified = true;
				}

				m_objectMap[dwNextOrPrev] = object;

			}
		}

		return bRet;
	}

	virtual void SetDefaultOffsetsAndSizes(void) = 0;
};