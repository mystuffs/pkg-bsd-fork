/*-
 * Copyright (c) 2021 Kyle Evans <kevans@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _PKGSIGN_H
#define _PKGSIGN_H

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <pkg.h>

struct pkgsign_ctx;
struct pkgsign_ops;
struct pkgsign_impl;
struct iovec;

/*
 * This should be embedded at the beginning of your pkgsign implementation's
 * context as needed.
 */
struct pkgsign_ctx {
	struct pkgsign_impl		*impl;
	pkg_password_cb			*pw_cb;
	const char			*path;
};

/* pkgsign request initialization/finalization. */
typedef int pkgsign_new_cb(const char *, struct pkgsign_ctx *);
typedef void pkgsign_free_cb(struct pkgsign_ctx *);

/* Sign (pkg_checksum), pass back signature and signature length. */
typedef int pkgsign_sign_cb(struct pkgsign_ctx *, const char *,
    unsigned char **, size_t *);

/* Validate the checksum against the expected signature. */
typedef int pkgsign_verify_cb(const struct pkgsign_ctx *, const char *,
    unsigned char *, size_t, int);

/*
 * Validate the checksum against the fingerprint's expected signature.  This
 * differs from the above for historical reasons, so it is both acceptable and
 * expected for them to be one and the same implementation.
 *
 * The longer explanation is that pkg historically signed the ShA256 hash of
 * a repo's contents as if it were SHA1, rather than SHA256.  This is largely
 * irrelevant, except that it's not interoperable with other implementations
 * that want to reproduce pkg's results because the hash function is physically
 * embedded in the resulting signature.
 */
typedef int pkgsign_verify_cert_cb(const struct pkgsign_ctx *, unsigned char *,
    size_t, unsigned char *, size_t, int);

/* Generate a signing key. */
typedef int pkgsign_generate_cb(struct pkgsign_ctx *, const struct iovec *,
    int);

/* Return information about a signing key. */
typedef int pkgsign_keyinfo_cb(struct pkgsign_ctx *, struct iovec **,
    int *);

/* Sign arbitrary data. */
typedef int pkgsign_sign_data_cb(struct pkgsign_ctx *, const unsigned char *,
    size_t, unsigned char **, size_t *);

/* Return the public key. */
typedef int pkgsign_pubkey_cb(struct pkgsign_ctx *, char **, size_t *);

struct pkgsign_ops {
	/*
	 * pkgsign_ctx_size <= sizeof(pkgsign_ctx) is wrong, but
	 * pkgsign_ctx_size == 0 will allocate just a pkgsign_ctx.
	 */
	size_t				 pkgsign_ctx_size;

	/* Optional request initialization/finalization handlers. */
	pkgsign_new_cb			*pkgsign_new;
	pkgsign_free_cb			*pkgsign_free;

	/* Optional key generation/information handlers. */
	pkgsign_generate_cb		*pkgsign_generate;
	pkgsign_keyinfo_cb		*pkgsign_keyinfo;
	pkgsign_pubkey_cb		*pkgsign_pubkey;
	pkgsign_sign_data_cb	*pkgsign_sign_data;

	/* Non-optional. */
	pkgsign_sign_cb			*pkgsign_sign;

	/* Non-optional, and may be the same function. */
	pkgsign_verify_cb		*pkgsign_verify;
	pkgsign_verify_cert_cb		*pkgsign_verify_cert;
};

int pkgsign_new_sign(const char *, struct pkgsign_ctx **);
int pkgsign_new_verify(const char *, const struct pkgsign_ctx **);
void pkgsign_set(struct pkgsign_ctx *, pkg_password_cb *, const char *);
void pkgsign_free(struct pkgsign_ctx *);

int pkgsign_sign(struct pkgsign_ctx *, const char *, unsigned char **, size_t *);
int pkgsign_verify(const struct pkgsign_ctx *, const char *, unsigned char *,
    size_t, int);
int pkgsign_verify_cert(const struct pkgsign_ctx *, unsigned char *, size_t,
    unsigned char *, size_t, int);

int pkgsign_generate(struct pkgsign_ctx *, const struct iovec *, int);
int pkgsign_keyinfo(struct pkgsign_ctx *, struct iovec **, int *);
int pkgsign_pubkey(struct pkgsign_ctx *, char **, size_t *);
int pkgsign_sign_data(struct pkgsign_ctx *, const unsigned char *,
    size_t, unsigned char **, size_t *);

const char *pkgsign_impl_name(const struct pkgsign_ctx *);

/* Newer signature types will encode a $PKGSIGN:<signer_type>$ */
#define	PKGSIGN_HEAD	"$PKGSIGN:"

#endif
