/*devGpib.h*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * Header file to be included in gpib device support modules
 *
 * Current Author: Marty Kraimer
 * Original Author: Benjamin Franksen
 ******************************************************************************
 *      This file is included by GPIB device support modules.
 *      A device support module must define one or more of the following:
 *
 *      DSET_AI
 *      DSET_AIRAW
 *      DSET_AO
 *      DSET_AORAW
 *      DSET_BI
 *      DSET_BO
 *      DSET_EV
 *      DSET_LI
 *      DSET_LO
 *      DSET_MBBI
 *      DSET_MBBID
 *      DSET_MBBO
 *      DSET_MBBOD
 *      DSET_SI
 *      DSET_SO
 *      DSET_WF
 *
 *      and then include this file.
 */
#ifndef INCdevGpibh
#define INCdevGpibh

#include <devCommonGpib.h>
#include <devSup.h>
#include <epicsExport.h>
/* forward declaration: */
static devGpibParmBlock devSupParms;

/* init_ai MUST be implemented by device specific support*/
static long init_ai(int pass);
static gDset DSET_AI = {
    6,
    {0, init_ai, devGpib_initAi, 0, devGpib_readAi, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_AI);

#ifdef DSET_AIRAW
static long dummySpecialLinconvAIRAW(struct aiRecord * pai, int after) { return 0; }
static gDset DSET_AIRAW = {
    6,
    {0, init_ai, devGpib_initAi, 0, devGpib_readAi, dummySpecialLinconvAIRAW},
    &devSupParms
};
epicsExportAddress(dset,DSET_AIRAW);
#endif

#ifdef DSET_AO
static gDset DSET_AO = {
    6,
    {0, 0, devGpib_initAo, 0, devGpib_writeAo, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_AO);
#endif

#ifdef DSET_AORAW
static long dummySpecialLinconvAORAW(struct aoRecord * pao, int after) { return 0; }
static gDset DSET_AORAW = {
    6,
    {0, 0, devGpib_initAo, 0, devGpib_writeAo, dummySpecialLinconvAORAW},
    &devSupParms
};
epicsExportAddress(dset,DSET_AORAW);
#endif

#ifdef DSET_BI
static gDset DSET_BI = {
    6,
    {0, 0, devGpib_initBi, 0, devGpib_readBi, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_BI);
#endif

#ifdef DSET_BO
static gDset DSET_BO = {
    6,
    {0, 0, devGpib_initBo, 0, devGpib_writeBo,0},
    &devSupParms
};
epicsExportAddress(dset,DSET_BO);
#endif

#ifdef DSET_EV
static gDset DSET_EV = {
    6,
    {0, 0, devGpib_initEv, 0, devGpib_readEv, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_EV);
#endif

#ifdef DSET_LI
static gDset DSET_LI = {
    6,
    {0, 0, devGpib_initLi, 0, devGpib_readLi, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_LI);
#endif

#ifdef DSET_LO
static gDset DSET_LO = {
    6,
    {0, 0, devGpib_initLo, 0, devGpib_writeLo,0},
    &devSupParms
};
epicsExportAddress(dset,DSET_LO);
#endif

#ifdef DSET_MBBI
static gDset DSET_MBBI = {
    6,
    {0, 0, devGpib_initMbbi, 0, devGpib_readMbbi, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_MBBI);
#endif

#ifdef DSET_MBBO
static gDset DSET_MBBO = {
    6,
    {0, 0, devGpib_initMbbo, 0, devGpib_writeMbbo,0},
    &devSupParms
};
epicsExportAddress(dset,DSET_MBBO);
#endif

#ifdef DSET_MBBID
static gDset DSET_MBBID = {
    6,
    {0, 0, devGpib_initMbbiDirect, 0, devGpib_readMbbiDirect,0},
    &devSupParms
};
epicsExportAddress(dset,DSET_MBBID);
#endif

#ifdef DSET_MBBOD
static gDset DSET_MBBOD = {
    6,
    {0, 0, devGpib_initMbboDirect, 0, devGpib_writeMbboDirect,0},
    &devSupParms
};
epicsExportAddress(dset,DSET_MBBOD);
#endif

#ifdef DSET_SI
static gDset DSET_SI = {
    6,
    {0, 0, devGpib_initSi, 0, devGpib_readSi, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_SI);
#endif

#ifdef DSET_SO
static gDset DSET_SO = {
    6,
    {0, 0, devGpib_initSo, 0, devGpib_writeSo,0},
    &devSupParms
};
epicsExportAddress(dset,DSET_SO);
#endif

#ifdef DSET_WF
static gDset DSET_WF = {
    6,
    {0, 0, devGpib_initWf, 0, devGpib_readWf, 0},
    &devSupParms
};
epicsExportAddress(dset,DSET_WF);
#endif

#endif /* INCdevGpibh */
