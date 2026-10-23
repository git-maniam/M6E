/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* PR time code. */

#include "vm/Time.h"

#ifdef SOLARIS
#  define _REENTRANT 1
#endif
#include <string.h>
#include <time.h>

#include "jstypes.h"


int64_t js::PRMJ_Now() {
  // `std::timespec_get(..., TIME_UTC)` is available since C++17, but it's
  // slightly slower than `clock_gettime(CLOCK_REALTIME, ...)`, so prefer the
  // latter.

  timespec ts;
  MOZ_ALWAYS_TRUE(clock_gettime(CLOCK_REALTIME, &ts) == 0);

  return int64_t(ts.tv_sec) * PRMJ_USEC_PER_SEC +
         int64_t(ts.tv_nsec / PRMJ_NSEC_PER_USEC);
}


#if !JS_HAS_INTL_API

/* Format a time value into a buffer. Same semantics as strftime() */
size_t js::PRMJ_FormatTime(char* buf, size_t buflen, const char* fmt,
                           const PRMJTime* prtm, int timeZoneYear,
                           int offsetInSeconds) {
  size_t result = 0;
  struct tm a;

  memset(&a, 0, sizeof(struct tm));

  a.tm_sec = prtm->tm_sec;
  a.tm_min = prtm->tm_min;
  a.tm_hour = prtm->tm_hour;
  a.tm_mday = prtm->tm_mday;
  a.tm_mon = prtm->tm_mon;
  a.tm_wday = prtm->tm_wday;

  /*
   * On systems where |struct tm| has members tm_gmtoff and tm_zone, we
   * must fill in those values, or else strftime will return wrong results
   * (e.g., bug 511726, bug 554338).
   */
#    if defined(HAVE_LOCALTIME_R) && defined(HAVE_TM_ZONE_TM_GMTOFF)
  char emptyTimeZoneId[] = "";
  {
    /*
     * Fill out |td| to the time represented by |prtm|, leaving the
     * timezone fields zeroed out. localtime_r will then fill in the
     * timezone fields for that local time according to the system's
     * timezone parameters. Use |timeZoneYear| for the year to ensure the
     * time zone name matches the time zone offset used by the caller.
     */
    struct tm td;
    memset(&td, 0, sizeof(td));
    td.tm_sec = prtm->tm_sec;
    td.tm_min = prtm->tm_min;
    td.tm_hour = prtm->tm_hour;
    td.tm_mday = prtm->tm_mday;
    td.tm_mon = prtm->tm_mon;
    td.tm_wday = prtm->tm_wday;
    td.tm_year = timeZoneYear - 1900;
    td.tm_yday = prtm->tm_yday;
    td.tm_isdst = prtm->tm_isdst;

    time_t t = mktime(&td);

    // If either mktime or localtime_r failed, fill in the fallback time
    // zone offset |offsetInSeconds| and set the time zone identifier to
    // the empty string.
    if (t != static_cast<time_t>(-1) && localtime_r(&t, &td)) {
      a.tm_gmtoff = td.tm_gmtoff;
      a.tm_zone = td.tm_zone;
    } else {
      a.tm_gmtoff = offsetInSeconds;
      a.tm_zone = emptyTimeZoneId;
    }
  }
#    endif

  /*
   * Years before 1900 and after 9999 cause strftime() to abort on Windows.
   * To avoid that we replace it with FAKE_YEAR_BASE + year % 100 and then
   * replace matching substrings in the strftime() result with the real year.
   * Note that FAKE_YEAR_BASE should be a multiple of 100 to make 2-digit
   * year formats (%y) work correctly (since we won't find the fake year
   * in that case).
   */
  constexpr int FAKE_YEAR_BASE = 9900;
  int fake_tm_year = 0;
  if (prtm->tm_year < 1900 || prtm->tm_year > 9999) {
    fake_tm_year = FAKE_YEAR_BASE + prtm->tm_year % 100;
    a.tm_year = fake_tm_year - 1900;
  } else {
    a.tm_year = prtm->tm_year - 1900;
  }
  a.tm_yday = prtm->tm_yday;
  a.tm_isdst = prtm->tm_isdst;

  /*
   * Even with the above, SunOS 4 seems to detonate if tm_zone and tm_gmtoff
   * are null.  This doesn't quite work, though - the timezone is off by
   * tzoff + dst.  (And mktime seems to return -1 for the exact dst
   * changeover time.)
   */


  result = strftime(buf, buflen, fmt, &a);


  if (fake_tm_year && result) {
    char real_year[16];
    char fake_year[16];
    size_t real_year_len;
    size_t fake_year_len;
    char* p;

    sprintf(real_year, "%d", prtm->tm_year);
    real_year_len = strlen(real_year);
    sprintf(fake_year, "%d", fake_tm_year);
    fake_year_len = strlen(fake_year);

    /* Replace the fake year in the result with the real year. */
    for (p = buf; (p = strstr(p, fake_year)); p += real_year_len) {
      size_t new_result = result + real_year_len - fake_year_len;
      if (new_result >= buflen) {
        return 0;
      }
      memmove(p + real_year_len, p + fake_year_len, strlen(p + fake_year_len));
      memcpy(p, real_year, real_year_len);
      result = new_result;
      *(buf + result) = '\0';
    }
  }
  return result;
}
#endif /* !JS_HAS_INTL_API */
