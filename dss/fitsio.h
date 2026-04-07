#pragma once
// fitsio.h - minimal stub for CFITSIO library
// Just enough to compile without the actual library

typedef void fitsfile;

// Type constants used in code
#define TBYTE      8
#define TSHORT    21
#define TUSHORT   22
#define TINT      31
#define TUINT     32
#define TLONG     41
#define TULONG    42
#define TFLOAT    42
#define TDOUBLE   82
#define TSTRING   16

#define READONLY   0
#define READWRITE  1

// Stub functions
inline int ffopen(fitsfile**, const char*, int, int*) { return 0; }
inline int ffclos(fitsfile*, int*) { return 0; }
inline int ffgisz(fitsfile*, int, long*, int*) { return 0; }
inline int ffgkys(fitsfile*, const char*, char*, char*, int*) { return 0; }
inline int ffgkyd(fitsfile*, const char*, double*, char*, int*) { return 0; }
inline int ffgkyj(fitsfile*, const char*, long*, char*, int*) { return 0; }
inline int ffgkyl(fitsfile*, const char*, int*, char*, int*) { return 0; }
