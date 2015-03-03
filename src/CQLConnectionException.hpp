/**
 * Copyright (c) 2014 DataStax
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _CQL_CONNECTION_EXCEPTION_HPP
#define _CQL_CONNECTION_EXCEPTION_HPP
#include <exception>
#include <string>

/**
 * Exception mechanism for the CQL connection class
 */
class CQLConnectionException : public std::exception {
public:
#ifdef _WIN32
	/**
	 * CQL connection exception class
	 *
	 * @param message Exception message
	 */
	CQLConnectionException(const std::string message) : std::exception(message.c_str()) {};
#else
	/**
	 * Message to display for exception
	 */
	std::string _message;
	/**
	 * CQL connection exception class
	 *
	 * @param message Exception message
	 */
	CQLConnectionException(const std::string message) : _message(message) {};
	/**
	 * Destructor
	 */
	~CQLConnectionException() throw() {}
	/**
	 * CQL connection exception class
	 *
	 * @return Exception message
	 */
	const char* what() const throw() {return _message.c_str();}
#endif
};

#endif //_CQL_CONNECTION_EXCEPTION_HPP

