/*  asynOptionSyncIO.h */
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

#ifndef asynOptionSyncIOH
#define asynOptionSyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynOptionSyncIOType "asynOptionSyncIO"
typedef struct asynOptionSyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                          asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*setOption)(asynUser *pasynUser, const char *key, const char *val, double timeout);
    asynStatus (*getOption)(asynUser *pasynUser, const char *key, char *val, int sizeval, double timeout);
    asynStatus (*setOptionOnce)(const char *port, int addr,
                    const char *key, const char *val, double timeout, const char *drvInfo);
    asynStatus (*getOptionOnce)(const char *port, int addr,
                    const char *key, char *val, int sizeval, double timeout, const char *drvInfo);
} asynOptionSyncIO;
epicsShareExtern asynOptionSyncIO *pasynOptionSyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynOptionSyncIOH */
