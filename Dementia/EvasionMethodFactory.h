#pragma once

#include "IEvasionMethodPlugin.h"

class EvasionMethodFactory
{
public:
	typedef enum _EVASION_METHOD_ID
	{
		MEMORYZE_USERMODE = 1,
		KERNELMODE,
		KERNELMODE_FSFILTER,
		METHOD_NOT_DEFINED = -1,
	} EVASION_METHOD_ID;

	typedef struct _EVASION_METHOD_BASIC_INFO
	{
		EVASION_METHOD_ID ID;
		TCharString name;
		TCharString shortDescription;
		TCharString longDescription;
		TCharString arguments;
		TCharString example;
	} EVASION_METHOD_BASIC_INFO;

	EvasionMethodFactory();
	~EvasionMethodFactory(void);

	boost::shared_ptr<IEvasionMethodPlugin> GetEvasionMethod(EVASION_METHOD_ID id);
	std::list<EVASION_METHOD_BASIC_INFO> GetEvasionMethodsBasicInfo(void);
private:
	std::list<boost::shared_ptr<IEvasionMethodPlugin>> m_evasionMethodsList;
};
