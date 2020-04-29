#include <stdlib.h>

int main(void)
{
  void *a, *b, *c;

  a = calloc(1, 24);
  b = calloc(4, 40);
  c = malloc(123);
  free(a);
  free(c);
 // a = malloc(0x5123);
//  a = calloc(1,1024);
 // b = calloc(1, 12322);
//  a = malloc(123);
 // c = realloc( (void*) 123, 1024);
 // free((void*)0x51232);
//  free(c);
//  free(a);
 // free(b);

  return 0;
}
