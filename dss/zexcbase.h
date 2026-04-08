#pragma once
// zexcbase.h - Minimal stubs for ZClass exception types
// Provides just enough for DSS code to compile without the full ZClass library.

#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>

class ZExceptionLocation
{
public:
	ZExceptionLocation(const char* fn = nullptr, const char* func = nullptr, unsigned long ln = 0)
		: fileName_(fn), functionName_(func), lineNumber_(ln) {}
	const char* fileName() const { return fileName_; }
	const char* functionName() const { return functionName_; }
	unsigned long lineNumber() const { return lineNumber_; }

private:
	const char* fileName_;
	const char* functionName_;
	unsigned long lineNumber_;
};

#define ZEXCEPTION_LOCATION() ZExceptionLocation(__FILE__, __FUNCTION__, __LINE__)

class ZException : public std::exception
{
public:
	enum Severity { recoverable = 0, unrecoverable = 1 };
	enum ErrorCodeGroup { baseLibrary = 0, CLibrary = 1, operatingSystem = 2, presentationSystem = 3, other_ = 4 };

	ZException() {}
	virtual ~ZException() {}

	virtual const char* name() const { return "ZException"; }
	virtual const char* text(unsigned long index = 0) const {
		if (index < texts_.size()) return texts_[index].c_str();
		return "";
	}
	virtual unsigned long textCount() const { return static_cast<unsigned long>(texts_.size()); }

	ZException& setText(const char* t) { if (texts_.empty()) texts_.push_back(t); else texts_[0] = t; return *this; }
	ZException& appendText(const char* t) { if (!texts_.empty()) texts_[0] += t; else texts_.push_back(t); return *this; }

	const ZExceptionLocation* locationAtIndex(unsigned long index = 0) const {
		if (index < locations_.size()) return &locations_[index];
		return nullptr;
	}
	unsigned long locationCount() const { return static_cast<unsigned long>(locations_.size()); }

	ZException& addLocation(const ZExceptionLocation& loc) {
		locations_.push_back(loc);
		return *this;
	}

	void logExceptionData() {
		std::cerr << "ZException: " << name() << ": " << text() << std::endl;
	}

	ZException& setSeverity(Severity) { return *this; }
	virtual bool isRecoverable() const { return true; }
	ZException& setErrorCodeGroup(int) { return *this; }

private:
	std::vector<std::string> texts_;
	std::vector<ZExceptionLocation> locations_;
};

#define ZTHROW(exc) \
	exc.addLocation(ZEXCEPTION_LOCATION()); \
	exc.logExceptionData(); \
	throw exc;

#define ZRETHROW(exc) \
	exc.addLocation(ZEXCEPTION_LOCATION()); \
	exc.logExceptionData(); \
	throw;

class ZAccessError : public ZException {
public:
	ZAccessError() {}
	ZAccessError(const char* msg) { setText(msg); }
	const char* name() const override { return "ZAccessError"; }
};

class ZInvalidParameter : public ZException {
public:
	ZInvalidParameter() {}
	ZInvalidParameter(const char* msg) { setText(msg); }
	const char* name() const override { return "ZInvalidParameter"; }
};

class ZInvalidRequest : public ZException {
public:
	ZInvalidRequest() {}
	ZInvalidRequest(const char* msg) { setText(msg); }
	const char* name() const override { return "ZInvalidRequest"; }
};

class ZSystemError : public ZException {
public:
	ZSystemError() {}
	ZSystemError(const char* msg) { setText(msg); }
	const char* name() const override { return "ZSystemError"; }
};

class ZOutOfMemory : public ZException {
public:
	ZOutOfMemory() {}
	ZOutOfMemory(const char* msg) { setText(msg); }
	ZOutOfMemory(const char* msg, int, Severity) { setText(msg); }
	const char* name() const override { return "ZOutOfMemory"; }
};

#define ZASSERTPARM(test) if(!(test)) throw ZInvalidParameter()
#define ZASSERTSTATE(test) if(!(test)) throw ZInvalidRequest()
