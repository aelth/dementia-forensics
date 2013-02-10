#pragma once

class IEvasionMethodPlugin
{
public:
	virtual ~IEvasionMethodPlugin(void)
	{
		// intentionally left empty
	}

	virtual TCharString GetName(void) { return m_Name; }
	virtual TCharString GetShortDescription(void) { return m_shortDesc; }
	virtual TCharString GetLongDescription(void) { return m_longDesc; }
	virtual TCharString GetArgumentDescription(void) { return m_argDesc; }
	virtual bool ParseArguments(std::string args) = 0;
	virtual TCharString GetExample(void) { return m_example; }
	virtual bool Execute(void) = 0;
	virtual int GetID(void) { return m_ID; }
protected:
	int m_ID;
	TCharString m_Name;
	TCharString m_shortDesc;
	TCharString m_longDesc;
	TCharString m_argDesc;
	TCharString m_example;
	boost::program_options::options_description m_options;
};
