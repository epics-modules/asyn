/*
 * ParamListCallbackError.h
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#ifndef PARAMLISTCALLBACKERROR_H_
#define PARAMLISTCALLBACKERROR_H_

#include "ParamListException.h"

class ParamListCallbackError: public ParamListException
{
public:
  ParamListCallbackError(paramList *pList);
};

#endif /* PARAMLISTCALLBACKERROR_H_ */
