/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_H_INCLUDED
#define ABTI_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>

#include "abt_config.h"
#include "abt.h"
#include "abtu.h"
#include "abti_error.h"
#include "abti_valgrind.h"


/* Constants */
#define ABTI_SCHED_NUM_PRIO         3

#define ABTI_XSTREAM_REQ_JOIN       (1 << 0)
#define ABTI_XSTREAM_REQ_EXIT       (1 << 1)
#define ABTI_XSTREAM_REQ_CANCEL     (1 << 2)
#define ABTI_XSTREAM_REQ_STOP       (1 << 3)
#define ABTI_XSTREAM_REQ_FREE_TIMER (1 << 4)

#define ABTI_SCHED_REQ_FINISH       (1 << 0)
#define ABTI_SCHED_REQ_EXIT         (1 << 1)

#define ABTI_THREAD_REQ_JOIN        (1 << 0)
#define ABTI_THREAD_REQ_EXIT        (1 << 1)
#define ABTI_THREAD_REQ_CANCEL      (1 << 2)
#define ABTI_THREAD_REQ_MIGRATE     (1 << 3)
#define ABTI_THREAD_REQ_TERMINATE   (1 << 4)
#define ABTI_THREAD_REQ_BLOCK       (1 << 5)
#define ABTI_THREAD_REQ_ORPHAN      (1 << 6)
#define ABTI_THREAD_REQ_NOPUSH      (1 << 7)
#define ABTI_THREAD_REQ_STOP        \
    (ABTI_THREAD_REQ_EXIT | ABTI_THREAD_REQ_TERMINATE)
#define ABTI_THREAD_REQ_NON_YIELD   \
    (ABTI_THREAD_REQ_EXIT    | ABTI_THREAD_REQ_CANCEL |        \
     ABTI_THREAD_REQ_MIGRATE | ABTI_THREAD_REQ_TERMINATE |     \
     ABTI_THREAD_REQ_BLOCK   | ABTI_THREAD_REQ_ORPHAN |        \
     ABTI_THREAD_REQ_NOPUSH)

#define ABTI_TASK_REQ_CANCEL        (1 << 0)

#define ABTI_THREAD_INIT_ID         0xFFFFFFFFFFFFFFFF
#define ABTI_TASK_INIT_ID           0xFFFFFFFFFFFFFFFF

#define ABTI_INDENT                 4

#define ABT_THREAD_TYPE_FULLY_FLEDGED      0
#define ABT_THREAD_TYPE_DYNAMIC_PROMOTION  1

#define ABT_ENABLE_NORMAL_PROFILE     0
#define ABT_ENABLE_PREEMPTION_PROFILE 0

#ifndef ABT_USE_FUTEX_ON_SLEEP
#define ABT_USE_FUTEX_ON_SLEEP 1
#endif

#ifndef ABT_SUB_XSTREAM_LOCAL_POOL
#define ABT_SUB_XSTREAM_LOCAL_POOL 1
#endif

#ifndef ABT_ENABLE_PREEMPTION_DEBUG
#define ABT_ENABLE_PREEMPTION_DEBUG 0
#endif

#if ABT_ENABLE_PREEMPTION_DEBUG
#define ABTI_PREEMPTION_DEBUG(event) \
    printf("[worker %d; KLT %d] " event " at %s (%s:%d)\n", ABTI_local_get_xstream_explicit()->rank, ABTI_sub_xstream_get_data_explicit()->id, __func__, __FILE__, __LINE__)
#define ABTI_PREEMPTION_DEBUG_1(event, ...) \
    printf("[worker %d; KLT %d] " event " at %s (%s:%d)\n", ABTI_local_get_xstream_explicit()->rank, ABTI_sub_xstream_get_data_explicit()->id, __VA_ARGS__, __func__, __FILE__, __LINE__)
#else
#define ABTI_PREEMPTION_DEBUG(event)
#define ABTI_PREEMPTION_DEBUG_1(event, ...)
#endif

enum ABTI_xstream_type {
    ABTI_XSTREAM_TYPE_PRIMARY,
    ABTI_XSTREAM_TYPE_SECONDARY
};

enum ABTI_sched_used {
    ABTI_SCHED_NOT_USED,
    ABTI_SCHED_MAIN,
    ABTI_SCHED_IN_POOL
};

enum ABTI_thread_type {
    ABTI_THREAD_TYPE_MAIN,
    ABTI_THREAD_TYPE_MAIN_SCHED,
    ABTI_THREAD_TYPE_INTERNAL,
    ABTI_THREAD_TYPE_USER
};

enum ABTI_mutex_attr_val {
    ABTI_MUTEX_ATTR_NONE = 0,
    ABTI_MUTEX_ATTR_RECURSIVE = 1 << 0
};

enum ABTI_stack_type {
    ABTI_STACK_TYPE_MEMPOOL = 0, /* Stack taken from the memory pool */
    ABTI_STACK_TYPE_MALLOC,      /* Stack allocated by malloc in Argobots */
    ABTI_STACK_TYPE_USER,        /* Stack given by a user */
    ABTI_STACK_TYPE_MAIN,        /* Stack of a main ULT. */
};

enum ABTI_preemption_timer_type {
    ABTI_PREEMPTION_TIMER_SIGNAL,
    ABTI_PREEMPTION_TIMER_DEDICATED,
};

/* Macro functions */
#define ABTI_UNUSED(a)              (void)(a)

#define ABTI_ENTER                  l_ABTI_entry_count++
#define ABTI_LEAVE                  (*ABTI_local_get_entry_count())--
#define ABTI_SAVE_ENTRY_COUNT       int abti_saved_entry_count = l_ABTI_entry_count
#define ABTI_RESTORE_ENTRY_COUNT    (*ABTI_local_get_entry_count()) = abti_saved_entry_count
#define ABTI_ENTRY_COUNT_SET(x)     l_ABTI_entry_count = x;

/* #define ABTI_ENTRY_COUNT_PRINT(event) \ */
/*     if (lp_ABTI_local && lp_ABTI_local->p_xstream) { \ */
/*         printf("[rank %d] %s %d at %s\n", lp_ABTI_local->p_xstream->rank, event, l_ABTI_entry_count, __func__); \ */
/*     } else { \ */
/*         printf("[rank ?] %s %d at %s\n", event, l_ABTI_entry_count, __func__); \ */
/*     } */
/* #define ABTI_ENTER                  l_ABTI_entry_count++;                                     ABTI_ENTRY_COUNT_PRINT("increment") */
/* #define ABTI_LEAVE                  (*ABTI_local_get_entry_count())--;                        ABTI_ENTRY_COUNT_PRINT("decrement") */
/* #define ABTI_SAVE_ENTRY_COUNT       int abti_saved_entry_count = l_ABTI_entry_count;          ABTI_ENTRY_COUNT_PRINT("save") */
/* #define ABTI_RESTORE_ENTRY_COUNT    (*ABTI_local_get_entry_count()) = abti_saved_entry_count; ABTI_ENTRY_COUNT_PRINT("restore") */
/* #define ABTI_ENTRY_COUNT_SET(x)     l_ABTI_entry_count = x;                                   ABTI_ENTRY_COUNT_PRINT("set") */


/* Data Types */
typedef struct ABTI_global                  ABTI_global;
typedef struct ABTI_local                   ABTI_local;
typedef struct ABTI_sub_xstream_list_local  ABTI_sub_xstream_list_local;
typedef struct ABTI_sub_xstream_list_global ABTI_sub_xstream_list_global;
typedef struct ABTI_sub_xstream             ABTI_sub_xstream;
typedef struct ABTI_sub_xstream_allocator   ABTI_sub_xstream_allocator;
typedef struct ABTI_xstream                 ABTI_xstream;
typedef enum ABTI_xstream_type              ABTI_xstream_type;
typedef struct ABTI_sched                   ABTI_sched;
typedef char *                              ABTI_sched_config;
typedef enum ABTI_sched_used                ABTI_sched_used;
typedef void *                              ABTI_sched_id;   /* Scheduler id */
typedef uint64_t                            ABTI_sched_kind; /* Scheduler kind */
typedef struct ABTI_pool                    ABTI_pool;
typedef struct ABTI_unit                    ABTI_unit;
typedef struct ABTI_thread_attr             ABTI_thread_attr;
typedef struct ABTI_thread                  ABTI_thread;
typedef enum ABTI_thread_type               ABTI_thread_type;
typedef enum ABTI_stack_type                ABTI_stack_type;
typedef struct ABTI_thread_req_arg          ABTI_thread_req_arg;
typedef struct ABTI_thread_list             ABTI_thread_list;
typedef struct ABTI_thread_entry            ABTI_thread_entry;
typedef struct ABTI_thread_htable           ABTI_thread_htable;
typedef struct ABTI_thread_queue            ABTI_thread_queue;
typedef struct ABTI_task                    ABTI_task;
typedef struct ABTI_key                     ABTI_key;
typedef struct ABTI_ktelem                  ABTI_ktelem;
typedef struct ABTI_ktable                  ABTI_ktable;
typedef struct ABTI_mutex_attr              ABTI_mutex_attr;
typedef struct ABTI_mutex                   ABTI_mutex;
typedef struct ABTI_cond                    ABTI_cond;
typedef struct ABTI_rwlock                  ABTI_rwlock;
typedef struct ABTI_eventual                ABTI_eventual;
typedef struct ABTI_future                  ABTI_future;
typedef struct ABTI_barrier                 ABTI_barrier;
typedef struct ABTI_timer                   ABTI_timer;
typedef struct ABTI_preemption_group        ABTI_preemption_group;
typedef struct ABTI_preemption_timer        ABTI_preemption_timer;
#ifdef ABT_CONFIG_USE_MEM_POOL
typedef struct ABTI_stack_header            ABTI_stack_header;
typedef struct ABTI_page_header             ABTI_page_header;
typedef struct ABTI_sp_header               ABTI_sp_header;
#endif


/* Architecture-Dependent Definitions */
#include "abtd.h"


/* Spinlock */
typedef struct ABTI_spinlock        ABTI_spinlock;
#include "abti_spinlock.h"


/* Definitions */
struct ABTI_mutex_attr {
    uint32_t attrs;             /* bit-or'ed attributes */
    uint32_t nesting_cnt;       /* nesting count */
    ABTI_unit *p_owner;         /* owner work unit */
    uint32_t max_handovers;     /* max. # of handovers */
    uint32_t max_wakeups;       /* max. # of wakeups */
};

struct ABTI_mutex {
    uint32_t val;                   /* 0: unlocked, 1: locked */
    ABTI_mutex_attr attr;           /* attributes */
    ABTI_thread_htable *p_htable;   /* a set of queues */
    ABTI_thread *p_handover;        /* next ULT for the mutex handover */
    ABTI_thread *p_giver;           /* current ULT that hands over the mutex */
};

struct ABTI_global {
    int max_xstreams;            /* Max. size of p_xstreams */
    int num_xstreams;            /* Current # of ESs */
    int num_active_xstreams;     /* For changing # of ESs adaptively */
    ABTI_xstream **p_xstreams;   /* ES array */
    ABTI_spinlock xstreams_lock; /* Spinlock protecting p_xstreams. Any write
                                  * to p_xstreams and p_xstreams[*] requires a
                                  * lock. Dereference does not require a lock.*/

    int num_cores;                /* Number of CPU cores */
    ABT_bool set_affinity;        /* Whether CPU affinity is used */
    ABT_bool use_logging;         /* Whether logging is used */
    ABT_bool use_debug;           /* Whether debug output is used */
    int key_table_size;           /* Default key table size */
    size_t thread_stacksize;      /* Default stack size for ULT (in bytes) */
    size_t sched_stacksize;       /* Default stack size for sched (in bytes) */
    uint32_t sched_event_freq;    /* Default check frequency for sched */
    long sched_sleep_nsec;        /* Default nanoseconds for scheduler sleep */
    ABTI_thread *p_thread_main;   /* ULT of the main function */
    int initial_num_sub_xstreams; /* Initial # of sub ESs */

    int preemption_timer_type;
    uint64_t preemption_intvl_usec;
    ABTI_sub_xstream_allocator *p_sub_allocator;

    uint32_t mutex_max_handovers;      /* Default max. # of local handovers */
    uint32_t mutex_max_wakeups;        /* Default max. # of wakeups */
    uint32_t os_page_size;             /* OS page size */
    uint32_t huge_page_size;           /* Huge page size */
#ifdef ABT_CONFIG_USE_MEM_POOL
    ABTI_spinlock mem_task_lock;       /* Spinlock protecting p_mem_task */
    uint32_t mem_page_size;            /* Page size for memory allocation */
    uint32_t mem_sp_size;              /* Stack page size */
    uint32_t mem_max_stacks;           /* Max. # of stacks kept in each ES */
    int mem_lp_alloc;                  /* How to allocate large pages */
    ABTI_stack_header *p_mem_stack;    /* List of ULT stack */
    ABTI_page_header *p_mem_task;      /* List of task block pages */
    ABTI_sp_header *p_mem_sph;         /* List of stack pages */
#endif

    ABT_bool pm_connected;      /* Is power mgmt. daemon connected? */
    char *pm_host;              /* Hostname for power mgmt. daemon */
    int pm_port;                /* Port number for power mgmt. daemon */
#ifdef ABT_CONFIG_PUBLISH_INFO
    ABT_bool pub_needed;        /* Is info. publishing needed? */
    char *pub_filename;         /* Filename for publishing */
    double pub_interval;        /* Time interval in seconds */
#endif

    ABT_bool print_config;      /* Whether to print config on ABT_init */
};

struct ABTI_local {
    ABTI_xstream *p_xstream;    /* Current ES */
    ABTI_thread *p_thread;      /* Current running ULT */
    ABTI_task *p_task;          /* Current running tasklet */

#ifdef ABT_CONFIG_USE_MEM_POOL
    uint32_t num_stacks;                /* Current # of stacks */
    ABTI_stack_header *p_mem_stack;     /* Free stack list */
    ABTI_page_header *p_mem_task_head;  /* Head of page list */
    ABTI_page_header *p_mem_task_tail;  /* Tail of page list */
#endif
};

struct ABTI_sub_xstream_list_local {
    ABTI_sub_xstream *p_head;
    uint32_t num;
};

struct ABTI_sub_xstream_list_global {
    ABTI_sub_xstream *p_head;
};

struct ABTI_sub_xstream {
    ABTI_sub_xstream *p_next;

    int id;
    ABT_bool is_main;
    int sleep_flag;
    uint32_t ready;
    uint32_t terminate_flag;
    ABTI_local *p_local;
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    void *p_local_log;
#endif
    ABTI_thread *p_base_thread;
    ABTD_xstream_context ctx;
    int affinity;

    ABTD_signal_timer timer;
    uint32_t timer_started;

    ABTI_sub_xstream *p_main;

    ABTI_thread *p_return_thread;

    int global_alloc;

#if ABT_ENABLE_PREEMPTION_PROFILE
    uint32_t preempted;
#endif
};

struct ABTI_sub_xstream_allocator {
    int sleep_flag;
    uint32_t terminate_flag;
    uint32_t create_count;
    ABTI_sub_xstream_list_global sub_list;
    ABTD_xstream_context ctx;
};

struct ABTI_xstream {
    int rank;                   /* Rank */
    ABTI_xstream_type type;     /* Type */
    volatile
    ABT_xstream_state state;    /* State */
    ABTI_sched **scheds;        /* Stack of running schedulers */
    int max_scheds;             /* Allocation size of the array scheds */
    int num_scheds;             /* Number of scheds */
    ABTI_spinlock sched_lock;   /* Lock for the scheduler management */

    uint32_t request;           /* Request */
    void *p_req_arg;            /* Request argument */
    ABTI_sched *p_main_sched;   /* Main scheduler */

    ABTD_xstream_context ctx;   /* ES context */

    ABT_bool preemptive;
    ABT_bool ptimer_on;
    ABTI_preemption_timer *p_preemption_timer;

    ABTI_sub_xstream *p_main;
    ABTI_sub_xstream *p_cur_sub;
    ABTI_sub_xstream_list_local sub_xstream_list;
    ABTI_thread *p_wakeup_thread;

     /* for adaptive # of ESs */
    ABT_bool disable_sleep;
    ABT_bool should_sleep;
    int sleep_flag;

    void *p_mlog_data;
#if ABT_ENABLE_PREEMPTION_PROFILE
    uint64_t t0;
#endif
};

struct ABTI_sched {
    ABTI_sched_used used;       /* To know if it is used and how */
    ABT_bool automatic;         /* To know if automatic data free */
    ABTI_sched_kind kind;       /* Kind of the scheduler  */
    ABT_sched_type type;        /* Can yield or not (ULT or task) */
    ABT_sched_state state;      /* State */
    uint32_t request;           /* Request */
    ABT_pool *pools;            /* Work unit pools */
    int num_pools;              /* Number of work unit pools */
    ABTI_thread *p_thread;      /* Associated ULT */
    ABTI_task *p_task;          /* Associated tasklet */
    ABTD_thread_context *p_ctx; /* Context */
    void *data;                 /* Data for a specific scheduler */

    /* Scheduler functions */
    ABT_sched_init_fn init;
    ABT_sched_run_fn  run;
    ABT_sched_free_fn free;
    ABT_sched_get_migr_pool_fn get_migr_pool;

#ifdef ABT_CONFIG_USE_DEBUG_LOG
    uint64_t id;                /* ID */
#endif
};

struct ABTI_pool {
    ABT_pool_access access;  /* Access mode */
    ABT_bool automatic;      /* To know if automatic data free */
    int32_t num_scheds;      /* Number of associated schedulers */
                             /* NOTE: int32_t to check if still positive */
#ifndef ABT_CONFIG_DISABLE_POOL_CONSUMER_CHECK
    ABTI_xstream *consumer;  /* Associated consumer ES */
#endif
#ifndef ABT_CONFIG_DISABLE_POOL_PRODUCER_CHECK
    ABTI_xstream *producer;  /* Associated producer ES */
#endif
    uint32_t num_blocked;    /* Number of blocked ULTs */
    int32_t num_migrations;  /* Number of migrating ULTs */
    void *data;              /* Specific data */
    uint64_t id;             /* ID */

    /* Functions to manage units */
    ABT_unit_get_type_fn           u_get_type;
    ABT_unit_get_thread_fn         u_get_thread;
    ABT_unit_get_task_fn           u_get_task;
    ABT_unit_is_in_pool_fn         u_is_in_pool;
    ABT_unit_create_from_thread_fn u_create_from_thread;
    ABT_unit_create_from_task_fn   u_create_from_task;
    ABT_unit_free_fn               u_free;

    /* Functions to manage the pool */
    ABT_pool_init_fn               p_init;
    ABT_pool_get_size_fn           p_get_size;
    ABT_pool_push_fn               p_push;
    ABT_pool_pop_fn                p_pop;
    ABT_pool_pop_timedwait_fn      p_pop_timedwait;
    ABT_pool_remove_fn             p_remove;
    ABT_pool_free_fn               p_free;
    ABT_pool_print_all_fn          p_print_all;
};

struct ABTI_unit {
    ABTI_unit *p_prev;
    ABTI_unit *p_next;
    ABT_pool pool;
    union {
        ABT_thread thread;
        ABT_task   task;
    };
    ABT_unit_type type;
};

struct ABTI_thread_attr {
    void *p_stack;                       /* Stack address */
    size_t stacksize;                    /* Stack size (in bytes) */
    ABTI_stack_type stacktype;           /* Stack type */
#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABT_bool migratable;                 /* Migratability */
    void (*f_cb)(ABT_thread, void *);    /* Callback function */
    void *p_cb_arg;                      /* Callback function argument */
#endif
    ABT_preemption_type preemption_type; /* Thread-safety */
};

struct ABTI_thread {
    ABTD_thread_context ctx;        /* Context */
    ABTI_unit unit_def;             /* Internal unit definition */
    ABT_thread_state state;         /* State */
    uint32_t request;               /* Request */
    ABTI_xstream *p_last_xstream;   /* Last ES where it ran */
#ifndef ABT_CONFIG_DISABLE_STACKABLE_SCHED
    ABTI_sched *is_sched;           /* If it is a scheduler, its ptr */
#endif
    ABT_unit unit;                  /* Unit enclosing this thread */
    ABTI_pool *p_pool;              /* Associated pool */
    uint32_t refcount;              /* Reference count */
    ABTI_thread_type type;          /* Type */
    ABTI_thread_req_arg *p_req_arg; /* Request argument */
    ABTI_spinlock lock;             /* Spinlock */
    ABTI_ktable *p_keytable;        /* ULT-specific data */
    ABTI_thread_attr attr;          /* Attributes */
    ABT_thread_id id;               /* ID */
    ABT_bool preempted;
    ABTI_sub_xstream *p_wakeup;
};

struct ABTI_thread_req_arg {
    uint32_t request;
    void *p_arg;
    ABTI_thread_req_arg *next;
};

struct ABTI_thread_list {
    ABTI_thread_entry *head;
    ABTI_thread_entry *tail;
};

struct ABTI_thread_entry {
    ABTI_thread *current;
    struct ABTI_thread_entry *next;
    ABT_unit_type type;
};

struct ABTI_task {
    ABTI_xstream *p_xstream;   /* Associated ES */
    ABT_task_state state;      /* State */
    uint32_t request;          /* Request */
    void (*f_task)(void *);    /* Task function */
    void *p_arg;               /* Task arguments */
#ifndef ABT_CONFIG_DISABLE_STACKABLE_SCHED
    ABTI_sched *is_sched;      /* If it is a scheduler, its ptr */
#endif
    ABTI_pool *p_pool;         /* Associated pool */
    ABT_unit unit;             /* Unit enclosing this task */
    ABTI_unit unit_def;        /* Internal unit definition */
    uint32_t refcount;         /* Reference count */
    ABTI_ktable *p_keytable;   /* Tasklet-specific data */
#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABT_bool migratable;       /* Migratability */
#endif
    uint64_t id;               /* ID */
};

struct ABTI_key {
    void (*f_destructor)(void *value);
    uint32_t id;
    uint32_t refcount;          /* Reference count */
    ABT_bool freed;             /* TRUE: freed, FALSE: not */
};

struct ABTI_ktelem {
    ABTI_key *p_key;
    void *value;
    struct ABTI_ktelem *p_next;
};

struct ABTI_ktable {
    int size;                   /* size of the table */
    int num;                    /* number of elements stored */
    ABTI_ktelem **p_elems;      /* element array */
};

struct ABTI_cond {
    ABTI_spinlock lock;
    ABTI_mutex *p_waiter_mutex;
    size_t num_waiters;
    ABTI_unit *p_head;          /* Head of waiters */
    ABTI_unit *p_tail;          /* Tail of waiters */
};

struct ABTI_rwlock {
    ABTI_mutex mutex;
    ABTI_cond  cond;
    size_t reader_count;
    int write_flag;
};

struct ABTI_eventual {
    ABTI_spinlock lock;
    ABT_bool ready;
    void *value;
    int nbytes;
    ABTI_unit *p_head;          /* Head of waiters */
    ABTI_unit *p_tail;          /* Tail of waiters */
};

struct ABTI_future {
    ABTI_spinlock lock;
    ABT_bool ready;
    uint32_t counter;
    uint32_t compartments;
    void **array;
    void (*p_callback)(void **arg);
    ABTI_unit *p_head;          /* Head of waiters */
    ABTI_unit *p_tail;          /* Tail of waiters */
};

struct ABTI_barrier {
    uint32_t num_waiters;
    volatile uint32_t counter;
    ABTI_thread **waiters;
    ABT_unit_type *waiter_type;
    ABTI_spinlock lock;
};

struct ABTI_timer {
    ABTD_time start;
    ABTD_time end;
};

struct ABTI_preemption_group {
    int num_xstreams;
    ABTI_xstream **p_xstreams;
    int index_until;
    int index_next;
    ABT_bool in_chain;
    uint64_t offset_nsec;
    uint32_t delete_counter;
};

struct ABTI_preemption_timer {
    ABTI_preemption_group *p_group;
    int preemption_index;
    uint64_t offset;
    uint64_t intvl;

    /* for dedicated timer */
    int rank;
    int sleep_flag;
    uint32_t terminate_flag;
    uint32_t start_flag;
    uint32_t stop_flag;
    ABTD_xstream_context ctx;
};

/* Global Data */
extern ABTI_global *gp_ABTI_global;

/* ES Local Data */
extern ABTD_XSTREAM_LOCAL ABTI_local *lp_ABTI_local;
extern ABTD_XSTREAM_LOCAL int l_ABTI_entry_count;
extern ABTD_XSTREAM_LOCAL ABTI_sub_xstream *lp_ABTI_sub_xstream;
extern ABTI_local *(*gp_ABTI_local_get)(void);
extern int *(*gp_ABTI_local_entry_count_get)(void);
extern ABTI_sub_xstream *(*gp_ABTI_get_sub_xstream)(void);

/* Global */
void ABTI_global_update_max_xstreams(int new_size);

/* ES Local Data */
int ABTI_local_init(void);
int ABTI_local_finalize(void);

/* Execution Stream (ES) */
int ABTI_xstream_create(ABTI_sched *p_sched, ABTI_xstream **pp_xstream);
int ABTI_xstream_create_sub(ABTI_sched *p_sched, ABTI_xstream **pp_xstream);
int ABTI_xstream_create_primary(ABTI_xstream **pp_xstream);
int ABTI_xstream_start(ABTI_xstream *p_xstream);
int ABTI_xstream_start_primary(ABTI_xstream *p_xstream, ABTI_thread *p_thread);
int ABTI_xstream_start_on_core(ABTI_xstream *p_xstream, int core_num);
int ABTI_xstream_free(ABTI_xstream *p_xstream);
void ABTI_xstream_schedule(void *p_arg);
int ABTI_xstream_run_unit(ABTI_xstream *p_xstream, ABT_unit unit,
                          ABTI_pool *p_pool);
int ABTI_xstream_schedule_thread(ABTI_xstream *p_xstream,
                                 ABTI_thread *p_thread);
void ABTI_xstream_schedule_task(ABTI_xstream *p_xstream, ABTI_task *p_task);
int ABTI_xstream_migrate_thread(ABTI_thread *p_thread);
int ABTI_xstream_set_main_sched(ABTI_xstream *p_xstream, ABTI_sched *p_sched);
int ABTI_xstream_check_events(ABTI_xstream *p_xstream, ABT_sched sched);
void *ABTI_xstream_launch_main_sched(void *p_arg);
int ABTI_xstream_sleep();
int ABTI_xstream_wakeup(ABTI_xstream *p_xstream);
int ABTI_xstream_check_sleep();
void ABTI_xstream_print(ABTI_xstream *p_xstream, FILE *p_os, int indent,
                        ABT_bool print_sub);

/* Sub Execution Stream (Sub ES) */
int ABTI_sub_xstream_create(ABTI_sub_xstream **pp_sub_xstream, int global_alloc);
int ABTI_sub_xstream_sleep(ABTI_sub_xstream *p_sub_xstream,
                           ABTI_xstream **pp_xstream,
                           void (*callback_fn)(void *), void *p_arg);
int ABTI_sub_xstream_wakeup(ABTI_sub_xstream *p_sub_xstream,
                            ABTI_xstream *p_xstream);
int ABTI_sub_xstream_free(ABTI_sub_xstream *p_sub_xstream);
ABT_bool ABTI_sub_xstream_exit_main(ABTI_thread *p_this_thread);
int ABTI_sub_xstream_back_to_main(ABTI_thread *p_this_thread);
ABTI_sub_xstream *ABTI_sub_xstream_get_data();
int ABTI_sub_xstream_list_local_init(ABTI_sub_xstream_list_local *p_sub_list,
                                     int init_num);
int ABTI_sub_xstream_list_local_free(ABTI_sub_xstream_list_local *p_sub_list);
int ABTI_sub_xstream_list_local_add(ABTI_sub_xstream_list_local *p_sub_list,
                                    ABTI_sub_xstream *p_sub_xstream);
int ABTI_sub_xstream_list_local_pop(ABTI_sub_xstream_list_local *p_sub_list,
                                     ABTI_sub_xstream **pp_sub_xstream);
int ABTI_sub_xstream_list_global_add(ABTI_sub_xstream_list_global *p_sub_list,
                                     ABTI_sub_xstream *p_sub_head,
                                     ABTI_sub_xstream *p_sub_tail);
int ABTI_sub_xstream_list_global_pop(ABTI_sub_xstream_list_global *p_sub_list,
                                     ABTI_sub_xstream **pp_sub_xstream);
int ABTI_sub_xstream_list_global_pop_all(ABTI_sub_xstream_list_global *p_sub_list,
                                         ABTI_sub_xstream **pp_sub_xstream);
int ABTI_sub_xstream_list_add(ABTI_sub_xstream_list_local *p_sub_list,
                              ABTI_sub_xstream *p_sub_xstream);
int ABTI_sub_xstream_list_pop(ABTI_sub_xstream_list_local *p_sub_list,
                              ABTI_sub_xstream **pp_sub_xstream);
int ABTI_sub_xstream_allocator_init(ABTI_sub_xstream_allocator **pp_allocator);
int ABTI_sub_xstream_allocator_free(ABTI_sub_xstream_allocator *p_allocator);
int ABTI_sub_xstream_allocator_req(ABTI_sub_xstream_allocator *p_allocator);

/* Scheduler */
ABT_sched_def *ABTI_sched_get_basic_def(void);
ABT_sched_def *ABTI_sched_get_basic_wait_def(void);
ABT_sched_def *ABTI_sched_get_prio_def(void);
ABT_sched_def *ABTI_sched_get_randws_def(void);
ABT_sched_def *ABTI_sched_get_sub_def(void);
int ABTI_sched_free(ABTI_sched *p_sched);
int ABTI_sched_get_migration_pool(ABTI_sched *, ABTI_pool *, ABTI_pool **);
ABTI_sched_kind ABTI_sched_get_kind(ABT_sched_def *def);
ABT_bool ABTI_sched_has_to_stop(ABTI_sched *p_sched, ABTI_xstream *p_xstream);
size_t ABTI_sched_get_size(ABTI_sched *p_sched);
size_t ABTI_sched_get_total_size(ABTI_sched *p_sched);
size_t ABTI_sched_get_effective_size(ABTI_sched *p_sched);
void ABTI_sched_print(ABTI_sched *p_sched, FILE *p_os, int indent,
                      ABT_bool print_sub);
void ABTI_sched_reset_id(void);

/* Scheduler config */
size_t ABTI_sched_config_type_size(ABT_sched_config_type type);
int ABTI_sched_config_read(ABT_sched_config config, int type, int num_vars,
                           void **variables);
int ABTI_sched_config_read_global(ABT_sched_config config,
                                  ABT_pool_access *access, ABT_bool *automatic);

/* Pool */
int ABTI_pool_get_fifo_def(ABT_pool_access access, ABT_pool_def *p_def);
int ABTI_pool_get_fifo_lockfree_def(ABT_pool_access access, ABT_pool_def *p_def);
int ABTI_pool_get_fifo_wait_def(ABT_pool_access access, ABT_pool_def *p_def);
#ifndef ABT_CONFIG_DISABLE_POOL_CONSUMER_CHECK
int ABTI_pool_set_consumer(ABTI_pool *p_pool, ABTI_xstream *p_xstream);
#endif
#ifndef ABT_CONFIG_DISABLE_POOL_PRODUCER_CHECK
int ABTI_pool_set_producer(ABTI_pool *p_pool, ABTI_xstream *p_xstream);
#endif
int ABTI_pool_accept_migration(ABTI_pool *p_pool, ABTI_pool *source);
void ABTI_pool_print(ABTI_pool *p_pool, FILE *p_os, int indent);
void ABTI_pool_reset_id(void);

/* User-level Thread (ULT)  */
int   ABTI_thread_migrate_to_pool(ABTI_thread *p_thread, ABTI_pool *p_pool);
int   ABTI_thread_create_internal(void (*thread_func)(void *), void *arg,
                                  ABTI_thread **pp_newthread);
int   ABTI_thread_create_main(ABTI_xstream *p_xstream, ABTI_thread **p_thread);
int   ABTI_thread_create_main_sched(ABTI_xstream *p_xstream, ABTI_sched *p_sched);
int   ABTI_thread_create_sched(ABTI_pool *p_pool, ABTI_sched *p_sched);
void  ABTI_thread_free(ABTI_thread *p_thread);
void  ABTI_thread_free_main(ABTI_thread *p_thread);
void  ABTI_thread_free_main_sched(ABTI_thread *p_thread);
int   ABTI_thread_set_blocked(ABTI_thread *p_thread);
void  ABTI_thread_suspend(ABTI_thread *p_thread);
int   ABTI_thread_set_ready(ABTI_thread *p_thread);
void  ABTI_thread_print(ABTI_thread *p_thread, FILE *p_os, int indent);
int   ABTI_thread_print_stack(ABTI_thread *p_thread, FILE *p_os);
#ifndef ABT_CONFIG_DISABLE_MIGRATION
void  ABTI_thread_add_req_arg(ABTI_thread *p_thread, uint32_t req, void *arg);
void *ABTI_thread_extract_req_arg(ABTI_thread *p_thread, uint32_t req);
void  ABTI_thread_put_req_arg(ABTI_thread *p_thread,
                              ABTI_thread_req_arg *p_req_arg);
ABTI_thread_req_arg *ABTI_thread_get_req_arg(ABTI_thread *p_thread,
                                             uint32_t req);
#endif
void  ABTI_thread_retain(ABTI_thread *p_thread);
void  ABTI_thread_release(ABTI_thread *p_thread);
void  ABTI_thread_reset_id(void);
ABT_thread_id ABTI_thread_get_id(ABTI_thread *p_thread);
ABT_thread_id ABTI_thread_self_id(void);
int ABTI_thread_get_xstream_rank(ABTI_thread *p_thread);
int ABTI_thread_self_xstream_rank(void);

/* ULT Attributes */
void ABTI_thread_attr_print(ABTI_thread_attr *p_attr, FILE *p_os, int indent);
void ABTI_thread_attr_get_str(ABTI_thread_attr *p_attr, char *p_buf);
ABTI_thread_attr *ABTI_thread_attr_dup(ABTI_thread_attr *p_attr);

/* ULT hash table */
ABTI_thread_htable *ABTI_thread_htable_create(uint32_t num_rows);
void ABTI_thread_htable_free(ABTI_thread_htable *p_htable);
void ABTI_thread_htable_push(ABTI_thread_htable *p_htable, int idx,
                             ABTI_thread *p_thread);
ABT_bool ABTI_thread_htable_add(ABTI_thread_htable *p_htable, int idx,
                                ABTI_thread *p_thread);
void ABTI_thread_htable_push_low(ABTI_thread_htable *p_htable, int idx,
                             ABTI_thread *p_thread);
ABT_bool ABTI_thread_htable_add_low(ABTI_thread_htable *p_htable, int idx,
                                    ABTI_thread *p_thread);
ABTI_thread *ABTI_thread_htable_pop(ABTI_thread_htable *p_htable,
                                    ABTI_thread_queue *p_queue);
ABTI_thread *ABTI_thread_htable_pop_low(ABTI_thread_htable *p_htable,
                                        ABTI_thread_queue *p_queue);
ABT_bool ABTI_thread_htable_switch_low(ABTI_thread_queue *p_queue,
                                       ABTI_thread *p_thread,
                                       ABTI_thread_htable *p_htable);

/* Tasklet */
int ABTI_task_create_sched(ABTI_pool *p_pool, ABTI_sched *p_sched);
void ABTI_task_free(ABTI_task *p_task);
void ABTI_task_print(ABTI_task *p_task, FILE *p_os, int indent);
void ABTI_task_retain(ABTI_task *p_task);
void ABTI_task_release(ABTI_task *p_task);
void ABTI_task_reset_id(void);
uint64_t ABTI_task_get_id(ABTI_task *p_task);

/* Key */
ABTI_ktable *ABTI_ktable_alloc(int size);
void ABTI_ktable_free(ABTI_ktable *p_ktable);

/* Mutex */
void ABTI_mutex_wait(ABTI_mutex *p_mutex, int val);
void ABTI_mutex_wait_low(ABTI_mutex *p_mutex, int val);
void ABTI_mutex_wake_se(ABTI_mutex *p_mutex, int num);
void ABTI_mutex_wake_de(ABTI_mutex *p_mutex);

/* Mutex Attributes */
void ABTI_mutex_attr_print(ABTI_mutex_attr *p_attr, FILE *p_os, int indent);
void ABTI_mutex_attr_get_str(ABTI_mutex_attr *p_attr, char *p_buf);

/* Event */
void ABTI_event_init(void);
void ABTI_event_finalize(void);
#ifdef ABT_CONFIG_HANDLE_POWER_EVENT
void ABTI_event_connect_power(char *p_host, int port);
void ABTI_event_disconnect_power(void);
ABT_bool ABTI_event_check_power(void);
#endif
#ifdef ABT_CONFIG_PUBLISH_INFO
void ABTI_event_inc_unit_cnt(ABTI_xstream *p_xstream, ABT_unit_type type);
void ABTI_event_publish_info(void);
#endif

/* Preemption timer */
int ABTI_preemption_timer_create(ABTI_xstream *p_xstream);
int ABTI_preemption_timer_delete(ABTI_xstream *p_xstream);

/* Information */
void ABTI_info_check_print_all_thread_stacks(void);

#include "abti_log.h"
#include "abti_event.h"
#include "abti_local.h"
#include "abti_global.h"
#include "abti_pool.h"
#include "abti_sched.h"
#include "abti_config.h"
#include "abti_stream.h"
#include "abti_self.h"
#include "abti_thread.h"
#include "abti_thread_attr.h"
#include "abti_task.h"
#include "abti_key.h"
#include "abti_mutex.h"
#include "abti_mutex_attr.h"
#include "abti_cond.h"
#include "abti_rwlock.h"
#include "abti_eventual.h"
#include "abti_future.h"
#include "abti_barrier.h"
#include "abti_timer.h"
#include "abti_mem.h"
#include "abti_mlog.h"

#endif /* ABTI_H_INCLUDED */
