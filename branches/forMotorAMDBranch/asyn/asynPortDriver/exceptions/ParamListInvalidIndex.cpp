/*
 * ParamListInvalidIndex.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#include "ParamListInvalidIndex.h"

ParamListInvalidIndex::ParamListInvalidIndex(paramList *pList):
      ParamListException(pList, "ParamListInvalidIndex",
          "The index requested is out of bounds")
{
  // TODO Auto-generated constructor stub

}
