#include "StdAfx.h"
#include "LogConsole.h"

LogConsole::LogConsole(void)
{
	// first obtain handle to console - exit on failure
	m_conHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	if(m_conHandle == INVALID_HANDLE_VALUE)
	{
		tcout << _T("CRITICAL ERROR: Could not obtain console handle, exiting...") << std::endl;
		exit(EXIT_FAILURE);
	}

	// save current (default) console text color - this color will be used for normal output to console
	// exit if this function fails
	CONSOLE_SCREEN_BUFFER_INFO sbInfo;
	if(!GetConsoleScreenBufferInfo(m_conHandle, &sbInfo))
	{
		tcout << _T("CRITICAL ERROR: Could not obtain screen buffer info, exiting...") << std::endl;
		exit(EXIT_FAILURE);
	}

	m_stdConsoleAttributes = sbInfo.wAttributes;
}

LogConsole::~LogConsole(void)
{
	// return console to "standard state"
	SetConsoleTextAttribute(m_conHandle, m_stdConsoleAttributes);
}

void LogConsole::Log(TCharString message, LogLevel level)
{
	// white color will be used as a default color for writing to console
	// other specific messages (WARNING, ERROR) will have specific colors
	ConsoleColor color = WHITE;

	if(level == SUCCESS)
	{
		color = GREEN;
	}
	else if(level == WARNING)
	{
		color = YELLOW;
		message = _T("[-] ") + message;
	}
	else if(level == ERR || level == CRITICAL_ERROR)
	{
		color = RED;
		message = _T("[ERROR] ") + message;
	}
	else if(level == DEBUG)
	{
		color = WHITE;
		message = _T("[DEBUG] ") + message;
	}

	WriteColored(message, color);
}

void LogConsole::WriteColored(TCharString message, ConsoleColor color)
{
	// default is "no color" - std console text attributes
	WORD wTextAttribute = m_stdConsoleAttributes;
	
	switch(color)
	{
		case WHITE:
			// WHITE = RGB + intensity
			wTextAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
			break;
		case GREEN:
			wTextAttribute = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			break;
		case YELLOW:
			wTextAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			break;
		case RED:
			wTextAttribute = FOREGROUND_RED | FOREGROUND_INTENSITY;
			break;
	}
	
	SetConsoleTextAttribute(m_conHandle, wTextAttribute);
	tcout << message << std::endl;

	// return console to "standard state" - not really necessary, but if unexpected failure occurrs, console might stay in a "colored" state
	SetConsoleTextAttribute(m_conHandle, m_stdConsoleAttributes);
}