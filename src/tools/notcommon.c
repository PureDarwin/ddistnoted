/*
 *  notcommon.c
 *  ddistnoted
 *
 *  Created by Stuart Crook on 01/03/2009.
 */

#include <CoreFoundation/CoreFoundation.h>
#include "notcommon.h"

Boolean parseArgs( int argc, const char * argv[] ) 
{
	long t;
	const char *ns = NULL;
	const char *os = NULL;
	
	if( argc < 5 ) return FALSE;
	
	// set default values
	names = NULL;
	objects = NULL;
	times = 1;
	p = 0;
	all = FALSE;
	immediately = FALSE;
	cf = FALSE;
	
	//printf("what?\n");
	
	for( int i = 1; i < argc; i++ )
	{
		if( strncmp("-n", argv[i], 2) == 0 )
		{
			if( ++i == argc ) return FALSE;
			ns = argv[i];
		}
		else if( strncmp("-o", argv[i], 2) == 0 )
		{
			if( ++i == argc ) return FALSE;
			os = argv[i];
		}
		else if( strncmp("-a", argv[i], 2) == 0 )
		{
			all = TRUE;
		}
		else if( strncmp("-i", argv[i], 2) == 0 )
		{
			immediately = TRUE;
		}
		else if( strncmp("-c", argv[i], 2) == 0 )
		{
			cf = TRUE;
		}
		else if( strncmp("-t", argv[i], 2) == 0 )
		{
			printf("times\n");
			if( ++i == argc ) return FALSE;
			t = strtol(argv[i], NULL, 10);
			if( (t == 0) && ((errno == EINVAL) || (errno == ERANGE)) ) return FALSE;
			times = (CFIndex)t;
		}
		else if( strncmp("-p", argv[i], 2) == 0 )
		{
			printf("pause\n");
			if( ++i == argc ) return FALSE;
			t = strtol(argv[i], NULL, 10);
			if( (t == 0) && ((errno == EINVAL) || (errno == ERANGE)) ) return FALSE;
			p = (CFIndex)t;
		}
	}
	
	// process the name and object arguments
	if( (ns == NULL) || (os == NULL) ) return FALSE;
	
	//printf("name = %s, object = %s\n", ns, os);

	CFStringRef str;
	
	str = CFStringCreateWithCString( kCFAllocatorDefault, ns, kCFStringEncodingASCII );
	names = CFStringCreateArrayBySeparatingStrings( kCFAllocatorDefault, str, CFSTR(",") );
	CFRelease(str);
	
	str = CFStringCreateWithCString( kCFAllocatorDefault, os, kCFStringEncodingASCII );
	objects = CFStringCreateArrayBySeparatingStrings( kCFAllocatorDefault, str, CFSTR(",") );
	CFRelease(str);
	
	
	// needed to define at least notification name and object
	if( (names == NULL) || (objects == NULL) ) return FALSE;

	return TRUE; 
}
