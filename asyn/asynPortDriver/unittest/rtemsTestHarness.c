/*************************************************************************\
* Copyright (c) 2006 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* Distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "epicsExit.h"
#include "epicsGeneralTime.h"

int
main(int argc, char **argv)
{
    extern void asynRunPortDriverTests(void);
    generalTimeReport(1);
    asynRunPortDriverTests();
    epicsExit(0);
    return 0;
}
