#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <sys/time.h>
#include <cmath>
#include <cstring>
#include <iomanip>
using namespace std;

#define HASH_TABLE_RATIO 2.0

int frame_sizes[] = {4096, 8192, 16384, 32768, 65536};

struct TraceItem{
    bool is_write; // ture: write, false: read
    unsigned long long page_number;
};

class Timer{
private:
    timeval start_time, end_time;
    double elapsed_sec(){
        return (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec) / 1e6;
    }
public:
    void start(){
        gettimeofday(&start_time, NULL);
    }
    double stop(){
        gettimeofday(&end_time, NULL);
        return elapsed_sec();
    }
    void clear(){
        memset(&start_time, 0, sizeof(start_time));
        memset(&end_time, 0, sizeof(end_time));
    }
};

vector<TraceItem> traces;

class PageMeta { // page link list
public:
    PageMeta *prev;
    PageMeta *next;
    unsigned long long page_number;
    bool dirty;
    string list_type = "NONE"; // NONE, WORKING, CLEAN, DIRTY
    // for hash table if collision => linklist in the bucket
    PageMeta *hash_next; 
    void initialize(unsigned long long page_number, bool dirty = false){
        this->page_number = page_number;
        this->dirty = dirty;
        this->prev = nullptr;
        this->next = nullptr;
        this->hash_next = nullptr;
    }
};

class SimpleHashTable {
private:
    vector<PageMeta*> buckets;
    int size;
    int mask;

public:
    void init(int frame_size) {
        // Find the nearest power of 2 as Hash Table size
        size = 1;
        while (size < frame_size * HASH_TABLE_RATIO) {
            size <<= 1;
        }
        buckets.assign(size, nullptr);
        mask = size - 1;
    }

    inline int hash_func(unsigned long long key) {
        return (key ^ (key >> 5)) & mask;
    }

    inline PageMeta* get(unsigned long long page_number) {
        int idx = hash_func(page_number);
        PageMeta* curr = buckets[idx];
        while (curr) {
            if (curr->page_number == page_number) return curr;
            curr = curr->hash_next;
        }
        return nullptr;
    }

    inline void put(PageMeta* node) {
        int idx = hash_func(node->page_number);
        node->hash_next = buckets[idx]; // insert in front
        buckets[idx] = node;
    }

    inline void remove(PageMeta* node) {
        int idx = hash_func(node->page_number);
        PageMeta* curr = buckets[idx];
        PageMeta* prev = nullptr;
        
        while (curr) {
            if (curr == node) {
                if (prev) {
                    prev->hash_next = curr->hash_next;
                } else {
                    buckets[idx] = curr->hash_next;
                }
                return;
            }
            prev = curr;
            curr = curr->hash_next;
        }
    }
};

class LRUPageCache {
private:
    vector<PageMeta> node_pool; // 一次先要一整塊存page meta的記憶體，避免一直new/delete
    vector<PageMeta*> free_list; // 有哪些page空間是空的可以用
    int size;
    SimpleHashTable map;
    PageMeta* head = nullptr; // MRU
    PageMeta* tail = nullptr; // LRU 
    long long hit = 0;
    long long miss = 0;
    long long write_back = 0;
public:
    LRUPageCache(int frames) {
        node_pool.resize(frames);
        free_list.reserve(frames);
        for (int i = 0; i < frames; i++) {
            free_list.push_back(&node_pool[i]);
        }
        map.init(frames);
    }
    void access_page(TraceItem trace) {
        PageMeta* target_page = map.get(trace.page_number);

        if (target_page){ // hit
            hit++;
            if (trace.is_write) target_page->dirty = true;

            // to MRU
            if (target_page != head){
                // remove
                if (target_page->prev) target_page->prev->next = target_page->next;
                if (target_page->next) target_page->next->prev = target_page->prev;
                if (target_page == tail) tail = target_page->prev;
                // to head
                target_page->next = head;
                target_page->prev = nullptr;
                if (head) head->prev = target_page;
                head = target_page;
            }
        } else { // miss
            miss++;
            PageMeta* new_cache_page = nullptr;
            if (!free_list.empty()){
                new_cache_page = free_list.back();
                free_list.pop_back();
            } else { // the size of cache is full
                PageMeta* page_to_remove = tail;

                if (page_to_remove->dirty) write_back++; // is written, write back
                map.remove(page_to_remove);

                if (tail->prev) tail = tail->prev, tail->next = nullptr; // is tail
                else head = nullptr, tail = nullptr; // is tail & head

                new_cache_page = page_to_remove; // Reuse memory
            }
            new_cache_page->initialize(trace.page_number, trace.is_write);

            // to MRU
            new_cache_page->next = head;
            new_cache_page->prev = nullptr;
            if (head) head->prev = new_cache_page;
            head = new_cache_page;
            if (!tail) tail = head;

            map.put(new_cache_page);
        }
    }
    void print_stats(int frames) {
        printf("%d\t%lld\t%lld\t\t%.10f\t\t%lld\n", 
               frames, hit, miss, (double)miss / (hit + miss), write_back);
    }
    void print_linklist(){
        PageMeta* curr = head;
        while (curr){
            cout << "[" << curr->page_number << (curr->dirty ? " D" : " C") << "] ";
            curr = curr->next;
        }
        cout << endl;
    }
};

class CFLRUPageCache {
private:
    vector<PageMeta> node_pool; // 一次先要一整塊存page meta的記憶體，避免一直new/delete
    vector<PageMeta*> free_list; // 有哪些page空間是空的可以用
    int size;
    int working_cap = 0;
    SimpleHashTable map;
    PageMeta* working_head = nullptr; // MRU
    PageMeta* working_tail = nullptr;
    int working_size = 0;
    PageMeta* dirty_head = nullptr;
    PageMeta* dirty_tail = nullptr;
    PageMeta* clean_head = nullptr;
    PageMeta* clean_tail = nullptr;
    long long hit = 0;
    long long miss = 0;
    long long write_back = 0;
public:
    CFLRUPageCache(int frames) {
        node_pool.resize(frames);
        free_list.reserve(frames);
        for (int i = 0; i < frames; i++) {
            free_list.push_back(&node_pool[i]);
        }
        map.init(frames);
        working_cap = frames * 3 / 4;
    }
    void remove_from_list(PageMeta* node) {
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        
        if (node->list_type == "WORKING") {
            if (node == working_head) working_head = node->next;
            if (node == working_tail) working_tail = node->prev;
            working_size--;
        } 
        else if (node->list_type == "CLEAN") {
            if (node == clean_head) clean_head = node->next;
            if (node == clean_tail) clean_tail = node->prev;
        } 
        else if (node->list_type == "DIRTY") {
            if (node == dirty_head) dirty_head = node->next;
            if (node == dirty_tail) dirty_tail = node->prev;
        }
        node->list_type = "NONE";
    }
    void add_to_list(PageMeta* node, string list_type) {
        PageMeta** head_ptr = nullptr;
        PageMeta** tail_ptr = nullptr;
        if (list_type == "WORKING") {
            head_ptr = &working_head; tail_ptr = &working_tail;
            working_size++;
        } else if (list_type == "CLEAN") {
            head_ptr = &clean_head; tail_ptr = &clean_tail;
        } else if (list_type == "DIRTY") {
            head_ptr = &dirty_head; tail_ptr = &dirty_tail;
        }

        node->next = *head_ptr;
        node->prev = nullptr;
        if (*head_ptr) (*head_ptr)->prev = node;
        *head_ptr = node;
        if (!*tail_ptr) *tail_ptr = node;
        
        node->list_type = list_type;
    }
    void move_to_CFlist(){
        if (!working_tail) return;
        PageMeta* node = working_tail;
        remove_from_list(node); 

        // 2. 根據 Dirty 狀態分流
        if (node->dirty) {
            add_to_list(node, "DIRTY");
        } else {
            add_to_list(node, "CLEAN");
        }
    }
    void access_page(TraceItem trace) {
        PageMeta* target_page = map.get(trace.page_number);

        if (target_page){ // hit
            hit++;
            if (trace.is_write) target_page->dirty = true;

            // to MRU
            if (target_page != working_head){
                remove_from_list(target_page);
                add_to_list(target_page, "WORKING");
                if (working_size > working_cap) move_to_CFlist();
            }
        } else { // miss
            miss++;
            PageMeta* new_cache_page = nullptr;
            if (!free_list.empty()){
                new_cache_page = free_list.back();
                free_list.pop_back();
            } else { // the size of cache is full
                PageMeta* page_to_remove = clean_tail; // default remove tail of clean list
                if (!page_to_remove){ // no clean => degrade to LRU
                    page_to_remove = dirty_tail;
                }

                if (page_to_remove->dirty) write_back++; // is written, write back
                map.remove(page_to_remove);
                remove_from_list(page_to_remove);

                new_cache_page = page_to_remove;
            }
            new_cache_page->initialize(trace.page_number, trace.is_write);
            add_to_list(new_cache_page, "WORKING");
            map.put(new_cache_page);
            if (working_size > working_cap) move_to_CFlist();
        }
    }
    void print_stats(int frames) {
        printf("%d\t%lld\t%lld\t\t%.10f\t\t%lld\n", 
               frames, hit, miss, (double)miss / (hit + miss), write_back);
    }
    void print_linklist(){
        PageMeta* curr = working_head;
        while (curr){
            cout << "[" << curr->page_number << (curr->dirty ? " D" : " C") << "] ";
            curr = curr->next;
        }
        cout << endl;
    }
};

void load_trace_file(const char* filename){
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        perror("[Error] Open trace file failed");
        exit(1);
    }

    char op;
    char byte_offset[32];
    traces.reserve(size_t(50 * 1e3 * 1e3)); // 50MB
    // int line_count = 0;
    while (fscanf(fp, " %c %s", &op, byte_offset) == 2) { // 2 arg matched
        // line_count++;
        // if (line_count % 1000000 == 0){

        //     cout << "\rLoaded " << line_count << " / 49000000 line. Complete " << double(line_count) / 49000000 * 100 << "%" << std::flush;
        // }
        unsigned long long addr = strtoull(byte_offset, NULL, 16);
        traces.push_back({(op == 'W'), addr >> 12}); // addr to page number, 4 KB: 2^(2+10)
    }
    fclose(fp);
}

void page_replacement_sim(int frame_size, string mode = "LRU"){

    if (mode == "LRU"){
        LRUPageCache cache(frame_size);
        for (TraceItem trace : traces){
            cache.access_page(trace);
        }
        cache.print_stats(frame_size);
    }
    else if (mode == "CFLRU"){
        CFLRUPageCache cache(frame_size);
        for (TraceItem trace : traces){
            cache.access_page(trace);
        }
        cache.print_stats(frame_size);
    }
}

signed main(int argc, char* argv[]){
    if (argc != 2){
        cout << "Only use exactly one argument: <trace_file>." << endl;
        return 1;
    }
    // cout << "load trace file: " << argv[1] << endl;
    load_trace_file(argv[1]);
    // cout << endl;

    // cout << "initialize timer and start testing..." << endl;
    Timer timer;
    timer.start();

    cout<< "LRU policy:" << endl;
    printf("Frame\tHit\t\tMiss\t\tPage fault ratio\tWrite back count\n");
    for(int frame_size : frame_sizes){
        page_replacement_sim(frame_size, "LRU");
    }

    double elapsed = timer.stop();
    cout << fixed << setprecision(6) << "Elapsed time: " << elapsed << " sec" << endl;

    timer.clear();
    cout << endl;
    timer.start();

    cout<< "CFLRU policy:" << endl;
    printf("Frame\tHit\t\tMiss\t\tPage fault ratio\tWrite back count\n");
    for(int frame_size : frame_sizes){
        page_replacement_sim(frame_size, "CFLRU");
    }

    elapsed = timer.stop();
    cout << fixed << setprecision(6) << "Elapsed time: " << elapsed << " sec" << endl;

}




