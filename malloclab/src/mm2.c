/*
	KI NAM KWON - 2014-16730

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
		//	size_t size2 = size + DSIZE;
		//#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
		#define NEXT_BLKP(bp)  ((void *)(bp) + GET_SIZE(HDRP(bp)))
		//#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
		#define PREV_BLKP(bp) ((void *)(bp) - GET_SIZE((void *)(bp) - DSIZE))
		/////////////////////////////////////////////////////////////////

		//similar macros for free list
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


		////////////////////////////////////////////////////////////////////
					/* FUNCTIONS RELATED TO LINKED LIST OF FREE BLOCKS*/
		///////////////////////////////////////////////////////////////////

		// initiate heap list pointer
		static char* heap_listp = 0;
		static char* free_list = 0;

		//function to manipulate doubly linked list of free blocks

		static void list_delete(void* ptr)
		{
			// [  4   ][    5   ][   6   ]
			// [   4   ][   6      ]

			if(PREV_FREE(ptr) == NULL){ 
					// case of removing 4
					// move header to 5, and make 5's prev null. 
				free_list = NEXT_FREE(ptr);
				PREV_FREE(NEXT_FREE(ptr)) = NULL;
			}else{ // remove 5, make 4's next 6, and 6's prev 4
				NEXT_FREE(PREV_FREE(ptr)) = NEXT_FREE(ptr);
				PREV_FREE(NEXT_FREE(ptr)) = PREV_FREE(ptr);
			}
		}

		static void list_add(void* ptr)
		{
			NEXT_FREE(ptr) = free_list;
			PREV_FREE(free_list) = ptr;
			PREV_FREE(ptr) = NULL;

			free_list = ptr; // 6 is new head
		}

		static void* coalesce(void *bp)
		{
			//next block's header pointer = HDRP(NEXT_BLKP(bp));
			//is next block's header pointer allocated? GET_ALLOC(HDRP(NEXT_BLKP(bp)))
			size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
			size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
			
			size_t size = GET_SIZE(HDRP(bp));


			if (prev_alloc && next_alloc){ // front and back not free, 
				//root ----  [                 20             ] ---- next
				list_add(bp); //10 byte free block added root change to r, point to n. 
			} else if (prev_alloc && !next_alloc) { //case 3 in ppt - next block free
				// root -- [  3  1][ 7 ][5        0] --       [next] - assign 7
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
		//start off with one heap list

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
	//new heap's header pointer's start, and footer pointer's start is zero-padded
	//note 1. input is size (very big value) - all free, and alloc bit zero (not filled)
	//since all heap is free, next block is epilogue. epilogue's header is alloc bit 1
		//why?? not know
	tail = bp;
	//coalesce if previous block was free
	return (coalesce(bp));
}

//function to look for asize long free bit in free block lists
// first fit method used
// returns bp if found, or NULL if no fit is found

static void *find_fit(size_t asize)
{
	void *bp;


	//think of way for bp to point start of free list, not header. 
	//probabily go on forever, as all get_alloc should be zero in explicit
	//may not need get_alloc 
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


	//assume asize 24, size(bp) = 64
	// and newsize (remaining) = 40.  40 >= 32, enough to store 32 byte req

	if(newsize >= (2 * DSIZE)) { //remaining block fits at at least 32 bytes
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		// [             64         ]
		//>[[  24  ][        40    ]]
		//>
		list_delete(bp); //not free anymore, so deal with it. 
		bp = NEXT_BLKP(bp); // somehow point to start of 40.  need to chnage next_blkp
	//bp = (char *)bp + get_size(hdrp(bp)); // current bp + size
	// current bp position + current block (allocated just now)'s size = point to 40 
		PUT(HDRP(bp), PACK(newsize, 0)); //beginnning of 40, pack new size
		PUT(FTRP(bp), PACK(newsize, 0)); // also at the back
		//list_add(bp);
		coalesce(bp); //with pointer now moved to new free location, coalesce!!
	} else{ 
		// [            64          ]
		// [[60               ][4  ]] 4 is too small to use/ does not align!
		PUT(HDRP(bp), PACK(size, 1)); // assign entire block 
		PUT(FTRP(bp), PACK(size, 1));
		list_delete(bp); //remove bp from free list
	}	
//	printf("heap top = %p\n", heap_listp);
//	printf("size allocated = %d\n", size);
//	printf("check = %p, %d\n\n", HDRP(bp), GET_SIZE(HDRP(bp)));
	
//	mm_check();		
}


/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
//	if (size == 128) printf("malloc here\n");


//	mm_check();
	size_t asize;		//adjusted block size
	size_t extendsize;	//extend heap if there is no fit (free)
	char *bp;			//block pointer address --> char or void?

	//ignore spurious requests
	if (size==0)
		return NULL;

	//adjust block size to include overhead and alignment reqs
	if (size <= DSIZE) //dsize = 2 words (16 bytes). align accordingly
		asize = 2*DSIZE; //request 15, give 32. 16 for alignment, 16 OH, hdr and ftr
	else
		asize = DSIZE* ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
		//request 20, give 48 since 40 is greater than 32
		//need room for header, footer, and should satisfy double-word alignment req
	//search free list for a fit
	if ((bp = find_fit(asize)) != NULL){ //need find_fit function
//		if (size > 163456) printf("\nfit found\n");

		place(bp, asize); //need place function. In bp (free block), place asize info
	//	checkheap(1);
		return bp;
	}
	
	


	// no fit found
	extendsize = MAX(asize, CHUNKSIZE); //if adjusted size is larger than one chunk, extend that much
	if ((bp = extend_heap(extendsize/WSIZE)) == NULL) //same as mm_init extend
//		printf("cannot extend further\n");
		return NULL;
	
	place(bp, asize);
	return bp;
	
	
/*
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
	return NULL;
    else {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }

*/
}

static int in_heap(const void* p){
	return p<=(void*)((char *)mem_heap_hi() + WSIZE) && p >= mem_heap_lo();
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) //free a block from heap. 
						//operation related to free list is done separately
						//operation related to coalesce done separately
{
//	printf("\tinit free\n");
	if (ptr == NULL) return;
//	if (!in_heap(ptr))
//		printf("@@@@@@@problem here\n");

	size_t size = GET_SIZE(HDRP(ptr));
	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));
	if(NEXT_BLKP(ptr) == heap_listp + WSIZE){
		printf("------adsf asdfasdfsad\n");
	}
	coalesce(ptr);
	
//	mm_check();
}



/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
//	if (size < 2000) {
//		printf("size = %d\n", size);
//		mm_check();
//	}
    void *oldptr = ptr;
    void *newptr;
 //   size_t copySize;
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




	//case 1 - new size smaller
	if (prevsize > size){
		if(prevsize - size <= 2*DSIZE){
			// in this case, info cannot be shrunk due to alignment. Do not change
			return ptr;
		}else{ // data can be shrunk
			//[          50               ]
			//[    20  1][        30     0]
//			printf("@@@@@@@@@@@@@@@@@\n");
			PUT(HDRP(ptr), PACK(size, 1)); //update info at start of ptr
			PUT(FTRP(ptr), PACK(size, 1)); //update info at start of ptr
			newptr = NEXT_BLKP(ptr); //begin of 30
			PUT(HDRP(newptr), PACK(prevsize - size, 0));
			PUT(HDRP(newptr), PACK(prevsize - size, 0));
			mm_free(newptr);
			list_delete(ptr); // not sure if needed. Should check
		//	list_add(newptr);
	//		coalesce(newptr);
			return newptr;
		}







	//case 2 - new size same
	} else if (GET_SIZE(HDRP(oldptr)) == size){ //no change in size
		return ptr;
	//case 3 - new size larger than prev







	} else{
		//case 3-1 - if free located after ptr can be utilized to fit new size
		int nextalloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
		size_t nextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		//next free part can be allocated
		if (!nextalloc && nextsize+prevsize >= size){ 
	//		printf("@@@@@another case found\n");
			size_t available = nextsize + prevsize;
			size_t newfreesize = available - size;

//			if (size < 2000){
//				printf("Before = \n");
//				check_free();
//			}

			list_delete(NEXT_BLKP(ptr));
			
			if(newfreesize >= 2*DSIZE) {
				PUT(HDRP(ptr), PACK(size, 1));
				PUT(FTRP(ptr), PACK(size, 1));
				newptr = NEXT_BLKP(ptr);

				PUT(HDRP(newptr), PACK(newfreesize, 0));
				PUT(FTRP(newptr), PACK(newfreesize, 0));
			//	list_add(newptr);
				coalesce(newptr);
//			if (size < 2000){
//				printf("*****************\nafter = \n");
//				check_free();
//			}


			//	check_free();
			} else{ //new free size between 0 and 16. Just use all space combined
				PUT(HDRP(ptr), PACK(available, 1));
				PUT(FTRP(ptr), PACK(available, 1));
			}

		//	if(size <700)	mm_check();

			return ptr;
		//case 3-2, back up current data, clear current data, and insert.
		} else {

		//	printf("new size larger than prev size\n");
			newptr = mm_malloc(size); // occupy space to back up

			if(newptr == NULL) {
	//			printf("cannot extend more\n");	
				return NULL;
			}

		//	place(newptr, size); // similar to malloc, assign area		
			memcpy(newptr, ptr, prevsize); //back up info of ptr to newptr, upto old size
			mm_free(ptr); // free
		//	list_add(newptr);

//			if (size < 700) {
//				printf("\t\t\t\t\t\tspeial check\n");
//				mm_check();
//			}
			return newptr;
		}
	}
}






///////////////////////////////////////////////////////////////////


int block_info(void* bp){
	int h = GET_SIZE(HDRP(bp));
	int f = GET_SIZE(FTRP(bp));
	void* next = NEXT_BLKP(HDRP(bp));
	void* prev = NEXT_BLKP(FTRP(bp));

	printf("current ptr = %p\n", bp);
	if (GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp))){

		printf("ptr = %p, head = %d, foot = %d\n",bp, GET_ALLOC(HDRP(bp)), GET_ALLOC(FTRP(bp)));
		printf("h alloc != f alloc\n");
		return 0;
	}
	printf("alloc = %s\n", GET_ALLOC(HDRP(bp))? "Yes" : "No");
	printf("HDR_size = %d, FTR_size = %d\n", h, f);
	if (h != f){
		printf("h size != f size\n");
		return 0;
	}
	printf("next loc = %p\n", next);
	printf("prev loc = %p\n\n", prev);
	return 1;
}


int free_check(void* bp){
	if (GET_ALLOC(HDRP(bp)) == 0 && GET_ALLOC(HDRP(NEXT_BLKP(bp))) == 0){
		printf("Repeated empty found, coalesce error\n");
		return 0;
	}
	return 1;
}


void check_free(){
	void *bp = free_list;
	int cnt = 0;
	while(NEXT_FREE(bp) != NULL){
		cnt++;
		block_info(bp);
		bp = NEXT_FREE(bp);
		if (cnt == 3){
			break;
		}
	}
}

int mm_check(void)
{

	//check heap list
	void *bp;
	int ret = 0;

	printf("-------heap_check-----\n");
	//start with bp = start of heap
	// continue to next block pointer
	for (bp=heap_listp+24; GET_SIZE(HDRP(bp))!= 0; bp = NEXT_BLKP(bp))
		ret = block_info(bp) + free_check(bp);
		//if nothing found
	printf("////heapcheck end////\n\n");

	printf("!@$!@#!@#------free check------\n");
//	printf("heap head = %p\n", heap_listp);
//	for (bp = NEXT_FREE(free_list); GET_SIZE(bp) != 0; bp = NEXT_FREE(bp)){
//		ret = block_info(bp);
//	}


	bp = free_list;
	int cnt = 0;

	while(NEXT_FREE(bp) != NULL){
		cnt++;
		ret = block_info(bp);
		bp = NEXT_FREE(bp);
		if (HDRP(bp) == free_list) break;
		if (cnt == 3){
			 break;
		}
	}
//	printf("final position = hd %p ft %p\n", HDRP(bp), FTRP(bp));
//	block_info(bp);
	
//	bp = free_list;
//	block_info(bp);
//	bp = NEXT_FREE(bp);
//	block_info(bp);

	printf("!@#!@#!/////freecheck end////\n\n");
	return ret;
	
}



