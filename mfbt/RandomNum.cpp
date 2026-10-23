/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "mozilla/RandomNum.h"

#  include <fcntl.h>
#  include <unistd.h>


#if defined(ANDROID) || defined(XP_DARWIN) || defined(__DragonFly__) ||    \
    defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__wasi__)
#  define USE_ARC4RANDOM
#endif


namespace mozilla {

MFBT_API bool GenerateRandomBytesFromOS(void* aBuffer, size_t aLength) {
  MOZ_ASSERT(aBuffer);
  MOZ_ASSERT(aLength > 0);

#if   defined(USE_ARC4RANDOM)  // defined(XP_WIN)

  arc4random_buf(aBuffer, aLength);
  return true;

#else


  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    return false;
  }

  ssize_t bytesRead = read(fd, aBuffer, aLength);

  close(fd);

  return (static_cast<size_t>(bytesRead) == aLength);

#endif
}

MFBT_API Maybe<uint64_t> RandomUint64() {
  uint64_t randomNum;
  if (!GenerateRandomBytesFromOS(&randomNum, sizeof(randomNum))) {
    return Nothing();
  }

  return Some(randomNum);
}

MFBT_API uint64_t RandomUint64OrDie() {
  uint64_t randomNum;
  MOZ_RELEASE_ASSERT(GenerateRandomBytesFromOS(&randomNum, sizeof(randomNum)));
  return randomNum;
}

}  // namespace mozilla
