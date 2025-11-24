#include <bits/stdc++.h>
#include <pthread.h>
#include <semaphore.h>
#include <chrono>
#include <unistd.h>
#define pii pair<int,int>
#define F first
#define S second

using namespace std;

struct Job {
    string sort_type = "bubble";
    int l, r;
    int mid = -1; // for merge sort
    int done = 0;
    // merge: [l, mid), [mid, r)
};

int total_nums = 0;
vector<int> vec;
pthread_mutex_t vec_mutex = PTHREAD_MUTEX_INITIALIZER;
vector<pii> section_bound;
sem_t job_sem;
pthread_mutex_t job_sem_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t completion_sem;
pthread_mutex_t completion_sem_mutex = PTHREAD_MUTEX_INITIALIZER;
queue<Job> job_que;
pthread_mutex_t job_que_mutex = PTHREAD_MUTEX_INITIALIZER;
vector<Job> completed_jobs;
pthread_mutex_t completed_jobs_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_per_pd(){
    total_nums=0;
    vec.clear();
    section_bound.clear();
    sem_init(&job_sem, 0, 0);
    sem_init(&completion_sem, 0, 0);
    queue<Job> empty;
    swap( job_que, empty );
    completed_jobs.clear();
}

bool cmp(Job a, Job b){
    return a.l < b.l;
}


void push_job_safe(const Job &job){
    pthread_mutex_lock(&job_que_mutex);
    job_que.push(job);
    pthread_mutex_unlock(&job_que_mutex);
    sem_post(&job_sem);
}

void push_completion_job_safe(const Job &job){
    pthread_mutex_lock(&completed_jobs_mutex);
    completed_jobs.push_back(job);
    pthread_mutex_unlock(&completed_jobs_mutex);
    sem_post(&completion_sem);
}

void bubble_sort(int l, int r){
    // cout << "Bubble sort segment [" << l << ", " << r << ")\n";
    for(int i = l; i < r - 1; ++i){
        for(int j = l; j < r - 1 - (i - l); ++j){
            if(vec[j + 1] < vec[j]){
                swap(vec[j + 1], vec[j]);
            }
        }
    }
    // cout << "Bubble sort segment [" << l << ", " << r << ") done.\n";
}

void merge_seg(int l, int mid, int r){
    // cout << "Merge segment [" << l << ", " << mid << ") and [" << mid << ", " << r << ")\n";
    vector<int> tmp_vec;
    int i = l, j = mid;
    while(i < mid && j < r){
        if(vec[i] <= vec[j]){
            tmp_vec.push_back(vec[i]);
            i++;
        } else {
            tmp_vec.push_back(vec[j]);
            j++;
        }
    }
    while(i < mid){
        tmp_vec.push_back(vec[i]);
        i++;
    }
    while(j < r){
        tmp_vec.push_back(vec[j]);
        j++;
    }
    for(int i = l; i < r; i++){
        vec[i] = tmp_vec[i - l];
    }
    // cout << "Merge segment [" << l << ", " << r << ") done.\n";
}

void* dispatcher_thread_function(void* arg){
    while(true){
        if(completed_jobs.size() == 1 && completed_jobs[0].l == 0 && completed_jobs[0].r == total_nums && job_que.size() == 0){
            push_job_safe(Job{"exit", 0, 0});
            break;
        }
        sem_wait(&completion_sem); // some job is completed => 判斷一次能不能合併 (不會重複判斷 只在有新完成 即有可能出現新merge時判斷)
        // int completion_sem_value=0;
        // sem_getvalue(&completion_sem, &completion_sem_value);
        // cout << "Dispatcher: a job completed, checking for possible merges...\n completion status: " << completion_sem_value << " completion que size: " << completed_jobs.size()  << endl;
        pthread_mutex_lock(&completed_jobs_mutex);
        sort(completed_jobs.begin(), completed_jobs.end(), cmp);

        for(int i=0;i<completed_jobs.size();i++){
            if(i+1 < completed_jobs.size() && 
               completed_jobs[i].r == completed_jobs[i+1].l &&
               (
               ((completed_jobs[i].r - completed_jobs[i].l) == (completed_jobs[i+1].r - completed_jobs[i+1].l) &&
               completed_jobs[i].l % (2 * (completed_jobs[i].r - completed_jobs[i].l)) == 0)
                ||
               (completed_jobs[i+1].r == total_nums &&
               completed_jobs[i].l % (2 * (completed_jobs[i].r - completed_jobs[i].l)) == 0)  
               )
            ){
                // condition 1: 避免出界
                // condition 2: segment相鄰
                // condition 3: segment長度相同
                // condition 4: segment為同一父節點 (特判最後一個segment)

                // can be merged
                int l = completed_jobs[i].l;
                int mid = completed_jobs[i].r;
                int r = completed_jobs[i+1].r;
                completed_jobs[i].done = 1;
                completed_jobs[i+1].done = 1;
                push_job_safe(Job{"merge", l, r, mid});
                i++; // skip next one
            }
        }
        
        for(int i=0;i<completed_jobs.size();i++){
            if(completed_jobs[i].done == 1){
                completed_jobs.erase(completed_jobs.begin() + i);
                i--;
            }
        }
        pthread_mutex_unlock(&completed_jobs_mutex);

    }
    // cout << "Dispatcher: all jobs completed, exiting.\n";
    return nullptr;
}

void* worker_thread_function(void* arg){
    while(true){
        sem_wait(&job_sem);
        // ----------------
        // int job_sem_value=0;
        // sem_getvalue(&job_sem, &job_sem_value);
        // cout << "Worker: a job is available, fetching...\n job status: "<< job_sem_value << " job que size: " << job_que.size() << endl;
        pthread_mutex_lock(&job_que_mutex);
        Job job = job_que.front();
        if(job.sort_type == "exit"){
            pthread_mutex_unlock(&job_que_mutex);
            sem_post(&job_sem); // let other workers see exit job
            break;
        }
        job_que.pop();
        pthread_mutex_unlock(&job_que_mutex);

        if(job.sort_type == "bubble"){
            bubble_sort(job.l, job.r);
        } else if(job.sort_type == "merge"){
            merge_seg(job.l, job.mid, job.r);
        } else {
            cerr << "[Error]: unknown job type\n";
            exit(1);
        }

        // ----------------
        push_completion_job_safe(job);
    }
    // cout << "Worker: all jobs completed, exiting.\n";
    return nullptr;
}

void multi_thread_merge_sort(int n) {
    /*
        n: number of threads
    */
    // cout << "Create " << n << "+1 threads\n";
    pthread_t dispatcher;
    int dispatcher_create_status = pthread_create(&dispatcher, nullptr, dispatcher_thread_function, nullptr);
    vector<pthread_t> worker_thread_pool(n);
    for(int i=0;i<n;i++){
        int worker_thread_create_status = pthread_create(&worker_thread_pool[i], nullptr, worker_thread_function, nullptr);
        if(worker_thread_create_status != 0){
            cerr << "Failed to create thread\n error code: " << worker_thread_create_status << "\n";
            exit(1);
        }
    }

    // start timing
    auto t1 = chrono::high_resolution_clock::now();

    // push 8 bubble sort jobs
    // cout << "Push 8 bubble sort jobs\n";
    for(int i = 0; i < 8; i++){
        int l = section_bound[i].F;
        int r = section_bound[i].S;
        push_job_safe(Job{"bubble", l, r});
    }
    // int job_sem_value=0;
    // sem_getvalue(&job_sem, &job_sem_value);
    // cout << "job_sem status: " << job_sem_value << "\n";
    // do jobs with multi-threads
    // ... done in worker_thread_function

    // wait for all jobs done
    for(int i=0;i<n;i++){
        pthread_join(worker_thread_pool[i], nullptr);
    }
    pthread_join(dispatcher, nullptr);
    // cout << "All threads joined.\n";
    auto t2 = chrono::high_resolution_clock::now();
    chrono::duration<double, std::milli> elapsed = t2 - t1;
    cout << "worker thread #" << n << ", elapsed " << std::fixed << std::setprecision(6) << elapsed.count() << " ms\n";

    // write output_n.txt
    string outname = "output_" + to_string(n) + ".txt";
    ofstream ofs(outname);
    if(!ofs){
        cerr<<"Cannot open " << outname << " for writing\n";
        exit(1);
    }
    for(int i=0;i<total_nums;i++){
        ofs << vec[i] << ' ';
    }
    ofs.close();

    // cleanup sem
    sem_destroy(&job_sem);
    sem_destroy(&completion_sem);

}

signed main(){


    for(int pd=1;pd<=8;pd++){ // parallel degree
        init_per_pd();
        ifstream ifs("input.txt");
        if(!ifs){
            cerr << "[Error]: open input.txt fail\n";
            exit(1);
        }
        int n;
        ifs >> n;
        total_nums = n;
        vec.resize(n);
        for(int i = 0; i < n; ++i) ifs >> vec[i];
        ifs.close();
        int section_num = n / 8;
        int section_rem = n % 8;
        for(int i = 0; i < 8; ++i){
            int l = i * section_num;
            int r = (i + 1) * section_num;
            // range: [l, r)
            if (i == 7) r += section_rem;
            section_bound.push_back({l, r});
        }
        // cout << "Start multi-threaded merge sort with " << pd << " threads.\n";
        multi_thread_merge_sort(pd);
        // cout << "Multi-threaded merge sort completed.\n";
    }

    return 0;


}
/*
[0, 127345)
[0, 63672) [63672, 127345)
[0, 31836) [31836, 63672) [63672, 95508) [95508, 127345)
[0, 15918) [15918, 31836) [31836, 47754) [47754, 63672) [63672, 79590) [79590, 95508) [95508, 111426) [111426, 127345)
*/

// 問題: 只有一個worker離開