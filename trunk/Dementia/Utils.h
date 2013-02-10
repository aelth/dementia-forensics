#pragma once

class Utils
{
public:
	Utils(void);
	~Utils(void);

	static bool IsAdmin(void);
	static bool GetSeDebugPrivilege(void);
	
private:
	static bool SetPrivilege(HANDLE hToken, LPCTSTR lpszPrivilege, BOOL bEnablePrivilege);
};
