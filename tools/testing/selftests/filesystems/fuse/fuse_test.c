// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */
#define _GNU_SOURCE

#include <alloca.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <linux/bpf.h>
#include <linux/random.h>
#include <linux/stat.h>
#include <linux/unistd.h>

#include <kselftest.h>

#include <include/uapi/linux/fuse.h>

#define TEST_FAILURE 1
#define TEST_SUCCESS 0

#define ptr_to_u64(p) ((__u64)p)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define le16_to_cpu(x)          (x)
#define le32_to_cpu(x)          (x)
#define le64_to_cpu(x)          (x)
#else
#error Big endian not supported!
#endif

struct {
	int file;
	int test;
	bool verbose;
} test_options;

#define TESTCOND(condition)						\
	do {								\
		if (!(condition)) {					\
			ksft_print_msg("%s failed %d\n",		\
				       __func__, __LINE__);		\
			goto out;					\
		} else if (test_options.verbose)			\
			ksft_print_msg("%s succeeded %d\n",		\
				       __func__, __LINE__);		\
	} while (false)

#define TESTCONDERR(condition)						\
	do {								\
		if (!(condition)) {					\
			ksft_print_msg("%s failed %d\n",		\
				       __func__, __LINE__);		\
			ksft_print_msg("Error %d (\"%s\")\n",		\
				       errno, strerror(errno));		\
			goto out;					\
		} else if (test_options.verbose)			\
			ksft_print_msg("%s succeeded %d\n",		\
				       __func__, __LINE__);		\
	} while (false)

#define TEST(statement, condition)					\
	do {								\
		statement;						\
		TESTCOND(condition);					\
	} while (false)

#define TESTERR(statement, condition)					\
	do {								\
		statement;						\
		TESTCONDERR(condition);					\
	} while (false)


bool test_equal_signed(long long int a, long long int b)
{
	if (a == b)
		return true;

	ksft_print_msg("Failed: %lld != %lld\n", a, b);
	return false;
}

bool test_equal_unsigned(long long unsigned a, long long unsigned b)
{
	if (a == b)
		return true;

	ksft_print_msg("Failed: %lld != %lld\n", a, b);
	return false;
}

#define TESTEQUAL(a, b)							\
	do{								\
		if (!_Generic((a),					\
		int: test_equal_signed((a), (b)),			\
		ssize_t: test_equal_signed((a), (b)),			\
		uint32_t: test_equal_unsigned((a), (b)),		\
		default: ((a) == (b)))) {				\
			ksft_print_msg("%s failed %d\n",		\
				       __func__, __LINE__);		\
			goto out;					\
		} else if (test_options.verbose)			\
			ksft_print_msg("%s succeeded %d\n",		\
				       __func__, __LINE__);		\
	} while (false)


#define TESTNE(statement, res)						\
	TESTCOND((statement) != (res))

/* For testing a syscall that returns 0 on success and sets errno otherwise */
#define TESTSYSCALL(statement) TESTCONDERR((statement) == 0)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

void print_bytes(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	int i;

	for (i = 0; i < size; ++i) {
		if (i % 0x10 == 0)
			printf("%08x:", i);
		printf("%02x ", (unsigned int) bytes[i]);
		if (i % 0x10 == 0x0f)
			printf("\n");
	}

	if (i % 0x10 != 0)
		printf("\n");
}

static char *concat_file_name(const char *dir, const char *file)
{
	char full_name[FILENAME_MAX] = "";

	if (snprintf(full_name, ARRAY_SIZE(full_name), "%s/%s", dir, file) < 0)
		return NULL;
	return strdup(full_name);
}

static char *setup_mount_dir()
{
	struct stat st;
	char *current_dir = getcwd(NULL, 0);
	char *mount_dir = concat_file_name(current_dir, "incfs-mount-dir");

	free(current_dir);
	if (stat(mount_dir, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return mount_dir;

		ksft_print_msg("%s is a file, not a dir.\n", mount_dir);
		return NULL;
	}

	if (mkdir(mount_dir, 0777)) {
		ksft_print_msg("Can't create mount dir.");
		return NULL;
	}

	return mount_dir;
}

#define TESTFUSEIN(_opcode, in_struct)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		ssize_t res = read(fuse_dev, &bytes_in,			\
			sizeof(bytes_in));				\
									\
		TESTEQUAL(in_header->opcode, _opcode);			\
		TESTEQUAL(res, sizeof(*in_header) + sizeof(*in_struct));\
	} while(false)

/* Special case lookup since it is asymmetric */
#define TESTFUSELOOKUP(expected)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		char *name = (char *) (bytes_in + sizeof(*in_header));	\
									\
		TESTEQUAL(read(fuse_dev, &bytes_in, sizeof(bytes_in)),	\
			  sizeof(*in_header) + strlen(expected) + 1);	\
		TESTEQUAL(in_header->opcode, FUSE_LOOKUP);		\
		TESTCOND(!strcmp(name, expected));			\
	} while(false)

#define TESTFUSEOUT(out_struct)						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header) +			\
				sizeof(*out_struct),			\
			.unique = in_header->unique,			\
		};							\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while(false)

#define TESTFUSEOUTEMPTY()						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header),			\
			.unique = in_header->unique,			\
		};							\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while(false)

#define TESTFUSEOUTREAD(data, length)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header) + length,		\
			.unique = in_header->unique,			\
		};							\
		memcpy(bytes_out + sizeof(*out_header), data, length);	\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while(false)

#define DECL_FUSE_IN(name)						\
	struct fuse_##name##_in *name##_in =				\
		(struct fuse_##name##_in *)				\
		(bytes_in + sizeof(struct fuse_in_header));

#define DECL_FUSE_OUT(name)						\
	struct fuse_##name##_out *name##_out =				\
		(struct fuse_##name##_out *)				\
		(bytes_out + sizeof(struct fuse_out_header))

#define DECL_FUSE(name)							\
	DECL_FUSE_IN(name);						\
	DECL_FUSE_OUT(name)

#define FUSE_ACTION	TEST(pid = fork(), pid != -1);			\
			if (pid) {
#define FUSE_DAEMON	} else {
#define FUSE_DONE		exit(TEST_SUCCESS);			\
			}						\
			TESTEQUAL(waitpid(pid, &status, 0), pid);	\
			TESTEQUAL(status, TEST_SUCCESS);

int mount_fuse(const char *mount_dir, const char *options, int *fuse_dev_ptr)
{
	int result = TEST_FAILURE;
	int fuse_dev = -1;
	char mount_options[FILENAME_MAX];
	uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
	uint8_t bytes_out[FUSE_MIN_READ_BUFFER];
	DECL_FUSE(init);

	TEST(fuse_dev = open("/dev/fuse", O_RDWR | O_CLOEXEC), fuse_dev != -1);
	snprintf(mount_options, FILENAME_MAX,
		 "fd=%d,user_id=0,group_id=0,rootmode=0040000%s",
		 fuse_dev, options);
	TESTSYSCALL(mount("ABC", mount_dir, "fuse", 0, mount_options));

	TESTFUSEIN(FUSE_INIT, init_in);
	TESTEQUAL(init_in->major, FUSE_KERNEL_VERSION);
	TESTEQUAL(init_in->minor, FUSE_KERNEL_MINOR_VERSION);
	*init_out = (struct fuse_init_out) {
		.major = FUSE_KERNEL_VERSION,
		.minor = FUSE_KERNEL_MINOR_VERSION,
		.max_readahead = 4096,
		.flags = 0,
		.max_background = 0,
		.congestion_threshold = 0,
		.max_write = 4096,
		.time_gran = 1000,
		.max_pages = 12,
		.map_alignment = 4096,
	};
	TESTFUSEOUT(init_out);

	*fuse_dev_ptr = fuse_dev;
	fuse_dev = -1;
	result = TEST_SUCCESS;
out:
	close(fuse_dev);
	return result;
}

int basic_test(const char *mount_dir)
{
	const char *test_name = "test";
	const char *test_data = "data";

	int result = TEST_FAILURE;
	int fuse_dev = -1;
	uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
	uint8_t bytes_out[FUSE_MIN_READ_BUFFER];
	DECL_FUSE_OUT(entry);
	DECL_FUSE(open);
	DECL_FUSE_IN(read);
	DECL_FUSE_IN(flush);
	DECL_FUSE_IN(release);
	char *filename = NULL;
	int fd = -1;
	int pid = -1;
	int status;

	TESTEQUAL(mount_fuse(mount_dir, "", &fuse_dev), 0);
	FUSE_ACTION
		char data[256];

		filename = concat_file_name(mount_dir, test_name);
		TESTERR(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);
		TESTEQUAL(read(fd, data, strlen(test_data)), strlen(test_data));
		TESTCOND(!strcmp(data, test_data));
		TESTSYSCALL(close(fd));
		fd = -1;
	FUSE_DAEMON
		TESTFUSELOOKUP(test_name);
		*entry_out = (struct fuse_entry_out) {
			.nodeid		= 2,
			.generation	= 1,
			.attr = (struct fuse_attr) {
				.ino = 100,
				.size = 4,
				.blksize = 512,
				.mode = S_IFREG,
			},
		};
		TESTFUSEOUT(entry_out);
		TESTFUSEIN(FUSE_OPEN, open_in);
		*open_out = (struct fuse_open_out) {
			.fh = 1,
			.open_flags = open_in->flags,
		};
		TESTFUSEOUT(open_out);
		TESTFUSEIN(FUSE_READ, read_in);
		TESTFUSEOUTREAD(test_data, strlen(test_data));
		TESTFUSEIN(FUSE_FLUSH, flush_in);
		TESTFUSEOUTEMPTY();
		TESTFUSEIN(FUSE_RELEASE, release_in);
		TESTFUSEOUTEMPTY();
	FUSE_DONE

	result = TEST_SUCCESS;
out:
	if (!pid)
		exit(TEST_FAILURE);
	close(fuse_dev);
	close(fd);
	free(filename);
	umount("dst");
	return result;
}

int install_bpf(const char *name, int *fd)
{
	int result = TEST_FAILURE;
	char path[PATH_MAX];
	char *last_slash;
	struct stat st;
	uint64_t *filter = NULL;
	int filter_fd = -1;
	union bpf_attr bpf_attr;
	char log[4096];

	TESTNE(readlink("/proc/self/exe", path, PATH_MAX), -1);
	TEST(last_slash = strrchr(path, '/'), last_slash);
	strcpy(last_slash + 1, name);
	TESTSYSCALL(stat(path, &st));
	TEST(filter = malloc(st.st_size), filter);
	TEST(filter_fd = open(path, O_RDONLY | O_CLOEXEC), filter_fd != -1);
	TESTEQUAL(read(filter_fd, filter, st.st_size), st.st_size);
	if (filter[st.st_size / sizeof(filter[0]) - 1] == 0)
		st.st_size -= sizeof(filter[0]);
	print_bytes(filter, st.st_size);
	bpf_attr = (union bpf_attr) {
		.prog_type = BPF_PROG_TYPE_TRACEPOINT,
		.insn_cnt = st.st_size / 8,
		.insns = ptr_to_u64(filter),
		.license = ptr_to_u64("GPL"),
		.log_buf = ptr_to_u64(log),
		.log_size = sizeof(log),
		.log_level = 2,
	};
	*fd = syscall(__NR_bpf, BPF_PROG_LOAD, &bpf_attr, sizeof(bpf_attr));
	printf("%s", log);
	TESTNE(*fd, -1);

	result = TEST_SUCCESS;
out:
	close(filter_fd);
	free(filter);
	return result;
}

int bpf_test(const char *mount_dir)
{
	const char *test_name = "test";
	int result = TEST_FAILURE;
	int bpf_fd = -1;
	char options[256];
	int fuse_dev = -1;
	uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
	uint8_t bytes_out[FUSE_MIN_READ_BUFFER];
	DECL_FUSE_OUT(entry);
	DECL_FUSE(open);
	DECL_FUSE_IN(flush);
	DECL_FUSE_IN(release);
	char *filename = NULL;
	int fd = -1;
	int pid = -1;
	int status;
	int tp = -1;
	char trace_buffer[256];
	ssize_t bytes_read;

	TESTEQUAL(install_bpf("test_trace.raw", &bpf_fd), 0);
	snprintf(options, sizeof(options), ",root_bpf=%d", bpf_fd);
	TESTEQUAL(mount_fuse(mount_dir, options, &fuse_dev), 0);

	FUSE_ACTION
		filename = concat_file_name(mount_dir, test_name);
		TESTERR(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);
		TESTSYSCALL(close(fd));
		fd = -1;
	FUSE_DAEMON
		TESTFUSELOOKUP(test_name);
		*entry_out = (struct fuse_entry_out) {
			.nodeid		= 2,
			.generation	= 1,
			.attr = (struct fuse_attr) {
				.ino = 100,
				.size = 4,
				.blksize = 512,
				.mode = S_IFREG,
			},
		};
		TESTFUSEOUT(entry_out);
		TESTFUSEIN(FUSE_OPEN, open_in);
		*open_out = (struct fuse_open_out) {
			.fh = 1,
			.open_flags = open_in->flags,
		};
		TESTFUSEOUT(open_out);
		TESTFUSEIN(FUSE_FLUSH, flush_in);
		TESTFUSEOUTEMPTY();
		TESTFUSEIN(FUSE_RELEASE, release_in);
		TESTFUSEOUTEMPTY();
	FUSE_DONE

	TEST(tp = open("/sys/kernel/debug/tracing/trace_pipe",
		       O_RDONLY | O_CLOEXEC), tp != -1);
	TEST(bytes_read = read(tp, trace_buffer, sizeof(trace_buffer)),
	     bytes_read > 0);
	printf("%s", trace_buffer);
	TESTNE(strstr(trace_buffer, "Hello Paul"), NULL);


	result = TEST_SUCCESS;
out:
	close(tp);
	close(fuse_dev);
	close(fd);
	free(filename);
	umount("dst");
	close(bpf_fd);
	return result;
}

int parse_options(int argc, char *const *argv)
{
	signed char c;

	while ((c = getopt(argc, argv, "f:t:v")) != -1)
		switch (c) {
		case 'f':
			test_options.file = strtol(optarg, NULL, 10);
			break;

		case 't':
			test_options.test = strtol(optarg, NULL, 10);
			break;

		case 'v':
			test_options.verbose = true;
			break;

		default:
			return -EINVAL;
		}

	return 0;
}

struct test_case {
	int (*pfunc)(const char *dir);
	const char *name;
};

void run_one_test(const char *mount_dir, struct test_case *test_case)
{
	ksft_print_msg("Running %s\n", test_case->name);
	if (test_case->pfunc(mount_dir) == TEST_SUCCESS)
		ksft_test_result_pass("%s\n", test_case->name);
	else
		ksft_test_result_fail("%s\n", test_case->name);
}

int main(int argc, char *argv[])
{
	char *mount_dir = NULL;
	int i;
	int fd, count;

	if (parse_options(argc, argv))
		ksft_exit_fail_msg("Bad options\n");

	// Seed randomness pool for testing on QEMU
	// NOTE - this abuses the concept of randomness - do *not* ever do this
	// on a machine for production use - the device will think it has good
	// randomness when it does not.
	fd = open("/dev/urandom", O_WRONLY | O_CLOEXEC);
	count = 4096;
	for (int i = 0; i < 128; ++i)
		ioctl(fd, RNDADDTOENTCNT, &count);
	close(fd);

	ksft_print_header();

	if (geteuid() != 0)
		ksft_print_msg("Not a root, might fail to mount.\n");

	mount_dir = setup_mount_dir();
	if (mount_dir == NULL)
		ksft_exit_fail_msg("Can't create a mount dir\n");

#define MAKE_TEST(test)                                                        \
	{                                                                      \
		test, #test                                                    \
	}
	struct test_case cases[] = {
		MAKE_TEST(basic_test),
		MAKE_TEST(bpf_test),
	};
#undef MAKE_TEST

	if (test_options.test) {
		if (test_options.test <= 0 ||
		    test_options.test > ARRAY_SIZE(cases))
			ksft_exit_fail_msg("Invalid test\n");

		ksft_set_plan(1);
		run_one_test(mount_dir, &cases[test_options.test - 1]);
	} else {
		ksft_set_plan(ARRAY_SIZE(cases));
		for (i = 0; i < ARRAY_SIZE(cases); ++i)
			run_one_test(mount_dir, &cases[i]);
	}

	umount2(mount_dir, MNT_FORCE);
	rmdir(mount_dir);
	return !ksft_get_fail_cnt() ? ksft_exit_pass() : ksft_exit_fail();
}
