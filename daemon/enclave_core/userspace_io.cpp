
#include "includes/userspace_io.h"

static unsigned char ptoken_aes_key[16] = {"\0"};

void set_ptoken_aes_key(unsigned char* key) {
    memcpy(ptoken_aes_key, key, sizeof(ptoken_aes_key));
}


static citadel_response_t core_handle_request(int32_t pid, struct citadel_op_request *request, void *metadata, bool translated, bool translate_success, citadel_response_t asm_result) {
    if (asm_result != CITADEL_OP_APPROVED) return asm_result;

    void *identifier;
    citadel_operation_t op = request->operation;
    citadel_response_t result = asm_result;

    if (op & CITADEL_OP_CLAIM) {
        // Create file
        enclave_printf("[#%d] File create.", pid);
        char id[_CITADEL_IDENTIFIER_LENGTH];
        if (!generate_xattr_ticket((const char*)metadata, (char*)&id, true, false))
            result = CITADEL_OP_ERROR;

        // Also claim access for free.
        if (result != CITADEL_OP_ERROR && (op & CITADEL_OP_OPEN)) {
            enclave_printf("[#%d] File open (post-claim).", pid);
            if (!generate_ticket(pid, (const char*)&id, CITADEL_OP_OPEN))
                result = CITADEL_OP_ERROR;
        }
        
        op &= ~CITADEL_OP_OPEN;
    }
    
    if (op & CITADEL_OP_OPEN) {
        // Open file
        enclave_printf("[#%d] File open.", pid);

        // TODO work around this.
        char id[_CITADEL_IDENTIFIER_LENGTH];
        if (translated && !translate_success) {
            enclave_printf("Inducting new file from open: %s", (const char*)metadata);
            if (!generate_xattr_ticket((const char*)metadata, (char*)&id, true, false))
                result = CITADEL_OP_ERROR;
            identifier = (void*)&id;
        }
        else {
            identifier = (translated ? metadata : request->subject);
        }
        if (result != CITADEL_OP_ERROR && !generate_ticket(pid, (const char*)identifier, request->operation))
            result = CITADEL_OP_ERROR;
    }

    if (op & (CITADEL_OP_PTY_ACCESS | CITADEL_OP_SOCKET)) {
        // Install ticket
        enclave_printf("[#%d] PTY or socket.", pid);
        if (!generate_ticket(pid, (const char*)request->subject, request->operation))
            result = CITADEL_OP_ERROR;
    }

    if (op & CITADEL_OP_SHM) {
        enclave_printf("[#%d] SHM access granted.", pid);
        if (!generate_ticket(pid, (const char*)request->subject, request->operation))
            result = CITADEL_OP_ERROR;
    }

    if (op & CITADEL_OP_REGISTER) {
        // Register
        enclave_printf("[#%d] Registered.", pid);
    }

    if (op & CITADEL_OP_DECLASSIFY) {
        // Create file
        enclave_printf("[#%d] File declassified.", pid);
        if (!generate_xattr_ticket((const char*)metadata, NULL, false, true))
            result = CITADEL_OP_ERROR;
    }

    return result;
}


uint8_t handle_request(uint8_t* data, size_t length, int32_t pid, uint8_t* ptoken, size_t ptoken_length) {
    
    // Before starting, check we have a valid ptoken output buffer.
    if (!ptoken || ptoken_length != _CITADEL_PROCESS_PTOKEN_LENGTH) {
        enclave_perror("Output ptoken buffer too small.");
        return CITADEL_OP_ERROR;
    }

    // First, check that the payload size is correct.
    struct citadel_op_request *request;
    struct citadel_op_extended_request *extended_request = NULL;
    if (length == sizeof(struct citadel_op_request)) {
        request = (struct citadel_op_request*)data;
    }
    else if (length == sizeof(struct citadel_op_extended_request)) {
        extended_request = (struct citadel_op_extended_request*)data;
        request = &extended_request->request;
    }
    else {
        enclave_perror("Invalid request size.");
        return CITADEL_OP_INVALID;
    }

    // Next, check that the signature matches.
    if(memcmp(request->signature, challenge_signature, sizeof(challenge_signature))) {
        enclave_perror("Invalid signature");
        return CITADEL_OP_INVALID;
    }


    // Then, try to decrypt the ptoken.
    size_t signed_payload_len = sizeof(citadel_ptoken_protected_t) + _CITADEL_IV_LENGTH + _CITADEL_TAG_LENGTH;
    unsigned char decrypted[signed_payload_len];
    size_t outlen = signed_payload_len;

    int aes_ret = aes_decrypt((unsigned char*)request->signed_ptoken, signed_payload_len, decrypted, &outlen, ptoken_aes_key, _CITADEL_AES_KEY_LENGTH);
    if(aes_ret) {
        enclave_perror("Failed to decrypt ptoken.");
        return CITADEL_OP_INVALID;
    }

    citadel_ptoken_protected_t *ptoken_payload = (citadel_ptoken_protected_t *)decrypted;

    // Check that the decrypted ptoken has the right size and signature.
    if(outlen != sizeof(citadel_ptoken_protected_t)) {
        enclave_perror("Invalid ptoken payload size.");
        return CITADEL_OP_INVALID;
    }

    if(memcmp(ptoken_payload->signature, challenge_signature, sizeof(challenge_signature))) {
        enclave_perror("Invalid ptoken signature.");
        return CITADEL_OP_INVALID;
    }

    // Check the PID reported by the IPC medium and signed in the payload.
    if (pid != ptoken_payload->pid) {
        enclave_perror("Mismatching PIDs --- forged request.");
        return CITADEL_OP_FORGED;
    }
    
    // char buffer[100];
    // int cx;
    // cx = snprintf(buffer, sizeof(buffer), "* Verified PID: %d", ptoken->pid);
    // ocall_print(buffer);
    // generate_xattr_ticket();
    // generate_ticket(1);

    memcpy(ptoken, ptoken_payload->ptoken, ptoken_length);

    void *metadata = NULL;
    if(extended_request) metadata = extended_request->metadata;
    uint8_t result = asm_handle_request(pid, request, metadata);

    // Install tickets if required.
    uint8_t internal_update = core_handle_request(pid, request, metadata, (extended_request ? extended_request->translate : false), (extended_request ? extended_request->translate_success : false), result);

    return internal_update;
}


void protect_socket(void) {
    enclave_printf("Protecting %s", _CITADEL_IPC_FILE);
    generate_xattr_ticket_internal(_CITADEL_IPC_FILE);
}