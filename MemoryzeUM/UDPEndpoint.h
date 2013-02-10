#pragma once
#include "IConnObject.h"

class UDPEndpoint : public IConnObject
{
public:
	UDPEndpoint(const DWORD dwPoolHeaderSize);
	~UDPEndpoint(void);
private:
	virtual void SetDefaultOffsetsAndSizes(void);
};
