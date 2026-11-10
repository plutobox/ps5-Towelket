/* native_shims.c - C library functions for the native PS5 title that
 * neither the console nor mihawk-99's platform layer provide.
 *
 * package-native.sh binds libc's names to these easyrpg_ps5_ functions with
 * --defsym, as it binds the platform layer's ps5_ functions: a title must
 * not define libc's own names. */

#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

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

/* curl and OpenSSL use these; the console's libc has none of them. */

/* pipe2() is pipe() with flags applied to both ends. */
int easyrpg_ps5_pipe2(int fds[2], int flags)
{
    int i;

    if (pipe(fds) != 0)
        return -1;
    for (i = 0; i < 2; i++) {
        if (flags & O_NONBLOCK)
            fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
        if (flags & O_CLOEXEC)
            fcntl(fds[i], F_SETFD, FD_CLOEXEC);
    }
    return 0;
}

/* sendmmsg() and recvmmsg() as loops of sendmsg() and recvmsg(): the
 * messages are handled one at a time, which is all they promise at least. */
int easyrpg_ps5_sendmmsg(int s, struct mmsghdr *msgvec, unsigned int vlen, int flags)
{
    unsigned int i;

    for (i = 0; i < vlen; i++) {
        ssize_t n = sendmsg(s, &msgvec[i].msg_hdr, flags);

        if (n < 0)
            return i ? (int) i : -1;
        msgvec[i].msg_len = (unsigned int) n;
    }
    return (int) vlen;
}

int easyrpg_ps5_recvmmsg(int s, struct mmsghdr *msgvec, unsigned int vlen, int flags,
                         const struct timespec *timeout)
{
    unsigned int i;

    (void) timeout;
    for (i = 0; i < vlen; i++) {
        ssize_t n = recvmsg(s, &msgvec[i].msg_hdr, i ? flags | MSG_DONTWAIT : flags);

        if (n < 0)
            return i ? (int) i : -1;
        msgvec[i].msg_len = (unsigned int) n;
    }
    return (int) vlen;
}

/* libzstd's optional tracing hooks, left undefined (weak) in the library; the
 * import check wants every referenced symbol defined. Returning 0 from
 * *_begin switches tracing off. */
unsigned long long ZSTD_trace_compress_begin(const void *cctx)
{
    (void) cctx;
    return 0;
}
void ZSTD_trace_compress_end(unsigned long long ctx, const void *trace)
{
    (void) ctx, (void) trace;
}
unsigned long long ZSTD_trace_decompress_begin(const void *dctx)
{
    (void) dctx;
    return 0;
}
void ZSTD_trace_decompress_end(unsigned long long ctx, const void *trace)
{
    (void) ctx, (void) trace;
}
