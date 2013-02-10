#include "StdAfx.h"
#include "HexWriter.h"
#include "Logger.h"

HexWriter::HexWriter(void)
{
}

HexWriter::~HexWriter(void)
{
}

std::string HexWriter::DwordToHexString(DWORD dwDword)
{
	return	ByteToHexString((dwDword >> 24) & 0xFF) +
			ByteToHexString((dwDword >> 16) & 0xFF) + 
			ByteToHexString((dwDword >> 8) & 0xFF) + 
			ByteToHexString(dwDword & 0xFF);
}

std::string HexWriter::ByteToHexString(BYTE byte)
{
	std::string ret = "";

	BYTE char1 = (byte & 0x0F);
	BYTE char2 = (byte & 0xF0) >> 4;

	ret = ret + ByteToHexChar(char2) + ByteToHexChar(char1);
	return ret;
}

std::string HexWriter::ByteToHexChar(BYTE byte)
{
	std::string	hexChar = "";

	if(byte >= 0 && byte <= 9) 
	{
		// 48 == ASCII Zero
		hexChar += (char) (byte + 48);
	}
	else if(byte >= 10 && byte <= 15) 
	{
		// 97 = ASCII 'a' (small a)
		hexChar += (char) (byte + 97 - 10);
	}
	else
	{
		hexChar = "?";
	}

	return hexChar;
}

std::string HexWriter::SafeASCII(BYTE byte)
{
	std::string asciiByte = "";
	if(byte >= 32 && byte <= 126) 
	{
		asciiByte += byte;
	}
	else
	{
		asciiByte = ".";
	}
	
	return asciiByte;
}

void HexWriter::HexDump(DWORD dwAddress, LPBYTE pbBuffer, DWORD dwBufferSize, DWORD dwHighlightOffset, DWORD dwHighlightLength)
{
	DWORD dwWrapSize = 15;
	DWORD dwWindowSize = 127;

	if(dwHighlightOffset >= dwBufferSize)
	{
		Logger::Instance().Log(_T("Highlight offset cannot be larger than buffer size!"), ERR);
		return;
	}

	if(	dwHighlightLength >= dwWindowSize || 
		(dwHighlightOffset + dwHighlightLength >= dwBufferSize))
	{
		Logger::Instance().Log(_T("Invalid highlight length!"), ERR);
		return;
	}

	// use integers on purpose, don't convert to decimal
	//DWORD dwPreHighlightLength = (dwWindowSize - dwHighlightLength) / 2;
	DWORD dwPreHighlightLength = 0;
	DWORD dwPostHighlightLength = dwWindowSize - dwPreHighlightLength - dwHighlightLength;

	DWORD dwStartOffset = dwHighlightOffset - dwPreHighlightLength;
	DWORD dwEndOffset = dwHighlightOffset + dwHighlightLength + dwPostHighlightLength;

	// check whether start and end offsets are out of the buffer and write shorter output (too lazy to shift the window)
	if(dwStartOffset < 0)
	{
		dwStartOffset = 0;
	}
	if(dwEndOffset >= dwBufferSize)
	{
		dwEndOffset = dwBufferSize - 1;
	}

	std::string hex = "";
	std::string ascii = "";
	
	// address points to start of the buffer - "shift" to the specified highlight offset
	dwAddress += dwHighlightOffset;
	for(DWORD i = dwStartOffset; i <= dwEndOffset; i++)
	{
		// if start or end of the line
		if(dwWrapSize % 15 == 0 && dwWrapSize != 0)
		{
			hex += DwordToHexString(dwAddress) + "   ";
			dwAddress += 0x10;
		}

		hex += ByteToHexString(pbBuffer[i]);
		ascii += SafeASCII(pbBuffer[i]);
		if(dwWrapSize % 4 == 0)
		{
			hex += " ";
		}

		// if end, write new line
		if(dwWrapSize == 0)
		{
			// append ASCII representation and go to the next line
			hex += " " + ascii + "\n";
			ascii = "";
			dwWrapSize = 16;
		}

		// handle last DWORD, don't want to subtract < 0
		dwWrapSize--;
	}

	// ugly hacks
#ifdef _UNICODE
	std::wstring ws;
	ws.assign(hex.begin(), hex.end());
	Logger::Instance().Log(ws, INFO); 
#else
	Logger::Instance().Log(hex, INFO); 
#endif // _UNICODE
}