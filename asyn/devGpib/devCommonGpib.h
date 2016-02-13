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
#include "shareLib.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

epicsShareFunc long  devGpib_initAi(struct aiRecord * pai);
epicsShareFunc long  devGpib_readAi(struct aiRecord * pai);
epicsShareFunc long  devGpib_initAo(struct aoRecord * pao);
epicsShareFunc long  devGpib_writeAo(struct aoRecord * pao);
epicsShareFunc long  devGpib_initBi(struct biRecord * pbi);
epicsShareFunc long  devGpib_readBi(struct biRecord * pbi);
epicsShareFunc long  devGpib_initBo(struct boRecord * pbo);
epicsShareFunc long  devGpib_writeBo(struct boRecord * pbo);
epicsShareFunc long  devGpib_initEv(struct eventRecord * pev);
epicsShareFunc long  devGpib_readEv(struct eventRecord * pev);
epicsShareFunc long  devGpib_initLi(struct longinRecord * pli);
epicsShareFunc long  devGpib_readLi(struct longinRecord * pli);
epicsShareFunc long  devGpib_initLo(struct longoutRecord * plo);
epicsShareFunc long  devGpib_writeLo(struct longoutRecord * plo);
epicsShareFunc long  devGpib_initMbbi(struct mbbiRecord * pmbbi);
epicsShareFunc long  devGpib_readMbbi(struct mbbiRecord * pmbbi);
epicsShareFunc long  devGpib_initMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
epicsShareFunc long  devGpib_readMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
epicsShareFunc long  devGpib_initMbbo(struct mbboRecord * pmbbo);
epicsShareFunc long  devGpib_writeMbbo(struct mbboRecord * pmbbo);
epicsShareFunc long  devGpib_initMbboDirect(struct mbboDirectRecord * pmbboDirect);
epicsShareFunc long  devGpib_writeMbboDirect(struct mbboDirectRecord * pmbboDirect);
epicsShareFunc long  devGpib_initSi(struct stringinRecord * psi);
epicsShareFunc long  devGpib_readSi(struct stringinRecord * psi);
epicsShareFunc long  devGpib_initSo(struct stringoutRecord * pso);
epicsShareFunc long  devGpib_writeSo(struct stringoutRecord * pso);
epicsShareFunc long  devGpib_initWf(struct waveformRecord * pwf);
epicsShareFunc long  devGpib_readWf(struct waveformRecord * pwf);

/*
 * SRQ support
 */
int boSRQonOff(struct gpibDpvt *pdpvt, int p1, int p2,char **p3);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* INCdevCommonGpibh */
