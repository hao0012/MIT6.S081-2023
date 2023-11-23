#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char* fmtname(char *path) {
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  return ++p;
}

int search(char* path, char* file) {
    int fd;
    struct dirent de;
    struct stat st;
    char buf[512]; char *p;
    if ((fd = open(path, O_RDONLY)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return 0;
    }

    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return 0;
    }

    switch (st.type)
    {
    case T_DEVICE:
    case T_FILE:
        if (strcmp(fmtname(path), file) == 0)
            printf("%s\n", path);
        break;
    case T_DIR:
        strcpy(buf, path);
        p = buf + strlen(buf); *p++ = '/';
        while(read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0) continue; // unused inode
            if (strcmp(de.name, ".") == 0 || (strcmp(de.name, "..")) == 0) continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            search(buf, file);
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        return 0;
    }

    char* root = argv[1];
    char* file = argv[2];
    return search(root, file);
}