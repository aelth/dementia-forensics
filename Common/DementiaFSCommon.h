#ifndef __DEMENTIAFSCOMMON_H_VERSION__
#define __DEMENTIAFSCOMMON_H_VERSION__ 100

#if _MSC_VER > 1000 
#pragma once  
#endif // _MSC_VER > 1000

// name of the port
#define PORT_NAME			L"\\DementiaPort"

// list of commands that are valid for "controlling" the FS module
// the commands specified here are actually one-to-one mapping to IOCTLs in normal driver
typedef enum _DEMENTIAFS_COMMAND {
	DmfsGetSymbols,
	DmfsSaveSymbols,
	DmfsStartHiding
} DEMENTIAFS_COMMAND;

// these structure represents the format of the messages transferred between user mode and the kernel mode
#pragma warning(push)
#pragma warning(disable:4200) // disable warnings for structures with zero length arrays.
typedef struct _COMMAND_MESSAGE {
	DEMENTIAFS_COMMAND Command;
	UCHAR pData[];
} COMMAND_MESSAGE, *PCOMMAND_MESSAGE;
#pragma warning(pop)

#endif // __DEMENTIAFSCOMMON_H_VERSION__