#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

#ifdef LAB_MMAP
#define PROT_NONE       0x000
#define PROT_READ       0x001
#define PROT_WRITE      0x002
#define PROT_EXEC       0x200

#define MAP_SHARED      0x001
#define MAP_PRIVATE     0x002
#endif
