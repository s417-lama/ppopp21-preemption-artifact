/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <pthread.h>

////////////////////////////////////////////////////////////////////////////////
// Configurable parameters
////////////////////////////////////////////////////////////////////////////////

typedef void *pool_t;

#define likely(cond)    __builtin_expect(!!(cond), 1)
#define unlikely(cond)  __builtin_expect(!!(cond), 0)

// Block size
#define BLOCKSIZE_LOG 6
#define BLOCKSIZE (1 << BLOCKSIZE_LOG)

// Cache line size
#define CACHELINE_SIZE 128

// Local pool capacity
#define LOCALPOOL_NUM_BLOCKS 4
// Number of blocks that are taken from a global pool. It should be smaller than LOCALPOOL_NUM_BLOCKS
#define GLOBAL_TO_LOCAL_NUM_BLOCKS 2

#if LOCALPOOL_NUM_BLOCKS <= GLOBAL_TO_LOCAL_NUM_BLOCKS
#error "LOCALPOOL_NUM_BLOCKS must be smaller than GLOBAL_TO_LOCAL_NUM_BLOCKS."
#endif

// Number of blocks that are returned to a global pool. It should be smaller than LOCALPOOL_NUM_BLOCKS
#define LOCAL_TO_GLOBAL_NUM_BLOCKS 2

#if LOCALPOOL_NUM_BLOCKS <= LOCAL_TO_GLOBAL_NUM_BLOCKS
#error "LOCALPOOL_NUM_BLOCKS must be smaller than LOCAL_TO_GLOBAL_NUM_BLOCKS."
#endif

#define MALLOC_ALIGNMENT CACHELINE_SIZE

// Define if page-level allocation is used.
// Page-level allocation
//   Pros: First-time allocation becomes fast.
//   Cons: Memory is never freed once allocated until pool itself gets freed.
// Object-level allocation
//   Pros: Memory can be freed if it gets beyond the threshold.
//   Cons: First-time allocation is slow.

#define USE_PAGE

#ifdef USE_PAGE

// Bulk size (bytes).
#define PAGESIZE (32 * 1024 * 1024)

#else

// The capacity of the global pool. It should be larger than GLOBAL_TO_LOCAL_NUM_BLOCKS and LOCAL_TO_GLOBAL_NUM_BLOCKS
#define GLOBALPOOL_NUM_BLOCKS 8

#if GLOBALPOOL_NUM_BLOCKS <= GLOBAL_TO_LOCAL_NUM_BLOCKS
#error "GLOBALPOOL_NUM_BLOCKS must be larger than GLOBAL_TO_LOCAL_NUM_BLOCKS."
#endif

#if GLOBALPOOL_NUM_BLOCKS <= LOCAL_TO_GLOBAL_NUM_BLOCKS
#error "GLOBALPOOL_NUM_BLOCKS must be larger than LOCAL_TO_GLOBAL_NUM_BLOCKS."
#endif

#endif // USE_PAGE

// Define if array-based block management is used.
// Array-based block management
//   Pros: No additional data segment is needed.
//   Cons: Block size is large, so copying blocks is costly.
// Linked list-based block management
//   Pros: Block size is small, so copying blocks is lightweight.
//   Cons: Each element has an additional data segment for linked list.

#define USE_ARRAY_BASED_BLOCK

////////////////////////////////////////////////////////////////////////////////
// Utility
////////////////////////////////////////////////////////////////////////////////

static inline void lock_acquire(int *p_int_lock) {
    // The caller assumes a spinlock. Do not yield inside when ULT is used.
    char *p_lock = (char *)p_int_lock;
    while (__atomic_test_and_set(p_lock, __ATOMIC_ACQ_REL) == 1) {
        while (__atomic_load_n(p_lock, __ATOMIC_ACQUIRE) != 0) {
#if defined(__GNUC__) && defined(__x86_64__)
            __asm__ __volatile__ ("pause");
#endif
        }
    }
}

static inline void lock_release(int *p_int_lock) {
    // The caller expects immediate return after unlock. Do not yield inside when ULT is used.
    char *p_lock = (char *)p_int_lock;
    __atomic_clear(p_lock, __ATOMIC_RELEASE);
}

static inline void *aligned_malloc(size_t size) {
    size_t alloc_size = (size + CACHELINE_SIZE - 1) & (~(CACHELINE_SIZE - 1));
    void *mem;
    int ret = posix_memalign(&mem, CACHELINE_SIZE, alloc_size);
    if (likely(ret == 0))
        return mem;
    return NULL;
}

static inline void *aligned_calloc(size_t size) {
    void *mem = aligned_malloc(size);
    if (likely(mem)) {
        memset(mem, 0, size);
        return mem;
    }
    return NULL;
}

static inline void aligned_free(void *ptr) {
    free(ptr);
}

static inline void *aligned_realloc(void *ptr, size_t old_size, size_t new_size) {
    void *mem = aligned_malloc(new_size);
    memcpy(mem, ptr, old_size);
    aligned_free(ptr);
    return mem;
}

////////////////////////////////////////////////////////////////////////////////
// Extensible thread-local entry
////////////////////////////////////////////////////////////////////////////////

struct local_pool;
static struct local_pool *local_pool_create();
static void local_pool_free(struct local_pool *p_local_pool);

struct thread_local_entry { // thread local
    size_t local_pools_len;
    // read local_pools and local_pools[x]  : no lock
    // update local_pools and local_pools[x]: thread_local_entry_manager.lock
    struct local_pool **local_pools;
} __attribute__((aligned(CACHELINE_SIZE)));

__attribute__((aligned(CACHELINE_SIZE))) __thread struct thread_local_entry *lp_thread_local_entry;

struct thread_local_entry_manager { // global
    // The lock owner may not yield when ULT is used.
    int lock;
    __attribute__((aligned(CACHELINE_SIZE)))
    // All these variables require thread_local_entry_manager.lock to read or update.
    size_t entry_list_len;
    char *entry_list;
    size_t thread_local_entries_len;
    struct thread_local_entry **thread_local_entries;
    int is_initialized_thread_local_entry_key;
    pthread_key_t thread_local_entry_key;
    int num_alive_threads;
} __attribute__((aligned(CACHELINE_SIZE)));

__attribute__((aligned(CACHELINE_SIZE))) struct thread_local_entry_manager g_thread_local_entry_manager;

void thread_local_entry_destructor(void *value) {
    struct thread_local_entry *p_entry = (struct thread_local_entry *)value;
    struct thread_local_entry_manager *p_entry_manager = &g_thread_local_entry_manager;
    lock_acquire(&p_entry_manager->lock);
    // Remove this entry from the p_entry_manager.
    for (int i = 0, iend = p_entry_manager->thread_local_entries_len; i < iend; i++) {
        if (p_entry_manager->thread_local_entries[i] == p_entry) {
            p_entry_manager->thread_local_entries[i] = NULL;
            break;
        }
    }
    p_entry_manager->num_alive_threads--;
    if (p_entry_manager->num_alive_threads == 0) {
        // Free dynamically allocated objects.
        // When all entries are freed, no other threads access thread local objects, so we can safely free all the objects.
        aligned_free(p_entry_manager->entry_list);
        p_entry_manager->entry_list = NULL;
        p_entry_manager->entry_list_len = 0;
        aligned_free(p_entry_manager->thread_local_entries);
        p_entry_manager->thread_local_entries = NULL;
        p_entry_manager->thread_local_entries_len = 0;
    }
    lock_release(&p_entry_manager->lock);
    // Free local pools.
    for (int entry_index = 0; entry_index < p_entry->local_pools_len; entry_index++) {
        if (p_entry->local_pools[entry_index])
            local_pool_free(p_entry->local_pools[entry_index]);
    }
    if (p_entry->local_pools)
        aligned_free(p_entry->local_pools);
    aligned_free(p_entry);
}

static inline struct local_pool *get_thread_local_pool(int entry_index) {
    // First, check its thread_local_entry
    struct thread_local_entry *p_entry = lp_thread_local_entry;
    if (unlikely(!p_entry)) {
        // Allocate a new local entry.
        struct thread_local_entry_manager *p_entry_manager = &g_thread_local_entry_manager;
        p_entry = (struct thread_local_entry *)aligned_calloc(sizeof(struct thread_local_entry));
        // Save a key to free p_entry when the caller dies.
        lock_acquire(&p_entry_manager->lock);
        if (!p_entry_manager->is_initialized_thread_local_entry_key) {
            pthread_key_create(&p_entry_manager->thread_local_entry_key, thread_local_entry_destructor);
            p_entry_manager->is_initialized_thread_local_entry_key = 1;
        }
        p_entry_manager->num_alive_threads++;
        pthread_setspecific(p_entry_manager->thread_local_entry_key, p_entry);
        lock_release(&p_entry_manager->lock);
        lp_thread_local_entry = p_entry;
    }
    if (unlikely(p_entry->local_pools_len <= entry_index)) {
        // Allocate or extend local_pools. This allocation is serialized.
        struct thread_local_entry_manager *p_entry_manager = &g_thread_local_entry_manager;
        lock_acquire(&p_entry_manager->lock);
        if (!p_entry->local_pools) {
            // Allocate new local_pools.
            size_t new_len = entry_index + 1;
            p_entry->local_pools = (struct local_pool **)aligned_calloc(sizeof(struct local_pool *) * new_len);
            p_entry->local_pools_len = new_len;
            int is_inserted = 0;
            for (int i = 0; i < p_entry_manager->thread_local_entries_len; i++) {
                if (!p_entry_manager->thread_local_entries[i]) {
                    p_entry_manager->thread_local_entries[i] = p_entry;
                    is_inserted = 1;
                    break;
                }
            }
            if (!is_inserted) {
                // Extend thread_local_entries_len.
                size_t old_thread_local_entries_len = p_entry_manager->thread_local_entries_len;
                size_t new_thread_local_entries_len = (old_thread_local_entries_len == 0) ? 2 : (old_thread_local_entries_len * 2);
                p_entry_manager->thread_local_entries = (struct thread_local_entry **)aligned_realloc(p_entry_manager->thread_local_entries, sizeof(struct thread_local_entry *) * old_thread_local_entries_len, sizeof(struct thread_local_entry *) * new_thread_local_entries_len);
                memset(p_entry_manager->thread_local_entries + old_thread_local_entries_len, 0, sizeof(struct thread_local_entry *) * (new_thread_local_entries_len - old_thread_local_entries_len));
                p_entry_manager->thread_local_entries_len = new_thread_local_entries_len;
                p_entry_manager->thread_local_entries[old_thread_local_entries_len] = p_entry;
            }
        } else {
            const size_t old_len = p_entry->local_pools_len;
            if (old_len <= entry_index) {
                // Extend new local_pools.
                size_t new_len = entry_index + 1;
                struct local_pool **new_local_pools = (struct local_pool **)aligned_realloc(p_entry->local_pools, sizeof(struct local_pool *) * old_len, sizeof(struct local_pool *) * new_len);
                memset(new_local_pools + old_len, 0, sizeof(struct local_pool *) * (new_len - old_len));
                p_entry->local_pools_len = new_len;
                p_entry->local_pools = new_local_pools;
            }
        }
        lock_release(&p_entry_manager->lock);
    }
    // Next, check if local_pools[entry_index] has an entity.
    struct local_pool **local_pools = p_entry->local_pools;
    struct local_pool *local_pool = local_pools[entry_index];
    if (unlikely(!local_pool)) {
        // Allocate and initialize local_pools[entry_index].
        local_pool = local_pool_create();
        local_pools[entry_index] = local_pool;
    }
    return local_pool;
}

// Return newly created entry_index.
static int create_new_entry() {
    struct thread_local_entry_manager *p_entry_manager = &g_thread_local_entry_manager;
    lock_acquire(&p_entry_manager->lock);
    size_t entry_list_len = p_entry_manager->entry_list_len;
    if (entry_list_len == 0) {
        // Allocate entry_list.
        entry_list_len = 8;
        p_entry_manager->entry_list = (char *)aligned_calloc(sizeof(char) * entry_list_len);
        p_entry_manager->entry_list_len = entry_list_len;
    }
    while (1) {
        // Allocate entry_list.
        char *entry_list = p_entry_manager->entry_list;
        for (int entry_index = 0; entry_index < entry_list_len; entry_index++) {
            if (entry_list[entry_index] == 0) {
                entry_list[entry_index] = 1;
                lock_release(&p_entry_manager->lock);
                return entry_index;
            }
        }
        // Extend entry list.
        size_t new_entry_list_len = entry_list_len * 2;
        entry_list = (char *)aligned_realloc(entry_list, entry_list_len, new_entry_list_len);
        memset(entry_list + entry_list_len, 0, sizeof(char) * (new_entry_list_len - entry_list_len));
        p_entry_manager->entry_list = entry_list;
        p_entry_manager->entry_list_len = new_entry_list_len;
        entry_list_len = new_entry_list_len;
    }
    // Unreachable.
}

// Destroy an entry.
static void free_entry(int entry_index) {
    struct thread_local_entry_manager *p_entry_manager = &g_thread_local_entry_manager;
    lock_acquire(&p_entry_manager->lock);
    // Free entry_index.
    p_entry_manager->entry_list[entry_index] = 0;
    // Free all local objects.
    for (int i = 0, iend = p_entry_manager->thread_local_entries_len; i < iend; i++) {
        struct thread_local_entry *p_entry = p_entry_manager->thread_local_entries[i];
        if (p_entry && entry_index < p_entry->local_pools_len && p_entry->local_pools[entry_index]) {
            local_pool_free(p_entry->local_pools[entry_index]);
            p_entry->local_pools[entry_index] = NULL;
        }
    }
    lock_release(&p_entry_manager->lock);
}

////////////////////////////////////////////////////////////////////////////////
// Synchronization-free-read extensible array.
////////////////////////////////////////////////////////////////////////////////

struct extensible_array {
    // This array will be freed if all values are set to NULL.
    void **values; // This array is extensible.
    int lock;
    int length;
    void *next_unused;
    // values has the following structure.
    // | len (8 bytes) | values (sizeof(void *) * len bytes)| next (8 bytes)
    //                 ^ pointed by values.
    // next points to the head of buffer, not head of values.
};

static inline void *allocate_extensible_array_values(size_t length) {
    char *values_buffer = aligned_malloc(length * sizeof(void *) + 16);
    *((uint64_t *)values_buffer) = (uint64_t)length;
    *((void **)(values_buffer + sizeof(void *) * length + 8)) = NULL;
    return (void *)(values_buffer + 8);
}

static void free_extensible_array(struct extensible_array *p_array, int index) {
    // Take a lock and check if no one uses it.
    lock_acquire(&p_array->lock);
    p_array->values[index] = NULL;
    int is_empty = 1;
    for (int i = 0; i < p_array->length; i++) {
        if (p_array->values[i] != NULL) {
            is_empty = 0;
            break;
        }
    }
    if (is_empty) {
        aligned_free(((char *)p_array->values) - 8);
        void *next_unused = p_array->next_unused;
        while (next_unused) {
            void *prev_unused = next_unused;
            size_t length = *((uint64_t *)prev_unused);
            next_unused = *(void **)(((char *)prev_unused) + sizeof(void *) * length + 8);
            aligned_free(prev_unused);
        }
        p_array->values = NULL;
        p_array->length = 0;
        p_array->next_unused = NULL;
    }
    lock_release(&p_array->lock);
}

static void write_extensible_array(struct extensible_array *p_array, int index, void *value) {
    assert(value != NULL);
    // Take a lock and update it.
    lock_acquire(&p_array->lock);
    if (p_array->length <= index) {
        int new_length = (p_array->length * 2 <= (index + 1)) ? (index + 1) : (p_array->length * 2);
        // Create new values and replace the old one.
        void **new_values = allocate_extensible_array_values(new_length);
        // Initialize new_values.
        memcpy(new_values, p_array->values, sizeof(void *) * p_array->length);
        memset(new_values + p_array->length, 0, sizeof(void *) * (new_length - p_array->length));
        // NEVER free the old values (since other threads might be accessing them)
        // Instead, add the old values to next_unused.
        if (p_array->values) {
            char *old_values = (((char *)p_array->values) - 8);
            *(void **)(old_values + sizeof(void *) * (*((uint64_t*)old_values)) + 8) = p_array->next_unused;
            p_array->next_unused = old_values;
        }
        // Update values first.
        p_array->values = new_values;
        // Update length at last.
        __atomic_store_n(&p_array->length, new_length, __ATOMIC_RELEASE);
    }
    // Write values.
    p_array->values[index] = value;
    lock_release(&p_array->lock);
}

////////////////////////////////////////////////////////////////////////////////
// Pools
////////////////////////////////////////////////////////////////////////////////

struct element {
#ifndef USE_ARRAY_BASED_BLOCK
    struct element *next;
    // element is the first sizeof(element) bytes of an element.
#endif
};

#ifdef USE_PAGE
struct memory_bulk {
    struct memory_bulk *next;
    // memory_bulk is the first sizeof(memory_bulk) bytes of a page.
} __attribute__((aligned(CACHELINE_SIZE)));
#endif

struct block {
#ifdef USE_ARRAY_BASED_BLOCK
    struct element *elements[BLOCKSIZE]; // This is part of block, so never freed.
#else
    struct element *head;
    struct element *tail;
#endif
};

struct local_pool {
    int num_elements;
#ifdef USE_PAGE
    size_t extra_mem_size;
    void *p_extra_mem_ptr;
#endif
    struct block blocks[LOCALPOOL_NUM_BLOCKS];
} __attribute__((aligned(CACHELINE_SIZE)));

struct global_pool {
    int entry_index;
    size_t element_size;
    __attribute__((aligned(CACHELINE_SIZE)))
    int lock;
    __attribute__((aligned(CACHELINE_SIZE)))
    int num_elements;
    int len_blocks;
    struct block *blocks;
#ifdef USE_PAGE
    // This is not protected by lock; accessed by swapping.
    __attribute__((aligned(CACHELINE_SIZE)))
    struct memory_bulk *bulk;
#endif
} __attribute__((aligned(CACHELINE_SIZE)));

struct global_pools {
    struct extensible_array pools;
} __attribute__((aligned(CACHELINE_SIZE)));

__attribute__((aligned(CACHELINE_SIZE))) struct global_pools g_global_pools;

#ifdef USE_PAGE
static inline void atomic_insert_bulk(struct global_pool *p_global_pool, struct memory_bulk *bulk) {
    while (1) {
        struct memory_bulk *cur_bulk = p_global_pool->bulk;
        bulk->next = cur_bulk;
        if (__atomic_compare_exchange_n(&p_global_pool->bulk, &cur_bulk, bulk, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}
#endif

static inline void *element_to_ptr(struct element *element) {
#ifdef USE_ARRAY_BASED_BLOCK
    return (void *)element;
#else
    return (void *)(((char *)element) + sizeof(struct element));
#endif
}

static inline struct element *ptr_to_element(void *ptr) {
#ifdef USE_ARRAY_BASED_BLOCK
    return (struct element *)ptr;
#else
    return (struct element *)(((char *)ptr) - sizeof(struct element));
#endif
}

static struct local_pool *local_pool_create() {
    struct local_pool *local_pool = (struct local_pool *)aligned_calloc(sizeof(struct local_pool));
    return local_pool;
}

static void local_pool_free(struct local_pool *p_local_pool) {
#ifndef USE_PAGE
    // Free all objects.
    const size_t blocki_from = 0;
    const size_t blocki_to = (p_local_pool->num_elements + BLOCKSIZE - 1) >> BLOCKSIZE_LOG;
    for (int block_i = blocki_from; block_i < blocki_to; block_i++) {
        struct element *element = p_local_pool->blocks[block_i].head;
        while(element) {
            struct element *next = element->next;
            aligned_free(element);
            element = next;
        }
    }
#endif
    aligned_free(p_local_pool);
}

int pool_create(size_t element_size, pool_t *handle) {
    struct global_pool *p_global_pool = (struct global_pool *)aligned_calloc(sizeof(struct global_pool));
    const int entry_index = create_new_entry();
    p_global_pool->entry_index = entry_index;
    p_global_pool->element_size = element_size;
    write_extensible_array(&g_global_pools.pools, entry_index, (void *)p_global_pool);
    *handle = (pool_t)((intptr_t)entry_index);
    return 0;
}

int pool_destroy(pool_t *handle) {
    const int entry_index = (int)((intptr_t)*handle);
    struct global_pool *p_global_pool = (void *)g_global_pools.pools.values[entry_index];
    // Free lock.
    p_global_pool->lock = 0;
#ifdef USE_PAGE
    // Free bulks.
    struct memory_bulk *bulk = p_global_pool->bulk;
    while (bulk) {
        struct memory_bulk *next = bulk->next;
        free(bulk);
        bulk = next;
    }
#else
    // Free all objects.
    const size_t blocki_from = 0;
    const size_t blocki_to = (p_global_pool->num_elements + BLOCKSIZE - 1) >> BLOCKSIZE_LOG;
    for (int block_i = blocki_from; block_i < blocki_to; block_i++) {
        struct element *element = p_global_pool->blocks[block_i].head;
        while(element) {
            struct element *next = element->next;
            aligned_free(element);
            element = next;
        }
    }
#endif
    // Free blocks.
    free(p_global_pool->blocks);
    // Free local pools.
    free_entry(entry_index);
    free_extensible_array(&g_global_pools.pools, entry_index);
    free(p_global_pool);
    return 0;
}

int pool_alloc(pool_t handle, void **ptr) {
    const int entry_index = (int)((intptr_t)handle);
    struct local_pool *local_pool = get_thread_local_pool(entry_index);
    if (unlikely(local_pool->num_elements == 0)) {
        struct global_pool *global_pool = (struct global_pool *)g_global_pools.pools.values[entry_index];
        int num_taken_blocks = 0;
        size_t element_size = 0;
        // Critical section. Take blocks from a global pool.
        lock_acquire(&global_pool->lock);
        {
            int num_global_pool_full_blocks = global_pool->num_elements >> BLOCKSIZE_LOG;
            num_taken_blocks = (num_global_pool_full_blocks < GLOBAL_TO_LOCAL_NUM_BLOCKS) ? num_global_pool_full_blocks : GLOBAL_TO_LOCAL_NUM_BLOCKS;
            if (num_taken_blocks != 0) {
                int global_block_i = global_pool->num_elements >> BLOCKSIZE_LOG;
                memcpy(local_pool->blocks, &global_pool->blocks[global_block_i - num_taken_blocks], sizeof(struct block) * num_taken_blocks);
                // local_pool->num_elements = BLOCKSIZE * num_taken_blocks; // Update later.
                global_pool->num_elements -= BLOCKSIZE * num_taken_blocks;
                if ((global_pool->num_elements & (BLOCKSIZE - 1)) != 0) { // global_pool->num_elements % BLOCKSIZE != 0
                    // Copy the last block of global_pool and clear the remaining blocks.
                    memcpy(&global_pool->blocks[global_block_i - num_taken_blocks], &global_pool->blocks[global_block_i], sizeof(struct block));
                    // memset(&global_pool->blocks[global_block_i - num_taken_blocks + 1], 0, sizeof(struct block) * (global_pool->len_blocks - (global_block_i - num_taken_blocks + 1)));
                } else {
                    // Clear the remaining blocks.
                    // memset(&global_pool->blocks[global_block_i - num_taken_blocks], 0, sizeof(struct block) * (global_pool->len_blocks - (global_block_i - num_taken_blocks)));
                }
            }
            if (GLOBAL_TO_LOCAL_NUM_BLOCKS != num_taken_blocks) {
#ifdef USE_ARRAY_BASED_BLOCK
                element_size = (global_pool->element_size + MALLOC_ALIGNMENT - 1) & (~(MALLOC_ALIGNMENT - 1));
#else
                element_size = (sizeof(struct element) + global_pool->element_size + MALLOC_ALIGNMENT - 1) & (~(MALLOC_ALIGNMENT - 1));
#endif
            }
        }
        lock_release(&global_pool->lock);
        int num_remaining_blocks = GLOBAL_TO_LOCAL_NUM_BLOCKS - num_taken_blocks;
        if (num_remaining_blocks) {
            int num_remaining_elements = num_remaining_blocks << BLOCKSIZE_LOG;
            assert(element_size);
            // Allocate remaining blocks.
#ifdef USE_PAGE
            // Use extra_mem_ptr.
            int block_i = num_taken_blocks;
            struct block *blocks = local_pool->blocks;
            size_t extra_mem_size = local_pool->extra_mem_size;
            char *p_extra_mem_ptr = (char *)local_pool->p_extra_mem_ptr;

            struct block *block = &blocks[block_i];
            int block_num_elements = 0;
#ifdef USE_ARRAY_BASED_BLOCK
            struct element **block_elements = block->elements;
#else
            struct element *block_head = NULL;
            struct element *block_tail = NULL;
#endif
            while (num_remaining_elements) {
                if (!p_extra_mem_ptr) {
                    // Allocate new page and extract elements from it.
                    extra_mem_size = PAGESIZE;
                    p_extra_mem_ptr = (char *)aligned_malloc(extra_mem_size);
                    struct memory_bulk *bulk = (struct memory_bulk *)p_extra_mem_ptr;
                    p_extra_mem_ptr += sizeof(struct memory_bulk);
                    extra_mem_size -= sizeof(struct memory_bulk);
                    // Add a new bulk.
                    atomic_insert_bulk(global_pool, bulk);
                }
                while (num_remaining_elements && extra_mem_size >= element_size) {
                    struct element *new_element = (struct element *)p_extra_mem_ptr;
                    p_extra_mem_ptr += element_size;
                    extra_mem_size -= element_size;
#ifdef USE_ARRAY_BASED_BLOCK
                    block_elements[block_num_elements] = new_element;
#else
                    new_element->next = NULL;
                    if (block_num_elements == 0) {
                        block_head = new_element;
                        block_tail = new_element;
                    } else {
                        block_tail->next = new_element;
                        block_tail = new_element;
                    }
#endif
                    block_num_elements++;
                    if (block_num_elements == BLOCKSIZE) {
#ifndef USE_ARRAY_BASED_BLOCK
                        block->head = block_head;
                        block->tail = block_tail;
#endif
                        block_i++;
                        block = &blocks[block_i];
                        block_num_elements = 0;
#ifdef USE_ARRAY_BASED_BLOCK
                        block_elements = block->elements;
#endif
                    }
                    num_remaining_elements--;
                }
                if (extra_mem_size < element_size) {
                    extra_mem_size = 0;
                    p_extra_mem_ptr = NULL;
                }
            }
            local_pool->extra_mem_size = extra_mem_size;
            local_pool->p_extra_mem_ptr = p_extra_mem_ptr;
            assert(block_num_elements == 0);
#else // !USE_PAGE
            int block_i = num_taken_blocks;
            struct block *blocks = local_pool->blocks;
            struct block *block = &blocks[block_i];
            int block_num_elements = 0;
#ifdef USE_ARRAY_BASED_BLOCK
            struct element **block_elements = block->elements;
#else
            struct element *block_head = NULL;
            struct element *block_tail = NULL;
#endif
            for (int i = 0; i < num_remaining_elements; i++) {
                // Allocate all single elements.
                struct element *new_element = (struct element *)aligned_malloc(element_size);
                // Put it to a tail block.
#ifdef USE_ARRAY_BASED_BLOCK
                block_elements[block_num_elements] = new_element;
#else
                new_element->next = NULL;
                if (block_num_elements == 0) {
                    block_head = new_element;
                    block_tail = new_element;
                } else {
                    block_tail->next = new_element;
                    block_tail = new_element;
                }
#endif
                block_num_elements++;
                if (block_num_elements == BLOCKSIZE) {
#ifndef USE_ARRAY_BASED_BLOCK
                    block->head = block_head;
                    block->tail = block_tail;
#endif
                    block_i++;
                    block = &blocks[block_i];
                    block_num_elements = 0;
#ifdef USE_ARRAY_BASED_BLOCK
                    block_elements = block->elements;
#endif
                }
            }
            assert(block_num_elements == 0);
#endif // USE_PAGE
        }
        local_pool->num_elements = GLOBAL_TO_LOCAL_NUM_BLOCKS * BLOCKSIZE; // Update local_pool variable here.
    }
    {
        // Take an element from a local pool.
        int local_pool_num_elements = --local_pool->num_elements;
        int block_i = local_pool_num_elements >> BLOCKSIZE_LOG;
        int block_num_elements = local_pool_num_elements & (BLOCKSIZE - 1);
        struct block *block = &local_pool->blocks[block_i];
#ifdef USE_ARRAY_BASED_BLOCK
        struct element *element = block->elements[block_num_elements];
#else
        struct element *element = block->head;
        struct element *next = element->next;
        block->head = next;
#endif
        *ptr = element_to_ptr(element);
    }
    return 0;
}

int pool_free(pool_t handle, void *ptr) {
    const int entry_index = (int)((intptr_t)handle);
    struct local_pool *local_pool = get_thread_local_pool(entry_index);
    if (unlikely(local_pool->num_elements == LOCALPOOL_NUM_BLOCKS * BLOCKSIZE)) {
        struct global_pool *global_pool = (struct global_pool *)g_global_pools.pools.values[entry_index];
        lock_acquire(&global_pool->lock);
        int len_required_blocks = (global_pool->num_elements + BLOCKSIZE - 1 + LOCAL_TO_GLOBAL_NUM_BLOCKS * BLOCKSIZE) >> BLOCKSIZE_LOG;
        struct block* blocks = global_pool->blocks;
        if (global_pool->len_blocks < len_required_blocks) {
            // Extend blocks.
            const int len_blocks = global_pool->len_blocks;
            int new_len_blocks = (len_blocks * 2 < len_required_blocks) ? len_required_blocks : (len_blocks * 2);
            struct block *new_blocks = (struct block *)aligned_realloc(blocks, sizeof(struct block) * len_blocks, sizeof(struct block) * new_len_blocks);
            // memset(&new_blocks[len_blocks], 0, sizeof(struct block) * (new_len_blocks - len_blocks));
            global_pool->len_blocks = new_len_blocks;
            blocks = new_blocks;
            global_pool->blocks = new_blocks;
        }
        // Return blocks to a global pool.
        {
            int block_i = global_pool->num_elements >> BLOCKSIZE_LOG;
            if ((global_pool->num_elements & (BLOCKSIZE - 1)) != 0) { // global_pool->num_elements % BLOCKSIZE != 0
                // Copy the last block of a global pool.
                memcpy(&blocks[block_i + LOCAL_TO_GLOBAL_NUM_BLOCKS], &blocks[block_i], sizeof(struct block));
            }
            memcpy(&blocks[block_i], &local_pool->blocks[LOCALPOOL_NUM_BLOCKS - LOCAL_TO_GLOBAL_NUM_BLOCKS], sizeof(struct block) * LOCAL_TO_GLOBAL_NUM_BLOCKS);
        }
        global_pool->num_elements += LOCAL_TO_GLOBAL_NUM_BLOCKS * BLOCKSIZE;
#ifndef USE_PAGE
        // Free objects if # of blocks is more than GLOBALPOOL_NUM_BLOCKS.
        if (global_pool->num_elements > GLOBALPOOL_NUM_BLOCKS * BLOCKSIZE) {
            const size_t blocki_from = GLOBALPOOL_NUM_BLOCKS;
            const size_t blocki_to = (global_pool->num_elements + BLOCKSIZE - 1) >> BLOCKSIZE_LOG;
            for (int block_i = blocki_from; block_i < blocki_to; block_i++) {
                struct element *element = blocks[block_i].head;
                while(element) {
                    struct element *next = element->next;
                    aligned_free(element);
                    element = next;
                }
            }
            global_pool->num_elements = GLOBALPOOL_NUM_BLOCKS * BLOCKSIZE;
        }
#endif
        lock_release(&global_pool->lock);
        // Local work which can be done outside the critical section.
        // memset(&local_pool->blocks[LOCALPOOL_NUM_BLOCKS - LOCAL_TO_GLOBAL_NUM_BLOCKS], 0, sizeof(struct block) * LOCAL_TO_GLOBAL_NUM_BLOCKS);
        local_pool->num_elements -= LOCAL_TO_GLOBAL_NUM_BLOCKS * BLOCKSIZE;
    }
    {
        // Return an element to a local pool.
        int block_i = local_pool->num_elements >> BLOCKSIZE_LOG;
        int local_pool_num_elements = local_pool->num_elements++;
        int block_num_elements = local_pool_num_elements & (BLOCKSIZE - 1);
        struct block *block = &local_pool->blocks[block_i];
        struct element *element = ptr_to_element(ptr);
#ifdef USE_ARRAY_BASED_BLOCK
        block->elements[block_num_elements] = element;
#else
        if (unlikely(block_num_elements == 0)) {
            block->head = element;
            block->tail = element;
            element->next = NULL;
        } else {
            element->next = block->head;
            block->head = element;
        }
#endif
    }
    return 0;
}

