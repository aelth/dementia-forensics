#pragma once

#include "ILogImpl.h"
#include "../Common/Singleton.h"

class Logger : public Singleton<Logger>
{
public:
	//static Logger &Instance() {
	//	// initialize one internal static instance of Logger class
	//	// synchronize this function - IT IS NOT THREAD SAFE SINGLETON OTHERWISE!
	//	boost::mutex mtx;
	//	boost::mutex::scoped_lock lock(mtx);
	//	static Logger _LogInternal;  
	//	return _LogInternal;
	//}

	void AddLogger(LogTarget target);
	void Log(TCharString message, LogLevel level);
	void SetDebug(bool debug);
	bool IsDebug();
private:
	bool m_isDebug;
	std::list<boost::shared_ptr<ILogImpl>> m_LogTargets;

	friend class Singleton<Logger>;

	// this is singleton pattern by Meyers -  hide constructor, copy constructor,
	// destructor and assignment operator
	Logger();
	Logger(Logger const &);
	Logger &operator=(Logger const &);
	~Logger();
};
