/*
 * malloclab - Implemented with an explicit free list allocator to manage allocation of and
 * freeing of memory.  
 *
 * Block structures:
 * An explicit list uses the payload to embed pointers to the previous and next free blocks
 * within a free block. The free and allocated block organizations are shown below:
 *
 * Allocated Block          Free Block
 *  ---------               ---------
 * | HEADER  |             | HEADER  |
 *  ---------               ---------
 * |         |             |  NEXT   |
 * |         |              ---------
 * | PAYLOAD |             |  PREV   |
 * |         |              ---------
 * |         |             |         |
 *  ---------              |         |
 * | FOOTER  |              ---------
 *  ---------              | FOOTER  |
 *                          ---------
 * 
 * Free list organization:
 * Free blocks on the heap are organized using an explicit free list with the head of the list being 
 * pointed to by a pointer free_listp (see diagram below in mm_init). Each free block contains two 
 * pointers, one pointing to the next free block, and one pointing to the previous free block. The 
 * minimum payload for a free block must be 8 bytes to support the two pointers. The overall size 
 * of a free block is then 16 bytes, which includes the 4 byte header and 4 byte footer. 
 *
 * Free list manipulation:
 * The free list is maintained as a doubly linked list. Free blocks are removed using a doubly linked
 * list removal strategy and then coalesced to merge any adjacent free blocks. Free blocks are added
 * to the list using a LIFO insertion policy. Each free block is added to the front of the free list. 
 * For more information on how the free list is modified, see the functions 'remove_freeblock' and
 * 'coalesce'.
 *
 *
 * Authors:
 * (1) Jonathan Whitaker
 * (2) Daniel Rushton
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
    "Daniel Rushton",
    /* Second member's UID (leave blank if none) */
    "U0850493"
};



// CONSTANTS
#define ALIGNMENT         8         // memory alignment factor
#define WSIZE             4         // Size in bytes of a single word 
#define DSIZE             8         // Size in bytes of a double word
#define INITSIZE          16        // Initial size of free list before first free block added
#define MINBLOCKSIZE      16        /* Minmum size for a free block, includes 4 bytes for header/footer
                                       and space within the payload for two pointers to the prev and next
                                       free blocks */

// MACROS
/* NOTE: Most of these macros came from the text book on Page 857 (Fig. 9.43). We added the
 * NEXT_FREE and PREV_FREE macros to traverse the free list */
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


// PROTOTYPES
static void *extend_heap(size_t words);
static void *find_fit(size_t size);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void remove_freeblock(void *bp);
// static int mm_check();


// Private variables represeneting the heap and free list within the heap
static char *heap_listp = 0;  /* Points to the start of the heap */
static char *free_listp = 0;  /* Poitns to the frist free block */


/* 
 * mm_init - Initializes the heap like that shown below.
 *  ____________                                                    _____________
 * |  PROLOGUE  |                8+ bytes or 2 ptrs                |   EPILOGUE  |
 * |------------|------------|-----------|------------|------------|-------------|
 * |   HEADER   |   HEADER   |        PAYLOAD         |   FOOTER   |    HEADER   |
 * |------------|------------|-----------|------------|------------|-------------|
 * ^            ^            ^       
 * heap_listp   free_listp   bp 
 */
int mm_init(void)
{
  // Initialize the heap with freelist prologue/epilogoue and space for the
  // initial free block. (32 bytes total)
  if ((heap_listp = mem_sbrk(INITSIZE + MINBLOCKSIZE)) == (void *)-1)
      return -1; 
  PUT(heap_listp,             PACK(MINBLOCKSIZE, 1));           // Prologue header 
  PUT(heap_listp +    WSIZE,  PACK(MINBLOCKSIZE, 0));           // Free block header 

  PUT(heap_listp + (2*WSIZE), PACK(0,0));                       // Space for next pointer 
  PUT(heap_listp + (3*WSIZE), PACK(0,0));                       // Space for prev pointer 
  
  PUT(heap_listp + (4*WSIZE), PACK(MINBLOCKSIZE, 0));           // Free block footer 
  PUT(heap_listp + (5*WSIZE), PACK(0, 1));                      // Epilogue header 

  // Point free_list to the first header of the first free block
  free_listp = heap_listp + (WSIZE);

  return 0;
}

/*
 * mm_malloc - Allocates a block of memory of memory of the given size aligned to 8-byte
 * boundaries.
 *
 * A block is allocated according to this strategy:
 * (1) If a free block of the given size is found, then allocate that free block and return
 * a pointer to the payload of that block.
 * (2) Otherwise a free block could not be found, so an extension of the heap is necessary.
 * Simply extend the heap and place the allocated block in the new free block.
 */
void *mm_malloc(size_t size)
{  
  
  if (size == 0)
      return NULL;

  size_t asize;       // Adjusted block size 
  size_t extendsize;  // Amount to extend heap by if no fit 
  char *bp;

  /* The size of the new block is equal to the size of the header and footer, plus
   * the size of the payload. Or MINBLOCKSIZE if the requested size is smaller.
   */
  asize = MAX(ALIGN(size) + DSIZE, MINBLOCKSIZE);
  
  // Search the free list for the fit 
  if ((bp = find_fit(asize))) {
    place(bp, asize);
    return bp;
  }

  // Otherwise, no fit was found. Grow the heap larger. 
  extendsize = MAX(asize, MINBLOCKSIZE);
  if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
    return NULL;

  // Place the newly allocated block
  place(bp, asize);

  return bp;
}

/*
 * mm_free - Frees the block being pointed to by bp.
 *
 * Freeing a block is as simple as setting its allocated bit to 0. After
 * freeing the block, the free blocks should be coalesced to ensure high
 * memory utilization. 
 */
void mm_free(void *bp)
{ 
  
  // Ignore spurious requests 
  if (!bp)
      return;

  size_t size = GET_SIZE(HDRP(bp));

  /* Set the header and footer allocated bits to 0, thus
   * freeing the block */
  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));

  // Coalesce to merge any free blocks and add them to the list 
  coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
  // If ptr is NULL, realloc is equivalent to mm_malloc(size)
  if (ptr == NULL)
    return mm_malloc(size);

  // If size is equal to zero, realloc is equivalent to mm_free(ptr)
  if (size == 0) {
    mm_free(ptr);
    return NULL;
  }
    
  /* Otherwise, we assume ptr is not NULL and was returned by an earlier malloc or realloc call.
   * Get the size of the current payload */
  size_t asize = MAX(ALIGN(size) + DSIZE, MINBLOCKSIZE);
  size_t current_size = GET_SIZE(HDRP(ptr));

  void *bp;
  char *next = HDRP(NEXT_BLKP(ptr));
  size_t newsize = current_size + GET_SIZE(next);

  /* Case 1: Size is equal to the current payload size */
  if (asize == current_size)
    return ptr;

  // Case 2: Size is less than the current payload size 
  if ( asize <= current_size ) {

    if( asize > MINBLOCKSIZE && (current_size - asize) > MINBLOCKSIZE) {  

      PUT(HDRP(ptr), PACK(asize, 1));
      PUT(FTRP(ptr), PACK(asize, 1));
      bp = NEXT_BLKP(ptr);
      PUT(HDRP(bp), PACK(current_size - asize, 1));
      PUT(FTRP(bp), PACK(current_size - asize, 1));
      mm_free(bp);
      return ptr;
    }

    // allocate a new block of the requested size and release the current block
    bp = mm_malloc(asize);
    memcpy(bp, ptr, asize);
    mm_free(ptr);
    return bp;
  }

  // Case 3: Requested size is greater than the current payload size 
  else {

    // next block is unallocated and is large enough to complete the request
    // merge current block with next block up to the size needed and free the 
    // remaining block.
    if ( !GET_ALLOC(next) && newsize >= asize ) {

      // merge, split, and release
      remove_freeblock(NEXT_BLKP(ptr));
      PUT(HDRP(ptr), PACK(asize, 1));
      PUT(FTRP(ptr), PACK(asize, 1));
      bp = NEXT_BLKP(ptr);
      PUT(HDRP(bp), PACK(newsize-asize, 1));
      PUT(FTRP(bp), PACK(newsize-asize, 1));
      mm_free(bp);
      return ptr;
    }  
    
    // otherwise allocate a new block of the requested size and release the current block
    bp = mm_malloc(asize); 
    memcpy(bp, ptr, current_size);
    mm_free(ptr);
    return bp;
  }

}


/*
 * extend_heap - Extends the heap by the given number of words rounded up to the 
 * nearest even integer.
 */
static void *extend_heap(size_t words)
{
  char *bp;
  size_t asize;

  /* Adjust the size so the alignment and minimum block size requirements
   * are met. */ 
  asize = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
  if (asize < MINBLOCKSIZE)
    asize = MINBLOCKSIZE;
  
  // Attempt to grow the heap by the adjusted size 
  if ((bp = mem_sbrk(asize)) == (void *)-1)
    return NULL;

  /* Set the header and footer of the newly created free block, and
   * push the epilogue header to the back */
  PUT(HDRP(bp), PACK(asize, 0));
  PUT(FTRP(bp), PACK(asize, 0));
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* Move the epilogue to the end */

  // Coalesce any partitioned free memory 
  return coalesce(bp); 
}

/*
 * find_fit - Attempts to find a free block of at least the given size in the free list.
 *
 * This function implements a first-fit search strategy for an explicit free list, which 
 * is simply a doubly linked list of free blocks.
 */
static void *find_fit(size_t size)
{
  // First-fit search 
  void *bp;

  /* Iterate through the free list and try to find a free block
   * large enough */
  for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE(bp)) {
    if (size <= GET_SIZE(HDRP(bp))) 
      return bp; 
  }
  // Otherwise no free block was large enough
  return NULL; 
}

/*
 * remove_freeblock - Removes the given free block pointed to by bp from the free list.
 * 
 * The explicit free list is simply a doubly linked list. This function performs a removal
 * of the block from the doubly linked free list.
 */
static void remove_freeblock(void *bp)
{
  if(bp) {
    if (PREV_FREE(bp))
      NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
    else
      free_listp = NEXT_FREE(bp);
    if(NEXT_FREE(bp) != NULL)
      PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
  }
}



/*
 * coalesce - Coalesces the memory surrounding block bp using the Boundary Tag strategy
 * proposed in the text (Page 851, Section 9.9.11).
 * 
 * Adjancent blocks which are free are merged together and the aggregate free block
 * is added to the free list. Any individual free blocks which were merged are removed
 * from the free list.
 */
static void *coalesce(void *bp)
{
  // Determine the current allocation state of the previous and next blocks 
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

  // Get the size of the current free block
  size_t size = GET_SIZE(HDRP(bp));

  /* If the next block is free, then coalesce the current block
   * (bp) and the next block */
  if (prev_alloc && !next_alloc) {           // Case 2 (in text) 
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));  
    remove_freeblock(NEXT_BLKP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  }

  /* If the previous block is free, then coalesce the current
   * block (bp) and the previous block */
  else if (!prev_alloc && next_alloc) {      // Case 3 (in text) 
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    bp = PREV_BLKP(bp); 
    remove_freeblock(bp);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  } 

  /* If the previous block and next block are free, coalesce
   * both */
  else if (!prev_alloc && !next_alloc) {     // Case 4 (in text) 
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(HDRP(NEXT_BLKP(bp)));
    remove_freeblock(PREV_BLKP(bp));
    remove_freeblock(NEXT_BLKP(bp));
    bp = PREV_BLKP(bp);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  }

  // Insert the coalesced block at the front of the free list 
  NEXT_FREE(bp) = free_listp;
  PREV_FREE(free_listp) = bp;
  PREV_FREE(bp) = NULL;
  free_listp = bp;

  // Return the coalesced block 
  return bp;
}

/*
 * place - Places a block of the given size in the free block pointed to by the given
 * pointer bp.
 *
 * This placement is done using a split strategy. If the difference between the size of block 
 * being allocated (asize) and the total size of the free block (fsize) is greater than or equal
 * to the mimimum block size, then the block is split into two parts. The first block is the 
 * allocated block of size asize, and the second block is the remaining free block with a size
 * corresponding to the difference between the two block sizes.  
 */
static void place(void *bp, size_t asize)
{  
  // Gets the total size of the free block 
  size_t fsize = GET_SIZE(HDRP(bp));

  // Case 1: Splitting is performed 
  if((fsize - asize) >= (MINBLOCKSIZE)) {

    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    remove_freeblock(bp);
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(fsize-asize, 0));
    PUT(FTRP(bp), PACK(fsize-asize, 0));
    coalesce(bp);
  }

  // Case 2: Splitting not possible. Use the full free block 
  else {

    PUT(HDRP(bp), PACK(fsize, 1));
    PUT(FTRP(bp), PACK(fsize, 1));
    remove_freeblock(bp);
  }
}

// consistency checker

// static int mm_check() {

//   // Is every block in the free list marked as free?
//   void *next;
//   for (next = free_listp; GET_ALLOC(HDRP(next)) == 0; next = NEXT_FREE(next)) {
//     if (GET_ALLOC(HDRP(next))) {
//       printf("Consistency error: block %p in free list but marked allocated!", next);
//       return 1;
//     }
//   }

//   // Are there any contiguous free blocks that escaped coalescing?
//   for (next = free_listp; GET_ALLOC(HDRP(next)) == 0; next = NEXT_FREE(next)) {

//     char *prev = PREV_FREE(HDRP(next));
//       if(prev != NULL && HDRP(next) - FTRP(prev) == DSIZE) {
//         printf("Consistency error: block %p missed coalescing!", next);
//         return 1;
//       }
//   }

//   // Do the pointers in the free list point to valid free blocks?
//   for (next = free_listp; GET_ALLOC(HDRP(next)) == 0; next = NEXT_FREE(next)) {

//     if(next < mem_heap_lo() || next > mem_heap_hi()) {
//       printf("Consistency error: free block %p invalid", next);
//       return 1;
//     }
//   }

//   // Do the pointers in a heap block point to a valid heap address?
//   for (next = heap_listp; NEXT_BLKP(next) != NULL; next = NEXT_BLKP(next)) {

//     if(next < mem_heap_lo() || next > mem_heap_hi()) {
//       printf("Consistency error: block %p outside designated heap space", next);
//       return 1;
//     }
//   }

//   return 0;
// }






