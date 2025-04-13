# Lab: Xv6 and Unix utilities

## sleep

只需使用`atoi`将参数转成`int`然后使用系统调用`sleep`就行了：

```c
int main(int argc, char *argv[])
{
    if (argc == 1) { // 参数不够
        exit(1);
    }

    int s = atoi(argv[1]); // 将数据从char*转成int
    sleep(s);
    exit(0);
}
```

## pingpong

用2个管道实现2个方向的数据传输。

先从父进程用`write`发送ping给子进程，子进程用`read`收到之后再发送pong给父进程。

注意pipe数组的0下标用于读取数据，1下标用于发送数据

```cpp
int main(int argc, char* argv[]) {
    int p2c[2]; // parent -> child
    int c2p[2]; // child -> parent

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
```

