/*
 * ParamValStringSizeRequestTooBig.h
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALSTRINGSIZEREQUESTTOOBIG_H_
#define PARAMVALSTRINGSIZEREQUESTTOOBIG_H_

#include <ParamValException.h>
#include <ParamVal.h>

class ParamValStringSizeRequestTooBig: public ParamValException
{
public:
  ParamValStringSizeRequestTooBig(ParamVal *param);
};

#endif /* PARAMVALSTRINGSIZEREQUESTTOOBIG_H_ */
