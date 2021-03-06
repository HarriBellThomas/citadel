
#ifndef _SECURITY_TRM_CACHE_H
#define _SECURITY_TRM_CACHE_H

struct ticket_reservation_node {
    struct rb_node node;
    pid_t pid;
    citadel_ticket_t *ticket_head;
    bool granted_pty;
};

extern void check_ticket_cache(void);
extern bool insert_ticket(citadel_update_record_t *record);

#endif  /* _SECURITY_TRM_CACHE_H */