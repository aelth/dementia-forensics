#include "StdAfx.h"
#include "Logger.h"
#include "LogConsole.h"

Logger::Logger(void)
{
	m_isDebug = false;
}

Logger::~Logger(void)
{
	// intentionally left empty
}

void Logger::AddLogger(LogTarget target)
{
	boost::mutex::scoped_lock lock(m_mutex);

	if(target == CONSOLE_LOG)
	{
		m_LogTargets.push_back(boost::shared_ptr<ILogImpl>(new LogConsole()));
	}
}

void Logger::Log(TCharString message, LogLevel level)
{
	boost::mutex::scoped_lock lock(m_mutex);
	
	std::list<boost::shared_ptr<ILogImpl>>::iterator iter;
	
	for(iter = m_LogTargets.begin(); iter != m_LogTargets.end(); ++iter)
	{
		(*iter)->Log(message, level);	
	}
}

void Logger::SetDebug(bool debug)
{
	boost::mutex::scoped_lock lock(m_mutex);
	m_isDebug = debug;
}

bool Logger::IsDebug()
{
	return m_isDebug;
}