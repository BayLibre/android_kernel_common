// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */
#define _GNU_SOURCE

#include "test_fuse.h"

static const char *ft_src = "ft-src";
static const char *ft_dst = "ft-dst";

/* Slow but semantically easy string functions */

/*
 * struct s just wraps a char pointer
 * It is a pointer to a malloc'd string, or null
 * All consumers handle null input correctly
 * All consumers free the string
 */
struct s {
	char *s;
};

struct s s(const char *s1)
{
	struct s s = {0};
	if (!s1)
		return s;

	s.s = malloc(strlen(s1) + 1);
	if (!s.s)
		return s;

	strcpy(s.s, s1);
	return s;
}

struct s s_cat(struct s s1, struct s s2)
{
	struct s s = {0};
	if (!s1.s || !s2.s)
		goto out;

	s.s = malloc(strlen(s1.s) + strlen(s2.s) + 1);
	if (!s.s)
		goto out;

	strcpy(s.s, s1.s);
	strcat(s.s, s2.s);
out:
	free(s1.s);
	free(s2.s);
	return s;
}

struct s s_path(struct s s1, struct s s2)
{
	return s_cat(s_cat(s1, s("/")), s2);
}

/*static int s_mkdirat(int dirfd, struct s pathname, mode_t mode)
{
	int res;
	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = mkdirat(dirfd, pathname.s, mode);
	free(pathname.s);
	return res;
}*/

int s_open(struct s pathname, int flags, ...)
{
	va_list ap;
	int res;

	va_start (ap, flags);
	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	if (flags & (O_CREAT | O_TMPFILE))
		res = open(pathname.s, flags, va_arg(ap, mode_t));
	else
		res = open(pathname.s, flags);

	free(pathname.s);
	va_end(ap);
	return res;
}

int s_openat(int dirfd, struct s pathname, int flags, ...)
{
	va_list ap;
	int res;

	va_start (ap, flags);
	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	if (flags & (O_CREAT | O_TMPFILE))
		res = openat(dirfd, pathname.s, flags, va_arg(ap, mode_t));
	else
		res = openat(dirfd, pathname.s, flags);

	free(pathname.s);
	va_end(ap);
	return res;
}

int s_creat(struct s pathname, mode_t mode)
{
	int res;

	if (!pathname.s) {
		errno = ENOMEM;
		return -1;
	}

	res = creat(pathname.s, mode);
	free(pathname.s);
	return res;
}

static void fill_buffer(uint8_t *data, size_t len, int file, int block)
{
	int i;
	int seed = 7919 * file + block;

	for (i = 0; i < len; i++) {
		seed = 1103515245 * seed + 12345;
		data[i] = (uint8_t)(seed >> (i % 13));
	}
}

static bool test_buffer(uint8_t *data, size_t len, int file, int block)
{
	int i;
	int seed = 7919 * file + block;

	for (i = 0; i < len; i++) {
		seed = 1103515245 * seed + 12345;
		if (data[i] != (uint8_t)(seed >> (i % 13)))
			return false;
	}

	return true;
}

static int create_file(int dir, struct s name, int index, size_t blocks)
{
	int result = TEST_FAILURE;
	int fd = -1;
	int i;
	uint8_t data[PAGE_SIZE];

	TEST(fd = s_openat(dir, name, O_CREAT | O_WRONLY, 0777), fd != -1);
	for (i = 0; i < blocks; ++i) {
		fill_buffer(data, PAGE_SIZE, index, i);
		TESTEQUAL(write(fd, data, sizeof(data)), PAGE_SIZE);
	}
	TESTSYSCALL(close(fd));
	result = TEST_SUCCESS;

out:
	close(fd);
	return result;
}

static int bpf_test_trace(const char *substr)
{
	int result = TEST_FAILURE;
	int tp = -1;
	char trace_buffer[4096] = {};
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

static int basic_test(const char *mount_dir)
{
	const char *test_name = "test";
	const char *test_data = "data";

	int result = TEST_FAILURE;
	int fuse_dev = -1;
	char *filename = NULL;
	int fd = -1;
	int pid = -1;
	int status;

	TESTEQUAL(mount_fuse(mount_dir, -1, -1, &fuse_dev), 0);
	FUSE_ACTION
		char data[256];

		filename = concat_file_name(mount_dir, test_name);
		TESTERR(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);
		TESTEQUAL(read(fd, data, strlen(test_data)), strlen(test_data));
		TESTCOND(!strcmp(data, test_data));
		TESTSYSCALL(close(fd));
		fd = -1;
	FUSE_DAEMON
		DECL_FUSE_IN(open);
		DECL_FUSE_IN(read);
		DECL_FUSE_IN(flush);
		DECL_FUSE_IN(release);

		TESTFUSELOOKUP(test_name);
		TESTFUSEOUT1(fuse_entry_out, ((struct fuse_entry_out) {
			.nodeid		= 2,
			.generation	= 1,
			.attr.ino = 100,
			.attr.size = 4,
			.attr.blksize = 512,
			.attr.mode = S_IFREG | 0777,
			}));

		TESTFUSEIN(FUSE_OPEN, open_in);
		TESTFUSEOUT1(fuse_open_out, ((struct fuse_open_out) {
			.fh = 1,
			.open_flags = open_in->flags,
		}));

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
	umount(mount_dir);
	return result;
}

static int bpf_test_real(const char *mount_dir)
{
	const char *test_name = "real";
	const char *test_data = "Weebles wobble but they don't fall down";
	int result = TEST_FAILURE;
	int bpf_fd = -1;
	int src_fd = -1;
	int fuse_dev = -1;
	char *filename = NULL;
	int fd = -1;
	char read_buffer[256] = {};
	ssize_t bytes_read;

	TEST(src_fd = open(ft_src, O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TEST(fd = openat(src_fd, test_name, O_CREAT | O_RDWR, 0777), fd != -1);
	TESTEQUAL(write(fd, test_data, strlen(test_data)), strlen(test_data));
	TESTSYSCALL(close(fd));
	fd = -1;

	TESTEQUAL(install_bpf("test_trace.raw", &bpf_fd), 0);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	filename = concat_file_name(mount_dir, test_name);
	TESTERR(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);
	bytes_read = read(fd, read_buffer, strlen(test_data));
	TESTEQUAL(bytes_read, strlen(test_data));
	TESTEQUAL(strcmp(test_data, read_buffer), 0);
	TESTEQUAL(bpf_test_trace("read"), 0);

	result = TEST_SUCCESS;
out:
	close(fuse_dev);
	close(fd);
	free(filename);
	umount(mount_dir);
	close(src_fd);
	close(bpf_fd);
	return result;
}


static int bpf_test_partial(const char *mount_dir)
{
	const char *test_name = "partial";
	int result = TEST_FAILURE;
	int bpf_fd = -1;
	int src_fd = -1;
	int fuse_dev = -1;
	char *filename = NULL;
	int fd = -1;
	int pid = -1;
	int status;

	TEST(src_fd = open(ft_src, O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TESTEQUAL(create_file(src_fd, s(test_name), 1, 2), 0);
	TESTEQUAL(install_bpf("test_trace.raw", &bpf_fd), 0);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	FUSE_ACTION
		uint8_t data[PAGE_SIZE];

		TEST(filename = concat_file_name(mount_dir, test_name),
		     filename);
		TESTERR(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);
		TESTEQUAL(read(fd, data, PAGE_SIZE), PAGE_SIZE);
		TESTEQUAL(bpf_test_trace("read"), 0);
		TESTCOND(test_buffer(data, PAGE_SIZE, 2, 0));
		TESTCOND(!test_buffer(data, PAGE_SIZE, 1, 0));
		TESTEQUAL(read(fd, data, PAGE_SIZE), PAGE_SIZE);
		TESTCOND(test_buffer(data, PAGE_SIZE, 1, 1));
		TESTCOND(!test_buffer(data, PAGE_SIZE, 2, 1));
		TESTSYSCALL(close(fd));
		fd = -1;
	FUSE_DAEMON
		DECL_FUSE_IN(open);
		DECL_FUSE_IN(read);
		DECL_FUSE_IN(forget);
		DECL_FUSE_IN(release);
		uint8_t data[PAGE_SIZE];

		TESTFUSEIN(FUSE_OPEN, open_in);
		TESTFUSEOUT1(fuse_open_out, ((struct fuse_open_out) {
			.fh = 1,
			.open_flags = open_in->flags,
		}));

		TESTFUSEIN(FUSE_READ, read_in);
		fill_buffer(data, PAGE_SIZE, 2, 0);
		TESTFUSEOUTREAD(data, PAGE_SIZE);

		TESTFUSEIN(FUSE_RELEASE, release_in);
		TESTFUSEOUTEMPTY();

		TESTFUSEIN(FUSE_FORGET, forget_in);
	FUSE_DONE

	result = TEST_SUCCESS;
out:
	if (!pid)
		exit(TEST_FAILURE);
	close(fuse_dev);
	close(fd);
	free(filename);
	umount(mount_dir);
	close(src_fd);
	close(bpf_fd);
	return result;
}

static int bpf_test_attrs(const char *mount_dir)
{
	const char *test_name = "partial";
	int result = TEST_FAILURE;
	int bpf_fd = -1;
	int src_fd = -1;
	int fuse_dev = -1;
	char *filename = NULL;
	struct stat st;

	TEST(src_fd = open(ft_src, O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TESTEQUAL(create_file(src_fd, s(test_name), 1, 2), 0);
	TESTEQUAL(install_bpf("test_trace.raw", &bpf_fd), 0);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	TEST(filename = concat_file_name(mount_dir, test_name), filename);
	TESTSYSCALL(stat(filename, &st));

	result = TEST_SUCCESS;
out:
	close(fuse_dev);
	free(filename);
	umount(mount_dir);
	close(src_fd);
	close(bpf_fd);
	return result;
}

static int bpf_test_readdir(const char *mount_dir)
{
	const char *names[] = {"real", "partial", "fake", ".", ".."};
	int result = TEST_FAILURE;
	int bpf_fd = -1;
	int src_fd = -1;
	int fuse_dev = -1;
	int pid = -1;
	int status;
	DIR *dir = NULL;
	struct dirent *dirent;

	TEST(src_fd = open(ft_src, O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TESTEQUAL(create_file(src_fd, s(names[0]), 1, 2), 0);
	TESTEQUAL(create_file(src_fd, s(names[1]), 1, 2), 0);
	TESTEQUAL(install_bpf("test_trace.raw", &bpf_fd), 0);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	FUSE_ACTION
		int i, j;

		TEST(dir = opendir(mount_dir), dir);
		for (i = 0; i < ARRAY_SIZE(names); ++i) {
			TEST(dirent = readdir(dir), dirent);

			for (j = 0; j < ARRAY_SIZE(names); ++j)
				if (names[j] &&
				    strcmp(names[j], dirent->d_name) == 0) {
					names[j] = NULL;
					break;
				}
			TESTNE(j, ARRAY_SIZE(names));
		}
		TEST(dirent = readdir(dir), dirent == NULL);
		TESTSYSCALL(closedir(dir));
		dir = NULL;
		TESTEQUAL(bpf_test_trace("readdir"), 0);
	FUSE_DAEMON
		struct fuse_in_header *in_header =
			(struct fuse_in_header *)bytes_in;
		ssize_t res = read(fuse_dev, bytes_in, sizeof(bytes_in));
		struct fuse_dirent *fuse_dirent =
			(struct fuse_dirent *) (bytes_in + res);

		TESTGE(res, sizeof(*in_header));
		TESTEQUAL(in_header->opcode, FUSE_READDIR | FUSE_POSTFILTER);
		*fuse_dirent = (struct fuse_dirent) {
			.ino = 100,
			.off = 5,
			.namelen = strlen("fake"),
			.type = DT_REG,
		};
		strcpy((char*)(bytes_in + res + sizeof(*fuse_dirent)), "fake");
		res += FUSE_DIRENT_ALIGN(sizeof(*fuse_dirent) + strlen("fake") + 1);
		TESTFUSEOUTREAD(bytes_in + sizeof(struct fuse_in_header),
				res - sizeof(struct fuse_in_header));
	FUSE_DONE

	result = TEST_SUCCESS;
out:
	closedir(dir);
	close(fuse_dev);
	umount(mount_dir);
	close(src_fd);
	close(bpf_fd);
	return result;
}

/*
 * This test is more to show what classic fuse does with a creat in a subdir
 * than a test of any new functionality
 */
static int bpf_test_creat(const char *mount_dir)
{
	const char *dir_name = "show";
	const char *file_name = "file";
	int result = TEST_FAILURE;
	int fuse_dev = -1;
	int pid = -1;
	int status;
	int fd = -1;

	TESTEQUAL(mount_fuse(mount_dir, -1, -1, &fuse_dev), 0);

	FUSE_ACTION
		TEST(fd = s_creat(s_path(s_path(s(mount_dir), s(dir_name)),
					 s(file_name)),
				  0777),
		     fd != -1);
		TESTSYSCALL(close(fd));
	FUSE_DAEMON
		DECL_FUSE_IN(create);
		DECL_FUSE_IN(release);
		DECL_FUSE_IN(flush);

		TESTFUSELOOKUP(dir_name);
		TESTFUSEOUT1(fuse_entry_out, ((struct fuse_entry_out) {
			.nodeid		= 3,
			.generation	= 1,
			.attr.ino = 100,
			.attr.size = 4,
			.attr.blksize = 512,
			.attr.mode = S_IFDIR | 0777,
			}));

		TESTFUSELOOKUP(file_name);
		TESTFUSEOUTERROR(-ENOENT);

		TESTFUSEINEXT(FUSE_CREATE, create_in, strlen(file_name) + 1);
		TESTFUSEOUT2(fuse_entry_out, ((struct fuse_entry_out) {
			.nodeid		= 2,
			.generation	= 1,
			.attr.ino = 200,
			.attr.size = 4,
			.attr.blksize = 512,
			.attr.mode = S_IFREG,
			}),
			fuse_open_out, ((struct fuse_open_out) {
			.fh = 1,
			.open_flags = create_in->flags,
			}));

		TESTFUSEIN(FUSE_FLUSH, flush_in);
		TESTFUSEOUTEMPTY();

		TESTFUSEIN(FUSE_RELEASE, release_in);
		TESTFUSEOUTEMPTY();
	FUSE_DONE

	result = TEST_SUCCESS;
out:
	close(fuse_dev);
	umount(mount_dir);
	return result;
}

static int bpf_test_hidden_entries(const char *mount_dir)
{
	const char *dir_names[] = {
		"show",
		"hide",
	};
	const char *file_name = "file";
	int result = TEST_FAILURE;
	int src_fd = -1;
	int bpf_fd = -1;
	int fuse_dev = -1;
	int pid = -1;
	int status;
	int fd = -1;

	TEST(src_fd = open(ft_src, O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TESTSYSCALL(mkdirat(src_fd, dir_names[0], 0777));
	TESTSYSCALL(mkdirat(src_fd, dir_names[1], 0777));
	TESTEQUAL(install_bpf("test_hidden.raw", &bpf_fd), 0);
	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	FUSE_ACTION
		TEST(fd = s_creat(s_path(s_path(s(mount_dir), s(dir_names[0])),
					 s(file_name)),
				  0777),
		     fd != -1);
		TESTSYSCALL(close(fd));
	FUSE_DAEMON
		DECL_FUSE_IN(release);

		TESTFUSEIN(FUSE_RELEASE, release_in);
		TESTFUSEOUTEMPTY();
	FUSE_DONE

	result = TEST_SUCCESS;
out:
	close(fuse_dev);
	umount(mount_dir);
	close(bpf_fd);
	close(src_fd);
	return result;
}

static int parse_options(int argc, char *const *argv)
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

static void run_one_test(const char *mount_dir, struct test_case *test_case)
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
	char *src_dir = NULL;
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

	src_dir = setup_mount_dir(ft_src);
	mount_dir = setup_mount_dir(ft_dst);
	if (src_dir == NULL || mount_dir == NULL)
		ksft_exit_fail_msg("Can't create a mount dir\n");

#define MAKE_TEST(test)                                                        \
	{                                                                      \
		test, #test                                                    \
	}
	struct test_case cases[] = {
		MAKE_TEST(basic_test),
		MAKE_TEST(bpf_test_real),
		MAKE_TEST(bpf_test_partial),
		MAKE_TEST(bpf_test_attrs),
		MAKE_TEST(bpf_test_readdir),
		MAKE_TEST(bpf_test_creat),
		MAKE_TEST(bpf_test_hidden_entries),
	};
#undef MAKE_TEST

	if (test_options.test) {
		if (test_options.test <= 0 ||
		    test_options.test > ARRAY_SIZE(cases))
			ksft_exit_fail_msg("Invalid test\n");

		ksft_set_plan(1);
		delete_dir_tree(mount_dir, false);
		delete_dir_tree(src_dir, false);
		run_one_test(mount_dir, &cases[test_options.test - 1]);
	} else {
		ksft_set_plan(ARRAY_SIZE(cases));
		for (i = 0; i < ARRAY_SIZE(cases); ++i) {
			delete_dir_tree(mount_dir, false);
			delete_dir_tree(src_dir, false);
			run_one_test(mount_dir, &cases[i]);
		}
	}

	umount2(mount_dir, MNT_FORCE);
	delete_dir_tree(mount_dir, true);
	delete_dir_tree(src_dir, true);
	return !ksft_get_fail_cnt() ? ksft_exit_pass() : ksft_exit_fail();
}
