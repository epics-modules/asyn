#ifndef _osiRpcH_
#define _osiRpcH_
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#include <rpc/rpc.h>

#ifdef vxWorks
#include <rpcLib.h>
#include <rpc/rpctypes.h>
#include <rpc/get_myaddr.h>
#include <rpc/pmap_clnt.h>
#include <inetLib.h>
#define THREAD_SAFE_RPC
#endif

#ifdef __rtems__
#include <rpc/pmap_clnt.h>
#include <rtems.h>
#define rpcTaskInit rtems_rpc_task_init
#define THREAD_SAFE_RPC
#endif

#ifdef __APPLE__
#define rpcTaskInit() 0
#define connectWithTimeout(arg1,arg2,arg3,arg4) connect((arg1),(arg2),(arg3))
#endif

#ifdef __linux__
#include <rpc/pmap_clnt.h>
#define rpcTaskInit() 0
#define connectWithTimeout(arg1,arg2,arg3,arg4) connect((arg1),(arg2),(arg3))
# ifdef svc_fdset   /* GLIBC-2.3 has thread-safe RPC and defines this macro */
#  define THREAD_SAFE_RPC
# endif
#endif

#ifdef SOLARIS
#include <rpc/svc_soc.h>
#include <rpc/clnt_soc.h>
#include <rpc/pmap_clnt.h>
#include <arpa/inet.h>
#define rpcTaskInit() 0
#define connectWithTimeout(arg1,arg2,arg3,arg4) connect((arg1),(arg2),(arg3))
#define THREAD_SAFE_RPC
#endif

#endif /* _osiRpcH_ */
