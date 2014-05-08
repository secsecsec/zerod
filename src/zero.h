#ifndef ZERO_H
#define ZERO_H

#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <net/if.h>

#include <freeradius-client.h>
#include <uthash/utarray.h>

#include "netmap.h"
#include "util.h"

#define MAX_THREAD_NAME 16

/**
 * For decreasing storage access concurrency useed dividing one type of storage to many substorages.
 * For example session storage uses lookup by ip and we use for substorage selection lower bits of ip address.
 */

// storage mask
#define STORAGE_MASK 0b1111u
// number of storages
#define STORAGE_SIZE ((STORAGE_MASK) + 1)
// retrieve storage index
#define STORAGE_IDX(x) ((x) & STORAGE_MASK)

#define ZUPSTREAM_MAX 1

// 2 mins
#define ZP2P_THROTTLE_TIME 120000000

struct ip_range;
struct event_base;
struct evconnlistener;
struct zsrules;

struct zif_pair {
    // LAN interface name
    char lan[IFNAMSIZ];
    // WAN interface name
    char wan[IFNAMSIZ];
    // affinity
    uint16_t affinity;
};

struct zoverlord {
    // thread index
    u_int idx;
    // thread handle
    pthread_t thread;
};

struct zring {
    // interfaces info
    struct zif_pair *if_pair;
    // thread handle
    pthread_t thread;
    // ring index
    uint16_t ring_id;
    // netmap rings
    struct znm_ring ring_lan;
    struct znm_ring ring_wan;
    // statistics
    struct {
        struct {
            // value counter (atomic)
            uint64_t count;
            struct speed_meter speed;
        } all, passed;
    } packets[DIR_MAX], traffic[DIR_MAX];
};

struct zupstream {
    struct token_bucket p2p_bw_bucket[DIR_MAX];
    struct speed_meter speed[DIR_MAX];
};

struct zero_config {
    // array of interface pairs
    UT_array interfaces;
    // wait time before start running operations on interfaces (seconds)
    u_int iface_wait_time;

    // overlord threads count
    u_int overlord_threads;

    // unauthorized client bandwidth limits (bytes)
    uint64_t unauth_bw_limit[DIR_MAX];

    // ip whitelist
    UT_array ip_whitelist;

    // path to radius configuration file
    char *radius_config_file;
    // radius NAS identifier
    char *radius_nas_identifier;

    // session_timeout (microseconds)
    uint64_t session_timeout;
    // session accounting update interval (microseconds)
    uint64_t session_acct_interval;
    // session authentication interval (microseconds)
    uint64_t session_auth_interval;

    // remote control address and port
    char *rc_listen_addr;

    // default upstream p2p bandwidth limits (bytes)
    uint64_t upstream_p2p_bw[DIR_MAX];

    // non-p2p ports (uint16_t array)
    UT_array p2p_ports_whitelist;

    // p2p ports (uint16_t array)
    UT_array p2p_ports_blacklist;

    // non-client bandwidth limits (bytes)
    uint64_t non_client_bw[DIR_MAX];

    // initial client bucket size (bytes)
    uint64_t initial_client_bucket_size;
};

struct zero_instance {
    // configuration, must not be used directly
    const struct zero_config *_cfg;
    // execution abort flag (atomic)
    bool abort;

    // active session count (atomic)
    u_int sessions_cnt;
    // authed clients count (atomic)
    u_int clients_cnt;
    // unauthed sessions count (atomic)
    u_int unauth_sessions_cnt;

    // global lock for s_sessions hash
    pthread_rwlock_t sessions_lock[STORAGE_SIZE];
    // global lock for s_clients hash
    pthread_rwlock_t clients_lock[STORAGE_SIZE];

    // hash ip->session
    struct zsession *sessions[STORAGE_SIZE];
    // hash user_id->client
    struct zclient *clients[STORAGE_SIZE];

    // radius handle
    rc_handle *radh;

    // master thread event base
    struct event_base *master_event_base;
    // remote control tcp connection listener
    struct evconnlistener *rc_tcp_listener;

    // rings information (zring array)
    UT_array rings;

    // upstreams
    struct zupstream upstreams[ZUPSTREAM_MAX];

    // non-client info
    struct {
        struct token_bucket bw_bucket[DIR_MAX];
        struct speed_meter speed[DIR_MAX];
    } non_client;
};

extern const UT_icd ut_zif_pair_icd;
extern const UT_icd ut_zring_icd;

// global app instance
extern struct zero_instance g_zinst;

/**
 * Global access to app instance.
 * @return App instance.
 */
static __inline struct zero_instance *zinst()
{
    return &g_zinst;
}

/**
 * Global access to app configuration.
 * @return App config.
 */
static __inline const struct zero_config *zcfg()
{
    return g_zinst._cfg;
}

int zero_instance_init(const struct zero_config *zconf);
void zero_instance_run();
void zero_instance_free();
void zero_instance_stop();

void zero_apply_rules(struct zsrules *rules);

// config.c
int zero_config_load(const char *path, struct zero_config *zconf);
void zero_config_free(struct zero_config *zconf);

// packet.c
int process_packet(unsigned char *packet, u_int len, enum flow_dir flow_dir);

// master.c
void master_worker();

// overlord.c
void *overlord_worker(void *arg);

// remotectl.c
int rc_listen();

#endif // ZERO_H