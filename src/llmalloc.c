#include "../inc/llmalloc.h"
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

static block_metadata* heap_tail = NULL; //effectively const, if u call the init function twice ure retarded 
static block_metadata* heap_head = NULL;

static size_t total_size = 0;

static void *expand_heap(const size_t);
static void *preallocated(const size_t);
static block_metadata *coalesce(block_metadata*);
static block_metadata *split(block_metadata *, size_t);
inline static size_t align(const size_t);
inline static size_t trim_top();


int heap_check(void) {
    if (!heap_tail) return 0;
    size_t meta = align(sizeof(block_metadata));
    block_metadata *cur = heap_tail;
    // basic cycle detection (Floyd) to catch cycles quickly
    block_metadata *slow = cur, *fast = cur;
    while (fast) {
        fast = fast->next;
        if (!fast) break;
        fast = fast->next;
        slow = slow->next;
        if (fast == slow) {
            write(1, "heap_check: cycle detected\n", 27);
            return 1;
        }
    }

    // physical adjacency + prev/next coherence
    while (cur) {
        // prev/next coherence
        if (cur->next && cur->next->prev != cur) {
            write(1, "heap_check: next->prev mismatch\n", 33);
            return 2;
        }
        if (cur->prev && cur->prev->next != cur) {
            write(1, "heap_check: prev->next mismatch\n", 33);
            return 3;
        }
        // physical adjacency check
        if (cur->next) {
            uintptr_t expected_next = (uintptr_t)cur + meta + cur->size;
            if ((uintptr_t)cur->next != expected_next) {
                write(1, "heap_check: non-contiguous physical layout\n", 43);
                return 4;
            }
        }
        cur = cur->next;
    }
    return 0;
}


void* malloc(const size_t size){
    size_t aligned_size = align(size);
    void *temp = preallocated(aligned_size);
    if(!temp){
       temp = expand_heap(aligned_size);
    }
    heap_check();
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
    size_t space_to_open = (size + align(sizeof(block_metadata)));
    if(block->size < space_to_open + MY_MALLOC_ALIGN){
        block->free = false;
        //if block is exactly the allocated size(or too tiny for splitting) just give it like a perfect fit
        return block;
    }
    block_metadata *new = (void*)((uint8_t*)block + space_to_open);
    new->size = block->size - space_to_open;
    new->next = block->next;
    if(new->next){
        new->next->prev = new;
    }
    else{
        heap_head = new;
    }
    new->free = true;
    new->prev = block;
    block->next = new;
    block->size = size;
    block->free = false;
    return block;
}

static void *expand_heap(const size_t size){
    block_metadata metadata = {
        .size = size,
        .free = false,
        .next = NULL,
        .prev = NULL
    };
    size_t effective_size = size+align(sizeof(block_metadata));
    block_metadata *temp_head = sbrk(effective_size);
    if(temp_head == (void*)-1){
        return NULL;
    }
    *temp_head = metadata;
    if(heap_head){
        heap_head->next = temp_head;
        temp_head->prev = heap_head;
    }
    heap_head = temp_head;
    total_size+=effective_size;
    if(!heap_tail){
        heap_tail = heap_head;
    }
    return (uint8_t*)temp_head + align(sizeof(block_metadata));
}

void free(void *block){
    if(!block){
        return;
    }
    block_metadata *metadata = (void*)((uint8_t*)block-align(sizeof(block_metadata)));
    if(metadata->free){
        errno = EALREADYFREE;
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
    heap_check();
}


static block_metadata *coalesce(block_metadata *block){
    // Merge with previous
    if (block->prev && block->prev->free) {
        block_metadata *prev = block->prev;
        prev->size += align(sizeof(block_metadata)) + block->size;
        prev->next = block->next;
        if (block->next)
            block->next->prev = prev;
        block = prev;
    }

    // Merge with next
    if (block->next && block->next->free) {
        block_metadata *next = block->next;
        block->size += align(sizeof(block_metadata)) + next->size;
        block->next = next->next;
        if (next->next)
            next->next->prev = block;
    }
    return block;
}

inline static size_t trim_top(){
    if(!heap_head)
        return 0;


    block_metadata *last_block = heap_head;


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
