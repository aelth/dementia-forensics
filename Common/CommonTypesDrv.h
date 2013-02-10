#ifndef __COMMONTYPESDRV_H_VERSION__
#define __COMMONTYPESDRV_H_VERSION__ 100

#if _MSC_VER > 1000 
#pragma once  
#endif // _MSC_VER > 1000

/** Maximum length of the symbol.

In DbgHelp.h this constant equals 2000. However, most of the symbols are well beneath the 64 - character limit.\n
For example, on most kernel images the longest symbol is 84 bytes long:
CmpRegistryMachineSystemCurrentControlSetControlSessionManagerMemoryManagementString:)
*/
#define MAX_SYM_NAME	128

/**	@struct _INTERNAL_SYMBOL
	@brief Symbol entity

Structure which defines a symbol entity.\n
Array of these structures is filled with appropriate information within DLL and sent to driver.
*/
typedef struct _INTERNAL_SYMBOL
{
	char	name[MAX_SYM_NAME];		/**< Name of the symbol*/
	ULONG64 u64address;				/**< 64-bit virtual address of the symbol*/
	ULONG	uOffset;				/**< If symbol is not a function, this represents the offset inside the parent*/
	ULONG	uBitPosition;			/**< If symbol is a bit field, this member represents bit position inside the bit field*/
	ULONG	uBitLength;				/**< If symbol is a bit field, this member represents the length of the bit field*/
} INTERNAL_SYMBOL, *PINTERNAL_SYMBOL;

/** Maximum length of object name (for example, process name) which will be hidden.
*/
#define MAX_OBJECT_NAME	256

typedef enum _TARGET_OBJECT_TYPE
{
	ABSTRACT,
	PROCESS,
	DRIVER,
	FILEOBJ,
	DLL,
} TARGET_OBJECT_TYPE;

typedef struct _TARGET_OBJECT
{
	TARGET_OBJECT_TYPE	type;
	char				szObjectName[MAX_OBJECT_NAME];
	ULONG_PTR			uPID;
	BOOLEAN				bHideAllocs;
	BOOLEAN				bHideThreads;
	BOOLEAN				bHideHandles;
	BOOLEAN				bHideImageFileObj;
	BOOLEAN				bHideJob;
	BOOLEAN				bHideVad;
} TARGET_OBJECT, *PTARGET_OBJECT;


#endif // __COMMONTYPESDRV_H_VERSION__