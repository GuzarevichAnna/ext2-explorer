CC      = gcc
CFLAGS  = -Wall -Wextra -g

TARGETS = get_metadata_and_block_numbers get_data parse_directory_contents

.PHONY: all test clean

all: $(TARGETS)

test: all
	valgrind --leak-check=full --error-exitcode=1 --log-file=valgrind_report.txt ./test_img.sh
	valgrind --leak-check=full --error-exitcode=1 --log-file=valgrind_report.txt ./test_loop.sh

clean:
	rm -f $(TARGETS)
	rm -rf ./ext2_dir
	rm ./ext2.img ./checksums* ./inodes* ./valgrind_report.txt