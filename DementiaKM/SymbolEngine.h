#ifndef __SYMBOLENGINE_H_VERSION__
#define __SYMBOLENGINE_H_VERSION__ 100

// this module's design is driven by ProcessHacker driver - I think they made very good job, thanks guys!

#define TAG_INTERNAL_SYMBOL_ARRAY ('amyS')

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#include "../Common/CommonTypesDrv.h"
#ifdef __cplusplus
};
#endif // __cplusplus

typedef struct _SYMBOL_ENTRY
{
	LIST_ENTRY ListEntry;
	INTERNAL_SYMBOL Symbol;
} SYMBOL_ENTRY, *PSYMBOL_ENTRY;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	VOID				SymInit(void);
	VOID				SymUnInit(void);
	BOOLEAN				SymIsInitialized(void);

	// returns the INTERNAL_SYMBOL array of currently defined symbols
	// caller is responsible for clearing the memory after use!
	PINTERNAL_SYMBOL	SymGetSymbols(OUT PULONG puSymbolCount);
	ULONG				SymGetSymbolCount(void);
	BOOLEAN				SymAddSymbol(IN PCHAR pszSymbolName, IN ULONG64 uSymbolAddress, IN ULONG uOffset, IN ULONG uBitPosition, IN ULONG uBitLength);
	NTSTATUS			SymAddSymbols(IN PINTERNAL_SYMBOL pSymbolsArray, IN ULONG uArraySize);
	BOOLEAN				SymUpdateSymbol(IN PINTERNAL_SYMBOL pSymbol);
	ULONG64				SymGetAddress(IN PCHAR pszSymbolName);

	// Exported address function checks if the address obtained by SymbolHelper in the user mode is equal to the address
	// obtained by MmGetSystemRoutineAddress (this method is preferred)
	ULONG64				SymGetExportedAddress(IN PCHAR pszSymbolName);
	ULONG				SymGetOffset(IN PCHAR pszSymbolName);
	ULONG				SymGetBitPosition(IN PCHAR pszSymbolName);
	ULONG				SymGetBitLength(IN PCHAR pszSymbolName);
	BOOLEAN				SymRemoveSymbol(IN PCHAR pszSymbolName);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __SYMBOLENGINE_H_VERSION__