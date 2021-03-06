#include <linux/types.h>
#include <linux/xattr.h>
#include <linux/binfmts.h>
#include <linux/lsm_hooks.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/uidgid.h>
#include <linux/kobject.h>
#include <linux/crypto.h>
#include <linux/mutex.h>
#include <linux/dcache.h>

#include <linux/random.h>

#include "../../includes/citadel.h"
#include "../../includes/payload_io.h"
#include "../../includes/crypto.h"
#include "../../includes/ticket_cache.h"

static unsigned char current_challenge[_CITADEL_CHALLENGE_LENGTH];
static char registered_name[_CITADEL_ASM_NAME_LENGTH];
static char aes_key[_CITADEL_AES_KEY_LENGTH];
static char ptoken_aes_key[_CITADEL_AES_KEY_LENGTH];
static int registered = 0;
static int32_t enclave_pid = 0;

int system_ready() {
    return registered;
}

int citadel_pid() {
    return (int)enclave_pid;
}

void* generate_challenge(size_t *len) {
    citadel_challenge_t *challenge;
    char *encrypted, *challenge_hex; //, *hexstring, *hexstring2;
    int encrypted_len;

    challenge = kzalloc(sizeof(citadel_challenge_t), GFP_KERNEL);
    if (!challenge) return NULL;

    // Set the signature and challenge.
    memcpy(challenge->signature, challenge_signature, sizeof(challenge_signature));
    get_random_bytes(current_challenge, sizeof(current_challenge));
    memcpy(challenge->challenge, current_challenge, sizeof(current_challenge));
    memset(challenge->name, 0, sizeof(challenge->name));
    memset(challenge->key, 0, sizeof(challenge->key));
    memset(challenge->padding, 1, sizeof(challenge->padding));
    challenge->pid = (pid_t)0;

    challenge_hex = to_hexstring((unsigned char*)current_challenge, sizeof(current_challenge));
    printkc("Generated a new challenge.\n");
    kfree(challenge_hex);
    
    
    // hexstring = to_hexstring((unsigned char*)challenge, sizeof(citadel_challenge_t));
    // printkc("Data: %s\n", hexstring);
    // kfree(hexstring);

    // Encrypt.
    encrypted = trm_rsa_encrypt((char*)challenge, sizeof(citadel_challenge_t), &encrypted_len);
    kfree(challenge);
    
    // hexstring2 = to_hexstring((unsigned char*)encrypted, encrypted_len);
    // printkc("Encrypted: %s\n", hexstring2);
    // kfree(hexstring2);

    // if (!encrypted || encrypted_len <= 0) return NULL;
    
    *len = (size_t)encrypted_len;
    return encrypted;
}

void process_challenge_response(void *response, size_t response_len) {
    citadel_challenge_t *challenge;
    size_t decrypted_len;
    // char *hex;

    if(response_len != _CITADEL_RSA_KEY_LENGTH) {
        printkc("Rejected challenge response: invalid length (%ld)\n", response_len);
        goto bail;
    }

    challenge = (citadel_challenge_t*) trm_rsa_decrypt((char*)response, response_len, (int*)&decrypted_len);
    if (decrypted_len == 0 || !challenge) {
        printkc("Rejected challenge response: decryption failed.\n");
        goto bail;
    }
    // hex = to_hexstring((unsigned char*)challenge, sizeof(citadel_challenge_t));
    // printkc("Decrypted challenge: %s\n", hex);
    // kfree(hex);

    // Valid decrypted payload, check signature.
    if(memcmp(challenge->signature, challenge_signature, sizeof(challenge_signature))) {
        printkc("Rejected challenge response: signature incorrect.\n");
        goto bail;
    }

    // Valid decrypted payload, check signature.
    if(memcmp(challenge->challenge, current_challenge, sizeof(current_challenge))) {
        printkc("Rejected challenge response: challenge key incorrect.\n");
        goto bail;
    }

    // Challenge response successful.
    memcpy(registered_name, challenge->name, sizeof(registered_name));
    memcpy(aes_key, challenge->key, sizeof(aes_key));
    memcpy(ptoken_aes_key, challenge->key, sizeof(aes_key));
    enclave_pid = challenge->pid;

    printkc("Successfully registered with %s (%d)\n", registered_name, enclave_pid);
    registered = 1;

bail: 
    if (challenge) kfree(challenge);
    return;
}


void update_aes_key(void *key, size_t key_len) {
    // char *h;
    int i;
    if(key_len >= sizeof(aes_key)) {
        for(i = 0; i < sizeof(aes_key); i++) {
            aes_key[i] = aes_key[i] ^ ((unsigned char*)key)[i];
        }
    }
    // h = to_hexstring(aes_key, sizeof(aes_key));
    // printkc("** Updated AES key.\n");
    // printkc("%s\n", h);
    // printkc("**\n");
    // kfree(h);
}


void process_received_update(void *update, size_t update_len) {
    char *plain;
    size_t outlen, iter;
    bool success = true;
    int res;
    citadel_update_header_t *hdr;
    citadel_update_record_t *rcrd;

    if (!registered) {
        printkc("Can't process update. Not registered.\n");
        return;
    }

    if (update_len <= _CITADEL_IV_LENGTH + _CITADEL_TAG_LENGTH) {
        printkc("Invalid payload. Too short (%ld)\n", update_len);
        return;
    }
    
    plain = kzalloc(update_len, GFP_KERNEL);
    outlen = update_len;
    res = trm_aes_decrypt(aes_key, update, update_len, plain, &outlen);

    // TODO make more robust.

    hdr = (citadel_update_header_t*)plain;
    if(memcmp(hdr->signature, challenge_signature, sizeof(challenge_signature))) {
        printkc("Rejected updates. Signature mismatch.\n");
    }

    // printkc("Received %d records.\n", hdr->records);
    rcrd = (citadel_update_record_t *)(plain + sizeof(citadel_update_header_t));
    for (iter = 0; iter < hdr->records; iter++) {
        success = insert_ticket(rcrd);
        if (!success) printkc("Failed to install a ticket for PID %d\n", current->pid);
        rcrd = (citadel_update_record_t *)(rcrd + 1);
    }

    update_aes_key(hdr->key_update, sizeof(hdr->key_update));
    kfree(plain);
}


void* generate_update(size_t *len) {

    void *update;
    citadel_update_header_t *hdr;
    citadel_update_record_t *rcrd;
    int num_records;
    int tmp;
    char *cipher; //, *hex;
    size_t outlen;
    int res;
    size_t required_space;
    
    num_records = 5;
    required_space = sizeof(citadel_update_header_t) + num_records * sizeof(citadel_update_record_t);

    update = kzalloc(required_space, GFP_KERNEL);
    if (!update) return NULL;

    hdr = (citadel_update_header_t*)update;
    memcpy(hdr->signature, challenge_signature, sizeof(challenge_signature));
    memset(hdr->key_update, 6, sizeof(hdr->key_update));
    hdr->records = (uint8_t)num_records;

    rcrd = (citadel_update_record_t *)(update + sizeof(citadel_update_header_t));
    for(tmp = 1; tmp < 2*num_records; tmp += 2) {
        rcrd->pid = 13;
        rcrd->operation = 0;
        memset(rcrd->identifier, tmp, sizeof(rcrd->identifier));
        rcrd = (citadel_update_record_t *)(rcrd + 1);
    }
    
    if (!registered) {
        printkc("Can't generate update. Not registered.\n");
        *len = 0;
        return NULL;
    }

    // TODO verify size
    cipher = kzalloc(required_space + _CITADEL_TAG_LENGTH + _CITADEL_IV_LENGTH, GFP_KERNEL);
    outlen = required_space + _CITADEL_TAG_LENGTH + _CITADEL_IV_LENGTH;
    res = trm_aes_encrypt(aes_key, update, required_space, cipher, &outlen);

    // hex = to_hexstring(cipher, outlen);
    // printkc("Generated cipher: %s\n", hex);
    // kfree(hex);

    kfree(update);
    *len = outlen;
    return cipher;
}

int xattr_enclave_installation(const void *value, size_t size, struct dentry *dentry) {
    char *plain, *identifier_hex;
    size_t outlen;
    int res, xattr_success;
    citadel_update_header_t *hdr;
    citadel_update_record_t *rcrd;
    citadel_inode_data_t *d_inode_data = citadel_inode(dentry->d_inode);

    if (!registered) {
        printkc("Can't process update. Not registered.\n");
        return -1;
    }

    if (size <= _CITADEL_IV_LENGTH + _CITADEL_TAG_LENGTH) {
        printkc( "Invalid payload. Too short (%ld)\n", size);
        return -1;
    }
    
    plain = kzalloc(size, GFP_KERNEL);
    outlen = size;

    res = trm_aes_decrypt(aes_key, (void*)value, size, plain, &outlen);
    if (res) {
        printkc( "Rejected updates. Decryption failed.\n");
        return -1;
    }

    hdr = (citadel_update_header_t*)plain;
    if(memcmp(hdr->signature, challenge_signature, sizeof(challenge_signature))) {
        printkc( "Rejected updates. Signature mismatch.\n");
        return -1;
    }

    // Only expecting a single record.
    if (hdr->records != 1) return -1;
    rcrd = (citadel_update_record_t *)(plain + sizeof(citadel_update_header_t));

    if(rcrd->operation & CITADEL_OP_DECLASSIFY) {
        if (!d_inode_data->in_realm) {
            // Already unclassified.
            update_aes_key(hdr->key_update, sizeof(hdr->key_update));
            kfree(identifier_hex);
            kfree(plain);
            return 0;
        }

        // Set the xattr values.        
        d_inode_data->needs_xattr_update = false;
        d_inode_data->checked_disk_xattr = true;
        xattr_success = __vfs_removexattr(dentry, _CITADEL_XATTR_IN_REALM);
        if(xattr_success == 0) {
            // Update internal kernel structure.
            printkc("Declassified file. Identifier: %s\n", identifier_hex);
            d_inode_data->in_realm = false;

            update_aes_key(hdr->key_update, sizeof(hdr->key_update));
            kfree(identifier_hex);
            kfree(plain);
            return 0;
        } else {
            d_inode_data->checked_disk_xattr = false;
            kfree(identifier_hex);
            kfree(plain);
            return -1;
        }
    }
    else {
        // Set the xattr values.
        d_inode_data->needs_xattr_update = false;
        d_inode_data->checked_disk_xattr = true;
        identifier_hex = to_hexstring(rcrd->identifier, _CITADEL_IDENTIFIER_LENGTH);
        // need to lock inode->i_rwsem
        // down_write(&(dentry->d_inode->i_rwsem));
        xattr_success = __vfs_setxattr_noperm(dentry, _CITADEL_XATTR_IDENTIFIER, (const void*)identifier_hex, _CITADEL_IDENTIFIER_LENGTH * 2, 0);
        if (rcrd->operation & CITADEL_OP_REGISTER) __vfs_setxattr_noperm(dentry, _CITADEL_XATTR_IN_REALM, NULL, 0, 0);
        // up_write(&(dentry->d_inode->i_rwsem));

        if(xattr_success == 0) {
            // Update internal kernel structure.
            printkc("Inducted file. Identifier: %s\n", identifier_hex);
            d_inode_data->in_realm = (rcrd->operation & CITADEL_OP_REGISTER) > 0;
            memcpy(d_inode_data->identifier, rcrd->identifier, sizeof(d_inode_data->identifier));
            d_inode_data->anonymous = false;

            update_aes_key(hdr->key_update, sizeof(hdr->key_update));
            kfree(identifier_hex);
            kfree(plain);
            return 0;
        } else {
            d_inode_data->checked_disk_xattr = false;
            kfree(identifier_hex);
            kfree(plain);
            return -1;
        }
    }
}


void* generate_ptoken(size_t *len) {
    char *cipher, *hex, *id;
    size_t outlen;
    int res, required_size;
    citadel_task_data_t *task_data = citadel_cred(current_cred());
    citadel_ptoken_protected_t to_encrypt;
    citadel_ptoken_t *signed_ptoken;

    if (!system_ready()) {
        printkc( "Can't provide ptoken. Not registered.\n");
        return NULL;
    }

    signed_ptoken = kzalloc(sizeof(citadel_ptoken_t), GFP_KERNEL);
    memcpy(signed_ptoken->signature, challenge_signature, sizeof(challenge_signature));
    get_random_bytes(signed_ptoken->ptoken, _CITADEL_PROCESS_PTOKEN_LENGTH);
    signed_ptoken->citadel_pid = (int32_t)enclave_pid;
    memcpy(signed_ptoken->process_identifier, task_data->identifier, sizeof(signed_ptoken->process_identifier));

    // Generate cipher payload.
    memcpy(to_encrypt.signature, challenge_signature, sizeof(challenge_signature));
    to_encrypt.pid = (int32_t)current->pid;
    memcpy(to_encrypt.ptoken, signed_ptoken->ptoken, _CITADEL_PROCESS_PTOKEN_LENGTH);
    memcpy(to_encrypt.process_identifier, task_data->identifier, sizeof(signed_ptoken->process_identifier));

    required_size = sizeof(citadel_ptoken_protected_t) + _CITADEL_TAG_LENGTH + _CITADEL_IV_LENGTH;
    cipher = kzalloc(required_size, GFP_KERNEL);
    outlen = required_size;
    res = trm_aes_encrypt(ptoken_aes_key, &to_encrypt, sizeof(citadel_ptoken_protected_t), cipher, &outlen);
    if (res) {
        kfree(cipher);
        *len = 0;
        return NULL;
    }

    memcpy(signed_ptoken->signed_ptoken, cipher, outlen);
    kfree(cipher);

    hex = to_hexstring(signed_ptoken->ptoken, _CITADEL_PROCESS_PTOKEN_LENGTH);
    id = to_hexstring(signed_ptoken->process_identifier, _CITADEL_IDENTIFIER_LENGTH);
    printkc("Generated ptoken for PID %d: %s, %s\n", current->pid, hex, id);
    kfree(hex);
    kfree(id);

    *len = sizeof(citadel_ptoken_t);
    return signed_ptoken;
}