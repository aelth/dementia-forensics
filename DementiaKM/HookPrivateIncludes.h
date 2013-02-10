#ifndef __HOOKPRIVATEINCLUDES_H_VERSION__
#define __HOOKPRIVATEINCLUDES_H_VERSION__ 100

#define METHOD_ENTRY_DETOUR_LENGTH 20
#define TAG_SAVED_BYTES ('aBS')
#define JMP	0xe9

typedef struct _HOOK
{
	PVOID		pOriginalFunction;		/**< Address of the original function, function which is hooked*/
	PVOID		pNewFunction;			/**< Address of the user supplied detour function*/
	PUCHAR		pSavedBytes;			/**< Saved bytes which were overwritten by the hook*/
	ULONG		uSavedBytesLength;		/**< Length of the saved bytes*/
} HOOK, *PHOOK;

typedef struct _HOOK_ENTRY
{
	LIST_ENTRY	ListEntry;				/**< HOOK_ENTRY structures will be linked in a doubly linked list, so we need LIST_ENTRY member*/
	HOOK		Hook;					/**< Embedded HOOK structure containing addresses of the original function, hook function and the saved bytes*/
} HOOK_ENTRY, *PHOOK_ENTRY;

typedef struct _MODULE_ENTRY {
	LIST_ENTRY		ModuleListEntry;
	LIST_ENTRY 		InMemoryOrderModuleList;
	LIST_ENTRY 		InInitializationOrderModuleList;
	PVOID  			ModuleBaseAddress;
	PVOID  			ModuleEntryAddress;
	ULONG_PTR		ModuleSize;
	UNICODE_STRING 	ModulePath;
	UNICODE_STRING 	ModuleName;
} MODULE_ENTRY, *PMODULE_ENTRY;

#endif // __HOOKPRIVATEINCLUDES_H_VERSION__