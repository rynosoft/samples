/*
 *  launchd.h
 *
 	This is an encapsulation of 
 *
 */
 
#include <launch.h>

const short	kMaxListeners					= 6;

class LaunchData
{
public:

					LaunchData();
	virtual			~LaunchData();

	int				KernelQueue(void) const		{ return mKernelQueue; }

private:

	int				mKernelQueue;
	unsigned		mSocketCount;
	launch_data_t	mSocketsDictionary;
	launch_data_t	mCheckinResponse;	
	launch_data_t	mListenerArray[kMaxListeners];

	bool			Register(void);
	bool			GetSocketData(void);
	void			SetupKernelEvents(void);
	
	launch_data_t	ListenerArrayForLabel(const char* inLabel);
};
