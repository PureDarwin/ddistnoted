/*
 *	ddistnoted -- the Darwin Distributed Notification Daemon
 *	Stuart Crook, 26/02/2009
 *
 *	A daemon designed to facilitate NSDistributedNotificationCenter and 
 *	CFNotificationCenter's distributed version, in a similar fashion to
 *	distnoted under OS X.
 *
 *	Unlike the OS X version, and in the interest of simplicity, this daemon
 *	does not take a client's suspension behaviour into consideration. If a
 *	notification for which the client has registered arrives, it is forwarded
 *	to the client, which (in the form of CFLite's CFNotificationCenter) then
 *	decides whether to deliver the notification or to queue it for later.
 */

#include <CoreFoundation/CoreFoundation.h>
#include "ddistnoted.h"

// because we're getting sigsevs
#include <execinfo.h>
#include <stdio.h>
#include <dlfcn.h>

static void Handler(int signal, siginfo_t *info, ucontext_t *uap) { //}(int signal) {
    void *pc = (unsigned char *)uap->uc_mcontext->__ss.__rip;
    // darwin_mcontext64
    // -> __es exception state _STRUCT_X86_EXCEPTION_STATE64
    //    __ss thread state    _STRUCT_X86_THREAD_STATE64
    Dl_info dl_info;
    int rc = dladdr(pc, &dl_info);
    if (rc) {
        printf("SIGSEV @ %s %s() + %ld\n", dl_info.dli_fname, dl_info.dli_sname, (pc - dl_info.dli_saddr));
    } else {
        printf("dladdr() failed\n");
    }
    exit(0);
}

/*
 *	Data structures
 */
typedef struct dndQueue {
	CFHashCode name;
	CFHashCode object;
	CFDataRef data;
} dndQueue;

typedef struct dndPortRecord {
	CFHashCode name;
	CFMessagePortRef port;
	long session;
	CFIndex count;
	dndQueue *queue;
} dndPortRecord;

// list of clients which have contacted the daemon
#define PORT_LIST_SIZE	64
static dndPortRecord *dndPortList = NULL;
static CFIndex dndPortListCount = 0;
static CFIndex dndPortListCapacity = 0;

typedef struct dndNotRecord {
	CFIndex index;
	CFHashCode name;
	CFHashCode object;
	long session;
} dndNotRecord;

// list of registered notifications, across all sessions
#define NOT_LIST_SIZE	256
static dndNotRecord *dndNotList = NULL;
static CFIndex dndNotListCount = 0;
static CFIndex dndNotListCapacity = 0;

/*
 *	Simple console-output-based diagnostic functions, really very definately 
 *	not to be left active in the final released code
 */
void _dndPrintPorts( void );
void _dndPrintNots( void );

void _dndPrintPorts( void )
{
    printf("PORT LIST: (count = %ld, capacity = %ld)\n", (long)dndPortListCount, (long)dndPortListCapacity);
	
	CFIndex count = dndPortListCount;
	CFIndex index = 0;
	dndPortRecord *ports = dndPortList;
	while(count--)
	{
        printf("  %3ld: name hash = 0x%lX, session = %ld\n", (long)index++, ports->name, ports->session);
		ports++;
	}
}

void _dndPrintNots( void )
{
    printf("NOTIFICATIONS LIST: (count = %ld, capacity = %ld)\n", (long)dndNotListCount, (long)dndNotListCapacity);
	
	CFIndex count = dndNotListCount;
	CFIndex index = 0;
	dndNotRecord *nots = dndNotList;
	while(count--) 
	{
		while(nots->session == 0) 
		{
            printf("  %3ld: --EMPTY--\n", (long)index++);
			nots++;
		}
        printf("  %3ld: index: %4ld name: 0x%8lX object: 0x%8lX session: %ld\n", (long)index++, (long)nots->index, nots->name, nots->object, nots->session);
		nots++;
	}
}

// amount of log noise we create
static Boolean verbose = FALSE;

/*
 *	Declarations of functions to handle each of these message types
 */
CFDataRef dndNotification( CFDataRef data ); 
CFDataRef dndRegisterPort( CFDataRef data );
CFDataRef dndUnregisterPort( CFDataRef data );
CFDataRef dndRegisterNotification( CFDataRef data );
CFDataRef dndUnregisterNotification( CFDataRef data );

/*
 *	Message recieved callback.
 *
 *	ddistnoted uses one thread (the main one) to handle all recieved messages and
 *	maintain its tables of ports and notifications. This ensure that only a single
 *	thread reads from or writes to these tables.
 */
CFDataRef dndMessageRecieved( CFMessagePortRef local, SInt32 msgid, CFDataRef data, void *info );
CFDataRef dndMessageRecieved( CFMessagePortRef local, SInt32 msgid, CFDataRef data, void *info )
{
    if (verbose) fprintf(stderr, "received a message\n");
	switch(msgid) {
		case NOTIFICATION: return dndNotification(data);
		case REGISTER_PORT: return dndRegisterPort(data);
		case UNREGISTER_PORT: return dndUnregisterPort(data);
		case REGISTER_NOTIFICATION: return dndRegisterNotification(data);
		case UNREGISTER_NOTIFICATION: return dndUnregisterNotification(data);
		//case SUSPEND: return dndSuspend(data);
		//case RESUME: return dndResume(data);
	}
	return NULL;
}

/*
 *	Process an incoming notification, copying it to various message queue
 *	according to its contents and flags, ready for the dispatch thread to
 *	send it.
 *
 *	A notification is a dndNotHeader struct followed by a serialised dict
 *	which ddistnoted frankly couldn't care less about. The header is copied
 *	and decoded but the entire data is passed on to clients, header and all.
 */
CFDataRef dndNotification( CFDataRef data )
{
	if( (dndPortListCount == 0) || (dndNotListCount == 0) ) return NULL;
	
	if(verbose) fprintf(stderr, "ddist: got a notification.\n");
	
	CFIndex length = CFDataGetLength(data);
	if( length < sizeof(dndNotHeader) ) return NULL; // an absolute minimum size

	dndNotHeader info;
	CFRange range = { 0, sizeof(dndNotHeader) };
	CFDataGetBytes(data, range, (UInt8 *)&info);
	
    if(verbose) fprintf(stderr, "ddist: len = %ld, sid = %ld, name = %8lX, object = %8lX, flags = %ld\n", length, info.session, info.name, info.object, info.flags);
	
	Boolean sendToAll = info.flags | kCFNotificationPostToAllSessions;
	
	CFIndex found = 0, index;
	CFIndex indexes[dndPortListCount];
	CFIndex count = dndNotListCount;
	dndNotRecord *nots = dndNotList;
	
	while(count--)
	{
		//printf("loop: count = %u, found = %u, nots = 0x%8X\n", count, found, nots);
		while( nots->session == 0 ) nots++;
		
		if( /* name */ ((nots->name == 0) || (nots->name == info.name))
		   /* object */ && ((nots->object == 0) || (nots->object == info.object))
		   /* session */ && (sendToAll || (nots->session == info.session)) )
		{
			index = found;
			while(index--) 
			{
				if( indexes[index] == nots->index )
					break;
			}
			if( index < 0 )
				indexes[found++] = nots->index;
		}
		nots++;
	}

    if(verbose) fprintf(stderr, "ddist: Matched notification with %ld observer(s)\n", found);

	//if( found != 0 ) CFDataRef dataCopy = CFDataCreateCopy( kCFAllocatorDefault, data );

	/*	Right, you don't have to tell me how bad this design is. For a start, the
		notification forwarding should be handled in a different thread, and for a
		second there's absolutely no error checking going on. We could at least check
		if the message port we're trying to send to is still valid. */
	
	// are there any clients to notify?
	CFMessagePortRef port;
	while(found--)
	{
		//printf("  found %u\n", indexes[found]);
		//index = indexes[found];
		port = (dndPortList + indexes[found])->port;
		if( (port != NULL) && (CFMessagePortIsValid(port) == TRUE) )
			CFMessagePortSendRequest( port, NOTIFICATION, data, 1.0, 1.0, NULL, NULL );
	}
	
	if(verbose) fprintf(stderr, "ddist: leaving notification function\n");
	return NULL;
}

/*
 *	Register an incoming message port. The port's name is stored in the data as a
 *	null-terminated ASCII string. If a remote port to it can be opened then a unique
 *	id will be assigned and returned.
 */
CFDataRef dndRegisterPort( CFDataRef data )
{
	if(verbose) fprintf(stderr, "ddist: register port\n");
	//static CFIndex otherCount = 0;
	//if(verbose) fprintf(stderr, "Message port for org.puredarwin.ddistnoted recieved message %d.\n", otherCount++);

	CFIndex length = CFDataGetLength(data);
	if( length < sizeof(long) ) return NULL; // an absolute minimum size
	
	//printf("ddist: data length = %d\n", length);
	
	long sid;
	CFRange range = { 0, sizeof(long) };
	CFDataGetBytes(data, range, (UInt8 *)&sid);
	
	//printf("\tsession id = %d\n", sid);
	
	length -= sizeof(long);
	if( length <= 0 ) return NULL;
	char chars[length];
	range.location = sizeof(long);
	range.length = length;
	CFDataGetBytes(data, range, (UInt8 *)chars);
	
	//printf("\tsending port name is '%s'\n", chars);
	
    if(verbose) fprintf(stderr, "register '%s', session %ld\n", chars, sid);
	
	CFStringRef name = CFStringCreateWithCString( kCFAllocatorDefault, (const char *)chars, kCFStringEncodingASCII );
	if( name == NULL )
	{
		fprintf(stderr, "Couldn't parse port name '%s'.\n", chars);
		
		return NULL;
	}
	
	//CFShow(name);

	// if we already have a post open to the sender, this returns it
	CFMessagePortRef port = CFMessagePortCreateRemote( kCFAllocatorDefault, name );
	if( port == NULL )
	{
		fprintf(stderr, "Couldn't open remote port back to '%s'.\n", chars);
		return NULL;
	}
	
	// see if the sender already exists
	CFHashCode hash = CFHash(name);
	dndPortRecord *ports = dndPortList;
	CFIndex count = dndPortListCount;
	while( count-- ) 
	{
		if( (ports++)->name == hash ) // a connection exists
			break;
	}
	
	/*	We're going to assign the remote port the uid hash and write its info
		into the port table at the current location of ports ... unless we're
		at the end of the table and need to extend it */
	
	
	if( (count == -1) && (dndPortListCount == dndPortListCapacity) )
	{
		dndPortListCapacity += PORT_LIST_SIZE;
        if(verbose) fprintf(stderr, "Having to extend port list to %ld entries.\n", (long)dndPortListCapacity);
		void *ptr = realloc(dndPortList, (dndPortListCapacity * sizeof(dndPortRecord)));
		
		if( ptr == NULL )
		{
            fprintf(stderr, "Unable to realloc larger port list (%ld entries).\n", (long)dndPortListCapacity);
				dndPortListCapacity -= PORT_LIST_SIZE;
			return NULL;
		}
		
		dndPortList = ptr;
		ports = dndPortList + dndPortListCount;
	}
	 
	// write info into the port record. if the port already exists this is a 
	//	little wasteful of our precious time
	ports->name = hash;
	ports->port = port;
	ports->session = sid;
	ports->count = 0;
	ports->queue = NULL;

	dndPortListCount++;
	
	//_dndPrintPorts();

//    fprintf(stderr, "returning %8lX from register port\n", hash);
	
	// return as a unique id the index of this record into the table
	return CFDataCreate( kCFAllocatorDefault, (const UInt8*)&hash, sizeof(CFHashCode) );
}

/*
 *	Unregister the port associated with a particular uid. At the moment this is
 *	a no-op because (i) the CFLite client doesn't ever get a chance to call it, and
 *	(ii) since there's no checking of where the messages come from, malicious code
 *	could randomly unregister every listening port.
 *
 *	Note that if this were ever to be implemented, records and their uids/indexes would
 *	never be reused durind the life of the daemon, and an unregistered port would be
 *	indicated by its port field being set to NULL. This is what the send thread will do
 *	if it finds a port is invalid when it goes to use it.
 */
CFDataRef dndUnregisterPort( CFDataRef data )
{
	fprintf(stderr, "UnregisterPort: should never be called\n");
	return NULL;
}

/*
 *	Register the port, identified but the given uid, to recieve a certain type of
 *	notification, identified by the hash codes of its name and object members.
 *
 */
CFDataRef dndRegisterNotification( CFDataRef data )
{
	if(verbose) fprintf(stderr, "register for a notification\n");
	
	if( dndPortListCount == 0 ) return NULL; // no clients registered
	
	CFIndex length = CFDataGetLength(data);
	if( length < sizeof(dndNotReg) ) return NULL;
	
	dndNotReg info;
	CFRange range = { 0, sizeof(dndNotReg) };
	CFDataGetBytes(data, range, (UInt8 *)&info);
	
    if(verbose) fprintf(stderr, "uid = %8lX, name = %8lX, object = %8lX\n", info.uid, info.name, info.object);
	
	// check that this uid is valid, and get its index into the ports table
	dndPortRecord *ports = dndPortList;
	CFIndex index = 0;
	while(TRUE) 
	{
		if(ports->name == info.uid)
		{
			if(ports->port == NULL) return NULL; // un-registered port
			break;
		}
		if( index++ == dndPortListCount )
		{
			if(verbose) 
                fprintf(stderr, "Recieved notification from unregistered port (0x%lX).\n", info.uid);
			return NULL;
		}
		ports++;
	}
	
    if(verbose) fprintf(stderr, "this client has index %ld\n", (long)index);
	
	// look for unique index-name-object tupple in the notifications table
	dndNotRecord *nots = dndNotList;
	CFIndex count = dndNotListCount;
	while ( count--) 
	{
		while(nots->session == 0) nots++; // the empty record marker
		if( (nots->index == index) && (nots->name == info.name) && (nots->object == info.object) )
			return NULL;
		nots++;
	}
	
	// notification isn't there, so we'll add it
	if( dndNotListCount == dndNotListCapacity )
	{
		dndNotListCapacity += NOT_LIST_SIZE;
        if(verbose) fprintf(stderr, "Having to extend notifications list to %ld entries.\n", (long)dndNotListCapacity);
		void *ptr = realloc(dndNotList, (dndNotListCapacity * sizeof(dndNotRecord)));

		if( ptr == NULL )
		{
            fprintf(stderr, "Unable to realloc larger notifications list (%ld entried).\n", (long)dndNotListCapacity);
			dndNotListCapacity -= NOT_LIST_SIZE;
			return NULL;
		}
		
		dndNotList = ptr;
		nots = dndNotList + dndNotListCount;
		for( int i = 0; i < NOT_LIST_SIZE; i++ ) (nots++)->session = 0;
		nots = dndNotList + dndNotListCount;		
	}
	else
	{
		nots = dndNotList;
		while(nots->session != 0) nots++;
	}
	
	// save the notificaton into nots
	nots->index = index;
	nots->name = info.name;
	nots->object = info.object;
	nots->session = ports->session;
	
	dndNotListCount++;
	
    if(verbose) fprintf(stderr, "registered %ld: %8lX, %8lX, %8lX\n", (long)nots->index, nots->name, nots->object, nots->session);
	
	//_dndPrintNots();
	
	// not sure if we should be returning a status message
	return NULL;
}

/*
 *	Unregister a client for a notification, removing its record from the
 *	notifications table.
 */
CFDataRef dndUnregisterNotification( CFDataRef data )
{
	if (verbose) fprintf(stderr, "Unregister for a notification.\n");
	
	if (!dndPortListCount || !dndNotListCount) return NULL;
	
	// copy & pasted (mostly) from above. could break out into separate functions
	CFIndex length = CFDataGetLength(data);
	if( length < sizeof(dndNotReg) ) return NULL;

	dndNotReg info;
	CFRange range = { 0, sizeof(dndNotReg) };
	CFDataGetBytes(data, range, (UInt8 *)&info);
	
    if(verbose) fprintf(stderr, "uid = %8lX, name = %8lX, object = %8lX\n", info.uid, info.name, info.object);
	
	// check that this uid is valid, and get its index into the ports table
	dndPortRecord *ports = dndPortList;
	CFIndex index = 0;
	while(TRUE) 
	{
		if(ports->name == info.uid)
		{
			if(ports->port == NULL) return NULL; // un-registered port
			break;
		}
		if( ++index == dndPortListCount ) return NULL;
		ports++;
	}
	
    if(verbose) fprintf(stderr, "client exists, has index %ld\n", (long)index);
	
	// look for unique index-name-object tupple in the notifications table
	dndNotRecord *nots = dndNotList;
	CFIndex count = dndNotListCount;
	while(count--) 
	{
		while(nots->session == 0) nots++; // the empty record marker
		if( (nots->index == index) && (nots->name == info.name) && (nots->object == info.object) )
		{
			nots->index = 0; // or course, these 3 are valid values...
			nots->name = 0;
			nots->object = 0;
			nots->session = 0;
			
			dndNotListCount--;

			_dndPrintNots();
			
			return NULL;
		}
		nots++;
	}

	if(verbose) fprintf(stderr, "leaving unregister notification\n");
	return NULL;
}

int main (int argc, const char * argv[]) {
    
    // SIGSEV signal handler
    struct sigaction action = { 0 };
    action.sa_sigaction = (void (*)(int, struct __siginfo *, void *))Handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &action, NULL);

    int c = -1;
    while ((c = getopt (argc, (char * const *)argv, "v")) != -1) {
        switch (c) {
            case 'v':
                verbose = true;
                break;
            default:
                fprintf(stderr, "unknown argument '-%c'\n", c);
                break;
        }
    }

	if (verbose) fprintf(stderr, "ddistnoted has started\n");
	
	// create the list for storing clients' info
	dndPortList = calloc(PORT_LIST_SIZE, sizeof(dndPortRecord));
	if (!dndPortList) {
		fprintf(stderr, "Couldn't create storage for port records\n");
		return 1;
	}
	dndPortListCapacity = PORT_LIST_SIZE;
	
	// create the list for storing notifications
	dndNotList = calloc(NOT_LIST_SIZE, sizeof(dndNotRecord));
	if (!dndNotList) {
		fprintf(stderr, "Couldn't create storage for notification records\n");
		return 1;
	}
	dndNotListCapacity = NOT_LIST_SIZE;
	
	// Create the message port. This will bootstrap_check_in() and claim the port launchd created for us
	CFMessagePortContext context = { 0, NULL, NULL, NULL, NULL };
	CFMessagePortRef port = CFMessagePortCreateLocal(kCFAllocatorDefault, CFSTR("org.puredarwin.ddistnoted"), dndMessageRecieved, &context, NULL);
	
	if (!port) {
		fprintf(stderr, "CFMessagePortCreateLocal() counldn't create local message port to org.puredarwin.ddistnoted\n");
		return 1;
	}
	
	// get the runloop source from the message port...
	CFRunLoopSourceRef rls = CFMessagePortCreateRunLoopSource( kCFAllocatorDefault, port, 0 );
    if (!rls) {
        fprintf(stderr, "CFMessagePortCreateRunLoopSource() failed\n");
        return 1;
    }
	
	// ...and add it to the main runloop
	CFRunLoopAddSource( CFRunLoopGetMain(), rls, kCFRunLoopCommonModes );
	
	// then run the runloop
	CFRunLoopRun();
	
	fprintf(stderr, "ddistnoted has escaped its runloop!\n");
    
	return 0;
}
