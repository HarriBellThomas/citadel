

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _LIBCITADEL_SHIM_H
#define _LIBCITADEL_SHIM_H

#include <stdio.h>

extern pid_t c_fork(void);
extern int c_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern int c_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern int c_listen(int sockfd, int backlog);
extern int c_mkfifo(const char *pathname, mode_t mode);
extern int c_open(const char* pathname, int oflag);
extern FILE *c_fopen(const char *pathname, const char *mode);
extern int c_shmget(key_t key, size_t size, int shmflg);
extern void *c_shmat(int shmid, const void *shmaddr, int shmflg);
extern int c_shmctl(int shmid, int cmd, struct shmid_ds *buf);
extern ssize_t c_read(int fildes, void *buf, size_t nbyte);
extern ssize_t c_write(int fd, const void *buf, size_t count);

#endif

#ifdef __cplusplus
}
#endif