#include "../inc/llmalloc.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

static void* heap_tail = NULL; //effectively const, if u call the init function twice ure retarded 
static void* heap_head = NULL;

static size_t total_size = 0;

static void *expand_heap(const size_t);
static void *preallocated(const size_t);
static block_metadata *coalesce(block_metadata*);
static block_metadata *split(block_metadata *, size_t);
inline static size_t align(const size_t);
inline static size_t trim_top();

void init_malloc(){
    if(!heap_tail && !heap_head){
        heap_tail = sbrk(0);
    }
}

void* malloc(const size_t size){
    if(!heap_tail){
        errno = ENOTINIT;
        return NULL;
    }
    size_t aligned_size = align(size);
    void *temp = preallocated(aligned_size);
    if(!temp){
       temp = expand_heap(aligned_size);
    }
    return temp;
}


inline static size_t align(const size_t size){
    return (size + (MY_MALLOC_ALIGN - 1)) & ~(MY_MALLOC_ALIGN - 1);
}

static void *preallocated(const size_t size){
    if(total_size <= 0)
        return NULL;
    block_metadata *temp = heap_tail;
    while(temp){
        if(temp->free&&temp->size>=size){
            block_metadata *new = split(temp, size);
            return (uint8_t*)new + align(sizeof(block_metadata));            
        }
        temp = temp->next;
    }
    return NULL;
}

//returns the new split block
static block_metadata *split(block_metadata *block, size_t size){
    if(size == block->size){
        block->free = false;
        return block;
    }
    block_metadata *next = block->next;
    size_t space_to_open = (size + align(sizeof(block_metadata)));
    block_metadata *new;
    if(next){
        new = (void*)((uint8_t*)next - space_to_open);
        next->prev = new;
    }
    else
        new = (void*)((uint8_t*)block + ((align(sizeof(block_metadata))+block->size) - space_to_open));
    block->next = new;
    block->size -=space_to_open;
    new->prev = block;
    new->next = next;
    new->free = false;
    new->size = size;
    return new;
}

static void *expand_heap(const size_t size){
     block_metadata metadata = {
        .size = size,
        .free = false,
        .next = NULL
    };
    size_t effective_size = size+align(sizeof(block_metadata));
    block_metadata *temp_head = sbrk(effective_size);
    if(temp_head == (void*)-1){
        perror("Out of memory\n");
        return NULL;
    }
    *temp_head = metadata;
    if(heap_head){
        ((block_metadata*)heap_head)->next = temp_head;
        temp_head->prev = heap_head;
    }
    heap_head = temp_head;
    total_size+=effective_size;
    return (uint8_t*)temp_head + align(sizeof(block_metadata));
}

void free(void *block){
    block_metadata *metadata = ((block_metadata*)block)-1;
    if(metadata->free){
        errno = EALREADYFREE;
        perror("Trying to free already free'd memory");
        return;
    }
    metadata->free = true;
    const block_metadata *newFree = coalesce(metadata);
    if(!newFree->next){
        heap_head = (void*)newFree;
    }
    if(total_size > PREALLOC_TRESHOLD){
        total_size -= trim_top();

    }
}


static block_metadata *coalesce(block_metadata *metadata){
    block_metadata *prev = metadata->prev;
    block_metadata *next = metadata->next;
    block_metadata *ret = NULL;
    size_t size = metadata->size;
    size_t size_pre = size;
    if(prev && prev->free){
        size_pre += prev->size;
        size += prev->size ;
        size += align(sizeof(block_metadata));
        if(next && next->free){
            size_pre+=next->size;
            size+=next->size;
            size+=align(sizeof(block_metadata));
            prev->next = next->next;
        }
        else 
            prev->next = next;
        if(prev->next)
            prev->next->prev = prev;
        ret = prev;
    }else {
        if(next && next->free){
            size_pre += next->size;
            size+=next->size;
            size+=align(sizeof(block_metadata));
            metadata->next = next->next;
            if(metadata->next)
                metadata->next->prev = metadata;
        }
        ret = metadata;
    }
    ret->size = size;
    return ret;
}

inline static size_t trim_top(){
    if(!heap_head)
        return 0;


    block_metadata *last_block = (block_metadata *) heap_head;


    if(last_block->free){
        size_t size_to_remove = total_size - PREALLOC_TRESHOLD;

        size_t meta_size = align(sizeof(block_metadata));
        void *current_break = sbrk(0);
        void* block_end = (uint8_t*)last_block + meta_size + last_block->size;

        if(block_end != current_break){
            return 0;
        }

        
        if(size_to_remove<last_block->size){
            last_block->size -= size_to_remove;
            if(sbrk((0-size_to_remove)) == (void*)-1){
                return 0;
            }
            return size_to_remove;
        }
        else if (size_to_remove <= (last_block->size +align(sizeof(block_metadata)))) {
            size_to_remove = last_block->size +align(sizeof(block_metadata));
            block_metadata *newHead = last_block->prev;
            if(newHead){
                newHead->next = last_block->next;
            }
            heap_head = newHead;
            if(sbrk((0-size_to_remove)) == (void*)-1){
                return 0;
            }
            return size_to_remove;
        }
        else {
            return 0;
        }
    }
    return 0;
}
size_t get_heap_usage(){
    return total_size;
}
