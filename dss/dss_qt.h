#pragma once
// Minimal Qt type stubs for DSS code.
// Provides just enough to satisfy dss/ code without real Qt dependency.

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <variant>
#include <iostream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <chrono>

#include "../libraw/include/libraw_types.h"

class QString;
class QStringList;
typedef double qreal;
typedef long long qint64;
typedef unsigned long long quint64;
typedef ptrdiff_t qsizetype;
typedef unsigned int uint;
typedef unsigned long ulong;

// ============================================================
// Forward declarations (for types used before full definition)
// ============================================================
class QByteArray;
class QChar;

// ============================================================
// Qt namespace (partial - needs to exist before some methods)
// ============================================================
namespace Qt {
	inline const char* endl = "\n";
	const int CaseInsensitive = 0;
	enum CheckState { Unchecked = 0, PartiallyChecked = 1, Checked = 2 };
}

// ============================================================
// Basic types with no Qt dependencies
// ============================================================

// --- QPoint ---
class QPoint
{
public:
	int xp;
	int yp;
	QPoint() : xp(0), yp(0) {}
	QPoint(int x, int y) : xp(x), yp(y) {}
	int x() const { return xp; }
	int y() const { return yp; }
	int& rx() { return xp; }
	int& ry() { return yp; }
};

// --- QPointF ---
class QPointF
{
public:
	qreal xp;
	qreal yp;
	QPointF() : xp(0), yp(0) {}
	QPointF(qreal x, qreal y) : xp(x), yp(y) {}
	qreal x() const { return xp; }
	qreal y() const { return yp; }
	qreal& rx() { return xp; }
	qreal& ry() { return yp; }
	QPointF operator*(qreal s) const { return QPointF(xp * s, yp * s); }
	QPointF operator/(qreal s) const { return QPointF(xp / s, yp / s); }
	QPointF operator+(const QPointF& o) const { return QPointF(xp + o.xp, yp + o.yp); }
	QPointF operator-(const QPointF& o) const { return QPointF(xp - o.xp, yp - o.yp); }
	QPointF& operator+=(const QPointF& o) { xp += o.xp; yp += o.yp; return *this; }
	QPointF& operator*=(qreal s) { xp *= s; yp *= s; return *this; }
	void setX(qreal x) { xp = x; }
	void setY(qreal y) { yp = y; }
};

// ============================================================
// QByteArray (no dependency on other Qt types)
// ============================================================

class QByteArray
{
public:
	QByteArray() = default;
	QByteArray(const char* s) : data_(s ? s : "") {}
	QByteArray(const char* s, int len) : data_(s, len) {}
	QByteArray(int size, char c = 0) : data_(size, c) {}

	int size() const { return static_cast<int>(data_.size()); }
	int length() const { return size(); }
	bool isEmpty() const { return data_.empty(); }
	const char* constData() const { return data_.c_str(); }
	const char* data() const { return data_.c_str(); }
	char* data() { return data_.data(); }
	void resize(int s) { data_.resize(s); }
	QByteArray& append(char c) { data_ += c; return *this; }
	QByteArray& append(const char* s) { data_ += s; return *this; }
	QByteArray& append(const QByteArray& o) { data_ += o.data_; return *this; }

private:
	std::string data_;
};

// ============================================================
// QString and QStringList
// ============================================================

// --- QString ---
class QString
{
public:
	QString() {}
	QString(const char* s) : data_(s) {}
	QString(const std::string& s) : data_(s) {}
	QString(const QString&) = default;
	QString& operator=(const QString&) = default;

	bool isEmpty() const { return data_.empty(); }
	void clear() { data_.clear(); }

	int length() const { return static_cast<int>(data_.size()); }
	int size() const { return static_cast<int>(data_.size()); }

	QString& append(const char* s) { data_ += s; return *this; }
	QString& append(const QString& s) { data_ += s.data_; return *this; }
	QString& append(int i) { data_ += std::to_string(i); return *this; }

	double toDouble(bool* ok = nullptr) const {
		try { size_t pos; double d = std::stod(data_, &pos); if (ok) *ok = true; return d; }
		catch (...) { if (ok) *ok = false; return 0.0; }
	}
	float toFloat(bool* ok = nullptr) const {
		try { size_t pos; float f = std::stof(data_, &pos); if (ok) *ok = true; return f; }
		catch (...) { if (ok) *ok = false; return 0.0f; }
	}
	int toInt(bool* ok = nullptr) const {
		try { size_t pos; int i = std::stoi(data_, &pos); if (ok) *ok = true; return i; }
		catch (...) { if (ok) *ok = false; return 0; }
	}
	long toLong(bool* ok = nullptr) const {
		try { size_t pos; long l = std::stol(data_, &pos); if (ok) *ok = true; return l; }
		catch (...) { if (ok) *ok = false; return 0L; }
	}

	QString arg(int i) const { return QString(std::to_string(i)); }
	QString arg(unsigned long i) const { return QString(std::to_string(i)); }
	QString arg(size_t i) const { return QString(std::to_string(i)); }
	QString arg(double d, int w = 0, char f = 'g', int prec = 6) const {
		std::ostringstream oss;
		oss.precision(prec);
		oss << std::fixed << d;
		return QString(oss.str());
	}
	QString arg(const QString& s) const { return QString(s.data_); }
	QString arg(const std::u16string& s) const { std::string narrow; narrow.reserve(s.size()); for (auto c : s) narrow += static_cast<char>(c); return QString(narrow); }
	QString arg(const char8_t* s) const { return QString(reinterpret_cast<const char*>(s)); }
	QString arg(const char* s) const { return QString(s); }
	QString arg(const wchar_t* s) const {
		std::string narrow;
		while (*s) narrow += static_cast<char>(*s++);
		return QString(narrow);
	}
	QString arg(const std::u16string& s1, const QString& s2) const {
		QString result = *this;
		// Replace %1 with s1, %2 with s2
		std::string narrow; narrow.reserve(s1.size());
		for (auto c : s1) narrow += static_cast<char>(c);
		result = result.replace("%1", narrow.c_str());
		result = result.replace("%2", s2.toStdString().c_str());
		return result;
	}

	int indexOf(const char* s) const { return static_cast<int>(data_.find(s)); }
	int indexOf(char c) const { return static_cast<int>(data_.find(c)); }
	int indexOf(const QString& s) const { return static_cast<int>(data_.find(s.data_)); }
	int indexOf(const char* s, int from) const { return static_cast<int>(data_.find(s, from)); }
	int indexOf(char c, int from) const { return static_cast<int>(data_.find(c, from)); }
	int indexOf(const QString& s, int from) const { return static_cast<int>(data_.find(s.data_, from)); }
	bool startsWith(const char* s) const { return data_.compare(0, strlen(s), s) == 0; }
	bool startsWith(const QString& s) const { return data_.compare(0, s.data_.size(), s.data_) == 0; }
	bool endsWith(const char* s) const { size_t len = strlen(s); return data_.size() >= len && data_.compare(data_.size() - len, len, s) == 0; }
	bool endsWith(const QString& s) const { return endsWith(s.data_.c_str()); }
	bool contains(const char* s) const { return data_.find(s) != std::string::npos; }
	bool contains(char c) const { return data_.find(c) != std::string::npos; }
	bool contains(const QString& s) const { return data_.find(s.data_) != std::string::npos; }

	QString left(int n) const { return QString(data_.substr(0, n)); }
	QString right(int n) const { return QString(data_.substr(data_.size() > static_cast<size_t>(n) ? data_.size() - n : 0)); }
	QString mid(int pos, int n = -1) const { return QString(data_.substr(pos, n)); }

	QString trimmed() const {
		size_t start = data_.find_first_not_of(" \t\r\n");
		size_t end = data_.find_last_not_of(" \t\r\n");
		if (start == std::string::npos) return QString();
		return QString(data_.substr(start, end - start + 1));
	}

	QByteArray toUtf8() const { return QByteArray(data_.c_str()); }
	static QString fromUtf8(const char* s) { return QString(s); }
	static QString fromUtf8(const QByteArray& ba) { return QString(ba.constData()); }
	std::string toStdString() const { return data_; }
	std::u16string toStdU16String() const {
		return std::u16string(data_.begin(), data_.end());
	}

	int compare(const char* s, bool cs = true) const {
		return cs ? data_.compare(s) : _stricmp(data_.c_str(), s);
	}
	int compare(const QString& o, bool cs = true) const {
		return cs ? data_.compare(o.data_) : _stricmp(data_.c_str(), o.data_.c_str());
	}
	int compare(const std::u16string& s, bool cs = true) const {
		std::string narrow; narrow.reserve(s.size());
		for (auto c : s) narrow += static_cast<char>(c);
		return cs ? data_.compare(narrow) : _stricmp(data_.c_str(), narrow.c_str());
	}

	bool operator==(const char* s) const { return data_ == s; }
	bool operator==(const QString& o) const { return data_ == o.data_; }
	bool operator!=(const QString& o) const { return data_ != o.data_; }
	bool operator<(const QString& o) const { return data_ < o.data_; }
	bool operator>(const QString& o) const { return data_ > o.data_; }
	bool operator<=(const QString& o) const { return data_ <= o.data_; }
	bool operator>=(const QString& o) const { return data_ >= o.data_; }
	QString operator+(const char* s) const { return QString(data_ + s); }
	QString operator+(const QString& o) const { return QString(data_ + o.data_); }
	QString& operator+=(const char* s) { data_ += s; return *this; }
	QString& operator+=(const QString& o) { data_ += o.data_; return *this; }
	QString& operator+=(char c) { data_ += c; return *this; }

	static QString number(int n) { return QString(std::to_string(n)); }
	static QString number(double d, char f = 'g', int prec = 6) {
		std::ostringstream oss;
		oss.precision(prec);
		if (f == 'f') oss << std::fixed;
		else if (f == 'e') oss << std::scientific;
		oss << d;
		return QString(oss.str());
	}
	static QString fromLatin1(const char* s, int len = -1) {
		if (len < 0) return QString(s);
		return QString(std::string(s, len));
	}
	static QString fromStdString(const std::string& s) { return QString(s); }
	static QString fromStdU16String(const std::u16string& s) {
		std::string result;
		result.reserve(s.size());
		for (char16_t c : s) result += static_cast<char>(c);
		return QString(result);
	}

	QString& setNum(int n) { data_ = std::to_string(n); return *this; }
	QString& setNum(double d, char f = 'g', int prec = 6) {
		std::ostringstream oss;
		oss.precision(prec);
		if (f == 'f') oss << std::fixed;
		else if (f == 'e') oss << std::scientific;
		oss << d;
		data_ = oss.str();
		return *this;
	}

	QString toUpper() const {
		std::string s = data_;
		std::transform(s.begin(), s.end(), s.begin(), ::toupper);
		return QString(s);
	}
	QString toLower() const {
		std::string s = data_;
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);
		return QString(s);
	}
	QString& replace(int pos, int len, const QString& s) {
		data_.replace(pos, len, s.data_);
		return *this;
	}
	QString& replace(const char* from, const char* to) {
		size_t pos = 0;
		size_t flen = strlen(from);
		size_t tlen = strlen(to);
		while ((pos = data_.find(from, pos)) != std::string::npos) {
			data_.replace(pos, flen, to);
			pos += tlen;
		}
		return *this;
	}
	QString& remove(int pos, int len) {
		data_.erase(pos, len);
		return *this;
	}
	QString simplified() const {
		QString result;
		bool lastWasSpace = true;
		for (char c : data_) {
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				if (!lastWasSpace) { result.data_ += ' '; lastWasSpace = true; }
			} else {
				result.data_ += c; lastWasSpace = false;
			}
		}
		return result.trimmed();
	}
	QString section(const char* sep, int start, int end = -1) const;
	QStringList split(const char* sep) const;
	QStringList split(const QString& sep) const;

private:
	std::string data_;
};

// --- QStringList (after QString definition) ---
class QStringList : public std::vector<QString>
{
public:
	QStringList() = default;
	QStringList(const QString& s) { push_back(s); }
	QStringList(const std::vector<QString>& v) : std::vector<QString>(v) {}
	QStringList(std::vector<QString>&& v) : std::vector<QString>(std::move(v)) {}
	QStringList(std::initializer_list<QString> init) : std::vector<QString>(init) {}
	int count() const { return static_cast<int>(size()); }
	bool isEmpty() const { return empty(); }
	void append(const QString& s) { push_back(s); }
	bool contains(const QString& s) const { return std::find(begin(), end(), s) != end(); }
	QString join(const QString& sep) const {
		std::string result;
		for (size_t i = 0; i < size(); i++) {
			if (i > 0) result += sep.toStdString();
			result += (*this)[i].toStdString();
		}
		return QString(result);
	}
	QStringList& operator<<(const QString& s) { push_back(s); return *this; }
};

// QString::split and section implementations (after QStringList)
inline QStringList QString::split(const char* sep) const {
	QStringList list;
	size_t pos = 0;
	size_t len = std::string(sep).size();
	while (pos < data_.size()) {
		size_t found = data_.find(sep, pos);
		if (found == std::string::npos) { list.push_back(QString(data_.substr(pos))); break; }
		list.push_back(QString(data_.substr(pos, found - pos)));
		pos = found + len;
	}
	return list;
}

inline QStringList QString::split(const QString& sep) const {
	return split(sep.data_.c_str());
}

inline QString QString::section(const char* sep, int start, int end) const {
	QStringList parts = split(sep);
	if (parts.isEmpty()) return QString();
	if (start < 0) start = parts.count() + start;
	if (end < 0) end = parts.count() + end;
	QString result;
	for (int i = start; i <= end && i < parts.count(); ++i) {
		if (i > start) result.data_ += sep;
		result.data_ += parts[i].data_;
	}
	return result;
}

// ============================================================
// QVariant (depends on QString)
// ============================================================

// ============================================================
// QMetaType (minimal stub)
// ============================================================

class QMetaType
{
public:
	enum Type { Unknown = -1, Bool = 1, Int = 2, Double = 6, QString = 10 };
	QMetaType() : type_(Unknown) {}
	QMetaType(int t) : type_(t) {}
	int id() const { return type_; }
	bool isValid() const { return type_ != Unknown; }
	bool operator==(const QMetaType& o) const { return type_ == o.type_; }
private:
	int type_;
};

class QVariant
{
public:
	enum Type { Invalid, Bool, Int, Double, String, List, LongLong };
	QVariant() : type_(Invalid), boolVal_(false), intVal_(0), uintVal_(0), doubleVal_(0.0) {}
	QVariant(bool b) : type_(Bool), boolVal_(b), intVal_(0), uintVal_(0), doubleVal_(0.0) {}
	QVariant(int i) : type_(Int), boolVal_(false), intVal_(i), uintVal_(0), doubleVal_(0.0) {}
	QVariant(unsigned int u) : type_(Int), boolVal_(false), intVal_(static_cast<int>(u)), uintVal_(u), doubleVal_(0.0) {}
	QVariant(double d) : type_(Double), boolVal_(false), intVal_(0), uintVal_(0), doubleVal_(d) {}
	QVariant(const char* s) : type_(String), boolVal_(false), intVal_(0), uintVal_(0), doubleVal_(0.0), strVal_(s) {}
	QVariant(const std::string& s) : type_(String), boolVal_(false), intVal_(0), uintVal_(0), doubleVal_(0.0), strVal_(s) {}
	QVariant(const QString& s) : type_(String), boolVal_(false), intVal_(0), uintVal_(0), doubleVal_(0.0), strVal_(s.toStdString()) {}

	bool toBool() const { return type_ == Bool ? boolVal_ : (type_ == Int ? intVal_ != 0 : type_ == Double ? doubleVal_ != 0.0 : false); }
	int toInt() const { return type_ == Int ? intVal_ : (type_ == Double ? static_cast<int>(doubleVal_) : (type_ == String ? std::stoi(strVal_) : 0)); }
	unsigned int toUInt() const { return type_ == Int ? uintVal_ : (type_ == Double ? static_cast<unsigned int>(doubleVal_) : 0); }
	double toDouble() const { return type_ == Double ? doubleVal_ : (type_ == Int ? static_cast<double>(intVal_) : (type_ == String ? std::stod(strVal_) : 0.0)); }
	QString toString() const { return type_ == String ? QString(strVal_) : QString(); }
	bool isNull() const { return type_ == Invalid; }
	bool operator!=(const QVariant& o) const {
		if (type_ != o.type_) return true;
		switch (type_) {
		case Bool: return boolVal_ != o.boolVal_;
		case Int: return intVal_ != o.intVal_;
		case Double: return doubleVal_ != o.doubleVal_;
		case String: return strVal_ != o.strVal_;
		default: return false;
		}
	}
	bool canConvert(Type) const { return true; }
	bool canConvert(const QMetaType&) const { return true; }
	bool convert(const QMetaType& target) {
		// Convert the held value to match the target type
		if (target.id() == QMetaType::Int) {
			intVal_ = toInt(); type_ = Int; return true;
		}
		if (target.id() == QMetaType::Double) {
			doubleVal_ = toDouble(); type_ = Double; return true;
		}
		if (target.id() == QMetaType::Bool) {
			boolVal_ = toBool(); type_ = Bool; return true;
		}
		if (target.id() == QMetaType::QString) {
			strVal_ = toString().toStdString(); type_ = String; return true;
		}
		return true;
	}
	QMetaType metaType() const {
		switch (type_) {
		case Bool: return QMetaType(QMetaType::Bool);
		case Int: return QMetaType(QMetaType::Int);
		case Double: return QMetaType(QMetaType::Double);
		case String: return QMetaType(QMetaType::QString);
		default: return QMetaType();
		}
	}

private:
	Type type_;
	bool boolVal_;
	int intVal_;
	unsigned int uintVal_;
	double doubleVal_;
	std::string strVal_;
};

// ============================================================
// QDate, QTime, QDateTime
// ============================================================

class QDate
{
public:
	QDate() = default;
	QDate(int y, int m, int d) : y_(y), m_(m), d_(d) {}

	static QDate fromString(const QString&, const char* = nullptr) { return QDate(); }
	QString toString(const char* = nullptr) const { return QString(); }
	bool isValid() const { return y_ != 0 || m_ != 0 || d_ != 0; }
	int year() const { return y_; }
	int month() const { return m_; }
	int day() const { return d_; }

private:
	int y_ = 0, m_ = 0, d_ = 0;
};

class QTime
{
public:
	QTime() = default;
	QTime(int h, int m, int s = 0, int ms = 0) : h_(h), m_(m), s_(s), ms_(ms) {}

	static QTime fromString(const QString&, const char* = nullptr) { return QTime(); }
	QString toString(const char* = nullptr) const { return QString(); }
	bool isValid() const { return h_ >= 0 && m_ >= 0 && s_ >= 0; }
	int hour() const { return h_; }
	int minute() const { return m_; }
	int second() const { return s_; }

private:
	int h_ = -1, m_ = -1, s_ = -1, ms_ = 0;
};

class QDateTime
{
public:
	QDateTime() = default;
	QDateTime(const QDateTime&) = default;
	QDateTime(const QDate& d, const QTime& t) : date_(d), time_(t) {}

	static QDateTime currentDateTime() { return QDateTime(); }
	static QDateTime fromString(const QString&, const char* = nullptr) { return QDateTime(); }
	static QDateTime fromSecsSinceEpoch(qint64) { return QDateTime(); }

	bool isValid() const { return date_.isValid(); }
	QString toString(const char* fmt = nullptr) const { return QString(); }
	qint64 secsTo(const QDateTime&) const { return 0; }
	bool operator>(const QDateTime&) const { return false; }

private:
	QDate date_;
	QTime time_;
};

// ============================================================
// File I/O: QFile, QFileInfo, QDir
// ============================================================

class QFile
{
public:
	enum OpenMode { ReadOnly = 1, WriteOnly = 2, ReadWrite = 3, Append = 4, Truncate = 8, Text = 16 };
	QFile() {}
	QFile(const std::string& name) : fileName_(name) {}
	QFile(const QString& name) : fileName_(name.toStdString()) {}
	QFile(const std::filesystem::path& p) : fileName_(p.string()) {}

	bool open(int) { return true; }
	void close() {}
	bool isOpen() const { return false; }
	int write(const char* data, int len) { return len; }
	int write(const std::string& data) { return static_cast<int>(data.size()); }
	int write(const QByteArray& ba) { return ba.size(); }
	bool remove() { return true; }
	QString fileName() const { return QString(fileName_); }
	QByteArray peek(qint64) { return QByteArray(); }
	bool exists() const { return std::filesystem::exists(fileName_); }
	static bool exists(const QString& path) { return std::filesystem::exists(path.toStdString()); }

private:
	std::string fileName_;
};

class QFileInfo
{
public:
	QFileInfo() = default;
	QFileInfo(const QString& path) : path_(path.toStdString()) {}
	QFileInfo(const std::filesystem::path& p) : path_(p.string()) {}

	QString path() const { return QString(path_.parent_path().string()); }
	QString fileName() const { return QString(path_.filename().string()); }
	QString completeBaseName() const {
		auto name = path_.stem().string();
		return QString(name);
	}
	QString suffix() const { return QString(path_.extension().string()); }
	QDateTime birthTime() const { return QDateTime(); }
	QDateTime lastModified() const { return QDateTime(); }
	bool exists() const { return std::filesystem::exists(path_); }
	bool isDir() const { return std::filesystem::is_directory(path_); }
	bool isFile() const { return std::filesystem::is_regular_file(path_); }

private:
	std::filesystem::path path_;
};

class QDir
{
public:
	static QString separator() { return "\\"; }
	static QString toNativeSeparators(const QString& p) { return p; }
};

// ============================================================
// I/O base classes
// ============================================================

class QIODeviceBase
{
public:
	static const int Text = 16;
	static const int ReadOnly = 1;
	static const int WriteOnly = 2;
};

class QIODevice
{
public:
	static constexpr int ReadOnly = 1;
	static constexpr int WriteOnly = 2;
};

// ============================================================
// QTextStream
// ============================================================

class QTextStream
{
public:
	QTextStream() : buf_(nullptr), file_(nullptr) {}
	QTextStream(QByteArray* ba) : buf_(ba), file_(nullptr) {}
	QTextStream(QFile* f) : buf_(nullptr), file_(f) {}

	QTextStream& operator<<(const char* s) { if (buf_) buf_->append(s); if (file_) file_->write(s, static_cast<int>(std::strlen(s))); return *this; }
	QTextStream& operator<<(const QString& s) { if (buf_) buf_->append(s.toUtf8()); if (file_) file_->write(s.toUtf8()); return *this; }
	QTextStream& operator<<(int i) { return *this << std::to_string(i).c_str(); }
	QTextStream& operator<<(size_t i) { return *this << std::to_string(i).c_str(); }
	QTextStream& operator<<(double d) { return *this << std::to_string(d).c_str(); }

	QString readLine() { return QString(); }
	bool atEnd() const { return true; }

private:
	QByteArray* buf_;
	QFile* file_;
};

// ============================================================
// QImage and pixel types
// ============================================================

class QImage
{
public:
	enum Format {
		Format_Invalid,
		Format_Mono,
		Format_MonoLSB,
		Format_Indexed8,
		Format_RGB32,
		Format_ARGB32,
		Format_ARGB32_Premultiplied,
		Format_Grayscale8,
		Format_Grayscale16,
		Format_RGBA64
	};

	QImage() : width_(0), height_(0), format_(Format_Invalid), data_() {}
	QImage(const QString&) : width_(0), height_(0), format_(Format_Invalid), data_() {}
	QImage(int w, int h, Format f) : width_(w), height_(h), format_(f), data_(w * h * 4, 0) {}

	bool isNull() const { return format_ == Format_Invalid; }
	int width() const { return width_; }
	int height() const { return height_; }
	Format format() const { return format_; }
	int bytesPerLine() const { return width_ * 4; }
	int bitPlaneCount() const { return 32; }

	uchar* bits() { return data_.data(); }
	const uchar* constBits() const { return data_.data(); }
	uchar* scanLine(int) { return data_.data(); }

	void setPixel(int, int, uint32_t) {}

private:
	int width_;
	int height_;
	Format format_;
	std::vector<uchar> data_;
};

typedef uint32_t QRgb;

class QRgba64
{
public:
	static QRgba64 fromRgba64(uint16_t r, uint16_t g, uint16_t b, uint16_t a = 65535)
	{
		QRgba64 c;
		c.r_ = r; c.g_ = g; c.b_ = b; c.a_ = a;
		return c;
	}
	uint16_t red() const { return r_; }
	uint16_t green() const { return g_; }
	uint16_t blue() const { return b_; }
	uint16_t alpha() const { return a_; }

private:
	uint16_t r_ = 0, g_ = 0, b_ = 0, a_ = 0;
};

inline int qRed(QRgb c) { return (c >> 16) & 0xFF; }
inline int qGreen(QRgb c) { return (c >> 8) & 0xFF; }
inline int qBlue(QRgb c) { return c & 0xFF; }
inline int qAlpha(QRgb c) { return (c >> 24) & 0xFF; }
inline QRgb qRgb(int r, int g, int b) { return (0xFFu << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF); }
inline QRgb qRgba(int r, int g, int b, int a) { return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF); }
inline QRgba64 qRgba64(uint16_t r, uint16_t g, uint16_t b, uint16_t a = 65535) { return QRgba64::fromRgba64(r, g, b, a); }

// ============================================================
// Misc Qt types
// ============================================================

class QCoreApplication
{
public:
	static QString translate(const char*, const char* key, const char* = nullptr) {
		return QString(key);
	}
};

class QSysInfo
{
public:
	static QString prettyProductName() { return QString("Windows 10"); }
	static QString buildAbi() { return QString("x86_64-little_endian-ilp32"); }
	static QString currentCpuArchitecture() { return QString("x86_64"); }
};

class QLocale
{
public:
	static constexpr int FloatingPointShortest = -128;
};

class QMimeType
{
public:
	QMimeType() = default;
	QString name() const { return QString(); }
	QStringList suffixes() const { return QStringList(); }
	bool isValid() const { return false; }
	bool isInherited(const QString&) const { return false; }
	bool inherits(const QString&) const { return false; }
	bool operator==(const QMimeType&) const { return true; }
	bool operator!=(const QMimeType&) const { return false; }
};

class QMimeDatabase
{
public:
	QMimeDatabase() = default;
	QMimeType mimeTypeForFile(const QString&) const { return QMimeType(); }
	QMimeType mimeTypeForFile(const std::string&) const { return QMimeType(); }
	QMimeType mimeTypeForFile(const std::filesystem::path&) const { return QMimeType(); }
	QMimeType mimeTypeForFile(const QFileInfo& fi) const { return QMimeType(); }
	QMimeType mimeTypeForName(const QString&) const { return QMimeType(); }
};

class QSettings
{
public:
	QVariant value(const QString&, const QVariant& = QVariant{}) const { return QVariant{}; }
	void setValue(const QString&, const QVariant&) {}
	void beginGroup(const QString&) {}
	void endGroup() {}
};

// ============================================================
// QElapsedTimer
// ============================================================

class QElapsedTimer
{
public:
	void start() { start_ = std::chrono::steady_clock::now(); }
	qint64 elapsed() const {
		auto now = std::chrono::steady_clock::now();
		return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
	}
	qint64 restart() {
		qint64 e = elapsed();
		start_ = std::chrono::steady_clock::now();
		return e;
	}
	bool isValid() const { return start_ != std::chrono::steady_clock::time_point{}; }

private:
	std::chrono::steady_clock::time_point start_;
};

// ============================================================
// QObject forward declaration
// ============================================================

class QObject {};

// ============================================================
// QMetaObject (minimal stub for dssbase.h)
// ============================================================

class QMetaObject
{
public:
	static void connectSlotsByName(QObject*) {}
};

// ============================================================
// Platform detection macros
// ============================================================

#if defined(_WIN32) || defined(_WIN64)
#define Q_OS_WIN
#elif defined(__linux__)
#define Q_OS_LINUX
#elif defined(__APPLE__)
#define Q_OS_MACOS
#define Q_OS_MAC
#endif

#if defined(__clang__)
#define Q_CC_CLANG
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define Q_PROCESSOR_X86_64
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define Q_PROCESSOR_ARM
#endif

// ============================================================
// Macros
// ============================================================

#define Q_DECLARE_TR_FUNCTIONS(x) \
	public: \
		static QString tr(const char* sourceText, const char* = nullptr, int = -1) { return QString(sourceText); }
#define _T(x) x

// QCoreApplication::translate is already defined above
