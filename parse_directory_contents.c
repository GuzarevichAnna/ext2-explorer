#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>

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

int main(void)
{
    unsigned char buf[4096];
    size_t bytes_read = 0;

    while (bytes_read < sizeof(buf))
    {
        ssize_t n = read(STDIN_FILENO, buf + bytes_read, sizeof(buf) - bytes_read);
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
            break;
        }
        bytes_read += n;
    }

    unsigned char *p = buf;
    unsigned char *end = buf + bytes_read;

    while (p < end)
    {
        uint32_t inode = read_le32(p, 0);
        uint16_t entry_len = read_le16(p, 4);
        uint8_t name_len = *(p + 6);


        if (inode != 0)
        {
            printf("inode: %u name: %.*s\n", inode, name_len, p + 8);
        }

        p += entry_len;
    }

    exit(0);
}