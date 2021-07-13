// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#include "test_fuse.h"

static int display_trace()
{
	int result = TEST_FAILURE;
	int pid = -1;
	int tp = -1;
	char trace_buffer;
	ssize_t bytes_read;

	TEST(pid = fork(), pid != -1);
	if (pid != 0)
		return TEST_SUCCESS;

	TEST(tp = open("/sys/kernel/debug/tracing/trace_pipe",
		       O_RDONLY | O_CLOEXEC), tp != -1);
	for(;;) {
		TEST(bytes_read = read(tp, &trace_buffer, sizeof(trace_buffer)),
		     bytes_read == 1);
		printf("%c", trace_buffer);
	}
out:
	if (pid == 0) {
		close(tp);
		exit(TEST_FAILURE);
	}
	return result;
}

int main(int argc, char *argv[])
{
	int result = TEST_FAILURE;
	char *mount_dir = NULL;
	char *src_dir = NULL;
	int bpf_fd = -1;
	int src_fd = -1;
	int fuse_dev = -1;

	if (geteuid() != 0)
		ksft_print_msg("Not a root, might fail to mount.\n");

	display_trace();

	TEST(src_dir = setup_mount_dir("fd-src"), src_dir);
	TEST(mount_dir = setup_mount_dir("fd-dst"), mount_dir);
	TESTEQUAL(install_bpf("test_daemon.raw", &bpf_fd), 0);
	TEST(src_fd = open("fd-src", O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	for(;;) {
		uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
		struct fuse_in_header *in_header =
			(struct fuse_in_header *)bytes_in;
		ssize_t res = read(fuse_dev, &bytes_in,	sizeof(bytes_in));

		if (res == -1)
			break;
		printf("opcode is %d\n", in_header->opcode);
	}

	result = TEST_SUCCESS;

out:
	umount2(mount_dir, MNT_FORCE);
	delete_dir_tree(mount_dir);
	rmdir(mount_dir);
	free(mount_dir);
	delete_dir_tree(src_dir);
	rmdir(src_dir);
	free(src_dir);
	return result;
}
