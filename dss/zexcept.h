#pragma once
// zexcept stub

#include <stdexcept>
#include <string>

class ZException : public std::runtime_error
{
public:
	ZException(const char* msg) : std::runtime_error(msg) {}
	ZException(const std::string& msg) : std::runtime_error(msg) {}
	void addLocation(const std::string&) {}
};

class ZInvalidParameter : public ZException
{
public:
	ZInvalidParameter(const char* msg) : ZException(msg) {}
	ZInvalidParameter(const std::string& msg) : ZException(msg) {}
	ZInvalidParameter(const char* msg1, const char* msg2) : ZException(std::string(msg1) + " " + std::string(msg2)) {}
};
