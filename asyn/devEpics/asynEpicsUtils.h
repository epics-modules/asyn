/*  asynEpicsUtils.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*22-July-2004 Mark Rivers*/

#ifndef asynEpicsUtilsH
#define asynEpicsUtilsH

#include <link.h>
#include <shareLib.h>
#include <epicsTypes.h>
#include <alarm.h>
#include "asynDriver.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct asynEpicsUtils {
    asynStatus (*parseLink)(asynUser *pasynUser, DBLINK *plink, 
                char **port, int *addr, char **userParam);
    asynStatus (*parseLinkMask)(asynUser *pasynUser, DBLINK *plink, 
                char **port, int *addr, epicsUInt32 *mask,char **userParam);
    asynStatus (*parseLinkFree)(asynUser *pasynUser, 
                char **port, char **userParam);
    void       (*asynStatusToEpicsAlarm)(asynStatus status, 
                epicsAlarmCondition defaultStat, epicsAlarmCondition *pStat, 
                epicsAlarmSeverity defaultSevr, epicsAlarmSeverity *pSevr);
} asynEpicsUtils;
epicsShareExtern asynEpicsUtils *pasynEpicsUtils;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynEpicsUtilsH */
