/*
 * pa_path_shim.c — LD_PRELOAD shim to run an extracted PulseAudio 17.0
 * from ~/opt without root access.
 *
 * PulseAudio has compiled-in absolute paths (module dir /etc/pulse ...).
 * This shim rewrites any path under the compiled-in prefixes to the
 * matching locations under $PA_SHIM_PREFIX (default /home/z/opt/usr).
 *
 * Build: gcc -shared -fPIC -o pa_path_shim.so pa_path_shim.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* OLD1 = "/usr/lib/pulse-17.0+dfsg1";
static const char* NEW1 = "/home/z/opt/usr/lib/pulse-17.0+dfsg1";
static const char* OLD2 = "/etc/pulse";
static const char* NEW2 = "/home/z/opt/etc/pulse";

/* returns malloc'ed rewritten path, or NULL when no rewrite needed */
static char* rewrite(const char* path)
{
    if (!path)
        return NULL;
    const char* nw = NULL;
    if (strncmp(path, OLD1, strlen(OLD1)) == 0)
        nw = NEW1;
    else if (strncmp(path, OLD2, strlen(OLD2)) == 0)
        nw = NEW2;
    if (!nw)
        return NULL;
    const char* old = nw == NEW1 ? OLD1 : OLD2;
    char out[1024];
    snprintf(out, sizeof(out), "%s%s", nw, path + strlen(old));
    return strdup(out);
}

/* ---------------- dlopen ---------------- */
static void* (*real_dlopen)(const char*, int);
void* dlopen(const char* file, int flags)
{
    if (!real_dlopen)
        real_dlopen = dlsym(RTLD_NEXT, "dlopen");
    char* rw = rewrite(file);
    void* r = real_dlopen(rw ? rw : file, flags);
    if (rw) {
        if (!r)
            fprintf(stderr, "[pa_shim] dlopen failed: %s (%s)\n", rw, dlerror());
        free(rw);
    }
    return r;
}

/* ---------------- open family ---------------- */
static int (*real_open)(const char*, int, ...);
int open(const char* path, int flags, ...)
{
    if (!real_open)
        real_open = dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    char* rw = rewrite(path);
    int r = (flags & (O_CREAT | O_TMPFILE))
                ? real_open(rw ? rw : path, flags, mode)
                : real_open(rw ? rw : path, flags);
    if (rw)
        free(rw);
    return r;
}

static int (*real_open64)(const char*, int, ...);
int open64(const char* path, int flags, ...)
{
    if (!real_open64)
        real_open64 = dlsym(RTLD_NEXT, "open64");
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    char* rw = rewrite(path);
    int r = (flags & (O_CREAT | O_TMPFILE))
                ? real_open64(rw ? rw : path, flags, mode)
                : real_open64(rw ? rw : path, flags);
    if (rw)
        free(rw);
    return r;
}

/* ---------------- stat family ---------------- */
static int (*real_stat)(const char*, void*);
int stat(const char* path, void* buf)
{
    if (!real_stat)
        real_stat = dlsym(RTLD_NEXT, "stat");
    char* rw = rewrite(path);
    int r = real_stat(rw ? rw : path, buf);
    if (rw)
        free(rw);
    return r;
}

static int (*real_stat64)(const char*, void*);
int stat64(const char* path, void* buf)
{
    if (!real_stat64)
        real_stat64 = dlsym(RTLD_NEXT, "stat64");
    char* rw = rewrite(path);
    int r = real_stat64(rw ? rw : path, buf);
    if (rw)
        free(rw);
    return r;
}

static int (*real_access)(const char*, int);
int access(const char* path, int mode)
{
    if (!real_access)
        real_access = dlsym(RTLD_NEXT, "access");
    char* rw = rewrite(path);
    int r = real_access(rw ? rw : path, mode);
    if (rw)
        free(rw);
    return r;
}

static FILE* (*real_fopen)(const char*, const char*);
FILE* fopen(const char* path, const char* m)
{
    if (!real_fopen)
        real_fopen = dlsym(RTLD_NEXT, "fopen");
    char* rw = rewrite(path);
    FILE* r = real_fopen(rw ? rw : path, m);
    if (rw)
        free(rw);
    return r;
}

static int (*real_mkdir)(const char*, unsigned);
int mkdir(const char* path, unsigned mode)
{
    if (!real_mkdir)
        real_mkdir = dlsym(RTLD_NEXT, "mkdir");
    char* rw = rewrite(path);
    int r = real_mkdir(rw ? rw : path, mode);
    if (rw)
        free(rw);
    return r;
}
