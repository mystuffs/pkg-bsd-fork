/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2023 Serenity Cyber Security, LLC
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * All rights reserved.
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

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <libgen.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/pkgdb.h"
#include "private/pkg.h"
#include "binary.h"

extern struct pkg_ctx ctx;

int
pkg_repo_binary_get_cached_name(struct pkg_repo *repo, struct pkg *pkg,
	char *dest, size_t destlen)
{
	const char *ext = NULL;
	const char *packagesite;
	struct stat st;

	packagesite = pkg_repo_url(repo);

	if (strncmp(packagesite, "file:/", 6) == 0) {
		snprintf(dest, destlen, "%s/%s", packagesite + 6,
		    pkg->repopath);
		return (EPKG_OK);
	}

	if (pkg->repopath != NULL)
		ext = strrchr(pkg->repopath, '.');

	if (ext != NULL) {
		/*
		 * The real naming scheme:
		 * <cachedir>/<name>-<version>-<checksum>.txz
		 */
		pkg_snprintf(dest, destlen, "%S/%n-%v%S%z%S",
		    ctx.cachedir, pkg, pkg, PKG_HASH_SEPSTR, pkg, ext);
		if (stat (dest, &st) == -1 || pkg->pkgsize != st.st_size)
			return (EPKG_FATAL);

	}
	else {
		pkg_snprintf(dest, destlen, "%S/%n-%v%S%z", ctx.cachedir, pkg,
		    pkg, PKG_HASH_SEPSTR, pkg);
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_create_symlink(struct pkg *pkg, const char *fname,
	const char *dir)
{
	const char *ext, *dest_fname;
	char link_dest_tmp[MAXPATHLEN], link_dest[MAXPATHLEN];

	/* Create symlink from full pkgname */
	ext = strrchr(fname, '.');
	pkg_snprintf(link_dest, sizeof(link_dest), "%S/%n-%v%S",
		dir, pkg, pkg, ext ? ext : "");
	snprintf(link_dest_tmp, sizeof(link_dest_tmp), "%s.new", link_dest);

	/* Ignore errors here */
	(void)unlink(link_dest_tmp);

	/* Trim the path to just the filename. */
	if ((dest_fname = strrchr(fname, '/')) != NULL)
		++dest_fname;
	if (symlink(dest_fname, link_dest_tmp) == -1) {
		pkg_emit_errno("symlink", link_dest);
		return (EPKG_FATAL);
	}

	if (rename(link_dest_tmp, link_dest) == -1) {
		pkg_emit_errno("rename", link_dest);
		unlink(link_dest_tmp);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
pkg_repo_binary_try_fetch(struct pkg_repo *repo, struct pkg *pkg,
	bool already_tried, bool mirror, const char *destdir)
{
	char dest[MAXPATHLEN];
	char url[MAXPATHLEN];
	char *dir = NULL;
	bool fetched = false;
	struct stat st;
	const char *packagesite = NULL;
	ssize_t offset = -1;
	int retval;

	int retcode = EPKG_OK;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE);

	if (mirror) {
		const char *cachedir;

		if (destdir != NULL)
			cachedir = destdir;
		else
			cachedir = ctx.cachedir;

		snprintf(dest, sizeof(dest), "%s/%s", cachedir, pkg->repopath);
	}
	else
		pkg_repo_binary_get_cached_name(repo, pkg, dest, sizeof(dest));

	/* If it is already in the local cachedir, dont bother to
	 * download it */
	if (stat(dest, &st) == 0) {
		/* try to resume */
		if (pkg->pkgsize > st.st_size) {
			offset = st.st_size;
			pkg_debug(1, "Resuming fetch");
		} else {
			goto checksum;
		}
	}

	/* Create the dirs in cachedir */
	dir = get_dirname(xstrdup(dest));
	if ((retcode = pkg_mkdirs(dir)) != EPKG_OK)
		goto cleanup;

	/*
	 * In multi-repos the remote URL is stored in pkg[PKG_REPOURL]
	 * For a single attached database the repository URL should be
	 * defined by URL.
	 */
	packagesite = pkg_repo_url(repo);

	if (packagesite == NULL || packagesite[0] == '\0') {
		pkg_emit_error("URL is not defined");
		retcode = 1;
		goto cleanup;
	}

	if (packagesite[strlen(packagesite) - 1] == '/')
		pkg_snprintf(url, sizeof(url), "%S%R", packagesite, pkg);
	else
		pkg_snprintf(url, sizeof(url), "%S/%R", packagesite, pkg);

	if (!mirror && strncasecmp(url, "file://", 7) == 0) {
		free(dir);
		if (access(url + strlen("file://"), F_OK) == 0) {
			return (EPKG_OK);
		}
		pkg_emit_error("cached package %s-%s: "
			       "%s is missing from repo\n",
			       pkg->name, pkg->version, url);
		return EPKG_FATAL;
	}

	retcode = pkg_fetch_file(repo, pkg->repopath, dest, 0, offset, pkg->pkgsize);

	if (offset == -1)
		fetched = true;

	if (retcode != EPKG_OK)
		goto cleanup;

checksum:
	/*	checksum calculation is expensive, if size does not
		match, skip it and assume failed checksum. */
	if (stat(dest, &st) == -1 || pkg->pkgsize != st.st_size) {
		if (already_tried) {
			pkg_emit_error("cached package %s-%s: "
			    "missing or size mismatch, cannot continue\n"
			    "Consider running 'pkg update -f'",
			    pkg->name, pkg->version);
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		unlink(dest);
		free(dir);
		pkg_emit_error("cached package %s-%s: "
		    "missing or size mismatch, fetching from remote",
		    pkg->name, pkg->version);
		return (pkg_repo_binary_try_fetch(repo, pkg, true, mirror, destdir));
	}
	retval = pkg_checksum_validate_file(dest, pkg->sum);
	if (retval == ENOENT) {
			pkg_emit_error("%s-%s missing "
			    "from repository", pkg->name, pkg->version);
			return EPKG_FATAL;
	}
	if (retval != 0) {
		if (already_tried || fetched) {
			pkg_emit_error("%s-%s failed checksum "
			    "from repository", pkg->name, pkg->version);
			retcode = EPKG_FATAL;
		} else {
			pkg_emit_error("cached package %s-%s: "
			    "checksum mismatch, fetching from remote",
			    pkg->name, pkg->version);
			unlink(dest);
			return (pkg_repo_binary_try_fetch(repo, pkg, true, mirror, destdir));
		}
	}

cleanup:

	if (retcode != EPKG_OK)
		unlink(dest);
	else if (!mirror && dir != NULL) {
		(void)pkg_repo_binary_create_symlink(pkg, dest, dir);
	}

	/* allowed even if dir is NULL */
	free(dir);

	return (retcode);
}

int
pkg_repo_binary_fetch(struct pkg_repo *repo, struct pkg *pkg)
{
	return (pkg_repo_binary_try_fetch(repo, pkg, false, false, NULL));
}

int
pkg_repo_binary_mirror(struct pkg_repo *repo, struct pkg *pkg,
	const char *destdir)
{
	return (pkg_repo_binary_try_fetch(repo, pkg, false, true, destdir));
}
