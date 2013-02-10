#pragma once

#include "IObjectSearch.h"
#include "IEPROCESSListener.h"
#include "DumpOverwriter.h"

class IConnObject : public IObjectSearch, public IEPROCESSListener
{
public:
	virtual bool Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts)
	{
		bool bFound = false;

		PBYTE pConnObject = pBuffer + dwObjectOffset;
		DWORD_PTR dwConnectionProcessAddressOrID = *((PDWORD_PTR)(pConnObject + m_dwConnectionOwnerOffset));

		// check if this connection object (allocation) belongs to a target process
		if(IsTargetProcessConnection(dwConnectionProcessAddressOrID))
		{
			// the entire connection object will be deleted with the allocation
			bFound = true;
		}
		// if it doesn't belong to a target process - cache it for later processing
		else
		{
			CONN_CACHE conn;
			conn.dwAddress = dwAddress;
			conn.dwObjectOffset = dwObjectOffset;
			conn.dwObjectSize = dwAllocationSize;
			conn.dwTargetProcessPIDOrAddress = dwConnectionProcessAddressOrID;
			m_connectionList.push_back(conn);
		}

		Artefact artefact = CreateArtefact(dwObjectOffset, 15, dwConnectionProcessAddressOrID);
		artefacts.push_back(artefact);

		return bFound;
	}

protected:
	DWORD m_dwConnectionOwnerOffset;
	DWORD m_dwPoolHeaderSize;
	bool m_isPID;
	
	// this structure will be used for storing connections when target process has not yet been found
	typedef struct _CONN_CACHE
	{
		DWORD_PTR dwAddress;
		DWORD dwObjectOffset;
		DWORD dwObjectSize;
		DWORD_PTR dwTargetProcessPIDOrAddress;
	} CONN_CACHE , *PCONN_CACHE;

	std::vector<CONN_CACHE> m_connectionList;
	
	bool IsTargetProcessConnection(DWORD_PTR dwConnProcAddrOrID)
	{
		bool bRet = false;

		// check if EPROCESSSearch has notified us with information about target process
		if(m_processPID != -1 && m_EPROCESSAddress != -1)
		{
			// PID or address of the EPROCESS block of the target process must be equal to current connection's 
			// PID/EPROCESS address of the connection owner
			if(dwConnProcAddrOrID == m_processPID ||
			   dwConnProcAddrOrID == m_EPROCESSAddress)
			{
				bRet = true;
			}
		}

		return bRet;
	}

	// this function will be called when EPROCESSSearch finds the target process
	virtual void ProcessNotification(void)
	{
		// process the cache list and delete all connections that have been cached and belong to the target process
		std::vector<CONN_CACHE>::iterator iter;

		// this can be done more elegantly with remove_if function, but this method also gets the job done
		for(iter = m_connectionList.begin(); iter != m_connectionList.end(); )
		{
			DWORD_PTR dwTargetProcessPIDOrAddress = (*iter).dwTargetProcessPIDOrAddress;
			if(IsTargetProcessConnection(dwTargetProcessPIDOrAddress))
			{
				DWORD_PTR dwAddressOfChange = (*iter).dwAddress + (*iter).dwObjectOffset - m_dwPoolHeaderSize;
				DWORD dwSize = (*iter).dwObjectSize;

				// write new buffer to file
				DumpOverwriter::Instance().WriteDump(dwAddressOfChange, NULL, dwSize);

				// erase the processed block, but be careful with the iterator
				iter = m_connectionList.erase(iter);
			}
			else
			{
				// move to next element as usual
				++iter;
			}
		}
	}
	
	virtual Artefact CreateArtefact(DWORD dwOffset, DWORD dwHighlightLength, DWORD_PTR dwConnectionPIDOrAddress)
	{
		Artefact artefact;
		artefact.dwOffset = dwOffset;
		artefact.dwHighlightLength = dwHighlightLength;

		// format output depending on the structure internals
		std::string szAddressOrPID = "";
		std::string szDescriptionText = "";

		if(m_isPID)
		{
			szAddressOrPID = boost::lexical_cast<std::string>(dwConnectionPIDOrAddress);
			szDescriptionText = "PID = ";
		}
		else
		{
			szAddressOrPID = boost::str(boost::format("%x") % dwConnectionPIDOrAddress);
			szDescriptionText = "EPROCESS @ ";
		}

		// ugly hacks
#ifdef _UNICODE
		std::wstring wszAddressOrPid;
		wszAddressOrPid.assign(szAddressOrPID.begin(), szAddressOrPID.end());
		std::wstring wszDescriptionText;
		wszDescriptionText.assign(szDescriptionText.begin(), szDescriptionText.end());
		artefact.szMessage = _T("Found potential connection block -- owner PROCESS with ") + wszDescriptionText + wszAddressOrPid;
#else
		artefact.szMessage = _T("Found potential connection block -- owner PROCESS with ") + szDescriptionText + szAddressOrPid;
#endif // _UNICODE

		return artefact;
	}

};