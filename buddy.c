#include "buddy.h"
#include <stdlib.h>
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096

// Free list structure
typedef struct free_node {
    struct free_node *next;
} free_node_t;

// Global state
static void *base_addr = NULL;
static int total_pages = 0;
static free_node_t *free_lists[MAX_RANK + 1]; // free_lists[1] to free_lists[16]
static char *alloc_rank; // Track the rank of each allocated block

// Helper functions
static inline int pages_in_rank(int rank) {
    if (rank <= 0) return 1;
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

// Initialize the buddy system
int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;

    // Clear free lists
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }

    // Allocate array to track allocation rank (0 = not allocated)
    alloc_rank = (char *)malloc(pgcount);
    for (int i = 0; i < pgcount; i++) {
        alloc_rank[i] = 0;
    }

    // Add all pages to appropriate free lists
    // Start from the largest possible blocks
    int idx = 0;
    while (idx < pgcount) {
        // Find the largest rank that fits
        int rank = MAX_RANK;
        while (rank > 0) {
            int pages = pages_in_rank(rank);
            // Check if this rank fits and is aligned
            if (idx + pages <= pgcount && (idx % pages) == 0) {
                break;
            }
            rank--;
        }

        if (rank > 0) {
            free_node_t *node = (free_node_t *)page_addr(idx);
            node->next = free_lists[rank];
            free_lists[rank] = node;
            idx += pages_in_rank(rank);
        } else {
            idx++; // Skip single page if no rank fits
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
    free_node_t *block = free_lists[current_rank];
    free_lists[current_rank] = block->next;

    // Split the block if necessary
    while (current_rank > rank) {
        current_rank--;
        int idx = page_index((void *)block);
        int buddy_idx = buddy_index(idx, current_rank);

        // Add the buddy to the free list
        free_node_t *buddy = (free_node_t *)page_addr(buddy_idx);
        buddy->next = free_lists[current_rank];
        free_lists[current_rank] = buddy;
    }

    // Mark allocation rank
    int idx = page_index((void *)block);
    alloc_rank[idx] = rank;

    return (void *)block;
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

    // Clear allocation rank
    alloc_rank[idx] = 0;

    // Try to merge with buddy
    while (rank < MAX_RANK) {
        int buddy_idx = buddy_index(idx, rank);

        // Check if buddy is in bounds and properly aligned
        if (buddy_idx < 0 || buddy_idx >= total_pages) break;

        int pages = pages_in_rank(rank);
        if (buddy_idx + pages > total_pages) break;

        // Check if buddy is free (not allocated)
        if (alloc_rank[buddy_idx] != 0) break;

        // Check if buddy is in the free list of this rank
        free_node_t **prev = &free_lists[rank];
        free_node_t *curr = free_lists[rank];
        int found = 0;

        while (curr != NULL) {
            if (page_index((void *)curr) == buddy_idx) {
                *prev = curr->next;
                found = 1;
                break;
            }
            prev = &curr->next;
            curr = curr->next;
        }

        if (!found) break;

        // Merge with buddy - use the lower address
        if (idx > buddy_idx) {
            idx = buddy_idx;
        }
        rank++;
    }

    // Add the merged block to the free list
    free_node_t *node = (free_node_t *)page_addr(idx);
    node->next = free_lists[rank];
    free_lists[rank] = node;

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
    } else {
        // Find the largest free block containing this page
        for (int rank = MAX_RANK; rank >= 1; rank--) {
            free_node_t *curr = free_lists[rank];
            while (curr != NULL) {
                int block_idx = page_index((void *)curr);
                int pages = pages_in_rank(rank);
                if (idx >= block_idx && idx < block_idx + pages) {
                    return rank;
                }
                curr = curr->next;
            }
        }
        return 1; // Default to rank 1 if not found
    }
}

// Query the count of free pages for a given rank
int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }

    int count = 0;
    free_node_t *curr = free_lists[rank];
    while (curr != NULL) {
        count++;
        curr = curr->next;
    }

    return count;
}
