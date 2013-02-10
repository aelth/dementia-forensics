#pragma once

#include "../Common/SymbolUDT.h"
#include "../Common/CommonTypesDrv.h"
#include "Logger.h"
#include "ITargetObject.h"

// define function pointer types to functions imported dynamically
typedef bool (*INITDBGSYMS) (VOID);
typedef bool (*GETSYMINFO)(SymbolList &);

class IDriverCommunicator
{
public:
	IDriverCommunicator(void)
	{
		m_isInstalled = false;
		m_symbolsInitialized = false;
		m_driverSymbolCount = 0;
	}
	virtual ~IDriverCommunicator(void)
	{
		// intentionally left empty!
	}

	virtual bool InstallDriver(void) = 0;
	virtual bool RemoveDriver(void) = 0;

	bool InitializeDriverSymbols(void)
	{
		// do nothing if symbols are already initialized
		if(m_symbolsInitialized)
		{
			return true;
		}

		// else load library dynamically
		TCHAR szCurrentDirectory[MAX_PATH + 1];
		if(!GetCurrentDirectory(MAX_PATH, szCurrentDirectory))
		{
			Logger::Instance().Log(_T("Cannot get current directory -- dynamic loading of SymbolHelper failed"), ERR);
			return false;
		}

		TCharString currDir(szCurrentDirectory);
		TCharString szSymbolHelperPath = currDir + _T("\\SymbolHelper.dll");

		// get SymbolHelper module used in GetProcAddress calls
		HMODULE hSymHelper = LoadLibrary(szSymbolHelperPath.c_str());
		if(hSymHelper == NULL)
		{
			Logger::Instance().Log(_T("Handle to SymbolHelper.dll could not be obtained, LoadLibrary failed"), ERR);
			return false;
		}

		// initialize all function pointers
		INITDBGSYMS pfnInitializeDebugSymbols = (INITDBGSYMS) GetProcAddress(hSymHelper, "InitializeDebugSymbols");
		GETSYMINFO pfnGetSymbolsInformation = (GETSYMINFO) GetProcAddress(hSymHelper, "GetSymbolsInformation");

		if(pfnInitializeDebugSymbols == NULL || pfnGetSymbolsInformation == NULL)
		{
			Logger::Instance().Log(_T("Pointers to SymbolHelper functions could not be obtained"), ERR);
			FreeLibrary(hSymHelper);
			return false;
		}

		if(!pfnInitializeDebugSymbols())
		{
			Logger::Instance().Log(_T("Failed to initialize debug symbols"), ERR);
			FreeLibrary(hSymHelper);
			return false;
		}

		// obtain symbol list from the driver
		SymbolList symbolList = GetDriverSymbols();

		if(!pfnGetSymbolsInformation(symbolList))
		{
			Logger::Instance().Log(_T("Failed to retrieve information about symbols"), ERR);
			FreeLibrary(hSymHelper);
			return false;
		}

		if(!SendSymbolsToDriver(symbolList))
		{
			Logger::Instance().Log(_T("Failure while sending symbols to kernel-mode"), ERR);
			FreeLibrary(hSymHelper);
			return false;
		}

		m_symbolsInitialized = true;
		FreeLibrary(hSymHelper);
		return true;
	}

	virtual bool StartHiding(const TargetObjectList &targetObjects) = 0;

protected:
	bool m_isInstalled;
	bool m_symbolsInitialized;
	DWORD m_driverSymbolCount;

	virtual SymbolList GetDriverSymbols(void) = 0;
	virtual bool SendSymbolsToDriver(const SymbolList &symbolList) = 0;

	VOID TransformFromArrayToSymbolList(SymbolList &symbolList, PINTERNAL_SYMBOL pSymbolsArray, const DWORD dwSymbolCount)
	{
		// put symbols into symbol list which will be passed to SymbolHelper
		for(DWORD i = 0; i < dwSymbolCount; i++)
		{
			INTERNAL_SYMBOL pSymbol = pSymbolsArray[i];

			// check if symbol name contains dot ('.') character
			// if yes, then the symbol is UDT - part of the name before the dot represents the name of the "structure"
			// part of the name after the dot represents the member of the structure
			char *pDotChar = NULL;
			if((pDotChar = strchr(pSymbol.name, '.')) != NULL)
			{
				char szUDTStructName[MAX_SYM_NAME];
				char szUDTMemberName[MAX_SYM_NAME];

				// get length of the part of the string before the dot
				DWORD dwUDTStructNameLen = (DWORD) (pDotChar - pSymbol.name);

				// store this part of the string into a separate variable
				strncpy_s(szUDTStructName, MAX_SYM_NAME, pSymbol.name, dwUDTStructNameLen);

				// skip dot
				pDotChar++;

				// copy part of the string after the dot
				strncpy_s(szUDTMemberName, MAX_SYM_NAME, pDotChar, (strlen(pSymbol.name) - dwUDTStructNameLen));

				// check if symbol with the given name is already present in our list
				bool bSymbolExists = false;

				SymbolList::iterator symIter;
				for(symIter = symbolList.begin(); symIter != symbolList.end(); ++symIter)
				{
					// symbol exists - add new child
					if(!strcmp((*symIter)->GetSymbolName().c_str(), szUDTStructName))
					{
						// check if given symbol is a bit field
						if(pSymbol.uBitPosition != -1 && pSymbol.uBitLength != -1)
						{
							(*symIter)->AddWantedChildSymbol(Symbol(new SymbolUDT(szUDTMemberName, false, true)));
						}
						else
						{
							(*symIter)->AddWantedChildSymbol(Symbol(new SymbolUDT(szUDTMemberName)));
						}

						bSymbolExists = true;
						break;
					}
				}

				// if symbol is not yet present in the list, add it
				if(!bSymbolExists)
				{
					Symbol sym(new SymbolUDT(szUDTStructName, true));
					// check if given symbol is a bit field
					if(pSymbol.uBitPosition != -1 && pSymbol.uBitLength != -1)
					{
						sym->AddWantedChildSymbol(Symbol(new SymbolUDT(szUDTMemberName, false, true)));
					}
					else
					{
						sym->AddWantedChildSymbol(Symbol(new SymbolUDT(szUDTMemberName)));
					}

					symbolList.push_back(sym);
				}
			}
			// no dot -- symbol is a plain function name
			else
			{
				symbolList.push_back(Symbol(new SymbolUDT(pSymbol.name, false, false, true)));
			}
		}
	}

	// recursive function which transforms SymbolList vector to linear array, taking all parent-child relationships
	// function returns number of "transformed" items
	DWORD TransformFromSymbolListToArray(const SymbolList &symbolList, PINTERNAL_SYMBOL pSymbolsArray, DWORD dwSymbolIndex, const TCharString &parentStructName)
	{
		SymbolList::const_iterator symIter;
		for(symIter = symbolList.begin(); symIter != symbolList.end(); ++symIter)
		{
			Symbol pSym = (*symIter);
			if(pSym->IsStruct())
			{
				// if it is "abstract" structure, get all children
				const SymbolList childSymbols = pSym->GetChildSymbols();

				// recursively go through children list
				// watch for unicode horror
#ifdef _UNICODE
				dwSymbolIndex = TransformFromSymbolListToArray(childSymbols, pSymbolsArray, dwSymbolIndex, pSym->GetSymbolNameW());
#else
				dwSymbolIndex = TransformFromSymbolListToArray(childSymbols, pSymbolsArray, dwSymbolIndex, pSym->GetSymbolName());
#endif // _UNICODE			
			}
			// for all other cases (address-only, bit-field), just copy all members
			else
			{
				// symIndex should never overflow the array!
				pSymbolsArray[dwSymbolIndex++] = TransformSymbolToInternalSymbol(pSym, parentStructName);
			}
		}

		return dwSymbolIndex;
	}

	INTERNAL_SYMBOL TransformSymbolToInternalSymbol(const Symbol sym, const TCharString &parentStructName)
	{
		// add it directly to symbol array
		INTERNAL_SYMBOL internalSymbol;

		std::string symbolName = "";

		// form name of the form "ABSTRACT_STRUCTURE.MemberVariable"
		if(parentStructName != _T(""))
		{
			symbolName.assign(parentStructName.begin(), parentStructName.end());
			symbolName += ".";
		}

		symbolName += sym->GetSymbolName();

		// copy all fields, although some of the fields are not relevant for certain symbol types
		// for example, if symbol is address-only, only name and address are the relevant fields, all other contain values that are not used
		strcpy_s(internalSymbol.name, MAX_SYM_NAME, symbolName.c_str());
		internalSymbol.u64address = sym->GetAddress();
		internalSymbol.uBitLength = sym->GetBitLength();
		internalSymbol.uBitPosition =  sym->GetBitPosition();
		internalSymbol.uOffset = sym->GetOffsetWithinParent();

		return internalSymbol;
	}
};
