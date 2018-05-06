/*
 *  notcommon.h
 *  ddistnoted
 *
 *  Created by Stuart Crook on 01/03/2009.
 *  Copyright 2009 Just About Managing ltd. All rights reserved.
 *
 */

#include <CoreFoundation/CoreFoundation.h>

CFArrayRef names, objects;
CFIndex times, p;
Boolean all, immediately, cf;

Boolean parseArgs( int argc, const char * argv[] );
