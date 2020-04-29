//------------------------------------------------------------------------------
//
// memtrace
//
// trace calls to the dynamic memory manager
//
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memlog.h>
#include <memlist.h>
#include "callinfo.h"

//
// function pointers to stdlib's memory management functions
//
static void *(*mallocp)(size_t size) = NULL;
static void (*freep)(void *ptr) = NULL;
static void *(*callocp)(size_t nmemb, size_t size);
static void *(*reallocp)(void *ptr, size_t size);

//
// statistics & other global variables
//
static unsigned long n_malloc  = 0;
static unsigned long n_calloc  = 0;
static unsigned long n_realloc = 0;
static unsigned long n_allocb  = 0;
static unsigned long n_freeb   = 0;
static item *list = NULL;

//
// init - this function is called once when the shared library is loaded
//
__attribute__((constructor))
void init(void)
{
  char *error;

  LOG_START();

  // initialize a new list to keep track of all memory (de-)allocations
  // (not needed for part 1)
  list = new_list();

}

//
// fini - this function is called once when the shared library is unloaded
//
__attribute__((destructor))
void fini(void)
{
  // ...

  LOG_STATISTICS(n_allocb, n_allocb/(n_malloc + n_calloc + n_realloc), n_freeb);

  LOG_STOP();

  // free list (not needed for part 1)
  free_list(list);
}

// ...

void *malloc(size_t size){
	if (!mallocp){
		mallocp = dlsym(RTLD_NEXT, "malloc");
	}
	int* p = mallocp(size);	
	LOG_MALLOC(size, p); // res = resister. mallocp print pointer address 0xff?
	n_malloc++;
	n_allocb += size;

	alloc(list, p, size); // do sth with this list. I should ADD ptr to the list, and the size


	return p;	
}


void *calloc(size_t nmemb, size_t size){
	if (!callocp){
		callocp = dlsym(RTLD_NEXT, "calloc");
	}
	int* p = callocp(nmemb, size);

	LOG_CALLOC(nmemb, size, p);
	n_calloc++;
	n_allocb += size*nmemb;

	alloc(list, p, size*nmemb);

	return p;
}

void *realloc(void *ptr, size_t size){
	if (!reallocp){
		reallocp = dlsym(RTLD_NEXT, "realloc");
	}
	item *tmp = find(list, ptr);

	int* p = reallocp(ptr, size);

	LOG_REALLOC(ptr, size, p);


//	fprintf(stderr, "-----\n");
	item *head = list;
	item *cur;
	cur = list->next;
	while ((cur != NULL) && (cur->ptr != tmp->ptr)) {
//		fprintf(stderr, "-----\n");
		list = cur; cur = cur->next;
	}

	list->next = cur->next;
	list = head;

	dealloc(list, ptr);
	alloc(list, p, size);

	n_realloc++;
	n_allocb += size;// need work after free
//	n_freeb += tmp->size;	// find ptr - list->ptr.size 
	return p;
}


void free(void *ptr){
	if (!freep){
		freep = dlsym(RTLD_NEXT, "free");
	}
	item *findfree = find(list, ptr);

	freep(ptr);
	
	LOG_FREE(ptr);
//	n_freeb += findfree->size;
}


