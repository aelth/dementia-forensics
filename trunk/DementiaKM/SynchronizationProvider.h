#ifndef __SYNCHRONIZATIONPROVIDER_H_VERSION__
#define __SYNCHRONIZATIONPROVIDER_H_VERSION__ 100

#ifdef __cplusplus
extern "C"{
#endif // __cplusplus
#include <ntddk.h>
#ifdef __cplusplus
};
#endif // __cplusplus

/* 
This file is mostly taken from KProcessHacker - kernel mode part of the Process Hacker made by wj32.

Some ideas are also taken from Rootkits: Subverting the Windows kernel - there is actually a bug in
the book code example:) GainExclusivity method will actually always return NULL, because there is
high probability that the code is running on PASSIVE_LEVEL, and check KeGetCurrentIrql() != DISPATCH_LEVEL
will fail.
*/

typedef struct _CPU_LOCK
{
	FAST_MUTEX	LockMutex;
	PKDPC		pDPCs;
	LONG		lNumberOfRaisedCPUs;
	LONG		lReleaseSignal;
	KIRQL		oldIrql;
	BOOLEAN		bIsAcquired;
} CPU_LOCK, *PCPU_LOCK;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	VOID		SyncInitializeCPULock(OUT PCPU_LOCK	pCPULock);
	BOOLEAN		SyncAcquireCPULock(__inout PCPU_LOCK pCPULock);
	VOID		SyncReleaseCPULock(__inout PCPU_LOCK pCPULock);

#ifdef __cplusplus
}; // extern "C"
#endif // __cplusplus


#endif // __SYNCHRONIZATIONPROVIDER_H_VERSION__