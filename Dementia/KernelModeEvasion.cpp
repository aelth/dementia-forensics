#include "StdAfx.h"
#include "KernelModeEvasion.h"
#include "Logger.h"
#include "ProcessTargetObject.h"
#include "DriverTargetObject.h"
#include "DriverCommunicator.h"
#include "DriverMinifilterCommunicator.h"

namespace po = boost::program_options;
static boost::scoped_ptr<IDriverCommunicator> m_driverCommunicator;

KernelModeEvasion::KernelModeEvasion(int ID)
{
	// set general information variables for this method
	m_ID = ID;
	m_driverName = _T("DementiaKM");

	// driver is located in the current dir by default
	m_driverPath = _T("DementiaKM.sys");

	// by default, driver is unloaded after hiding
	m_unloadDriver = true;

	m_Name = _T("Generic kernel-mode evasion module");
	m_shortDesc = _T("Hides kernel level objects related to target process using kernel driver");
	m_longDesc = _T("This evasion method uses kernel driver in order to hide valuable artifacts ") 
		_T("and evade analysis. The method works by hooking NtWriteFile() function inside the kernel and ")
		_T("modify obtained buffers (i.e. conceal presence of arbitrary objects)");
	m_argDesc = _T("\t-i|--info - this information\n\t\t")
		_T("-P|--process-name - name of the process whose artifacts (EPROCESS block etc.) should be hidden\n\t\t")
		_T("-p|--process-id - ID of the process whose artifacts (EPROCESS block etc.) should be hidden\n\t\t")
		_T("-D|--driver-name - name of the driver that should be hidden, along with the additional artifacts\n\t\t")
		_T("--no-unload - by default, driver will be unloaded after hiding. This flag indicates that the driver will not be unloaded and will remain active after this program exits.\n\t\t")
		_T("--no-threads-hide - by default, all threads of the target process are hidden. This flag indicates no thread hiding\n\t\t")
		_T("--no-handles-hide - by default, all handles/objects uniquely opened within the target process are hidden. This flag indicates no handle hiding\n\t\t")
		_T("--no-image-hide - by default, file object representing the process image is hidden. This flag indicates no image file hiding\n\t\t")
		_T("--no-vad-hide - by default, private memory ranges of the target process are deleted. This flag indicates no hiding of process private memory ranges\n\t\t")
		_T("--no-job-hide - by default, if target process belongs to a job, it is removed from the job. Job is removed if no processes are left. This flag indicates no job hiding\n\t\t")
		_T("-d|--driver - name of the kernel-mode driver (default: DementiaKM.sys)\n\t\t");
	m_example = _T("TBD");

	// initialize options_description, for command line arguments parsing
	m_options.add_options()
		("info,i", "detailed method/module description")
		("process-name,P", po::value<std::vector<std::string>>(), "name of the process whose artifacts (EPROCESS block etc.) should be hidden")
		("process-id,p", po::value<std::vector<DWORD>>(), "ID of the process whose artifacts (EPROCESS block etc.) should be hidden")
		("driver-name,D", po::value<std::vector<std::string>>(), "name of the driver that should be hidden, along with the additional artifacts")
		("no-unload", "by default, driver will be unloaded after hiding. This flag indicates that the driver will not be unloaded and will remain active after this program exits.\n\t\t")
		("no-threads-hide", "by default, all threads of the target process are hidden - this flag indicates no thread hiding")
		("no-handles-hide", "by default, all handles/objects uniquely opened within the target process are hidden - this flag indicates no handle hiding")
		("no-image-hide", "by default, file object representing the process image is hidden. This flag indicates no image file hiding")
		("no-vad-hide", "by default, private memory ranges of the target process are deleted. This flag indicates no hiding of process private memory ranges")
		("no-job-hide", "by default, target process is removed from a job it belongs to. Job is removed if no processes are left. This flag indicates no job hiding")
		("driver,d", po::value<std::string>(), "name of the kernel-mode driver (default: \"DementiaKM.sys\")")
		;

	m_driverCommunicator.reset(NULL);

	// register control handler which will remove the driver in case CTRL combination is pressed on the keyboard
	//SetConsoleCtrlHandler((PHANDLER_ROUTINE) CtrlHandler, TRUE); 
}

KernelModeEvasion::~KernelModeEvasion(void)
{
	if(m_driverCommunicator.get() != NULL && m_unloadDriver == TRUE)
	{
		m_driverCommunicator->RemoveDriver();
	}
}

// TODO: VERY UGLY FUNCTION -- requires quite a bit of modifications and fine-tuning!
bool KernelModeEvasion::ParseArguments(std::string args)
{
	try
	{
		// use the same splitting code as in other plugins
		std::vector<std::string> tokens;
		std::istringstream argsStream(args);
		copy(std::istream_iterator<std::string>(argsStream), std::istream_iterator<std::string>(), std::back_inserter<std::vector<std::string>>(tokens));

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

		if(vm.count("info") || (!vm.count("process-name") && !vm.count("process-id") && !vm.count("driver-name"))) {
			TCharString moduleInfo = _T("\nModule: ") + m_Name + _T("\n\nArguments: ") + m_argDesc + _T("\n\nDescription: ") + m_longDesc + _T("\n\nExample: ") + m_example;
			Logger::Instance().Log(moduleInfo, INFO);
			return false;
		}

		std::string driverPath;

		// assign driver path
		if(vm.count("driver"))
		{
			driverPath = vm["driver"].as<std::string>();
		}

#ifdef _UNICODE
		if(!driverPath.empty())
		{
			m_driverPath.assign(driverPath.begin(), driverPath.end());
		}
#else
		if(!driverPath.empty())
		{
			m_driverPath = driverPath;
		}
#endif // _UNICODE

		if(vm.count("no-unload"))
		{
			m_unloadDriver = false;
		}

		bool bHideThreads = true;
		if(vm.count("no-threads-hide"))
		{
			bHideThreads = false;
		}

		bool bHideHandles = true;
		if(vm.count("no-handles-hide"))
		{
			bHideHandles = false;
		}

		bool bHideImgFileObj = true;
		if(vm.count("no-image-hide"))
		{
			bHideImgFileObj = false;
		}

		bool bHideJob = true;
		if(vm.count("no-job-hide"))
		{
			bHideJob = false;
		}

		bool bHideVad = true;
		if(vm.count("no-vad-hide"))
		{
			bHideVad = false;
		}

		// add all processes
		if(vm.count("process-name"))
		{
			// first try to add the processes by name
			std::vector<std::string> strTargetProcessNames = vm["process-name"].as<std::vector<std::string>>();
			std::vector<std::string>::const_iterator targetProcNamesIter;

			for(targetProcNamesIter = strTargetProcessNames.begin(); targetProcNamesIter != strTargetProcessNames.end(); ++targetProcNamesIter)
			{
				std::string procName = *targetProcNamesIter;
				TCharString targetProcessName;

#ifdef _UNICODE
				targetProcessName.assign(procName.begin(), procName.end());
#else
				targetProcessName = procName;
#endif // _UNICODE
				
				m_targetObjects.push_back(TargetObjectPtr(new ProcessTargetObject(targetProcessName, -1, true, bHideThreads, 
															bHideHandles, bHideImgFileObj, bHideJob, bHideVad)));
			}
		}

		if(vm.count("process-id"))
		{
			// now try to add them via PID
			std::vector<DWORD_PTR> targetProcessPIDs = vm["process-id"].as<std::vector<DWORD_PTR>>();
			std::vector<DWORD_PTR>::const_iterator targetProcPIDsIter;

			for(targetProcPIDsIter = targetProcessPIDs.begin(); targetProcPIDsIter != targetProcessPIDs.end(); ++targetProcPIDsIter)
			{
				DWORD_PTR dwPID = *targetProcPIDsIter;

				m_targetObjects.push_back(TargetObjectPtr(new ProcessTargetObject(_T(""), dwPID, true, bHideThreads, 
															bHideHandles, bHideImgFileObj, bHideJob, bHideVad)));
			}
		}

		// add all drivers
		if(vm.count("driver-name"))
		{
			std::vector<std::string> strTargetDriverNames = vm["driver-name"].as<std::vector<std::string>>();
			std::vector<std::string>::const_iterator targetDriverNamesIter;

			for(targetDriverNamesIter = strTargetDriverNames.begin(); targetDriverNamesIter != strTargetDriverNames.end(); ++targetDriverNamesIter)
			{
				std::string driverName = *targetDriverNamesIter;
				TCharString targetDriverName;

#ifdef _UNICODE
				targetDriverName.assign(driverName.begin(), driverName.end());
#else
				targetDriverName = driverName;
#endif // _UNICODE

				m_targetObjects.push_back(TargetObjectPtr(new DriverTargetObject(targetDriverName)));
			}
		}

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

bool KernelModeEvasion::Execute(void)
{	
	// UGLY UGLY UGLY
	if(m_ID == 2)
	{
		m_driverCommunicator.reset(new DriverCommunicator(m_driverName, m_driverPath, m_driverName, m_unloadDriver));
	}
	else
	{
		m_driverCommunicator.reset(new DriverMinifilterCommunicator());
	}
	
	if(!m_driverCommunicator->InstallDriver())
	{
		Logger::Instance().Log(_T("Driver installation failed"), CRITICAL_ERROR);
		return false;
	}

	if(!m_driverCommunicator->InitializeDriverSymbols())
	{
		Logger::Instance().Log(_T("Driver symbol initialization failure"), CRITICAL_ERROR);
		return false;
	}

	m_driverCommunicator->StartHiding(m_targetObjects);
	return true;
}