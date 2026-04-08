// stubs.cpp - Stub implementations for excluded source files
// Provides no-op/empty implementations for functions from:
//   FITSUtil.cpp, TIFFUtil.cpp, BitmapInfo.cpp, BackgroundCalibration.cpp,
//   AvxImageFilter, AvxCfaProcessing, etc.

#include "pch.h"

#include "FITSUtil.h"
#include "TIFFUtil.h"
#include "BitmapExt.h"
#include "MemoryBitmap.h"
#include "GrayBitmap.h"
#include "Multitask.h"
#include "BilinearParameters.h"
#include "BackgroundCalibration.h"
#include "avx_simd_check.h"
#include "BitMapFiller.h"
#include "RegisterEngine.h"
#include "MultiBitmap.h"
#include "MedianFilterEngine.h"

// ---- FITSUtil stubs ----

CFITSHeader::CFITSHeader() {}
CFITSHeader::~CFITSHeader() {}
bool CFITSReader::Open() { return false; }
bool CFITSReader::Read() { return false; }
bool CFITSReader::Close() { return true; }
bool CFITSWriter::Open() { return false; }
bool CFITSWriter::Write() { return false; }
bool CFITSWriter::Close() { return true; }
void CFITSWriter::SetFormat(int, int, FITSFORMAT, CFATYPE) {}

CFATYPE GetFITSCFATYPE() { return CFATYPE_NONE; }
bool GetFITSInfo(const fs::path&, CBitmapInfo&) { return false; }
bool ReadFITS(const fs::path&, std::shared_ptr<CMemoryBitmap>&, DSS::OldProgressBase*) { return false; }
bool WriteFITS(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, FITSFORMAT, const QString&, int, int, double) { return false; }
bool WriteFITS(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, FITSFORMAT, const QString&) { return false; }
bool WriteFITS(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, FITSFORMAT) { return false; }
bool WriteFITS(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, const QString&, int, int, double) { return false; }
bool WriteFITS(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, const QString&) { return false; }
bool WriteFITS(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*) { return false; }
bool IsFITSPicture(const fs::path&, CBitmapInfo&) { return false; }
int  LoadFITSPicture(const fs::path&, CBitmapInfo&, std::shared_ptr<CMemoryBitmap>&, DSS::OldProgressBase*) { return -1; }
bool IsFITSRawBayer() { return false; }
bool IsFITSSuperPixels() { return false; }
bool IsFITSBilinear() { return false; }
bool IsFITSAHD() { return true; }
double GetFITSBrightnessRatio() { return 1.0; }
void GetFITSRatio(double& r, double& g, double& b) { r = g = b = 1.0; }

// ---- TIFFUtil stubs ----

bool IsTIFFPicture(const fs::path&, CBitmapInfo&) { return false; }
int  LoadTIFFPicture(const fs::path&, CBitmapInfo&, std::shared_ptr<CMemoryBitmap>&, DSS::OldProgressBase*) { return -1; }
bool ReadTIFF(const fs::path&, std::shared_ptr<CMemoryBitmap>&, DSS::OldProgressBase*) { return false; }
bool WriteTIFF(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, const QString&, int, int, double, double) { return false; }
bool WriteTIFF(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*) { return false; }
bool WriteTIFF(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, const QString&) { return false; }
bool WriteTIFF(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, TIFFFORMAT, TIFFCOMPRESSION, const QString&, int, int, double, double) { return false; }
bool WriteTIFF(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, TIFFFORMAT, TIFFCOMPRESSION, const QString&) { return false; }
bool WriteTIFF(const fs::path&, CMemoryBitmap*, DSS::OldProgressBase*, TIFFFORMAT, TIFFCOMPRESSION) { return false; }

// ---- BitmapInfo / EXIF stubs ----

bool RetrieveEXIFInfo(const fs::path&, CBitmapInfo&) { return false; }

// ---- BackgroundCalibration stubs ----

BcRT RationalModel::calibrate(double r, double g, double b) const { return {r, g, b}; }
void RationalParams::initialize(double, double, double, double, double, double) {}

template<>
double BackgroundCalibrator::calculateModelParameters(CMemoryBitmap const&, bool, const char8_t*) { return 0.0; }

BackgroundCalibrator makeBackgroundCalibrator(BACKGROUNDCALIBRATIONINTERPOLATION, BACKGROUNDCALIBRATIONMODE, RGBBACKGROUNDCALIBRATIONMETHOD, double) {
    return BackgroundCalibrator{NoneModel{}, 1.0, Mode::PerChannel, RgbMethod::Median};
}

// ---- CMultiBitmap stubs ----

void CMultiBitmap::removeTempFiles() {}
void CMultiBitmap::SetBitmapModel(const CMemoryBitmap*) {}

// ---- BitmapFiller stubs ----

std::unique_ptr<BitmapFillerInterface> BitmapFillerInterface::makeBitmapFiller(CMemoryBitmap*, DSS::OldProgressBase*) {
    return nullptr;
}

// ---- DSS::registerSubRect stub ----

size_t DSS::registerSubRect(const CGrayBitmap&, double, const DSSRect&, STARSET&, std::pair<double, double>*, const QPointF&) {
    return 0;
}

// ---- AVX stubs ----
// We provide stubs for the AVX template classes without including the AVX headers.
// The avx_filter.h, avx_cfa.h, avx_luminance.h define the class interfaces.
// We include them indirectly through pch.h -> MedianFilterEngine.h -> avx_filter.h
// But avx_cfa.h and avx_luminance.h may need simde. Let's just declare the needed signatures.

// Forward-declare the AvxImageFilter template (already declared in avx_filter.h)
#include "avx_filter.h"

template<typename T>
AvxImageFilter<T>::AvxImageFilter(CInternalMedianFilterEngineT<T>*) {}

template<typename T>
int AvxImageFilter<T>::filter(const size_t, const size_t) { return 1; }

template class AvxImageFilter<unsigned char>;
template class AvxImageFilter<unsigned short>;
template class AvxImageFilter<unsigned int>;
template class AvxImageFilter<float>;
template class AvxImageFilter<double>;

// AvxCfaProcessing and AvxLuminance stubs - include full headers (simde available via pch.h)
#include "avx_cfa.h"
#include "avx_luminance.h"

AvxCfaProcessing::AvxCfaProcessing(const size_t, const size_t, const CMemoryBitmap& inputbm)
    : redPixels(), greenPixels(), bluePixels(), inputBitmap(inputbm), vectorsPerLine(0), avxEnabled(false) {}
void AvxCfaProcessing::init(const size_t, const size_t) {}
int  AvxCfaProcessing::interpolate(const size_t, const size_t, const int) { return 1; }

AvxLuminance::AvxLuminance(const CMemoryBitmap& inputbm, CMemoryBitmap& outbm)
    : inputBitmap(inputbm), outputBitmap(outbm), avxEnabled(false) {}

// ---- CMultiBitmap additional stubs ----

bool CMultiBitmap::AddBitmap(CMemoryBitmap*, DSS::OldProgressBase*) { return false; }
std::shared_ptr<CMemoryBitmap> CMultiBitmap::GetResult(DSS::OldProgressBase*) { return nullptr; }
