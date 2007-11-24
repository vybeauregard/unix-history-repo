/*-
 * Copyright (c) 2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#else
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>
#include <openssl/evp.h>
#define	_OpenSSL_
#endif
#include <geom/eli/g_eli.h>

#ifdef _KERNEL
MALLOC_DECLARE(M_ELI);

static int
g_eli_crypto_done(struct cryptop *crp)
{

	crp->crp_opaque = (void *)crp;
	wakeup(crp);
	return (0);
}

static int
g_eli_crypto_cipher(u_int algo, int enc, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{
	struct cryptoini cri;
	struct cryptop *crp;
	struct cryptodesc *crd;
	struct uio *uio;
	struct iovec *iov;
	uint64_t sid;
	u_char *p;
	int error;

	bzero(&cri, sizeof(cri));
	cri.cri_alg = algo;
	cri.cri_key = __DECONST(void *, key);
	cri.cri_klen = keysize;
	error = crypto_newsession(&sid, &cri, 0);
	if (error != 0)
		return (error);
	p = malloc(sizeof(*crp) + sizeof(*crd) + sizeof(*uio) + sizeof(*iov),
	    M_ELI, M_NOWAIT | M_ZERO);
	if (p == NULL) {
		crypto_freesession(sid);
		return (ENOMEM);
	}
	crp = (struct cryptop *)p;	p += sizeof(*crp);
	crd = (struct cryptodesc *)p;	p += sizeof(*crd);
	uio = (struct uio *)p;		p += sizeof(*uio);
	iov = (struct iovec *)p;	p += sizeof(*iov);

	iov->iov_len = datasize;
	iov->iov_base = data;

	uio->uio_iov = iov;
	uio->uio_iovcnt = 1;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_resid = datasize;

	crd->crd_skip = 0;
	crd->crd_len = datasize;
	crd->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
	if (enc)
		crd->crd_flags |= CRD_F_ENCRYPT;
	crd->crd_alg = algo;
	crd->crd_key = __DECONST(void *, key);
	crd->crd_klen = keysize;
	bzero(crd->crd_iv, sizeof(crd->crd_iv));
	crd->crd_next = NULL;

	crp->crp_sid = sid;
	crp->crp_ilen = datasize;
	crp->crp_olen = datasize;
	crp->crp_opaque = NULL;
	crp->crp_callback = g_eli_crypto_done;
	crp->crp_buf = (void *)uio;
	crp->crp_flags = CRYPTO_F_IOV | CRYPTO_F_CBIFSYNC | CRYPTO_F_REL;
	crp->crp_desc = crd;

	error = crypto_dispatch(crp);
	if (error == 0) {
		while (crp->crp_opaque == NULL)
			tsleep(crp, PRIBIO, "geli", hz / 5);
		error = crp->crp_etype;
	}

	free(crp, M_ELI);
	crypto_freesession(sid);
	return (error);
}
#else	/* !_KERNEL */
static int
g_eli_crypto_cipher(u_int algo, int enc, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{
	EVP_CIPHER_CTX ctx;
	const EVP_CIPHER *type;
	u_char iv[keysize];
	int outsize;

	switch (algo) {
	case CRYPTO_NULL_CBC:
		type = EVP_enc_null();
		break;
	case CRYPTO_AES_CBC:
		switch (keysize) {
		case 128:
			type = EVP_aes_128_cbc();
			break;
		case 192:
			type = EVP_aes_192_cbc();
			break;
		case 256:
			type = EVP_aes_256_cbc();
			break;
		default:
			return (EINVAL);
		}
		break;
	case CRYPTO_BLF_CBC:
		type = EVP_bf_cbc();
		break;
	case CRYPTO_3DES_CBC:
		type = EVP_des_ede3_cbc();
		break;
	default:
		return (EINVAL);
	}

	EVP_CIPHER_CTX_init(&ctx);

	EVP_CipherInit_ex(&ctx, type, NULL, NULL, NULL, enc);
	EVP_CIPHER_CTX_set_key_length(&ctx, keysize / 8);
	EVP_CIPHER_CTX_set_padding(&ctx, 0);
	bzero(iv, sizeof(iv));
	EVP_CipherInit_ex(&ctx, NULL, NULL, key, iv, enc);

	if (EVP_CipherUpdate(&ctx, data, &outsize, data, datasize) == 0) {
		EVP_CIPHER_CTX_cleanup(&ctx);
		return (EINVAL);
	}
	assert(outsize == (int)datasize);

	if (EVP_CipherFinal_ex(&ctx, data + outsize, &outsize) == 0) {
		EVP_CIPHER_CTX_cleanup(&ctx);
		return (EINVAL);
	}
	assert(outsize == 0);

	EVP_CIPHER_CTX_cleanup(&ctx);
	return (0);
}
#endif	/* !_KERNEL */

int
g_eli_crypto_encrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{

	return (g_eli_crypto_cipher(algo, 1, data, datasize, key, keysize));
}

int
g_eli_crypto_decrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize)
{

	return (g_eli_crypto_cipher(algo, 0, data, datasize, key, keysize));
}

void
g_eli_crypto_hmac_init(struct hmac_ctx *ctx, const uint8_t *hkey,
    size_t hkeylen)
{
	u_char k_ipad[128], key[128];
	SHA512_CTX lctx;
	u_int i;

	bzero(key, sizeof(key));
	if (hkeylen == 0)
		; /* do nothing */
	else if (hkeylen <= 128)
		bcopy(hkey, key, hkeylen);
	else {
		/* If key is longer than 128 bytes reset it to key = SHA512(key). */
		SHA512_Init(&lctx);
		SHA512_Update(&lctx, hkey, hkeylen);
		SHA512_Final(key, &lctx);
	}

	/* XOR key with ipad and opad values. */
	for (i = 0; i < sizeof(key); i++) {
		k_ipad[i] = key[i] ^ 0x36;
		ctx->k_opad[i] = key[i] ^ 0x5c;
	}
	bzero(key, sizeof(key));
	/* Perform inner SHA512. */
	SHA512_Init(&ctx->shactx);
	SHA512_Update(&ctx->shactx, k_ipad, sizeof(k_ipad));
}

void
g_eli_crypto_hmac_update(struct hmac_ctx *ctx, const uint8_t *data,
    size_t datasize)
{

	SHA512_Update(&ctx->shactx, data, datasize);
}

void
g_eli_crypto_hmac_final(struct hmac_ctx *ctx, uint8_t *md, size_t mdsize)
{
	u_char digest[SHA512_MDLEN];
	SHA512_CTX lctx;

	SHA512_Final(digest, &ctx->shactx);
	/* Perform outer SHA512. */
	SHA512_Init(&lctx);
	SHA512_Update(&lctx, ctx->k_opad, sizeof(ctx->k_opad));
	bzero(ctx, sizeof(*ctx));
	SHA512_Update(&lctx, digest, sizeof(digest));
	SHA512_Final(digest, &lctx);
	/* mdsize == 0 means "Give me the whole hash!" */
	if (mdsize == 0)
		mdsize = SHA512_MDLEN;
	bcopy(digest, md, mdsize);
}

void
g_eli_crypto_hmac(const uint8_t *hkey, size_t hkeysize, const uint8_t *data,
    size_t datasize, uint8_t *md, size_t mdsize)
{
	struct hmac_ctx ctx;

	g_eli_crypto_hmac_init(&ctx, hkey, hkeysize);
	g_eli_crypto_hmac_update(&ctx, data, datasize);
	g_eli_crypto_hmac_final(&ctx, md, mdsize);
}
