#ifndef __OBJECTHIDERPRIVATEINCLUDES_H_VERSION__
#define __OBJECTHIDERPRIVATEINCLUDES_H_VERSION__ 100

#include <ntddk.h>
#include "SymbolWrapper.h"
#include "SortedList.h"

// EXHANDLE structure is not available in debugging symbols - it is needed for PspCidTable parsing
// structure is taken from ReactOS source code
typedef struct _EXHANDLE
{
	union
	{
		struct
		{
			ULONG TagBits:2;
			ULONG Index:30;
		};
		HANDLE GenericHandleOverlay;
		ULONG_PTR Value;
	};
} EXHANDLE, *PEXHANDLE;

// simple structure used during enumeration of csrss handles
// first member is the hide list, second member is the target process that needs to be hidden
typedef struct _CSRSSHTABLEENUM
{
	PSORTED_LIST pList;
	PEPROCESS pTargetProcess;
	PVOID pHandleTable;
} CSRSSHTABLEENUM, *PCSRSSHTABLEENUM;

// defines for object handle parsing (handle table entry -> object header)
#define OBJ_LOCK 0x00000001
#define OBJ_INHERIT 0x00000002
#define OBJ_AUDIT_OBJECT_CLOSE 0x00000004
#define OBJ_HANDLE_ATTRIBUTES   (OBJ_LOCK | OBJ_INHERIT | OBJ_AUDIT_OBJECT_CLOSE)

// size of HANDLE_TABLE_ENTRY structure - this structure is using two unions and it is complicated to retreive
// the size using SymbolHelper - thus fixed sizes for 32-bit and 64-bit systems will be defined here
#ifdef _WIN64
	#define HANDLE_TABLE_ENTRY_SIZE	0x10
#else // _WIN32
	#define HANDLE_TABLE_ENTRY_SIZE 0x8
#endif // _WIN64

// 32-bit and 64-bit code are currently the same, but Windows 8 apparently made some changes to handle tables
#ifdef _WIN64
#define OHP_GET_OBJ_HEADER(Object) \
	((PVOID)((ULONG_PTR)(Object) & ~OBJ_HANDLE_ATTRIBUTES))
#else // _WIN32
#define OHP_GET_OBJ_HEADER(Object) \
	((PVOID)((ULONG_PTR)(Object) & ~OBJ_HANDLE_ATTRIBUTES))
#endif // _WIN64

// defines for object header transformations
#define OB_FLAG_CREATE_INFO                     0x01
#define OB_FLAG_KERNEL_MODE                     0x02
#define OB_FLAG_CREATOR_INFO                    0x04
#define OB_FLAG_EXCLUSIVE                       0x08
#define OB_FLAG_PERMANENT                       0x10
#define OB_FLAG_SECURITY                        0x20
#define OB_FLAG_SINGLE_PROCESS                  0x40
#define OB_FLAG_DEFER_DELETE                    0x80

#define OHP_OBJ_HEADER_TO_OBJ(Obj, BodyOffset)														\
	((PVOID) SYMW_MEMBER_FROM_OFFSET(Obj, (BodyOffset), 0))											\

#define OHP_OBJ_TO_OBJ_HEADER(Obj, BodyOffset)														\
	((PVOID) SYMW_MEMBER_FROM_OFFSET(Obj, 0, (BodyOffset)))											\

// all header -> xxx_info macros are virtually the same - using multiple macros because of
// distinct and easy-to-use names
#define OHP_OBJ_HEADER_TO_NAME_INFO(Obj, NameInfoOffset)											\
	((PVOID)(! (*((PULONG) SYMW_MEMBER_FROM_OFFSET(Obj, (NameInfoOffset), 0))) ?					\
	NULL: ((PVOID) SYMW_MEMBER_FROM_OFFSET(Obj, 0, (NameInfoOffset)))

#define OHP_OBJ_HEADER_TO_HANDLE_INFO(Obj, HandleInfoOffset)										\
	((PVOID)(! (*((PULONG) SYMW_MEMBER_FROM_OFFSET(Obj, (HandleInfoOffset), 0))) ?					\
	NULL: ((PVOID) SYMW_MEMBER_FROM_OFFSET(Obj, 0, (HandleInfoOffset)))

#define OHP_OBJ_HEADER_TO_QUOTA_INFO(Obj, QuotaInfoOffset)											\
	((PVOID)(! (*((PULONG) SYMW_MEMBER_FROM_OFFSET(Obj, (QuotaInfoOffset), 0))) ?					\
	NULL: ((PVOID) SYMW_MEMBER_FROM_OFFSET(Obj, 0, (QuotaInfoOffset)))

#define OHP_OBJ_HEADER_TO_CREATOR_INFO(Obj, Flags, CreatorInfoSize)									\
	((PVOID)(! ((*((PULONG) SYMW_MEMBER_FROM_OFFSET(Obj, (Flags), 0))) & OB_FLAG_CREATOR_INFO) ?	\
	NULL: ((PVOID) SYMW_MEMBER_FROM_OFFSET(Obj, 0, (CreatorInfoSize)))

#define OHP_OBJ_HEADER_TO_EXCLUSIVE_PROC(Obj, Flags, QuotaInfoOffset)								\
	((PVOID)(! ((*((PULONG) SYMW_MEMBER_FROM_OFFSET(Obj, (Flags), 0))) & OB_FLAG_EXCLUSIVE) ?		\
	NULL: ((PVOID) SYMW_MEMBER_FROM_OFFSET(Obj, 0, (QuotaInfoOffset)))


//#define OBJECT_HEADER_TO_EXCLUSIVE_PROCESS(h)               \
//	((!((h)->Flags & OB_FLAG_EXCLUSIVE)) ?                  \
//NULL: (((POBJECT_HEADER_QUOTA_INFO)((PCHAR)(h) -    \
//	(h)->QuotaInfoOffset))->ExclusiveProcess))

// define for ObGetObjectType function - available only on Windows 7 and above
typedef POBJECT_TYPE(NTAPI *OBGETOBJECTTYPE)(IN PVOID);

// define for ObReferenceProcessHandleTable function
typedef PVOID (NTAPI *OBREFERENCEPROCESSHANDLETABLE) (IN PEPROCESS);

// define for ObReferenceProcessHandleTable function in Vista and later
// on Vista and above, PEPROCESS member is passed in EAX register
typedef PVOID (NTAPI *OBREFERENCEPROCESSHANDLETABLEVISTA) (VOID);

// define for ObDeReferenceProcessHandleTable function
typedef VOID (NTAPI *OBDEREFERENCEPROCESSHANDLETABLE) (IN PEPROCESS);

// on Vista and above, PEPROCESS member is passed in ECX register
// define for ObDeReferenceProcessHandleTable function in Vista and later
typedef VOID (FASTCALL *OBDEREFERENCEPROCESSHANDLETABLEVISTA) (IN PEPROCESS);

// NOTE: Windows 8 apparently have different function signature! Need to research...
typedef BOOLEAN (NTAPI *PEX_ENUM_HANDLE_CALLBACK)(IN OUT PVOID, IN HANDLE, IN PVOID);

// define for ExEnumHandleTable function
typedef BOOLEAN (NTAPI *EXENUMHANDLETABLE) (IN PVOID, IN PEX_ENUM_HANDLE_CALLBACK, IN OUT PVOID , OUT PHANDLE OPTIONAL);

// define for ExpLookupHandleTableEntry function (below Vista)
typedef PVOID (NTAPI *EXPLOOKUPHANDLETABLEENTRY) (IN PVOID, IN EXHANDLE);

// on Vista and above, first argument (pointer to handle table) is passed in ECX register
// second argument (since it's too big to fit in EDX) is passed through stack
// define for ExpLookupHandleTableEntry function in Vista and later
typedef PVOID (FASTCALL *EXPLOOKUPHANDLETABLEENTRYVISTA) (IN PVOID, IN EXHANDLE);

#endif // __OBJECTHIDERPRIVATEINCLUDES_H_VERSION__