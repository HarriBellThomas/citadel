
#include "interface.h"

uint8_t asm_handle_request(int32_t pid, struct citadel_op_request *request, void *metadata) {
    // ocall_print("asm_handle_request");
    // if (metadata && request->operation == CITADEL_OP_OPEN) {
    //     enclave_printf("[ASM] PID %d wants to open file:", pid);
    //     print_hex((unsigned char*)metadata, _CITADEL_IDENTIFIER_LENGTH);
    // } 
    // else 
    {
        asm_printf("PID %d, operation 0x%02X", pid, request->operation);
        asm_hex(request->subject, sizeof(request->subject));
    }
    return CITADEL_OP_APPROVED;
}
