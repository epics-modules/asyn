/*  asynUtils.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*22-July-2004 Mark Rivers*/

#ifndef asynUtilsH
#define asynUtilsH

#include <link.h>
#include "asynDriver.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct asynUtils {
    asynStatus (*parseVmeIo)(asynUser *pasynUser, DBLINK *plink, int *card, int *signal, 
                char **port, char **userParam);
} asynUtils;
epicsShareExtern asynUtils *pasynUtils;


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynUtilsH */
