#pragma once

class Injector
{
public:
	Injector(TCHAR *pszDLLFullPath);
	~Injector(void);

	bool InjectToProcess(DWORD dwProcessID);

private:
	boost::scoped_array<TCHAR> m_szDLLFullPath;
};
