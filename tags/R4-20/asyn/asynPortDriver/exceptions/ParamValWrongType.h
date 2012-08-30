/*
 * ParamValWrongType.h
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALWRONGTYPE_H_
#define PARAMVALWRONGTYPE_H_

#include <stdexcept>

class ParamValWrongType: public std::logic_error {
public:
	ParamValWrongType(const std::string& description);
};

#endif /* PARAMVALWRONGTYPE_H_ */
