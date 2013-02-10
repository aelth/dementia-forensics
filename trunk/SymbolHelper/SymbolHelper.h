#include "SymbolStructs.h"
#include "../Common/SymbolUDT.h"
#include "../Common/WinternalStructures.h"

class SymbolHelper
{
public:
	SymbolHelper(void);
	~SymbolHelper(void);

	static bool InitializeDebugHelpSymbols(void);
	static bool GetSymbolsInformation(SymbolList &symbolList);

private:
	static SYMINIT m_fnSymInitialize;
	static SYMCLEAN m_fnSymCleanup;
	static SYMOPTIONS m_fnSymSetOptions;
	static FINDEXEIMAGE m_fnFindExecutableImage;
	static SYMLOAD64 m_fnSymLoadModule64;
	static SYMUNLOAD64 m_fnSymUnloadModule64;
	static SYMENUMTYPES m_fnSymEnumTypes;
	static SYMENUMTYPESBYNAME m_fnSymEnumTypesByName;
	static SYMGETTYPEFROMNAME m_fnSymGetTypeFromName;
	static SYMGETINFO64 m_fnSymGetModuleInfo64;
	static SYMGETTYPEINFO m_fnSymGetTypeInfo;
	static SYMENUMSYMS m_fnSymEnumSymbols;
	static SYMNAME m_fnSymFromName;
	static bool m_isDebugHelpInitialized;
	static TCharString m_szSymbolsPath;
	static boost::filesystem::path m_basePath;
	static TCharString m_szMicrosoftSymbolServer;

	typedef DWORD (WINAPI *NTQUERYSYSINFO) (SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

	static void InitializeUserDefinedTypes(void);
	static TCharString GetSymbolsPath(void);
	static DWORD WrapNtQuerySystemInformation(SYSTEM_INFORMATION_CLASS systemInfoClass, OUT PVOID *ppSystemInfo, ULONG uCurrentSize, OUT PULONG32 puReturnLength);
	static bool FormKernelImagePath(IN const TCharString &kernelImageName, OUT TCharString &fullKernelPath, OUT TCharString &shortImageName);
	static bool	LoadKernelSymbols(HANDLE hProcess, const TCharString &szKernelImagePath, const TCharString &szKernelImageName, const ULONG_PTR uKernelStartAddress, const ULONG_PTR uKernelSize, SymbolList &symbolList);

	static BOOL CALLBACK EnumSymProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext);

	// user is responsible for cleaning up the children params!
	static PTI_FINDCHILDREN_PARAMS WINAPI GetChildrenParams(HANDLE hProcess, const ULONG64 uModuleBase, const std::string &symbolName, PULONG64 pul64StructLength);
	static void ObtainOffsets(HANDLE hProcess, const ULONG64 uModuleBase, PTI_FINDCHILDREN_PARAMS pChildrenParams, SymbolList &childVector);
};