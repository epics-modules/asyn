/* devCommonGpib.h */

/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
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

epicsShareFunc long epicsShareAPI devGpib_initAi(struct aiRecord * pai);
epicsShareFunc long epicsShareAPI devGpib_readAi(struct aiRecord * pai);
epicsShareFunc long epicsShareAPI devGpib_initAo(struct aoRecord * pao);
epicsShareFunc long epicsShareAPI devGpib_writeAo(struct aoRecord * pao);
epicsShareFunc long epicsShareAPI devGpib_initBi(struct biRecord * pbi);
epicsShareFunc long epicsShareAPI devGpib_readBi(struct biRecord * pbi);
epicsShareFunc long epicsShareAPI devGpib_initBo(struct boRecord * pbo);
epicsShareFunc long epicsShareAPI devGpib_writeBo(struct boRecord * pbo);
epicsShareFunc long epicsShareAPI devGpib_initEv(struct eventRecord * pev);
epicsShareFunc long epicsShareAPI devGpib_readEv(struct eventRecord * pev);
epicsShareFunc long epicsShareAPI devGpib_initLi(struct longinRecord * pli);
epicsShareFunc long epicsShareAPI devGpib_readLi(struct longinRecord * pli);
epicsShareFunc long epicsShareAPI devGpib_initLo(struct longoutRecord * plo);
epicsShareFunc long epicsShareAPI devGpib_writeLo(struct longoutRecord * plo);
epicsShareFunc long epicsShareAPI devGpib_initMbbi(struct mbbiRecord * pmbbi);
epicsShareFunc long epicsShareAPI devGpib_readMbbi(struct mbbiRecord * pmbbi);
epicsShareFunc long epicsShareAPI 
    devGpib_initMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
epicsShareFunc long epicsShareAPI 
    devGpib_readMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
epicsShareFunc long epicsShareAPI devGpib_initMbbo(struct mbboRecord * pmbbo);
epicsShareFunc long epicsShareAPI devGpib_writeMbbo(struct mbboRecord * pmbbo);
epicsShareFunc long epicsShareAPI 
    devGpib_initMbboDirect(struct mbboDirectRecord * pmbboDirect);
epicsShareFunc long epicsShareAPI 
    devGpib_writeMbboDirect(struct mbboDirectRecord * pmbboDirect);
epicsShareFunc long epicsShareAPI devGpib_initSi(struct stringinRecord * psi);
epicsShareFunc long epicsShareAPI devGpib_readSi(struct stringinRecord * psi);
epicsShareFunc long epicsShareAPI devGpib_initSo(struct stringoutRecord * pso);
epicsShareFunc long epicsShareAPI devGpib_writeSo(struct stringoutRecord * pso);
epicsShareFunc long epicsShareAPI devGpib_initWf(struct waveformRecord * pwf);
epicsShareFunc long epicsShareAPI devGpib_readWf(struct waveformRecord * pwf);


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif	/* INCdevCommonGpibh */
