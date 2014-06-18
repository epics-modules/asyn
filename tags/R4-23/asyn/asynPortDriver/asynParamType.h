/*
 * asynParamType.h
 *
 *  Created on: Dec 13, 2011
 *      Author: hammonds
 */

#ifndef ASYNPARAMTYPE_H_
#define ASYNPARAMTYPE_H_

/** Parameter data types for the parameter library */
typedef enum {
    asynParamNotDefined,     /**< Undefined */
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



#endif /* ASYNPARAMTYPE_H_ */
