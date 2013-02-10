#pragma once
#include "../Common/SymbolUDT.h"

// this struct will be used for returning information about found artifacts
typedef struct _Artifact
{
	DWORD dwOffset;
	DWORD dwHighlightLength;
	TCharString szMessage;
} Artefact;


class IObjectSearch
{
public:
	virtual ~IObjectSearch(void)
	{
		// intentionally left empty
	}

	virtual std::string GetName(void) const
	{
		return m_szObjName;
	}

	virtual DWORD GetTag(void) const
	{
		return m_dwTag;
	}

	virtual SymbolList GetSymbols(void) const
	{
		return m_symbolList;
	}

	virtual bool Search(PBYTE pBuffer, DWORD dwSize, DWORD dwAllocationSize, DWORD_PTR dwAddress, DWORD dwObjectOffset, std::vector<Artefact> &artefacts) = 0;

	virtual void LinearizeSymbols() { LinearizeSymbols(m_symbolList); }
protected:
	// object name should explicitly be string, since DBGHELP API developers are kind a lost in UNICODE and non-UNICODE, just as I am:)
	std::string m_szObjName;
	DWORD m_dwTag;
	SymbolList m_symbolList;
	std::map<std::string, DWORD> m_offsetMap;
	std::map<std::string, DWORD> m_structSizeMap;

	// first member of this map is structure member
	// second member is pair (bit position inside bitfield, length in bits)
	std::map<std::string, std::pair<DWORD, DWORD>> m_bitFieldMap;

	// this function takes all of the symbols from the vector/list, and puts it into offset map
	// main purpose of this function is to have quick lookup of wanted values
	virtual void LinearizeSymbols(const SymbolList &symbolList)
	{
		SymbolList::const_iterator symbolIt;
		for(symbolIt = symbolList.begin(); symbolIt != symbolList.end(); ++symbolIt)
		{
			// get all child symbols of a given "abstract" symbol (symbol representing a class, structure, just a name, like _EPROCESS)
			// depth first search (in a way)
			if((*symbolIt)->IsStruct())
			{
				// for all abstract symbols (structs), save size into special size map
				m_structSizeMap[(*symbolIt)->GetSymbolName()] = (*symbolIt)->GetStructSize();
				LinearizeSymbols((*symbolIt)->GetChildSymbols());
			}
			// if it is bit field, store it to a special map
			else if((*symbolIt)->IsBitField())
			{
				m_bitFieldMap[(*symbolIt)->GetSymbolName()] = std::make_pair((*symbolIt)->GetBitPosition(), (*symbolIt)->GetBitLength());
			}
			else
			{
				// use [] operator instead of insert because it automatically updates the value in the map (insert leaves the old value)
				m_offsetMap[(*symbolIt)->GetSymbolName()] = (*symbolIt)->GetOffsetWithinParent();
			}
		}
	}

	virtual void SetDefaultOffsetsAndSizes(void) = 0;
};
