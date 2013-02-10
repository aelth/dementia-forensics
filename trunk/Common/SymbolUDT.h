#pragma once

/*	DbgHelp functions are terribly documented - everything about symbols was learned through disassembly and
great program SymbolTypeViewer by laboskopia (it's not open source, but it's written in C#:) ).

This class and entire handling of *USER DEFINED TYPES* in SymbolHelper is
poorly written. Handling of functions and sending them to driver works perfectly, but UDTs are
different - they are extremely interconnected (tree like structures with many references back to
different "trees") and very hard to deal with.

Lack of time prevented me to make better working and looking class:(

NOTE:	WCHARs must be used instead of TCHARs. Why? Because careless DbgHelp authors sometimes
use TCHARs and sometimes WCHARs, but conversion presents big runtime overhead, especially
in unoptimized data structures such as the ones used here in SymbolHelper class...
*/
class SymbolUDT;
typedef boost::shared_ptr<SymbolUDT> Symbol;
typedef std::vector<Symbol> SymbolList;
typedef std::map<std::string, SymbolList> SymbolsMap;


class SymbolUDT
{
public:
	SymbolUDT(const std::string &szSymbolName, bool isStruct = false, bool isBitField = false, bool isAddress = false);
	SymbolUDT(const SymbolUDT &symbol);
	~SymbolUDT(void);

	std::string GetSymbolName(void) const { return m_szSymbolName; }
	std::wstring GetSymbolNameW(void) const;
	void SetSymbolName(const std::string &szSymbolName) { m_szSymbolName = szSymbolName; }

	ULONG GetSymbolID(void) const { return m_symbolID; }
	void SetSymbolID(ULONG symID) { m_symbolID = symID; }

	DWORD GetOffsetWithinParent(void) const { return m_offsetWithinParent; }
	void SetOffsetWithinParent(DWORD parentOffset) { m_offsetWithinParent = parentOffset; }

	DWORD64 GetAddress(void) const { return m_address; }
	void SetAddress(DWORD64 address) { m_address = address; }

	bool IsStruct(void) const { return m_isStruct; }
	void SetStruct(void) { m_isStruct = true; }

	bool IsAddressOnly(void) const { return m_isAddressOnly; }
	void SetAddressOnly(void) { m_isAddressOnly = true; }

	DWORD GetStructSize(void) const { return m_structSize; }
	void SetStructSize(DWORD dwSize) { m_structSize = dwSize; }

	bool IsBitField(void) const { return m_isBitField; }
	void SetBitField(void) { m_isBitField = true; }

	DWORD GetBitPosition(void) const { return m_bitPosition; }
	void SetBitPosition(DWORD bitPosition) { m_bitPosition = bitPosition; }

	DWORD GetBitLength(void) const { return m_bitLength; }
	void SetBitLength(DWORD bitLength) { m_bitLength = bitLength; }

	void SetSymbol(const Symbol &symbol);

	Symbol GetParentSymbol() const { return m_parentSymbol; }
	void SetParentSymbol(const Symbol parentSymbol) { m_parentSymbol = parentSymbol; }

	void AddWantedChildSymbol(Symbol symbol);
	SymbolList GetChildSymbols(void) const { return m_wantedChildSymbols; }

private:
	std::string m_szSymbolName;
	ULONG m_symbolID;
	DWORD m_offsetWithinParent;
	DWORD64 m_address;
	ULONG m_structSize;
	bool m_isBitField;
	// if isStruct = true, then this symbol is only a "container" for other symbols - it doesn't have offset!
	bool m_isStruct;
	// if isAddressOnly = true, no offsets are collected, only address of the object (i.e. function, variable, etc.) in memory is collected
	bool m_isAddressOnly;
	DWORD m_bitPosition;
	DWORD m_bitLength;
	Symbol m_parentSymbol;
	SymbolList m_wantedChildSymbols;
};