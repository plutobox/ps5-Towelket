/* pixman's build configuration for the PS5 (x86-64, FreeBSD-based system).
 * Written by hand in place of meson's: SSE2 and SSSE3 fast paths, no
 * thread-local storage, no MMX/AVX. */
#define PACKAGE "pixman"
#define PACKAGE_VERSION "0.43.4"
#define PIXMAN_NO_TLS 1
#define USE_SSE2 1
#define USE_SSSE3 1
#define HAVE_BUILTIN_CLZ 1
#define HAVE_GCC_VECTOR_EXTENSIONS 1
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_MPROTECT 1
#define HAVE_GETPAGESIZE 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_UNISTD_H 1
