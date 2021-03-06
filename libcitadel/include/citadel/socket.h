#ifndef _LIBCITADEL_SOCKET_H
#define _LIBCITADEL_SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>

#include "citadel.h"
#include "cache.h"

extern bool citadel_socket(int socket_fd, struct sockaddr *address, bool *tainted);

#endif