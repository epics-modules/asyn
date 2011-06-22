/*
 * asynParamTypes.h
 *
 *  Created on: Jun 20, 2011
 *      Author: hammonds
 */

#ifndef ASYNPARAMTYPES_H_
#define ASYNPARAMTYPES_H_
#include <asynStandardInterfaces.h>

/** Parameter data types for the parameter library */
typedef enum
{
  asynParamTypeUndefined, /**< Undefined */
  asynParamInt32,
  asynParamUInt32Digital,
  asynParamFloat64,
  asynParamOctet,
  asynParamInt8Array,
  asynParamInt16Array,
  asynParamInt32Array,
  asynParamFloat32Array,
  asynParamFloat64Array,
  asynParamGenericPointer
} asynParamType;

/* Synonyms for some unused asyn error codes for use by parameter library */
#define asynParamAlreadyExists  asynTimeout
#define asynParamNotFound       asynOverflow
#define asynParamWrongType      asynDisconnected
#define asynParamBadIndex       asynDisabled
#define asynParamUndefined      asynError

#endif /* ASYNPARAMTYPES_H_ */
