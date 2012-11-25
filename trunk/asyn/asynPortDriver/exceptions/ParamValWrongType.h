/*
 * ParamValWrongType.h
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALWRONGTYPE_H_
#define PARAMVALWRONGTYPE_H_

#include <stdexcept>

using std::logic_error;
class ParamValWrongType: public logic_error {
public:
	ParamValWrongType(const std::string& description);
};

#endif /* PARAMVALWRONGTYPE_H_ */
