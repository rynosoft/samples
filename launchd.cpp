/*
 *  launchd.cpp
 *
 */

#include "launchd.h"
#include "ASLLogging.h"

#include <launch.h>

#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LAUNCH_SOCKET_1	"PrimaryListeningSocket"
#define LAUNCH_SOCKET_2	"SecondaryListeningSocket"
#define LAUNCH_SOCKET_3	"SecureListeningSocket"
#define LAUNCH_SOCKET_4	"RemoteSocket"
#define LAUNCH_SOCKET_5	"RemoteWebsocket"
#define LAUNCH_SOCKET_6	"RemoteControlSocket"


//	Constructor
//
LaunchData::LaunchData()
{
	bool		success;
	
	
	mCheckinResponse = NULL;
	
	for (int idx=0; idx < kMaxListeners; idx++)
	{
		mListenerArray[idx] = NULL;
	}
	
	if (-1 == (mKernelQueue = kqueue()))
	{
		Debug_Log_ASL("failed to get kernel queue: %m");
	}
	
	// each of these can throw so caller/creator must be prepared to catch
	success = Register();
	if (success)
	{
		success = GetSocketData();
		if (success)
		{
			SetupKernelEvents();
		}
	}
}


// Destructor
//
LaunchData::~LaunchData()
{
	if (mCheckinResponse)
	{
		launch_data_free(mCheckinResponse);
	}

}


//	Checkin/register with launchd
//
bool
LaunchData::Register()
{
	bool			success = true;
	launch_data_t	checkin_request;
	launch_data_t	the_label;
	
	
	Debug_Log_ASL("");

	// Register ourselves with launchd.
	checkin_request = launch_data_new_string(LAUNCH_KEY_CHECKIN);
	if (checkin_request == NULL) 
	{
		throw("Checkin failed - couldn't create string");
	}

	mCheckinResponse = launch_msg(checkin_request);
	if (mCheckinResponse == NULL)
	{
		throw;		// IPC failure
	}
	if (LAUNCH_DATA_ERRNO == launch_data_get_type(mCheckinResponse)) 
	{
		errno = launch_data_get_errno(mCheckinResponse);
		throw errno;
	}

	the_label = launch_data_dict_lookup(mCheckinResponse, LAUNCH_JOBKEY_LABEL);
	if (NULL == the_label) 
	{
		throw("No label found");
	}
	Debug_Log_ASL("Checkin successful for %s", launch_data_get_string(the_label));

	return success;
}


//	Parse the data from launchd for info about our sockets
//
bool
LaunchData::GetSocketData()
{
	bool			success = true;
	
	
	Debug_Log_ASL("");

	mSocketsDictionary = launch_data_dict_lookup(mCheckinResponse, LAUNCH_JOBKEY_SOCKETS);
	if (NULL == mSocketsDictionary) 
	{
		throw("No sockets found to answer requests on!");
	}
	mSocketCount = launch_data_dict_get_count(mSocketsDictionary);
	Debug_Log_ASL("Incoming socket count: %d", mSocketCount);
	
	//	TODO: iterate the dictionary rather than looking up each entry
	mListenerArray[0] = ListenerArrayForLabel(LAUNCH_SOCKET_1);		// these can return NULL
	mListenerArray[1] = ListenerArrayForLabel(LAUNCH_SOCKET_2);
	mListenerArray[2] = ListenerArrayForLabel(LAUNCH_SOCKET_3);
	mListenerArray[3] = ListenerArrayForLabel(LAUNCH_SOCKET_4);
	mListenerArray[4] = ListenerArrayForLabel(LAUNCH_SOCKET_5);
	mListenerArray[5] = ListenerArrayForLabel(LAUNCH_SOCKET_6);
	
	return success;
}


//	Find and parse the data for the info for a particular socket
//
launch_data_t
LaunchData::ListenerArrayForLabel(const char* inLabel)
{
	launch_data_t		listening_fd_array = NULL;
	int					listenerCount, listenerIdx;
	
	
	listening_fd_array = launch_data_dict_lookup(mSocketsDictionary, inLabel);
	if (NULL != listening_fd_array)
	{
		listenerCount = launch_data_array_get_count(listening_fd_array);
		Debug_Log_ASL("%s listener array count: %d", inLabel, listenerCount);
		if (listenerCount == 1)
		{
			for (listenerIdx = 0; listenerIdx < listenerCount; listenerIdx++) 
			{
				struct sockaddr_in	addr;
				unsigned int		len = sizeof(struct sockaddr);
				launch_data_t		this_listening_fd;
				
				this_listening_fd = launch_data_array_get_index(listening_fd_array, listenerIdx);
				
				int	fd = launch_data_get_fd(this_listening_fd);
				
				getsockname(fd, (struct sockaddr *) &addr, &len);
				Debug_Log_ASL("Socket %d is listening on %s:%d\n", fd, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
			}
		}
	}
	else
	{
		Debug_Log_ASL("Couldn't find %s", inLabel);
	}

	return listening_fd_array;
}


//	Add each listening socket to the kernel event queue. Each incoming request will be posted as a kernel event.
//
void
LaunchData::SetupKernelEvents()
{
	short				idx;
	struct kevent		kev_init;
	
	
	Debug_Log_ASL("");
	for (idx = 0; idx < kMaxListeners; idx++) 
	{
		if (mListenerArray[idx])
		{
			int					listenerCount, listenerIdx;
			
			listenerCount = launch_data_array_get_count(mListenerArray[idx]);
			for (listenerIdx = 0; listenerIdx < listenerCount; listenerIdx++) 
			{
				int				fd;
				launch_data_t	this_listening_fd = launch_data_array_get_index(mListenerArray[idx], listenerIdx);
				
				fd = launch_data_get_fd(this_listening_fd);
				
				// add an event to the kernel queue for incoming connections on this socket
				EV_SET(&kev_init, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
				if (kevent(mKernelQueue, &kev_init, 1, NULL, 0, NULL) == -1) 
				{
					struct sockaddr_in	addr;
					unsigned int		len = sizeof(struct sockaddr);
					
					Debug_Log_ASL("kevent() call failed: %m");
					getsockname(fd, (struct sockaddr *) &addr, &len);
					Debug_Log_ASL("Socket %d on %s:%d\n", listenerIdx, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
					throw("Could not add socket to kernel event queue");
				}
				else 
				{
					Debug_Log_ASL("kev_init successful");
				}

			}
		}
		else 
		{
			Debug_Log_ASL("mListenerArray[%d] is empty", idx);
		}

	}
}

