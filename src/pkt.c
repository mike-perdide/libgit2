/*
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * In addition to the permissions in the GNU General Public License,
 * the authors give you unlimited permission to link the compiled
 * version of this file into combinations with other programs,
 * and to distribute those combinations without any restriction
 * coming from the use of this file.  (The General Public License
 * restrictions do apply in other respects; for example, they cover
 * modification of the file, and distribution when not linked into
 * a combined executable.)
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "git2/pkt.h"
#include "git2/types.h"
#include "git2/errors.h"

#include "common.h"
#include "util.h"
#include "netops.h"

#include <ctype.h>

#define PKT_LEN_SIZE 4

static int flush_pkt(git_pkt **out)
{
	git_pkt *pkt;

	pkt = git__malloc(sizeof(git_pkt));
	if (pkt == NULL)
		return GIT_ENOMEM;

	pkt->type = GIT_PKT_FLUSH;
	*out = pkt;

	return GIT_SUCCESS;
}

/*
 * Parse an other-ref line.
 */
int ref_pkt(git_pkt **out, const char *line, size_t len)
{
	git_pkt_ref *pkt;
	int error, has_caps = 0;

	pkt = git__malloc(sizeof(git_pkt_ref));
	if (pkt == NULL)
		return GIT_ENOMEM;

	memset(pkt, 0x0, sizeof(git_pkt_ref));
	pkt->type = GIT_PKT_REF;
	error = git_oid_fromstr(&pkt->head.oid, line);
	if (error < GIT_SUCCESS) {
		error = git__throw(error, "Failed to parse reference ID");
		goto out;
	}

	/* Check for a bit of consistency */
	if (line[GIT_OID_HEXSZ] != ' ') {
		error = git__throw(GIT_EOBJCORRUPTED, "Failed to parse ref. No SP");
		goto out;
	}

	/* Jump from the name */
	line += GIT_OID_HEXSZ + 1;
	len -= (GIT_OID_HEXSZ + 1);

	if (strlen(line) < len)
		has_caps = 1;

	if (line[len - 1] == '\n')
		--len;

	pkt->head.name = git__malloc(len + 1);
	if (pkt->head.name == NULL) {
		error = GIT_ENOMEM;
		goto out;
	}
	memcpy(pkt->head.name, line, len);
	pkt->head.name[len] = '\0';

	if (has_caps) {
		pkt->capabilities = strchr(pkt->head.name, '\0') + 1;
	}

out:
	if (error < GIT_SUCCESS)
		free(pkt);
	else
		*out = (git_pkt *)pkt;

	return error;
}

static ssize_t parse_len(const char *line)
{
	char num[PKT_LEN_SIZE + 1];
	int i, error;
	long len;
	const char *num_end;

	memcpy(num, line, PKT_LEN_SIZE);
	num[PKT_LEN_SIZE] = '\0';

	for (i = 0; i < PKT_LEN_SIZE; ++i) {
		if (!isxdigit(num[i]))
			return GIT_ENOTNUM;
	}

	error = git__strtol32(&len, num, &num_end, 16);
	if (error < GIT_SUCCESS) {
		return error;
	}

	return (unsigned int) len;
}

/*
 * As per the documentation, the syntax is:
 *
 * pkt-line    = data-pkt / flush-pkt
 * data-pkt    = pkt-len pkt-payload
 * pkt-len     = 4*(HEXDIG)
 * pkt-payload = (pkt-len -4)*(OCTET)
 * flush-pkt   = "0000"
 *
 * Which means that the first four bytes are the length of the line,
 * in ASCII hexadecimal (including itself)
 */

int git_pkt_parse_line(git_pkt **head, const char *line, const char **out, size_t bufflen)
{
	int error = GIT_SUCCESS;
	size_t len;

	/* Not even enough for the length */
	if (bufflen > 0 && bufflen < PKT_LEN_SIZE)
		return GIT_ESHORTBUFFER;

	error = parse_len(line);
	if (error < GIT_SUCCESS) {
		return git__throw(error, "Failed to parse pkt length");
	}

	len = error;

	/*
	 * If we were given a buffer length, then make sure there is
	 * enough in the buffer to satisfy this line
	 */
	if (bufflen > 0 && bufflen < len)
		return GIT_ESHORTBUFFER;

	line += PKT_LEN_SIZE;
	/*
	 * TODO: How do we deal with empty lines? Try again? with the next
	 * line?
	 */
	if (len == PKT_LEN_SIZE) {
		*out = line;
		return GIT_SUCCESS;
	}

	if (len == 0) { /* Flush pkt */
		*out = line;
		return flush_pkt(head);
	}

	len -= PKT_LEN_SIZE; /* the encoded length includes its own size */

	/*
	 * For now, we're just going to assume we're parsing references
	 */

	error = ref_pkt(head, line, len);
	*out = line + len;

	return error;
}

void git_pkt_free(git_pkt *pkt)
{
	if(pkt->type == GIT_PKT_REF) {
		git_pkt_ref *p = (git_pkt_ref *) pkt;
		free(p->head.name);
	}

	free(pkt);
}

int git_pkt_send_flush(int s)
{
	char flush[] = "0000";

	return gitno_send(s, flush, STRLEN(flush), 0);
}
