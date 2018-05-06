/*
 *  postdnot.c
 *  postdnot -- a simple command line utility for posting distributed 
 *		notifications, either via CoreFoundation's CFNotificationCenter 
 *		or directly to the ddistnoted daemon.
 *
 *  Created by Stuart Crook on 01/03/2009.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include "ddistnoted.h"
#include "notcommon.h"
#include "sigseg_handler.h"

void usage( void );
void postCF( CFOptionFlags options );
void postDirect( CFOptionFlags options );

void usage( void )
{
	printf("\npostdnot: Post a distributed notification.\n");
	printf("    -name notificationName[,notificationName]\n");
	printf("    -object objectName[,objectName]\n");
	printf("    [-all]  ~ deliver to all sessions\n");
	printf("    [-immediately]  ~ deliver immediately\n");
	printf("    [-cf]  ~ post using a CFNotificationCenter\n");
	printf("    [-times x]  ~ repeate all notifications x times\n");
	printf("    [-pause y]  ~ wait y seconds between posting each notification\n");
	printf("Options can be abbreviated to their first letter (eg. '-n').\n");
}

void postCF( CFOptionFlags options )
{
	CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
    if (!center) {
        printf("Error: Could not get the distributed centre\n");
        return;
    }
	
	CFIndex count = times;
	int nameCount = CFArrayGetCount(names);
	int objectCount = CFArrayGetCount(objects);
	
    printf("going to be sending some messages...\n");
    
	while (count--) {
		for ( int j = 0; j < objectCount; j++ ) {
			for ( int i = 0; i < nameCount; i++ ) {
                CFStringRef name = CFArrayGetValueAtIndex(names, i);
                CFStringRef object = CFArrayGetValueAtIndex(objects, j);
				CFNotificationCenterPostNotificationWithOptions( center, name, object, NULL, options );
                printf("posted name: %s, object: %s\n", CFStringGetCStringPtr(name, kCFStringEncodingUTF8), CFStringGetCStringPtr(object, kCFStringEncodingUTF8));
			}
		}
		if ( p != 0 ) sleep(p);
	}
}

void postDirect( CFOptionFlags options )
{
	CFMessagePortRef remote = CFMessagePortCreateRemote( kCFAllocatorDefault, CFSTR("org.puredarwin.ddistnoted") );
	if( remote == NULL ) return;

	CFIndex count = 0;
	int nameCount = CFArrayGetCount(names);
	int objectCount = CFArrayGetCount(objects);
	dndNotHeader header;

	CFMutableArrayRef array = CFArrayCreateMutable( kCFAllocatorDefault, 3, NULL );
	CFArrayAppendValue(array, kCFBooleanFalse);
	CFArrayAppendValue(array, kCFBooleanFalse);
	CFArrayAppendValue(array, kCFBooleanFalse); // so we don't have to reset this...
	CFWriteStreamRef ws;
	CFDataRef data;
	
	while((times == 0) || (count++ != times))
	{
		//printf("hello\n");
		for( int j = 0; j < objectCount; j++ )
		{
			for( int i = 0; i < nameCount; i++ )
			{
				CFArraySetValueAtIndex(array, 0, CFArrayGetValueAtIndex(names, i));
				CFArraySetValueAtIndex(array, 1, CFArrayGetValueAtIndex(objects, j));
				
				header.name = CFHash(CFArrayGetValueAtIndex(names, i));
				header.object = CFHash(CFArrayGetValueAtIndex(objects, j));
				header.flags = options;
				header.session = geteuid();
				
				ws = CFWriteStreamCreateWithAllocatedBuffers( kCFAllocatorDefault, kCFAllocatorDefault );
				CFWriteStreamOpen(ws);
				CFWriteStreamWrite( ws, (const UInt8 *)&header, sizeof(dndNotHeader) );				
				CFPropertyListWriteToStream( array, ws, kCFPropertyListBinaryFormat_v1_0, NULL );
				CFWriteStreamClose(ws);
				data = CFWriteStreamCopyProperty( ws, kCFStreamPropertyDataWritten );
				CFRelease(ws);
				
				if( data == NULL ) return;
				
				CFMessagePortSendRequest( remote, NOTIFICATION, data, 1.0, 1.0, NULL, NULL );
				
				CFRelease(data);
			}
		}
		if(p != 0) sleep(p);
	}
}

int main (int argc, const char * argv[]) 
{
	if (!parseArgs(argc, argv)) {
		usage();
		return -1;
	}
    
    // This works around an issue where calls to CFRunLoopGetCurrent() returns a junk address from the TDS
    CFRunLoopGetMain();

    install_signal_handler(SIGSEGV);
	
    printf("now: times = %ld\n", (long)times);
    printf("     pause = %ld\n", (long)p);
	if(all) printf("     all = TRUE\n");
	else printf("     all = FALSE\n");
	if(immediately) printf("     immediately = TRUE\n");
	else printf("     immediately = FALSE\n");
	if(cf) printf("     cf = TRUE\n");
	else printf("     cf = FALSE\n");

	CFShow(names);
	CFShow(objects);
	
	CFOptionFlags options = 0;
	if(all) options |= kCFNotificationPostToAllSessions;
	if(immediately) options |= kCFNotificationDeliverImmediately;
	
    if (cf) {
		postCF(options);
    } else {
		postDirect(options);
    }
    
	return 0;
}
