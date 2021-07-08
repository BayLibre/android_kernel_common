// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#include "test_fuse.h"

int bpf_test_trace(const char *substr)
{
	int result = TEST_FAILURE;
	int tp = -1;
	char trace_buffer[256] = {};
	ssize_t bytes_read;
	TEST(tp = open("/sys/kernel/debug/tracing/trace_pipe",
		       O_RDONLY | O_CLOEXEC), tp != -1);
	TEST(bytes_read = read(tp, trace_buffer, sizeof(trace_buffer)),
	     bytes_read > 0);
	if (test_options.verbose)
		ksft_print_msg("%s\n", trace_buffer);
	TESTNE(strstr(trace_buffer, substr), NULL);
	result = TEST_SUCCESS;
out:
	close(tp);
	return result;
}

int main(int argc, char *argv[])
{
	int result = TEST_FAILURE;
	char *mount_dir = NULL;
	int bpf_fd = -1;
	int dir_fd = -1;
	int fuse_dev = -1;

	if (geteuid() != 0)
		ksft_print_msg("Not a root, might fail to mount.\n");

	TEST(mount_dir = setup_mount_dir(), mount_dir);
	TESTEQUAL(install_bpf("test_daemon.raw", &bpf_fd), 0);
	TEST(dir_fd = open(".", O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     dir_fd != -1);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, dir_fd, &fuse_dev), 0);

	for(;;) {
		uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
		//uint8_t bytes_out[FUSE_MIN_READ_BUFFER];
		struct fuse_in_header *in_header =
			(struct fuse_in_header *)bytes_in;
		ssize_t res = read(fuse_dev, &bytes_in,	sizeof(bytes_in));

		TESTNE(res, -1);
		printf("opcode is %d\n", in_header->opcode);
	}

	result = TEST_SUCCESS;

out:
	umount2(mount_dir, MNT_FORCE);
	rmdir(mount_dir);
	return result;
}
