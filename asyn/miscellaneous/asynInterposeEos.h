/*asynInterposeEos.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * End-of-string processing for asyn
 *
 * Author: Eric Norum
 */

#ifndef asynInterposeEos_H
#define asynInterposeEos_H

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

ASYN_API int asynInterposeEosConfig(const char *portName,int addr,
                                         int processEosIn,int processEosOut);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInterposeEos_H */
