#include "SymbolWrapper.h"
#include "SymbolEngine.h"

BOOLEAN SymWAddSymbol(IN PCHAR pszSymbolName, IN ULONG64 uSymbolAddress, IN ULONG uOffset, IN ULONG uBitPosition, IN ULONG uBitLength)
{
	return SymAddSymbol(pszSymbolName, uSymbolAddress, uOffset, uBitPosition, uBitLength);
}

BOOLEAN SymWInitializeAddress(IN OUT PVOID *ppOutAddress, const IN PCHAR szSymbolName, IN BOOLEAN bIsExportedAddress)
{
	ASSERTMSG("Pointer to symbol name cannot be NULL!", szSymbolName != NULL);

	// get symbol offsets
	ULONG64 uAddress64 = -1;
	if(*ppOutAddress == NULL)
	{
		if(bIsExportedAddress)
		{
			PVOID pAddress = (PVOID) SymGetExportedAddress(szSymbolName);
			if(pAddress == NULL)
			{
				KdPrint(("[DEBUG] ERROR - Address of symbol %s cannot be initialized with -1\n", szSymbolName));
				return FALSE;
			}

			*ppOutAddress = pAddress;
		}
		else
		{
			uAddress64 = SymGetAddress(szSymbolName);
			if(uAddress64 == -1)
			{
				KdPrint(("[DEBUG] ERROR - Address of symbol %s cannot be initialized with -1\n", szSymbolName));
				return FALSE;
			}

			*ppOutAddress = (PVOID) uAddress64;
		}
	}

	return TRUE;
}

BOOLEAN SymWInitializeOffset(IN OUT PULONG puOutOffset, const IN PCHAR szSymbolName)
{
	ASSERTMSG("Pointer to offset variable cannot be NULL!", puOutOffset != NULL);
	ASSERTMSG("Pointer to symbol name cannot be NULL!", szSymbolName != NULL);

	// get symbol offsets
	ULONG uOffset = -1;
	if(*puOutOffset == -1)
	{
		uOffset = SymGetOffset(szSymbolName);
		if(uOffset == -1)
		{
			KdPrint(("[DEBUG] ERROR - %s member cannot be initialized with -1 offset\n", szSymbolName));
			return FALSE;
		}

		*puOutOffset = uOffset;
	}

	return TRUE;
}

BOOLEAN SymWInitializeBitPosAndLength(IN OUT PULONG puOutBitPosition, IN OUT PULONG puOutBitLength, const IN PCHAR szSymbolName)
{
	ASSERTMSG("Pointer to bit position variable cannot be NULL!", puOutBitPosition != NULL);
	ASSERTMSG("Pointer to bit length variable cannot be NULL!", puOutBitLength != NULL);
	ASSERTMSG("Pointer to symbol name cannot be NULL!", szSymbolName != NULL);

	// get symbol bit position and length
	ULONG uBitPosition = -1;
	ULONG uBitLength = -1;
	if(*puOutBitPosition == -1 && *puOutBitLength == -1)
	{
		uBitPosition = SymGetBitPosition(szSymbolName);
		uBitLength = SymGetBitLength(szSymbolName);
		if(uBitPosition == -1 || uBitLength == -1)
		{
			KdPrint(("[DEBUG] ERROR - Bit position/length of member %s cannot have value -1 \n", szSymbolName));
			return FALSE;
		}

		*puOutBitPosition = uBitPosition;
		*puOutBitLength = uBitLength;
	}

	return TRUE;
}