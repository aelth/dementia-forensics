#include "stdafx.h"
#include "SymbolUDT.h"

SymbolUDT::SymbolUDT(const std::string &szSymbolName, bool isStruct, bool isBitField, bool isAddress)
	: m_szSymbolName(szSymbolName) 
{
	m_symbolID = 0;
	m_offsetWithinParent = -1;
	m_address = -1;
	m_bitPosition = -1;
	m_bitLength = -1;
	m_structSize = -1;
	m_isStruct = isStruct;
	m_isAddressOnly = isAddress;
	m_isBitField = isBitField;
}

SymbolUDT::SymbolUDT(const SymbolUDT &symbol)
	: m_szSymbolName(symbol.m_szSymbolName)
{
	// copy constructor copies all wanted parameters
	m_symbolID = symbol.m_symbolID;
	m_offsetWithinParent = symbol.m_offsetWithinParent;
	m_address = symbol.m_address;
	m_bitPosition = symbol.m_bitPosition;
	m_bitLength = symbol.m_bitLength;
	m_structSize = symbol.m_structSize;
	m_isStruct = symbol.m_isStruct;
	m_isAddressOnly = symbol.m_isAddressOnly;
	m_isBitField = symbol.m_isBitField;
	m_parentSymbol.reset(symbol.m_parentSymbol.get());

	SymbolList::const_iterator iter;
	for(iter = symbol.m_wantedChildSymbols.begin(); iter != symbol.m_wantedChildSymbols.end(); ++iter)
	{
		m_wantedChildSymbols.push_back(*iter);
	}
}

SymbolUDT::~SymbolUDT(void)
{
	// intentionally left empty
}
std::wstring SymbolUDT::GetSymbolNameW(void) const
{
	std::wstring ws;
	ws.append(m_szSymbolName.begin(), m_szSymbolName.end());

	return ws;
}


void SymbolUDT::AddWantedChildSymbol(Symbol symbol)
{
	// first try to see if symbol with this name already exists
	SymbolList::const_iterator childsIter;
	for(childsIter = m_wantedChildSymbols.begin(); childsIter != m_wantedChildSymbols.end(); ++childsIter)
	{
		if(!(*childsIter)->GetSymbolName().compare(symbol->GetSymbolName()))
		{
			// if name exists, exit
			return;
		}
	}

	m_wantedChildSymbols.push_back(symbol);
}

void SymbolUDT::SetSymbol(const Symbol &symbol)
{
	m_szSymbolName = symbol->GetSymbolName();
	m_symbolID = symbol->GetSymbolID();
	m_offsetWithinParent = symbol->GetOffsetWithinParent();
	m_address = symbol->GetAddress();
	m_bitPosition = symbol->GetBitPosition();
	m_bitLength = symbol->GetBitLength();
	m_structSize = symbol->GetStructSize();
	m_isStruct = symbol->IsStruct();
	m_isAddressOnly = symbol->IsAddressOnly();
	m_isBitField = symbol->IsBitField();
}

