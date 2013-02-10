#pragma once
#include <boost/thread/mutex.hpp>

template<class T> 
class Singleton
{
public:
	static T &Instance() {
		// initialize one internal static instance of the Singleton class
		// synchronize this function - IT IS NOT THREAD SAFE SINGLETON OTHERWISE!
		boost::mutex mtx;
		boost::mutex::scoped_lock lock(mtx);
		static T _singletonInstance;  
		return _singletonInstance;
	}

protected:
	Singleton(void)
	{
		// intentionally left empty
	}

	virtual ~Singleton(void)
	{
		// intentionally left empty
	}
	
	// mutex used for synchronization of reading/writing operations inside the Singleton instances
	boost::mutex m_mutex;
private:
	// this is singleton pattern by Meyers -  hide copy constructor and assignment operator - others are protected
	Singleton(Singleton const &);
	Singleton &operator=(Singleton const &);
};