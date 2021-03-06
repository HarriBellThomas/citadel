

#ifndef _LIBCITADEL_FILE_H
#define _LIBCITADEL_FILE_H

#include "citadel.h"

extern bool citadel_file_claim(const char *path, size_t length);
extern bool citadel_file_claim_force(const char *path, size_t length);
extern bool citadel_file_open(const char *path, size_t length);
extern bool citadel_file_open_ext(const char *path, size_t length, bool *from_cache);
extern void citadel_declare_fd(int fd, citadel_operation_t op);
extern bool citadel_declassify(const char *path, size_t length);

#endif