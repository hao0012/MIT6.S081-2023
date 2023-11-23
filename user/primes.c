#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define WRITE 1
#define READ 0

int Fork() {
    int pid;
    if ((pid = fork()) < 0) {
        exit(1);
    }
    return pid;
}

void operate(int read_fd) {
    int n;
    if (read(read_fd, &n, 4) != 0) {
        printf("prime %d\n", n);

        int p[2]; pipe(p);

        /* child */
        if (Fork() == 0) {
            close(p[WRITE]);
            operate(p[READ]);
        }
        /* parent */
        close(p[READ]);
        int d;
        while (read(read_fd, &d, 4) != 0) {
            if (d % n != 0) {
                write(p[WRITE], &d, sizeof(int));
            }
        }
        close(p[WRITE]);
        int s;
        while (wait(&s) != -1);
    }
    close(read_fd);
    exit(0);
}

int main(int argc, char *argv[]) {
    int p[2]; pipe(p);
    if (Fork() == 0) { /* child */
        close(p[WRITE]);
        operate(p[READ]);
    }
    close(p[READ]);
    for (int i = 2; i <= 35; i++)
        write(p[WRITE], &i, sizeof(int));
    close(p[WRITE]);
    int s;
    while (wait(&s) != -1);
    exit(0);
}