/*asynOctetTrapReadWrite.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* 
*  Interpose Interface for asynOctet
*
*  This driver intercepts reads and writes and calls any client
*  that has installed a callback.
*
*  Author: Dirk Zimoch
*
*/
 

#ifndef asynOctetTrapReadWrite_h
#define asynOctetTrapReadWrite_h

#include <asynDriver.h>

#define asynOctetTrapReadWriteType "asynOctetTrapReadWrite"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*asynOctetTrapReadWriteCallback) (asynUser *pasynUser,
    const char *data, int numchars, asynStatus status);

/* asynOctetTrapReadWrite methods are thread-safe but might block.
*  Thus, they can be called directly by a task that is allowed to
*  block, or indirectly with asynManager->queueRequest()
*  Note that, when using asynManager->queueRequest, a callback
*  cannot be installed or removed as long as some other asynUser has
*  locked the port/address.
*
*  Before pasynUser is freed, it must remove all callbacks that it has
*  previously installed or the system might crash!
*/

typedef struct asynOctetTrapReadWrite {
    asynStatus (*installReadCallback) (void *drvPvt, asynUser *pasynUser,
        asynOctetTrapReadWriteCallback callback);
    asynStatus (*installWriteCallback) (void *drvPvt, asynUser *pasynUser,
        asynOctetTrapReadWriteCallback callback);
    asynStatus (*removeReadCallback) (void *drvPvt, asynUser *pasynUser,
        asynOctetTrapReadWriteCallback callback);
    asynStatus (*removeWriteCallback) (void *drvPvt, asynUser *pasynUser,
        asynOctetTrapReadWriteCallback callback);
} asynOctetTrapReadWrite;

int epicsShareAPI asynOctetTrapReadWriteConfig(const char *portName, int addr);

#ifdef __cplusplus
}
#endif

#endif /* asynOctetTrapReadWrite_h */

