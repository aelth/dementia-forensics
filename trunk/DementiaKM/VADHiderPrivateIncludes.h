#ifndef __VADHIDERPRIVATEINCLUDES_H_VERSION__
#define __VADHIDERPRIVATEINCLUDES_H_VERSION__ 100

#include <ntddk.h>

// KAPC_STATE structure, according to the WinDBG - it looks the same on all OS versions
typedef struct _KAPC_STATE
{
	LIST_ENTRY ApcListHead[2];
	PEPROCESS Process;
	UCHAR KernelApcInProgress;
	UCHAR KernelApcPending;
	UCHAR UserApcPending;
} KAPC_STATE, *PKAPC_STATE;

// defines for KeStackAttachProcess/KeUnstackDetachProcess functions
typedef VOID (NTAPI *KESTACKATTACHPROCESS) (IN PEPROCESS, OUT PKAPC_STATE);
typedef VOID (NTAPI *KEUNSTACKDETACHPROCESS) (IN PKAPC_STATE);

#endif // __VADHIDERPRIVATEINCLUDES_H_VERSION__

