/*
 * asynPortDriverErrorStates.h
 *
 *  Created on: Dec 13, 2011
 *      Author: hammonds
 */

#ifndef ASYNPORTDRIVERERRORSTATES_H_
#define ASYNPORTDRIVERERRORSTATES_H_

#include <asynDriver.h>

/* Extend asynManager error list.  We should have a way of knowing what the last error in asyn is */
#define asynParamAlreadyExists (asynStatus)(asynDisabled + 1)
#define asynParamNotFound      (asynStatus)(asynDisabled + 2)
#define asynParamWrongType     (asynStatus)(asynDisabled + 3)
#define asynParamBadIndex      (asynStatus)(asynDisabled + 4)
#define asynParamUndefined     (asynStatus)(asynDisabled + 5)


#endif /* ASYNPORTDRIVERERRORSTATES_H_ */
