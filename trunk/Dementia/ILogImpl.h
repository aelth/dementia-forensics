#pragma once

/*	Enumeration specifying the log level of the message, sorted by verbosity 
(debug, info, success, warning, error, critical error).
*/
typedef enum _LogLevel
{
	HEADER,
	DEBUG,
	INFO,
	SUCCESS,
	WARNING,
	ERR,
	CRITICAL_ERROR,
} LogLevel;

/*	LogTarget defines the target of the log message.
Currently supported targets are:
* CONSOLE - log to stdout, console
* FILE - log to file
*/
typedef enum _LogTarget
{
	CONSOLE_LOG,
	FILE_LOG
} LogTarget;

class ILogImpl
{
public:
	virtual ~ILogImpl(void)
	{
		// intentionally left empty
	}

	virtual void Log(TCharString message, LogLevel level) = 0;
};
