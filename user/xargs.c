#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define ARGV_SIZE 16

int read_line(char* argv[]) {
    char c;
    char * buf = (char *)malloc(ARGV_SIZE); 
    int i = 0, n = 0;
    while (read(0, &c, 1) > 0) {
        if (c == '\n' || c == ' ') {
            argv[n] = buf; n++;
            buf = (char*) malloc(ARGV_SIZE);
            i = 0;
            continue;
        }
        buf[i++] = c;
    }
    argv[n] = 0x0;
    return n;
}

int Fork() {
    int pid;
    if ((pid = fork()) < 0) {
        exit(1);
    }
    return pid;
}

void Exec(char* cmd, char* argv[]) {
    if (exec(cmd, argv) < 0) {
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    char* cmd_name = argv[1];
    char* cmd_argv[MAXARG];

    cmd_argv[0] = cmd_name;
    int i = 2;
    while (argv[i]) {
        cmd_argv[i - 1] = argv[i];
        i++;
    }

    // input from other cmd
    char* input[MAXARG];
    read_line(input);
    
    int j = 0;
    while (input[j]) {
        cmd_argv[i - 1] = input[j];
        i++; j++;
    }

    if (Fork() > 0) {
        Exec(cmd_name, cmd_argv);
    }

    return 0;
}