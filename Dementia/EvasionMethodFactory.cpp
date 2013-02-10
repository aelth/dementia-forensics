#include "StdAfx.h"
#include "EvasionMethodFactory.h"
#include "MemoryzeUM.h"
#include "KernelModeEvasion.h"

EvasionMethodFactory::EvasionMethodFactory(void)
{
	// register all evasion modules here by putting them in the list
	m_evasionMethodsList.push_back(boost::shared_ptr<IEvasionMethodPlugin>(new MemoryzeUM(MEMORYZE_USERMODE)));
	m_evasionMethodsList.push_back(boost::shared_ptr<IEvasionMethodPlugin>(new KernelModeEvasion(KERNELMODE)));
	m_evasionMethodsList.push_back(boost::shared_ptr<IEvasionMethodPlugin>(new KernelModeEvasion(KERNELMODE_FSFILTER)));
}

EvasionMethodFactory::~EvasionMethodFactory(void)
{
	// intentionally left empty
}

std::list<EvasionMethodFactory::EVASION_METHOD_BASIC_INFO> EvasionMethodFactory::GetEvasionMethodsBasicInfo(void)
{
	std::list<EVASION_METHOD_BASIC_INFO> basicInfoList;
	std::list<boost::shared_ptr<IEvasionMethodPlugin>>::iterator iter;

	for(iter = m_evasionMethodsList.begin(); iter != m_evasionMethodsList.end(); ++iter)
	{
		boost::shared_ptr<IEvasionMethodPlugin> evasionMethod = *iter;
		
		// get basic info about the current module
		EVASION_METHOD_BASIC_INFO basicInfo;
		basicInfo.ID = (EVASION_METHOD_ID) evasionMethod->GetID();
		basicInfo.name = evasionMethod->GetName();
		basicInfo.shortDescription = evasionMethod->GetShortDescription();
		basicInfo.longDescription = evasionMethod->GetLongDescription();
		basicInfo.arguments = evasionMethod->GetArgumentDescription();
		basicInfo.example = evasionMethod->GetExample();

		basicInfoList.push_back(basicInfo);
	}

	return basicInfoList;
}

boost::shared_ptr<IEvasionMethodPlugin> EvasionMethodFactory::GetEvasionMethod(EVASION_METHOD_ID id)
{
	std::list<boost::shared_ptr<IEvasionMethodPlugin>>::iterator iter;

	// iterate through the list of modules and immediately return the correct one, if found
	for(iter = m_evasionMethodsList.begin(); iter != m_evasionMethodsList.end(); ++iter)
	{
		boost::shared_ptr<IEvasionMethodPlugin> evasionMethod = *iter;
		if(evasionMethod->GetID() == id)
		{
			return evasionMethod;
		}
	}

	return boost::shared_ptr<IEvasionMethodPlugin>();
}