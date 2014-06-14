/*asynInterposeFlush.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#ifndef asynInterposeEos_H
#define asynInterposeEos_H

#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

epicsShareFunc int asynInterposeFlushConfig(
    const char *portName,int addr, int timeout);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInterposeEos_H */
