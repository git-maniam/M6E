/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "storage_test_harness.h"
#include "nsIFile.h"
#include "prio.h"

/**
 * This file tests that the file permissions of the sqlite files match what
 * we request they be
 */

TEST(storage_file_perms, Test)
{
  nsCOMPtr<mozIStorageConnection> db(getDatabase());
  nsCOMPtr<nsIFile> dbFile;
  do_check_success(db->GetDatabaseFile(getter_AddRefs(dbFile)));

  uint32_t perms = 0;
  do_check_success(dbFile->GetPermissions(&perms));

  // This reflexts the permissions defined by SQLITE_DEFAULT_FILE_PERMISSIONS in
  // third_party/sqlite3/src/Makefile.in and must be kept in sync with that
  do_check_true(perms == (PR_IRUSR | PR_IWUSR | PR_IRGRP | PR_IROTH));
}
