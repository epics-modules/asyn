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
 * Current Author: Benjamin Franksen
 * Original Author: John Winans
 */
#ifndef INCdevCommonGpibh
#define INCdevCommonGpibh

#include <devGpibSupport.h>
#include "shareLib.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

epicsShareFunc long epicsShareAPI devGpibLib_initAi(struct aiRecord * pai);
epicsShareFunc long epicsShareAPI devGpibLib_readAi(struct aiRecord * pai);
epicsShareFunc long epicsShareAPI devGpibLib_initAo(struct aoRecord * pao);
epicsShareFunc long epicsShareAPI devGpibLib_writeAo(struct aoRecord * pao);
epicsShareFunc long epicsShareAPI devGpibLib_initBi(struct biRecord * pbi);
epicsShareFunc long epicsShareAPI devGpibLib_readBi(struct biRecord * pbi);
epicsShareFunc long epicsShareAPI devGpibLib_initBo(struct boRecord * pbo);
epicsShareFunc long epicsShareAPI devGpibLib_writeBo(struct boRecord * pbo);
epicsShareFunc long epicsShareAPI devGpibLib_initEv(struct eventRecord * pev);
epicsShareFunc long epicsShareAPI devGpibLib_readEv(struct eventRecord * pev);
epicsShareFunc long epicsShareAPI devGpibLib_initLi(struct longinRecord * pli);
epicsShareFunc long epicsShareAPI devGpibLib_readLi(struct longinRecord * pli);
epicsShareFunc long epicsShareAPI devGpibLib_initLo(struct longoutRecord * plo);
epicsShareFunc long epicsShareAPI devGpibLib_writeLo(struct longoutRecord * plo);
epicsShareFunc long epicsShareAPI devGpibLib_initMbbi(struct mbbiRecord * pmbbi);
epicsShareFunc long epicsShareAPI devGpibLib_readMbbi(struct mbbiRecord * pmbbi);
epicsShareFunc long epicsShareAPI 
    devGpibLib_initMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
epicsShareFunc long epicsShareAPI 
    devGpibLib_readMbbiDirect(struct mbbiDirectRecord * pmbbiDirect);
epicsShareFunc long epicsShareAPI devGpibLib_initMbbo(struct mbboRecord * pmbbo);
epicsShareFunc long epicsShareAPI devGpibLib_writeMbbo(struct mbboRecord * pmbbo);
epicsShareFunc long epicsShareAPI 
    devGpibLib_initMbboDirect(struct mbboDirectRecord * pmbboDirect);
epicsShareFunc long epicsShareAPI 
    devGpibLib_writeMbboDirect(struct mbboDirectRecord * pmbboDirect);
epicsShareFunc long epicsShareAPI devGpibLib_initSi(struct stringinRecord * psi);
epicsShareFunc long epicsShareAPI devGpibLib_readSi(struct stringinRecord * psi);
epicsShareFunc long epicsShareAPI devGpibLib_initSo(struct stringoutRecord * pso);
epicsShareFunc long epicsShareAPI devGpibLib_writeSo(struct stringoutRecord * pso);
epicsShareFunc long epicsShareAPI devGpibLib_initWf(struct waveformRecord * pwf);
epicsShareFunc long epicsShareAPI devGpibLib_readWf(struct waveformRecord * pwf);


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif	/* INCdevCommonGpibh */
