/*
 * ASYN support for HiSLIP
 *

 ***************************************************************************
 * Copyright (c) 2020 N. Yamamoto <noboru.yamamoto@kek.jp>
 * based on AsynUSBTMC supoort by
 * Copyright (c) 2013 W. Eric Norum <wenorum@lbl.gov>                      *
 * This file is distributed subject to a Software License Agreement found  *
 * in the file LICENSE that is included with this distribution.            *
 ***************************************************************************
 */
//-*- coding:utf-8 -*-
/* struct HiSLIPFatalError:Exception;  //    Exception raised up for HiSLIP fatal errors */

/* struct HiSLIPError:Exception; */

/* struct _HiSLIP; */

/* struct HiSLIP:_HiSLIP; */

//constants

typedef enum CC_reuqest{
			RemoteDisable=0,
			RemoteEnable=1,
			RemoteDisableGTL=2, // disable remote  and goto local
			RemoteEnableGTR=3, // Enable remote and goto remote
			RemoteEnableLLO=4, // Enable remote and lock out local
			RemoteEnableGTRLLO=5, //
			RTL=6
} CC_request_t;

typedef enum CC_Lock{
		     release =0,
		     request =1
} CC_Lock_t;

typedef enum CC_LockResponse{
			     fail=0,         //Lock was requested but not granted
			     success=1,      //release of exclusive lock
			     success_shared=2, //release of shared lock
			     error=3 // Invalide
} CC_LockResponse_t;

static const long  HiSLIP_PROTOCOL_VERSION_MAX = 257 ; // # <major><minor> = <1><1> that is 257
static const long  HiSLIP_INITIAL_MESSAGE_ID = 0xffffff00 ;
static const long  HiSLIP_UNKNOWN_MESSAGE_ID  = 0xffffffff ;
static const long  HiSLIP_MAXIMUM_MESSAGE_SIZE = 272;  //# Following VISA 256 bytes + header length 16 bytes
static const long  HiSLIP_SOCKET_TIMEOUT = 1; //# Socket timeout
static const long  HiSLIP_LOCK_TIMEOUT = 3000;//# Lock timeout
static const long  HiSLIP_Default_Port=4880;
//
typedef enum HiSLIP_Message_Types{
				  Initialize = 0,
				  InitializeResponse = 1,
				  FatalError = 2,
				  Error = 3,
				  AsyncLock = 4,
				  AsyncLockResponse = 5,
				  Data = 6,
				  DataEnd = 7,
				  DeviceClearComplete = 8,
				  DeviceClearAcknowledge = 9,
				  AsyncRemoteLocalControl = 10,
				  AsyncRemoteLocalResponse = 11,
				  Trigger = 12,
				  Interrupted = 13,
				  AsyncInterrupted = 14,
				  AsyncMaximumMessageSize = 15,
				  AsyncMaximumMessageSizeResponse = 16,
				  AsyncInitialize = 17,
				  AsyncInitializeResponse = 18,
				  AsyncDeviceClear = 19,
				  AsyncServiceRequest = 20,
				  AsyncStatusQuery = 21,
				  AsyncStatusResponse = 22,
				  AsyncDeviceClearAcknowledge = 23,
				  AsyncLockInfo = 24,
				  AsyncLockInfoResponse = 25
} HiSLIP_Message_Types_t;

static const char *HiSLIP_Error_Messages[] = {
	"Unidentified error",
	"Unrecognized Message Type",
	"Unrecognized control code",
	"Unrecognized Vendor Defined Message",
	"Message too large"
};

static const char *HiSLIP_Fatal_Error_Messages[] = {
	"Unidentified error",
	"Poorly formed message header",
	"Attempt to use connection without both channels established",
	"Invalid Initialization Sequence",
	"Server refused connection due to maximum number of clients exceeded"
};


