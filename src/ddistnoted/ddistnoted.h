/*
 *  ddistnoted.h
 *  ddistnoted
 *
 *  Created by Stuart Crook on 01/03/2009.
 */

// message ids recognised by ddistnoted
#define NOTIFICATION			0
#define REGISTER_PORT			1
#define UNREGISTER_PORT			2
#define REGISTER_NOTIFICATION	3
#define UNREGISTER_NOTIFICATION	4

// structure used to pass register and un-register requests
typedef struct dndNotReg {
	CFHashCode uid;
	CFHashCode name;
	CFHashCode object;
	//CFNotificationSuspensionBehavior sb;
} dndNotReg;

// the notification header structure
typedef struct dndNotHeader {
	long session;
	CFHashCode name;
	CFHashCode object;
	CFIndex flags;
} dndNotHeader;

