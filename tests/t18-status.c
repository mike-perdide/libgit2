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
#include "test_lib.h"
#include "test_helpers.h"
#include "fileops.h"
#include "git2/status.h"

#define STATUS_FOLDER TEST_RESOURCES "/status"
#define TEMP_STATUS_FOLDER TEMP_FOLDER "status"

static const char *test_blob_oid = "d4fa8600b4f37d7516bef4816ae2c64dbf029e3a";

static int copy_status_repo(char *path_statusfiles, char *temp_path)
{
	char current_workdir[GIT_PATH_MAX];
	char gitted[GIT_PATH_MAX];
	int error;

	error = p_getcwd(current_workdir, sizeof(current_workdir));
	if (error < 0)
		return error;
	strcpy(path_statusfiles, current_workdir);
	git_path_join(path_statusfiles, path_statusfiles, TEMP_STATUS_FOLDER);

	error = copydir_recurs(STATUS_FOLDER, path_statusfiles);
	if (error < 0)
		return error;

	git_path_join(gitted, path_statusfiles, ".gitted");
	git_path_join(temp_path, path_statusfiles, ".git");
	copydir_recurs(gitted, temp_path);
	git_futils_rmdir_r(gitted, 1);

	return GIT_SUCCESS;
}

BEGIN_TEST(file0, "test retrieving OID from a file apart from the ODB")
	char path_statusfiles[GIT_PATH_MAX];
	char temp_path[GIT_PATH_MAX];
	git_oid expected_id, actual_id;

	must_pass(copy_status_repo(path_statusfiles, temp_path));

	git_path_join(temp_path, path_statusfiles, "new_file");

	must_pass(git_futils_exists(temp_path));

	git_oid_fromstr(&expected_id, test_blob_oid);
	must_pass(git_odb_hashfile(&actual_id, temp_path, GIT_OBJ_BLOB));

	must_be_true(git_oid_cmp(&expected_id, &actual_id) == 0);

	git_futils_rmdir_r(TEMP_STATUS_FOLDER, 1);
END_TEST

static const char *entry_paths[] = {
	"current_file",
	"file_deleted",
	"modified_file",
	"new_file",
	"staged_changes",
	"staged_changes_file_deleted",
	"staged_changes_modified_file",
	"staged_delete_file_deleted",
	"staged_delete_modified_file",
	"staged_new_file",
	"staged_new_file_deleted_file",
	"staged_new_file_modified_file",

	"subdir/current_file",
	"subdir/deleted_file",
	"subdir/modified_file",
	"subdir/new_file",
};
static const unsigned int entry_statuses[] = {
	GIT_STATUS_CURRENT,
	GIT_STATUS_WT_DELETED,
	GIT_STATUS_WT_MODIFIED,
	GIT_STATUS_WT_NEW,
	GIT_STATUS_INDEX_MODIFIED,
	GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_WT_DELETED,
	GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_WT_MODIFIED,
	GIT_STATUS_INDEX_DELETED,
	GIT_STATUS_INDEX_DELETED | GIT_STATUS_WT_NEW,
	GIT_STATUS_INDEX_NEW,
	GIT_STATUS_INDEX_NEW | GIT_STATUS_WT_DELETED,
	GIT_STATUS_INDEX_NEW | GIT_STATUS_WT_MODIFIED,

	GIT_STATUS_CURRENT,
	GIT_STATUS_WT_DELETED,
	GIT_STATUS_WT_MODIFIED,
	GIT_STATUS_WT_NEW,
};
#define ENTRY_COUNT 16

static unsigned int get_expected_entry_status(const char *path)
{
	int i;

	for (i = 0; i < ENTRY_COUNT; ++i)
		if (!strcmp(path, entry_paths[i]))
			return entry_statuses[i];

	return (unsigned int)-1;
}

struct status_entry_counts {
	int wrong_status_flags_count;
	int entry_count;
};

static int status_cb(const char *path, unsigned int status_flags, void *payload)
{
	unsigned int expected_status_flags = get_expected_entry_status(path);
	struct status_entry_counts *counts = (struct status_entry_counts *)payload;

	counts->entry_count++;
	if (status_flags != expected_status_flags)
		counts->wrong_status_flags_count++;

	return GIT_SUCCESS;
}

BEGIN_TEST(statuscb0, "test retrieving status for worktree of repository")
	char path_statusfiles[GIT_PATH_MAX];
	char temp_path[GIT_PATH_MAX];
	git_repository *repo;
	struct status_entry_counts counts;

	must_pass(copy_status_repo(path_statusfiles, temp_path));

	must_pass(git_repository_open(&repo, temp_path));

	memset(&counts, 0x0, sizeof(struct status_entry_counts));
	git_status_foreach(repo, status_cb, &counts);
	must_be_true(counts.entry_count == ENTRY_COUNT);
	must_be_true(counts.wrong_status_flags_count == 0);

	git_repository_free(repo);

	git_futils_rmdir_r(TEMP_STATUS_FOLDER, 1);
END_TEST

BEGIN_TEST(singlestatus0, "test retrieving status for single file")
	char path_statusfiles[GIT_PATH_MAX];
	char temp_path[GIT_PATH_MAX];
	git_repository *repo;
	unsigned int status_flags;
	int i;

	must_pass(copy_status_repo(path_statusfiles, temp_path));

	must_pass(git_repository_open(&repo, temp_path));

	for (i = 0; i < ENTRY_COUNT; ++i) {
		must_pass(git_status_file(&status_flags, repo, entry_paths[i]));
		must_be_true(status_flags == entry_statuses[i]);
	}

	git_repository_free(repo);

	git_futils_rmdir_r(TEMP_STATUS_FOLDER, 1);
END_TEST

BEGIN_TEST(singlestatus1, "test retrieving status for nonexistent file")
	char path_statusfiles[GIT_PATH_MAX];
	char temp_path[GIT_PATH_MAX];
	git_repository *repo;
	unsigned int status_flags;
	int error;

	must_pass(copy_status_repo(path_statusfiles, temp_path));

	must_pass(git_repository_open(&repo, temp_path));

	// "nonexistent" does not exist in HEAD, Index or the worktree
	error = git_status_file(&status_flags, repo, "nonexistent");
	must_be_true(error == GIT_ENOTFOUND);

	git_repository_free(repo);

	git_futils_rmdir_r(TEMP_STATUS_FOLDER, 1);
END_TEST

BEGIN_SUITE(status)
	ADD_TEST(file0);
	ADD_TEST(statuscb0);
	ADD_TEST(singlestatus0);
	ADD_TEST(singlestatus1);
END_SUITE

