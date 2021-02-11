/* devCommonGpib.h */

/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * Interface for GPIB device support library
 *
 * Current Author: Marty Kraimer
 * Original Authors: John Winans and Benjamin Franksen
 */
#ifndef INCdevCommonGpibh
#define INCdevCommonGpibh

#include <devSupportGpib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

ASYN_API long  devGpib_initAi(struct aiRecord * pai);
ASYN_API long  devGpib_readAi(struct aiRecord * pai);
ASYN_API long  devGpib_initAo(struct aoRecord * pao);
ASYN_API long  devGpib_writeAo(struct aoRecord * pao);
ASYN_API long  devGpib_initBi(struct biRecord * pbi);
ASYN_API long  devGpib_readBi(struct biRecord * pbi);
ASYN_API long  devGpib_initBo(struct boRecord * pbo);
ASYN_API long  devGpib_writeBo(struct boRecord * pbo);
ASYN_API long  devGpib_initEv(struct eventRecord * pev);
ASYN_API long  devGpib_readEv(struct eventRecord * pev);
ASYN_API long  devGpib_initLi(struct longinRecord * pli);
ASYN_API long  devGpib_readLi(struct longinRecord * pli);
ASYN_API long  devGpib_initLo(struct longoutRecord * plo);
ASYN_API long  devGpib_writeLo(struct longoutRecord * plo);
ASYN_API long  devGpib_initMbbi(struct mbbiRecord * pmbbi);
ASYN_API long  devGpib_readMbbi(struct mbbiRecord * pmbbi);
ASYN_API long  devGpib_initMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
ASYN_API long  devGpib_readMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
ASYN_API long  devGpib_initMbbo(struct mbboRecord * pmbbo);
ASYN_API long  devGpib_writeMbbo(struct mbboRecord * pmbbo);
ASYN_API long  devGpib_initMbboDirect(struct mbboDirectRecord * pmbboDirect);
ASYN_API long  devGpib_writeMbboDirect(struct mbboDirectRecord * pmbboDirect);
ASYN_API long  devGpib_initSi(struct stringinRecord * psi);
ASYN_API long  devGpib_readSi(struct stringinRecord * psi);
ASYN_API long  devGpib_initSo(struct stringoutRecord * pso);
ASYN_API long  devGpib_writeSo(struct stringoutRecord * pso);
ASYN_API long  devGpib_initWf(struct waveformRecord * pwf);
ASYN_API long  devGpib_readWf(struct waveformRecord * pwf);

/*
 * SRQ support
 */
int boSRQonOff(struct gpibDpvt *pdpvt, int p1, int p2,char **p3);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* INCdevCommonGpibh */
