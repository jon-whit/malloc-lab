/*
 * This solution uses a single explicit free list to manage allocation
 * and freeing of 
 *
 *
 *  
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



// CONSTANTS


#define ALIGNMENT 8
#define WSIZE             4         // size in bytes of a single word 
#define DSIZE             8         // size in bytes of a double word
#define INITSIZE          16        // initial size of free list before first free block added
#define MINBLOCKSIZE      16        /* minmum size for a free block, includes a 4 bytes header/footer
                                       and space within the payload for two pointers to the prev and next
                                       free blocks */



// MACROS


#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p)        (*(size_t *)(p))
#define PUT(p, val)   (*(size_t *)(p) = (val))
#define GET_SIZE(p)  (GET(p) & ~0x1)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp)     ((void *)(bp) - WSIZE)
#define FTRP(bp)     ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((void *)(bp) - GET_SIZE(HDRP(bp) - WSIZE))
#define NEXT_FREE(bp)(*(void **)(bp))
#define PREV_FREE(bp)(*(void **)(bp + WSIZE))




// PROTORTYPES


static void *extend_heap(size_t words);
static void *find_fit(size_t size);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void remove_freeblock(void *bp);


// private variables represeneting the heap and free list within the heap
static char *heap_listp = 0;  /* Points to the start of the heap */
static char *free_listp = 0;  /* Poitns to the frist free block */


/*  free blocks on the heap are organized in an explicit free list which is
 *  always pointed to by free_listp. Each free block contains two pointers 
 *  pointing to the next free block and the previous free block. Thus the minimum
 *  payload for a free block must be 8 bytes to support the two pointers. The overall
 *  size of a free block is then 16 bytes including the 4 byte header and 4 byte footer.
 *  the heap_list maintains a 4 byte prologoue and 4 byte epilogue.
 *
 */ 

/* 
 * mm_init - Initializes the heap like that shown below..
 *  _____________                                                   _____________
 * |  PROLOGUE  |                8+ bytes or 2 ptrs                |   EPILOGUE  |
 * |------------|------------|-----------|------------|------------|-------------|
 * |    HEADER  |   HEADER   |        PAYLOAD         |   FOOTER   |    HEADER   |
 * |------------|------------|-----------|------------|------------|-------------|
 * ^            ^            ^       
 * heap_listp   free_listp   bp 
 */


int mm_init(void)
{
  // initialize the heap with freelist prologue/epilogoue and space for the
  // initial free block. (32 bytes total)
  if ((heap_listp = mem_sbrk(INITSIZE + MINBLOCKSIZE)) == (void *)-1)
      return -1; 
  PUT(heap_listp,             PACK(MINBLOCKSIZE, 1));           /* Prologue header */ 
  PUT(heap_listp +    WSIZE,  PACK(MINBLOCKSIZE, 0));           /* Free block header */

  PUT(heap_listp + (2*WSIZE), PACK(0,0));                       /* space for next pointer */
  PUT(heap_listp + (3*WSIZE), PACK(0,0));                       /* space for prev pointer */
  
  PUT(heap_listp + (4*WSIZE), PACK(MINBLOCKSIZE, 0));           /* Free block footer */
  PUT(heap_listp + (5*WSIZE), PACK(0, 1));                      /* Epilogue header */

  // point free_list to the first header of the first free block
  free_listp = heap_listp + (WSIZE);

  return 0;
}


void *mm_malloc(size_t size)
{  
  
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
  for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE(bp)) {
      if (size <= (size_t) GET_SIZE(HDRP(bp))) {
          return bp;
      }
  }

  return NULL; /* No fit */
}


static void remove_freeblock(void *bp)
{
  if (PREV_FREE(bp))
      NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
  else
      free_listp = NEXT_FREE(bp);
  PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
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
  NEXT_FREE(bp) = free_listp;
  PREV_FREE(free_listp) = bp;
  PREV_FREE(bp) = NULL;
  free_listp = bp;

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



/** END HELPER FUNCTIONS **/
