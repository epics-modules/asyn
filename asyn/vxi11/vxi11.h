/******************************************************************************
 *
 * $RCSfile: vxi11.h,v $ 
 *
 ******************************************************************************/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * Constants for implementation of VXI-11 Instrument Protocol Specification
 *
 * Author: Benjamin Franksen
 *
 ******************************************************************************
 *
 * Notes: See the document on VXI-11
 *
 */
#ifndef VXI11_H
#define VXI11_H

/* VXI-11 error codes */
#define VXI_OK           0 /* no error */
#define VXI_SYNERR       1 /* syntax error */
#define VXI_NOACCESS     3 /* device not accessible */
#define VXI_INVLINK      4 /* invalid link identifier */
#define VXI_PARAMERR     5 /* parameter error */
#define VXI_NOCHAN       6 /* channel not established */
#define VXI_NOTSUPP      8 /* operation not supported */
#define VXI_NORES        9 /* out of resources */
#define VXI_DEVLOCK      11 /* device locked by another link */
#define VXI_NOLOCK       12 /* no lock held by this link */
#define VXI_IOTIMEOUT    15 /* I/O timeout */
#define VXI_IOERR        17 /* I/O error */
#define VXI_INVADDR      21 /* invalid address */
#define VXI_ABORT        23 /* abort */
#define VXI_CHANEXIST    29 /* channel already established */
/* VXI-11 flags  */
#define VXI_WAITLOCK     1 /* block the operation on a locked device */
#define VXI_ENDW         8 /* device_write: mark last char with END indicator */
#define VXI_TERMCHRSET   128 /* device_read: stop on termination character */
/* VXI-11 read termination reasons */
#define VXI_REQCNT       1 /* requested # of bytes have been transferred */
#define VXI_CHR          2 /* termination character matched */
#define VXI_ENDR         4 /* END indicator read */
/* VXI-11 command codes */
#define VXI_CMD_SEND 0x020000 /* send command string */
#define VXI_CMD_STAT 0x020001 /* get bus status */
#define VXI_CMD_ATN  0x020002 /* set ATN */
#define VXI_CMD_REN  0x020003 /* set REN */
#define VXI_CMD_PCTL 0x020004 /* pass control to another interface */
#define VXI_CMD_ADDR 0x02000a /* set interface (controller) address */
#define VXI_CMD_IFC  0x020010 /* IFC pulse */
/* VXI-11 bus status request numbers */
#define VXI_BSTAT_REN                   1 /* REN line TRUE */
#define VXI_BSTAT_SRQ                   2 /* SRQ line TRUE */
#define VXI_BSTAT_NDAC                  3 /* NDAC line TRUE */
#define VXI_BSTAT_SYSTEM_CONTROLLER     4 /*gateway is system controller*/
#define VXI_BSTAT_CONTROLLER_IN_CHARGE  5 /* gateway is not idle */
#define VXI_BSTAT_TALKER                6 /* gateway is talker */
#define VXI_BSTAT_LISTENER              7 /* gateway is listener */
#define VXI_BSTAT_BUS_ADDRESS           8 /* bus address */
#define NETWORK_ORDER 1 /* IOC uses network byte order */
#endif /* VXI11_H */
