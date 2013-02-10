#ifndef __SEGMENTHIDER_H_VERSION__
#define __SEGMENTHIDER_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#include "SortedList.h"
#ifdef __cplusplus
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	NTSTATUS ShInit(VOID);
	BOOLEAN ShInitSymbols(VOID);
	VOID ShUnInit(VOID);

	BOOLEAN ShFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PVOID pSegment);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __SEGMENTHIDER_H_VERSION__