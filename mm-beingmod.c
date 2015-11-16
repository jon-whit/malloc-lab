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

#define WSIZE      4
#define DSIZE      8
#define INITSIZE 16
#define MINBLOCKSIZE 16

#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Basic constants and macros */

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)        (*(size_t *)(p))
#define PUT(p, val)   (*(size_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x1)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given payload ptr bp, compute the address of its header and footer */
#define HDRP(bp)     ((void *)(bp) - WSIZE)
#define FTRP(bp)     ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given a payload bp, compute the address of next and previous payload blocks */
#define NEXT_BLKP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((void *)(bp) - GET_SIZE(HDRP(bp) - WSIZE))

/* Given block ptr bp, compute address of next and previous free blocks */
#define SUCC_FREEP(bp)(*(void **)(bp))
#define PRED_FREEP(bp)(*(void **)(bp + WSIZE))

/* Declarations */
static void *extend_heap(size_t words);
static void *find_fit(size_t size);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void remove_freeblock(void *bp);
static void insert_freeblock(void *bp);

/* Private global variables */
static char *heap_listp = 0;  /* Points to the start of the heap */
static char *free_listp = 0;  /* Poitns to the frist free block */

/* 
 * mm_init - Initializes the heap like that shown below..
 *           _________________________                                        _____________
 *          |         PROLOGUE        |                                      |   EPILOGUE  |
 * |--------|------------|------------|-------------|-----------|------------|-------------|
 * |        |    HEADER  |   FOOTER   |    HEADER   |  PAYLOAD  |   FOOTER   |    HEADER   |
 * |--------|------------|------------|-------------|-----------|------------|-------------|
 * ^                                  ^
 * heap_listp                         free_listp
 */
int mm_init(void)
{
    /* Attempt to create an empty heap with the prologue and epilogue */
    if ((heap_listp = mem_sbrk(INITSIZE + MINBLOCKSIZE)) == (void *)-1)
        return -1; 

    PUT(heap_listp, 0);                                /* Alignment padding due to epilogue */
    PUT(heap_listp + (1*WSIZE), PACK(INITSIZE, 1));    /* Prologue header */ 
    PUT(heap_listp + (2*WSIZE), PACK(INITSIZE, 1));    /* Prologue footer */

    PUT(heap_listp + (3*WSIZE), PACK(MINBLOCKSIZE, 0)); /* Free block header */
    PUT(heap_listp + (4*WSIZE), 0); /* Free block successor */
    PUT(heap_listp + (5*WSIZE), 0); /* Free block predecessor */
    PUT(heap_listp + (6*WSIZE), PACK(MINBLOCKSIZE, 0)); /* Free block footer */

    PUT(heap_listp + (7*WSIZE), PACK(0, 1));    /* Epilogue header */


    /* Initialize the explicit free list */
    free_listp = heap_listp + (4*WSIZE);

    /* Extend the empty heap with a free block of size bytes */
    if (extend_heap(MINBLOCKSIZE/WSIZE) == NULL)
        return -1;

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{  
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    size_t asize; /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap by if no fit */
    char *bp;

    /* The size of the new block is equal to the size of the header and footer, plus
     * the size of the payload
     */
    asize = MAX(ALIGN(size) + DSIZE, MINBLOCKSIZE);
    
    /* Search the free list for the fit */
    if ((bp = find_fit(asize))) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, MINBLOCKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{ 
    /* Ignore spurious requests */
    if (!bp)
        return;

    size_t size = GET_SIZE(HDRP(bp));

    /* Set the header and footer allocated bits */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* Coalesce to merge any free blocks and add them to the list */
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    return NULL;
}

/** BEGIN HELPER FUNCTIONS **/

static void *extend_heap(size_t words)
{
    char *bp;
    size_t asize;

    asize = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if (asize < MINBLOCKSIZE)
        asize = MINBLOCKSIZE;
    
    /* Attempt to grow the heap by the requested size */
    if ((bp = mem_sbrk(asize)) == (void *)-1)
        return NULL;

    /* Set the header and footer of the newly created free block, and
     * replace the epilogue header */
    PUT(HDRP(bp), PACK(asize, 0));
    PUT(FTRP(bp), PACK(asize, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* Move the epilogue to the end */

    /* Coalesce if the previous block was free */
    return coalesce(bp); 
}

/*
 * Attempts to find the right size of free block in the free list. The free list is 
 * implemented as an explicit list, which is simply a doubly linked list.
 */
static void *find_fit(size_t size)
{
    /* First-fit search */
    void *bp;

    /* Iterate through the free list and try to find a free block
     * large enough */
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = SUCC_FREEP(bp)) {
        if (size <= (size_t) GET_SIZE(HDRP(bp))) {
            return bp;
        }
    }

    return NULL; /* No fit */
}

/*
 * coalesce - Coalesces the memory surround block bp using the Boundary Tag strategy
 * proposed in the text (Page 851, Section 9.9.11).
 * 
 * Adjancent blocks which are free are merged together and the aggregate free block
 * is added to the free list. Any individual free blocks which were merged are removed
 * from the free list.
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* If the next block is free, then coalesce the current
     * block (bp) and the next block */
    if (prev_alloc && !next_alloc) {           /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_freeblock(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    /* If the previous block is free, then coalesce the current
     * block (bp) and the previous block */
    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp); 
        remove_freeblock(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } 

    /* If the previous block and next block are free, coalesce
     * both */
    else if (!prev_alloc && !next_alloc) {     /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
                GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_freeblock(PREV_BLKP(bp));
        remove_freeblock(NEXT_BLKP(bp));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    /* Insert the coalesced block at the front of the free list */
    insert_freeblock(bp);

    /* Return the coalesced block */
    return bp;
}

static void place(void *bp, size_t asize)
{  
    /* Gets the total size of the free block */
    size_t csize = GET_SIZE(HDRP(bp));
 
    if((csize - asize) >= (MINBLOCKSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        remove_freeblock(bp);
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        coalesce(bp);
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        remove_freeblock(bp);
    }
}

static void insert_freeblock(void *bp)
{
    SUCC_FREEP(bp) = free_listp;
    PRED_FREEP(free_listp) = bp;
    PRED_FREEP(bp) = NULL;
    free_listp = bp;
}

static void remove_freeblock(void *bp)
{
    if (PRED_FREEP(bp))
        SUCC_FREEP(PRED_FREEP(bp)) = SUCC_FREEP(bp);
    else
        free_listp = SUCC_FREEP(bp);
    PRED_FREEP(SUCC_FREEP(bp)) = PRED_FREEP(bp);
}

/** END HELPER FUNCTIONS **/
