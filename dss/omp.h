#pragma once
// omp.h - stub for OpenMP runtime functions
// Provides empty implementations of OpenMP functions

inline int omp_get_num_procs() { return 1; }
inline int omp_get_num_threads() { return 1; }
inline int omp_get_thread_num() { return 0; }
inline void omp_set_num_threads(int) {}

#define omp_get_max_threads() 1

// OpenMP pragmas will be ignored by the compiler
#ifndef _OPENMP
#define _OPENMP 201511
#endif
