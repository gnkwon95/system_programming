/*nd the maximum size of web objects to be cached */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <stdlib.h>

/* The cache will be represented as a linked list of web objects
   The eviction policy will be LRU and each object will hold a
   timestamp indicating when it was last used */

typedef struct web_object{
  char *data;
  unsigned int timestamp;
  unsigned int size;
  char* path;
  struct web_object* next;
} web_object;

typedef struct cache_LL{
  web_object* head;
  unsigned int size;
}cache_LL;

void cache_init();
web_object* checkCache(cache_LL* cache, char* path);
void addToCache(cache_LL* cache, char* data, char* path, unsigned int addSize);
void evictAnObject(cache_LL* cache);
