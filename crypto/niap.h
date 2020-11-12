// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Input and output vectors for NIAP FPT_TST_EXT.1 cryptographic
 * self-tests.
 *
 * Copyright (c) 2019-2021 Google LLC.
 */
#ifndef _CRYPTO_NIAP_H
#define _CRYPTO_NIAP_H

#include <linux/module.h>

struct generic_data {
	const u8 *key;
	size_t key_length;
	const u8 *iv;
	size_t iv_length;
	const u8 *input;
	size_t in_length;
	const u8 *output;
	size_t out_length;
};

struct aead_data {
	const u8 *key;
	size_t key_length;
	const u8 *iv;
	size_t iv_length;
	const u8 *input;
	size_t in_length;
	const u8 *output;
	size_t out_length;
	const u8 *assoc;
	size_t assoc_length;
	size_t tag_length;
};

struct drbg_data {
	u8 *entropy;
	size_t entropy_length;
	u8 *pers;
	size_t pers_length;
	u8 *entpr_a;
	u8 *entpr_b;
	size_t entpr_length;
	u8 *add_a;
	u8 *add_b;
	size_t add_length;
	u8 *output;
	size_t out_length;
};

struct niap_test {
	const char *alg;
	bool encrypt;
	int (*func)(const struct niap_test *);
	union {
		struct generic_data cipher;
		struct generic_data hash;
		struct aead_data aead;
		struct drbg_data drbg;
	};
};

static int __init niap_test_cipher(const struct niap_test *test);
static int __init niap_test_hash(const struct niap_test *test);
static int __init niap_test_aead(const struct niap_test *test);
static int __init niap_test_drbg(const struct niap_test *test);

// "message"
static const u8 niap_message[] __initconst = {
	0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Constants for SHA hashes
static const u8 niap_sha1_output[] __initconst = {
	0x2e, 0x25, 0x94, 0x36, 0xee, 0x77, 0xd7, 0x6f,
        0x01, 0xa7, 0xff, 0x81, 0x0d, 0x2a, 0x9a, 0xb1,
        0xf4, 0xb7, 0xe6, 0x30
};

static const u8 niap_sha224_output[] __initconst = {
	0xe6, 0xd4, 0xdd, 0x0d, 0xb3, 0xfe, 0x0b, 0xc8,
        0x31, 0x0d, 0x68, 0x21, 0xe3, 0x87, 0xa1, 0x0c,
        0xf1, 0x6e, 0x27, 0xb6, 0x2d, 0x5a, 0x61, 0xa1,
        0x9c, 0x51, 0xa5, 0x2f
};

static const u8 niap_sha256_output[] __initconst = {
	0xa0, 0x90, 0x1c, 0xb8, 0xb4, 0x22, 0xa3, 0xef,
	0x7d, 0x34, 0x45, 0x2d, 0x73, 0x32, 0x6c, 0x1e,
	0x09, 0x2d, 0x9e, 0x82, 0xee, 0x3f, 0x32, 0xd3,
	0xe8, 0x64, 0x30, 0x50, 0xb0, 0x2e, 0x7d, 0xf4
};

static const u8 niap_sha384_output[] __initconst = {
	0x6a, 0xa4, 0x59, 0xd3, 0x58, 0x83, 0xf8, 0x76,
	0xb3, 0x6d, 0x3c, 0xb2, 0x83, 0xd4, 0x5c, 0xc4,
	0xd1, 0x99, 0x78, 0x00, 0xcd, 0xeb, 0x74, 0xbe,
	0xc6, 0x32, 0x61, 0xcf, 0x21, 0xc5, 0xf8, 0x9b,
	0x42, 0xc0, 0xbf, 0x54, 0x57, 0xb0, 0xa6, 0x3d,
	0x41, 0xa0, 0xb6, 0x9d, 0xa6, 0x64, 0x1b, 0x91
};

static const u8 niap_sha512_output[] __initconst = {
	0x79, 0xe9, 0x87, 0xf8, 0x28, 0xc6, 0x9b, 0xd8,
        0xc9, 0xfc, 0xd2, 0xbf, 0x34, 0x35, 0xea, 0x21,
        0x8a, 0x5e, 0x2a, 0x5e, 0xea, 0x96, 0x71, 0x6a,
        0xc6, 0x76, 0xf6, 0x21, 0x95, 0x4b, 0x9d, 0xb7,
        0xb5, 0xb8, 0xec, 0xd6, 0xc7, 0xbc, 0x40, 0xd5,
        0x9f, 0x65, 0xdc, 0xa0, 0x58, 0x2e, 0x4e, 0x29,
        0xfc, 0xdd, 0x71, 0x0a, 0x5e, 0x64, 0x11, 0xb7,
        0x9c, 0xfb, 0x67, 0xb1, 0xbc, 0xef, 0x42, 0x80
};

// Constants for hmac(sha)
static const u8 niap_hmac_sha_key[] __initconst = {
	0x84, 0xe1, 0xb6, 0x59, 0x69, 0x4f, 0x5e, 0xa5,
	0x07, 0xcc, 0x74, 0x06, 0xb8, 0xec, 0x53, 0x4a
};

static const u8 niap_hmac_sha1_output[] __initconst = {
	0x20, 0x15, 0xdf, 0xe8, 0x62, 0x23, 0x52, 0x3d,
	0x53, 0x11, 0xa8, 0x6f, 0xdc, 0x19, 0xa5, 0x2d,
	0x10, 0x87, 0x00, 0xee
};

static const u8 niap_hmac_sha224_output[] __initconst = {
	0x69, 0x58, 0x87, 0x0e, 0x3f, 0x89, 0x26, 0x89,
	0x4c, 0x98, 0x69, 0xbd, 0x2c, 0xc1, 0xa7, 0x5b,
	0xa1, 0x66, 0xfd, 0xf5, 0x62, 0x8a, 0xf3, 0x13,
	0x37, 0xbf, 0x98, 0x62
};

static const u8 niap_hmac_sha256_output[] __initconst = {
	0x73, 0x82, 0x30, 0x7d, 0xc1, 0xd6, 0x12, 0xd9,
	0xd9, 0x1a, 0x65, 0xa1, 0x47, 0xc8, 0x20, 0x13,
	0xff, 0x4d, 0x69, 0x28, 0x05, 0x3b, 0xc2, 0xdf,
	0xea, 0x66, 0x44, 0x9b, 0xc4, 0x76, 0x73, 0x1d
};

static const u8 niap_hmac_sha512_output[] __initconst = {
	0x72, 0x74, 0x73, 0x5e, 0xa5, 0x1d, 0x1c, 0xba,
	0x62, 0xa7, 0x93, 0xbb, 0xa9, 0xc3, 0x97, 0x86,
	0x9d, 0x46, 0x26, 0xe7, 0x1b, 0x8c, 0x73, 0xf7,
	0x1a, 0xcb, 0xae, 0xda, 0x32, 0x77, 0x2d, 0x9b,
	0x73, 0xa1, 0xb1, 0x21, 0x16, 0x00, 0x38, 0xc8,
	0x61, 0x9c, 0xb9, 0x7f, 0x3c, 0x3b, 0x16, 0xe1,
	0xb8, 0x1d, 0x34, 0x8c, 0x82, 0xdc, 0xe6, 0xe3,
	0xc6, 0xaa, 0x4c, 0x10, 0x54, 0x3c, 0x09, 0xf4
};

// Constants for AES128
static const u8 niap_aes_key[] __initconst = {
	0x6f, 0x1e, 0x97, 0x12, 0xcf, 0x82, 0x40, 0x79,
	0x2c, 0x4f, 0x8a, 0x82, 0x78, 0x2a, 0x8c, 0xdd
};

static const u8 niap_ecb_aes_encrypted[] __initconst = {
	0xc0, 0x85, 0x2a, 0xc4, 0x01, 0x25, 0x1e, 0x15,
	0xfd, 0x53, 0x10, 0x84, 0x42, 0x96, 0xa9, 0x0c
};

static const u8 niap_cbc_aes_iv[] __initconst = {
	0x0c, 0xa7, 0x8b, 0xd2, 0x55, 0x73, 0x85, 0xef,
	0xe2, 0x1b, 0x96, 0xb1, 0x0f, 0x14, 0xec, 0xf0
};

static const u8 niap_cbc_aes_encrypted[] __initconst = {
	0xd1, 0x4f, 0xe0, 0x79, 0xa1, 0x68, 0xe4, 0xa4,
	0x51, 0x96, 0xf6, 0x10, 0x5d, 0x6a, 0x63, 0xa0
};

// Constants for gcm(aes)
static const u8 niap_gcm_aes_plaintext[] __initconst =
	"\xd9\x31\x32\x25\xf8\x84\x06\xe5\xa5\x59\x09\xc5\xaf\xf5\x26\x9a"
	"\x86\xa7\xa9\x53\x15\x34\xf7\xda\x2e\x4c\x30\x3d\x8a\x31\x8a\x72"
	"\x1c\x3c\x0c\x95\x95\x68\x09\x53\x2f\xcf\x0e\x24\x49\xa6\xb5\x25"
	"\xb1\x6a\xed\xf5\xaa\x0d\xe6\x57\xba\x63\x7b\x39";

static const u8 niap_gcm_aes_key[] __initconst =
	"\xfe\xff\xe9\x92\x86\x65\x73\x1c\x6d\x6a\x8f\x94\x67\x30\x83\x08";

static const u8 niap_gcm_aes_iv[] __initconst =
	"\xca\xfe\xba\xbe\xfa\xce\xdb\xad\xde\xca\xf8\x88";

static const u8 niap_gcm_aes_assoc[] __initconst =
	"\xfe\xed\xfa\xce\xde\xad\xbe\xef\xfe\xed\xfa\xce\xde\xad\xbe\xef"
	"\xab\xad\xda\xd2";

static const u8 niap_gcm_aes_ciphertext[] __initconst =
	"\x42\x83\x1e\xc2\x21\x77\x74\x24\x4b\x72\x21\xb7\x84\xd0\xd4\x9c"
	"\xe3\xaa\x21\x2f\x2c\x02\xa4\xe0\x35\xc1\x7e\x23\x29\xac\xa1\x2e"
	"\x21\xd5\x14\xb2\x54\x66\x93\x1c\x7d\x8f\x6a\x5a\xac\x84\xaa\x05"
	"\x1b\xa3\x0b\x39\x6a\x0a\xac\x97\x3d\x58\xe0\x91"
	"\x5b\xc9\x4f\xbc\x32\x21\xa5\xdb\x94\xfa\xe9\x5a\xe7\x12\x1a\x47";

static const size_t niap_gcm_aes_tag_len = 16;

// The list of power-up tests
static const struct niap_test niap_tests[] __initconst = {
	{
		.alg		= "sha1",
		.func		= niap_test_hash,
		.hash		= {
			.key		= NULL,
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_sha1_output,
			.out_length	= sizeof(niap_sha1_output)
		}
	}, {
		.alg		= "sha224",
		.func		= niap_test_hash,
		.hash		= {
			.key		= NULL,
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_sha224_output,
			.out_length	= sizeof(niap_sha224_output)
		}
	}, {
		.alg		= "sha256",
		.func		= niap_test_hash,
		.hash		= {
			.key		= NULL,
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_sha256_output,
			.out_length	= sizeof(niap_sha256_output)
		}
	}, {
		.alg		= "sha384",
		.func		= niap_test_hash,
		.hash		= {
			.key		= NULL,
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_sha384_output,
			.out_length	= sizeof(niap_sha384_output)
		}
	}, {
		.alg		= "sha512",
		.func		= niap_test_hash,
		.hash		= {
			.key		= NULL,
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_sha512_output,
			.out_length	= sizeof(niap_sha512_output)
		}
	}, {
		.alg		= "hmac(sha1)",
		.func		= niap_test_hash,
		.hash		= {
			.key		= niap_hmac_sha_key,
			.key_length	= sizeof(niap_hmac_sha_key),
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_hmac_sha1_output,
			.out_length	= sizeof(niap_hmac_sha1_output)
		}
	}, {
		.alg		= "hmac(sha224)",
		.func		= niap_test_hash,
		.hash		= {
			.key		= niap_hmac_sha_key,
			.key_length	= sizeof(niap_hmac_sha_key),
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_hmac_sha224_output,
			.out_length	= sizeof(niap_hmac_sha224_output)
		}
	}, {
		.alg		= "hmac(sha256)",
		.func		= niap_test_hash,
		.hash		= {
			.key		= niap_hmac_sha_key,
			.key_length	= sizeof(niap_hmac_sha_key),
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_hmac_sha256_output,
			.out_length	= sizeof(niap_hmac_sha256_output)
		}
	}, {
		.alg		= "hmac(sha512)",
		.func		= niap_test_hash,
		.hash		= {
			.key		= niap_hmac_sha_key,
			.key_length	= sizeof(niap_hmac_sha_key),
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_hmac_sha512_output,
			.out_length	= sizeof(niap_hmac_sha512_output)
		}
	}, {
		.alg		= "cbc(aes)",
		.encrypt	= true,
		.func		= niap_test_cipher,
		.cipher		= {
			.key		= niap_aes_key,
			.key_length	= sizeof(niap_aes_key),
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.iv		= niap_cbc_aes_iv,
			.iv_length	= sizeof(niap_cbc_aes_iv),
			.output		= niap_cbc_aes_encrypted,
			.out_length	= sizeof(niap_cbc_aes_encrypted)
		}
	}, {
		.alg		= "cbc(aes)",
		.encrypt	= false,
		.func		= niap_test_cipher,
		.cipher		= {
			.key		= niap_aes_key,
			.key_length	= sizeof(niap_aes_key),
			.input		= niap_cbc_aes_encrypted,
			.in_length	= sizeof(niap_cbc_aes_encrypted),
			.iv		= niap_cbc_aes_iv,
			.iv_length	= sizeof(niap_cbc_aes_iv),
			.output		= niap_message,
			.out_length	= sizeof(niap_message)
		}

	}, {
		.alg		= "ecb(aes)",
		.encrypt	= true,
		.func		= niap_test_cipher,
		.cipher		= {
			.key		= niap_aes_key,
			.key_length	= sizeof(niap_aes_key),
			.input		= niap_message,
			.in_length	= sizeof(niap_message),
			.output		= niap_ecb_aes_encrypted,
			.out_length	= sizeof(niap_ecb_aes_encrypted)
		}
	}, {
		.alg		= "ecb(aes)",
		.encrypt	= false,
		.func		= niap_test_cipher,
		.cipher		= {
			.key		= niap_aes_key,
			.key_length	= sizeof(niap_aes_key),
			.input		= niap_ecb_aes_encrypted,
			.in_length	= sizeof(niap_ecb_aes_encrypted),
			.output		= niap_message,
			.out_length	= sizeof(niap_message)
		}
	}, {
		.alg		= "gcm(aes)",
		.encrypt	= true,
		.func		= niap_test_aead,
		.aead		= {
			.key		= niap_gcm_aes_key,
			.key_length	= sizeof(niap_gcm_aes_key) - 1,
			.iv		= niap_gcm_aes_iv,
			.iv_length	= sizeof(niap_gcm_aes_iv) - 1,
			.input		= niap_gcm_aes_plaintext,
			.in_length	= sizeof(niap_gcm_aes_plaintext) - 1,
			.assoc		= niap_gcm_aes_assoc,
			.assoc_length	= sizeof(niap_gcm_aes_assoc) - 1,
			.output		= niap_gcm_aes_ciphertext,
			.out_length	= sizeof(niap_gcm_aes_ciphertext) - 1,
			.tag_length	= niap_gcm_aes_tag_len
		}
	}, {
		.alg		= "gcm(aes)",
		.encrypt	= false,
		.func		= niap_test_aead,
		.aead		= {
			.key		= niap_gcm_aes_key,
			.key_length	= sizeof(niap_gcm_aes_key) - 1,
			.iv		= niap_gcm_aes_iv,
			.iv_length	= sizeof(niap_gcm_aes_iv) - 1,
			.input		= niap_gcm_aes_ciphertext,
			.in_length	= sizeof(niap_gcm_aes_ciphertext) - 1,
			.assoc		= niap_gcm_aes_assoc,
			.assoc_length	= sizeof(niap_gcm_aes_assoc) - 1,
			.output		= niap_gcm_aes_plaintext,
			.out_length	= sizeof(niap_gcm_aes_plaintext) - 1,
			.tag_length	= niap_gcm_aes_tag_len
		}
	}, {
		.alg	= "drbg_pr_hmac_sha256",
		.func	= niap_test_drbg,
		.drbg	= {
			.entropy =
				"\xc7\xcc\xbc\x67\x7e\x21\x66\x1e\x27\x2b\x63\xdd"
				"\x3a\x78\xdc\xdf\x66\x6d\x3f\x24\xae\xcf\x37\x01"
				"\xa9\x0d\x89\x8a\xa7\xdc\x81\x58\xae\xb2\x10\x15"
				"\x7e\x18\x44\x6d\x13\xea\xdf\x37\x85\xfe\x81\xfb",
			.entropy_length = 48,
			.entpr_a =
				"\x7b\xa1\x91\x5b\x3c\x04\xc4\x1b\x1d\x19\x2f\x1a"
				"\x18\x81\x60\x3c\x6c\x62\x91\xb7\xe9\xf5\xcb\x96"
				"\xbb\x81\x6a\xcc\xb5\xae\x55\xb6",
			.entpr_b =
				"\x99\x2c\xc7\x78\x7e\x3b\x88\x12\xef\xbe\xd3\xd2"
				"\x7d\x2a\xa5\x86\xda\x8d\x58\x73\x4a\x0a\xb2\x2e"
				"\xbb\x4c\x7e\xe3\x9a\xb6\x81\xc1",
			.entpr_length = 32,
			.output =
				"\x95\x6f\x95\xfc\x3b\xb7\xfe\x3e\xd0\x4e\x1a\x14"
				"\x6c\x34\x7f\x7b\x1d\x0d\x63\x5e\x48\x9c\x69\xe6"
				"\x46\x07\xd2\x87\xf3\x86\x52\x3d\x98\x27\x5e\xd7"
				"\x54\xe7\x75\x50\x4f\xfb\x4d\xfd\xac\x2f\x4b\x77"
				"\xcf\x9e\x8e\xcc\x16\xa2\x24\xcd\x53\xde\x3e\xc5"
				"\x55\x5d\xd5\x26\x3f\x89\xdf\xca\x8b\x4e\x1e\xb6"
				"\x88\x78\x63\x5c\xa2\x63\x98\x4e\x6f\x25\x59\xb1"
				"\x5f\x2b\x23\xb0\x4b\xa5\x18\x5d\xc2\x15\x74\x40"
				"\x59\x4c\xb4\x1e\xcf\x9a\x36\xfd\x43\xe2\x03\xb8"
				"\x59\x91\x30\x89\x2a\xc8\x5a\x43\x23\x7c\x73\x72"
				"\xda\x3f\xad\x2b\xba\x00\x6b\xd1",
			.out_length = 128,
			.add_a =
				"\x18\xe8\x17\xff\xef\x39\xc7\x41\x5c\x73\x03\x03"
				"\xf6\x3d\xe8\x5f\xc8\xab\xe4\xab\x0f\xad\xe8\xd6"
				"\x86\x88\x55\x28\xc1\x69\xdd\x76",
			.add_b =
				"\xac\x07\xfc\xbe\x87\x0e\xd3\xea\x1f\x7e\xb8\xe7"
				"\x9d\xec\xe8\xe7\xbc\xf3\x18\x25\x77\x35\x4a\xaa"
				"\x00\x99\x2a\xdd\x0a\x00\x50\x82",
			.add_length = 32,
			.pers =
				"\xbc\x55\xab\x3c\xf6\x52\xb0\x11\x3d\x7b\x90\xb8"
				"\x24\xc9\x26\x4e\x5a\x1e\x77\x0d\x3d\x58\x4a\xda"
				"\xd1\x81\xe9\xf8\xeb\x30\x8f\x6f",
			.pers_length = 32,
		}
	}, {
		.alg	= "drbg_nopr_hmac_sha256",
		.func	= niap_test_drbg,
		.drbg	= {
			.entropy =
				"\xf9\x7a\x3c\xfd\x91\xfa\xa0\x46\xb9\xe6\x1b\x94"
				"\x93\xd4\x36\xc4\x93\x1f\x60\x4b\x22\xf1\x08\x15"
				"\x21\xb3\x41\x91\x51\xe8\xff\x06\x11\xf3\xa7\xd4"
				"\x35\x95\x35\x7d\x58\x12\x0b\xd1\xe2\xdd\x8a\xed",
			.entropy_length = 48,
			.output =
				"\xc6\x87\x1c\xff\x08\x24\xfe\x55\xea\x76\x89\xa5"
				"\x22\x29\x88\x67\x30\x45\x0e\x5d\x36\x2d\xa5\xbf"
				"\x59\x0d\xcf\x9a\xcd\x67\xfe\xd4\xcb\x32\x10\x7d"
				"\xf5\xd0\x39\x69\xa6\x6b\x1f\x64\x94\xfd\xf5\xd6"
				"\x3d\x5b\x4d\x0d\x34\xea\x73\x99\xa0\x7d\x01\x16"
				"\x12\x6d\x0d\x51\x8c\x7c\x55\xba\x46\xe1\x2f\x62"
				"\xef\xc8\xfe\x28\xa5\x1c\x9d\x42\x8e\x6d\x37\x1d"
				"\x73\x97\xab\x31\x9f\xc7\x3d\xed\x47\x22\xe5\xb4"
				"\xf3\x00\x04\x03\x2a\x61\x28\xdf\x5e\x74\x97\xec"
				"\xf8\x2c\xa7\xb0\xa5\x0e\x86\x7e\xf6\x72\x8a\x4f"
				"\x50\x9a\x8c\x85\x90\x87\x03\x9c",
			.out_length = 128,
			.add_a =
				"\x51\x72\x89\xaf\xe4\x44\xa0\xfe\x5e\xd1\xa4\x1d"
				"\xbb\xb5\xeb\x17\x15\x00\x79\xbd\xd3\x1e\x29\xcf"
				"\x2f\xf3\x00\x34\xd8\x26\x8e\x3b",
			.add_b =
				"\x88\x02\x8d\x29\xef\x80\xb4\xe6\xf0\xfe\x12\xf9"
				"\x1d\x74\x49\xfe\x75\x06\x26\x82\xe8\x9c\x57\x14"
				"\x40\xc0\xc9\xb5\x2c\x42\xa6\xe0",
			.add_length = 32,
			.pers = NULL,
			.pers_length = 0,
		}
	}
};

#endif	/* _CRYPTO_NIAP_H */
