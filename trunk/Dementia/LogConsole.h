#pragma once

#include "ILogImpl.h"

class LogConsole : public ILogImpl
{
public:
	LogConsole(void);
	~LogConsole(void);

	virtual void Log(TCharString message, LogLevel level);
private:
	HANDLE m_conHandle;
	USHORT m_stdConsoleAttributes;

	typedef enum _ConsoleColor
	{
		GREEN,
		RED,
		YELLOW,
		WHITE,
	} ConsoleColor;

	void WriteColored(TCharString message, ConsoleColor color);
};
