#include "buddy.h"
#include <stdlib.h>
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096

// Free list structure
typedef struct free_node {
    struct free_node *next;
    struct free_node *prev;
} free_node_t;

// Global state
static void *base_addr = NULL;
static int total_pages = 0;
static free_node_t *free_lists[MAX_RANK + 1]; // free_lists[1] to free_lists[16]
static int free_counts[MAX_RANK + 1]; // Count of free blocks at each rank
static char *alloc_rank; // Track the rank of each allocated block (0 = free)
static char *block_rank; // Track the rank of each block in free list

// Helper functions
static inline int pages_in_rank(int rank) {
    return 1 << (rank - 1);
}

static inline int page_index(void *p) {
    return ((char *)p - (char *)base_addr) / PAGE_SIZE;
}

static inline void *page_addr(int idx) {
    return (char *)base_addr + idx * PAGE_SIZE;
}

static inline int buddy_index(int idx, int rank) {
    int pages = pages_in_rank(rank);
    return idx ^ pages;
}

static inline int is_valid_address(void *p) {
    if (p < base_addr) return 0;
    long offset = (char *)p - (char *)base_addr;
    if (offset < 0 || offset >= (long)total_pages * PAGE_SIZE) return 0;
    if (offset % PAGE_SIZE != 0) return 0;
    return 1;
}

// Add block to free list
static inline void add_to_free_list(int idx, int rank) {
    free_node_t *node = (free_node_t *)page_addr(idx);
    node->next = free_lists[rank];
    node->prev = NULL;
    if (free_lists[rank]) {
        free_lists[rank]->prev = node;
    }
    free_lists[rank] = node;
    block_rank[idx] = rank;
    free_counts[rank]++;
}

// Remove block from free list
static inline void remove_from_free_list(int idx, int rank) {
    free_node_t *node = (free_node_t *)page_addr(idx);

    if (node->prev) {
        node->prev->next = node->next;
    } else {
        free_lists[rank] = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    }

    block_rank[idx] = 0;
    free_counts[rank]--;
}

// Initialize the buddy system
int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;

    // Clear free lists and counts
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
        free_counts[i] = 0;
    }

    // Allocate arrays
    alloc_rank = (char *)malloc(pgcount);
    block_rank = (char *)malloc(pgcount);
    for (int i = 0; i < pgcount; i++) {
        alloc_rank[i] = 0;
        block_rank[i] = 0;
    }

    // Add all pages to appropriate free lists
    int idx = 0;
    while (idx < pgcount) {
        // Find the largest rank that fits
        int rank = MAX_RANK;
        while (rank > 0) {
            int pages = pages_in_rank(rank);
            if (idx + pages <= pgcount && (idx % pages) == 0) {
                break;
            }
            rank--;
        }

        if (rank > 0) {
            add_to_free_list(idx, rank);
            idx += pages_in_rank(rank);
        } else {
            idx++;
        }
    }

    return OK;
}

// Allocate pages
void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }

    // Find a free block of the requested rank or larger
    int current_rank = rank;
    while (current_rank <= MAX_RANK && free_lists[current_rank] == NULL) {
        current_rank++;
    }

    if (current_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC);
    }

    // Remove the block from the free list
    int idx = page_index((void *)free_lists[current_rank]);
    remove_from_free_list(idx, current_rank);

    // Split the block if necessary
    while (current_rank > rank) {
        current_rank--;
        int buddy_idx = buddy_index(idx, current_rank);
        add_to_free_list(buddy_idx, current_rank);
    }

    // Mark allocation
    alloc_rank[idx] = rank;

    return page_addr(idx);
}

// Return pages
int return_pages(void *p) {
    if (!is_valid_address(p)) {
        return -EINVAL;
    }

    int idx = page_index(p);

    // Check if the page is allocated
    int rank = alloc_rank[idx];
    if (rank == 0) {
        return -EINVAL;
    }

    // Clear allocation
    alloc_rank[idx] = 0;

    // Try to merge with buddy
    while (rank < MAX_RANK) {
        int buddy_idx = buddy_index(idx, rank);

        // Check if buddy is in bounds
        if (buddy_idx < 0 || buddy_idx >= total_pages) break;

        int pages = pages_in_rank(rank);
        if (buddy_idx + pages > total_pages) break;

        // Check if buddy is free and at the same rank
        if (alloc_rank[buddy_idx] != 0) break;
        if (block_rank[buddy_idx] != rank) break;

        // Remove buddy from free list
        remove_from_free_list(buddy_idx, rank);

        // Merge - use lower address
        if (idx > buddy_idx) {
            idx = buddy_idx;
        }
        rank++;
    }

    // Add the merged block to the free list
    add_to_free_list(idx, rank);

    return OK;
}

// Query the rank of a page
int query_ranks(void *p) {
    if (!is_valid_address(p)) {
        return -EINVAL;
    }

    int idx = page_index(p);

    // Check if it's allocated
    if (alloc_rank[idx] != 0) {
        return alloc_rank[idx];
    }

    // Check if it's a free block start
    if (block_rank[idx] != 0) {
        return block_rank[idx];
    }

    // Find the free block containing this page
    for (int rank = MAX_RANK; rank >= 1; rank--) {
        int pages = pages_in_rank(rank);
        // Check possible block starts that could contain this page
        int start_idx = (idx / pages) * pages;
        if (start_idx >= 0 && start_idx < total_pages && block_rank[start_idx] == rank) {
            return rank;
        }
    }

    return 1;
}

// Query the count of free pages for a given rank
int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }

    return free_counts[rank];
}
