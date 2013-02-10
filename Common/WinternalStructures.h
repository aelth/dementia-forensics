#pragma once

#define STATUS_SUCCESS				0x00000000
#define STATUS_INFO_LENGTH_MISMATCH 0xC0000004
#define STATUS_BUFFER_OVERFLOW		0x80000005

/**	@enum _SYSTEM_INFORMATION_CLASS
@brief System information classes

Mostly undocumented enumeration which specifies the identifiers for the
system information classes used in many @c NtQuery* and @c NtSet* functions.\n

We need only @c SystemModuleInformation here, but some other @e semi-documented
classes are listed here aswell.\n
This enum is taken directly from MSDN documentation and, as such, should be considered
to be fairly stable and consistent.\n
Much more information about system information classes can be found in <em>Gary Nebbett's
Native API reference</em>.

*/
typedef enum _SYSTEM_INFORMATION_CLASS {
	SystemBasicInformation = 0,						/**< Specifies number of active processors, and some basic memory information*/
	SystemPerformanceInformation = 2,				/**< Contains many performance counters and detailed memory usage*/
	SystemTimeOfDayInformation = 3,					/**< Contains boot time, current time and time zone information*/
	SystemProcessInformation = 5,					/**< Detailed information about processes and their threads*/
	SystemProcessorPerformanceInformation = 8,		/**< Per-processor timing information*/
	SystemModuleInformation = 11,					/**< Contains information about all modules in the system, including kernel image and dll's*/
	SystemHandleInformation = 16,					/**< Information about all handles on the system*/
	SystemInterruptInformation = 23,				/**< Contains information about context-switching counts and number of DPC issued*/
	SystemExceptionInformation = 33,				/**< Number of total exceptions handled*/
	SystemRegistryQuotaInformation = 37,			/**< Information about registry quotas*/
	SystemLookasideInformation = 45					/**< Detailed information about lookaside lists, usable probably only by the kernel mode code*/
} SYSTEM_INFORMATION_CLASS;


/* @enum _OBJECT_INFORMATION_CLASS
@brief Object information classes

This enum is used for NtQueryObjectInformation - it is used to retreive all handles open by the process.
*/
typedef enum _OBJECT_INFORMATION_CLASS{
	ObjectBasicInformation,
	ObjectNameInformation,
	ObjectTypeInformation,
	ObjectAllTypesInformation,
	ObjectHandleInformation
} OBJECT_INFORMATION_CLASS;

/**	@struct _SYSTEM_MODULE_INFORMATION
@brief System module information class

Struct contains all relevant information for the loaded module.
*/

typedef struct _SYSTEM_MODULE_INFORMATION
{
	ULONG	Reserved[2];								/**< Unknown bytes*/
#ifdef _WIN64
	ULONG	Reserved3;
	ULONG	Reserved4;
#endif // _WIN64
	PVOID	Base;									/**< The base address of the module*/
	ULONG  	Size;									/**< The size of module in bytes*/
	ULONG  	Flags;									/**< A bit-array describing various states of the module*/
	USHORT 	Index;									/**< Index of this particular module in the array of all modules in the system*/
	USHORT 	Unknown;									/**< Usage unknown, probably some kind of ID*/
	USHORT 	LoadCount;								/**< Number of references (open handles) to the module*/
	USHORT 	ModuleNameOffset;						/**< The offset to the final filename component of the image*/
	CHAR   	ImageName[256];							/**< Char array containing name of the module*/
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

/**	@struct _SYSTEM_MODULE_INFORMATION_EX
@brief	Wrapper around @ref _SYSTEM_MODULE_INFORMATION structure

When @c NtQuerySystemInformation is called with @c SystemModuleInformation class specified,
it returns information about modules in this format exactly.
*/
typedef struct _SYSTEM_MODULE_INFORMATION_EX
{
	ULONG						ModulesCount;								/**< Total number of modules in the system*/
	SYSTEM_MODULE_INFORMATION	Modules[1];
} SYSTEM_MODULE_INFORMATION_EX, *PSYSTEM_MODULE_INFORMATION_EX;

typedef struct _SYSTEM_HANDLE {
	ULONG		uIdProcess;
	UCHAR		ObjectType;
	UCHAR		Flags;
	USHORT		Handle;
	PVOID		pObject;
	ACCESS_MASK	GrantedAccess;
} SYSTEM_HANDLE, *PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION {
	ULONG			uCount;
	SYSTEM_HANDLE	Handles[1];
} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR  Buffer;
} UNICODE_STRING;
typedef UNICODE_STRING *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;

typedef UNICODE_STRING OBJECT_NAME_INFORMATION;
typedef UNICODE_STRING *POBJECT_NAME_INFORMATION;