
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  int p2c[2];
  int c2p[2];
  
  pipe(p2c);
  pipe(c2p);

  int pid = fork();
  // parent
  if (pid != 0) {
    write(p2c[1], "ping", 4);

    char buf[5];
    read(c2p[0], buf, 4);
    printf("%d: received pong\n", getpid());
    exit(0);
  }

  // child
  char buf[5];
  read(p2c[0], buf, 4);
  printf("%d: received ping\n", getpid());

  write(c2p[1], "pong", 4);
  exit(0);
}