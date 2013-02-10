#ifndef __CRASHDUMPDETECTORPRIVATEINCLUDES_H_VERSION__
#define __CRASHDUMPDETECTORPRIVATEINCLUDES_H_VERSION__ 100

#define TAG_ADDRESS_FIX_LOOKASIDE ('LxfA')

typedef struct _RANGE_TRANSLATION {
	ULONG_PTR uFileOffset;
	ULONG_PTR uRealStartAddress;
	ULONG_PTR uRangeSize;
} RANGE_TRANSLATION, *PRANGE_TRANSLATION;

typedef struct _ADDRESS_FIX_ENTRY
{
	LIST_ENTRY ListEntry;
	HANDLE hDumpFile;
	ULONG uNumberOfRanges;
	RANGE_TRANSLATION Ranges[100];								// using just 100 ranges - this proved to be enough for any tested machine
} ADDRESS_FIX_ENTRY, *PADDRESS_FIX_ENTRY;

// thanks to Matthieu Suiche, Volatility and Singularity!
#define DMP_HEADER_COMMENT_SIZE             (128)

#ifdef _WIN64
	#define DMP_PHYSICAL_MEMORY_BLOCK_SIZE   (700)
	#define DMP_CONTEXT_RECORD_SIZE          (3000)
	#define DMP_RESERVED_0_SIZE              (4016)

	typedef struct _PHYSICAL_MEMORY_RUN {
		ULONG64 BasePage;
		ULONG64 PageCount;
	} PHYSICAL_MEMORY_RUN, *PPHYSICAL_MEMORY_RUN;

	typedef struct _PHYSICAL_MEMORY_DESCRIPTOR {
		ULONG NumberOfRuns;
		ULONG64 NumberOfPages;
		PHYSICAL_MEMORY_RUN Run[1];
	} PHYSICAL_MEMORY_DESCRIPTOR, *PPHYSICAL_MEMORY_DESCRIPTOR;

	typedef struct _DUMP_HEADER {
		ULONG Signature;
		ULONG ValidDump;
		ULONG MajorVersion;
		ULONG MinorVersion;
		ULONG64 DirectoryTableBase;
		ULONG64 PfnDataBase;
		ULONG64 PsLoadedModuleList;
		ULONG64 PsActiveProcessHead;
		ULONG MachineImageType;
		ULONG NumberProcessors;
		ULONG BugCheckCode;
		ULONG64 BugCheckParameter1;
		ULONG64 BugCheckParameter2;
		ULONG64 BugCheckParameter3;
		ULONG64 BugCheckParameter4;
		CHAR VersionUser[32];
		ULONG64 KdDebuggerDataBlock;

		union {
			PHYSICAL_MEMORY_DESCRIPTOR PhysicalMemoryBlock;
			UCHAR PhysicalMemoryBlockBuffer [DMP_PHYSICAL_MEMORY_BLOCK_SIZE];
		};
		UCHAR ContextRecord [DMP_CONTEXT_RECORD_SIZE];
		EXCEPTION_RECORD64 Exception;
		ULONG DumpType;
		LARGE_INTEGER RequiredDumpSpace;
		LARGE_INTEGER SystemTime;
		CHAR Comment [DMP_HEADER_COMMENT_SIZE];   // May not be present.
		LARGE_INTEGER SystemUpTime;
		ULONG MiniDumpFields;
		ULONG SecondaryDataState;
		ULONG ProductType;
		ULONG SuiteMask;
		ULONG WriterStatus;
		UCHAR Unused1;
		UCHAR KdSecondaryVersion;       // Present only for W2K3 SP1 and better
		UCHAR Unused[2];
		UCHAR _reserved0[DMP_RESERVED_0_SIZE];
	} DUMP_HEADER, *PDUMP_HEADER;
#else // _WIN32
	#define DMP_PHYSICAL_MEMORY_BLOCK_SIZE   (700)
	#define DMP_CONTEXT_RECORD_SIZE          (1200)
	#define DMP_RESERVED_0_SIZE              (1768)
	#define DMP_RESERVED_2_SIZE              (16)
	#define DMP_RESERVED_3_SIZE              (56)

	typedef struct _PHYSICAL_MEMORY_RUN {
		ULONG BasePage;
		ULONG PageCount;
	} PHYSICAL_MEMORY_RUN, *PPHYSICAL_MEMORY_RUN;

	typedef struct _PHYSICAL_MEMORY_DESCRIPTOR {
		ULONG NumberOfRuns;
		ULONG NumberOfPages;
		PHYSICAL_MEMORY_RUN Run[1];
	} PHYSICAL_MEMORY_DESCRIPTOR, *PPHYSICAL_MEMORY_DESCRIPTOR;

	typedef struct _DUMP_HEADER {
		/* 0x000 */ ULONG Signature;
		/* 0x004 */ ULONG ValidDump;
		/* 0x008 */ ULONG MajorVersion;
		/* 0x00C */ ULONG MinorVersion;
		/* 0x010 */ ULONG DirectoryTableBase;
		/* 0x014 */ ULONG PfnDataBase;
		/* 0x018 */ PLIST_ENTRY PsLoadedModuleList;
		/* 0x01C */ PLIST_ENTRY PsActiveProcessHead;
		/* 0x020 */ ULONG MachineImageType;
		/* 0x024 */ ULONG NumberOfProcessors;
		/* 0x028 */ ULONG BugCheckCode;
		/* 0x02C */ ULONG BugCheckParameter1;
		/* 0x030 */ ULONG BugCheckParameter2;
		/* 0x034 */ ULONG BugCheckParameter3;
		/* 0x038 */ ULONG BugCheckParameter4;
		/* 0x03C */ UCHAR VersionUser[32];
		/* 0x05C */ UCHAR PaeEnabled;
		/* 0x05d */ UCHAR KdSecondaryVersion;
		/* 0x05e */ UCHAR Spare3[2];
		/* 0x060 */ PVOID KdDebuggerDataBlock;
		union {
			/* 0x064 */ PHYSICAL_MEMORY_DESCRIPTOR PhysicalMemoryBlock;
			/* 0x064 */ UCHAR PhysicalMemoryBlockBuffer[DMP_PHYSICAL_MEMORY_BLOCK_SIZE];
		};
		union {
			/* 0x320 */ CONTEXT Context;
			/* 0x320 */ UCHAR ContextRecord[DMP_CONTEXT_RECORD_SIZE];
		};
		/* 0x7d0 */ EXCEPTION_RECORD32 Exception;
		/* 0x820 */ UCHAR Comment[DMP_HEADER_COMMENT_SIZE];
		/* 0x8a0 */ UCHAR _reserved0[DMP_RESERVED_0_SIZE];
		/* 0xf88 */ ULONG DumpType;
		/* 0xf8c */ ULONG MiniDumpFields;
		/* 0xf90 */ ULONG SecondaryDataState;
		/* 0xf94 */ ULONG ProductType;
		/* 0xf98 */ ULONG SuiteMask;
		/* 0xf9c */ ULONG WriterStatus;
		/* 0xfa0 */ LARGE_INTEGER RequiredDumpSpace;
		/* 0xfa8 */ UCHAR _reserved2[DMP_RESERVED_2_SIZE];
		/* 0xfb8 */ LARGE_INTEGER SystemUpTime;
		/* 0xfc0 */ LARGE_INTEGER SystemTime;
		/* 0xfc8 */ UCHAR _reserved3[DMP_RESERVED_3_SIZE];
	} DUMP_HEADER, *PDUMP_HEADER;
#endif // _WIN64

#endif // __CRASHDUMPDETECTORPRIVATEINCLUDES_H_VERSION__