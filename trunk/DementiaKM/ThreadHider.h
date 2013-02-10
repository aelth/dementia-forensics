#ifndef __THREADHIDER_H_VERSION__
#define __THREADHIDER_H_VERSION__ 100

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

	NTSTATUS ThInit(VOID);
	BOOLEAN ThInitSymbols(VOID);
	VOID ThUnInit(VOID);

	BOOLEAN ThFindHideAddreses(IN OUT PSORTED_LIST pList, const IN PETHREAD pTargetThread, const IN BOOLEAN bDeleteThreadHandle);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus

#endif // __THREADHIDER_H_VERSION__