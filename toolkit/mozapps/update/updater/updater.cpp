/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 *  Manifest Format
 *  ---------------
 *
 *  contents = 1*( line )
 *  line     = method LWS *( param LWS ) CRLF
 *  CRLF     = "\r\n"
 *  LWS      = 1*( " " | "\t" )
 *
 *  Available methods for the manifest file:
 *
 *  updatev3.manifest
 *  -----------------
 *  method   = "add" | "add-if" | "add-if-not" | "patch" | "patch-if" |
 *             "remove" | "rmdir" | "rmrfdir" | type
 *
 *  'add-if-not' adds a file if it doesn't exist.
 *
 *  'type' is the update type (e.g. complete or partial) and when present MUST
 *  be the first entry in the update manifest. The type is used to support
 *  removing files that no longer exist when when applying a complete update by
 *  causing the actions defined in the precomplete file to be performed.
 *
 *  precomplete
 *  -----------
 *  method   = "remove" | "rmdir"
 */

#if defined(MOZ_BSPATCH)
#  include "bspatch.h"
#  include "crctable.h"
#endif  // defined(MOZ_BSPATCH)

#if defined(MOZ_ZUCCHINI)
#  include "mozilla/moz_zucchini.h"
#endif  // defined(MOZ_ZUCCHINI)

#if !defined(MOZ_BSPATCH) && !defined(MOZ_ZUCCHINI)
#  error Updater enabled, but all supported patch formats are turned off.
#endif  // !defined(MOZ_BSPATCH) && !defined(MOZ_ZUCCHINI)

#include "progressui.h"
#include "archivereader.h"
#include "readstrings.h"
#include "updatererrors.h"
#include "elevation_type.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <chrono>
#include <memory>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include "updatecommon.h"
#  include "UpdateSettingsUtil.h"
#  include "updaterfileutils_osx.h"
#  include <sys/xattr.h>

#include "mozilla/CmdLineAndEnvUtils.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"

// Amount of the progress bar to use in each of the 4 update phases,
// should total 100.0. Estimates are for non-staged updates, because
// only they show the progress UI.
#define PROGRESS_PREPARE_SIZE 20.0f
#define PROGRESS_DRAFT_SIZE 65.0f
#define PROGRESS_EXECUTE_SIZE 10.0f
#define PROGRESS_FINISH_SIZE 5.0f

// Maximum amount of time in ms to wait for the parent process to close. The 30
// seconds is rather long but there have been bug reports where the parent
// process has exited after 10 seconds and it is better to give it a chance.
#define PARENT_WAIT 30000

// These functions are defined in launchchild_osx.mm
void CleanupElevatedMacUpdate(bool aFailureOccurred);
bool IsOwnedByGroupAdmin(const char* aAppBundle);
bool IsRecursivelyWritable(const char* aPath);
// Pass a valid pid as aWaitForPid to wait for that process to go away before
// the app is launched.
void LaunchMacApp(int argc, const char** argv, pid_t aWaitForPid);
void LaunchMacPostProcess(const char* aAppBundle);
bool ObtainUpdaterArguments(int* aArgc, char*** aArgv,
                            MARChannelStringTable* aMARStrings);
bool ServeElevatedUpdate(int aArgc, const char** aArgv,
                         const char* aMARChannelID);
void SetGroupOwnershipAndPermissions(const char* aAppBundle);
bool PerformInstallationFromDMG(int argc, char** argv);
struct UpdateServerThreadArgs {
  int argc;
  const NS_tchar** argv;
  const char* marChannelID = "";
};

#ifndef _O_BINARY
#  define _O_BINARY 0
#endif

#ifndef NULL
#  define NULL (0)
#endif

#ifndef SSIZE_MAX
#  define SSIZE_MAX LONG_MAX
#endif

// We want to use execv to invoke the callback executable on platforms where
// we were launched using execv.  See nsUpdateDriver.cpp.


#if defined(MOZ_VERIFY_MAR_SIGNATURE) && defined(MAR_NSS)
#  include "nss.h"
#  include "prerror.h"
#endif


//-----------------------------------------------------------------------------

/**
 * This enum is used to indicate on Windows why the post-updater is running.
 *
 * If the updater elevates, the post-updater runs twice: once as the system
 * user, and once as the current user. There's some logic that we don't want to
 * do in the second case; for an example, see bug 2004959.
 *
 * Notice that the CurrentUser case only runs in installations that elevate
 * (via UAC or the Maintenance Service) to perform the update; therefore,
 * nothing should run _only_ when the postupdate target is 'CurrentUser'.
 * Ideally it would do nothing, see bug 2030813.
 */
enum class PostUpdateTarget {
  // The post-updater is being run for the entire installation.
  Installation,
  // The post-updater has just finished the elevated update and is running as
  // the user who prompted the update.
  CurrentUser,
};

static ElevationType sElevationType = ElevationType::Unknown;

//-----------------------------------------------------------------------------

// A simple stack based container for a FILE struct that closes the
// file descriptor from its destructor.
class AutoFile {
 public:
  explicit AutoFile(FILE* file = nullptr) : mFile(file) {}

  ~AutoFile() { close(); }

  AutoFile& operator=(FILE* file) {
    close();
    mFile = file;
    return *this;
  }

  operator FILE*() { return mFile; }

  FILE* get() { return mFile; }

 private:
  FILE* mFile;

  void close() {
    if (mFile != nullptr) {
      int rv = fclose(mFile);
      if (rv != 0) {
        LOG(("File close did not execute successfully"));
      }
      mFile = nullptr;
    }
  }
};

//-----------------------------------------------------------------------------


// Just a simple class that sets a umask value in its constructor and resets
// it in its destructor.
class UmaskContext {
 public:
  explicit UmaskContext(mode_t umaskToSet);
  ~UmaskContext();

 private:
  mode_t mPreviousUmask;
};

UmaskContext::UmaskContext(mode_t umaskToSet) {
  mPreviousUmask = umask(umaskToSet);
}

UmaskContext::~UmaskContext() { umask(mPreviousUmask); }


//-----------------------------------------------------------------------------

typedef void (*ThreadFunc)(void* param);

#  include <pthread.h>

class Thread {
 public:
  int Run(ThreadFunc func, void* param) {
    return pthread_create(&thr, nullptr, (void* (*)(void*))func, param);
  }
  int Join() {
    void* result;
    return pthread_join(thr, &result);
  }

 private:
  pthread_t thr;
};


//-----------------------------------------------------------------------------

static NS_tchar gPatchDirPath[MAXPATHLEN];
static NS_tchar gInstallDirPath[MAXPATHLEN];
static NS_tchar gWorkingDirPath[MAXPATHLEN];
constinit static ArchiveReader gArchiveReader;
static bool gSucceeded = false;
static bool sStagedUpdate = false;
static bool sReplaceRequest = false;
// When the updater needs to elevate, we generally run the updater again with
// elevation. These two invocations differ in many important ways. The elevated
// updater doesn't touch any files that don't require that elevation, it
// basically just changes the installation directory, runs PostUpdate
// (on Windows), and exits to let the first updater invocation finalize the
// update (write its own logs, conditionally move the elevated updater
// logs/status to the update directory, call the callback application, etc).
static UpdaterInvocation gInvocation = UpdaterInvocation::Unknown;

// `argv` indices for standard invocation. Does not apply to other methods of
// invocation including when `--openAppBundle`, or `-dmgInstall` are used.
// Note that `argv[1]` is the argument version, which is needed by the MMS, but
// not by the updater since it is guaranteed to be the same version as the
// application launching it.
static const int kPatchDirIndex = 2;
static const int kInstallDirIndex = 3;
static const int kApplyToDirIndex = 4;
static const int kWhichInvocationIndex = 5;
// Note that this is the first optional argument.
static const int kWaitPidIndex = 6;
static const int kCallbackWorkingDirIndex = 7;
// This indicates the entry in `argv` that is the callback binary path. All
// arguments after this one are treated as arguments to the callback.
static const int kCallbackIndex = 8;

// The pid of the process that invoked us, when it asked us to wait for it. We
// hand this to the callback launch so that the relaunched application does not
// overlap with the process it replaces. See LaunchMacApp().
static pid_t gCallbackWaitPid = 0;

// This string contains the MAR channel IDs that are later extracted by one of
// the `ReadMARChannelIDsFrom` variants.
constinit static MARChannelStringTable gMARStrings;

// Normally, we run updates as a result of user action (the user started Firefox
// or clicked a "Restart to Update" button). But there are some cases when
// we are not:
// a) The callback app is a background task. If true then the updater is
//    likely being run as part of a background task.
//    The updater could be run with no callback, but this only happens
//    when performing a staged update (see calls to ProcessUpdates), and there
//    are already checks for sStagedUpdate when showing UI or elevating.
// b) The environment variable MOZ_APP_SILENT_START is set and not empty. This
//    is set, for instance, on macOS when Firefox had no windows open for a
//    while and restarted to apply updates.
//
// In these cases, the update should be installed silently, so we shouldn't:
// a) show progress UI
// b) prompt for elevation
static bool sUpdateSilently = false;


static const NS_tchar kWhitespace[] = NS_T(" \t");
static const NS_tchar kNL[] = NS_T("\r\n");
static const NS_tchar kQuote[] = NS_T("\"");

static inline size_t mmin(size_t a, size_t b) { return (a > b) ? b : a; }

static NS_tchar* mstrtok(const NS_tchar* delims, NS_tchar** str) {
  if (!*str || !**str) {
    *str = nullptr;
    return nullptr;
  }

  // skip leading "whitespace"
  NS_tchar* ret = *str;
  const NS_tchar* d;
  do {
    for (d = delims; *d != NS_T('\0'); ++d) {
      if (*ret == *d) {
        ++ret;
        break;
      }
    }
  } while (*d);

  if (!*ret) {
    *str = ret;
    return nullptr;
  }

  NS_tchar* i = ret;
  do {
    for (d = delims; *d != NS_T('\0'); ++d) {
      if (*i == *d) {
        *i = NS_T('\0');
        *str = ++i;
        return ret;
      }
    }
    ++i;
  } while (*i);

  *str = nullptr;
  return ret;
}

static const NS_tchar* UpdateLogFilename() {
  if (gInvocation == UpdaterInvocation::Second) {
    return NS_T("update-elevated.log");
  }
  return NS_T("update.log");
}


/**
 * Calls LogFinish for the update log. On Windows, the unelevated updater copies
 * the update status file and the update log file that were written by the
 * elevated updater from the secure directory to the update patch directory.
 *
 * NOTE: All calls to WriteStatusFile MUST happen before calling output_finish
 *       because this function copies the update status file for the elevated
 *       updater and writing the status file after calling output_finish will
 *       overwrite it.
 */
static void output_finish() {
  LogFinish();
}

/**
 * Coverts a relative update path to a full path.
 *
 * @param  relpath
 *         The relative path to convert to a full path.
 * @return valid filesystem full path or nullptr if memory allocation fails.
 */
static NS_tchar* get_full_path(const NS_tchar* relpath) {
  NS_tchar* destpath = sStagedUpdate ? gWorkingDirPath : gInstallDirPath;
  size_t lendestpath = NS_tstrlen(destpath);
  size_t lenrelpath = NS_tstrlen(relpath);
  NS_tchar* s = new NS_tchar[lendestpath + lenrelpath + 2];

  NS_tchar* c = s;

  NS_tstrcpy(c, destpath);
  c += lendestpath;
  NS_tstrcat(c, NS_T("/"));
  c++;

  NS_tstrcat(c, relpath);
  c += lenrelpath;
  *c = NS_T('\0');
  return s;
}

/**
 * Converts a full update path into a relative path; reverses get_full_path.
 *
 * @param  fullpath
 *         The absolute path to convert into a relative path.
 * return pointer to the location within fullpath where the relative path starts
 *        or fullpath itself if it already looks relative.
 */
static const NS_tchar* get_relative_path(const NS_tchar* fullpath) {
  if (fullpath[0] != '/') {
    return fullpath;
  }

  NS_tchar* prefix = sStagedUpdate ? gWorkingDirPath : gInstallDirPath;

  // If the path isn't long enough to be absolute, return it as-is.
  if (NS_tstrlen(fullpath) <= NS_tstrlen(prefix)) {
    return fullpath;
  }

  return fullpath + NS_tstrlen(prefix) + 1;
}

/**
 * Gets the platform specific path and performs simple checks to the path. If
 * the path checks don't pass nullptr will be returned.
 *
 * @param  line
 *         The line from the manifest that contains the path.
 * @param  isdir
 *         Whether the path is a directory path. Defaults to false.
 * @return valid filesystem path or nullptr if the path checks fail.
 */
static NS_tchar* get_valid_path(NS_tchar** line, bool isdir = false) {
  NS_tchar* path = mstrtok(kQuote, line);
  if (!path) {
    LOG(("get_valid_path: unable to determine path: " LOG_S, *line));
    return nullptr;
  }

  // All paths must be relative from the current working directory
  if (path[0] == NS_T('/')) {
    LOG(("get_valid_path: path must be relative: " LOG_S, path));
    return nullptr;
  }


  if (isdir) {
    // Directory paths must have a trailing forward slash.
    if (path[NS_tstrlen(path) - 1] != NS_T('/')) {
      LOG(
          ("get_valid_path: directory paths must have a trailing forward "
           "slash: " LOG_S,
           path));
      return nullptr;
    }

    // Remove the trailing forward slash because stat on Windows will return
    // ENOENT if the path has a trailing slash.
    path[NS_tstrlen(path) - 1] = NS_T('\0');
  }

  // Don't allow relative paths that resolve to a parent directory.
  if (NS_tstrstr(path, NS_T("..")) != nullptr) {
    LOG(("get_valid_path: paths must not contain '..': " LOG_S, path));
    return nullptr;
  }

  return path;
}

/*
 * Gets a quoted path. The return value is malloc'd and it is the responsibility
 * of the caller to free it.
 *
 * @param  path
 *         The path to quote.
 * @return On success the quoted path and nullptr otherwise.
 */
static NS_tchar* get_quoted_path(const NS_tchar* path) {
  size_t lenQuote = NS_tstrlen(kQuote);
  size_t lenPath = NS_tstrlen(path);
  size_t len = lenQuote + lenPath + lenQuote + 1;

  NS_tchar* s = (NS_tchar*)malloc(len * sizeof(NS_tchar));
  if (!s) {
    return nullptr;
  }

  NS_tchar* c = s;
  NS_tstrcpy(c, kQuote);
  c += lenQuote;
  NS_tstrcat(c, path);
  c += lenPath;
  NS_tstrcat(c, kQuote);
  c += lenQuote;
  *c = NS_T('\0');
  return s;
}

static void ensure_write_permissions(const NS_tchar* path) {
  struct stat fs;
  if (!stat(path, &fs) && !(fs.st_mode & S_IWUSR)) {
    (void)chmod(path, fs.st_mode | S_IWUSR);
  }
}

static int ensure_remove(const NS_tchar* path) {
  ensure_write_permissions(path);
  int rv = NS_tremove(path);
  if (rv) {
    LOG(("ensure_remove: failed to remove file: " LOG_S ", rv: %d, err: %d",
         path, rv, errno));
  }
  return rv;
}

// Remove the directory pointed to by path and all of its files and
// sub-directories.
static int ensure_remove_recursive(const NS_tchar* path,
                                   bool continueEnumOnFailure = false) {
  // We use lstat rather than stat here so that we can successfully remove
  // symlinks.
  struct NS_tstat_t sInfo;
  int rv = NS_tlstat(path, &sInfo);
  if (rv) {
    // This error is benign
    return rv;
  }
  if (!S_ISDIR(sInfo.st_mode)) {
    return ensure_remove(path);
  }

  NS_tDIR* dir;
  NS_tdirent* entry;

  dir = NS_topendir(path);
  if (!dir) {
    LOG(("ensure_remove_recursive: unable to open directory: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return rv;
  }

  while ((entry = NS_treaddir(dir)) != nullptr) {
    if (NS_tstrcmp(entry->d_name, NS_T(".")) &&
        NS_tstrcmp(entry->d_name, NS_T(".."))) {
      NS_tchar childPath[MAXPATHLEN];
      NS_tsnprintf(childPath, sizeof(childPath) / sizeof(childPath[0]),
                   NS_T("%s/%s"), path, entry->d_name);
      rv = ensure_remove_recursive(childPath);
      if (rv && !continueEnumOnFailure) {
        break;
      }
    }
  }

  NS_tclosedir(dir);

  if (rv == OK) {
    ensure_write_permissions(path);
    rv = NS_trmdir(path);
    if (rv) {
      LOG(("ensure_remove_recursive: unable to remove directory: " LOG_S
           ", rv: %d, err: %d",
           path, rv, errno));
    }
  }
  return rv;
}

static bool is_read_only(const NS_tchar* flags) {
  size_t length = NS_tstrlen(flags);
  if (length == 0) {
    return false;
  }

  // Make sure the string begins with "r"
  if (flags[0] != NS_T('r')) {
    return false;
  }

  // Look for "r+" or "r+b"
  if (length > 1 && flags[1] == NS_T('+')) {
    return false;
  }

  // Look for "rb+"
  if (NS_tstrcmp(flags, NS_T("rb+")) == 0) {
    return false;
  }

  return true;
}

static FILE* ensure_open(const NS_tchar* path, const NS_tchar* flags,
                         unsigned int options) {
  ensure_write_permissions(path);
  FILE* f = NS_tfopen(path, flags);
  if (is_read_only(flags)) {
    // Don't attempt to modify the file permissions if the file is being opened
    // in read-only mode.
    return f;
  }
  if (NS_tchmod(path, options) != 0) {
    if (f != nullptr) {
      fclose(f);
    }
    return nullptr;
  }
  struct NS_tstat_t ss;
  if (NS_tstat(path, &ss) != 0 || ss.st_mode != options) {
    if (f != nullptr) {
      fclose(f);
    }
    return nullptr;
  }
  return f;
}

// Ensure that the directory containing this file exists.
static int ensure_parent_dir(const NS_tchar* path) {
  int rv = OK;

  NS_tchar* slash = (NS_tchar*)NS_tstrrchr(path, NS_T('/'));
  if (slash) {
    *slash = NS_T('\0');
    rv = ensure_parent_dir(path);
    // Only attempt to create the directory if we're not at the root
    if (rv == OK && *path) {
      rv = NS_tmkdir(path, 0755);
      // If the directory already exists, then ignore the error.
      if (rv < 0 && errno != EEXIST) {
        LOG(("ensure_parent_dir: failed to create directory: " LOG_S ", "
             "err: %d",
             path, errno));
        rv = WRITE_ERROR;
      } else {
        rv = OK;
      }
    }
    *slash = NS_T('/');
  }
  return rv;
}

static int ensure_copy_symlink(const NS_tchar* path, const NS_tchar* dest) {
  // Copy symlinks by creating a new symlink to the same target
  NS_tchar target[MAXPATHLEN + 1] = {NS_T('\0')};
  int rv = readlink(path, target, MAXPATHLEN);
  if (rv == -1) {
    LOG(("ensure_copy_symlink: failed to read the link: " LOG_S ", err: %d",
         path, errno));
    return READ_ERROR;
  }
  rv = symlink(target, dest);
  if (rv == -1) {
    LOG(("ensure_copy_symlink: failed to create the new link: " LOG_S
         ", target: " LOG_S " err: %d",
         dest, target, errno));
    return READ_ERROR;
  }
  return 0;
}

// Copy the file named path onto a new file named dest.
static int ensure_copy(const NS_tchar* path, const NS_tchar* dest) {
  struct NS_tstat_t ss;
  int rv = NS_tlstat(path, &ss);
  if (rv) {
    LOG(("ensure_copy: failed to read file status info: " LOG_S ", err: %d",
         path, errno));
    return READ_ERROR;
  }

  if (S_ISLNK(ss.st_mode)) {
    return ensure_copy_symlink(path, dest);
  }

  AutoFile infile(ensure_open(path, NS_T("rb"), ss.st_mode));
  if (!infile) {
    LOG(("ensure_copy: failed to open the file for reading: " LOG_S ", err: %d",
         path, errno));
    return READ_ERROR;
  }
  AutoFile outfile(ensure_open(dest, NS_T("wb"), ss.st_mode));
  if (!outfile) {
    LOG(("ensure_copy: failed to open the file for writing: " LOG_S ", err: %d",
         dest, errno));
    return WRITE_ERROR;
  }

  // This block size was chosen pretty arbitrarily but seems like a reasonable
  // compromise. For example, the optimal block size on a modern OS X machine
  // is 100k */
  const int blockSize = 32 * 1024;
  void* buffer = malloc(blockSize);
  if (!buffer) {
    return UPDATER_MEM_ERROR;
  }

  while (!feof(infile.get())) {
    size_t read = fread(buffer, 1, blockSize, infile);
    if (ferror(infile.get())) {
      LOG(("ensure_copy: failed to read the file: " LOG_S ", err: %d", path,
           errno));
      free(buffer);
      return READ_ERROR;
    }

    size_t written = 0;

    while (written < read) {
      size_t chunkWritten = fwrite(buffer, 1, read - written, outfile);
      if (chunkWritten <= 0) {
        LOG(("ensure_copy: failed to write the file: " LOG_S ", err: %d", dest,
             errno));
        free(buffer);
        return WRITE_ERROR_FILE_COPY;
      }

      written += chunkWritten;
    }
  }

  rv = NS_tchmod(dest, ss.st_mode);

  free(buffer);
  return rv;
}

// Returns true if the path is that of a draft file, that is, a file that the
// Draft phase of a non-staged update writes new contents to.
static bool is_draft_path(const NS_tchar* path) {
  size_t pathLen = NS_tstrlen(path);
  size_t extLen = NS_tstrlen(DRAFT_EXT);
  return pathLen > extLen && !NS_tstricmp(path + pathLen - extLen, DRAFT_EXT);
}

template <unsigned N>
struct copy_recursive_skiplist {
  NS_tchar paths[N][MAXPATHLEN];

  void append(unsigned index, const NS_tchar* path, const NS_tchar* suffix) {
    NS_tsnprintf(paths[index], MAXPATHLEN, NS_T("%s/%s"), path, suffix);
  }

  bool find(const NS_tchar* path) {
    for (int i = 0; i < static_cast<int>(N); ++i) {
      if (!NS_tstricmp(paths[i], path)) {
        return true;
      }
    }
    return false;
  }
};

// Copy all of the files and subdirectories under path to a new directory named
// dest. The path names in the skiplist will be skipped and will not be copied.
// Draft files are always skipped: a draft that a crashed updater instance left
// behind is not part of the installation, so it must not be copied into an
// updated one, where nothing would ever get rid of it.
template <unsigned N>
static int ensure_copy_recursive(const NS_tchar* path, const NS_tchar* dest,
                                 copy_recursive_skiplist<N>& skiplist) {
  struct NS_tstat_t sInfo;
  int rv = NS_tlstat(path, &sInfo);
  if (rv) {
    LOG(("ensure_copy_recursive: path doesn't exist: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return READ_ERROR;
  }

  if (S_ISLNK(sInfo.st_mode)) {
    return ensure_copy_symlink(path, dest);
  }

  if (!S_ISDIR(sInfo.st_mode)) {
    return ensure_copy(path, dest);
  }

  rv = NS_tmkdir(dest, sInfo.st_mode);
  if (rv < 0 && errno != EEXIST) {
    LOG(("ensure_copy_recursive: could not create destination directory: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return WRITE_ERROR;
  }

  NS_tDIR* dir;
  NS_tdirent* entry;

  dir = NS_topendir(path);
  if (!dir) {
    LOG(("ensure_copy_recursive: path is not a directory: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return READ_ERROR;
  }

  while ((entry = NS_treaddir(dir)) != nullptr) {
    if (NS_tstrcmp(entry->d_name, NS_T(".")) &&
        NS_tstrcmp(entry->d_name, NS_T(".."))) {
      NS_tchar childPath[MAXPATHLEN];
      NS_tsnprintf(childPath, sizeof(childPath) / sizeof(childPath[0]),
                   NS_T("%s/%s"), path, entry->d_name);
      if (skiplist.find(childPath) || is_draft_path(entry->d_name)) {
        continue;
      }
      NS_tchar childPathDest[MAXPATHLEN];
      NS_tsnprintf(childPathDest,
                   sizeof(childPathDest) / sizeof(childPathDest[0]),
                   NS_T("%s/%s"), dest, entry->d_name);
      rv = ensure_copy_recursive(childPath, childPathDest, skiplist);
      if (rv) {
        break;
      }
    }
  }
  NS_tclosedir(dir);
  return rv;
}


// Renames the specified file to the new file specified. If the destination file
// exists it is removed.
static int rename_file(const NS_tchar* spath, const NS_tchar* dpath,
                       bool allowDirs = false) {
  int rv = ensure_parent_dir(dpath);
  if (rv) {
    return rv;
  }

  struct NS_tstat_t spathInfo;
  rv = NS_tstat(spath, &spathInfo);
  if (rv) {
    LOG(("rename_file: failed to read file status info: " LOG_S ", "
         "err: %d",
         spath, errno));
    return READ_ERROR;
  }

  if (!S_ISREG(spathInfo.st_mode)) {
    if (allowDirs && !S_ISDIR(spathInfo.st_mode)) {
      LOG(("rename_file: path present, but not a file: " LOG_S ", err: %d",
           spath, errno));
      return RENAME_ERROR_EXPECTED_FILE;
    }
    LOG(("rename_file: proceeding to rename the directory"));
  }

  if (!NS_taccess(dpath, F_OK)) {
    rv = ensure_remove(dpath);
    if (rv) {
      LOG(
          ("rename_file: destination file exists and could not be "
           "removed: " LOG_S,
           dpath));
      return WRITE_ERROR_DELETE_FILE;
    }
  }

  if (NS_trename(spath, dpath) != 0) {
    LOG(("rename_file: failed to rename file - src: " LOG_S ", "
         "dst:" LOG_S ", err: %d",
         spath, dpath, errno));
    return WRITE_ERROR;
  }

  return OK;
}


//-----------------------------------------------------------------------------

// Create a backup of the specified file by renaming it. Sets created on
// success.
static int backup_create(const NS_tchar* path, bool& created) {
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup) / sizeof(backup[0]),
               NS_T("%s") BACKUP_EXT, path);

  int rv = rename_file(path, backup);
  if (rv == OK) {
    created = true;
  }
  return rv;
}

// Rename the backup of the specified file that was created by renaming it back
// to the original file.
static int backup_restore(const NS_tchar* path, const NS_tchar* relPath,
                          bool created) {
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup) / sizeof(backup[0]),
               NS_T("%s") BACKUP_EXT, path);

  NS_tchar relBackup[MAXPATHLEN];
  NS_tsnprintf(relBackup, sizeof(relBackup) / sizeof(relBackup[0]),
               NS_T("%s") BACKUP_EXT, relPath);

  if (NS_taccess(backup, F_OK)) {
    LOG(("backup_restore: backup file doesn't exist: " LOG_S, relBackup));
    return OK;
  }

  if (!created) {
    LOG(
        ("backup_restore: not restoring a backup that this action did not "
         "create: " LOG_S,
         relBackup));
    return OK;
  }

  return rename_file(backup, path);
}

// Discard the backup of the specified file that was created by renaming it.
static int backup_discard(const NS_tchar* path, const NS_tchar* relPath) {
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup) / sizeof(backup[0]),
               NS_T("%s") BACKUP_EXT, path);

  NS_tchar relBackup[MAXPATHLEN];
  NS_tsnprintf(relBackup, sizeof(relBackup) / sizeof(relBackup[0]),
               NS_T("%s") BACKUP_EXT, relPath);

  // Nothing to discard
  if (NS_taccess(backup, F_OK)) {
    return OK;
  }

  int rv = ensure_remove(backup);
  if (rv) {
    LOG(("backup_discard: unable to remove: " LOG_S, relBackup));
    return WRITE_ERROR_DELETE_BACKUP;
  }

  return OK;
}

[[nodiscard]] static bool draft_path(NS_tchar (&draft)[MAXPATHLEN],
                                     const NS_tchar* path) {
  return NS_tvsnprintf(draft, MAXPATHLEN, NS_T("%s") DRAFT_EXT, path);
}

// Discard the file that holds the draft contents of the specified file. This is
// also used to get rid of files that a previous updater instance left behind
// when it crashed, so it only ever discards drafts for paths that the update
// being applied produces new contents for.
static int draft_discard(const NS_tchar* path, const NS_tchar* relPath) {
  NS_tchar draft[MAXPATHLEN];
  NS_tchar relDraft[MAXPATHLEN];
  if (!draft_path(draft, path) || !draft_path(relDraft, relPath)) {
    LOG(("draft_discard: draft path too long for: " LOG_S, relPath));
    return USAGE_ERROR;
  }

  // Nothing to discard.
  if (NS_taccess(draft, F_OK)) {
    return OK;
  }

  LOG(("draft_discard: discarding draft file: " LOG_S, relDraft));

  int rv = ensure_remove(draft);
  if (rv) {
    LOG(("draft_discard: unable to remove: " LOG_S, relDraft));
    return WRITE_ERROR_DELETE_FILE;
  }

  return OK;
}

// Move the file that holds the draft contents of the specified file into
// place. The specified file must have been moved out of the way already.
static int draft_commit(const NS_tchar* path) {
  NS_tchar draft[MAXPATHLEN];
  if (!draft_path(draft, path)) {
    LOG(("draft_commit: draft path too long"));
    return USAGE_ERROR;
  }

  return rename_file(draft, path);
}

// Helper function for post-processing a temporary backup.
static void backup_finish(const NS_tchar* path, const NS_tchar* relPath,
                          int status, bool created) {
  if (status == OK) {
    backup_discard(path, relPath);
  } else {
    backup_restore(path, relPath, created);
  }
}

static int extract_file(const NS_tchar* itemPath, const NS_tchar* dstPath) {
  return gArchiveReader.ExtractFile(itemPath, dstPath);
}

static int extract_file_to_stream(const NS_tchar* itemPath, FILE* dstStream) {
  return gArchiveReader.ExtractFileToStream(itemPath, dstStream);
}

//-----------------------------------------------------------------------------

static int DoUpdate();

class Action {
 public:
  Action() : mProgressCost(1), mNext(nullptr), mPrev(nullptr) {}
  virtual ~Action() = default;

  virtual int Parse(NS_tchar* line) = 0;

  // Do any preprocessing to ensure that the action can be performed.  Draft
  // will be called if this Action and all others return OK from this method.
  virtual int Prepare() = 0;

  // In non-staged updates, write the new contents of the file that this action
  // produces to a draft file next to the target file.  Execute will be called
  // if this Action and all others return OK from this method.
  virtual int Draft() = 0;

  // Perform the operation.  Return OK to indicate success.  After all actions
  // have been executed, Finish will be called.  A requirement of Execute is
  // that its operation be reversable from Finish.
  virtual int Execute() = 0;

  // Finish is called after execution of all actions.  If status is OK, then
  // all actions were successfully executed.  Otherwise, some action failed.
  virtual void Finish(int status) = 0;

  int mProgressCost;

 protected:
  // Whether this action created the backup of its file. Only such backups are
  // restored: one left behind by an earlier updater run can be outdated.
  bool mBackupCreated = false;

 private:
  Action* mNext;
  Action* mPrev;

  friend class ActionList;
};

class RemoveFile : public Action {
 public:
  RemoveFile() : mSkip(0) {}

  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override { return OK; }
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mFile;
  mozilla::UniquePtr<NS_tchar[]> mRelPath;
  int mSkip;
};

int RemoveFile::Parse(NS_tchar* line) {
  // format "<deadfile>"

  NS_tchar* validPath = get_valid_path(&line);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mRelPath.get(), validPath);

  mFile.reset(get_full_path(validPath));
  if (!mFile) {
    return PARSE_ERROR;
  }

  return OK;
}

int RemoveFile::Prepare() {
  // Skip the file if it already doesn't exist.
  int rv = NS_taccess(mFile.get(), F_OK);
  if (rv) {
    mSkip = 1;
    mProgressCost = 0;
    return OK;
  }

  LOG(("PREPARE REMOVEFILE " LOG_S, mRelPath.get()));

  // Make sure that we're actually a file...
  struct NS_tstat_t fileInfo;
  rv = NS_tstat(mFile.get(), &fileInfo);
  if (rv) {
    LOG(("failed to read file status info: " LOG_S ", err: %d", mFile.get(),
         errno));
    return READ_ERROR;
  }

  if (!S_ISREG(fileInfo.st_mode)) {
    LOG(("path present, but not a file: " LOG_S, mFile.get()));
    return DELETE_ERROR_EXPECTED_FILE;
  }

  NS_tchar* slash = (NS_tchar*)NS_tstrrchr(mFile.get(), NS_T('/'));
  if (slash) {
    *slash = NS_T('\0');
    rv = NS_taccess(mFile.get(), W_OK);
    *slash = NS_T('/');
  } else {
    rv = NS_taccess(NS_T("."), W_OK);
  }

  if (rv) {
    LOG(("access failed: %d", errno));
    return WRITE_ERROR_FILE_ACCESS_DENIED;
  }

  return OK;
}

int RemoveFile::Execute() {
  if (mSkip) {
    return OK;
  }

  LOG(("EXECUTE REMOVEFILE " LOG_S, mRelPath.get()));

  // The file is checked for existence here and in Prepare since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mFile.get(), F_OK);
  if (rv) {
    LOG(("file cannot be removed because it does not exist; skipping"));
    mSkip = 1;
    return OK;
  }

  if (sStagedUpdate) {
    // Staged updates don't need backup files so just remove it.
    rv = ensure_remove(mFile.get());
    if (rv) {
      return WRITE_ERROR_DELETE_FILE;
    }
  } else {
    // Rename the old file. It will be removed in Finish.
    rv = backup_create(mFile.get(), mBackupCreated);
    if (rv) {
      LOG(("backup_create failed: %d", rv));
      return rv;
    }
  }

  return OK;
}

void RemoveFile::Finish(int status) {
  if (mSkip) {
    return;
  }

  LOG(("FINISH REMOVEFILE " LOG_S, mRelPath.get()));

  // Staged updates don't create backup files.
  if (!sStagedUpdate) {
    backup_finish(mFile.get(), mRelPath.get(), status, mBackupCreated);
  }
}

class RemoveDir : public Action {
 public:
  RemoveDir() : mSkip(0) {}

  int Parse(NS_tchar* line) override;
  int Prepare() override;  // check that the source dir exists
  int Draft() override { return OK; }
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mDir;
  mozilla::UniquePtr<NS_tchar[]> mRelPath;
  int mSkip;
};

int RemoveDir::Parse(NS_tchar* line) {
  // format "<deaddir>/"

  NS_tchar* validPath = get_valid_path(&line, true);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mRelPath.get(), validPath);

  mDir.reset(get_full_path(validPath));
  if (!mDir) {
    return PARSE_ERROR;
  }

  return OK;
}

int RemoveDir::Prepare() {
  // We expect the directory to exist if we are to remove it.
  int rv = NS_taccess(mDir.get(), F_OK);
  if (rv) {
    mSkip = 1;
    mProgressCost = 0;
    return OK;
  }

  LOG(("PREPARE REMOVEDIR " LOG_S "/", mRelPath.get()));

  // Make sure that we're actually a dir.
  struct NS_tstat_t dirInfo;
  rv = NS_tstat(mDir.get(), &dirInfo);
  if (rv) {
    LOG(("failed to read directory status info: " LOG_S ", err: %d",
         mRelPath.get(), errno));
    return READ_ERROR;
  }

  if (!S_ISDIR(dirInfo.st_mode)) {
    LOG(("path present, but not a directory: " LOG_S, mRelPath.get()));
    return DELETE_ERROR_EXPECTED_DIR;
  }

  rv = NS_taccess(mDir.get(), W_OK);
  if (rv) {
    LOG(("access failed: %d, %d", rv, errno));
    return WRITE_ERROR_DIR_ACCESS_DENIED;
  }

  return OK;
}

int RemoveDir::Execute() {
  if (mSkip) {
    return OK;
  }

  LOG(("EXECUTE REMOVEDIR " LOG_S "/", mRelPath.get()));

  // The directory is checked for existence at every step since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mDir.get(), F_OK);
  if (rv) {
    LOG(("directory no longer exists; skipping"));
    mSkip = 1;
  }

  return OK;
}

void RemoveDir::Finish(int status) {
  if (mSkip || status != OK) {
    return;
  }

  LOG(("FINISH REMOVEDIR " LOG_S "/", mRelPath.get()));

  // The directory is checked for existence at every step since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mDir.get(), F_OK);
  if (rv) {
    LOG(("directory no longer exists; skipping"));
    return;
  }

  if (status == OK) {
    if (NS_trmdir(mDir.get())) {
      LOG(("non-fatal error removing directory: " LOG_S "/, rv: %d, err: %d",
           mRelPath.get(), rv, errno));
    }
  }
}

class AddFile : public Action {
 public:
  AddFile() : mAdded(false) {}

  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mFile;
  mozilla::UniquePtr<NS_tchar[]> mRelPath;
  bool mAdded;
};

int AddFile::Parse(NS_tchar* line) {
  // format "<newfile>"

  NS_tchar* validPath = get_valid_path(&line);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mRelPath.get(), validPath);

  mFile.reset(get_full_path(validPath));
  if (!mFile) {
    return PARSE_ERROR;
  }

  return OK;
}

int AddFile::Prepare() {
  LOG(("PREPARE ADD " LOG_S, mRelPath.get()));

  return OK;
}

int AddFile::Draft() {
  // Logged before the staged update check on purpose: staged and non-staged
  // updater tests compare against the same expected update logs.
  LOG(("DRAFT ADD " LOG_S, mRelPath.get()));

  if (sStagedUpdate) {
    return OK;
  }

  NS_tchar draft[MAXPATHLEN];
  if (!draft_path(draft, mFile.get())) {
    LOG(("draft path too long for: " LOG_S, mRelPath.get()));
    return USAGE_ERROR;
  }

  int rv = ensure_parent_dir(mFile.get());
  if (rv) {
    return rv;
  }

  // Get rid of a draft that a previous updater instance left behind, so that
  // the new one is created with the mode that the archive asks for.
  rv = draft_discard(mFile.get(), mRelPath.get());
  if (rv) {
    return rv;
  }

  return extract_file(mRelPath.get(), draft);
}

int AddFile::Execute() {
  LOG(("EXECUTE ADD " LOG_S, mRelPath.get()));

  int rv;

  // First make sure that we can actually get rid of any existing file.
  rv = NS_taccess(mFile.get(), F_OK);
  if (rv == 0) {
    if (sStagedUpdate) {
      // Staged updates don't need backup files so just remove it.
      if (ensure_remove(mFile.get())) {
        return WRITE_ERROR_DELETE_FILE;
      }
    } else {
      rv = backup_create(mFile.get(), mBackupCreated);
      if (rv) {
        return rv;
      }
    }
  } else {
    rv = ensure_parent_dir(mFile.get());
    if (rv) {
      return rv;
    }
  }

  if (!sStagedUpdate) {
    // The new contents have already been written during the Draft phase.
    rv = draft_commit(mFile.get());
    if (!rv) {
      mAdded = true;
    }
    return rv;
  }

  rv = extract_file(mRelPath.get(), mFile.get());
  if (!rv) {
    mAdded = true;
  }
  return rv;
}

void AddFile::Finish(int status) {
  LOG(("FINISH ADD " LOG_S, mRelPath.get()));
  // Staged updates don't create backup files.
  if (!sStagedUpdate) {
    // Get rid of the new contents if they were never moved into place, which
    // happens when the update failed before or during the Execute phase.
    draft_discard(mFile.get(), mRelPath.get());

    // When there is an update failure and a file has been added it is removed
    // here since there might not be a backup to replace it.
    if (status && mAdded) {
      if (NS_tremove(mFile.get()) && errno != ENOENT) {
        LOG(("non-fatal error after update failure removing added file: " LOG_S
             ", err: %d",
             mFile.get(), errno));
      }
    }
    backup_finish(mFile.get(), mRelPath.get(), status, mBackupCreated);
  }
}

class PatchFileDecoder {
 public:
  // This method is the only valid way to create a PatchFileDecoder object,
  // which ensures that any such object has successfully called the Load()
  // method. Child classes must hide their constructors and declare the parent
  // class as a friend.
  template <typename ChildClass>
  static mozilla::UniquePtr<PatchFileDecoder> TryLoadAs(FILE* aPatchFile,
                                                        int* aReturnValue) {
    // We cannot use MakeUnique because child classes hide their constructors.
    mozilla::UniquePtr<ChildClass> ptr{new ChildClass()};
    fseek(aPatchFile, 0, SEEK_SET);
    int rv = ptr->Load(aPatchFile);
    if (rv != OK) {
      ptr.reset();
    }
    if (aReturnValue) {
      *aReturnValue = rv;
    }
    return ptr;
  }

  virtual ~PatchFileDecoder() = default;

  [[nodiscard]] virtual int ComputeCrc32(const uint8_t* aBuf, size_t aBufSize,
                                         unsigned int& aOutCrc32) = 0;

  virtual off_t SourceSize() = 0;
  virtual off_t DestinationSize() = 0;
  virtual unsigned int SourceCrc32() = 0;

  // Applies the loaded patch to aCheckedSrcBuf, and writes the result to
  // aDstFile. aDstFile is never deleted, cleanup is up to the caller.
  // Assumes that the crc32 and size of aCheckedSrcBuf have been
  // checked by the caller.
  [[nodiscard]] virtual int Apply(const uint8_t* aCheckedSrcBuf,
                                  size_t aCheckedSrcBufSize,
                                  FILE* aDstFile) = 0;

  // Release resources early, returning a status code.
  [[nodiscard]] virtual int Finalize() { return OK; }

 protected:
  virtual int Load(FILE* aPatchFile) = 0;
};

#if defined(MOZ_BSPATCH)
class BSPatchFileDecoder : public PatchFileDecoder {
 public:
  ~BSPatchFileDecoder() override = default;

  [[nodiscard]] int ComputeCrc32(const uint8_t* aBuf, size_t aBufSize,
                                 unsigned int& aOutCrc32) override;

  off_t SourceSize() override;

  off_t DestinationSize() override;

  unsigned int SourceCrc32() override;

  [[nodiscard]] int Apply(const uint8_t* aSrcBuf, size_t aSrcBufSize,
                          FILE* aDstFile) override;

 protected:  // Comply with PatchFileDecoder::TryLoadAs requirements
  BSPatchFileDecoder() = default;
  int Load(FILE* aPatchFile) override;
  friend class PatchFileDecoder;

 private:
  FILE* mPatchFile{};
  MBSPatchHeader mHeader{};
};

// This BZ2_crc32Table variable lives in libbz2. We just took the
// data structure from bz2 and created crctables.h
int BSPatchFileDecoder::ComputeCrc32(const uint8_t* aBuf, size_t aBufSize,
                                     unsigned int& aOutCrc32) {
  unsigned int crc = 0xffffffffL;

  const uint8_t* end = aBuf + aBufSize;
  for (; aBuf != end; ++aBuf)
    crc = (crc << 8) ^ BZ2_crc32Table[(crc >> 24) ^ *aBuf];

  aOutCrc32 = ~crc;
  return OK;
}

int BSPatchFileDecoder::Load(FILE* aPatchFile) {
  mPatchFile = aPatchFile;
  return MBS_ReadHeader(aPatchFile, &mHeader);
}

off_t BSPatchFileDecoder::SourceSize() {
  return static_cast<off_t>(mHeader.slen);
}

off_t BSPatchFileDecoder::DestinationSize() {
  return static_cast<off_t>(mHeader.dlen);
}

unsigned int BSPatchFileDecoder::SourceCrc32() { return mHeader.scrc32; }

int BSPatchFileDecoder::Apply(const uint8_t* aCheckedSrcBuf,
                              size_t aCheckedSrcBufSize, FILE* aDstFile) {
  return MBS_ApplyPatch(&mHeader, mPatchFile, aCheckedSrcBuf, aDstFile);
}
#endif  // defined(MOZ_BSPATCH)

#if defined(MOZ_ZUCCHINI)
void LogZucchiniMessage(const char* aMessage) { LOG(("%s", aMessage)); }

// Best-effort conversion from Zucchini status codes to updater status codes
int FromZucchiniStatus(zucchini::status::Code code) {
  int result = OK;
  switch (code) {
    case zucchini::status::kStatusSuccess:
      break;
    case zucchini::status::kStatusFileReadError:
    case zucchini::status::kStatusPatchReadError:
      result = READ_ERROR;
      break;
    case zucchini::status::kStatusFileWriteError:
      result = WRITE_ERROR;
      break;
    case zucchini::status::kStatusPatchWriteError:
      result = WRITE_ERROR_PATCH_FILE;
      break;
    case zucchini::status::kStatusInvalidOldImage:
    case zucchini::status::kStatusInvalidNewImage:
      result = CRC_ERROR;
      break;
    case zucchini::status::kStatusOutOfMemory:
      result = BSPATCH_MEM_ERROR;
      break;
    case zucchini::status::kStatusInvalidParam:
    case zucchini::status::kStatusDiskFull:
    case zucchini::status::kStatusIoError:
    case zucchini::status::kStatusFatal:
    default:
      result = UNEXPECTED_BSPATCH_ERROR;
      break;
  }
  if (result != OK) {
    LOG(
        ("FromZucchiniStatus: encountered zucchini error %d, converting to "
         "updater error %d",
         code, result));
  }
  return result;
}

class ZucchiniPatchFileDecoder : public PatchFileDecoder {
 public:
  ~ZucchiniPatchFileDecoder() override = default;

  [[nodiscard]] int ComputeCrc32(const uint8_t* aBuf, size_t aBufSize,
                                 unsigned int& aOutCrc32) override;

  off_t SourceSize() override;

  off_t DestinationSize() override;

  unsigned int SourceCrc32() override;

  [[nodiscard]] int Apply(const uint8_t* aCheckedSrcBuf,
                          size_t aCheckedSrcBufSize, FILE* aDstFile) override;

  [[nodiscard]] int Finalize() override;

 protected:  // Comply with PatchFileDecoder::TryLoadAs requirements
  ZucchiniPatchFileDecoder() = default;
  int Load(FILE* aPatchFile) override;
  friend class PatchFileDecoder;

 private:
  zucchini::mozilla::MappedPatch mMappedPatch;
  uint32_t mSourceSize{};
  uint32_t mDestinationSize{};
  uint32_t mSourceCrc32{};
};

int ZucchiniPatchFileDecoder::ComputeCrc32(const uint8_t* aBuf, size_t aBufSize,
                                           unsigned int& aOutCrc32) {
  return FromZucchiniStatus(
      zucchini::mozilla::ComputeCrc32(aBuf, aBufSize, aOutCrc32));
}

int ZucchiniPatchFileDecoder::Load(FILE* aPatchFile) {
  return FromZucchiniStatus(mMappedPatch.Load(
      aPatchFile, &mSourceSize, &mDestinationSize, &mSourceCrc32));
}

off_t ZucchiniPatchFileDecoder::SourceSize() {
  return static_cast<off_t>(mSourceSize);
}

off_t ZucchiniPatchFileDecoder::DestinationSize() {
  return static_cast<off_t>(mDestinationSize);
}

unsigned int ZucchiniPatchFileDecoder::SourceCrc32() {
  return static_cast<int>(mSourceCrc32);
}

int ZucchiniPatchFileDecoder::Apply(const uint8_t* aCheckedSrcBuf,
                                    size_t aCheckedSrcBufSize, FILE* aDstFile) {
  // SAFETY: The caller has already checked that the crc32 and size of
  // aCheckedSrcBuf match with the contents of the patch file as a requirement
  // of PatchFileDecoder::Apply.
  return FromZucchiniStatus(
      mMappedPatch.ApplyUnsafe(aCheckedSrcBuf, aCheckedSrcBufSize, aDstFile));
}

int ZucchiniPatchFileDecoder::Finalize() {
  return FromZucchiniStatus(mMappedPatch.Finalize());
}
#endif  // defined(MOZ_ZUCCHINI)

class PatchFile : public Action {
 public:
  PatchFile() : mPatchFile(nullptr), mPatchIndex(-1), mBufSize(0) {}

  ~PatchFile() override;

  int Parse(NS_tchar* line) override;
  int Prepare() override;  // should check for patch file and for checksum here
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 private:
  enum class PatchDest {
    InPlace,
    Draft,
  };

  int LoadSourceFile(FILE* ofile);

  // This consumes the patch, so it may only be called once.
  int ApplyPatchTo(PatchDest aDest);

  static int sPatchIndex;

  const NS_tchar* mPatchFile;
  mozilla::UniquePtr<NS_tchar[]> mFile;
  mozilla::UniquePtr<NS_tchar[]> mFileRelPath;
  int mPatchIndex;
  mozilla::UniquePtr<PatchFileDecoder> mPatchFileDecoder;
  mozilla::UniquePtr<uint8_t[]> mBuf;
  size_t mBufSize;
  NS_tchar mPatchPath[MAXPATHLEN];
  AutoFile mPatchStream;
};

int PatchFile::sPatchIndex = 0;

PatchFile::~PatchFile() {
  // Make sure mPatchStream gets unlocked on Windows; the system will do that,
  // but not until some indeterminate future time, and we want determinism.
  // Normally this happens at the end of Execute, when we close the stream;
  // this call is here in case Execute errors out.
  // Patch files are written to the <working_dir>/updating directory which is
  // removed after the update has finished so don't delete patch files here.
}

int PatchFile::LoadSourceFile(FILE* ofile) {
  struct stat os;
  int rv = fstat(fileno((FILE*)ofile), &os);
  if (rv) {
    LOG(("LoadSourceFile: unable to stat destination file: " LOG_S ", "
         "err: %d",
         mFileRelPath.get(), errno));
    return READ_ERROR;
  }

  off_t expectedSize = mPatchFileDecoder->SourceSize();
  if (os.st_size != expectedSize) {
    LOG(
        ("LoadSourceFile: destination file size %jd does not match expected "
         "size %jd",
         static_cast<intmax_t>(os.st_size),
         static_cast<intmax_t>(expectedSize)));
    return LOADSOURCE_ERROR_WRONG_SIZE;
  }

  mBufSize = os.st_size;
  mBuf = mozilla::MakeUniqueFallible<uint8_t[]>(mBufSize);
  if (!mBuf) {
    return UPDATER_MEM_ERROR;
  }

  size_t r = mBufSize;
  uint8_t* rb = mBuf.get();
  while (r) {
    const size_t count = mmin(SSIZE_MAX, r);
    size_t c = fread(rb, 1, count, ofile);
    if (c != count) {
      LOG(("LoadSourceFile: error reading destination file: " LOG_S,
           mFileRelPath.get()));
      return READ_ERROR;
    }

    r -= c;
    rb += c;
  }

  // Verify that the contents of the source file correspond to what we expect.

  unsigned int crc = 0;
  rv = mPatchFileDecoder->ComputeCrc32(mBuf.get(), mBufSize, crc);
  if (rv != OK) {
    LOG(("LoadSourceFile: crc computation failed, err: %d", rv));
    return rv;
  }
  unsigned int expectedCrc = mPatchFileDecoder->SourceCrc32();

  if (crc != expectedCrc) {
    LOG(
        ("LoadSourceFile: destination file crc %u does not match expected "
         "crc %u",
         crc, expectedCrc));
    return CRC_ERROR;
  }

  return OK;
}

int PatchFile::Parse(NS_tchar* line) {
  // format "<patchfile>" "<filetopatch>"

  // Get the path to the patch file inside of the mar
  mPatchFile = mstrtok(kQuote, &line);
  if (!mPatchFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  NS_tchar* validPath = get_valid_path(&line);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mFileRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mFileRelPath.get(), validPath);

  mFile.reset(get_full_path(validPath));
  if (!mFile) {
    return PARSE_ERROR;
  }

  return OK;
}

int PatchFile::Prepare() {
  LOG(("PREPARE PATCH " LOG_S, mFileRelPath.get()));

  // extract the patch to a temporary file
  mPatchIndex = sPatchIndex++;

  NS_tsnprintf(mPatchPath, sizeof(mPatchPath) / sizeof(mPatchPath[0]),
               NS_T("%s/updating/%d.patch"), gWorkingDirPath, mPatchIndex);

  // The removal of pre-existing patch files here is in case a previous update
  // crashed and left these files behind.
  if (NS_tremove(mPatchPath) && errno != ENOENT) {
    LOG(("failure removing pre-existing patch file: " LOG_S ", err: %d",
         mPatchPath, errno));
    return WRITE_ERROR;
  }

  mPatchStream = NS_tfopen(mPatchPath, NS_T("wb+"));
  if (!mPatchStream) {
    return WRITE_ERROR;
  }


  return extract_file_to_stream(mPatchFile, mPatchStream);
}

int PatchFile::Draft() {
  // Logged before the staged update check on purpose: staged and non-staged
  // updater tests compare against the same expected update logs.
  LOG(("DRAFT PATCH " LOG_S, mFileRelPath.get()));

  if (sStagedUpdate) {
    return OK;
  }

  // Get rid of a draft that a previous updater instance left behind. Its mode
  // doesn't matter, because ApplyPatchTo sets the mode of the draft it writes,
  // but on Windows ApplyPatchTo opens the draft exclusively, so an undeletable
  // one has to be moved out of the way first, which is what draft_discard does.
  int rv = draft_discard(mFile.get(), mFileRelPath.get());
  if (rv) {
    return rv;
  }

  return ApplyPatchTo(PatchDest::Draft);
}

int PatchFile::Execute() {
  LOG(("EXECUTE PATCH " LOG_S, mFileRelPath.get()));

  if (!sStagedUpdate) {
    // The new contents have already been written during the Draft phase. Rename
    // the destination file so that it can be used to restore the file to its
    // original state if there is an error, then move the new contents in place.
    int rv = backup_create(mFile.get(), mBackupCreated);
    if (rv) {
      return rv;
    }

    return draft_commit(mFile.get());
  }

  return ApplyPatchTo(PatchDest::InPlace);
}

int PatchFile::ApplyPatchTo(PatchDest aDest) {
  NS_tchar draft[MAXPATHLEN];
  if (aDest == PatchDest::Draft && !draft_path(draft, mFile.get())) {
    LOG(("draft path too long for: " LOG_S, mFileRelPath.get()));
    return USAGE_ERROR;
  }
  const NS_tchar* destPath = aDest == PatchDest::Draft ? draft : mFile.get();

  int rv = UNEXPECTED_BSPATCH_ERROR;

  // zucchini patch files start with "Zucc" bytes, while bspatch patch files
  // start with "MBDIFF10" bytes. Since these bytes are checked, there is no
  // risk of a loader accepting a patch in the wrong format and we can safely
  // iterate over the formats.

#if defined(MOZ_BSPATCH)
  mPatchFileDecoder =
      PatchFileDecoder::TryLoadAs<BSPatchFileDecoder>(mPatchStream, &rv);
#endif  // defined(MOZ_BSPATCH)

#if defined(MOZ_ZUCCHINI)
  if (!mPatchFileDecoder) {
    mPatchFileDecoder = PatchFileDecoder::TryLoadAs<ZucchiniPatchFileDecoder>(
        mPatchStream, &rv);
  }
#endif  // defined(MOZ_ZUCCHINI)

  if (!mPatchFileDecoder) {
    return rv;
  }

  FILE* origfile = nullptr;
  origfile = NS_tfopen(mFile.get(), NS_T("rb"));

  if (!origfile) {
    LOG(("unable to open destination file: " LOG_S ", err: %d",
         mFileRelPath.get(), errno));
    return READ_ERROR;
  }

  rv = LoadSourceFile(origfile);
  fclose(origfile);
  if (rv) {
    LOG(("LoadSourceFile failed"));
    return rv;
  }

  // The new file inherits the mode of the file that we are patching.
  struct NS_tstat_t ss;
  rv = NS_tstat(mFile.get(), &ss);
  if (rv) {
    LOG(("failed to read file status info: " LOG_S ", err: %d",
         mFileRelPath.get(), errno));
    return READ_ERROR;
  }

  unsigned int destMode = ss.st_mode;

  off_t dlen = mPatchFileDecoder->DestinationSize();

#if defined(HAVE_POSIX_FALLOCATE)
  AutoFile ofile(ensure_open(destPath, NS_T("wb+"), destMode));
  if (ofile != nullptr) {
    // Preallocation is only an anti-fragmentation optimization; a failure here
    // is not fatal because writing the patched contents will fail below if
    // there really is no space. posix_fallocate returns the error number
    // instead of setting errno.
    int fallocateRv = posix_fallocate(fileno((FILE*)ofile), 0, dlen);
    if (fallocateRv != 0) {
      LOG(("failed to preallocate space for new file: " LOG_S ", err: %d",
           mFileRelPath.get(), fallocateRv));
    }
  }
#else
  AutoFile ofile(ensure_open(destPath, NS_T("wb+"), destMode));
  if (ofile != nullptr) {
    // Modified code from FileUtils.cpp
    fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, dlen};
    // Try to get a continous chunk of disk space
    rv = fcntl(fileno((FILE*)ofile), F_PREALLOCATE, &store);
    if (rv == -1) {
      // OK, perhaps we are too fragmented, allocate non-continuous
      store.fst_flags = F_ALLOCATEALL;
      rv = fcntl(fileno((FILE*)ofile), F_PREALLOCATE, &store);
    }

    if (rv != -1 && ftruncate(fileno((FILE*)ofile), dlen) != 0) {
      LOG(("failed to set the size of the new file: " LOG_S ", err: %d",
           mFileRelPath.get(), errno));
    }
  }
#endif

  if (ofile == nullptr) {
    LOG(("unable to create new file: " LOG_S ", err: %d", mFileRelPath.get(),
         errno));
    return WRITE_ERROR_OPEN_PATCH_FILE;
  }


  // SAFETY: We have manually checked that the size and crc32 of mBuf match with
  // the patch in PatchFile::LoadSourceFile.
  rv = mPatchFileDecoder->Apply(mBuf.get(), mBufSize, ofile);

  if (rv == OK) {
    // Manually release resources, and propagate any failure that could reflect
    // process instability (e.g. OOM).
    rv = mPatchFileDecoder->Finalize();
  }

  // Go ahead and do a bit of cleanup now to minimize runtime overhead.
  // Release the patch decoder and any resources it holds (such as
  // memory-mapped patch files in zucchini) so they don't accumulate
  // across sequential patch actions.
  mPatchFileDecoder.reset();
  // Make sure mPatchStream gets unlocked on Windows; the system will do that,
  // but not until some indeterminate future time, and we want determinism.
  // Set mPatchStream to nullptr to make AutoFile close the file,
  // so it can be deleted on Windows.
  mPatchStream = nullptr;
  // Patch files are written to the <working_dir>/updating directory which is
  // removed after the update has finished so don't delete patch files here.
  mPatchPath[0] = NS_T('\0');
  mBuf.reset();
  mBufSize = 0;

  return rv;
}

void PatchFile::Finish(int status) {
  LOG(("FINISH PATCH " LOG_S, mFileRelPath.get()));

  // Staged updates don't create backup files.
  if (!sStagedUpdate) {
    // Get rid of the new contents if they were never moved into place, which
    // happens when the update failed before or during the Execute phase.
    draft_discard(mFile.get(), mFileRelPath.get());

    backup_finish(mFile.get(), mFileRelPath.get(), status, mBackupCreated);
  }
}

class AddIfFile : public AddFile {
 public:
  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 protected:
  mozilla::UniquePtr<NS_tchar[]> mTestFile;
};

int AddIfFile::Parse(NS_tchar* line) {
  // format "<testfile>" "<newfile>"

  mTestFile.reset(get_full_path(get_valid_path(&line)));
  if (!mTestFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  return AddFile::Parse(line);
}

int AddIfFile::Prepare() {
  // If the test file does not exist, then skip this action.
  if (NS_taccess(mTestFile.get(), F_OK)) {
    mTestFile = nullptr;
    return OK;
  }

  return AddFile::Prepare();
}

int AddIfFile::Draft() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Draft();
}

int AddIfFile::Execute() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Execute();
}

void AddIfFile::Finish(int status) {
  if (!mTestFile) {
    return;
  }

  AddFile::Finish(status);
}

class AddIfNotFile : public AddFile {
 public:
  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 protected:
  mozilla::UniquePtr<NS_tchar[]> mTestFile;
};

int AddIfNotFile::Parse(NS_tchar* line) {
  // format "<testfile>" "<newfile>"

  mTestFile.reset(get_full_path(get_valid_path(&line)));
  if (!mTestFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  return AddFile::Parse(line);
}

int AddIfNotFile::Prepare() {
  // If the test file exists, then skip this action.
  if (!NS_taccess(mTestFile.get(), F_OK)) {
    mTestFile = nullptr;
    return OK;
  }

  return AddFile::Prepare();
}

int AddIfNotFile::Draft() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Draft();
}

int AddIfNotFile::Execute() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Execute();
}

void AddIfNotFile::Finish(int status) {
  if (!mTestFile) {
    return;
  }

  AddFile::Finish(status);
}

class PatchIfFile : public PatchFile {
 public:
  int Parse(NS_tchar* line) override;
  int Prepare() override;  // should check for patch file and for checksum here
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mTestFile;
};

int PatchIfFile::Parse(NS_tchar* line) {
  // format "<testfile>" "<patchfile>" "<filetopatch>"

  mTestFile.reset(get_full_path(get_valid_path(&line)));
  if (!mTestFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  return PatchFile::Parse(line);
}

int PatchIfFile::Prepare() {
  // If the test file does not exist, then skip this action.
  if (NS_taccess(mTestFile.get(), F_OK)) {
    mTestFile = nullptr;
    return OK;
  }

  return PatchFile::Prepare();
}

int PatchIfFile::Draft() {
  if (!mTestFile) {
    return OK;
  }

  return PatchFile::Draft();
}

int PatchIfFile::Execute() {
  if (!mTestFile) {
    return OK;
  }

  return PatchFile::Execute();
}

void PatchIfFile::Finish(int status) {
  if (!mTestFile) {
    return;
  }

  PatchFile::Finish(status);
}

//-----------------------------------------------------------------------------


static void LaunchCallbackApp(const NS_tchar* workingDir, int argc,
                              NS_tchar** argv, bool usingService) {
  putenv(const_cast<char*>("MOZ_LAUNCHED_CHILD=1"));

  // Run from the specified working directory (see bug 312360).
  if (NS_tchdir(workingDir) != 0) {
    LOG(("Warning: chdir failed"));
  }

#if defined(USE_EXECV)
  execv(argv[0], argv);
#else
  LaunchMacApp(argc, (const char**)argv, gCallbackWaitPid);
#endif
}

static void WriteUpdateTelemetry(const NS_tchar* aInstallDir) {
  auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  char content[128];
  snprintf(content, sizeof(content) / sizeof(content[0]),
           "{\"install_timestamp\":\"%lld\"}", (long long)nowMs);

  if (setxattr(aInstallDir, "org.mozilla.update-telemetry", content,
               strlen(content), 0, 0) != 0) {
    LOG(("Failed to set org.mozilla.update-telemetry xattr: %d", errno));
  }
}

static bool WriteToFile(const NS_tchar* aFilename, const char* aStatus) {
  LOG(("Writing status to file: %s", aStatus));

  NS_tchar statusFilePath[MAXPATHLEN + 1] = {NS_T('\0')};
  NS_tsnprintf(statusFilePath,
               sizeof(statusFilePath) / sizeof(statusFilePath[0]),
               NS_T("%s/%s"), gPatchDirPath, aFilename);
  // Make sure that the directory for the update status file exists
  if (ensure_parent_dir(statusFilePath)) {
    LOG(("WriteToFile failed to ensure parent directory's existence"));
    return false;
  }

  AutoFile statusFile(CreateAndOpenFile(statusFilePath, true));
  if (statusFile == nullptr) {
    LOG(("WriteToFile failed to open status file: %d", errno));
    return false;
  }

  if (fwrite(aStatus, strlen(aStatus), 1, statusFile) != 1) {
    LOG(("WriteToFile failed to write to status file: %d", errno));
    return false;
  }


  return true;
}

/**
 * Writes a string to the update.status file.
 *
 * NOTE: All calls to WriteStatusFile MUST happen before calling output_finish
 *       because the output_finish function copies the update status file for
 *       the elevated updater and writing the status file after calling
 *       output_finish will overwrite it.
 *
 * @param  aStatus
 *         The string to write to the update.status file.
 * @return true on success.
 */
static bool WriteStatusFile(const char* aStatus) {
  return WriteToFile(NS_T("update.status"), aStatus);
}

/**
 * Writes a string to the update.status file based on the status param.
 *
 * NOTE: All calls to WriteStatusFile MUST happen before calling output_finish
 *       because the output_finish function copies the update status file for
 *       the elevated updater and writing the status file after calling
 *       output_finish will overwrite it.
 *
 * @param  status
 *         A status code used to determine what string to write to the
 *         update.status file (see code).
 */
static void WriteStatusFile(int status) {
  const char* text;

  char buf[32];
  if (status == OK) {
    if (sStagedUpdate) {
      text = "applied\n";
    } else {
      text = "succeeded\n";
    }
  } else {
    snprintf(buf, sizeof(buf) / sizeof(buf[0]), "failed: %d\n", status);
    text = buf;
  }

  WriteStatusFile(text);
}


#ifdef MOZ_MAINTENANCE_SERVICE
/*
 * Read the update.status file and sets isPendingService to true if
 * the status is set to pending-service.
 *
 * @param  isPendingService Out parameter for specifying if the status
 *         is set to pending-service or not.
 * @return true if the information was retrieved and it is pending
 *         or pending-service.
 */
static bool IsUpdateStatusPendingService() {
  NS_tchar filename[MAXPATHLEN];
  NS_tsnprintf(filename, sizeof(filename) / sizeof(filename[0]),
               NS_T("%s/update.status"), gPatchDirPath);

  AutoFile file(NS_tfopen(filename, NS_T("rb")));
  if (file == nullptr) {
    return false;
  }

  const size_t bufferLength = 32;
  char buf[bufferLength] = {0};
  size_t charsRead = fread(buf, sizeof(buf[0]), bufferLength - 1, file);
  if (ferror(file)) {
    return false;
  }
  buf[charsRead] = '\0';

  return UpdateStatusIs(buf, "pending-service") ||
         UpdateStatusIs(buf, "applied-service");
}

/*
 * Reads the secure update status file and sets isFailed to true if the
 * status is set to failed.
 *
 * @param  isFailed Out parameter for specifying if the status
 *                  is set to failed or not.
 * @param  errorCode Optional out parameter. If a pointer is passed and the
 *                   update status file contains an error code, the code
 *                   will be returned via the out parameter. If a pointer is
 *                   passed and the update status file does not contain an error
 *                   code, or any error code after the status could not be
 *                   parsed, mozilla::Nothing will be returned via this
 *                   parameter.
 * @return true if the information was retrieved successfully.
 */
static bool IsSecureUpdateStatusFailed(
    bool& isFailed, mozilla::Maybe<int>* errorCode = nullptr) {
  return CompareSecureUpdateStatus("failed", isFailed, errorCode);
}

/**
 * This function determines whether the error represented by the passed error
 * code could potentially be recovered from or bypassed by updating without
 * using the Maintenance Service (i.e. by showing a UAC prompt).
 * We don't really want to show a UAC prompt, but it's preferable over the
 * manual update doorhanger
 *
 * @param   errorCode An integer error code from the update.status file. Should
 *                    be one of the codes enumerated in updatererrors.h.
 * @returns true if the code represents a Maintenance Service specific error.
 *          Otherwise, false.
 */
static bool IsServiceSpecificErrorCode(int errorCode) {
  return ((errorCode >= 24 && errorCode <= 33) ||
          (errorCode >= 49 && errorCode <= 58));
}
#endif

/*
 * Copy the entire contents of the application installation directory to the
 * destination directory for the update process.
 *
 * @return 0 if successful, an error code otherwise.
 */
static int CopyInstallDirToDestDir() {
  // These files should not be copied over to the updated app
#  define SKIPLIST_COUNT 0
  copy_recursive_skiplist<SKIPLIST_COUNT> skiplist;

  return ensure_copy_recursive(gInstallDirPath, gWorkingDirPath, skiplist);
}

/*
 * Replace the application installation directory with the destination
 * directory in order to finish a staged update task
 *
 * @return 0 if successful, an error code otherwise.
 */
static int ProcessReplaceRequest() {
  // The replacement algorithm is like this:
  // 1. Move destDir to tmpDir.  In case of failure, abort.
  // 2. Move newDir to destDir.  In case of failure, revert step 1 and abort.
  // 3. Delete tmpDir (or defer it to the next reboot).

  NS_tchar destDir[MAXPATHLEN];
  NS_tsnprintf(destDir, sizeof(destDir) / sizeof(destDir[0]),
               NS_T("%s/Contents"), gInstallDirPath);

  NS_tchar tmpDir[MAXPATHLEN];
  NS_tsnprintf(tmpDir, sizeof(tmpDir) / sizeof(tmpDir[0]), NS_T("%s.bak"),
               destDir);

  NS_tchar newDir[MAXPATHLEN];
  NS_tsnprintf(newDir, sizeof(newDir) / sizeof(newDir[0]),
               NS_T("%s/Contents"), gWorkingDirPath);

  // First try to remove the possibly existing temp directory, because if this
  // directory exists, we will fail to rename destDir.
  // No need to error check here because if this fails, we will fail in the
  // next step anyways.
  ensure_remove_recursive(tmpDir);

  LOG(("Begin moving destDir (" LOG_S ") to tmpDir (" LOG_S ")", destDir,
       tmpDir));
  int rv = rename_file(destDir, tmpDir, true);
  if (rv) {
    // The status file will have 'pending' written to it so there is no value in
    // returning an error specific for this failure.
    LOG(("Moving destDir to tmpDir failed, err: %d", rv));
    return rv;
  }

  LOG(("Begin moving newDir (" LOG_S ") to destDir (" LOG_S ")", newDir,
       destDir));
  rv = rename_file(newDir, destDir, true);
  if (rv) {
    LOG(("Moving failed. Begin copying newDir (" LOG_S ") to destDir (" LOG_S
         ")",
         newDir, destDir));
    copy_recursive_skiplist<0> skiplist;
    rv = ensure_copy_recursive(newDir, destDir, skiplist);
  }
  if (rv) {
    LOG(("Moving newDir to destDir failed, err: %d", rv));
    LOG(("Now, try to move tmpDir back to destDir"));
    ensure_remove_recursive(destDir);
    int rv2 = rename_file(tmpDir, destDir, true);
    if (rv2) {
      LOG(("Moving tmpDir back to destDir failed, err: %d", rv2));
    }
    // The status file will be have 'pending' written to it so there is no value
    // in returning an error specific for this failure.
    return rv;
  }


  LOG(("Now, remove the tmpDir"));
  rv = ensure_remove_recursive(tmpDir, true);
  if (rv) {
    LOG(("Removing tmpDir failed, err: %d", rv));
  }

  // On OS X, we we need to remove the staging directory after its Contents
  // directory has been moved.
  NS_tchar updatedAppDir[MAXPATHLEN];
  NS_tsnprintf(updatedAppDir, sizeof(updatedAppDir) / sizeof(updatedAppDir[0]),
               NS_T("%s/Updated.app"), gPatchDirPath);
  ensure_remove_recursive(updatedAppDir);

  gSucceeded = true;
  WriteUpdateTelemetry(gInstallDirPath);

  return 0;
}


#ifdef MOZ_VERIFY_MAR_SIGNATURE
/**
 * This function reads in the ACCEPTED_MAR_CHANNEL_IDS from a string buffer.
 *
 * @param aChannels   A string buffer containing the MAR channel(s).
 * @param aResults    A pointer to the location to store the read strings.
 * @return OK on success
 */
static int ReadMARChannelIDsFromBuffer(char* aChannels,
                                       MARChannelStringTable* aResults) {
  const unsigned int kNumStrings = 1;
  const char* kUpdaterKeys = "ACCEPTED_MAR_CHANNEL_IDS\0";
  return ReadStringsFromBuffer(aChannels, kUpdaterKeys, kNumStrings,
                               &aResults->MARChannelID, "Settings");
}

/**
 * This function reads in the `ACCEPTED_MAR_CHANNEL_IDS` from the appropriate
 * (platform-dependent) source and populates `gMARStrings`.
 *
 * @return
 *        `OK` on success, `UPDATE_SETTINGS_FILE_CHANNEL` on failure.
 */
static int PopulategMARStrings() {
  if (gMARStrings.MARChannelID && gMARStrings.MARChannelID[0] != '\0') {
    return OK;
  }

  int rv = UPDATE_SETTINGS_FILE_CHANNEL;
  if (gInvocation != UpdaterInvocation::Second) {
    if (std::optional<std::string> marChannels =
            UpdateSettingsUtil::GetAcceptedMARChannelsValue()) {
      rv = ReadMARChannelIDsFromBuffer(marChannels->data(), &gMARStrings);
    }
  }
  return rv == OK ? OK : UPDATE_SETTINGS_FILE_CHANNEL;
}
#endif  // MOZ_VERIFY_MAR_SIGNATURE

static int GetUpdateFileName(NS_tchar* fileName, int maxChars) {
  NS_tsnprintf(fileName, maxChars, NS_T("%s/update.mar"), gPatchDirPath);
  return OK;
}

static void UpdateThreadFunc(void* param) {
  // open ZIP archive and process...
  int rv;
  if (sReplaceRequest) {
    rv = ProcessReplaceRequest();
  } else {
    NS_tchar dataFile[MAXPATHLEN];
    rv = GetUpdateFileName(dataFile, sizeof(dataFile) / sizeof(dataFile[0]));
    if (rv == OK) {
      rv = gArchiveReader.Open(dataFile);
    }

#ifdef MOZ_VERIFY_MAR_SIGNATURE
    if (rv == OK) {
      rv = gArchiveReader.VerifySignature();
    }

    if (rv == OK) {
      rv = PopulategMARStrings();
      if (rv == OK) {
        rv = gArchiveReader.VerifyProductInformation(
            gMARStrings.MARChannelID.get(), MOZ_APP_VERSION);
      }
    }
#endif

    if (rv == OK && sStagedUpdate) {
#ifdef TEST_UPDATER
      // The MOZ_TEST_SKIP_UPDATE_STAGE environment variable prevents copying
      // the files in dist/bin in the test updater when staging an update since
      // this can cause tests to timeout.
      if (EnvHasValue("MOZ_TEST_SKIP_UPDATE_STAGE")) {
        rv = OK;
      } else if (EnvHasValue("MOZ_TEST_SLOW_SKIP_UPDATE_STAGE")) {
        // The following is to simulate staging so the UI tests have time to
        // show that the update is being staged.
        NS_tchar continueFilePath[MAXPATHLEN] = {NS_T('\0')};
        NS_tsnprintf(continueFilePath,
                     sizeof(continueFilePath) / sizeof(continueFilePath[0]),
                     NS_T("%s/continueStaging"), gInstallDirPath);
        // Use 300 retries for staging requests to lessen the likelihood of
        // tests intermittently failing on verify tasks due to launching the
        // updater. The total time to wait with the default interval of 100 ms
        // is approximately 30 seconds. The total time for tests is longer to
        // account for the extra time it takes for the updater to launch.
        const int max_retries = 300;
        int retries = 0;
        while (retries++ < max_retries) {
          usleep(100000);
          // Continue after the continue file exists and is removed.
          if (!NS_tremove(continueFilePath)) {
            break;
          }
        }
        rv = OK;
      } else {
        rv = CopyInstallDirToDestDir();
      }
#else
      rv = CopyInstallDirToDestDir();
#endif
    }

    if (rv == OK) {
      rv = DoUpdate();
      gArchiveReader.Close();
      NS_tchar updatingDir[MAXPATHLEN];
      NS_tsnprintf(updatingDir, sizeof(updatingDir) / sizeof(updatingDir[0]),
                   NS_T("%s/updating"), gWorkingDirPath);
      ensure_remove_recursive(updatingDir);
    }
  }

  if (rv && (sReplaceRequest || sStagedUpdate)) {
    ensure_remove_recursive(gWorkingDirPath);
    // When attempting to replace the application, we should fall back
    // to non-staged updates in case of a failure.  We do this by
    // setting the status to pending, exiting the updater, and
    // launching the callback application.  The callback application's
    // startup path will see the pending status, and will start the
    // updater application again in order to apply the update without
    // staging.
    if (sReplaceRequest) {
      WriteStatusFile(sElevationType == ElevationType::ElevatedByMMS
                          ? "pending-service"
                          : "pending");
    } else {
      WriteStatusFile(rv);
    }
    LOG(("failed: %d", rv));
#ifdef TEST_UPDATER
    // Some tests need to use --test-process-updates again.
    putenv(const_cast<char*>("MOZ_TEST_PROCESS_UPDATES="));
#endif
  } else {
#ifdef TEST_UPDATER
    const char* forceErrorCodeString = getenv("MOZ_FORCE_ERROR_CODE");
    if (forceErrorCodeString && *forceErrorCodeString) {
      rv = atoi(forceErrorCodeString);
    }
#endif
    if (rv) {
      LOG(("failed: %d", rv));
    } else {
      // If the update was successful we need to update the timestamp on the
      // top-level Mac OS X bundle directory so that Mac OS X's Launch Services
      // picks up any major changes when the bundle is updated.
      if (!sStagedUpdate && utimes(gInstallDirPath, nullptr) != 0) {
        LOG(("Couldn't set access/modification time on application bundle."));
      }
      if (!sStagedUpdate) {
        WriteUpdateTelemetry(gInstallDirPath);
      }
      LOG(("succeeded"));
    }
    WriteStatusFile(rv);
  }

  LOG(("calling QuitProgressUI"));
  QuitProgressUI();
}

static void ServeElevatedUpdateThreadFunc(void* param) {
  UpdateServerThreadArgs* threadArgs = (UpdateServerThreadArgs*)param;
  gSucceeded = ServeElevatedUpdate(threadArgs->argc, threadArgs->argv,
                                   threadArgs->marChannelID);
  if (!gSucceeded) {
    WriteStatusFile(ELEVATION_CANCELED);
  }
  QuitProgressUI();
}

void freeArguments(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    free(argv[i]);
  }
  free(argv);
}

int LaunchCallbackAndPostProcessApps(int argc, NS_tchar** argv
                                     ,
                                     mozilla::UniquePtr<UmaskContext>
                                         umaskContext
) {
  // We want to make sure to call `output_finish` before we leave this function
  // and, if we end up launching the callback app, we want to call it before
  // we do that (so that the callback app can operate on the output).
  // But we want to do this as late as possible to make the log as detailed as
  // possible.
  class RaiiOutputFinish {
   public:
    RaiiOutputFinish() : mCalled(false) {}
    ~RaiiOutputFinish() { call(); }
    void call() {
      if (!mCalled) {
        mCalled = true;
        output_finish();
      }
    }

   private:
    bool mCalled;
  } raii_output_finish;

  umaskContext.reset();

  if (argc > kCallbackIndex) {
    if (gInvocation == UpdaterInvocation::First) {
      if (gSucceeded) {
        LOG(("Launching macOS post update process"));
        LaunchMacPostProcess(gInstallDirPath);
      } else {
        LOG(("Not launching macOS post update process because !gSucceeded"));
      }

    raii_output_finish.call();
    LaunchCallbackApp(argv[kCallbackWorkingDirIndex], argc - kCallbackIndex,
                      argv + kCallbackIndex,
                      sElevationType == ElevationType::ElevatedByMMS);
  } else {  // isElevationTypeElevated(sElevationType)
    LOG(
        ("This is the second instance. Skipping LaunchMacPostProcess and "
         "LaunchCallbackApp"));
  }
}
else {
  LOG(
      ("No callback arg. Skipping LaunchWinPostProcess and "
       "LaunchCallbackApp"));
}
return 0;
}

bool ShouldRunSilently(int argc, NS_tchar** argv) {
#ifdef MOZ_BACKGROUNDTASKS
  // If the callback has a --backgroundtask switch, consider it a background
  // task. The CheckArg semantics aren't reproduced in full here,
  // there's e.g. no check for a parameter and no case-insensitive comparison.
  for (int i = 1; i < argc; ++i) {
    if (const auto option = mozilla::internal::ReadAsOption(argv[i])) {
      const NS_tchar* arg = option.value();
      if (NS_tstrcmp(arg, NS_T("backgroundtask")) == 0) {
        return true;
      }
    }
  }
#endif  // MOZ_BACKGROUNDTASKS

  if (EnvHasValue("MOZ_APP_SILENT_START")) {
    return true;
  }

  return false;
}

int NS_main(int argc, NS_tchar** argv) {
  LogToOS(NS_T("Updater started"));

  // We may need to tweak our argument list when we launch the Second Updater
  // Invocation (SUI), so we are going to make a copy of our arguments to
  // modify.
  int suiArgc = argc;
  mozilla::UniquePtr<const NS_tchar*[]> suiArgv =
      mozilla::MakeUnique<const NS_tchar*[]>(suiArgc);
  for (int argIndex = 0; argIndex < suiArgc; argIndex++) {
    suiArgv.get()[argIndex] = argv[argIndex];
  }

  sElevationType = getElevationType(argc, argv);
  if (sElevationType == ElevationType::Error ||
      sElevationType == ElevationType::Unknown) {
    fprintf(stderr, "Can't determine elevation state. Exiting.\n");
    return 1;
  }

  if (argc == 2 && NS_tstrcmp(argv[1], NS_T("--channels-allowed")) == 0) {
#ifdef MOZ_VERIFY_MAR_SIGNATURE
    int rv = PopulategMARStrings();
    if (rv == OK) {
      printf("Channels Allowed: '%s'\n", gMARStrings.MARChannelID.get());
      return 0;
    }
    printf("Error: %d\n", rv);
    return 1;
#else
      printf("Not Applicable: No support for signature verification\n");
      return 0;
#endif
  }

  // `isDMGInstall` is only ever true for macOS, but we are declaring it here
  // to avoid a ton of extra #ifdef's.
  bool isDMGInstall = false;

  if (argc > 2 && NS_tstrcmp(argv[1], NS_T("--openAppBundle")) == 0) {
    LogToOS(NS_T("Opening App Bundle"));
    // We have been asked to open a .app bundle:
    //
    //   --openAppBundle [--wait-pid <pid>] <app bundle> [arguments...]
    //
    // The optional pid belongs to the process we are replacing. Drop the
    // arguments we consume here and launch the .app bundle with the rest.
    int appIndex = 2;
    pid_t waitForPid = 0;
    if (argc > 4 && NS_tstrcmp(argv[2], NS_T("--wait-pid")) == 0) {
      waitForPid = static_cast<pid_t>(NS_tatoi(argv[3]));
      appIndex = 4;
    }
    LaunchMacApp(argc - appIndex, (const char**)argv + appIndex, waitForPid);
    return 0;
  }

  // We want to control file permissions explicitly, or else we could end up
  // corrupting installs for other users on the system. Accordingly, set the
  // umask to 0 for all file creations below and reset it on exit. See Bug
  // 1337007
  mozilla::UniquePtr<UmaskContext> umaskContext(new UmaskContext(0));

  if (isElevationTypeElevated(sElevationType)) {
    LogToOS(NS_T("Updater is elevated"));
    if (!ObtainUpdaterArguments(&argc, &argv, &gMARStrings)) {
      // Won't actually get here because ObtainUpdaterArguments will terminate
      // the current process on failure.
      return 1;
    }
  }

  if (argc == 4 && (strstr(argv[1], "-dmgInstall") != nullptr)) {
    isDMGInstall = true;
    if (isElevationTypeElevated(sElevationType)) {
      freeArguments(argc, argv);
      CleanupElevatedMacUpdate(true);
      return 0;
    }
  }

  if (!isDMGInstall) {
    // Skip update-related code path for DMG installs.

#if defined(MOZ_VERIFY_MAR_SIGNATURE) && defined(MAR_NSS)
    // If using NSS for signature verification, initialize NSS but minimize
    // the portion we depend on by avoiding all of the NSS databases.
    if (NSS_NoDB_Init(nullptr) != SECSuccess) {
      PRErrorCode error = PR_GetError();
      fprintf(stderr, "Could not initialize NSS: %s (%d)",
              PR_ErrorToName(error), (int)error);
      _exit(1);
    }
#endif

    // To process an update the updater command line must at a minimum have the
    // argument version as the first argument, the directory path containing the
    // updater.mar file to process as the second argument, the install directory
    // as the third argument, the directory to apply the update to as the fourth
    // argument, and which updater invocation this is as the fifth argument.
    // When the updater is launched by another process, the PID of the parent
    // process should be provided in the optional sixth argument and the updater
    // will wait on the parent process to exit if the value is non-zero and the
    // process is present. This is necessary due to not being able to update
    // files that are in use on Windows. The optional seventh argument is the
    // callback's working directory and the optional eighth argument is the
    // callback path. The callback is the application to launch after updating
    // and it will be launched when these arguments are provided whether the
    // update was successful or not. All remaining arguments are optional and
    // are passed to the callback when it is launched.
    if (argc < kWaitPidIndex) {
      fprintf(stderr,
              "Usage: updater arg-version patch-dir install-dir apply-to-dir "
              "which-invocation [wait-pid [callback-working-dir callback-path "
              "args...]]\n");
      if (isElevationTypeElevated(sElevationType)) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
      return 1;
    }


    gInvocation = getUpdaterInvocationFromArg(argv[kWhichInvocationIndex]);
    switch (gInvocation) {
      case UpdaterInvocation::First:
        // We are in the first invocation, which means the next will be the
        // second invocation.
        suiArgv.get()[kWhichInvocationIndex] = secondUpdateInvocationArg;
        break;
      case UpdaterInvocation::Second:
        // We are in the second invocation. Ensure that we don't start a
        // third invocation.
        suiArgv.get()[kWhichInvocationIndex] =
            NS_T("SHOULD_NOT_CALL_THIRD_INSTANCE");
        break;
      case UpdaterInvocation::Error:
        fprintf(stderr, "Error invocation of updater.exe\n");
        return 1;
      default:
        fprintf(stderr, "Unknown invocation of updater.exe\n");
        return 1;
    }
  } else { /* else if (isDMGInstall) */
    // We already exited in the other case.
    gInvocation = UpdaterInvocation::First;
  }
  if (!isValidInvocationForElevationType(gInvocation, sElevationType)) {
    fprintf(stderr, "Error: invocation <%s> not valid with elevation type %s\n",
            updaterInvocationToString(gInvocation),
            elevationTypeToString(sElevationType));
    return 1;
  }

  // The directory containing the update information.
  NS_tstrncpy(gPatchDirPath, argv[kPatchDirIndex], MAXPATHLEN);
  gPatchDirPath[MAXPATHLEN - 1] = NS_T('\0');

  if (!isDMGInstall) {
    // This check is also performed in workmonitor.cpp since the maintenance
    // service can be called directly.
    if (!IsValidFullPath(argv[kPatchDirIndex])) {
      // Since the status file is written to the patch directory and the patch
      // directory is invalid don't write the status file.
      fprintf(stderr,
              "The patch directory path is not valid for this "
              "application (" LOG_S ")\n",
              argv[kPatchDirIndex]);
      if (isElevationTypeElevated(sElevationType)) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
      return 1;
    }

    // This check is also performed in workmonitor.cpp since the maintenance
    // service can be called directly.
    if (!IsValidFullPath(argv[kInstallDirIndex])) {
      WriteStatusFile(INVALID_INSTALL_DIR_PATH_ERROR);
      fprintf(stderr,
              "The install directory path is not valid for this "
              "application (" LOG_S ")\n",
              argv[kInstallDirIndex]);
      if (isElevationTypeElevated(sElevationType)) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
      return 1;
    }

  }  // if (!isDMGInstall)

  // The directory we're going to update to.
  // We copy this string because we need to remove trailing slashes.  The C++
  // standard says that it's always safe to write to strings pointed to by argv
  // elements, but I don't necessarily believe it.
  NS_tstrncpy(gInstallDirPath, argv[kInstallDirIndex], MAXPATHLEN);
  gInstallDirPath[MAXPATHLEN - 1] = NS_T('\0');
  NS_tchar* slash = NS_tstrrchr(gInstallDirPath, NS_SLASH);
  if (slash && !slash[1]) {
    *slash = NS_T('\0');
  }


  if (!isDMGInstall) {

  }  // if (!isDMGInstall)

  // If there is a PID specified and it is not '0' then wait for the process to
  // exit.
  NS_tpid pid = 0;
  if (argc > kWaitPidIndex) {
    pid = NS_tatoi(argv[kWaitPidIndex]);
    if (pid == -1) {
      // This is a signal from the parent process that the updater should stage
      // the update.
      sStagedUpdate = true;
    } else if (NS_tstrstr(argv[kWaitPidIndex], NS_T("/replace"))) {
      // We're processing a request to replace the application with a staged
      // update.
      sReplaceRequest = true;
    }
    if (pid > 0) {
      // Remember the caller so that we can wait for it to go away before we
      // relaunch the application on its behalf.
      gCallbackWaitPid = static_cast<pid_t>(pid);
    }
  }

  if (!isDMGInstall) {
    // This check is also performed in workmonitor.cpp since the maintenance
    // service can be called directly.
    if (!IsValidFullPath(argv[kApplyToDirIndex])) {
      WriteStatusFile(INVALID_WORKING_DIR_PATH_ERROR);
      fprintf(stderr,
              "The working directory path is not valid for this "
              "application (" LOG_S ")\n",
              argv[kApplyToDirIndex]);
      if (isElevationTypeElevated(sElevationType)) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
      return 1;
    }
    // The directory we're going to update to.
    // We copy this string because we need to remove trailing slashes.  The C++
    // standard says that it's always safe to write to strings pointed to by
    // argv elements, but I don't necessarily believe it.
    NS_tstrncpy(gWorkingDirPath, argv[kApplyToDirIndex], MAXPATHLEN);
    gWorkingDirPath[MAXPATHLEN - 1] = NS_T('\0');
    slash = NS_tstrrchr(gWorkingDirPath, NS_SLASH);
    if (slash && !slash[1]) {
      *slash = NS_T('\0');
    }

    if (argc > kCallbackIndex) {
      if (!IsValidFullPath(argv[kCallbackIndex])) {
        WriteStatusFile(INVALID_CALLBACK_PATH_ERROR);
        fprintf(stderr,
                "The callback file path is not valid for this "
                "application (" LOG_S ")\n",
                argv[kCallbackIndex]);
        if (isElevationTypeElevated(sElevationType)) {
          freeArguments(argc, argv);
          CleanupElevatedMacUpdate(true);
        }
        return 1;
      }

      size_t len = NS_tstrlen(gInstallDirPath);
      NS_tchar callbackInstallDir[MAXPATHLEN] = {NS_T('\0')};
      NS_tstrncpy(callbackInstallDir, argv[kCallbackIndex], len);
      if (NS_tstrcmp(gInstallDirPath, callbackInstallDir) != 0) {
        WriteStatusFile(INVALID_CALLBACK_DIR_ERROR);
        fprintf(stderr,
                "The callback file must be located in the "
                "installation directory (" LOG_S ")\n",
                argv[kCallbackIndex]);
        if (isElevationTypeElevated(sElevationType)) {
          freeArguments(argc, argv);
          CleanupElevatedMacUpdate(true);
        }
        return 1;
      }

      sUpdateSilently =
          ShouldRunSilently(argc - kCallbackIndex, argv + kCallbackIndex);
    }

  }  // if (!isDMGInstall)

  if (!sUpdateSilently && !isDMGInstall
      && !isElevationTypeElevated(sElevationType)
  ) {
    InitProgressUI(&argc, &argv);
  }

  if (!isElevationTypeElevated(sElevationType) &&
      (!IsRecursivelyWritable(argv[kInstallDirIndex]) || isDMGInstall)) {
    // If the app directory isn't recursively writeable or if this is a DMG
    // install, an elevated helper process is required.
    if (sUpdateSilently) {
      // An elevated update always requires an elevation dialog, so if we are
      // updating silently, don't do an elevated update.
      // This means that we cannot successfully perform silent updates from
      // non-admin accounts on a Mac.
      // It also means that we cannot silently perform the first update by an
      // admin who was not the installing user. Once the first update has been
      // installed, the permissions of the installation directory should be
      // changed such that we don't need to elevate in the future.
      // Firefox shouldn't actually launch the updater at all in this case. This
      // is defense in depth.
      WriteStatusFile(SILENT_UPDATE_NEEDED_ELEVATION_ERROR);
      fprintf(stderr,
              "Skipping update to avoid elevation prompt from silent update.");
    } else {
      UpdateServerThreadArgs threadArgs;
      threadArgs.argc = suiArgc;
      threadArgs.argv = suiArgv.get();
      threadArgs.marChannelID = "";

#  ifdef MOZ_VERIFY_MAR_SIGNATURE
      // Try to populate gMARStrings so that we can pass the resulting MAR
      // channel ID to the elevated updater via IPC. If this fails (observed on
      // some macOS standard-profile elevated updates where the unelevated
      // updater cannot resolve the weak UpdateSettingsGetAcceptedMARChannels
      // symbol from UpdateSettings.framework), proceed with an empty channel
      // ID rather than aborting the elevated update.
      // ArchiveReader::VerifyProductInformation skips the channel-match check
      // when the channel ID is empty; the MAR's cryptographic signature is
      // still verified, preserving the security posture that existed prior to
      // bug 2028575.
      if (PopulategMARStrings() == OK) {
        threadArgs.marChannelID = gMARStrings.MARChannelID.get();
      } else {
        fprintf(stderr,
                "Unable to retrieve MAR channels in unelevated updater; "
                "proceeding with elevation using an empty channel ID.\n");
      }
#  endif  // MOZ_VERIFY_MAR_SIGNATURE

      Thread t1;
      if (t1.Run(ServeElevatedUpdateThreadFunc, &threadArgs) == 0) {
        // Show an indeterminate progress bar while an elevated update is in
        // progress.
        if (!isDMGInstall) {
          ShowProgressUI(true);
        }
      }
      t1.Join();
    }

    LaunchCallbackAndPostProcessApps(argc, argv, std::move(umaskContext));
    return gSucceeded ? 0 : 1;
  }


  if (!isDMGInstall) {
    NS_tchar logFilePath[MAXPATHLEN + 1] = {L'\0'};
      NS_tsnprintf(logFilePath, sizeof(logFilePath) / sizeof(logFilePath[0]),
                   NS_T("%s/%s"), gPatchDirPath, UpdateLogFilename());
    LogInit(logFilePath);

    LOG(("sElevationType == ElevationType::ElevatedByMMS=%s",
         sElevationType == ElevationType::ElevatedByMMS ? "true" : "false"));
    LOG(("sUpdateSilently=%s", sUpdateSilently ? "true" : "false"));
    LOG(("isElevationTypeElevated(sElevationType)=%s",
         isElevationTypeElevated(sElevationType) ? "true" : "false"));
    LOG(("gInvocation=%s", updaterInvocationToString(gInvocation)));

    if (!WriteStatusFile("applying")) {
      LOG(("failed setting status to 'applying'"));
      if (isElevationTypeElevated(sElevationType)) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
      output_finish();
      return 1;
    }

    if (sStagedUpdate) {
      LOG(("Performing a staged update"));
    } else if (sReplaceRequest) {
      LOG(("Performing a replace request"));
    }

    LOG(("PATCH DIRECTORY " LOG_S, gPatchDirPath));
    LOG(("INSTALLATION DIRECTORY " LOG_S, gInstallDirPath));
    LOG(("WORKING DIRECTORY " LOG_S, gWorkingDirPath));




    if (sStagedUpdate) {
#ifdef TEST_UPDATER
      // This allows testing that the correct UI after an update staging failure
      // that falls back to applying the update on startup. It is simulated due
      // to the difficulty of creating the conditions for this type of staging
      // failure.
      if (EnvHasValue("MOZ_TEST_STAGING_ERROR")) {
        // WRITE_ERROR is one of the cases where the staging failure falls back
        // to applying the update on startup.
        WriteStatusFile(WRITE_ERROR);
        output_finish();
        return 0;
      }
#endif
      // When staging updates, blow away the old installation directory and
      // create it from scratch.
      ensure_remove_recursive(gWorkingDirPath);
    }
    if (!sReplaceRequest) {
      // Try to create the destination directory if it doesn't exist
      int rv = NS_tmkdir(gWorkingDirPath, 0755);
      if (rv != OK && errno != EEXIST) {
        if (isElevationTypeElevated(sElevationType)) {
          freeArguments(argc, argv);
          CleanupElevatedMacUpdate(true);
        }
        output_finish();
        return 1;
      }
    }


    // Run update process on a background thread. ShowProgressUI may return
    // before QuitProgressUI has been called, so wait for UpdateThreadFunc to
    // terminate. Avoid showing the progress UI when staging an update, or if
    // this is an elevated process on macOS.
    Thread t;
    if (t.Run(UpdateThreadFunc, nullptr) == 0) {
      if (!sStagedUpdate && !sReplaceRequest && !sUpdateSilently
          && !isElevationTypeElevated(sElevationType)
      ) {
        ShowProgressUI();
      }
    }
    t.Join();


  }  // if (!isDMGInstall)

  if (isElevationTypeElevated(sElevationType)) {
    SetGroupOwnershipAndPermissions(gInstallDirPath);
    freeArguments(argc, argv);
    CleanupElevatedMacUpdate(false);
  } else if (IsOwnedByGroupAdmin(gInstallDirPath)) {
    // If the group ownership of the Firefox .app bundle was set to the "admin"
    // group during a previous elevated update, we need to ensure that all files
    // in the bundle have group ownership of "admin" as well as write permission
    // for the group to not break updates in the future.
    SetGroupOwnershipAndPermissions(gInstallDirPath);
  }

  LOG(("Running LaunchCallbackAndPostProcessApps"));

  int retVal = LaunchCallbackAndPostProcessApps(argc, argv
                                                  ,
                                                  std::move(umaskContext)
  );

  return retVal ? retVal : (gSucceeded ? 0 : 1);
}

class ActionList {
 public:
  ActionList() : mFirst(nullptr), mLast(nullptr), mCount(0) {}
  ~ActionList();

  void Append(Action* action);
  int Prepare();
  int Draft();
  int Execute();
  void Finish(int status);

 private:
  Action* mFirst;
  Action* mLast;
  int mCount;
};

ActionList::~ActionList() {
  Action* a = mFirst;
  while (a) {
    Action* b = a;
    a = a->mNext;
    delete b;
  }
}

void ActionList::Append(Action* action) {
  if (mLast) {
    mLast->mNext = action;
  } else {
    mFirst = action;
  }

  action->mPrev = mLast;
  mLast = action;
  mCount++;
}

int ActionList::Prepare() {
  // If the action list is empty then we should fail in order to signal that
  // something has gone wrong. Otherwise we report success when nothing is
  // actually done. See bug 327140.
  if (mCount == 0) {
    LOG(("empty action list"));
    return MAR_ERROR_EMPTY_ACTION_LIST;
  }

  Action* a = mFirst;
  int i = 0;
  while (a) {
    int rv = a->Prepare();
    if (rv) {
      return rv;
    }

    float percent = float(++i) / float(mCount);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

int ActionList::Draft() {
  int currentProgress = 0, maxProgress = 0;
  Action* a = mFirst;
  while (a) {
    maxProgress += a->mProgressCost;
    a = a->mNext;
  }

  a = mFirst;
  while (a) {
    int rv = a->Draft();
    if (rv) {
      LOG(("### draft failed"));
      return rv;
    }

    currentProgress += a->mProgressCost;
    float percent = float(currentProgress) / float(maxProgress);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE + PROGRESS_DRAFT_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

int ActionList::Execute() {
  int currentProgress = 0, maxProgress = 0;
  Action* a = mFirst;
  while (a) {
    maxProgress += a->mProgressCost;
    a = a->mNext;
  }

  a = mFirst;
  while (a) {
    int rv = a->Execute();
    if (rv) {
      LOG(("### execution failed"));
      return rv;
    }

    currentProgress += a->mProgressCost;
    float percent = float(currentProgress) / float(maxProgress);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE + PROGRESS_DRAFT_SIZE +
                     PROGRESS_EXECUTE_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

void ActionList::Finish(int status) {
  // On failure, rolling back requires unwinding the executed actions in
  // reverse order to correctly handle multiple actions touching the same path.
  // This is unsupported in the general case, but tolerated if the first action
  // is a REMOVEFILE and the second an ADD like in non-staged complete updates.
  // On success the order is preserved because RemoveDir removes directories
  // here and needs to visit children before their parents.
  Action* a = status == OK ? mFirst : mLast;
  int i = 0;
  while (a) {
    a->Finish(status);

    float percent = float(++i) / float(mCount);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE + PROGRESS_DRAFT_SIZE +
                     PROGRESS_EXECUTE_SIZE + PROGRESS_FINISH_SIZE * percent);

    a = status == OK ? a->mNext : a->mPrev;
  }

  if (status == OK) {
    gSucceeded = true;
  }
}

#if   defined(HAVE_FTS_H)
  int add_dir_entries(const NS_tchar* dirpath, ActionList* list) {
    int rv = OK;
    FTS* ftsdir;
    FTSENT* ftsdirEntry;
    mozilla::UniquePtr<NS_tchar[]> searchpath(get_full_path(dirpath));

    // Remove the trailing slash so the paths don't contain double slashes. The
    // existence of the slash has already been checked in DoUpdate.
    searchpath[NS_tstrlen(searchpath.get()) - 1] = NS_T('\0');
    char* const pathargv[] = {searchpath.get(), nullptr};

    // FTS_NOCHDIR is used so relative paths from the destination directory are
    // returned.
    if (!(ftsdir = fts_open(pathargv,
                            FTS_PHYSICAL | FTS_NOSTAT | FTS_XDEV | FTS_NOCHDIR,
                            nullptr))) {
      return UNEXPECTED_FILE_OPERATION_ERROR;
    }

    while ((ftsdirEntry = fts_read(ftsdir)) != nullptr) {
      NS_tchar foundpath[MAXPATHLEN];
      NS_tchar* quotedpath = nullptr;
      mozilla::UniquePtr<Action> action;

      switch (ftsdirEntry->fts_info) {
        // Filesystem objects that shouldn't be in the application's directories
        case FTS_SL:
        case FTS_SLNONE:
        case FTS_DEFAULT:
          LOG(("add_dir_entries: found a non-standard file: " LOG_S,
               ftsdirEntry->fts_path));
          // Fall through and try to remove as a file
          [[fallthrough]];

        // Files
        case FTS_F:
        case FTS_NSOK:
          // Add the file to be removed to the ActionList.
          NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                       NS_T("%s"), ftsdirEntry->fts_accpath);
          quotedpath = get_quoted_path(get_relative_path(foundpath));
          if (!quotedpath) {
            rv = UPDATER_QUOTED_PATH_MEM_ERROR;
            break;
          }
          action = std::make_unique<RemoveFile>();
          rv = action->Parse(quotedpath);
          free(quotedpath);
          if (!rv) {
            list->Append(action.release());
          }
          break;

        // Directories
        case FTS_DP:
          // Add the directory to be removed to the ActionList.
          NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                       NS_T("%s/"), ftsdirEntry->fts_accpath);
          quotedpath = get_quoted_path(get_relative_path(foundpath));
          if (!quotedpath) {
            rv = UPDATER_QUOTED_PATH_MEM_ERROR;
            break;
          }

          action = std::make_unique<RemoveDir>();
          rv = action->Parse(quotedpath);
          free(quotedpath);
          if (!rv) {
            list->Append(action.release());
          }
          break;

        // Errors
        case FTS_DNR:
        case FTS_NS:
          // ENOENT is an acceptable error for FTS_DNR and FTS_NS and means that
          // we're racing with ourselves. Though strange, the entry will be
          // removed anyway.
          if (ENOENT == ftsdirEntry->fts_errno) {
            rv = OK;
            break;
          }
          [[fallthrough]];

        case FTS_ERR:
          rv = UNEXPECTED_FILE_OPERATION_ERROR;
          LOG(("add_dir_entries: fts_read() error: " LOG_S ", err: %d",
               ftsdirEntry->fts_path, ftsdirEntry->fts_errno));
          break;

        case FTS_DC:
          rv = UNEXPECTED_FILE_OPERATION_ERROR;
          LOG(("add_dir_entries: fts_read() returned FT_DC: " LOG_S,
               ftsdirEntry->fts_path));
          break;

        default:
          // FTS_D is ignored and FTS_DP is used instead (post-order).
          rv = OK;
          break;
      }

      if (rv != OK) {
        break;
      }
    }

    fts_close(ftsdir);

    return rv;
  }

#else

int add_dir_entries(const NS_tchar* dirpath, ActionList* list) {
  int rv = OK;
  NS_tchar foundpath[PATH_MAX];
  struct {
    dirent dent_buffer;
    char chars[NAME_MAX];
  } ent_buf;
  struct dirent* ent;
  mozilla::UniquePtr<NS_tchar[]> searchpath(get_full_path(dirpath));

  DIR* dir = opendir(searchpath.get());
  if (!dir) {
    LOG(("add_dir_entries error on opendir: " LOG_S ", err: %d",
         searchpath.get(), errno));
    return UNEXPECTED_FILE_OPERATION_ERROR;
  }

  while (readdir_r(dir, (dirent*)&ent_buf, &ent) == 0 && ent) {
    if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
      continue;
    }

    NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                 NS_T("%s%s"), searchpath.get(), ent->d_name);
    struct stat64 st_buf;
    int test = stat64(foundpath, &st_buf);
    if (test) {
      closedir(dir);
      return UNEXPECTED_FILE_OPERATION_ERROR;
    }
    if (S_ISDIR(st_buf.st_mode)) {
      NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                   NS_T("%s%s/"), dirpath, ent->d_name);
      // Recurse into the directory.
      rv = add_dir_entries(foundpath, list);
      if (rv) {
        LOG(("add_dir_entries error: " LOG_S ", err: %d", foundpath, rv));
        closedir(dir);
        return rv;
      }
    } else {
      // Add the file to be removed to the ActionList.
      NS_tchar* quotedpath = get_quoted_path(get_relative_path(foundpath));
      if (!quotedpath) {
        closedir(dir);
        return PARSE_ERROR;
      }

      mozilla::UniquePtr<Action> action(new RemoveFile());
      rv = action->Parse(quotedpath);
      if (rv) {
        LOG(("add_dir_entries Parse error on recurse: " LOG_S ", err: %d",
             quotedpath, rv));
        free(quotedpath);
        closedir(dir);
        return rv;
      }
      free(quotedpath);

      list->Append(action.release());
    }
  }
  closedir(dir);

  // Add the directory to be removed to the ActionList.
  NS_tchar* quotedpath = get_quoted_path(get_relative_path(dirpath));
  if (!quotedpath) {
    return PARSE_ERROR;
  }

  mozilla::UniquePtr<Action> action(new RemoveDir());
  rv = action->Parse(quotedpath);
  if (rv) {
    LOG(("add_dir_entries Parse error on close: " LOG_S ", err: %d", quotedpath,
         rv));
  } else {
    list->Append(action.release());
  }
  free(quotedpath);

  return rv;
}

#endif

/*
 * Gets the contents of an update manifest file. The return value is malloc'd
 * and it is the responsibility of the caller to free it.
 *
 * @param  manifest
 *         The full path to the manifest file.
 * @return On success the contents of the manifest and nullptr otherwise.
 */
static NS_tchar* GetManifestContents(const NS_tchar* manifest) {
  AutoFile mfile(NS_tfopen(manifest, NS_T("rb")));
  if (mfile == nullptr) {
    LOG(("GetManifestContents: error opening manifest file: " LOG_S, manifest));
    return nullptr;
  }

  struct stat ms;
  int rv = fstat(fileno((FILE*)mfile), &ms);
  if (rv) {
    LOG(("GetManifestContents: error stating manifest file: " LOG_S, manifest));
    return nullptr;
  }

  char* mbuf = (char*)malloc(ms.st_size + 1);
  if (!mbuf) {
    return nullptr;
  }

  size_t r = ms.st_size;
  char* rb = mbuf;
  while (r) {
    const size_t count = mmin(SSIZE_MAX, r);
    size_t c = fread(rb, 1, count, mfile);
    if (c != count) {
      LOG(("GetManifestContents: error reading manifest file: " LOG_S,
           manifest));
      free(mbuf);
      return nullptr;
    }

    r -= c;
    rb += c;
  }
  *rb = '\0';

  return mbuf;
}

int AddPreCompleteActions(ActionList* list) {
  mozilla::UniquePtr<NS_tchar[]> manifestPath(
      get_full_path(NS_T("Contents/Resources/precomplete")));

  NS_tchar* buf = GetManifestContents(manifestPath.get());
  if (!buf) {
    LOG(
        ("AddPreCompleteActions: error getting contents of precomplete "
         "manifest"));
    // Applications aren't required to have a precomplete manifest. The mar
    // generation scripts enforce the presence of a precomplete manifest.
    return OK;
  }
  NS_tchar* rb = buf;

  int rv;
  NS_tchar* line;
  while ((line = mstrtok(kNL, &rb)) != nullptr) {
    // skip comments
    if (*line == NS_T('#')) {
      continue;
    }

    NS_tchar* token = mstrtok(kWhitespace, &line);
    if (!token) {
      LOG(("AddPreCompleteActions: token not found in manifest"));
      free(buf);
      return PARSE_ERROR;
    }

    Action* action = nullptr;
    if (NS_tstrcmp(token, NS_T("remove")) == 0) {  // rm file
      action = new RemoveFile();
    } else if (NS_tstrcmp(token, NS_T("remove-cc")) ==
               0) {  // no longer supported
      continue;
    } else if (NS_tstrcmp(token, NS_T("rmdir")) == 0) {  // rmdir if  empty
      action = new RemoveDir();
    } else {
      LOG(("AddPreCompleteActions: unknown token: " LOG_S, token));
      free(buf);
      return PARSE_ERROR;
    }

    if (!action) {
      free(buf);
      return BAD_ACTION_ERROR;
    }

    rv = action->Parse(line);
    if (rv) {
      delete action;
      free(buf);
      return rv;
    }

    list->Append(action);
  }

  free(buf);
  return OK;
}

int DoUpdate() {
  NS_tchar manifest[MAXPATHLEN];
  NS_tsnprintf(manifest, sizeof(manifest) / sizeof(manifest[0]),
               NS_T("%s/updating/update.manifest"), gWorkingDirPath);
  ensure_parent_dir(manifest);

  // extract the manifest
  int rv = gArchiveReader.ExtractFile("updatev3.manifest", manifest);
  if (rv) {
    LOG(("DoUpdate: error extracting manifest file"));
    return rv;
  }

  NS_tchar* buf = GetManifestContents(manifest);
  // The manifest is located in the <working_dir>/updating directory which is
  // removed after the update has finished so don't delete it here.
  if (!buf) {
    LOG(("DoUpdate: error opening manifest file: " LOG_S, manifest));
    return READ_ERROR;
  }
  NS_tchar* rb = buf;

#if defined(MOZ_ZUCCHINI)
#  if defined(TEST_UPDATER)
  zucchini::mozilla::TestOptions options;
  options.logDestructorMarker = EnvHasValue("MOZ_TEST_ZUCCHINI_DTOR_MARKER");
  options.triggerBadAlloc = EnvHasValue("MOZ_TEST_ZUCCHINI_BAD_ALLOC");
  options.triggerCheckFailure = EnvHasValue("MOZ_TEST_ZUCCHINI_CHECK_FAILURE");
  zucchini::mozilla::SetTestOptions(options);
#  endif  // TEST_UPDATER

  zucchini::mozilla::SetLogFunction(LogZucchiniMessage);
#endif  // defined(MOZ_ZUCCHINI)

  ActionList list;
  NS_tchar* line;
  bool isFirstAction = true;
  while ((line = mstrtok(kNL, &rb)) != nullptr) {
    // skip comments
    if (*line == NS_T('#')) {
      continue;
    }

    NS_tchar* token = mstrtok(kWhitespace, &line);
    if (!token) {
      LOG(("DoUpdate: token not found in manifest"));
      free(buf);
      return PARSE_ERROR;
    }

    if (isFirstAction) {
      isFirstAction = false;
      // The update manifest isn't required to have a type declaration. The mar
      // generation scripts enforce the presence of the type declaration.
      if (NS_tstrcmp(token, NS_T("type")) == 0) {
        const NS_tchar* type = mstrtok(kQuote, &line);
        LOG(("UPDATE TYPE " LOG_S, type));
        if (NS_tstrcmp(type, NS_T("complete")) == 0) {
          rv = AddPreCompleteActions(&list);
          if (rv) {
            free(buf);
            return rv;
          }
        }
        continue;
      }
    }

    Action* action = nullptr;
    if (NS_tstrcmp(token, NS_T("remove")) == 0) {  // rm file
      action = new RemoveFile();
    } else if (NS_tstrcmp(token, NS_T("rmdir")) == 0) {  // rmdir if empty
      action = new RemoveDir();
    } else if (NS_tstrcmp(token, NS_T("rmrfdir")) == 0) {  // rmdir recursive
      const NS_tchar* reldirpath = mstrtok(kQuote, &line);
      if (!reldirpath) {
        free(buf);
        return PARSE_ERROR;
      }

      if (reldirpath[NS_tstrlen(reldirpath) - 1] != NS_T('/')) {
        free(buf);
        return PARSE_ERROR;
      }

      rv = add_dir_entries(reldirpath, &list);
      if (rv) {
        free(buf);
        return rv;
      }

      continue;
    } else if (NS_tstrcmp(token, NS_T("add")) == 0) {
      action = new AddFile();
    } else if (NS_tstrcmp(token, NS_T("patch")) == 0) {
      action = new PatchFile();
    } else if (NS_tstrcmp(token, NS_T("add-if")) == 0) {  // Add if exists
      action = new AddIfFile();
    } else if (NS_tstrcmp(token, NS_T("add-if-not")) ==
               0) {  // Add if not exists
      action = new AddIfNotFile();
    } else if (NS_tstrcmp(token, NS_T("patch-if")) == 0) {  // Patch if exists
      action = new PatchIfFile();
    } else {
      LOG(("DoUpdate: unknown token: " LOG_S, token));
      free(buf);
      return PARSE_ERROR;
    }

    if (!action) {
      free(buf);
      return BAD_ACTION_ERROR;
    }

    rv = action->Parse(line);
    if (rv) {
      delete action;
      free(buf);
      return rv;
    }

    list.Append(action);
  }

  rv = list.Prepare();
  if (rv) {
    free(buf);
    return rv;
  }

  rv = list.Draft();
  if (rv == OK) {
    rv = list.Execute();
  }

  list.Finish(rv);
  free(buf);
  return rv;
}
