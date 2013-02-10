#include "stdafx.h"
#include "SymbolHelper.h"
#include "ExportedFunctions.h"

// static members initialization
SYMINIT SymbolHelper::m_fnSymInitialize = NULL;
SYMCLEAN SymbolHelper::m_fnSymCleanup = NULL;
SYMOPTIONS SymbolHelper::m_fnSymSetOptions = NULL;
FINDEXEIMAGE SymbolHelper::m_fnFindExecutableImage = NULL;
SYMLOAD64 SymbolHelper::m_fnSymLoadModule64 = NULL;
SYMUNLOAD64 SymbolHelper::m_fnSymUnloadModule64 = NULL;
SYMENUMTYPES SymbolHelper::m_fnSymEnumTypes = NULL;
SYMENUMTYPESBYNAME SymbolHelper::m_fnSymEnumTypesByName = NULL;
SYMGETTYPEFROMNAME SymbolHelper::m_fnSymGetTypeFromName = NULL;
SYMGETINFO64 SymbolHelper::m_fnSymGetModuleInfo64 = NULL;
SYMGETTYPEINFO SymbolHelper::m_fnSymGetTypeInfo = NULL;
SYMENUMSYMS SymbolHelper::m_fnSymEnumSymbols = NULL;
SYMNAME SymbolHelper::m_fnSymFromName = NULL;
bool SymbolHelper::m_isDebugHelpInitialized = false;
TCharString SymbolHelper::m_szSymbolsPath;
boost::filesystem::path SymbolHelper::m_basePath;
TCharString	SymbolHelper::m_szMicrosoftSymbolServer(TEXT("*http://msdl.microsoft.com/download/symbols"));



SYMBOLHELPER_API bool InitializeDebugSymbols(void)
{
	return SymbolHelper::InitializeDebugHelpSymbols();
}

SYMBOLHELPER_API bool GetSymbolsInformation(SymbolList &symbolList)
{
	return SymbolHelper::GetSymbolsInformation(symbolList);
}

SymbolHelper::SymbolHelper(void)
{
	// intentionally left empty
}

SymbolHelper::~SymbolHelper(void)
{
	// intentionally left empty
}	

bool SymbolHelper::InitializeDebugHelpSymbols(void)
{
	// first obtain my full path (since this DLL can be loaded in any process,
	// current directory cannot be used for this purpose)
	TCHAR szDLLSelfName[MAX_PATH + 1];
	HMODULE hMyHandle = GetModuleHandle(TEXT("SymbolHelper.dll"));
	if(hMyHandle == NULL)
	{
		OutputDebugString(TEXT("Cannot get handle of this module"));
		return false;
	}

	if(!GetModuleFileName(hMyHandle, szDLLSelfName, MAX_PATH + 1))
	{
		OutputDebugString(TEXT("Full path of this module could not be obtained"));
		return false;
	}

	m_basePath = boost::filesystem::path(szDLLSelfName).parent_path();
	TCharString szDbgHelpPath = m_basePath.string() + "\\dbghelp.dll";

	// get dbghelp.dll handle
	HMODULE hDbgHlp = LoadLibrary(szDbgHelpPath.c_str());

	if(hDbgHlp == NULL)
	{
		OutputDebugString(TEXT("Initialization of dbghelp symbols failed because handle to dbghelp.dll could not be obtained"));
		return false;
	}

	// initialize all function pointers
	m_fnSymInitialize = (SYMINIT) GetProcAddress(hDbgHlp, TEXT("SymInitialize"));
	m_fnSymCleanup = (SYMCLEAN) GetProcAddress(hDbgHlp, TEXT("SymCleanup"));
	m_fnSymSetOptions = (SYMOPTIONS) GetProcAddress(hDbgHlp, TEXT("SymSetOptions"));
	m_fnFindExecutableImage = (FINDEXEIMAGE) GetProcAddress(hDbgHlp, TEXT("FindExecutableImage"));
	m_fnSymLoadModule64 = (SYMLOAD64) GetProcAddress(hDbgHlp, TEXT("SymLoadModule64"));
	m_fnSymUnloadModule64 = (SYMUNLOAD64) GetProcAddress(hDbgHlp, TEXT("SymUnloadModule64"));
	m_fnSymEnumTypes = (SYMENUMTYPES) GetProcAddress(hDbgHlp, TEXT("SymEnumTypes"));
	m_fnSymEnumTypesByName = (SYMENUMTYPESBYNAME) GetProcAddress(hDbgHlp, TEXT("SymEnumTypesByName"));
	m_fnSymGetTypeFromName = (SYMGETTYPEFROMNAME) GetProcAddress(hDbgHlp, TEXT("SymGetTypeFromName"));
	m_fnSymGetModuleInfo64 = (SYMGETINFO64) GetProcAddress(hDbgHlp, TEXT("SymGetModuleInfo64"));
	m_fnSymGetTypeInfo = (SYMGETTYPEINFO) GetProcAddress(hDbgHlp, TEXT("SymGetTypeInfo"));
	m_fnSymEnumSymbols = (SYMENUMSYMS) GetProcAddress(hDbgHlp, TEXT("SymEnumSymbols"));
	m_fnSymFromName = (SYMNAME) GetProcAddress(hDbgHlp, TEXT("SymFromName"));

	if(	m_fnSymInitialize == NULL || m_fnSymCleanup == NULL || m_fnSymSetOptions == NULL || 
		m_fnFindExecutableImage == NULL || m_fnSymLoadModule64 == NULL || m_fnSymUnloadModule64 == NULL || 
		m_fnSymEnumTypes == NULL || m_fnSymEnumTypesByName == NULL || m_fnSymGetTypeFromName == NULL || 
		m_fnSymGetModuleInfo64 == NULL || m_fnSymGetTypeInfo == NULL || m_fnSymEnumSymbols == NULL || m_fnSymFromName == NULL
		)
	{
		OutputDebugString(TEXT("Initialization of dbghelp symbols failed because some of the required functions could not be loaded"));
		return false;
	}

	m_isDebugHelpInitialized = true;

	HANDLE hProcess = GetCurrentProcess();
	TCharString szSymbolsPath = GetSymbolsPath();
	if(szSymbolsPath.empty())
	{
		OutputDebugString(TEXT("Initialization of symbols failed because symbols path cannot be obtained"));
		return false;
	}

	if(!m_fnSymInitialize(	hProcess,						// dummy handle which identifies the caller (not really used)
							szSymbolsPath.c_str(),			// symbol path used
							FALSE							// do not invade the process (don't load symbols for all modules in the process)
						 )
						 )
	{
		OutputDebugString(TEXT("Initialization of private symbols failed because SymInitialize method returned error value"));
		return false;
	}

	// initialize DbgHelp options
	m_fnSymSetOptions(	SYMOPT_UNDNAME |					// we want undecorated symbols (no C++)
						SYMOPT_AUTO_PUBLICS |				// searching private symbols before public ones
						SYMOPT_DEFERRED_LOADS |				// we don't want to load any symbols until we need them
						SYMOPT_CASE_INSENSITIVE |			// our search is case insensitive
						SYMOPT_DEBUG						// and all debug messages are relayed to DbgPrint
		);

	return true;
}

bool SymbolHelper::GetSymbolsInformation(SymbolList &symbolList)
{
	// if debug API has not been initialized
	if(!m_isDebugHelpInitialized)
	{
		// try to initialize it now
		if(!InitializeDebugHelpSymbols())
		{
			return false;
		}
	}

	PSYSTEM_MODULE_INFORMATION_EX pKernelModules = NULL;
	ULONG32 uLen = 0;
	if(WrapNtQuerySystemInformation(SystemModuleInformation, (PVOID *) &pKernelModules, 0, &uLen) != STATUS_SUCCESS)
	{
		OutputDebugString(TEXT("Initialization of private symbols failed because list of kernel modules could not be obtained"));
		VirtualFree(pKernelModules, 0, MEM_RELEASE);
		return false;
	}

	TCharString kernelPath;
	TCharString imageName;

	if(!FormKernelImagePath(pKernelModules->Modules[0].ImageName, kernelPath, imageName))
	{
		OutputDebugString(TEXT("Initialization of private symbols failed because kernel image path could not be formed"));
		VirtualFree(pKernelModules, 0, MEM_RELEASE);
		return false;
	}

	// load needed kernel symbols
	if(!LoadKernelSymbols(GetCurrentProcess(), kernelPath, imageName, (ULONG_PTR) pKernelModules->Modules[0].Base, pKernelModules->Modules[0].Size, symbolList))
	{
		VirtualFree(pKernelModules, 0, MEM_RELEASE);
		return false;
	}

	// it is possible that we got all the way down here, with no errors, but symbols were not found!
	VirtualFree(pKernelModules, 0, MEM_RELEASE);
	return true;

}

TCharString SymbolHelper::GetSymbolsPath(void)
{	
	// if symbol path has already been initialized
	if(!m_szSymbolsPath.empty())
	{
		// return it
		return m_szSymbolsPath;
	}

	// first obtain working directory
	m_szSymbolsPath = m_basePath.string();
	if(m_szSymbolsPath.empty())
	{
		return m_szSymbolsPath;
	}

	// prepend SRV* to it
	m_szSymbolsPath.insert(0, TEXT("SRV*"));

	// download symbols to Symbols directory inside the working directory
	m_szSymbolsPath.append(TEXT("\\Symbols"));
	m_szSymbolsPath.append(m_szMicrosoftSymbolServer);

	return m_szSymbolsPath;
}

bool SymbolHelper::LoadKernelSymbols(HANDLE hProcess, const TCharString &szKernelImagePath, const TCharString &szKernelImageName, const ULONG_PTR uKernelStartAddress, const ULONG_PTR uKernelSize, SymbolList &symbolList)
{
	TCHAR kernelPathDummy[MAX_PATH + 1];

	HANDLE hFile = m_fnFindExecutableImage(	szKernelImagePath.c_str(),				// name of the symbol file to be located (we want kernel symbols)
											GetSymbolsPath().c_str(),				// path were symbol files are located									
											kernelPathDummy							// pointer to a buffer that receives the full path of the executable
										   );
	if(hFile == NULL)
	{
		OutputDebugString(TEXT("Initialization of kernel symbols failed because handle to kernel image could not be opened"));
		return false;
	}

	// using kernel base address, we can now load all symbols for the kernel image (if possible)
	DWORD64 dwModuleAddress = m_fnSymLoadModule64(	hProcess,							// handle which was passed to SymInitialize
													hFile,								// handle to executable image
													szKernelImageName.c_str(),			// name of the executable
													NULL,								// module name
													uKernelStartAddress,				// load address of the module (using base address of the kernel)
													uKernelSize							// size of module
												 );

	if(dwModuleAddress == 0)
	{
		OutputDebugString(TEXT("Initialization of kernel symbols failed because kernel module address could not be obtained"));
		return false;
	}

	ObtainOffsets(hProcess, uKernelStartAddress, NULL, symbolList);

	// CLEANUP
	// first we unload the loaded module and terminate the symbol handler with SymCleanup
	if(!m_fnSymUnloadModule64(hProcess, dwModuleAddress))
	{
		OutputDebugString(TEXT("Could not unload symbol modules, not a critical error"));
	}

	if(!m_fnSymCleanup(hProcess))
	{
		OutputDebugString(TEXT("Could not perform symbol cleanup, not a critical error"));
	}

	// close the handle of the file returned by FindExecutableImage routine
	CloseHandle(hFile);

	return true;	
}

BOOL CALLBACK SymbolHelper::EnumSymProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext)
{
	PDWORD64 pdwSymbolAddress = (PDWORD64) UserContext;
	*pdwSymbolAddress = pSymInfo->Address;

	// continue enumeration
	return true;
}

PTI_FINDCHILDREN_PARAMS WINAPI SymbolHelper::GetChildrenParams(HANDLE hProcess, const ULONG64 uModuleBase, const std::string &symbolName, PULONG64 pul64StructLength)
{
	SYMBOL_INFO symInfo;
	symInfo.SizeOfStruct = sizeof(SYMBOL_INFO);

	// try to get information about the type from the type name
	if(!m_fnSymGetTypeFromName(	hProcess,								// handle which was passed to SymInitialize
								uModuleBase,							// base address of the kernel
								symbolName.c_str(),								// name of the type for which info is being retrieved
								&symInfo
							 )
							 )
	{
		OutputDebugString(TEXT("Could not obtain type information from name"));
		return NULL;
	}

	// before getting children, get my size
	// get number of children types of the specified type
	if(!m_fnSymGetTypeInfo(	hProcess,				// handle which was passed to SymInitialize
							uModuleBase,			// base address of the kernel
							symInfo.TypeIndex,		// type index
							TI_GET_LENGTH,			// size of the struct
							pul64StructLength		// variable which will hold total number of children
							)
							)
	{
		OutputDebugString(TEXT("Could not obtain struct size!"));
		return NULL;
	}

	DWORD dwNumOfChildren = 0;
	// get number of children types of the specified type
	if(!m_fnSymGetTypeInfo(	hProcess,				// handle which was passed to SymInitialize
							uModuleBase,			// base address of the kernel
							symInfo.TypeIndex,		// type index
							TI_GET_CHILDRENCOUNT,	// we want number of children elements
							&dwNumOfChildren		// variable which will hold total number of children
						   )
						   )
	{
		OutputDebugString(TEXT("Could not obtain children information, i.e. number of symbol children"));
		return NULL;
	}

	// if call succeeded, but there are no children, continue
	if(dwNumOfChildren == 0)
	{
		OutputDebugString(TEXT("Children count retrieval successful, but no children found!"));
		return NULL;
	}

	// get all children type indices
	DWORD dwSizeofParams = sizeof(TI_FINDCHILDREN_PARAMS) + dwNumOfChildren * sizeof(ULONG);
	PTI_FINDCHILDREN_PARAMS pChildrenParams = (PTI_FINDCHILDREN_PARAMS) VirtualAlloc(NULL, dwSizeofParams, MEM_COMMIT, PAGE_READWRITE);
	pChildrenParams->Count = dwNumOfChildren;
	pChildrenParams->Start = 0;

	if(!m_fnSymGetTypeInfo(	hProcess,				// handle which was passed to SymInitialize
							uModuleBase,			// base address of the kernel
							symInfo.TypeIndex,		// type index
							TI_FINDCHILDREN,		// we want to retrieve indexes of all children elements
							pChildrenParams			// variable which will hold array of children indexes
						  )
						  )
	{
		OutputDebugString(TEXT("Could not obtain children information, i.e. enumeration of children symbols failed"));
		VirtualFree(pChildrenParams, 0, MEM_RELEASE);
		return NULL;
	}
	
	return pChildrenParams;
}

/* 
	TODO:	This function is a bit messy - recursion coupled with iteration and some stack fiddling.
*/
void SymbolHelper::ObtainOffsets(HANDLE hProcess, const ULONG64 uModuleBase, PTI_FINDCHILDREN_PARAMS pChildrenParams, SymbolList &symbolList)
{
	// check whether types are already defined - define if they're not
	if(symbolList.empty())
	{
		OutputDebugString(TEXT("Symbol list argument is empty"));
		return;
	}

	SymbolList::iterator symbolIterator;
	BOOL bRet = FALSE;
	std::stack<PTI_FINDCHILDREN_PARAMS> childrenParamsStack;

	// for every wanted UDT
	for(symbolIterator = symbolList.begin(); symbolIterator != symbolList.end(); ++symbolIterator)
	{
		// symbol is an "abstract" symbol - a structure
		if((*symbolIterator)->IsStruct())
		{
			// if some parameters already exist and new struct member was encountered, add current params to stack
			if(pChildrenParams != NULL)
			{
				childrenParamsStack.push(pChildrenParams);
			}

			ULONG64 ul64StructSize = 0;
			pChildrenParams = GetChildrenParams(hProcess, uModuleBase, (*symbolIterator)->GetSymbolName(), &ul64StructSize);
			(*symbolIterator)->SetStructSize(ul64StructSize);

			if(pChildrenParams == NULL)
			{
				OutputDebugString(TEXT("Error while obtaining children members of a symbol - GetChildrenParams returned NULL"));
				continue;
			}

			ObtainOffsets(hProcess, uModuleBase, pChildrenParams, (*symbolIterator)->GetChildSymbols());
			// free memory allocated by the function if the allocation was successful, and struct symbol is currently being analyzed
			if(pChildrenParams != NULL)
			{
				VirtualFree(pChildrenParams, 0, MEM_RELEASE);
				pChildrenParams = NULL;
			}
		}
		// if current symbol is a function or variable (i.e. only address needs to be obtained)
		else if((*symbolIterator)->IsAddressOnly())
		{
			DWORD64 dw64Address = 0;
			// get symbol address using SymEnumSymbols procedure
			BOOL bRet = m_fnSymEnumSymbols(	hProcess,									// handle which was passed to SymInitialize
											uModuleBase,								// base address of the kernel
											(*symbolIterator)->GetSymbolName().c_str(),	// regular expression which indicates the names of the symbols to be enumerated
											EnumSymProc,								// callback method which receives the symbol information
											&dw64Address								// context passed to previous callback method
										   );

			if(bRet == FALSE)
			{
				OutputDebugString(TEXT("Error while enumerating symbol addresses - SymEnumSymbols failed"));
				continue;
			}

			(*symbolIterator)->SetAddress(dw64Address);
		}
		// real member of a struct - get offset
		else
		{
			PWCHAR pszSymbolName = NULL;

			// if current children params are NULL, try to retrieve the ones from the stack
			if(pChildrenParams == NULL)
			{
				if(childrenParamsStack.empty())
				{
					OutputDebugString(TEXT("Trying to get member offset, but children parameters structure is empty!"));
					return;
				}

				pChildrenParams = childrenParamsStack.top();
				childrenParamsStack.pop();
			}

			DWORD dwNumOfChildren = pChildrenParams->Count;

			// for every child
			for(DWORD i = 0; i < dwNumOfChildren; i++)
			{
				// obtain its name
				
				bRet = m_fnSymGetTypeInfo(	hProcess,						// handle which was passed to SymInitialize
											uModuleBase,					// base address of the kernel
											pChildrenParams->ChildId[i],	// child type index
											TI_GET_SYMNAME,					// name of the symbol
											&pszSymbolName					// pointer to variable which will contain name of the symbol
											);
				// if error occurred, continue with the next child
				if(bRet == FALSE)
				{
					if(pszSymbolName != NULL)
					{
						LocalFree(pszSymbolName);
					}
					continue;
				}

				// if we are on our current symbol
				if(!(*symbolIterator)->GetSymbolNameW().compare(pszSymbolName))
				{
					// remember offset inside the structure
					DWORD dwChildOffset = 0;
					bRet = m_fnSymGetTypeInfo(	hProcess,						// handle which was passed to SymInitialize
												uModuleBase,					// base address of the kernel
												pChildrenParams->ChildId[i],	// child type index
												TI_GET_OFFSET,					// offset of the child structure inside the parent
												&dwChildOffset					// pointer to variable which will receive the offset
											 );

					if(bRet == FALSE)
					{
						OutputDebugString(TEXT("Cannot get offset of a member variable!"));
						break;
					}

					(*symbolIterator)->SetOffsetWithinParent(dwChildOffset);

					// if this is bitfield, we want bit length and bit position in the variable
					if((*symbolIterator)->IsBitField())
					{
						DWORD dwBitLength;
						bRet = m_fnSymGetTypeInfo(	hProcess,						// handle which was passed to SymInitialize
													uModuleBase,					// base address of the kernel
													pChildrenParams->ChildId[i],	// child type index
													TI_GET_LENGTH,					// length in bits inside the bitfield
													&dwBitLength					// pointer to variable which will receive the offset
							);

						if(bRet == FALSE)
						{
							OutputDebugString(TEXT("Cannot get length of a bitfield in bits for a member!"));
							break;
						}

						(*symbolIterator)->SetBitLength(dwBitLength);

						DWORD dwBitPosition;
						bRet = m_fnSymGetTypeInfo(	hProcess,						// handle which was passed to SymInitialize
													uModuleBase,					// base address of the kernel
													pChildrenParams->ChildId[i],	// child type index
													TI_GET_BITPOSITION,				// bit position of a bitfield
													&dwBitPosition					// pointer to variable which will receive the offset
												 );

						if(bRet == FALSE)
						{
							OutputDebugString(TEXT("Cannot get bit field offset of a member!"));
							break;
						}
					
						(*symbolIterator)->SetBitPosition(dwBitPosition);
					}
					break;
				}
			}

			if(pszSymbolName != NULL)
			{
				LocalFree(pszSymbolName);
			}
		}
	}
}

DWORD SymbolHelper::WrapNtQuerySystemInformation(SYSTEM_INFORMATION_CLASS systemInfoClass, OUT PVOID *ppSystemInfo, ULONG uCurrentSize, OUT PULONG32 puReturnLength)
{
	ULONG uLen = uCurrentSize;
	DWORD status = STATUS_INFO_LENGTH_MISMATCH;

	NTQUERYSYSINFO pfnNtQuerySystemInformation = (NTQUERYSYSINFO) GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")), ("NtQuerySystemInformation"));
	// if this is the first time we're trying to use NtQuerySystemInformation...
	if(pfnNtQuerySystemInformation == NULL)
	{
		OutputDebugString(TEXT("Could not obtain NtQuerySystemInformation address"));
		return STATUS_ACCESS_VIOLATION;
	}

	// if caller wants that we allocate memory for the buffer, we will do it
	if(uCurrentSize == 0 && *ppSystemInfo == NULL)
	{
		// allocate dummy 4-byte buffer for storing system information - this buffer won't be actually used for storage
		ULONG uDummySize = 4;
		PVOID buf = VirtualAlloc(NULL, uDummySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if(buf == NULL)
		{
			return STATUS_NO_MEMORY;
		}

		// with this first call to NtQuerySystemInformation
		// we determine the required size of the buffer - this size is stored in len variable
		status = pfnNtQuerySystemInformation(systemInfoClass, buf, uDummySize, &uLen);

		// if NtQuerySystemInformation returned error different than STATUS_INFO_LENGTH_MISMATCH, then something
		// differently went wrong and we don't know how to handle this situation
		if(status != STATUS_SUCCESS && status != STATUS_INFO_LENGTH_MISMATCH)
		{
			OutputDebugString(TEXT("NtQuerySystemInformation failed while storing info into 4-byte dummy buffer"));
			return status;
		}

		// release the dummy buffer
		VirtualFree(buf, 0, MEM_RELEASE);

		*ppSystemInfo = VirtualAlloc(NULL, uLen, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if(*ppSystemInfo == NULL)
		{
			return STATUS_NO_MEMORY;
		}
	}

	// it's possible that length will "fluctuate" between separate NtQuerySystemInformation calls
	// we thus use infinite loop which will break on failure (not length mismatch) or success
	while(true)
	{
		// allocate new buffer of adequate capacity
		ULONG uNewReturnLength = 0;

		// get system information
		status = pfnNtQuerySystemInformation(systemInfoClass, *ppSystemInfo, uLen, &uNewReturnLength);
		uLen = uNewReturnLength;

		// if everything went ok, we break
		if(status == STATUS_SUCCESS)
		{
			break;
		}
		// as noted previously, size of the buffer between separate calls may fluctuate
		// for example, if many processes exist, and some are deleted between these two calls
		else if(status == STATUS_INFO_LENGTH_MISMATCH)
		{
			// someone could try DoS against us, by always returning length mismatch status
			// we break if required size is greater than given threshold (10 MB)
			if(uLen >= 0xA00000)
			{
				OutputDebugString(TEXT("Information requested from the system exceeds 10 MBs, assuming this is malicious behaviour"));
				VirtualFree(*ppSystemInfo, 0, MEM_RELEASE);
				break;
			} 
			else
			{
				// 256-byte increment, try to "unroll" this loop a bit
				uLen += 0x100;

				// free old buffer and allocate new one with increased size
				VirtualFree(*ppSystemInfo, 0, MEM_RELEASE);
				*ppSystemInfo = VirtualAlloc(NULL, uLen, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
				if(*ppSystemInfo == NULL)
				{
					return STATUS_NO_MEMORY;
				}
				continue;
			}
		}
		else
		{
			OutputDebugString(TEXT("NtQuerySystemInformation failed while retrieving system information into buffer of appropriate capacity"));
			VirtualFree(*ppSystemInfo, 0, MEM_RELEASE);
			break;
		}
	}

	// set length parameter to the length which was required
	*puReturnLength = uLen;
	return status;
}

bool SymbolHelper::FormKernelImagePath(IN const TCharString &kernelImageName, OUT TCharString &fullKernelPath, OUT TCharString &shortImageName)
{
	TCHAR systemDirectory[MAX_PATH + 1];
	bool retStatus = false;

	if(!GetSystemDirectory(systemDirectory, MAX_PATH))
	{
		OutputDebugString(TEXT("System directory cannot be obtained and kernel image path cannot be formed"));
		return retStatus;
	}

	boost::filesystem::path kernelPath(systemDirectory);
	const TCharString::size_type npos = -1;
	TCharString::size_type lastDirSeparatorIndex = kernelImageName.find_last_of(TEXT("\\"));
	if(lastDirSeparatorIndex != npos)
	{
		shortImageName.clear();
		shortImageName.append(kernelImageName, lastDirSeparatorIndex + 1, MAX_PATH);
		kernelPath /= shortImageName;
		fullKernelPath = kernelPath.string();
		retStatus = true;
	}
	else
	{
		OutputDebugString(TEXT("Cannot find last separator in passed kernel image name"));
		retStatus = false;
	}

	return retStatus;
}