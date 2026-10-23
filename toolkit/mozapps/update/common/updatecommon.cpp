/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "updatedefines.h"

#  include <os/log.h>

#include <stdio.h>
#include <stdarg.h>

#include "updatecommon.h"

void UpdateLog::Init(NS_tchar* logFilePath) {
  if (logFP) {
    return;
  }

  // When the path is over the length limit disable logging by not opening the
  // file and not setting logFP.
  int dstFilePathLen = NS_tstrlen(logFilePath);
  if (dstFilePathLen > 0 && dstFilePathLen < MAXPATHLEN - 1) {
    NS_tstrncpy(mDstFilePath, logFilePath, MAXPATHLEN);
    logFP = CreateAndOpenFile(mDstFilePath, false);
    if (logFP == nullptr) {
      LogToOS(NS_T("Failed to create FILE*"));
    }
  }
}

void UpdateLog::Finish() {
  if (!logFP) {
    return;
  }


  fclose(logFP);
  logFP = nullptr;
}

void UpdateLog::Flush() {
  if (!logFP) {
    return;
  }

  fflush(logFP);
}

void UpdateLog::PrintTimestampPrefix() {
  if (!logFP) {
    return;
  }

  time_t rawtime = time(nullptr);
  struct tm* timeinfo = localtime(&rawtime);

  if (nullptr != timeinfo) {
    // attempt to format the time similar to rfc-3339 so that it works with
    // sort(1). xxxx-xx-xx xx:xx:xx+xxxx -> 24 chars + 1 NUL
    const size_t buffer_size = 25;
    char buffer[buffer_size] = {0};

    if (0 == strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S%z", timeinfo)) {
      buffer[0] = '\0';  // reset buffer into a defined state and try posix ts
      if (0 > snprintf(buffer, buffer_size, "%d", (int)mktime(timeinfo))) {
        buffer[0] = '\0';  // reset and give up
      }
    }

    fprintf(logFP, "%s: ", buffer);
  }
}

void UpdateLog::Printf(const char* fmt, ...) {
  if (!logFP) {
    return;
  }

  PrintTimestampPrefix();

  va_list ap;
  va_start(ap, fmt);
  vfprintf(logFP, fmt, ap);
  va_end(ap);

  fprintf(logFP, "\n");
  // When the updater crashes on Windows the log file won't be flushed and this
  // can make it easier to debug what is going on.
  fflush(logFP);
}

void UpdateLog::WarnPrintf(const char* fmt, ...) {
  if (!logFP) {
    return;
  }

  PrintTimestampPrefix();

  va_list ap;
  va_start(ap, fmt);
  fprintf(logFP, "*** Warning: ");
  vfprintf(logFP, fmt, ap);
  fprintf(logFP, "***\n");
  va_end(ap);
  // When the updater crashes on Windows the log file won't be flushed and this
  // can make it easier to debug what is going on.
  fflush(logFP);
}

/**
 * Creates and opens a file for logging in read/write mode.
 *
 * @param filePath  The path of the log file.
 * @param binary  If the file should be opened as binary (ignored on
 * macOS/Linux).
 * @return a pointer to the FILE struct or nullptr on error.
 */
FILE* CreateAndOpenFile(NS_tchar* filePath, bool binary) {
  LogToOS(NS_T("Opening logfile"));
  NS_tchar* lastSeperator = NS_tstrrchr(filePath, '/');
  if (lastSeperator == nullptr) {
    // No separator, disable logging.
    return nullptr;
  }

  const NS_tchar templateSuffix[] = NS_T("/temp.XXXXXX");
  size_t templateLen = NS_tstrlen(templateSuffix);

  long dirLength = lastSeperator - filePath;
  // +1 to account for the \0 terminator
  if (dirLength < 0 ||
      (unsigned long)dirLength >= MAXPATHLEN - (templateLen + 1)) {
    // Too short, or too long. Disable logging.
    return nullptr;
  }

  NS_tchar tmpFilePath[MAXPATHLEN] = {L'\0'};
  // Use memcpy here to only copy the directory portion of filePath
  // and using strncpy triggered GCC's stringop-truncation error because
  // filePath's \0 terminator wasn't being carried over. This isn't a problem
  // here because the next strncpy adds it.
  memcpy(tmpFilePath, filePath, dirLength);
  NS_tstrncpy(tmpFilePath + dirLength, templateSuffix, MAXPATHLEN - dirLength);

  int fd = mkstemp(tmpFilePath);
  if (fd == -1) {
    LogToOS(NS_T("Failed to open tmp"));
    return nullptr;
  }

  if (rename(tmpFilePath, filePath) == -1) {
    LogToOS(NS_T("Failed to rename"));
    close(fd);
    return nullptr;
  }

  LogToOS(NS_T("Opening file*"));
  return fdopen(fd, "w+");
}


/**
 * Performs checks of a full path for validity for this application.
 *
 * @param  origFullPath
 *         The full path to check.
 * @return true if the path is valid for this application and false otherwise.
 */
bool IsValidFullPath(NS_tchar* origFullPath) {
  // Subtract 1 from MAXPATHLEN for null termination.
  if (NS_tstrlen(origFullPath) > MAXPATHLEN - 1) {
    // The path is longer than acceptable for this application.
    return false;
  }

  // Only allow full paths.
  if (origFullPath[0] != NS_T('/')) {
    return false;
  }

  // The path must not traverse directories
  if (NS_tstrstr(origFullPath, NS_T("/../")) != nullptr) {
    return false;
  }

  // The path shall not have a path traversal suffix
  const NS_tchar invalidSuffix[] = NS_T("/..");
  size_t pathLen = NS_tstrlen(origFullPath);
  size_t invalidSuffixLen = NS_tstrlen(invalidSuffix);
  if (invalidSuffixLen <= pathLen &&
      NS_tstrncmp(origFullPath + pathLen - invalidSuffixLen, invalidSuffix,
                  invalidSuffixLen) == 0) {
    return false;
  }
  return true;
}

// This is never deallocated by the system
MOZ_RUNINIT static os_log_t updaterLogger =
    os_log_create("org.mozilla.updater", "Updater");

/**
 * Logs a message to the system log for debugging purposes before our log
 * file has been set up.
 *
 * @param  message
 *         The message to log.
 */
void LogToOS(const NS_tchar* message) {
  os_log(updaterLogger, "%{public}s", message);
}

bool EnvHasValue(const char* name) {
  const char* val = getenv(name);
  return (val && *val);
}
