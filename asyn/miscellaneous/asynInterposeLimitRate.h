/*asynInterposeLimitRate.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * Rate-limiting interpose layer for asyn
 *
 * Enforces a minimum interval between successive writes on a port.
 */

#ifndef asynInterposeLimitRate_H
#define asynInterposeLimitRate_H

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

ASYN_API int asynInterposeLimitRateConfig(const char *portName, double minInterval);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInterposeLimitRate_H */
