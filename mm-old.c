/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your identifying information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "nighthawk",
    /* First member's full name */
    "Jonathan Whitaker",
    /* First member's UID */
    "U0752100",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's UID (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Declare header structure */
typedef struct block hblock;

struct block {
    size_t header;
    hblock *succ_p;
    hblock *pred_p;
    size_t footer;
};

#define WSIZE      4
#define DSIZE      8

/* Basic constants and macros */
#define HSIZE      ALIGN(sizeof(hblock)) /* The minimum size of a block */

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)        (*(size_t *)(p))
#define PUT(p, val)   (*(size_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x1)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute the address of its header and footer */
#define HDRP(bp)     ((char *)(bp))
#define FTRP(bp)     ((char *)(bp) + GET_SIZE(bp))

/* Given a block ptr bp, compute the address of next and previous payload blocks */
#define NEXT_BLKP(bp)    ((char *)(bp) + GET_SIZE(bp))
#define PREV_BLKP(bp)    ((char *)(bp) - GET_SIZE(bp))

/* Declarations */
static void *find_free(size_t size);
static void *coalesce(hblock *p);
static void place(hblock *p, size_t newsize);
void print_heap();
static void remove_free_block(hblock *bp);

/* Private global variables */
static hblock *p;         /* Points to the starting block at all times */

/* 
 * mm_init - Initialize the malloc package, and return 0 if successful and -1 otherwise.
 */
int mm_init(void)
{
    /* Attempt to create an empty heap with just a prologue 
     * at the beginning 
     */
    if (mem_sbrk(HSIZE) == (void *)-1)
        return -1;
 
    p = (hblock *) mem_heap_lo();

    /* Set the block size of the header, and make it point to itself */
    p->header = HSIZE | 0x1; // The prologue will be the only allocated block of HSIZE
    p->footer = p->header;
    p->succ_p = p;
    p->pred_p = p;

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    /* Ignore spurious requests */
    if (size < 1)
        return NULL;

    /* The size of the new block is equal to the size of the header, plus
     * the size of the payload
     */
    int newsize = ALIGN(size + HSIZE);
    
    /* Try to find a free block that is large enough */
    hblock *bp = (hblock *) find_free(newsize);
   
    /* If a large enough free block was not found, then coalesce
     * the existing free blocks */ 

    /* After coalsecing, if a large enough free block cannot be found, then
     * extend the heap with a free block */
    if (bp == NULL) { 
        bp = mem_sbrk(newsize);
        if ((long)(bp) == -1)
            return NULL;
        else {
            bp->header = newsize | 0x1;
            bp->footer = bp->header;
        }
    }
    else {
        /* Otherwise, a free block of the appropriate size was found. Place
         * the block */
        place(bp, newsize); 
    }

    // Return a pointer to the payload
    return (char *) bp + HSIZE;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    /* Get the pointer to the allocated block */
    hblock *bp = ptr - HSIZE; 
    //hblock *cbp; // stores a pointer to the coalesced block

    //cbp = (hblock *) coalesce(bp);

    /* Modify the allocated bit for the header and footer */ 
    bp->header &= ~1;
    bp->footer = bp->header;

    /* Set up the doubly linked explicit free list */
    bp->succ_p = p->succ_p;
    bp->pred_p = p;
    p->succ_p = bp;
    bp->succ_p->pred_p = bp;
   
    return;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    return NULL;
}

/** BEGIN HELPER FUNCTIONS **/

/*
 * Attempts to find the right size of free block in the free list. The free list is 
 * implemented as an explicit list, which is simply a doubly linked list.
 */
static void *find_free(size_t size)
{
    /* Iterate over each of the blocks in the free list until a block
     * of the appropriate size is found. If the block wraps back around
     * to the prologue, then a free block wasn't found.
     */ 
    hblock *bp;
    for(bp = p->succ_p; bp != p && bp->header < size; bp = bp->succ_p) {
    }

    /* If the free list wrapped back around, then there were no free spots */
    if (bp == p)
        return NULL;
    else
    /* Otherwise return the pointer to the free block */
        return bp; 
}

static void *coalesce(hblock *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(bp);

    /* If the next block is free, then coalesce the current
     * block (bp) and the next block */
    if (prev_alloc && !next_alloc) {           /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        bp->header = size | 0x0;
        bp->footer = bp->header;
    }

    /* If the previous block is free, then coalesce the current
     * block (bp) and the previous block */
    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        size += GET_SIZE(FTRP(PREV_BLKP(bp)));
        bp->footer = size | 0x0;
        bp->pred_p->header = size | 0x0;
    }

    
    else if (!prev_alloc && !next_alloc) {
        size += GET_SIZE(FTRP(PREV_BLKP(bp))) + 
                GET_SIZE(HDRP(NEXT_BLKP(bp)));
        bp->pred_p->header = size | 0x0;
        bp->succ_p->footer = size | 0x0;
    }

    return bp;
}

static void place(hblock *bp, size_t newsize)
{  
    size_t csize = GET_SIZE(bp->header);

    if ((csize - newsize) >= 24) {
        bp->header = newsize | 0x1;
        bp->footer = bp->header;
        remove_free_block(bp);
        bp = (hblock *) NEXT_BLKP(bp);
        bp->header = (csize-newsize) | 0x0;
        bp->footer = bp->header; 
        coalesce(bp);
    }


    else {
        bp->header = csize | 0x1;
        bp->footer = bp->header;
        remove_free_block(bp);
    }
    /* Set the allocated bit of the header and footer */
    //bp->header |= 0x1;
    //bp->footer = bp->header;

    /* Set up the link for the free list */  
    //remove_free_block(bp);

    return;
}

void print_heap()
{
    hblock *bp = mem_heap_lo();
    while(bp < (hblock *) mem_heap_hi()) {
        printf("%s block at %p, size %d\n", 
               GET_ALLOC(bp) ? "allocated":"free", bp, GET_SIZE(bp));
        bp = (hblock *) NEXT_BLKP(bp); 
    }
}

static void remove_free_block(hblock *bp)
{
    bp->pred_p->succ_p = bp->succ_p;
    bp->succ_p->pred_p = bp->pred_p;
}
/** END HELPER FUNCTIONS **/
