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

#include "../libraw/include/libraw_types.h"

class QString;
typedef double qreal;
typedef long long qint64;
typedef unsigned long long quint64;

// ============================================================
// Forward declarations (for types used before full definition)
// ============================================================
class QByteArray;

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
	QPointF operator*(qreal s) const { return QPointF(xp * s, yp * s); }
	QPointF operator/(qreal s) const { return QPointF(xp / s, yp / s); }
	QPointF operator+(const QPointF& o) const { return QPointF(xp + o.xp, yp + o.yp); }
	QPointF operator-(const QPointF& o) const { return QPointF(xp - o.xp, yp - o.yp); }
	QPointF& operator+=(const QPointF& o) { xp += o.xp; yp += o.yp; return *this; }
	QPointF& operator*=(qreal s) { xp *= s; yp *= s; return *this; }
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

// --- QStringList (must be before QString because split() returns it) ---
class QStringList : public std::vector<QString>
{
public:
	QStringList() = default;
	int count() const { return static_cast<int>(size()); }
	bool isEmpty() const { return empty(); }
};

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

	QString arg(int i) const { return QString(std::to_string(i)); }
	QString arg(double d, int w = 0, char f = 'g', int prec = 6) const {
		std::ostringstream oss;
		oss.precision(prec);
		oss << std::fixed << d;
		return QString(oss.str());
	}

	int indexOf(const char* s) const { return static_cast<int>(data_.find(s)); }
	int indexOf(char c) const { return static_cast<int>(data_.find(c)); }

	QString left(int n) const { return QString(data_.substr(0, n)); }
	QString right(int n) const { return QString(data_.substr(data_.size() > static_cast<size_t>(n) ? data_.size() - n : 0)); }
	QString mid(int pos, int n = -1) const { return QString(data_.substr(pos, n)); }

	QString trimmed() const {
		size_t start = data_.find_first_not_of(" \t\r\n");
		size_t end = data_.find_last_not_of(" \t\r\n");
		if (start == std::string::npos) return QString();
		return QString(data_.substr(start, end - start + 1));
	}

	QStringList split(const char* sep) const;
	QStringList split(const QString& sep) const;

	QByteArray toUtf8() const { return QByteArray(data_.c_str()); }
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

	bool operator==(const char* s) const { return data_ == s; }
	bool operator==(const QString& o) const { return data_ == o.data_; }
	bool operator!=(const QString& o) const { return data_ != o.data_; }
	bool operator<(const QString& o) const { return data_ < o.data_; }
	QString operator+(const char* s) const { return QString(data_ + s); }
	QString operator+(const QString& o) const { return QString(data_ + o.data_); }

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
		return QString(std::string(s.begin(), s.end()));
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

private:
	std::string data_;
};

// QString::split inline implementations
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

// ============================================================
// QVariant (depends on QString)
// ============================================================

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

	bool toBool() const { return type_ == Bool ? boolVal_ : (type_ == Int ? intVal_ != 0 : type_ == Double ? doubleVal_ != 0.0 : false); }
	int toInt() const { return type_ == Int ? intVal_ : (type_ == Double ? static_cast<int>(doubleVal_) : 0); }
	unsigned int toUInt() const { return type_ == Int ? uintVal_ : (type_ == Double ? static_cast<unsigned int>(doubleVal_) : 0); }
	double toDouble() const { return type_ == Double ? doubleVal_ : (type_ == Int ? static_cast<double>(intVal_) : 0.0); }
	QString toString() const { return type_ == String ? QString(strVal_) : QString(); }
	bool canConvert(Type) const { return true; }

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

	bool isValid() const { return date_.isValid(); }
	QString toString(const char* fmt = nullptr) const { return QString(); }

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

class QMimeDatabase
{
public:
	QMimeDatabase() = default;
};

class QSettings
{
public:
	QVariant value(const QString&, const QVariant& = QVariant{}) const { return QVariant{}; }
	void setValue(const QString&, const QVariant&) {}
};

// ============================================================
// Macros
// ============================================================

#define Q_DECLARE_TR_FUNCTIONS(x)
#define _T(x) x
