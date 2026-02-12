#ifndef _MY_MALLOC_H_
#define _MY_MALLOC_H_
#include<stddef.h>
#include<stdbool.h>
#include<stdint.h>

#define ENOTINIT 0
#define EALREADYFREE 1
#define ECANTFREE 2
//512mb of memory will never be given back to the cpu after being allocated, this is to create a
//fast arena where I can allocate memory without needing to syscall
#ifndef PREALLOC_TRESHOLD
    #define PREALLOC_TRESHOLD 512UL * 1024 * 1024
#endif //PREALOC_TESHOLD

//algned to 16 bytes bcuz sum homophobic bs standard idk bra
#ifndef MY_MALLOC_ALIGN 
    #define MY_MALLOC_ALIGN 16
#endif //MY_MALLOC_ALIGN

typedef struct _Block_Metadata{
    size_t size;
    bool free;
    struct _Block_Metadata *next;
    struct _Block_Metadata *prev;
} block_metadata;

void *malloc(size_t size);
void free(void*);
size_t get_heap_usage();
#endif //_MY_MALLOC_H_