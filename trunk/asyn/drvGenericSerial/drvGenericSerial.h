/*drvTermiosTtyGpib.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
#ifndef drvTermiosTtyGpibH
#define drvTermiosTtyGpibH

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern int drvGenericSerialConfigure(     /* RETURNS: 0 or -1 */
    int     linkNumber,         /* unique identifier */
    char   *serialDeviceName,   /* serial line name (e.g. /dev/ttyS0) */
    int     openOnlyOnReset     /* 1 = open conn on link reset only */
);

extern void registerEpicsIocshSttyCommand(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*drvTermiosTtyGpibH*/
