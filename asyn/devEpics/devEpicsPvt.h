/***********************************************************************
* Copyright (c) 2023 Michael Davidsaver
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

#ifndef DEVEPICSPVT_H
#define DEVEPICSPVT_H

#ifdef __cplusplus
extern "C" {
#endif

struct dbCommon;

const char* asynDbGetInfo(struct dbCommon *prec, const char *infoname);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DEVEPICSPVT_H
