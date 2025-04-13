# Lab File System

## Large files

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150048552.png" alt="image-20240402185433289" style="zoom:50%;" />

通过使用doubly indirect block来增加一个文件的最大大小，使最大大小达到256 * 256 + 256 + 11 blocks。

### fs.h和file.h

前11个地址为直接地址，第12个地址为indirect地址，第13个地址为doubly indirect地址：

```c
// fs.h
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define N_DOUBLY_INDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + N_DOUBLY_INDIRECT)

struct dinode {
  short type;           
  short major;          
  short minor;          
  short nlink;          
  uint size;            
  uint addrs[NDIRECT + 2];   // 11 + 1 + 1
};

// file.h
struct inode {
  ...
  uint addrs[NDIRECT + 2]; // 11 + 1 + 1
};
```

### fs.c

`bmap`中模仿前面的代码即可，需要获得2次地址，每次检查地址是否分配，别忘了`brelse`。

```c
// fs.c
static uint bmap(struct inode *ip, uint bn) { // bn: nth block
  ...
  if(bn < NDIRECT) {
	...
  }
  bn -= NDIRECT;
  if(bn < NINDIRECT) {
	...
  }
  bn -= NINDIRECT;
  if (bn < N_DOUBLY_INDIRECT) {
    // 第一层地址
    if ((addr = ip->addrs[NDIRECT + 1]) == 0) {
      addr = balloc(ip->dev);
      if (addr == 0) return 0;
      ip->addrs[NDIRECT + 1] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    int node_num = bn / NINDIRECT;
    // 第二层地址
    if ((addr = a[node_num]) == 0) {
      uint tmp = balloc(ip->dev);
      if (tmp) {
        a[node_num] = tmp;
        log_write(bp);
      }
    }
    brelse(bp);
    bn -= node_num * NINDIRECT;
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    // 实际的block地址
    if ((addr = a[bn]) == 0) {
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

## Symbolic links

添加软链接功能，软链接文件中保存了源文件的路径名，当这个软连接文件打开时，需要找到源文件，并且需要支持递归的软链接。

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150052366.png" alt="image-20240416151630322" style="zoom:50%;" />



<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150054100.png" alt="image-20240416151601963" style="zoom:50%;" />

