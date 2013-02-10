#include "StdAfx.h"
#include "Utils.h"
#include "Logger.h"

Utils::Utils(void)
{
	// intentionally left empty
}

Utils::~Utils(void)
{
	// intentionally left empty
}

/*
	This function is more or less taken from the Microsoft documentation:
	http://support.microsoft.com/kb/q118626
*/
bool Utils::IsAdmin(void)
{
	bool bIsAdmin = false;
	HANDLE hToken = NULL;
	HANDLE hImpersonationToken = NULL;
	PACL pACL = NULL;
	PSECURITY_DESCRIPTOR psdAdmin = NULL;
	PSID psidAdmin = NULL;

	// obtain a primary token and then create a duplicate impersonation token.
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_DUPLICATE|TOKEN_QUERY, TRUE, &hToken))
	{
		if (GetLastError() != ERROR_NO_TOKEN)
		{
			Logger::Instance().Log(_T("Cannot obtain token of the current thread - NO TOKEN returned"), ERR);
			return bIsAdmin;
		}

		// if we cannot obtain thread token, try to obtain the process token
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE|TOKEN_QUERY, &hToken))
		{
			Logger::Instance().Log(_T("Cannot obtain token of the current process"), ERR);
			return bIsAdmin;
		}
	}
	
	if (!DuplicateToken (hToken, SecurityImpersonation, &hImpersonationToken))
	{
		Logger::Instance().Log(_T("Failure while duplicating impersonation token (SecurityImpersonation)"), ERR);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	// create SID that represents the local administrators group.
	SID_IDENTIFIER_AUTHORITY SystemSidAuthority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&SystemSidAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &psidAdmin))
	{
		Logger::Instance().Log(_T("Could not create SID for local admin group"), ERR);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	// create the security descriptor and DACL with an ACE that allows only local admins access.
	psdAdmin = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (psdAdmin == NULL)
	{
		Logger::Instance().Log(_T("Could not allocate memory for SID"), ERR);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	if (!InitializeSecurityDescriptor(psdAdmin, SECURITY_DESCRIPTOR_REVISION))
	{
		Logger::Instance().Log(_T("Could not initialize local admin SID"), ERR);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}
		
	// compute size needed for the ACL and allocate memory
	DWORD dwACLSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(psidAdmin) - sizeof(DWORD);
	pACL = (PACL)LocalAlloc(LPTR, dwACLSize);
	if (pACL == NULL)
	{
		Logger::Instance().Log(_T("Could not allocate memory for ACL"), ERR);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	if (!InitializeAcl(pACL, dwACLSize, ACL_REVISION2))
	{
		Logger::Instance().Log(_T("Could not initialize ACL"), ERR);
		if (pACL) LocalFree(pACL);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	const DWORD ACCESS_READ = 1;
	const DWORD ACCESS_WRITE = 2;
	DWORD dwAccessMask= ACCESS_READ | ACCESS_WRITE;
	
	// grant access to the passed admin SID
	if (!AddAccessAllowedAce(pACL, ACL_REVISION2, dwAccessMask, psidAdmin))
	{
		Logger::Instance().Log(_T("Granting access to admin SID failed"), ERR);
		if (pACL) LocalFree(pACL);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	if (!SetSecurityDescriptorDacl(psdAdmin, TRUE, pACL, FALSE))
	{
		Logger::Instance().Log(_T("Set DACL failed"), ERR);
		if (pACL) LocalFree(pACL);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	// set the group and owner so that enough of the security descriptor is filled out to
	// fulfill AccessCheck
	SetSecurityDescriptorGroup(psdAdmin, psidAdmin, FALSE);
	SetSecurityDescriptorOwner(psdAdmin, psidAdmin, FALSE);

	if (!IsValidSecurityDescriptor(psdAdmin))
	{
		Logger::Instance().Log(_T("SID invalid, cannot check access"), ERR);
		if (pACL) LocalFree(pACL);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	DWORD dwAccessDesired = ACCESS_READ;

	// after that, perform the access check. This will determine whether
	// the current user is a local admin.
	// initialize GenericMapping structure even though you do not use generic rights
	GENERIC_MAPPING GenericMapping;
	GenericMapping.GenericRead    = ACCESS_READ;
	GenericMapping.GenericWrite   = ACCESS_WRITE;
	GenericMapping.GenericExecute = 0;
	GenericMapping.GenericAll     = ACCESS_READ | ACCESS_WRITE;

	DWORD dwStatus;
	DWORD dwStructureSize = sizeof(PRIVILEGE_SET);
	PRIVILEGE_SET ps;

	BOOL tmpIsAdmin = FALSE;
	if (!AccessCheck(psdAdmin, hImpersonationToken, dwAccessDesired, &GenericMapping, &ps, &dwStructureSize, &dwStatus, &tmpIsAdmin))
	{
		bIsAdmin = false;
		Logger::Instance().Log(_T("AccessCheck failed"), ERR);
		if (pACL) LocalFree(pACL);
		if (psdAdmin) LocalFree(psdAdmin);
		if (psidAdmin) FreeSid(psidAdmin);
		if (hImpersonationToken) CloseHandle (hImpersonationToken);
		if (hToken) CloseHandle (hToken);
		return bIsAdmin;
	}

	bIsAdmin = (bool) tmpIsAdmin;
	return bIsAdmin;
}

bool Utils::GetSeDebugPrivilege(void)
{
	HANDLE hToken = NULL;

	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		Logger::Instance().Log(_T("Cannot obtain token of the current process"), ERR);
		return false;
	}

	return SetPrivilege(hToken, SE_DEBUG_NAME, TRUE);
}

bool Utils::SetPrivilege(HANDLE hToken, LPCTSTR lpszPrivilege, BOOL bEnablePrivilege)
{
	LUID luid;
	TCharString privilege(lpszPrivilege);

	// lookup passed privilege on the local system and store it into LUID
	if (!LookupPrivilegeValue(NULL, lpszPrivilege, &luid ))
	{
		Logger::Instance().Log(_T("Cannot lookup \"") + privilege + _T("\" privilege"), ERR);
		return false;
	}

	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	if (bEnablePrivilege)
	{
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	}
	else
	{
		tp.Privileges[0].Attributes = 0;
	}

	// enable the privilege or disable all privileges.
	if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES) NULL, (PDWORD) NULL))
	{ 
		Logger::Instance().Log(_T("Failed to adjust token \"") + privilege + _T("\" privilege"), ERR);
		return false;
	} 

	return true;
}


