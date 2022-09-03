
#if defined(_M_IX86) || defined(_M_X64)
#define HAVE_SSE4_1 1
#else
#undef HAVE_SSE4_1
#define HAVE_NEON 1
#endif


