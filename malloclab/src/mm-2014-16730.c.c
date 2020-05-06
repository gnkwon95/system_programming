/*
	KI NAM KWON - 2014-16730

	Description on major functions:
	Use explicit free list, resulting in final score of 83/100

	mm_init(): starts dynamic memory allocation. initiates heap list and free list with padding, and extends to minimum space. 
	mm_malloc(): First function on dynamic memory allocation. Assigns heap space according to given size. First adjust size to fit alignment, find appropriate free space starting from front of heap, and place space in free space. Adjust remaining free space if there is any.
	mm_free(): Second function on dynamic allocation. Sets certain memory assignment free. This function resets heap allocation in heap list to zero, and adds free block to free list.
	mm_realloc(): Third function on dynamic memory allocation. Identify previous memory allocation, and replace it with new size. 1) If the new size is smaller, it can fit where it was. If there is new free space generated, assign that space to be free. 2) If the new size is same as before, do nothing. 3) If the new size is larger, there are two possibilities. 3-1) If there is free space after current block so the block size can be enlarged, do so. Remaining free space is, if any, set as free block still available. 3-2) If not, malloc new size and copy previous memory data to new block. Then free previous block. 


	Descriptions on supporting functions:
	coalesce(): Combines separate free blocks into one on call to freeing a block. Check if there is a block before or after a block set free, and group if possible. 
	extend_heap(): Used to extend heap by chunksize, if previous function in malloc or realloc finds not enough space in previous allocaed heap space to allocate new memory. 
	find_fit(): Scans through free list to find first space enough to fit required memory. If found, return that block's pointer. If not, returns null to indicate need for extend_heap
	place(): Assign specific size in specific block given as input. If the free block can be split to block allocation and smaller free size left, divide accordingly and assign allocation bit for each. Add new smaller free block to free list. Remove the full block from free list.

	Descriptions on free list related functions:
	list_delete(): Function to remove free block from free list. If block to remove is the first block, make next block head. If not, connect blocks before and after the freed block.
	list_add(): Add free block to free list, at front. New block's prev is null, and next are old blocks. old blocks' before becomes new block. Change head to new block.

	Descriptions on check functions:
	block_info() : prints out basic information about specified block, and checks if allocation and front/head pointer does not match
	free_check() : checks if two consecutive free blocks are found, indicating coalesence error 
	mm_check() : function that called block_info() and free_check(). Prints out current heap list status and free list status so user can check any allocation unintended. If error is found in block_info() or free_check(), print it out.
	check_free() : Function to check only free list status.

*/


		#include <stdio.h>
		#include <stdlib.h>
		#include <assert.h>
		#include <unistd.h>
		#include <string.h>

		#include "mm.h"
		#include "memlib.h"

		/* single word (4) or double word (8) alignment */
		#define ALIGNMENT 8

		/* rounds up to the nearest multiple of ALIGNMENT */
		#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


		#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

		/////////////////////////////////////////////////////////////////////
		//start off with basic macros


		/*Basic constants and macros: */
		#define WSIZE      4 /* Word and header/footer size (bytes) */
		#define DSIZE      (2 * WSIZE)    /* Doubleword size (bytes) */
		#define CHUNKSIZE  (1 << 12)      /* Extend heap by this amount (bytes) */

		#define MAX(x, y)  ((x) > (y) ? (x) : (y))  

		/* Pack a size and allocated bit into a word. */
		#define PACK(size, alloc)  ((size) | (alloc))

		/* Read and write a word at address p. */
		#define GET(p)       (*(unsigned int *)(p))
		#define PUT(p, val)  (*(unsigned int *)(p) = (val))

		/* Read the size and allocated fields from address p. */
		#define GET_SIZE(p)   (GET(p) & ~(DSIZE - 1))
		#define GET_ALLOC(p)  (GET(p) & 0x1)

		/* Given block ptr bp, compute address of its header and footer. */
		#define HDRP(bp)  ((char *)(bp) - WSIZE)
		#define FTRP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

		/* Given block ptr bp, compute address of next and previous blocks. */
		#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
		#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
		/////////////////////////////////////////////////////////////////

		//macros for free list
		#define NEXT_FREE(bp) (*(char **)(bp+WSIZE))
		#define PREV_FREE(bp) (*(char **) (bp))



		/*pre-defined helper functions */

		// basic helpers
		static void *coalesce (void *bp);
		static void *extend_heap(size_t words);
		static void *find_fit(size_t asize);
		static void place(void *bp, size_t asize);

		//my helper for checking
		int block_info(void* bp);
		int free_check(void* bp);
		int mm_check();
		void check_free();

		//functions related to free list manipulation
		static void list_delete(void* ptr);
		static void list_add(void* ptr);
		//coalesce also fits here		

		////////////////////////////////////////////////////////////////////
					/* FUNCTIONS RELATED TO LINKED LIST OF FREE BLOCKS*/
		///////////////////////////////////////////////////////////////////

		// initiate heap list pointer
		static char* heap_listp = 0;
		static char* free_list = 0;

		//functions to manipulate doubly linked list of free blocks

		// remove a block from free list
		static void list_delete(void* ptr)
		{
			if(PREV_FREE(ptr) == NULL){ // case first block in free list
				free_list = NEXT_FREE(ptr);
				PREV_FREE(NEXT_FREE(ptr)) = NULL;
			}else{ // not first block, have something in front
				NEXT_FREE(PREV_FREE(ptr)) = NEXT_FREE(ptr);
				PREV_FREE(NEXT_FREE(ptr)) = PREV_FREE(ptr);
			}
		}

		//add a block into free list
		static void list_add(void* ptr) //
		{
			NEXT_FREE(ptr) = free_list;
			PREV_FREE(free_list) = ptr;
			PREV_FREE(ptr) = NULL;

			free_list = ptr;
		}

		static void* coalesce(void *bp)
		{
			//next block's header pointer = HDRP(NEXT_BLKP(bp));
			//is next block's header pointer allocated? GET_ALLOC(HDRP(NEXT_BLKP(bp)))
			size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
			size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
			
			size_t size = GET_SIZE(HDRP(bp));


			if (prev_alloc && next_alloc){ // front and back not free, 
				list_add(bp); 
			} else if (prev_alloc && !next_alloc) { //case 3 in ppt - next block free
				size += GET_SIZE(HDRP(NEXT_BLKP(bp))); //block now + next free's size

				list_delete(NEXT_BLKP(bp)); //remove next block from free list

				PUT(HDRP(bp), PACK(size, 0)); //update heap allocation with PUT
				PUT(FTRP(bp), PACK(size, 0));

				list_add(bp); // all heap updated, add updated free bp to free list

			} else if (!prev_alloc && next_alloc) {
				size += GET_SIZE(HDRP(PREV_BLKP(bp))); // same as above, add prev size
				list_delete(PREV_BLKP(bp)); // previous block remove from free

				PUT(FTRP(bp), PACK(size, 0));
				PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); //note, current bp is not header
											//its previous block is the header
				bp = PREV_BLKP(bp); // so update bp
				list_add(bp);

			} else{ //simple combination of case 2 and 3
				size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
				list_delete(NEXT_BLKP(bp));
				list_delete(PREV_BLKP(bp));

				PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
				PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));

				bp = PREV_BLKP(bp);
				list_add(bp);
			}
			return (bp);		

		}


		/////////////////////////////////////////////////////////////////////
	
//starts dynamic memory allocation. initiates heap list and free list with padding, and extends to minimum space.
		int mm_init(void)
		{

				/* Create the initial empty heap. */
			if ((heap_listp = mem_sbrk(8 * WSIZE)) == NULL) 
				return (-1);	

			PUT(heap_listp, 0);                            /* Alignment padding */
			PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
			PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* Epilogue header */
	heap_listp += (2 * WSIZE);

	// free list should equal heap list at initiation
	free_list = heap_listp;

	NEXT_FREE(free_list) = NULL;
	PREV_FREE(free_list) = NULL;

	/* Extend the empty heap with a free block of CHUNKSIZE bytes. */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)	//heap space for 2^9 words/ 2^12 bytes
		return (-1);


	return (0);
	
}
//Used to extend heap by chunksize, if previous function in malloc or realloc finds not enough space in previous allocaed heap space to allocate new memory.
static void *extend_heap(size_t words)
{
	char *bp;		//block pointer address
	size_t size;

	//allocate an even number of words to maintain alignment
	size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;


	if ((long)(bp = mem_sbrk(size)) == -1) //bp points to new memory address of heap
		return NULL;

	//initialize free block header/footer and the epilogue header
	PUT(HDRP(bp), PACK(size, 0));  			//free block header
	PUT(FTRP(bp), PACK(size, 0)); 			//free block footer
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));	//new epilogue header

	//coalesce if previous block was free
	return (coalesce(bp));
}

//function to look for asize long free bit in free block lists
// first fit method used
// returns bp if found, or NULL if no fit is found

static void *find_fit(size_t asize)
{
	void *bp;

	//use free_list to start, and move on 

	for (bp=free_list; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE(bp)){
		if (asize <= GET_SIZE(HDRP(bp))) //block of required size found
			return (bp);
	}
		//if nothing found
	return (NULL);
}


//function to place required size in free list
//need to work in conjunction with coalesce
static void place(void *bp, size_t asize)
{
	//bp has free list's address to place the size
	//asize is size to be placed in free (fit 5 byte to 8 byte)

	size_t size = GET_SIZE(HDRP(bp)); //gets size of free block 
	size_t newsize = size - asize; //remaining block size after allocation


	if(newsize >= (2 * DSIZE)) { //remaining block fits and leaves free blocks
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));

		list_delete(bp); //bp not free anymore, remove from free list
		bp = NEXT_BLKP(bp); // 
		
		PUT(HDRP(bp), PACK(newsize, 0)); //beginnning of next block, pack new size
		PUT(FTRP(bp), PACK(newsize, 0)); // also at the back
		coalesce(bp); //with pointer now moved to new free location, coalesce!!
	} else{ 
		PUT(HDRP(bp), PACK(size, 1)); // assign entire block
		PUT(FTRP(bp), PACK(size, 1));
		list_delete(bp); //remove bp from free list
	}	

}


/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
	size_t asize;		//adjusted block size
	size_t extendsize;	//extend heap if there is no fit (free)
	char *bp;			//block pointer address

	if (size==0)
		return NULL;

	//adjust block size to include overhead and alignment reqs
	if (size <= DSIZE) //dsize = 2 words. align accordingly
		asize = 2*DSIZE; //request 15, give 32. 16 for alignment, 16 OH, hdr and ftr
	else
		asize = DSIZE* ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
		//need room for header, footer, and should satisfy double-word alignment req

	//search free list for a fit
	if ((bp = find_fit(asize)) != NULL){ 
		place(bp, asize); //need place function. In bp (free block), place asize info
		return bp;
	}
	
	extendsize = MAX(asize, CHUNKSIZE); //if adjusted size is larger than one chunk, extend that much
	if ((bp = extend_heap(extendsize/WSIZE)) == NULL) //same as mm_init extend
		return NULL;
	
	place(bp, asize);
	return bp;
	
	
}

/*
Second function on dynamic allocation. Sets certain memory assignment free. This function resets heap allocation in heap list to zero, and adds free block to free list.
 */
void mm_free(void *ptr) //free a block from heap. 
						//operation related to free list is done in coalesce
						//operation related to coalesce done separately
{
	if (ptr == NULL) return;

	size_t size = GET_SIZE(HDRP(ptr));
	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));

	coalesce(ptr);	
}

/*
 * mm_realloc - Third function on dynamic memory allocation. Identify previous memory allocation, and replace it with new size. 1) If the new size is smaller, it can fit where it was. If there is new free space generated, assign that space to be free. 2) If the new size is same as before, do nothing. 3) If the new size is larger, there are two possibilities. 3-1) If there is free space after current block so the block size can be enlarged, do so. Remaining free space is, if any, set as free block still available. 3-2) If not, malloc new size and copy previous memory data to new block. Then free previous block.-
 */
void *mm_realloc(void *ptr, size_t size)
{

    void *oldptr = ptr;
    void *newptr;
	size_t prevsize = GET_SIZE(HDRP(oldptr));

	if (size == 0){
		mm_free(ptr);
		return (NULL);
	}

	if (ptr == NULL){
		mm_malloc(size);
		return (NULL);
	}
	//update size to be aligned
	if(size<=DSIZE){
		size = 2*DSIZE;
	} else{
		size = DSIZE*((size + DSIZE+DSIZE-1)/DSIZE);
	}


	//case 1 - new size smaller than previous size
	if (prevsize > size){
		if(prevsize - size <= 2*DSIZE){
			// in this case, info cannot be shrunk due to alignment. Do not change
			return ptr;
		}else{ // data can be shrunk

			PUT(HDRP(ptr), PACK(size, 1)); //update info at start of ptr
			PUT(FTRP(ptr), PACK(size, 1)); //update info at start of ptr
			newptr = NEXT_BLKP(ptr); //begin of free block

			PUT(HDRP(newptr), PACK(prevsize - size, 0)); //pack with zero
			PUT(HDRP(newptr), PACK(prevsize - size, 0));
			mm_free(newptr); //next block added to free list
			list_delete(ptr);  //previous full sized free block is removed from list
			return newptr;
		}

	//case 2 - new size same as previous, nothing change
	} else if (GET_SIZE(HDRP(oldptr)) == size){
		return ptr;


	//case 3 - new size larger than prev
	} else{


		int nextalloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
		size_t nextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		
		//case 3-1 - if free located after ptr can be utilized to fit new size
		if (!nextalloc && nextsize+prevsize >= size){ 
			size_t available = nextsize + prevsize; // total free available
			size_t newfreesize = available - size; //new free size left

			list_delete(NEXT_BLKP(ptr));
			
			if(newfreesize >= 2*DSIZE) { // similar to place. 
				//If available space can be divided to new allocation + remaining free
				PUT(HDRP(ptr), PACK(size, 1)); //update size in hdr/ftr 
				PUT(FTRP(ptr), PACK(size, 1));
				newptr = NEXT_BLKP(ptr); //point next free block

				PUT(HDRP(newptr), PACK(newfreesize, 0)); //set that block zero
				PUT(FTRP(newptr), PACK(newfreesize, 0));
				coalesce(newptr); //handle free block
			} else{ //new free size between 0 and 16. Use all space combined
				PUT(HDRP(ptr), PACK(available, 1));
				PUT(FTRP(ptr), PACK(available, 1));
			}
			return ptr;


		//case 3-2, add to back, and clear previous block
		} else {
	
			newptr = mm_malloc(size); // allocate new size where available

			if(newptr == NULL) {
				return NULL;
			}

			memcpy(newptr, ptr, prevsize); //back up info of ptr to newptr, upto old size
			mm_free(ptr); // free previous block

			return newptr;
		}
	}
}


///////////////////////////////////////////////////////////////////

//prints out information about block, and finds if any mismatch in information is stored
int block_info(void* bp){
	int h = GET_SIZE(HDRP(bp));
	int f = GET_SIZE(FTRP(bp));
	void* next = NEXT_BLKP(HDRP(bp));
	void* prev = NEXT_BLKP(FTRP(bp));

	printf("current ptr = %p\n", bp);
		//error on setting alloc
	if (GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp))){

		printf("ptr = %p, head = %d, foot = %d\n",bp, GET_ALLOC(HDRP(bp)), GET_ALLOC(FTRP(bp)));
		printf("h alloc != f alloc\n");
		return 0;
	}
	printf("alloc = %s\n", GET_ALLOC(HDRP(bp))? "Yes" : "No");
	printf("HDR_size = %d, FTR_size = %d\n", h, f);

		//error on header size and footer size difference
	if (h != f){
		printf("h size != f size\n");
		return 0;
	}
	printf("next loc = %p\n", next);
	printf("prev loc = %p\n\n", prev);
	return 1;
}

//function to find any coalesce error
int free_check(void* bp){
	if (GET_ALLOC(HDRP(bp)) == 0 && GET_ALLOC(HDRP(NEXT_BLKP(bp))) == 0){
		printf("Repeated empty found, coalesce error\n");
		return 0;
	}
	return 1;
}


//private function used just to check free blocks - and detect errors
void check_free(){
	void *bp = free_list;
	while(NEXT_FREE(bp) != NULL){
		block_info(bp);
		bp = NEXT_FREE(bp);
	}
}

int mm_check(void)
{

	void *bp;
	int ret = 0;


	//check conditions about heap
	printf("-------heap_check-----\n");
	//start with bp = start of heap, after prologue/epilogue
	// continue to next block pointer
	for (bp=heap_listp+24; GET_SIZE(HDRP(bp))!= 0; bp = NEXT_BLKP(bp))
		ret = block_info(bp) + free_check(bp);
		//print and check error about block, or its free information
	
	if (!ret){ //if error found
		return 0;
	}

	printf("-------heapcheck end-----\n\n");



	printf("------free check------\n");
	bp = free_list; // similar to heap check, but jump with NEXT_FREE, not every block

	while(NEXT_FREE(bp) != NULL){
		ret = block_info(bp);
		bp = NEXT_FREE(bp);
	}
	printf("-----freecheck end-----\n\n");
	return ret;
	
}



