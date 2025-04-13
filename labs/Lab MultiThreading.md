# Lab MultiThreading

## Uthread: switching between threads

任务：补全所给代码，模拟用户线程的调度。

模仿内核中的线程调度即可。

### 上下文信息

用和内核中的`struct context`一样的结构体保存上下文：

```c
struct context {
  uint64 ra;
  uint64 sp;
  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```

在线程中添加context：

```c
struct thread {
  char            stack[STACK_SIZE]; /* the thread's stack */
  int             state;             /* FREE, RUNNING, RUNNABLE */
  struct context  context;
};
```

### 切换上下文

```c
void thread_schedule(void) {
	// ...
	if (current_thread != next_thread) {         /* switch threads?  */
  	next_thread->state = RUNNING;
  	t = current_thread;
  	current_thread = next_thread;
    
  	thread_switch((uint64)(&t->context), (uint64)(&current_thread->context));
	} else {
	  next_thread = 0;
	}
}
```

### 线程创建

创建线程时指明线程开始执行代码的位置(ra)和线程的栈的位置(sp)，注意内存中栈从高向低生长，因此一开始sp应该指向内存的高处：

```c
void thread_create(void (*func)()) {
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  t->context.ra = (uint64)func;
  t->context.sp = (uint64)t->stack + STACK_SIZE - 1;
}
```

![image-20231228192527075](https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150121692.png)

## Using Threads

如果不加锁，2个线程同时在`put`时，`table[i]`只会指向其中一个，另一个会丢失，造成内存泄漏。

没懂的是老师说在`put`和`get`中都要加，但是感觉`get`不用加吧，毕竟只是读数据，而且我没在`get`中加锁也通过了测试。

比较简单，只需要为每个bucket设一个lock，每当`put`访问特定bucket时上锁即可：

```c
pthread_mutex_t locks[NBUCKET];

static void put(int key, int value) {
  int i = key % NBUCKET;

  // is the key already present?
  struct entry *e = 0;
  pthread_mutex_lock(&locks[i]);
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&locks[i]);

}

int main(int argc, char *argv[]) {
  // ...
  for (int i = 0; i < NBUCKET; i++) {
    pthread_mutex_init(&locks[i], NULL);
  }
  // ...
}
```

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150125068.png" alt="image-20231230113937785" style="zoom:50%;" />

## Barrier

barrier指的是程序的某处，线程要在该处等待，直到所有进程都到该处之后才能继续执行。

### pthread_cond_wait

```c
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
```

让进程在条件`cond`上阻塞，调用时进程必须已经获得了`mutex`，这个函数执行时会释放`mutex`，并在返回前重新上锁。

`pthread_cond_broadcast(&cond)`类似。

### 实现

`bstate.nthread`随着执行`barrier`自增，直到和`nthread`相等。

最后进来的线程负责唤醒所有阻塞的线程并增加`round`，这样保证了`round`每轮只增加1。

```c
static void barrier() {
  pthread_mutex_lock(&bstate.barrier_mutex);
  bstate.nthread++;
  if (bstate.nthread == nthread) {
    pthread_cond_broadcast(&bstate.barrier_cond);
    bstate.round++;
    bstate.nthread = 0;
  } else {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}
```



<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150127253.png" alt="image-20231230160852980" style="zoom:50%;" />
