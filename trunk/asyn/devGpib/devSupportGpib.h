/* devSupportGpib.h */

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
#ifndef INCdevSupportGpibh
#define INCdevSupportGpibh

#include <callback.h>
#include <dbScan.h>
#include <devSup.h>
#include <shareLib.h>

/* supported record types */
#include <dbCommon.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <biRecord.h>
#include <boRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <mbbiDirectRecord.h>
#include <mbboDirectRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>
#include <stringinRecord.h>
#include <stringoutRecord.h>
#include <eventRecord.h>
#include <waveformRecord.h>

#include <asynDriver.h>
#include <asynGpibDriver.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* structures used by instrument device support*/
typedef struct gpibCmd gpibCmd;
typedef struct devGpibNames devGpibNames;
typedef struct devGpibParmBlock devGpibParmBlock;
/*structures used by devGpibCommon, devSupportGpib, or other support*/
typedef struct gDset gDset;
typedef struct gpibDpvt gpibDpvt;
typedef struct devGpibPvt devGpibPvt;
typedef struct devSupportGpib devSupportGpib;

struct gpibCmd {
    gDset *dset;/* used to indicate record type supported */
    int type;	/* enum - GPIBREAD...GPIBSRQHANDLER */
    short pri;	/* request priority IB_Q_LOW, IB_G_MEDIUM, or IB_Q_HIGH */
    char *cmd;	/* CONSTANT STRING to send to instrument */
    char *format;	/* string used to generate or interpret msg */
    int rspLen;	/* room for response error message */
    int msgLen;	/* room for return data message length */
    /*convert is optional custom routine for conversions */
    int (*convert) (gpibDpvt *pgpibDpvt,int P1, int P2, char **P3);
    /*P1 plays a dual role.  user defined parameter for convert. For EFAST
      it is number of entries in EFAST table*/
    int P1;
    int P2;	/* user defined parameter used in convert() */
    /* P3 plays dual role. For EFAST address of EFAST table   */
    /* For convert it is passed to convert */
    char **P3;
    devGpibNames *pdevGpibNames; /* pointer to name strings */
    int eosChar;
};
/*Define so that it is easy to check for valid set of commands*/
#define GPIBREAD        0x00000001
#define GPIBWRITE       0x00000002
#define GPIBCMD         0x00000004
#define GPIBACMD        0x00000008
#define GPIBSOFT        0x00000010
#define GPIBREADW       0x00000020
#define GPIBRAWREAD     0x00000040
#define	GPIBEFASTO      0x00000080
#define	GPIBEFASTI      0x00000100
#define	GPIBEFASTIW     0x00000200
#define GPIBIFC         0x00000400
#define GPIBREN         0x00000800
#define GPIBDCL         0x00001000
#define GPIBLLO         0x00002000
#define GPIBSDC         0x00004000
#define GPIBGTL         0x00008000
#define GPIBRESETLNK    0x00010000
#define GPIBSRQHANDLER  0x00020000
/*GPIBEOS can be combined with some of the above commands*/
/*If set gpibCmd.eosChar is end of message terminator*/
#define GPIBEOS	        0x80000000
#define IB_Q_LOW     asynQueuePriorityLow
#define IB_Q_MEDIUM  asynQueuePriorityMedium
#define IB_Q_HIGH    asynQueuePriorityHigh
#define FILL	{NULL,0,0,NULL,NULL,0,0,NULL,0,0,NULL,NULL,-1}
#define FILL10	FILL,FILL,FILL,FILL,FILL,FILL,FILL,FILL,FILL,FILL

struct devGpibNames {
    int count;	          /* CURRENTLY only used for MBBI and MBBO */
    char **item;
    unsigned long *value; /* CURRENTLY only used for MBBI and MBBO */
    short nobt;	          /* CURRENTLY only used for MBBI and MBBO */
};

struct devGpibParmBlock {
    char *name;	        /* Name of this device support*/
    gpibCmd *gpibCmds;	/* pointer to gpib command list */
    int numparams;	/* number of elements in the command list */
    double timeout;	/* seconds to wait for I/O */
    double timeWindow;  /* seconds to stop I/O after a timeout*/
    int respond2Writes; /* set to true if a device responds to writes */
    int *debugFlag;	/* pointer to debug flag */
};

/* EFAST tables must be defied as follows
    static char *xxxx[] = {"xxx","yyy",...,0};
    IT MUST PROVIDE A 0 AS THE LAST STRING
*/

struct gDset {
    long number;
    DEVSUPFUN funPtr[6];
    devGpibParmBlock *pdevGpibParmBlock;
};

struct gpibDpvt {
    ELLNODE node; /*For use by devSupportGpib*/
    devGpibParmBlock *pdevGpibParmBlock; 
    CALLBACK callback;
    dbCommon *precord;
    asynUser *pasynUser;
    asynCommon *pasynCommon;
    void *asynCommonPvt;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
    asynGpib *pasynGpib;
    void *asynGpibPvt;
    int parm;                 /* parameter index into gpib commands */
    char *rsp;                /* for read/write message error responses */
    char *msg;                /* for read/write messages */
    int efastVal;             /* For GPIBEFASTxxx */
    void     *pupvt;          /*private pointer for custom code*/
    devGpibPvt *pdevGpibPvt;  /*private for devGpibCommon*/
};

typedef void (*gpibWork)(gpibDpvt *pgpibDpvt,int failure);
/* If a method retuns int then 0,1) => (OK, failure) */
struct devSupportGpib {
    long (*initRecord)(dbCommon *precord, struct link * plink);
    long (*processGPIBSOFT)(gpibDpvt *pgpibDpvt);
    void (*queueReadRequest)(gpibDpvt *pgpibDpvt, gpibWork finish);
    void (*queueWriteRequest)(gpibDpvt *pgpibDpvt, gpibWork finish);
    void (*queueRequest)(gpibDpvt *pgpibDpvt, gpibWork work);
    void (*report)(int interest);
    void (*registerSrqHandler)( gpibDpvt *pgpibDpvt,
        srqHandler handler,void *unsollicitedHandlerPvt);
    int (*writeMsgLong)(gpibDpvt *pgpibDpvt,long val);
    int (*writeMsgULong)(gpibDpvt *pgpibDpvt,unsigned long val);
    int (*writeMsgDouble)(gpibDpvt *pgpibDpvt,double val);
    int (*writeMsgString)(gpibDpvt *pgpibDpvt,const char *str);
};
epicsShareExtern devSupportGpib *pdevSupportGpib;

/* macros for accessing some commonly used fields*/
#define gpibDpvtGet(precord) ((gpibDpvt *)(precord)->dpvt)
#define devGpibPvtGet(precord) (((gpibDpvt *)((precord)->dpvt))->pdevGpibPvt)
#define gpibCmdGet(pgpibDpvt) \
    (&(pgpibDpvt)->pdevGpibParmBlock->gpibCmds[(pgpibDpvt)->parm])
#define gpibCmdGetType(pdpvt) \
    ((pdpvt)->pdevGpibParmBlock->gpibCmds[((pdpvt))->parm].type)
#define gpibCmdTypeNoEOS(type) ((type) & ~GPIBEOS)
#define devGpibNamesGet(pdpvt) \
    ((pdpvt)->pdevGpibParmBlock->gpibCmds[((pdpvt))->parm].pdevGpibNames)
#define requestProcessCallback(pgpibDpvt) \
    callbackRequestProcessCallback( \
       &((pgpibDpvt)->callback),(pgpibDpvt)->precord->prio,(pgpibDpvt)->precord)

/*  gpibCmd.type ************************************************************
 *
 * GPIBREAD: (1) gpibDpvt.cmd is sent to the instrument
 *           (2) data is read from the inst into gpibDpvt.msg
 *           (3) data is read from the buffer using gpibCmd.format
 * GPIBWRITE:(1) gpibDpvt.msg is created from val or rval using gpibCmd.format.
 *           (2) gpibDpvt.msg is sent to the instrument
 * GPIBCMD:  (1) gpibDpvt.cmd is sent to the instrument
 * GPIBACMD: (1) gpibDpvt.cmd is sent to the instrument with ATN active
 * GPIBSOFT: (1) No GPIB activity involved - normally retrieves internal data
 * GPIBREADW:(1) gpibDpvt.cmd is sent to the instrument
 *           (2) Wait for SRQ
 *           (3) data is read from the inst into gpibDpvt.msg
 *           (4) data is read from the buffer using gpibCmd.format
 * GPIBRAWREAD:(1) data is read from the inst into gpibDpvt.msg
 *           (2) data is read from the buffer using gpibCmd.format
 * The following is only supported on mbbo and bo record types.
 * GPIBEFASTO: (1) sends the string pointed to by p3[VAL] w/o formating
 * The following are only supported on mbbi and bi record types.
 * GPIBEFASTI: (1) gpibDpvt.cmd is sent to the instrument
 *             (2) data is read from the inst into gpibDpvt.msg
 *             (3) Check the response against P3[0..?]
 *             (4) Set the value field to index when response = P3[index]
 * GPIBEFASTIW:(1) gpibDpvt.cmd is sent to the instrument
 *             (2) Wait for SRQ
 *             (3) data is read from the inst into gpibDpvt.msg
 *             (4) Check the response against P3[0..?]
 *             (5) Set the value field to index when response = P3[index]
 * Commands for Bus Management Lines
 * GPIBIFC:     bo only. (0,1) => (do nothing, pulse Interface Clear)
 * GPIBREN:     bo only. (0,1) => (drop,assert) Remote Enable
 * Universial Commands
 * GPIBDCL:     bo only. (0,1) => (do nothing, send Device Clear)
 * GPIBLLO:     bo only. (0,1) => (do nothing, Send Local Lockout)
 * Addressed Commands
 * GPIBSDC:     bo only. (0,1) => (do nothing, send Selective Device Clear)
 * GPIBGTL:     bo only. (0,1) => (do nothing, send Go To Local)
 *
 * GPIBRESETLNK:bo only. (0,1) => (do nothing, Reset Link Thread)
 * GPIBSRQHANDLER: longin only. Register SRQ handler. val is status byte
 *
 * If a particular GPIB message does not fit one of these formats, a custom
 * routine may be provided. Store a pointer to this routine in the
 * gpibCmd.convert field to use it rather than the above approaches.
 *     It is called as follows:
 *         convert(pgpibDpvt,P1,P2,P3);
 *     where P1,P2,P3 are from gpibCmd
 ******************************************************************************/

/*  devGpibNames ************************************************
 * devGpibNames defines strings that are put into the record's
 *     znam & onam fields for BI an BO records
 *     zrst, onst... fields For MBBI or MBBO record.
 *
 * Before these strings are placed into the record, the record is
 * checked to see if there is already a string defined (could be user-entered
 * with DCT.)  If there is already a string present, it will be preserved.
 *
 * There MUST ALWAYS be 2 and only 2 entries in the names.item list
 * for BI and BO records if a name list is being specified for them here.
 * The names.count field is ignored for BI and BO record types, but
 * should be properly specified as 2 for future compatibility.
 *
 * NOTE:
 * If a name string is filled in an an MBBI/MBBO record, it's corresponding
 * value will be filled in as well.  For this reason, there MUST be
 * a value array and a valid nobt value for every MBBI/MBBO record that
 * contains an item array!
 ******************************************************************************/

/*    devGpibParmBlock ****************************************************
 * name:
 *   Name of this device support.
 * gpibCmds:
 *   Pointer to the gpibCmds array.
 * numparams:
 *   The number of parameters described in the gpibCmds array.
 * timeout:
 *   number of seconds to wait for I/O completion.
 * timeWindow:
 *   Number of seconds that should be skipped after a timeout. All commands
 *   issued within this time window will be aborted and returned as errors.
 * respond2Writes:
 *   Set to TRUE if the device responds to write operations.  This causes
 *   a read operation to follow each write operation.
 * debugFlag:
 *   Must point to a flag used to request debugging traces for the device
 *   while executing library code.
 ******************************************************************************/

/*    gDset  ************************************
 * Overloads the EPICS base definition of a DSET.
 * It allows for exactly 6 methods.
 * The address of a devGpibParmBlock follows the method definitions.
 * The instrument device support myst initialize pdevGpibParmBlock.
 *****************************************************************************/

/* gpibDpvt - dbCommon.dpvt is the address of a gpibDpvt***************
 *
 * node - For private use by devSupportGpib
 * pdevGpibParmBlock - address of the devGpibParmBlock for the instrument
 * gpibAddr - gpib address of instrument
 * callback - For use by requestProcessCallback
 * precord - address of record with dbCommon.dpvt pointing to this
 * pasynUser - For calling asynCommon methods
 * pasynCommon,asynCommonPvt - For calling asynCommon
 * pasynOctet,asynOctetPvt - For calling asynOctet
 * pasynGpib,asynGpibPvt - For calling asynGpib. May be null.
 * parm - The index of the gpibCmd in the gpibCmd array
 * rsp - Response buffer of length gpibCmd.rspLen
 * msg - Message buffer of length gpibCmd.msgLen
 * efastVal - For EFAST requests this is the index
 * pupvt - For use by specialized code. e.g. conversions.
 * pdevGpibPvt - For private use by devSupportGpib.c
 ****************************************************************************/

/* devSupportGpib - support methods for dbCommonGpib of special support
 * initRecord - Perform common initialization for a record instance
 * processGPIBSOFT - Perform operation for GPIBSOFT
 * queueReadRequest - Handle READ, READW, EFASTI, EFASTIW, and RAWREAD
 * queueWriteRequest - Handle WRITE, CMD, ACMD, EFSTO
 * queueRequest - queue a request that caller will process
 * report - issue a report about this instrument
 * registerSrqHandler - register a srqHandler for instrument
 * writeMsgLong - write gpibDpvt.msg with a long value
 * writeMsgULong - write gpibDpvt.msg with an unsigned long value
 * writeMsgDouble - write gpibDpvt.msg with a double value
 * writeMsgString - write gpibDpvt.msg with a char * value
 ****************************************************************************/

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif	/* INCdevSupportGpibh */
