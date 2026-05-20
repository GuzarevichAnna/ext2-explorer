#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>
#include <errno.h>

// offsets in Superblock
#define SB_INODES_COUNT 0
#define SB_BLOCKS_COUNT 4
#define SB_R_BLOCKS_COUNT 8
#define SB_FREE_BLOCKS_COUNT 12
#define SB_FREE_INODES_COUNT 16
#define SB_FIRST_DATA_BLOCK 20
#define SB_LOG_BLOCK_SIZE 24
#define SB_LOG_FRAG_SIZE 28
#define SB_BLOCKS_PER_GROUP 32
#define SB_FRAGS_PER_GROUP 36
#define SB_INODES_PER_GROUP 40
#define SB_MAGIC 56
#define SB_INODE_SIZE 88

// offsets in Block Group Descriptor Table
#define GD_BLOCK_BITMAP 0
#define GD_INODE_BITMAP 4
#define GD_INODE_TABLE 8

// offsets in Inode
#define I_MODE 0
#define I_UID 2
#define I_SIZE 4
#define I_ATIME 8
#define I_CTIME 12
#define I_MTIME 16
#define I_GID 24
#define I_LINKS_COUNT 26
#define I_BLOCKS 28
#define I_FLAGS 32
#define I_OSD1 36
#define I_BLOCK 40

#define IFIFO 0x1000
#define IFCHR 0x2000
#define IFDIR 0x4000
#define IFBLK 0x6000
#define IFREG 0x8000
#define IFLNK 0xA000
#define IFSOCK 0xC000

#define IRUSR 0x0100
#define IWUSR 0x0080
#define IXUSR 0x0040
#define IRGRP 0x0020
#define IWGRP 0x0010
#define IXGRP 0x0008
#define IROTH 0x0004
#define IWOTH 0x0002
#define IXOTH 0x0001
#define ISUID 0x0800
#define ISGID 0x0400
#define ISVTX 0x0200

static uint16_t read_le16(const unsigned char *buf, int off)
{
    uint16_t v;
    memcpy(&v, buf + off, sizeof(v));
    return le16toh(v);
}

static uint32_t read_le32(const unsigned char *buf, int off)
{
    uint32_t v;
    memcpy(&v, buf + off, sizeof(v));
    return le32toh(v);
}

static void print_time(const char *label, uint32_t t)
{
    char buf[64];
    time_t tt = t;
    struct tm *tm = localtime(&tt);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    printf("%s          %s\n", label, buf);
}

static void print_mode(uint16_t mode)
{
    int inode_type = mode & 0xF000;
    int permissions = mode & 0xFFF;
    char *inode_type_str;
    switch (inode_type)
    {
    case IFIFO:
    {
        inode_type_str = "FIFO";
        break;
    }
    case IFCHR:
    {
        inode_type_str = "Character device";
        break;
    }
    case IFDIR:
    {
        inode_type_str = "Directory";
        break;
    }
    case IFBLK:
    {
        inode_type_str = "Block device";
        break;
    }
    case IFREG:
    {
        inode_type_str = "Regular file";
        break;
    }
    case IFLNK:
    {
        inode_type_str = "Symbolic link";
        break;
    }
    case IFSOCK:
    {
        inode_type_str = "Unix socket";
        break;
    }
    default:
    {
        inode_type_str = "Unknown";
        break;
    }
    }
    printf("Type                 %s\n", inode_type_str);
    printf("Permissions          %03o\n", permissions);
}

static void traverse_indirect(int fd, unsigned block_size, uint32_t block,
                              int cur_level, int max_level, int indent)
{
    if (block == 0)
    {
        return;
    }

    unsigned char *buf = malloc(block_size);
    if (!buf)
    {
        fprintf(stderr, "Error in malloc\n");
        exit(1);
    }

    if (lseek(fd, block * (unsigned long long)block_size, SEEK_SET) < 0)
    {
        fprintf(stderr, "Error in lseek\n");
        free(buf);
        exit(1);
    }

    size_t bytes_read = 0;
    while (bytes_read < block_size)
    {
        int n = read(fd, buf + bytes_read, block_size - bytes_read);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue; // retry read
            }
            fprintf(stderr, "Error in read\n");
            free(buf);
            exit(1);
        }
        if (n == 0)
        {
            fprintf(stderr, "Error in read\n");
            free(buf);
            exit(1);
        }
        bytes_read += n;
    }

    if (cur_level == max_level)
    {
        // cur_level points to data
        int entries = block_size / 4;
        for (int i = 0; i < entries; i++)
        {
            uint32_t data_block = read_le32(buf, i * 4);
            if (data_block == 0)
            {
                continue;
            }
            printf("%*sdata             %u\n", indent, "", data_block);
        }
    }
    else
    {
        // cur_level points to another layer of indirect bloks
        int entries = block_size / 4;
        for (int i = 0; i < entries; i++)
        {
            uint32_t next = read_le32(buf, i * 4);
            if (next == 0)
            {
                continue;
            }
            printf("%*sindirect (level %d): %u\n", indent, "", cur_level + 1, next);
            traverse_indirect(fd, block_size, next, cur_level + 1, max_level, indent + 4);
        }
    }
    free(buf);
}

static void print_blocks(int fd, unsigned block_size,
                         const uint32_t i_block[15])
{
    printf("\nBlocks:\n");

    for (int i = 0; i < 12; i++)
    {
        if (i_block[i] != 0)
        {
            printf("  direct[%2d]        %u\n", i, i_block[i]);
        }
    }

    if (i_block[12] != 0)
    {
        printf("  singly indirect    %u\n", i_block[12]);
        traverse_indirect(fd, block_size, i_block[12], 1, 1, 6);
    }

    if (i_block[13] != 0)
    {
        printf("  doubly indirect    %u\n", i_block[13]);
        traverse_indirect(fd, block_size, i_block[13], 1, 2, 6);
    }

    if (i_block[14] != 0)
    {
        printf("  triply indirect    %u\n", i_block[14]);
        traverse_indirect(fd, block_size, i_block[14], 1, 3, 6);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <image_file> <inode_number>\n", argv[0]);
        exit(1);
    }

    const char *fname = argv[1];
    unsigned inode_num = (unsigned)atoi(argv[2]);
    if (inode_num == 0)
    {
        fprintf(stderr, "Error in atoi\n");
        exit(1);
    }

    int fd = open(fname, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Error in open\n");
        exit(1);
    }

    unsigned char sb_buf[1024];
    if (lseek(fd, 1024, SEEK_SET) < 0)
    {
        close(fd);
        fprintf(stderr, "Error in lseek\n");
        exit(1);
    }
    size_t bytes_read = 0;
    while (bytes_read < 1024)
    {
        int n = read(fd, sb_buf + bytes_read, 1024 - bytes_read);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            close(fd);
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        if (n == 0)
        {
            close(fd);
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        bytes_read += n;
    }

    uint32_t block_size = 1024 << read_le32(sb_buf, SB_LOG_BLOCK_SIZE);
    uint32_t inodes_per_group = read_le32(sb_buf, SB_INODES_PER_GROUP);
    uint32_t inode_size = read_le16(sb_buf, SB_INODE_SIZE);
    uint32_t first_data_block = read_le32(sb_buf, SB_FIRST_DATA_BLOCK);
    uint32_t total_inodes = read_le32(sb_buf, SB_INODES_COUNT);

    if (inode_num > total_inodes)
    {
        fprintf(stderr, "Invalid inode number\n");
        close(fd);
        exit(1);
    }

    uint32_t group = (inode_num - 1) / inodes_per_group;
    uint32_t index = (inode_num - 1) % inodes_per_group;

    uint32_t block_group_descriptor_table_block = first_data_block + 1;
    unsigned char gd_buf[32];
    off_t gd_offset = (off_t)block_group_descriptor_table_block * block_size + group * 32;
    if (lseek(fd, gd_offset, SEEK_SET) < 0) {
        close(fd);
        fprintf(stderr, "Error in lseek\n");
        exit(1);
    }
    bytes_read = 0;
    while (bytes_read < 32)
    {
        int n = read(fd, gd_buf + bytes_read, 32 - bytes_read);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            close(fd);
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        if (n == 0)
        {
            close(fd);
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        bytes_read += n;
    }

    uint32_t inode_table = read_le32(gd_buf, GD_INODE_TABLE);

    off_t inode_off = (off_t)inode_table * block_size + index * inode_size;
    unsigned char *inode_buf = malloc(inode_size);
    if (!inode_buf)
    {
        close(fd);
        fprintf(stderr, "Error in malloc\n");
        exit(1);
    }
    if (lseek(fd, inode_off, SEEK_SET) < 0) {
        close(fd);
        fprintf(stderr, "Error in lseek\n");
    }
    bytes_read = 0;
    while (bytes_read < inode_size)
    {
        int n = read(fd, inode_buf + bytes_read, inode_size - bytes_read);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            free(inode_buf);
            close(fd);
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        if (n == 0)
        {
            free(inode_buf);
            close(fd);
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        bytes_read += n;
    }

    uint16_t mode = read_le16(inode_buf, I_MODE);
    uint16_t uid = read_le16(inode_buf, I_UID);
    uint32_t size = read_le32(inode_buf, I_SIZE);
    uint32_t atime = read_le32(inode_buf, I_ATIME);
    uint32_t ctime = read_le32(inode_buf, I_CTIME);
    uint32_t mtime = read_le32(inode_buf, I_MTIME);
    uint16_t gid = read_le16(inode_buf, I_GID);
    uint16_t links = read_le16(inode_buf, I_LINKS_COUNT);
    uint32_t blocks = read_le32(inode_buf, I_BLOCKS);
    uint32_t i_block[15];
    for (int i = 0; i < 15; i++)
    {
        i_block[i] = read_le32(inode_buf, I_BLOCK + i * 4);
    }

    free(inode_buf);

    printf("Inode %u:\n", inode_num);
    print_mode(mode);
    printf("UID                  %u\n", uid);
    printf("GID                  %u\n", gid);
    printf("Size                 %u bytes\n", size);
    printf("Links                %u\n", links);
    printf("Blocks (512-byte)    %u\n", blocks);
    print_time("Access time", atime);
    print_time("Modify time", mtime);
    print_time("Change time", ctime);
    print_blocks(fd, block_size, i_block);

    close(fd);
    exit(0);
}