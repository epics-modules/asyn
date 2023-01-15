/***********************************************************************
* Copyright (c) 2023 Michael Davidsaver
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#include <epicsAssert.h>

#include <epicsVersion.h>
#include <dbStaticLib.h>
#include <dbAccess.h>

#include <asynDriver.h>
#include "devEpicsPvt.h"

#if EPICS_VERSION_INT < VERSION_INT(3, 16, 1, 0)
static
void dbInitEntryFromRecord(struct dbCommon *prec,
                           struct dbEntry *pdbentry)
{
    long status;
    dbInitEntry(pdbbase, pdbentry);
    status = dbFindRecord(pdbentry, prec->name);
    assert(!status); /* we have dbCommon* so the record must exist */
}
#endif

const char* asynDbGetInfo(struct dbCommon *prec, const char *infoname)
{
    const char *ret = NULL;
    DBENTRY ent;
    dbInitEntryFromRecord(prec, &ent);
    ret = dbGetInfo(&ent, infoname);
    dbFinishEntry(&ent);
    return ret;
}
