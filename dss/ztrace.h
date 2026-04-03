#pragma once
// ztrace stub macros - no-op implementations

#define ZFUNCTRACE_RUNTIME()
#define ZTRACE_RUNTIME(...)
#define ZASSERTSTATE(x) do { if (!(x)) { } } while(0)
#define ZASSERT(x) do { if (!(x)) { } } while(0)
#define ZEXCEPTION_LOCATION() ""
