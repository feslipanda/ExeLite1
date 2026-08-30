#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

static const char* FAKE_TMP = "/data/user/0/com.exelite.launcher/files/engine_v2/tmp";

static const char* rewrite_path(const char* path, char* buf) {
    if (!path) return path;
    if (strncmp(path, "/tmp", 4) == 0) {
        if (path[4] == '\0') {
            return FAKE_TMP;
        } else if (path[4] == '/') {
            sprintf(buf, "%s%s", FAKE_TMP, path + 4);
            return buf;
        }
    }
    return path;
}

int mkdir(const char *path, mode_t mode) {
    static int (*real_mkdir)(const char*, mode_t) = NULL;
    if (!real_mkdir) real_mkdir = dlsym(RTLD_NEXT, "mkdir");
    char buf[1024];
    return real_mkdir(rewrite_path(path, buf), mode);
}

int mkdirat(int dirfd, const char *path, mode_t mode) {
    static int (*real_mkdirat)(int, const char*, mode_t) = NULL;
    if (!real_mkdirat) real_mkdirat = dlsym(RTLD_NEXT, "mkdirat");
    char buf[1024];
    return real_mkdirat(dirfd, rewrite_path(path, buf), mode);
}

int lstat(const char *path, struct stat *buf_stat) {
    static int (*real_lstat)(const char*, struct stat*) = NULL;
    if (!real_lstat) real_lstat = dlsym(RTLD_NEXT, "lstat");
    char buf[1024];
    return real_lstat(rewrite_path(path, buf), buf_stat);
}

int stat(const char *path, struct stat *buf_stat) {
    static int (*real_stat)(const char*, struct stat*) = NULL;
    if (!real_stat) real_stat = dlsym(RTLD_NEXT, "stat");
    char buf[1024];
    return real_stat(rewrite_path(path, buf), buf_stat);
}

int unlink(const char *path) {
    static int (*real_unlink)(const char*) = NULL;
    if (!real_unlink) real_unlink = dlsym(RTLD_NEXT, "unlink");
    char buf[1024];
    return real_unlink(rewrite_path(path, buf));
}

int rmdir(const char *path) {
    static int (*real_rmdir)(const char*) = NULL;
    if (!real_rmdir) real_rmdir = dlsym(RTLD_NEXT, "rmdir");
    char buf[1024];
    return real_rmdir(rewrite_path(path, buf));
}

int access(const char *path, int mode) {
    static int (*real_access)(const char*, int) = NULL;
    if (!real_access) real_access = dlsym(RTLD_NEXT, "access");
    char buf[1024];
    return real_access(rewrite_path(path, buf), mode);
}

int open(const char *path, int flags, ...) {
    static int (*real_open)(const char*, int, ...) = NULL;
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    char buf[1024];
    const char *new_path = rewrite_path(path, buf);
    
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode_t mode = va_arg(args, mode_t);
        va_end(args);
        return real_open(new_path, flags, mode);
    }
    return real_open(new_path, flags);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    static int (*real_bind)(int, const struct sockaddr*, socklen_t) = NULL;
    if (!real_bind) real_bind = dlsym(RTLD_NEXT, "bind");
    
    if (addr && addr->sa_family == AF_UNIX) {
        struct sockaddr_un *un = (struct sockaddr_un *)addr;
        char buf[1024];
        const char *new_path = rewrite_path(un->sun_path, buf);
        if (new_path != un->sun_path) {
            struct sockaddr_un new_un;
            memset(&new_un, 0, sizeof(new_un));
            new_un.sun_family = AF_UNIX;
            strncpy(new_un.sun_path, new_path, sizeof(new_un.sun_path) - 1);
            return real_bind(sockfd, (struct sockaddr *)&new_un, sizeof(new_un));
        }
    }
    return real_bind(sockfd, addr, addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    static int (*real_connect)(int, const struct sockaddr*, socklen_t) = NULL;
    if (!real_connect) real_connect = dlsym(RTLD_NEXT, "connect");
    
    if (addr && addr->sa_family == AF_UNIX) {
        struct sockaddr_un *un = (struct sockaddr_un *)addr;
        char buf[1024];
        const char *new_path = rewrite_path(un->sun_path, buf);
        if (new_path != un->sun_path) {
            struct sockaddr_un new_un;
            memset(&new_un, 0, sizeof(new_un));
            new_un.sun_family = AF_UNIX;
            strncpy(new_un.sun_path, new_path, sizeof(new_un.sun_path) - 1);
            return real_connect(sockfd, (struct sockaddr *)&new_un, sizeof(new_un));
        }
    }
    return real_connect(sockfd, addr, addrlen);
}
