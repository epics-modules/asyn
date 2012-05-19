/*
 * ParamListInvalidIndex.h
 *
 *  Created on: Dec 15, 2011
 *      Author: hammonds
 */

#ifndef PARAMLISTINVALIDINDEX_H_
#define PARAMLISTINVALIDINDEX_H_

#include <stdexcept>

class ParamListInvalidIndex: public std::logic_error {
public:
	ParamListInvalidIndex(const std::string& description);
};

#endif /* PARAMLISTINVALIDINDEX_H_ */
