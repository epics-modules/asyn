/*
 * ParamListParamNotFound.h
 *
 *  Created on: Dec 15, 2011
 *      Author: hammonds
 */

#ifndef PARAMLISTPARAMNOTFOUND_H_
#define PARAMLISTPARAMNOTFOUND_H_

#include <stdexcept>

class ParamListParamNotFound: public std::logic_error {
public:
	ParamListParamNotFound(const std::string& description);
};

#endif /* PARAMLISTPARAMNOTFOUND_H_ */
