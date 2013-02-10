#pragma once

// http://illegalargumentexception.blogspot.fr/2008/04/c-file-hex-dump-application.html -- great help

class HexWriter
{
public:
	HexWriter(void);
	~HexWriter(void);

	static void HexDump(DWORD dwAddress, LPBYTE pbBuffer, DWORD dwBufferSize, DWORD dwHighlightOffset, DWORD dwHighlightLength);

private:
	static std::string DwordToHexString(DWORD dwDword);
	static std::string ByteToHexString(BYTE byte);
	static std::string ByteToHexChar(BYTE byte);
	static std::string SafeASCII(BYTE byte);
};
