
#ifndef _ASM_INTERFACE
#define _ASM_INTERFACE

#include "../Enclave_t.h"
#include "../../_citadel_shared.h"
#include "../includes/enclave.h"

extern uint8_t asm_handle_request(int32_t pid, struct citadel_op_request *request, void *metadata);

#endif  /* _ASM_INTERFACE */

