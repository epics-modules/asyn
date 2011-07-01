/*
 * ParamListInvalidIndex.h
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#ifndef PARAMLISTINVALIDINDEX_H_
#define PARAMLISTINVALIDINDEX_H_

#include "ParamListException.h"
class paramList;

class ParamListInvalidIndex: public ParamListException
{
public:
  ParamListInvalidIndex(paramList *pList);
};

#endif /* PARAMLISTINVALIDINDEX_H_ */
