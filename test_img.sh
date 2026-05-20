#!/bin/bash

echo TEST WITHOUT EXPLICIT LOOP ATTACH
echo
echo

BLOCK_SIZE=2048

truncate -s 8G ext2.img
mkfs.ext2 ext2.img -g 4096 -N 2048 -b $BLOCK_SIZE
file ext2.img # check that FS type is ext2
mkdir ext2_dir
sudo mount -t ext2 ext2.img ext2_dir
sudo chown -R $USER:$USER ext2_dir

# directories
mkdir ext2_dir/dir_1/
mkdir ext2_dir/dir_1/dir_2/
# usual file
dd if=/dev/urandom of=ext2_dir/dir_1/dir_2/usual_file.bin bs=512 count=1
# sparse file
truncate -s 5G ext2_dir/sparse_file.bin
dd if=/dev/urandom of=ext2_dir/sparse_file.bin bs=512 count=1
# indirect file
dd if=/dev/urandom of=ext2_dir/indirect_file.bin bs=$BLOCK_SIZE count=13

# save inodes and checksums
find ext2_dir -printf '%i %p\n' > inodes_img.txt
find ext2_dir -type f -exec sha512sum {} + > checksums_img.txt
sha512sum -c checksums_img.txt

sudo umount ext2_dir

INODE_USUAL=$(grep '/usual_file.bin$' inodes_img.txt | awk '{print $1}')
INODE_SPARSE=$(grep '/sparse_file.bin$' inodes_img.txt | awk '{print $1}')
INODE_INDIRECT=$(grep '/indirect_file.bin$' inodes_img.txt | awk '{print $1}')
INODE_DIR1=$(grep '/dir_1$' inodes_img.txt | awk '{print $1}')
INODE_DIR2=$(grep '/dir_2$' inodes_img.txt | awk '{print $1}')

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
    inode=$(grep "/$name$" inodes_img.txt | awk '{print $1}')
    ORIG_SUM=$(grep "$name" checksums_img.txt | awk '{print $1}')
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