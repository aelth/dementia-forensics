#pragma once
#include "IConnObject.h"

class TCPEndpoint : public IConnObject
{
public:
	TCPEndpoint(const DWORD dwPoolHeaderSize);
	~TCPEndpoint(void);
private:
	virtual void SetDefaultOffsetsAndSizes(void);
};
