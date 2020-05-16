
#include "../includes/common.h"

/* LSM's BLOB allocation. */
struct lsm_blob_sizes trm_blob_sizes __lsm_ro_after_init = {
	.lbs_cred = sizeof(struct task_trm),
	.lbs_file = 0, //sizeof(struct smack_known *),
	.lbs_inode = sizeof(struct inode_trm),
	.lbs_ipc = 0, //sizeof(struct smack_known *),
	.lbs_msg_msg = 0 //sizeof(struct smack_known *),
};


char* to_hexstring(unsigned char *buf, unsigned int len) {
    char   *out;
	size_t  i;

    if (buf == NULL || len == 0) return NULL;

    out = kmalloc(2*len+1, GFP_KERNEL);
    for (i=0; i<len; i++) {
		out[i*2]   = "0123456789ABCDEF"[buf[i] >> 4];
		out[i*2+1] = "0123456789ABCDEF"[buf[i] & 0x0F];
	}
    out[len*2] = '\0';
    return out;
}



// void find_dentry_root(struct dentry *dentry) {
// 	printk(PFX "Finding dentry root...\n");
// 	struct dentry *current_dentry = dentry;
// 	printk(PFX "%s\n", current_dentry->d_name->name);
// 	while(current_dentry->d_parent != NULL) {
// 		current_dentry = current_dentry->d_parent;
// 		printk(PFX "%s\n", current_dentry->d_name->name);
// 	}
// }

static char *tomoyo_get_dentry_path(struct dentry *dentry, char * const buffer,
				    const int buflen)
{
	char *pos = ERR_PTR(-ENOMEM);

	if (buflen >= 256) {
		pos = dentry_path_raw(dentry, buffer, buflen - 1);
		if (!IS_ERR(pos) && *pos == '/' && pos[1]) {
			struct inode *inode = d_backing_inode(dentry);

			if (inode && S_ISDIR(inode->i_mode)) {
				buffer[buflen - 2] = '/';
				buffer[buflen - 1] = '\0';
			}
		}
	}
	return pos;
}

/**
 * tomoyo_get_local_path - Get the path of a dentry.
 *
 * @dentry: Pointer to "struct dentry".
 * @buffer: Pointer to buffer to return value in.
 * @buflen: Sizeof @buffer.
 *
 * Returns the buffer on success, an error code otherwise.
 */
static char *tomoyo_get_local_path(struct dentry *dentry, char * const buffer,
				   const int buflen)
{
	struct super_block *sb = dentry->d_sb;
	char *pos = tomoyo_get_dentry_path(dentry, buffer, buflen);

	if (IS_ERR(pos))
		return pos;
	/* Convert from $PID to self if $PID is current thread. */
	if (sb->s_magic == PROC_SUPER_MAGIC && *pos == '/') {
		char *ep;
		const pid_t pid = (pid_t) simple_strtoul(pos + 1, &ep, 10);

		if (*ep == '/' && pid && pid ==
		    task_tgid_nr_ns(current, sb->s_fs_info)) {
			pos = ep - 5;
			if (pos < buffer)
				goto out;
			memmove(pos, "/self", 5);
		}
		goto prepend_filesystem_name;
	}
	/* Use filesystem name for unnamed devices. */
	if (!MAJOR(sb->s_dev))
		goto prepend_filesystem_name;
	{
		struct inode *inode = d_backing_inode(sb->s_root);

		/*
		 * Use filesystem name if filesystem does not support rename()
		 * operation.
		 */
		if (!inode->i_op->rename)
			goto prepend_filesystem_name;
	}
	/* Prepend device name. */
	{
		char name[64];
		int name_len;
		const dev_t dev = sb->s_dev;

		name[sizeof(name) - 1] = '\0';
		snprintf(name, sizeof(name) - 1, "dev(%u,%u):", MAJOR(dev),
			 MINOR(dev));
		name_len = strlen(name);
		pos -= name_len;
		if (pos < buffer)
			goto out;
		memmove(pos, name, name_len);
		return pos;
	}
	/* Prepend filesystem name. */
prepend_filesystem_name:
	{
		const char *name = sb->s_type->name;
		const int name_len = strlen(name);

		pos -= name_len + 1;
		if (pos < buffer)
			goto out;
		memmove(pos, name, name_len);
		pos[name_len] = ':';
	}
	return pos;
out:
	return ERR_PTR(-ENOMEM);
}


char *get_path_for_dentry(struct dentry *dentry) {
	// char *pos = NULL;
    // char *pathname = NULL;
	// size_t pathname_len = 2048;
	// struct inode *d_inode;

	// pathname = kzalloc(pathname_len, GFP_NOFS);
	// pos = pathname;
	// if(pathname) {
	// 	pos = dentry_path_raw(dentry, pathname, pathname_len - 1);
	// 	if (!IS_ERR(pos)) {
	// 		if(*pos == '/' && pos[1]) {
	// 			printk(PFX "Doing inode things\n");
	// 			d_inode = d_backing_inode(dentry);
	// 			if (d_inode) {
	// 				printk(PFX "Got inode\n");
	// 				if(S_ISDIR(d_inode->i_mode)) {
	// 					printk(PFX "Is directory\n");
	// 					pathname[pathname_len - 2] = '/';
	// 					pathname[pathname_len - 1] = '\0';
	// 				}
	// 			}
	// 		}

	// 		printk(PFX "%p %p\n", pathname, pos);
	// 		return pathname;
	// 	} else {
	// 		printk(PFX "Aborting get_path_for_dentry()\n");
	// 		return pathname; // Empty buffer. 3101233312 - 2936675473
	// 	}
	// }
	return NULL;
	// pos = tomoyo_get_local_path(dentry, pathname, pathname_len);
	// if (IS_ERR(pos)) {
	// 	return pathname;
	// }

	// return pos;
}


/*
 * Internal.
 * Raw call to __vfs_setxattr_noperm. Requires caller to organise dentry locking.
 * Returns error code of the VFS call.
 */
int __internal_set_xattr(struct dentry *dentry, const char *name, char *value, size_t len) {
	int res;
	// need to lock inode->i_rwsem
    // down_write(&(dentry->d_inode->i_rwsem));
    res = __vfs_setxattr_noperm(dentry, name, (const void*)value, len, 0);
	// up_write(&(dentry->d_inode->i_rwsem));
	return res;
}


int __internal_set_in_realm(struct dentry *dentry) {
	return __internal_set_xattr(dentry, TRM_XATTR_REALM_NAME, NULL, 0);
}

int __internal_set_identifier(struct dentry *dentry, char *value, size_t len) {
	char *hex;
	int res;

	hex = to_hexstring(value, len);
	if (!hex) return -ENOMEM;
	res = __internal_set_xattr(dentry, TRM_XATTR_ID_NAME, hex, _TRM_IDENTIFIER_LENGTH * 2 + 1);
	kfree(hex);
	return res;
}

int set_xattr_in_realm(struct dentry *dentry) {
	int res;
	// down_write(&(dentry->d_inode->i_rwsem));
	res = __internal_set_in_realm(dentry);
	// up_write(&(dentry->d_inode->i_rwsem));
	return res;
}

int set_xattr_identifier(struct dentry *dentry, char *value, size_t len) {
	int res_a, res_b;
	// down_write(&(dentry->d_inode->i_rwsem));
	res_a = __internal_set_identifier(dentry, value, len);
	res_b = __internal_set_in_realm(dentry);
	// up_write(&(dentry->d_inode->i_rwsem));
	return (res_a < res_b) ? res_a : res_b;
}

void* _hex_identifier_to_bytes(char* hexstring) {
	size_t i, j;
	size_t len = strlen(hexstring);
	size_t final_len = len / 2;
	unsigned char* identifier; 

    if(len % 2 != 0) return NULL;

	identifier = (unsigned char*) kmalloc(final_len, GFP_KERNEL);
    for (i = 0, j = 0; j < final_len; i += 2, j++) {
        identifier[j] = (hexstring[i] % 32 + 9) % 25 * 16 + (hexstring[i+1] % 32 + 9) % 25;
	}
	return identifier;
}

char *get_xattr_identifier(struct dentry *dentry) {
	int x;
	char *hex_identifier, *identifier;
	size_t identifier_length = 2 * _TRM_IDENTIFIER_LENGTH + 1;
	
	hex_identifier = kzalloc(identifier_length, GFP_KERNEL);
	x = __vfs_getxattr(dentry, d_backing_inode(dentry), TRM_XATTR_ID_NAME, hex_identifier, identifier_length);
	if (x > 0) {
		printk(PFX "Loaded xattr from disk: %s\n", hex_identifier);
		identifier = _hex_identifier_to_bytes(hex_identifier);
	} else {
		identifier = NULL;
	}

	kfree(hex_identifier);
	return identifier;
}

void realm_housekeeping(struct inode_trm *i_trm, struct dentry *dentry) {
	int res;
    if (!i_trm->in_realm) return;
    if (i_trm->needs_xattr_update) {
		i_trm->needs_xattr_update = false;
		res = set_xattr_in_realm(dentry);
		// printk(PFX "realm_housekeeping -> set xattr (%d)\n", res);
		// TODO support setting identifier
    }
}


void global_housekeeping(struct inode_trm *i_trm, struct dentry *dentry) {
	int x;
	char *identifier;

	// Abort if invalid.
	if (i_trm == NULL || dentry == NULL) return;

	if (!i_trm->checked_disk_xattr) {
		i_trm->checked_disk_xattr = true;

		// Fetch identifier.
		identifier = get_xattr_identifier(dentry);
		if (identifier) {
			memcpy(i_trm->identifier, identifier, _TRM_IDENTIFIER_LENGTH);
			i_trm->in_realm = true;
			kfree(identifier);
		}
		else {
			// No identifier, check for anonymous entity.
			x = __vfs_getxattr(dentry, d_backing_inode(dentry), TRM_XATTR_REALM_NAME, NULL, 0);
			i_trm->in_realm = (x > 0);
		}
    }

	if (i_trm->in_realm) realm_housekeeping(i_trm, dentry);
}