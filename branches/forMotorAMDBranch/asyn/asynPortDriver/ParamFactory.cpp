/*
 * ParamFactory.cpp
 *
 *  Created on: Jun 20, 2011
 *      Author: hammonds
 */
#include <ParamFactory.h>

#include <ParamVal.h>
#include <Int32Param.h>
#include <UInt32DigitalParam.h>
#include <Float64Param.h>
#include <OctetParam.h>
#include <Int8ArrayParam.h>
#include <Int16ArrayParam.h>
#include <Int32ArrayParam.h>
#include <Float32ArrayParam.h>
#include <Float64ArrayParam.h>
#include <GenericPointerParam.h>
#include <asynPortDriver.h>

ParamVal* ParamFactory::createParam(const char *name, asynParamType type,
    int index, paramList *parentList)
{
  ParamVal *pVal;
  switch (type)
  {
  case asynParamInt32:
    pVal = new Int32Param(name, index, parentList);
    break;
  case asynParamUInt32Digital:
    pVal = new UInt32DigitalParam(name, index, parentList);
    break;
  case asynParamFloat64:
    pVal = new Float64Param(name, index, parentList);
    break;
  case asynParamOctet:
    pVal = new OctetParam(name, index, parentList);
    break;
  case asynParamInt8Array:
    pVal = new Int8ArrayParam(name, index, parentList);
    break;
  case asynParamInt16Array:
    pVal = new Int16ArrayParam(name, index, parentList);
    break;
  case asynParamInt32Array:
    pVal = new Int32ArrayParam(name, index, parentList);
    break;
  case asynParamFloat32Array:
    pVal = new Float32ArrayParam(name, index, parentList);
    break;
  case asynParamFloat64Array:
    pVal = new Float64ArrayParam(name, index, parentList);
    break;
  case asynParamGenericPointer:
    pVal = new GenericPointerParam(name, index, parentList);
    break;
  };
  return pVal;
}
