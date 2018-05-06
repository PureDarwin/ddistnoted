/*
 *  waitdnot.c
 *  ddistnoted
 *
 *  Created by Stuart Crook on 01/03/2009.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include "ddistnoted.h"
#include "notcommon.h"
#include "sigseg_handler.h"

#include <pthread/pthread.h>

void usage( void );
void waitCF( CFNotificationSuspensionBehavior sb );
void waitDirect( void );

void waitCFCallBack( CFNotificationCenterRef center, void *observer, CFStringRef name, const void *object, CFDictionaryRef userInfo );
CFDataRef waitDirectCallBack( CFMessagePortRef local, SInt32 msgid, CFDataRef data, void *info );

void usage( void )
{
	printf("\nwaitdnot: Register and wait for a distributed notification.\n");
	printf("    -name notificationName[,notificationName]\n");
	printf("    -object objectName[,objectName]\n");
	printf("    [-cf]  ~ wait using a CFNotificationCenter\n");
	printf("    [-times x]  ~ wait for x matching notifications\n");
	printf("    [-pause y]  ~ wait for up to y seconds for each repeate notification\n");
	printf("Options can be abbreviated to their first letter (eg. '-n').\n");
}

void waitCFCallBack( CFNotificationCenterRef center, void *observer, CFStringRef name, const void *object, CFDictionaryRef userInfo )
{
    printf("waitdnot: Got a CF notification!\n");
}

CFDataRef waitDirectCallBack( CFMessagePortRef local, SInt32 msgid, CFDataRef data, void *info )
{
    printf("waitdnot: Got a notification!\n");
	return NULL;
}

void waitCF( CFNotificationSuspensionBehavior sb )
{
	CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
    if (!center) {
        printf("couldn't get the CFNotificationCenterGetDistributedCenter()\n");
        return;
    }
	
	int nameCount = CFArrayGetCount(names);
	int objectCount = CFArrayGetCount(objects);
	
	for (int j = 0; j < objectCount; j++) {
		for (int i = 0; i < nameCount; i++) {
            printf("going to add an observer\n");
			CFNotificationCenterAddObserver(center, NULL, waitCFCallBack, CFArrayGetValueAtIndex(names, i), CFArrayGetValueAtIndex(objects, j), sb);
		}
	}
	
	//if(times == 0)
	CFRunLoopRun();

	//CFRunLoopRunInMode( kCFRunLoopCommonModes, (CFTimeInterval)p, (times==1) );
}

void WaitDirect(void) {
	
    // generate a unique port name for the current task
    char *uname;
    asprintf(&uname, "ddistnoted-%u", getpid());
    printf("message port name will be '%s'\n", uname);
	CFStringRef name = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault, uname, kCFStringEncodingASCII, NULL);
	
	// create the local port now, because the daemon will look for it
	CFMessagePortContext context = { 0, NULL, NULL, NULL, NULL };
	CFMessagePortRef local = CFMessagePortCreateLocal( kCFAllocatorDefault, name, waitDirectCallBack, &context, NULL );
	if (!local) {
		printf("CFMessagePortCreateLocal() failed to create a local message port\n");
		return;
	}
	
	CFRunLoopSourceRef rls = CFMessagePortCreateRunLoopSource( kCFAllocatorDefault, local, 0 );
    if (!rls) {
        printf("CFMessagePortCreateRunLoopSource() failed\n");
        return;
    }
	CFRunLoopAddSource( CFRunLoopGetMain(), rls, kCFRunLoopCommonModes );
	
	// create the remote port
	CFMessagePortRef remote = CFMessagePortCreateRemote(kCFAllocatorDefault, CFSTR("org.puredarwin.ddistnoted"));
	if (!remote) {
		printf("CFMessagePortCreateRemote() failed to connect to message port org.puredarwin.ddistnoted\n");
		return;
	}
	
	// squeeze our unique name, as ASCII, into a data object...
	CFIndex length = CFStringGetLength(name);
	UInt8 data[(++length + sizeof(long))];
	
	// if this isn't set -- and on other platforms -- we could maybe use userIds
	long session = getuid();
    printf("session id = %ld\n", session);
	
	if (session == 0) session = 21; // argh!!!
	
	//session = strtol( getenv("SECURITYSESSIONID"), NULL, 16 );
	*(long *)data = session;
	//printf("session id = %d\n", *(long *)data);
	
	// getsid() returns the same as getpid()...
	//printf("and getsid() reports %u\n", getsid(0));
	
	CFStringGetCString(name, (char *)(data + sizeof(long)), length, kCFStringEncodingASCII);
	CFDataRef dataOut = CFDataCreate( kCFAllocatorDefault, (const UInt8 *)data, (length + sizeof(long)) );
	//printf("'unique' name: '%s'\n", data);
	
	// ...send the register message...
	CFDataRef dataIn = NULL;
	SInt32 result = CFMessagePortSendRequest( remote, REGISTER_PORT, dataOut, 1.0, 1.0, kCFRunLoopDefaultMode, &dataIn);
    if (result || !dataIn || !CFDataGetLength(dataIn)) {
        printf("CFMessagePortSendRequest() failed (%d)\n", result);
        return;
    }
	
	CFHashCode hash;
	CFRange range = { 0, sizeof(CFHashCode) };
	CFDataGetBytes(dataIn, range, (UInt8 *)&hash);
	CFRelease(dataIn);
	CFRelease(dataOut);
	
	int nameCount = CFArrayGetCount(names);
	int objectCount = CFArrayGetCount(objects);
	dndNotReg info = { hash, 0, 0 };
	CFStringRef str;
	
	for( int j = 0; j < objectCount; j++ )
	{
		for( int i = 0; i < nameCount; i++ )
		{
			str = CFArrayGetValueAtIndex(names, i);
			if( kCFCompareEqualTo == CFStringCompare(str, CFSTR("_"), 0) )
				info.name = 0;
			else
				info.name = CFHash(str);
			
			str = CFArrayGetValueAtIndex(objects, j);
			if( kCFCompareEqualTo == CFStringCompare(str, CFSTR("_"), 0) )
				info.object = 0;
			else
				info.object = CFHash(str);
			
			dataIn = CFDataCreate( kCFAllocatorDefault, (const UInt8 *)&info, sizeof(dndNotReg) );
				
			CFMessagePortSendRequest( remote, REGISTER_NOTIFICATION, dataIn, 1.0, 1.0, NULL, NULL );
				
			CFRelease(dataIn);
		}
	}
	
	CFRunLoopRun(); // forever
}

int main (int argc, const char * argv[]) {
	
    // check that we were given valid args
    if (!parseArgs(argc, argv)) {
		usage();
		return -1;
	}

    // This works around an issue where calls to CFRunLoopGetCurrent() returns a junk address from the TDS
    CFRunLoopGetMain();
    
    install_signal_handler(SIGSEGV);
    
    printf("now: times = %ld\n", times);
    printf("     pause = %ld\n", p);
    printf("     all = %s\n", all ? "TRUE" : "FALSE");
    printf("     immediately = %s\n", immediately ? "TRUE" : "FALSE");
    printf("     cf = %s\n", cf ? "TRUE" : "FALSE");

    printf("names: ");
    for (int i = 0; i < CFArrayGetCount(names); i++) {
        if (i) printf("       ");
        CFStringRef name = CFArrayGetValueAtIndex(names, i);
        printf("%s\n", CFStringGetCStringPtr(name, kCFStringEncodingUTF8));
    }
    
    printf("objects: ");
    for (int i = 0; i < CFArrayGetCount(objects); i++) {
        if (i) printf("         ");
        CFStringRef name = CFArrayGetValueAtIndex(objects, i);
        printf("%s\n", CFStringGetCStringPtr(name, kCFStringEncodingUTF8));
    }
	
    if (cf) {
		waitCF(0);
    } else {
		WaitDirect();
    }
	
	return 0;
}
