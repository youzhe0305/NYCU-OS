#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/time.h>
using namespace std;
using u32 = uint32_t;

double running_time_calculate(const timeval s, const timeval e){
    long sec  = e.tv_sec  - s.tv_sec;
    long usec = e.tv_usec - s.tv_usec;
    return sec + usec / 1000000.0;
}

void init_matrix(vector<vector<u32>> &M, const u32 N){
    for(u32 i=0;i<N;i++)
        for(u32 j=0;j<N;j++){
            M[i][j] = i * N + j;
        }
}

pair<u32, u32*> create_shm_matrix(const u32 N){

    u32 shm_id = shmget(IPC_PRIVATE, sizeof(u32) * N * N, IPC_CREAT | 0600);
    if(shm_id < 0){
        cerr << "[Error]: create_shm_matrix fail\n";
        exit(1);
    }   
    void* addr = shmat(shm_id, nullptr, 0);
    if(addr == (void*)-1){
        cerr << "[Error]: shm attach to automatically assigned addr fail\n";
    }
    u32* int_arr_addr = reinterpret_cast<u32*>(addr);
    return {shm_id, int_arr_addr};
}

void multiply_rows(vector<vector<u32>> &A, vector<vector<u32>> &B, u32* C, const u32 N, u32 process_num, u32 id){
    u32 part_rows = u32(ceil(double(N) / double(process_num)) ); // a process need to deal with how many rows;
    u32 rows_st = id * part_rows;
    u32 rows_ed = min((id + 1) * part_rows, N);
    for(u32 i=rows_st;i<rows_ed;i++){
        for(u32 j=0;j<N;j++){
            C[i * N + j] = 0;
            for(u32 k=0;k<N;k++){
                C[i * N + j] += A[i][k] * B[k][j];
            }
        }
    }

}

void run_matrix_multiply(vector<vector<u32>> &A, vector<vector<u32>> &B, u32* C, const u32 N, u32 process_num){

    struct timeval start, end;
    gettimeofday(&start, 0);

    vector<pid_t> pids;
    for(u32 i=0;i<process_num;i++){ // split C matrix to process_num part
        pid_t pid = fork();
        // share memory C's pointer is get by children process 
        if(pid < 0){
            cerr << "[Error]: Fork fail\n";
            exit(1);
        }
        else if(pid == 0){ // children process;
            multiply_rows(A, B, C, N, process_num, i);
            shmdt(C);
            exit(0);
        }
        else{
            pids.push_back(pid);
        }
    }
    for (pid_t pid : pids) {
        int status = 0;
        waitpid(pid, &status, 0);
    }

    gettimeofday(&end, 0);
    double running_time = running_time_calculate(start, end);
    cout<< "Elapsed time: " << running_time << " sec";
}

u32 check_sum(u32* C, const u32 N){
    u32 sum = 0;
    for(u32 i=0;i<N*N;i++){
        sum += C[i];
    }
    return sum;
}

signed main(){

    u32 n;
    cout<<"Input the matrix dimension: ";
    cin>>n;
    cout<<endl;
    const u32 N = static_cast<u32>(n);
    vector<vector<u32>> A(N, vector<u32>(N));
    vector<vector<u32>> B(N, vector<u32>(N));
    init_matrix(A, N);
    init_matrix(B, N);
    auto [shm_id_C, C] = create_shm_matrix(N);
    for(u32 process_num=1;process_num<=16;process_num++){
        cout<< "Multiplying matrices using " << process_num << " processes" <<endl;
        run_matrix_multiply(A, B, C, N, process_num);
        u32 sum = check_sum(C, N);
        cout << ", Checksum: " << sum << endl;
    }
    shmdt(C);
    shmctl(shm_id_C, IPC_RMID, nullptr);
    return 0;
}