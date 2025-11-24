#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
using namespace std;

struct Program{
    string program_name;
    vector<string> args;
    string infile;
    string outfile;
    bool background = false;
};

class Parser{
    public:
        vector<Program> programs = {};
        bool is_pipeline = false;
        bool parse(vector<string> tokens){
            Program current_program = Program{};
            int inner_command_index = 0;
            for(int i=0;i<tokens.size();i++){
                if(inner_command_index == 0){
                    current_program.program_name = tokens[i];
                    inner_command_index++;
                    continue;
                }
                if(tokens[i] == "&"){
                    current_program.background = true;
                    if(i+1 < tokens.size() && tokens[i+1] != "|"){
                        cerr << "[Error] syntax error: '&' must be at the end of command or before '|'\n";
                        return false;
                    }
                    continue;
                }
                if(tokens[i] == "|"){
                    is_pipeline = true;
                    programs.push_back(current_program);
                    current_program = Program{};
                    inner_command_index = 0;
                    continue;
                }
                if(tokens[i] == ">"){
                    if(i+1 >= tokens.size()){
                        cerr << "[Error] syntax error: missing filename after '>'\n";
                        return false;
                    }
                    current_program.outfile = tokens[i+1];
                    i++;
                    continue;
                }
                if(tokens[i] == "<"){
                    if(i+1 >= tokens.size()){
                        cerr << "[Error] syntax error: missing filename after '<'\n";
                        return false;
                    }
                    current_program.infile = tokens[i+1];
                    i++;
                    continue;
                }
                current_program.args.push_back(tokens[i]);
            }
            programs.push_back(current_program);
            return true;
        }
};

// static make function can only be used in this .cpp file
static void print_command_prefix(){
    cout << ">" << flush; 
    // flush for instant output, otherwise, it will be stores in buffer, and output after endline
    // most env will auto flush, but some doesn't => we handle it
}

static vector<string> tokenize(string command){
    vector<string> command_tokens;
    string token;
    for(int i=0;i<command.length();i++){
        if(command[i] == ' ' || command[i] == '\t' || command[i] == '\n' || command[i] == '\r'){
            if(!token.empty()){
                command_tokens.push_back(token);
                token.clear();
            }
        }
        else if(command[i] == '&' || command[i] == '<' || command[i] == '>' || command[i] == '|'){
            if (!token.empty()) {
                command_tokens.push_back(token);
                token.clear();
            }
            command_tokens.push_back(string(1, command[i]));
        }
        else{
            token.push_back(command[i]);
        }
    }
    if(!token.empty()){ // avoid token that is not pushed
        command_tokens.push_back(token);
        token.clear();
    }
    return command_tokens;
}

static void redirect_handler(Program program){
        if(!program.infile.empty()){
            int file_descriptor = open(program.infile.c_str(), O_RDONLY); // system call
            // file_descriptor: pointer of I/O space, the opened file content will be read from this space
            // STDIN_FILENO = 0, STDOUT_FILENO = 1, STDERR_FILENO = 2 
            if(file_descriptor < 0){
                cerr << "[Error] open infile failed: " << "\n";
                exit(1); // normal error
            }
            if(dup2(file_descriptor, STDIN_FILENO) < 0){ // duplicate file channel's work to stdin
                cerr << "[Error] duplicate infile to STDIN failed: " << "\n";
                exit(1);
            }
            close(file_descriptor);
        }
        if(!program.outfile.empty()){
            int file_descriptor = open(program.outfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(file_descriptor < 0){
                cerr << "[Error] open outfile failed: " << "\n";
                exit(1);
            }
            if(dup2(file_descriptor, STDOUT_FILENO) < 0){
                cerr << "[Error] duplicate outfile to STDOUT failed: " << "\n";
                exit(1);
            }
            close(file_descriptor);
        }
}

static void sigchld_handler(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {} // non-blocking
    // waitpid(pid, &status, 0) => parent will wait untill child process ends
    // waitpid(-1, &status, WNOHANG) => parent will not wait, just check if any child process ends
    // with while, it can know when child process ends, or it will keep doing what parent process should do
}

static vector<char*> args_to_argv(const Program& program){
    vector<char*> argv;
    argv.clear();
    argv.push_back(const_cast<char*>(program.program_name.c_str()));
    for(const string& arg: program.args){
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

static void run_program(Program program){
    if(program.program_name.empty()) return;
    pid_t pid = fork(); // forked process starts from here
    if(pid < 0) {
        cerr << ("[Error] Fork Failed");
        exit(-1); // not standard error
    }
    if(pid == 0){ // child process, exeute the program
        signal(SIGINT, SIG_DFL);
        redirect_handler(program);
        vector<char*> argv = args_to_argv(program);
        int execvp_status = execvp(program.program_name.c_str(), argv.data()); // replace current process with excuted program
        if(execvp_status < 0){
            cerr << "[Error] execvp failed: " << "\n";
        }
        exit(127);
    } else { // parent process, pid is child process' id
        if(!program.background){ // wait for child process
            waitpid(pid, nullptr, 0);
        } else {
            // no need to do any waiting
        }
    }
}

static void run_pipeline_programs(vector<Program> programs){

    int program_num = programs.size();
    if(program_num <= 1) return;
    int input_file_descriptor = STDIN_FILENO; // input channel for program
    int pipe_file_descriptors[2]; // to receive pipe's input, output file descriptor
    // 1: input of pipe, 0: output of pipe => progA -> 1-pipe-0 -> progB
    vector<pid_t> pids;

    
    for(int i=0;i<program_num;i++){
        Program program = programs[i];

        if(i <= program_num - 2){ // not last program => no need to deal with pipeline
            int pipe_status = pipe(pipe_file_descriptors); // create a pipe after this program
            if(pipe_status < 0){
                cerr << "[Error] create pipe failed: " << "\n";
            }
        }

        pid_t pid = fork(); // forked process starts from here
        if(pid < 0) {
            cerr << ("[Error] Fork Failed");
            exit(-1); // not standard error
        }
        if(pid == 0){ // child process, exeute the program
            signal(SIGINT, SIG_DFL);
            if(input_file_descriptor != STDIN_FILENO){
                dup2(input_file_descriptor, STDIN_FILENO);
                close(input_file_descriptor); // close the pipe (pipe end when input, output end)
            }
            if(i <= program_num - 2){
                dup2(pipe_file_descriptors[1], STDOUT_FILENO);
                close(pipe_file_descriptors[1]);
                close(pipe_file_descriptors[0]); // child process will copy a pipe and its pointer => close child process' pipe output pointer
            }

            redirect_handler(program);
            vector<char*> argv = args_to_argv(program);
            int execvp_status = execvp(program.program_name.c_str(), argv.data()); // replace current process with excuted program
            if(execvp_status < 0){
                cerr << "[Error] execvp failed: " << "\n";
            }
            exit(127);
        } else { // parent process, pid is child process' id

            if(input_file_descriptor != STDIN_FILENO){
                close(input_file_descriptor); // pervious pipe's output space pointer
            }
            if (i <= program_num - 2) {
                close(pipe_file_descriptors[1]);
                input_file_descriptor = pipe_file_descriptors[0];
            }

            if(!program.background){ // wait for child process
                pids.push_back(pid);
            } else {
                // no need to do any waiting
            }
        }
    }
    for (pid_t pid : pids) { // wait for all child, for better parallelism
        waitpid(pid, nullptr, 0);
    }

}

signed main(){

    signal(SIGINT, SIG_IGN); // ignore Ctrl-C in shell

    struct sigaction sa{};
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, nullptr);

    while(1){
        print_command_prefix();
        string command;
        istream& getline_status = getline(cin, command);
        if(!getline_status){
            if(cin.eof()) break; // crtl + D, user exists
            if(cin.fail()){ 
                clearerr(stdin);
                continue; 
            } // unknow error, redo
            break;
        }
        if(command.empty()) continue;
        vector<string> command_tokens = tokenize(command);
        if(command_tokens.empty()) continue; // no command, just some spaces
        
        Parser parser = Parser{};
        bool parse_status = parser.parse(command_tokens);
        if(!parse_status) continue; // parse error, error info in parse function
        if(parser.programs.empty()) continue;

        if(!parser.is_pipeline){
            run_program(parser.programs[0]);
        }
        else{
            run_pipeline_programs(parser.programs);
        }

    }
    return 0;

}