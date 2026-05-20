#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>

// offsets in Superblock
#define SB_INODES_COUNT 0
#define SB_BLOCKS_COUNT 4
#define SB_FIRST_DATA_BLOCK 20
#define SB_LOG_BLOCK_SIZE 24
#define SB_BLOCKS_PER_GROUP 32
#define SB_INODES_PER_GROUP 40
#define SB_MAGIC 56
#define SB_INODE_SIZE 88

// offsets in Block Group Descriptor Table
#define GD_BLOCK_BITMAP 0
#define GD_INODE_BITMAP 4
#define GD_INODE_TABLE 8

// offsets in Inode
#define I_MODE 0
#define I_SIZE 4
#define I_BLOCK 40

#define IFDIR 0x4000
#define IFREG 0x8000

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

static void read_full_block(int fd, uint32_t block_addr,
                            uint32_t block_size, unsigned char *buf)
{
    if (lseek(fd, (off_t)block_addr * block_size, SEEK_SET) < 0)
    {
        fprintf(stderr, "Error in lseek\n");
        exit(1);
    }
    size_t total = 0;
    while (total < block_size)
    {
        ssize_t n = read(fd, buf + total, block_size - total);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        if (n == 0)
        {
            fprintf(stderr, "Error in read\n");
            exit(1);
        }
        total += n;
    }
}

static uint32_t get_block(int fd, uint32_t block_size,
                                   const uint32_t blocks[15],
                                   uint32_t logical_block)
{
    uint32_t ptrs_per_block = block_size / 4;

    if (logical_block < 12)
    {
        // direct block
        return blocks[logical_block];
    }
    logical_block -= 12;

    if (logical_block < ptrs_per_block)
    {
        // in singly indirect blocks
        unsigned char *buf = malloc(block_size);
        if (!buf)
        {
            fprintf(stderr, "Eror in malloc\n");
            exit(1);
        }
        read_full_block(fd, blocks[12], block_size, buf);
        uint32_t phys = read_le32(buf, logical_block * 4);
        free(buf);
        return phys;
    }
    logical_block -= ptrs_per_block;

    if (logical_block < ptrs_per_block * ptrs_per_block)
    {
        // in doubly indirect blocks
        unsigned char *buf1 = malloc(block_size);
        if (!buf1)
        {
            fprintf(stderr, "Error in malloc\n");
            exit(1);
        }
        read_full_block(fd, blocks[13], block_size, buf1);
        uint32_t idx1 = logical_block / ptrs_per_block;
        uint32_t idx2 = logical_block % ptrs_per_block;
        uint32_t block1 = read_le32(buf1, idx1 * 4);
        free(buf1);
        unsigned char *buf2 = malloc(block_size);
        if (!buf2)
        {
            fprintf(stderr, "Error in malloc\n");
            exit(1);
        }
        read_full_block(fd, block1, block_size, buf2);
        uint32_t phys = read_le32(buf2, idx2 * 4);
        free(buf2);
        return phys;
    }
    logical_block -= ptrs_per_block * ptrs_per_block;

    // in triply indirect blocks
    unsigned char *buf1 = malloc(block_size);
    if (!buf1)
    {
        fprintf(stderr, "Error in malloc\n");
        exit(1);
    }
    read_full_block(fd, blocks[14], block_size, buf1);
    uint32_t idx1 = logical_block / (ptrs_per_block * ptrs_per_block);
    uint32_t rem = logical_block % (ptrs_per_block * ptrs_per_block);
    uint32_t idx2 = rem / ptrs_per_block;
    uint32_t idx3 = rem % ptrs_per_block;
    uint32_t block1 = read_le32(buf1, idx1 * 4);
    free(buf1);

    unsigned char *buf2 = malloc(block_size);
    if (!buf2)
    {
        fprintf(stderr, "Error in malloc\n");
        exit(1);
    }
    read_full_block(fd, block1, block_size, buf2);
    uint32_t block2 = read_le32(buf2, idx2 * 4);
    free(buf2);

    unsigned char *buf3 = malloc(block_size);
    if (!buf3)
    {
        fprintf(stderr, "Error in malloc\n");
        exit(1);
    }
    read_full_block(fd, block2, block_size, buf3);
    uint32_t phys = read_le32(buf3, idx3 * 4);
    free(buf3);
    return phys;
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

    if (lseek(fd, 1024, SEEK_SET) < 0)
    {
        close(fd);
        fprintf(stderr, "Error in lseek\n");
        exit(1);
    }

    unsigned char sb_buf[1024];
    size_t sb_read = 0;
    while (sb_read < 1024)
    {
        ssize_t n = read(fd, sb_buf + sb_read, 1024 - sb_read);
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
        sb_read += n;
    }

    uint32_t block_size = 1024 << read_le32(sb_buf, SB_LOG_BLOCK_SIZE);
    uint32_t inodes_per_group = read_le32(sb_buf, SB_INODES_PER_GROUP);
    uint32_t inode_size = read_le16(sb_buf, SB_INODE_SIZE);
    uint32_t first_data_block = read_le32(sb_buf, SB_FIRST_DATA_BLOCK);
    uint32_t total_inodes = read_le32(sb_buf, SB_INODES_COUNT);

    if (inode_num > total_inodes)
    {
        close(fd);
        fprintf(stderr, "Invalid inode number\n");        
        exit(1);
    }

    uint32_t group = (inode_num - 1) / inodes_per_group;
    uint32_t index = (inode_num - 1) % inodes_per_group;
    uint32_t block_group_descriptor_table_block = first_data_block + 1;

    unsigned char gd_buf[32];
    off_t gd_offset = (off_t)block_group_descriptor_table_block * block_size + group * 32;
    if (lseek(fd, gd_offset, SEEK_SET) < 0)
    {
        close(fd);
        fprintf(stderr, "Error in lseek\n");        
        exit(1);
    }
    size_t gd_read = 0;
    while (gd_read < 32)
    {
        ssize_t n = read(fd, gd_buf + gd_read, 32 - gd_read);
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
        gd_read += n;
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
    if (lseek(fd, inode_off, SEEK_SET) < 0)
    {
        free(inode_buf);
        close(fd);
        fprintf(stderr, "Error in lseek\n");
        exit(1);
    }
    size_t in_read = 0;
    while (in_read < inode_size)
    {
        ssize_t n = read(fd, inode_buf + in_read, inode_size - in_read);
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
        in_read += n;
    }

    uint16_t mode = read_le16(inode_buf, I_MODE);
    uint32_t size = read_le32(inode_buf, I_SIZE);
    uint32_t blocks[15];
    for (int i = 0; i < 15; i++)
    {
        blocks[i] = read_le32(inode_buf, I_BLOCK + i * 4);
    }
    free(inode_buf);

    int inode_type = mode & 0xF000;
    if (inode_type != IFREG && inode_type != IFDIR)
    {
        close(fd);
        fprintf(stderr, "Inode is neither a regular file nor a directory\n");
        exit(1);
    }

    uint32_t num_blocks = (size + block_size - 1) / block_size;
    unsigned char *data_buf = malloc(block_size + 1);
    if (!data_buf)
    {
        close(fd);
        fprintf(stderr, "Error in malloc\n");
        exit(1);
    }

    for (uint32_t i = 0; i < num_blocks; i++)
    {
        uint32_t physical_block_number = get_block(fd, block_size, blocks, i);

        uint32_t bytes_to_write;
        if (i == num_blocks - 1)
        {
            bytes_to_write = size % block_size;
            if (bytes_to_write == 0)
            {
                bytes_to_write = block_size;
            }
        }
        else
        {
            bytes_to_write = block_size;
        }

        if (physical_block_number == 0)
        {
            // sparse block, print zeros
            memset(data_buf, 0, bytes_to_write);
        }
        else
        {
            read_full_block(fd, physical_block_number, block_size, data_buf);
        }

        size_t bytes_written = 0;
        while (bytes_written < bytes_to_write)
        {
            ssize_t n = write(STDOUT_FILENO, data_buf + bytes_written,
                              bytes_to_write - bytes_written);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                free(data_buf);
                close(fd);
                fprintf(stderr, "Error in write\n");
                exit(1);
            }
            bytes_written += n;
        }
    }

    free(data_buf);
    close(fd);
    exit(0);
}