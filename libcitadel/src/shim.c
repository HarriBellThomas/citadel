#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/uio.h>
#include <sys/sendfile.h>

#include "../include/citadel/shim.h"
#include "../include/citadel/citadel.h"

pid_t c_fork(void) {
    pid_t res = fork();
    if (res == 0) {
        // Child process.
        if (!citadel_init())
            citadel_printf("[Shim] Citadel failed to init.\n");
    }
    return res;
}

int c_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    bool tainted = true;
    if (citadel_socket(sockfd, (struct sockaddr *)addr, &tainted)) {
        int res = bind(sockfd, addr, addrlen);
        if (tainted && res >= 0 && addr->sa_family == AF_UNIX) {
            struct sockaddr_un *local_addr = (struct sockaddr_un *)addr;
            if (!citadel_file_claim_force(local_addr->sun_path, strlen(local_addr->sun_path)+1)) {
                unlink(local_addr->sun_path);
                return -EPERM;
            }
        }
        return res;
    }
    return -EPERM;
}

int c_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    bool tainted = true;
    if (citadel_socket(sockfd, (struct sockaddr *)addr, &tainted)) {
        if (tainted && addr->sa_family == AF_UNIX) {
            struct sockaddr_un *local_addr = (struct sockaddr_un *)addr;
            if (!citadel_file_open(local_addr->sun_path, strlen(local_addr->sun_path)+1)) 
                return -EPERM;
        }
        return connect(sockfd, addr, addrlen);
    }
    return -EPERM;
}

int c_listen(int sockfd, int backlog) {
    if (citadel_validate_fd(sockfd, NULL, NULL, NULL, NULL))
        return listen(sockfd, backlog);
    return -EPERM;
}

int c_mkfifo(const char *pathname, mode_t mode) {
    int res = mkfifo(pathname, mode);
    if (res >= 0 && !citadel_file_claim_force(pathname, strlen(pathname)+1)) {
        unlink(pathname);
        return -EPERM;
    }
    return res;
}


int c_open(const char *pathname, int oflag, mode_t mode) {
    int fd;
    bool from_cache = false;
    citadel_info("open(%s)\n", pathname);

    bool creating = access(pathname, F_OK) == -1 && (oflag & O_CREAT) > 0;
    if (!am_tainted() || creating) {
        fd = open(pathname, oflag, mode);
        if (!am_tainted() && fd > 0) return fd;
        if (fd == -1 && (errno != EPERM && errno != EACCES)) return -1;
        if (fd != -1) close(fd);
    }

    errno = 0;
    if (!citadel_file_open_ext(pathname, strlen(pathname)+1, &from_cache))
        return -EPERM;

    fd = open(pathname, oflag, mode);
    citadel_declare_fd(fd, CITADEL_OP_OPEN);
    if (!am_tainted()) set_taint();
    citadel_info("End open(%s, %d, %d)\n", pathname, from_cache, am_tainted());
    return fd;
}

int c_close(int fd) {
    if (fd == -1) return -1;
    citadel_remove_fd(fd);
    return close(fd);
}

FILE *c_fopen(const char *pathname, const char *mode) {
    if (!citadel_file_open(pathname, strlen(pathname)+1))
        return (void*)(-EPERM);
    return fopen(pathname, mode);
}

int c_shmget(key_t key, size_t size, int shmflg) {
    if (!citadel_shm_access(key, false))
        return -EPERM;
    int shmid = shmget(key, size, shmflg);
    declare_shmid_from_key(key, shmid);
    return shmid;
}

void *c_shmat(int shmid, const void *shmaddr, int shmflg) {
    if (!citadel_shm_access(shmid, true))
        return (void*)(-EPERM);
    return shmat(shmid, shmaddr, shmflg);
}

int c_shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    if (!citadel_shm_access(shmid, true))
        return -EPERM;
    return shmctl(shmid, cmd, buf);
}

ssize_t c_read(int fildes, void *buf, size_t nbyte) {
    if (citadel_validate_fd_anon(fildes))
        return read(fildes, buf, nbyte);
    errno = EPERM;
    return -1;
}

ssize_t c_write(int fd, const void *buf, size_t count) {
    citadel_printf("write()\n");
    if (citadel_validate_fd_anon(fd))
        return write(fd, (void*)buf, count);
    errno = EPERM;
    return -1;
}

ssize_t c_pread(int fd, void *buf, size_t count, off_t offset) {
    if (citadel_validate_fd_anon(fd))
        return pread(fd, buf, count, offset);
    errno = EPERM;
    return -1;
}

ssize_t c_pwrite(int fd, const void *buf, size_t count, off_t offset) {
    if (citadel_validate_fd_anon(fd))
        return pwrite(fd, buf, count, offset);
    errno = EPERM;
    return -1;
}

ssize_t c_send(int socket, const void *buffer, size_t length, int flags) {
    citadel_printf("send()\n");
    if (citadel_validate_fd_anon(socket))
        return send(socket, buffer, length, flags);
    errno = EPERM;
    return -1;
}


ssize_t c_recv(int sockfd, void *buf, size_t len, int flags) {
    citadel_printf("recv()\n");
    if (citadel_validate_fd_anon(sockfd))
        return recv(sockfd, buf, len, flags);
    errno = EPERM;
    return -1;
}

ssize_t c_writev(int fd, const struct iovec *iov, int iovcnt) {
    if (citadel_validate_fd_anon(fd)) 
        return writev(fd, iov, iovcnt);
    errno = EPERM;
    return -1;
}

ssize_t c_sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    // printf("%d, %d\n", out_fd, in_fd);
    if (citadel_validate_fd_anon(out_fd) && citadel_validate_fd_anon(in_fd)) 
        return sendfile(out_fd, in_fd, offset, count);
    errno = EPERM;
    return -1;
}

int c_socketpair(int domain, int type, int protocol, int sv[2]) {
    int ret = socketpair(domain, type, protocol, sv);
    if (!ret) {
        // Transient fds, but still need to record them in the cache.
        citadel_declare_fd(sv[0], CITADEL_OP_NOP);
        citadel_declare_fd(sv[1], CITADEL_OP_NOP);
    }
    return ret;
}
// ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
// ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
// int open(const char *path, int oflag, .../*,mode_t mode */);
// int openat(int fd, const char *path, int oflag, ...);
// int creat(const char *path, mode_t mode);
// FILE *fopen(const char *restrict filename, const char *restrict mode);
// FILE *fopen(const char *pathname, const char *mode);

//        FILE *fdopen(int fd, const char *mode);

//        FILE *freopen(const char *pathname, const char *mode, FILE *stream);
