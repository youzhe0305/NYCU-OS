#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define POOL_SIZE 20000
#define HEADER_SIZE 32
#define NUM_LEVELS 11
#define ALLOC_SIZE_UNIT 32 


typedef struct ChunkHeader {
    size_t chunk_size;            // 8 bytes in 64-bit system
    int is_free;                 // 4 bytes
    struct ChunkHeader *next_chunk; // 8 bytes
    struct ChunkHeader *prev_chunk; // 8 bytes
    char padding[4];                // 4 bytes, padding the chunk to 32 bytes
} ChunkHeader;

void *pool_start = NULL;
ChunkHeader *free_lists[NUM_LEVELS]; // 用metadata來當作linklist內容維護

static int compute_level(size_t size) {
    size_t size_levels = size / ALLOC_SIZE_UNIT; // 32
    for (int i = 1; i < NUM_LEVELS; i++) {
        if (size_levels == 0) {
            return i - 1;
        }
        size_levels = size_levels >> 1;
    }
    return NUM_LEVELS - 1;
}

static void add_to_free_list(ChunkHeader *header) {

    header->is_free = 1;
    int level = compute_level(header->chunk_size);
    header->next_chunk = NULL;

    if (free_lists[level] == NULL) {
        free_lists[level] = header;
        header->prev_chunk = NULL;
    } else {
        ChunkHeader *current = free_lists[level];
        while (current->next_chunk != NULL) { // find the tail
            current = current->next_chunk;
        }
        current->next_chunk = header;
        header->prev_chunk = current;
    }
}

static void remove_from_free_list(ChunkHeader *header) {
    
    int level = compute_level(header->chunk_size);
    if (header->prev_chunk) { // not head
        header->prev_chunk->next_chunk = header->next_chunk;
    } else { // head
        free_lists[level] = header->next_chunk;
    }

    if (header->next_chunk) { // not tail
        header->next_chunk->prev_chunk = header->prev_chunk;
    }

    header->next_chunk = NULL;
    header->prev_chunk = NULL;
    header->is_free = 0;
}

static void init_pool() {
    pool_start = mmap(
        NULL, // 自己決定addr
        POOL_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );

    for (int i = 0; i < NUM_LEVELS; i++) {
        free_lists[i] = NULL;
    }

    ChunkHeader *initial_chunk = (ChunkHeader *)pool_start; // init chunk的位置 == pool_start拿到的位置
    initial_chunk->chunk_size = POOL_SIZE - HEADER_SIZE;

    add_to_free_list(initial_chunk);
}

static size_t round_up_32(size_t size) {
    if (size == 0) {
        return ALLOC_SIZE_UNIT; // 即使請求 0, 至少也分配 32
    }
    return (size + 31) / 32 * 32;
}

static ChunkHeader *find_the_biggest_free_chunk(){
    ChunkHeader *biggest_chunk = NULL;
    for (int i = NUM_LEVELS - 1; i >= 0; i--) {
        ChunkHeader *current = free_lists[i];
        while (current != NULL) {
            if (biggest_chunk == NULL || current->chunk_size > biggest_chunk->chunk_size) {
                biggest_chunk = current;
            }
            current = current->next_chunk;
        }
        if (biggest_chunk != NULL) {
            break;
        }
    }
    return biggest_chunk;
}

void *malloc(size_t size){

    if (pool_start == NULL) {
        init_pool();
    }

    if( size == 0) {
        ChunkHeader *biggest_chunk = find_the_biggest_free_chunk();
        char buffer[128];
        sprintf(buffer, "Max Free Chunk Size = %zu\n", biggest_chunk->chunk_size);
        write(STDOUT_FILENO, buffer, strlen(buffer));
        munmap(pool_start, POOL_SIZE);
        pool_start = NULL;
        return NULL;
    }

    if (size == -1){ // for debug
        ChunkHeader *biggest_chunk = find_the_biggest_free_chunk();
        char buffer[128];
        if(biggest_chunk == NULL){
            sprintf(buffer, "[DEBUG] No Free Chunk Available\n");
            write(STDOUT_FILENO, buffer, strlen(buffer));
        }
        else{
            sprintf(buffer, "[DEBUG] Max Free Chunk Size = %zu\n", biggest_chunk->chunk_size);
            write(STDOUT_FILENO, buffer, strlen(buffer));
        }
        return NULL;
    }

    size = round_up_32(size);
    int level = compute_level(size);
    ChunkHeader *level_start;
    ChunkHeader *best_fit_chunk = NULL;
    for(int i = level; i < NUM_LEVELS; i++){
        ChunkHeader *level_start = free_lists[i];
        while (level_start != NULL) {
            if (level_start->chunk_size >= size) { // large enough
                if (best_fit_chunk == NULL || level_start->chunk_size < best_fit_chunk->chunk_size) { // more proper fit
                    best_fit_chunk = level_start;
                }
            }
            level_start = level_start->next_chunk;
        }
        if (best_fit_chunk != NULL) { // find a best fit chunk
            break;
        }
    }
    if (best_fit_chunk == NULL) {
        return NULL; // no any enough chunk
    }
    remove_from_free_list(best_fit_chunk);
    size_t remaining_size = best_fit_chunk->chunk_size - size;

    if (remaining_size >= HEADER_SIZE + ALLOC_SIZE_UNIT) { // can be split
        ChunkHeader *splited_chunk = (ChunkHeader *)((char *)best_fit_chunk + HEADER_SIZE + size); // 1. to 1 byte unit 2. offset to next chunk's position
        splited_chunk->chunk_size = remaining_size - HEADER_SIZE;
        add_to_free_list(splited_chunk);
        best_fit_chunk->chunk_size = size;
    }

    return (char *)best_fit_chunk + HEADER_SIZE; // where the data section start
}

void free(void *ptr){
    // user get data section position, offset to chunk position
    ChunkHeader *chunk_to_free = (ChunkHeader *)((char *)ptr - HEADER_SIZE);
    ChunkHeader *next_chunk_in_mem = (ChunkHeader *)((char *)ptr + chunk_to_free->chunk_size);
    if ((char *)next_chunk_in_mem >= (char *)pool_start + POOL_SIZE) { // out of pool range
        next_chunk_in_mem = NULL;
    }
    ChunkHeader *prev_chunk_in_mem = NULL;
    ChunkHeader *current = (ChunkHeader *)pool_start; 
    if(chunk_to_free == (ChunkHeader *)pool_start){ // first chunk, no previous
        prev_chunk_in_mem = NULL;
    }
    else{
        while (current != NULL && (char *)current < (char *)chunk_to_free) { // find the previoud one from start
            ChunkHeader *next = (ChunkHeader *)((char *)current + HEADER_SIZE + current->chunk_size);
            if (next == chunk_to_free) {
                prev_chunk_in_mem = current;
                break;
            }
            current = next;
        }
    }

    ChunkHeader *merged_chunk = chunk_to_free;
    merged_chunk->is_free = 1;
    if(next_chunk_in_mem != NULL && next_chunk_in_mem->is_free){
        remove_from_free_list(next_chunk_in_mem);
        merged_chunk->chunk_size += HEADER_SIZE + next_chunk_in_mem->chunk_size;
    }
    if(prev_chunk_in_mem != NULL && prev_chunk_in_mem->is_free){
        remove_from_free_list(prev_chunk_in_mem);
        merged_chunk = prev_chunk_in_mem;
        merged_chunk->chunk_size += HEADER_SIZE + chunk_to_free->chunk_size;
    }
    add_to_free_list(merged_chunk);
}