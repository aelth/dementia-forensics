#pragma once
#include "IConnObject.h"

class TCPListener : public IConnObject
{
public:
	TCPListener(const DWORD dwPoolHeaderSize);
	~TCPListener(void);

private:
	virtual void SetDefaultOffsetsAndSizes(void);
};
