#!/bin/bash

echo
echo
echo
echo TEST WITH EXPLICIT LOOP ATTACH
echo
echo

BLOCK_SIZE=2048

LOOP=$(sudo losetup -f)
sudo losetup $LOOP ext2.img
sudo mount -t ext2 ext2.img ext2_dir
sudo chown -R $USER:$USER ext2_dir

# save inodes and checksums
find ext2_dir -printf '%i %p\n' > inodes_loop.txt
find ext2_dir -type f -exec sha512sum {} + > checksums_loop.txt
sha512sum -c checksums_loop.txt

sudo umount ext2_dir

INODE_USUAL=$(grep '/usual_file.bin$' inodes_loop.txt | awk '{print $1}')
INODE_SPARSE=$(grep '/sparse_file.bin$' inodes_loop.txt | awk '{print $1}')
INODE_INDIRECT=$(grep '/indirect_file.bin$' inodes_loop.txt | awk '{print $1}')
INODE_DIR1=$(grep '/dir_1$' inodes_loop.txt | awk '{print $1}')
INODE_DIR2=$(grep '/dir_2$' inodes_loop.txt | awk '{print $1}')

echo
echo
echo "Testing get_metadata_and_block_numbers"
./get_metadata_and_block_numbers ext2.img $INODE_USUAL
echo
./get_metadata_and_block_numbers ext2.img $INODE_SPARSE
echo
./get_metadata_and_block_numbers ext2.img $INODE_INDIRECT
echo
./get_metadata_and_block_numbers ext2.img $INODE_DIR1
echo
./get_metadata_and_block_numbers ext2.img $INODE_DIR2

echo
echo
echo "Testing get_data"
for name in usual_file.bin sparse_file.bin indirect_file.bin; do
    inode=$(grep "/$name$" inodes_loop.txt | awk '{print $1}')
    ORIG_SUM=$(grep "$name" checksums_loop.txt | awk '{print $1}')
    ACTUAL_SUM=$(./get_data ext2.img $inode | sha512sum | awk '{print $1}')
    if [ "$ORIG_SUM" != "$ACTUAL_SUM" ]; then
        echo "FAIL: $name checksum mismatch" >&2
        exit 1
    fi
    echo "OK: $name"
done

echo
echo
echo "Testing parse_directory_contests"
echo "Root (inode 2):"
./get_data ext2.img 2 | ./parse_directory_contents
echo "dir_1 (inode $INODE_DIR1):"
./get_data ext2.img $INODE_DIR1 | ./parse_directory_contents
echo "dir_2 (inode $INODE_DIR2):"
./get_data ext2.img $INODE_DIR2 | ./parse_directory_contents