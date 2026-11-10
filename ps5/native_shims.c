/* native_shims.c - C library functions for the native PS5 title that
 * neither the console nor mihawk-99's platform layer provide.
 *
 * package-native.sh binds libc's names to these easyrpg_ps5_ functions with
 * --defsym, as it binds the platform layer's ps5_ functions: a title must
 * not define libc's own names. */

#include <sys/stat.h>

/* lstat() fails for a title. A title has no symbolic links, so stat()
 * answers the same. */
int easyrpg_ps5_lstat(const char *restrict path, struct stat *restrict sb)
{
    return stat(path, sb);
}

/* harfbuzz switches to the C locale around strtod with uselocale(); a title
 * has one locale, so there is nothing to switch. */
void *easyrpg_ps5_uselocale(void *locale)
{
    (void) locale;
    return (void *) 0;
}

/* pixman's other-architecture detection, which is never called on x86 but
 * is referenced by the generic implementation chooser: no fast paths. */
void *_pixman_arm_get_implementations(void *fallback) { return fallback; }
void *_pixman_ppc_get_implementations(void *fallback) { return fallback; }
void *_pixman_mips_get_implementations(void *fallback) { return fallback; }

/* libiconv's wide-character loops ask for MB_CUR_MAX, which FreeBSD's libc
 * spells ___mb_cur_max(); a title is not given it. The code pages used
 * here never reach those loops, so the C locale's 1 is right. */
int easyrpg_ps5____mb_cur_max(void)
{
    return 1;
}
