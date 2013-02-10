#ifndef __SYMBOLWRAPPER_H_VERSION__
#define __SYMBOLWRAPPER_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define SYMW_MEMBER_FROM_OFFSET(Base, Increment, Decrement)       \
	((((ULONG_PTR)(Base)) - (Decrement)) + (Increment))

	/*
		WARNING --	these functions DO NOT check whether SymbolEngine is initialized.
					Internal SymbolEngine functions do have such asserts, but it is up to the client of this
					functions to check whether SymbolEngine is working
	*/
	BOOLEAN	SymWAddSymbol(IN PCHAR pszSymbolName, IN ULONG64 uSymbolAddress, IN ULONG uOffset, IN ULONG uBitPosition, IN ULONG uBitLength);
	BOOLEAN SymWInitializeAddress(IN OUT PVOID *ppOutAddress, const IN PCHAR szSymbolName, IN BOOLEAN bIsExportedAddress);
	BOOLEAN SymWInitializeOffset(IN OUT PULONG puOutOffset, const IN PCHAR szSymbolName);
	BOOLEAN SymWInitializeBitPosAndLength(IN OUT PULONG puOutBitPosition, IN OUT PULONG puOutBitLength, const IN PCHAR szSymbolName);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __SYMBOLWRAPPER_H_VERSION__