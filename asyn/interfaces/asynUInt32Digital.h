/*  asynUInt32Digital.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  28-June-2004 Mark Rivers

*/

#ifndef asynUInt32DigitalH
#define asynUInt32DigitalH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynUInt32DigitalType "asynUInt32Digital"
typedef struct asynUInt32Digital {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
         epicsUInt32 value, epicsUInt32 mask);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *value, epicsUInt32 mask);
} asynUInt32Digital;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynUInt32DigitalH */
