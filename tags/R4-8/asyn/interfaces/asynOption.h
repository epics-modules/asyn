/*asynOption.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#ifndef asynOptionH
#define asynOptionH
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynOptionType "asynOption"
/*The following are generic methods to set/get device options*/
typedef struct asynOption {
    asynStatus (*setOption)(void *drvPvt, asynUser *pasynUser,
                                const char *key, const char *val);
    asynStatus (*getOption)(void *drvPvt, asynUser *pasynUser,
                                const char *key, char *val, int sizeval);
}asynOption;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynOptionH */
