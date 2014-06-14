/************************************************************************\
* Copyright (c) 2011 Lawrence Berkeley National Laboratory, Accelerator
* Technology Group, Engineering Division
* This code is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef asynInterposeCom_H
#define asynInterposeCom_H

#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

epicsShareFunc int asynInterposeCOM(const char *portName);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInterposeCom_H */
