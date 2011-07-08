/*
 * ParamListParamNotFound.h
 *
 *  Created on: Jul 7, 2011
 *      Author: hammonds
 */

#ifndef PARAMLISTPARAMNOTFOUND_H_
#define PARAMLISTPARAMNOTFOUND_H_

#include "ParamListException.h"

class ParamListParamNotFound : public ParamListException
{
public:
    ParamListParamNotFound(paramList *pList, char *name);
    virtual ~ParamListParamNotFound();
private:
    char *requestedName;
};

#endif /* PARAMLISTPARAMNOTFOUND_H_ */
