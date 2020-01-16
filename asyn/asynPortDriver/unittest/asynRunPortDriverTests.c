/*************************************************************************\
* Copyright (c) 2010 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* Distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 * Run asynPortDriver tests as a batch.
 *
 * Do *not* include performance measurements here, they don't help to
 * prove functionality (which is the point of this convenience routine).
 */

#include <stdio.h>
#include <epicsThread.h>
#include <epicsUnitTest.h>

int asynPortDriverTest(void);

void asynRunPortDriverTests(void)
{
    testHarness();

    runTest(asynPortDriverTest);

    /*
     * Report now in case epicsExitTest dies
     */
    testHarnessDone();
}
