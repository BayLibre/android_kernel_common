// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NIAP FPT_TST_EXT.1 cryptographic self-tests.
 *
 * Copyright (c) 2019-2021 Google LLC.
 */

#include <crypto/aead.h>
#include <crypto/drbg.h>
#include <crypto/hash.h>
#include <crypto/rng.h>
#include <crypto/skcipher.h>
#include <linux/crypto.h>
#include <linux/minmax.h>
#include <linux/reboot.h>
#include <linux/scatterlist.h>

#include "niap.h"

static inline void dump(const u8 *buf, unsigned int length)
{
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 1, buf, length,
		       false);
}

static int __init niap_test_cipher(const struct niap_test *test)
{
	int err = 0;
	DECLARE_CRYPTO_WAIT(wait);
	struct crypto_skcipher *tfm = NULL;
	struct skcipher_request *req = NULL;
	struct scatterlist src;
	struct scatterlist dst;
	u8 *input = NULL;
	u8 *iv = NULL;
	u8 *output = NULL;

	input = kmemdup(test->cipher.input, test->cipher.in_length, GFP_KERNEL);
	output = kzalloc(test->cipher.out_length, GFP_KERNEL);

	if (!input || !output) {
		err = -ENOMEM;
		goto out;
	}

	if (test->cipher.iv_length) {
		iv = kmemdup(test->cipher.iv, test->cipher.iv_length,
			     GFP_KERNEL);
		if (!iv) {
			err = -ENOMEM;
			goto out;
		}
	}

	tfm = crypto_alloc_skcipher(test->alg, 0, 0);
	if (IS_ERR(tfm)) {
		err = PTR_ERR(tfm);
		tfm = NULL;
		goto out;
	}

	err = crypto_skcipher_setkey(tfm, test->cipher.key,
				     test->cipher.key_length);
	if (err)
		goto out;

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto out;
	}

	sg_init_one(&src, input, test->cipher.in_length);
	sg_init_one(&dst, output, test->cipher.out_length);

	skcipher_request_set_tfm(req, tfm);
	skcipher_request_set_callback(
		req, CRYPTO_TFM_REQ_MAY_SLEEP | CRYPTO_TFM_REQ_MAY_BACKLOG,
		crypto_req_done, &wait);
	skcipher_request_set_crypt(req, &src, &dst, test->cipher.in_length, iv);

	if (test->encrypt)
		err = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);
	else
		err = crypto_wait_req(crypto_skcipher_decrypt(req), &wait);
	if (err)
		goto out;

	if (memcmp(output, test->cipher.output, test->cipher.out_length)) {
		pr_err("niap_test_cipher: invalid result:\n");
		dump(output, test->cipher.out_length);
		err = -EBADMSG;
	}

out:
	if (err)
		pr_err("niap_test_cipher: %s %s failed: %d\n", test->alg,
		       test->encrypt ? "encryption" : "decryption", err);
	else
		pr_info("niap_test_cipher: %s %s passed\n", test->alg,
			test->encrypt ? "encryption" : "decryption");

	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	kfree(input);
	kfree(iv);
	kfree(output);

	return err;
}

static int __init niap_test_hash(const struct niap_test *test)
{
	int err = 0;
	struct crypto_shash *tfm = NULL;
	struct shash_desc *desc = NULL;
	u8 *output = NULL;

	output = kzalloc(test->hash.out_length, GFP_KERNEL);
	if (!output) {
		err = -ENOMEM;
		goto out;
	}

	tfm = crypto_alloc_shash(test->alg, 0, 0);
	if (IS_ERR(tfm)) {
		err = PTR_ERR(tfm);
		tfm = NULL;
		goto out;
	}

	if (test->hash.key) {
		err = crypto_shash_setkey(tfm, test->hash.key,
					  test->hash.key_length);
		if (err)
			goto out;
	}

	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		err = -ENOMEM;
		goto out;
	}

	desc->tfm = tfm;

	err = crypto_shash_digest(desc, test->hash.input, test->hash.in_length,
				  output);
	if (err)
		goto out;

	if (memcmp(output, test->hash.output, test->hash.out_length)) {
		pr_err("niap_test_hash: invalid result:\n");
		dump(output, test->hash.out_length);
		err = -EBADMSG;
	}

out:
	if (err)
		pr_err("niap_test_hash: %s failed: %d\n", test->alg, err);
	else
		pr_info("niap_test_hash: %s passed\n", test->alg);

	crypto_free_shash(tfm);
	kfree(desc);
	kfree(output);

	return err;
}

struct tcrypt_result {
	struct completion completion;
	int err;
};

static void tcrypt_complete(struct crypto_async_request *req, int err)
{
	struct tcrypt_result *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

static int __init niap_test_aead(const struct niap_test *test)
{
	int err = 0;
	struct crypto_aead *tfm;
	struct aead_request *req = NULL;
	struct scatterlist *sg = NULL;
	struct tcrypt_result result;
	unsigned int k, authsize, in_out_size;
	u8 *input_output = NULL;

	tfm = crypto_alloc_aead(test->alg, 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("niap_test_aead: Failed to load transform for %s: %ld\n",
		       test->alg, PTR_ERR(tfm));
		err = PTR_ERR(tfm);
		goto out;
	}

	sg = kmalloc(sizeof(*sg) * 8 * 2, GFP_KERNEL);
	init_completion(&result.completion);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("niap_test_aead: Failed to allocate request for %s\n",
		       test->alg);
		err = -ENOMEM;
		goto out;
	}

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tcrypt_complete, &result);

	err = crypto_aead_setkey(tfm, test->aead.key, test->aead.key_length);
	if (err) {
		goto out;
	}

	authsize = test->aead.tag_length;
	err = crypto_aead_setauthsize(tfm, authsize);
	if (err) {
		pr_err("niap_test_aead: Failed to set authsize to %u for %s\n",
		       authsize, test->alg);
		goto out;
	}

	k = !!test->aead.assoc_length;
	sg_init_table(sg, k + 1);
	sg_set_buf(&sg[0], test->aead.assoc, test->aead.assoc_length);

	in_out_size = max(test->aead.in_length, test->aead.out_length);
	input_output = kzalloc(in_out_size, GFP_KERNEL);
	if (!input_output) {
		err = -ENOMEM;
		goto out;
	}
	memcpy(input_output, test->aead.input, test->aead.in_length);
	sg_set_buf(&sg[k], input_output, in_out_size);

	aead_request_set_crypt(req, sg, sg, test->aead.in_length,
			       test->aead.iv);
	aead_request_set_ad(req, test->aead.assoc_length);

	err = test->encrypt ? crypto_aead_encrypt(req)
			    : crypto_aead_decrypt(req);

	switch (err) {
	case 0:
		break;
	case -EINPROGRESS:
	case -EBUSY:
		wait_for_completion(&result.completion);
		reinit_completion(&result.completion);
		err = result.err;
		if (err)
			goto out;
		break;
	case -EBADMSG:
		pr_err("niap_test_aead: -EBADMSG\n");
		/* fall through */
	default:
		goto out;
		break;
	}

	if (memcmp(input_output, test->aead.output, test->aead.out_length))
		err = -EINVAL;

out:
	if (err)
		pr_err("niap_test_aead: %s failed: %d\n", test->alg, err);
	else
		pr_info("niap_test_aead: %s %s passed\n", test->alg,
			test->encrypt ? "encryption" : "decryption");

	kfree(sg);
	aead_request_free(req);
	crypto_free_aead(tfm);
	kfree(input_output);
	return err;
}

static int __init niap_test_drbg(const struct niap_test *test)
{
	int err;
	struct crypto_rng *drng;
	u8 *output = NULL;
	struct drbg_test_data test_data;
	struct drbg_string addtl, pers, testentropy;

	drng = crypto_alloc_rng(test->alg, 0, 0);
	if (IS_ERR(drng)) {
		pr_err("niap_test_drbg: could not allocate DRNG handle for %s\n",
		       test->alg);
		return PTR_ERR(drng);
	}

	test_data.testentropy = &testentropy;
	drbg_string_fill(&testentropy, test->drbg.entropy,
			 test->drbg.entropy_length);
	drbg_string_fill(&pers, test->drbg.pers, test->drbg.pers_length);
	err = crypto_drbg_reset_test(drng, &pers, &test_data);
	if (err) {
		pr_err("niap_test_drbg: Failed to reset rng\n");
		goto out;
	}

	output = kzalloc(test->drbg.out_length, GFP_KERNEL);
	if (!output) {
		err = -ENOMEM;
		goto out;
	}

	drbg_string_fill(&addtl, test->drbg.add_a, test->drbg.add_length);
	if (test->drbg.entpr_length) {
		drbg_string_fill(&testentropy, test->drbg.entpr_a,
				 test->drbg.entpr_length);
		err = crypto_drbg_get_bytes_addtl_test(drng, output,
						       test->drbg.out_length,
						       &addtl, &test_data);
	} else {
		err = crypto_drbg_get_bytes_addtl(
			drng, output, test->drbg.out_length, &addtl);
	}

	if (err < 0) {
		pr_err("niap_test_drbg: could not obtain random data for %s\n",
		       test->alg);
		goto out;
	}

	drbg_string_fill(&addtl, test->drbg.add_b, test->drbg.add_length);
	if (test->drbg.entpr_length) {
		drbg_string_fill(&testentropy, test->drbg.entpr_b,
				 test->drbg.entpr_length);
		err = crypto_drbg_get_bytes_addtl_test(drng, output,
						       test->drbg.out_length,
						       &addtl, &test_data);
	} else {
		err = crypto_drbg_get_bytes_addtl(
			drng, output, test->drbg.out_length, &addtl);
	}
	if (err < 0) {
		pr_err("niap_test_drbg: could not obtain random data for %s\n",
		       test->alg);
		goto out;
	}

	err = 0;
	if (memcmp(output, test->drbg.output, test->drbg.out_length)) {
		pr_err("niap_test_drbg: invalid result:\n");
		err = -EBADMSG;
	}

out:
	if (err)
		pr_err("niap_test_drbg: %s failed: %d\n", test->alg, err);
	else
		pr_info("niap_test_drbg: %s passed\n", test->alg);
	crypto_free_rng(drng);
	kfree(output);
	return err;
}

static int __init niap_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(niap_tests); i++) {
		const struct niap_test *test = &niap_tests[i];

		if (test->func(test)) {
			pr_err("NIAP: Power-on self-test failed\n");
			panic("FIPS/NIAP crypto self-tests failed\n");
		}
	}

	return 0;
}

late_initcall(niap_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NIAP FPT_TST_EXT.1 cryptographic self-tests");
