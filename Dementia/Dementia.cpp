// Dementia.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Logger.h"
#include "EvasionMethodFactory.h"

// create namespace alias for boost program option parser
namespace po = boost::program_options;

void WriteProgramHeader()
{
	Logger::Instance().Log(_T("Dementia - v1.0 -- Windows memory anti-forensics suite"), SUCCESS);
	Logger::Instance().Log(_T("Copyright (c) 2012, Luka Milkovic <milkovic.luka@gmail.com> or <luka.milkovic@infigo.hr>"), SUCCESS);
}

po::options_description DefineArguments(int argc, _TCHAR *argv[], std::list<EvasionMethodFactory::EVASION_METHOD_BASIC_INFO> &methodsList)
{
	// create a big string which will contain information for all evasion methods
	TCharString methodsInfoTemp;
	methodsInfoTemp.reserve(4096);

	methodsInfoTemp = _T("Evasion method. Following methods are supported (specify wanted method by number):\n\n");

	std::list<EvasionMethodFactory::EVASION_METHOD_BASIC_INFO>::iterator iter;
	for(iter = methodsList.begin(); iter != methodsList.end(); ++iter)
	{
		methodsInfoTemp += boost::lexical_cast<TCharString>((*iter).ID) + _T(" - ") + (*iter).name + _T(": \t") + (*iter).shortDescription + _T("\n");
	}

	std::string methodsInfo;

	// ugly hacks
#ifdef _UNICODE
	// explicitly create std::string, and convert our wstring to this
	// don't use TCharString because of the used boost library
	methodsInfo.assign(methodsInfoTemp.begin(), methodsInfoTemp.end());
#else
	methodsInfo = methodsInfoTemp;
#endif // _UNICODE

	po::options_description desc("General options");
	desc.add_options()
		("help,h", "view program description")
		("debug,d", "print verbose/detailed program output - useful for debugging")
		("file,f", "write all program output to \"log.txt\" file")
		("method,m", po::value<int>(), methodsInfo.c_str())
		("args,a", po::value<std::string>(), "pass specific arguments to the evasion method (use info for method specific help) -- arguments are passed in quotes!")
		;

	return desc;
}

int ParseArguments(int argc, _TCHAR * argv[], std::list<EvasionMethodFactory::EVASION_METHOD_BASIC_INFO> &methodsList, std::string &methodArgs)
{
	try 
	{
		po::options_description desc = DefineArguments(argc, argv, methodsList);
		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if(vm.count("help") || !vm.count("method")) {
			Logger::Instance().Log(_T("\nUsage: Dementia.exe [-h|--help -d|--debug -f|--file] -m <evasion_method> [-a|--args]"), INFO);

			// this SHOULD be logged to logger, but I'm too lazy to overload << for the Logger:(
			std::cout << desc << std::endl;
			return -1;
		}

		if(vm.count("debug"))
		{
			Logger::Instance().SetDebug(true);
		}

		if(vm.count("file"))
		{
			Logger::Instance().AddLogger(CONSOLE_LOG);
		}

		if(vm.count("args"))
		{
			std::string outArgs = vm["args"].as<std::string>();
			methodArgs = outArgs;
		}

		int evasionMethodID = vm["method"].as<int>();

		return evasionMethodID;
	}
	catch(std::exception& e) 
	{
		std::string msg(e.what());
		msg = "\nException occurred while parsing arguments: " + msg;

// ugly hacks
#ifdef _UNICODE
		std::wstring ws;
		ws.assign(msg.begin(), msg.end());
		Logger::Instance().Log(ws, CRITICAL_ERROR); 
#else
		Logger::Instance().Log(msg, CRITICAL_ERROR); 
#endif // _UNICODE
		return -1;
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	// adding default logger - console window
	Logger::Instance().AddLogger(CONSOLE_LOG);
	
	WriteProgramHeader();

	// create new evasion method factory, and obtain basic info about the registered modules
	boost::scoped_ptr<EvasionMethodFactory> evasionMethodFactory(new EvasionMethodFactory());
	std::list<EvasionMethodFactory::EVASION_METHOD_BASIC_INFO> methodsInfo = evasionMethodFactory->GetEvasionMethodsBasicInfo();

	// parse command line arguments and determine which method should be used
	// using std::string directly, because of the conversion that would otherwise take place
	std::string methodArguments;
	EvasionMethodFactory::EVASION_METHOD_ID evasionMethodID = (EvasionMethodFactory::EVASION_METHOD_ID) ParseArguments(argc, argv, methodsInfo, methodArguments);
	if(evasionMethodID == -1)
	{
		exit(EXIT_FAILURE);
	}

	boost::shared_ptr<IEvasionMethodPlugin> evasionMethod(evasionMethodFactory->GetEvasionMethod(evasionMethodID));
	if(evasionMethod == boost::shared_ptr<IEvasionMethodPlugin>())
	{
		Logger::Instance().Log(_T("Given method ID does not exist - please check your input parameters!"), CRITICAL_ERROR);
		exit(EXIT_FAILURE);
	}
	if(!evasionMethod->ParseArguments(methodArguments))
	{
		exit(EXIT_FAILURE);
	}

	evasionMethod->Execute();

	return 0;
}

