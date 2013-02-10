#ifndef __PROCESSHIDERPRIVATEINCLUDES_H_VERSION__
#define __PROCESSHIDERPRIVATEINCLUDES_H_VERSION__ 100

#include <ntddk.h>
#include "../Common/CommonTypesDrv.h"

#define TAG_TARGET_PROCESS_LOOKASIDE ('lrpT')
#define TAG_TARGET_PEPROCESS_ARRAY ('rraP')

#ifdef _WIN64
typedef struct _EX_PUSH_LOCK                 // 7 elements, 0x8 bytes (sizeof) 
{                                                                              
	union
	{                                                                          
		struct
		{
			UINT64	Locked : 1;         // 0 BitPosition                  
			UINT64	Waiting : 1;        // 1 BitPosition                  
			UINT64	Waking : 1;         // 2 BitPosition                  
			UINT64	MultipleShared : 1; // 3 BitPosition                  
			UINT64	Shared : 60;        // 4 BitPosition                  
		};                                                                     
		UINT64	Value;                                                    
		VOID*	Ptr;                                                      
	};                                                                         
}EX_PUSH_LOCK, *PEX_PUSH_LOCK;
#else // _WIN32
typedef struct _EX_PUSH_LOCK                 // 7 elements, 0x8 bytes (sizeof) 
{                                                                              
	union
	{                                                                          
		struct
		{
			unsigned int	Locked : 1;         // 0 BitPosition                  
			unsigned int	Waiting : 1;        // 1 BitPosition                  
			unsigned int	Waking : 1;         // 2 BitPosition                  
			unsigned int	MultipleShared : 1; // 3 BitPosition                  
			unsigned int	Shared : 28;        // 4 BitPosition                  
		};                                                                     
		unsigned int	Value;                                                    
		VOID*	Ptr;                                                      
	};                                                                         
}EX_PUSH_LOCK, *PEX_PUSH_LOCK;
#endif // _WIN64

// define for PsLookupProcessByProcessId function
typedef NTSTATUS (NTAPI *PSLOOKUPPROCESSBYPROCESSID) (IN HANDLE, OUT PEPROCESS *);

// define for PsGetNextProcessThread function
typedef PETHREAD (NTAPI *PSGETNEXTPROCESSTHREAD) (IN PEPROCESS, IN PETHREAD OPTIONAL);

// define for PsGetNextProcessThread function on Vista and above
// on Vista and above, PEPROCESS member is passed in EAX register
typedef PETHREAD (NTAPI *PSGETNEXTPROCESSTHREADVISTA) (IN PETHREAD OPTIONAL);

// define for PsReferenceProcessFilePointer
typedef NTSTATUS (NTAPI *PSREFPROCESSFILEPOINTER) (IN PEPROCESS, OUT PFILE_OBJECT *);

// define for ExAcquireRundownProtection (below Vista)
typedef BOOLEAN (NTAPI *EXACQUIRERUNDOWNPROTECTION)(IN PVOID);

// define for ExAcquireRundownProtectionEx (Vista and above)
typedef BOOLEAN (FASTCALL *EXACQUIRERUNDOWNPROTECTIONEX)(IN PVOID);

// define for ExReleaseRundownProtection (below Vista)
typedef VOID (NTAPI *EXRELEASERUNDOWNPROTECTION)(IN PVOID);

// define for ExReleaseRundownProtectionEx (Vista and above)
typedef VOID (FASTCALL *EXRELEASERUNDOWNPROTECTIONEX)(IN PVOID);

typedef struct _PROC_HIDE
{
	LIST_ENTRY	ListEntry;
	PVOID		pEPROCESS;
	TARGET_OBJECT targetObject;
} PROC_HIDE, *PPROC_HIDE;

// methods exported by the kernel
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	// used for process locking and process list protection
	__declspec(dllimport) VOID FASTCALL ExfAcquirePushLockShared(PEX_PUSH_LOCK PushLock);
	__declspec(dllimport) VOID FASTCALL ExfReleasePushLock(PEX_PUSH_LOCK PushLock);
	

#ifdef __cplusplus
};	// extern "C"
#endif // __cplusplus


#endif // __PROCESSHIDERPRIVATEINCLUDES_H_VERSION__