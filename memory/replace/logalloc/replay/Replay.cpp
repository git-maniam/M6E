/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define MOZ_MEMORY_IMPL
#include "mozmemory_wrap.h"

#  include <sys/mman.h>
#  include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "mozilla/Assertions.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "FdPrintf.h"

using namespace mozilla;

static void die(const char* message) {
  /* Here, it doesn't matter that fprintf may allocate memory. */
  fprintf(stderr, "%s\n", message);
  exit(1);
}


/* We don't want to be using malloc() to allocate our internal tracking
 * data, because that would change the parameters of what is being measured,
 * so we want to use data types that directly use mmap/VirtualAlloc. */
template <typename T, size_t Len>
class MappedArray {
 public:
  MappedArray() : mPtr(nullptr) {
  }

  ~MappedArray() {
    if (mPtr) {
      munmap(mPtr, sizeof(T) * Len);
    }
  }

  T& operator[](size_t aIndex) const {
    if (mPtr) {
      return mPtr[aIndex];
    }

    size_t data_size = sizeof(T) * Len;
    size_t size = data_size;
    mPtr = reinterpret_cast<T*>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                     MAP_ANON | MAP_PRIVATE, -1, 0));
    if (mPtr == MAP_FAILED) {
      die("Mmap error");
    }
    return mPtr[aIndex];
  }

  bool ownsMapping(uintptr_t addr) const { return addr == (uintptr_t)mPtr; }

  bool allocated() const { return !!mPtr; }

 private:
  mutable T* mPtr;
};

/* Type for records of allocations. */
struct MemSlot {
  void* mPtr;

  // mRequest is only valid if mPtr is non-null.  It doesn't need to be cleared
  // when memory is freed or realloc()ed.
  size_t mRequest;
};

/* An almost infinite list of slots.
 * In essence, this is a linked list of arrays of groups of slots.
 * Each group is 1MB. On 64-bits, one group allows to store 64k allocations.
 * Each MemSlotList instance can store 1023 such groups, which means more
 * than 67M allocations. In case more would be needed, we chain to another
 * MemSlotList, and so on.
 * Using 1023 groups makes the MemSlotList itself page sized on 32-bits
 * and 2 pages-sized on 64-bits.
 */
class MemSlotList {
  static constexpr size_t kGroups = 1024 - 1;
  static constexpr size_t kGroupSize = (1024 * 1024) / sizeof(MemSlot);

  MappedArray<MemSlot, kGroupSize> mSlots[kGroups];
  MappedArray<MemSlotList, 1> mNext;

 public:
  MemSlot& operator[](size_t aIndex) const {
    if (aIndex < kGroupSize * kGroups) {
      return mSlots[aIndex / kGroupSize][aIndex % kGroupSize];
    }
    aIndex -= kGroupSize * kGroups;
    return mNext[0][aIndex];
  }

  // Ask if any of the memory-mapped buffers use this range.
  bool ownsMapping(uintptr_t aStart) const {
    for (const auto& slot : mSlots) {
      if (slot.allocated() && slot.ownsMapping(aStart)) {
        return true;
      }
    }
    return mNext.ownsMapping(aStart) ||
           (mNext.allocated() && mNext[0].ownsMapping(aStart));
  }
};

/* Helper class for memory buffers */
class Buffer {
 public:
  Buffer() : mBuf(nullptr), mLength(0) {}

  Buffer(const void* aBuf, size_t aLength)
      : mBuf(reinterpret_cast<const char*>(aBuf)), mLength(aLength) {}

  /* Constructor for string literals. */
  template <size_t Size>
  explicit Buffer(const char (&aStr)[Size]) : mBuf(aStr), mLength(Size - 1) {}

  /* Returns a sub-buffer up-to but not including the given aNeedle character.
   * The "parent" buffer itself is altered to begin after the aNeedle
   * character.
   * If the aNeedle character is not found, return the entire buffer, and empty
   * the "parent" buffer. */
  Buffer SplitChar(char aNeedle) {
    char* buf = const_cast<char*>(mBuf);
    char* c = reinterpret_cast<char*>(memchr(buf, aNeedle, mLength));
    if (!c) {
      return Split(mLength);
    }

    Buffer result = Split(c - buf);
    // Remove the aNeedle character itself.
    Split(1);
    return result;
  }

  // Advance to the position after aNeedle.  This is like SplitChar but does not
  // return the skipped portion.
  void Skip(char aNeedle, unsigned nTimes = 1) {
    for (unsigned i = 0; i < nTimes; i++) {
      SplitChar(aNeedle);
    }
  }

  void SkipWhitespace() {
    while (mLength > 0) {
      if (!IsSpace(mBuf[0])) {
        break;
      }
      mBuf++;
      mLength--;
    }
  }

  static bool IsSpace(char c) {
    switch (c) {
      case ' ':
      case '\t':
      case '\n':
      case '\v':
      case '\f':
      case '\r':
        return true;
    }
    return false;
  }

  /* Returns a sub-buffer of at most aLength characters. The "parent" buffer is
   * amputated of those aLength characters. If the "parent" buffer is smaller
   * than aLength, then its length is used instead. */
  Buffer Split(size_t aLength) {
    Buffer result(mBuf, std::min(aLength, mLength));
    mLength -= result.mLength;
    mBuf += result.mLength;
    return result;
  }

  /* Move the buffer (including its content) to the memory address of the aOther
   * buffer. */
  void Slide(Buffer aOther) {
    memmove(const_cast<char*>(aOther.mBuf), mBuf, mLength);
    mBuf = aOther.mBuf;
  }

  /* Returns whether the two involved buffers have the same content. */
  bool operator==(Buffer aOther) {
    return mLength == aOther.mLength &&
           (mBuf == aOther.mBuf || !strncmp(mBuf, aOther.mBuf, mLength));
  }

  bool operator!=(Buffer aOther) { return !(*this == aOther); }

  /* Returns true if the buffer is not empty. */
  explicit operator bool() { return mLength; }

  char operator[](size_t n) const { return mBuf[n]; }

  /* Returns the memory location of the buffer. */
  const char* get() { return mBuf; }

  /* Returns the memory location of the end of the buffer (technically, the
   * first byte after the buffer). */
  const char* GetEnd() { return mBuf + mLength; }

  /* Extend the buffer over the content of the other buffer, assuming it is
   * adjacent. */
  void Extend(Buffer aOther) {
    MOZ_ASSERT(aOther.mBuf == GetEnd());
    mLength += aOther.mLength;
  }

  size_t Length() const { return mLength; }

 private:
  const char* mBuf;
  size_t mLength;
};

/* Helper class to read from a file descriptor line by line. */
class FdReader {
 public:
  explicit FdReader(int aFd, bool aNeedClose = false)
      : mFd(aFd),
        mNeedClose(aNeedClose),
        mData(&mRawBuf, 0),
        mBuf(&mRawBuf, sizeof(mRawBuf)) {}

  FdReader(FdReader&& aOther) noexcept
      : mFd(aOther.mFd),
        mNeedClose(aOther.mNeedClose),
        mData(&mRawBuf, 0),
        mBuf(&mRawBuf, sizeof(mRawBuf)) {
    memcpy(mRawBuf, aOther.mRawBuf, sizeof(mRawBuf));
    aOther.forget();
  }

  FdReader& operator=(const FdReader&) = delete;
  FdReader(const FdReader&) = delete;

  FdReader& operator=(FdReader&& aOther) {
    mFd = aOther.mFd;
    mNeedClose = aOther.mNeedClose;
    mData = aOther.mData;
    mBuf = aOther.mBuf;

    aOther.forget();

    return *this;
  }

  ~FdReader() {
    if (mNeedClose) {
      close(mFd);
    }
  }

 private:
  void forget() {
    mFd = -1;
    mNeedClose = false;
    mData = Buffer();
    mBuf = Buffer();
  }

 public:
  /* Read a line from the file descriptor and returns it as a Buffer instance */
  Buffer ReadLine() {
    while (true) {
      Buffer result = mData.SplitChar('\n');

      /* There are essentially three different cases here:
       * - '\n' was found "early". In this case, the end of the result buffer
       *   is before the beginning of the mData buffer (since SplitChar
       *   amputated it).
       * - '\n' was found as the last character of mData. In this case, mData
       *   is empty, but still points at the end of mBuf. result points to what
       *   used to be in mData, without the last character.
       * - '\n' was not found. In this case too, mData is empty and points at
       *   the end of mBuf. But result points to the entire buffer that used to
       *   be pointed by mData.
       * Only in the latter case do both result and mData's end match, and it's
       * the only case where we need to refill the buffer.
       */
      if (result.GetEnd() != mData.GetEnd()) {
        return result;
      }

      /* Since SplitChar emptied mData, make it point to what it had before. */
      mData = result;

      /* And move it to the beginning of the read buffer. */
      mData.Slide(mBuf);

      FillBuffer();

      if (!mData) {
        return Buffer();
      }
    }
  }

 private:
  /* Fill the read buffer. */
  void FillBuffer() {
    size_t size = mBuf.GetEnd() - mData.GetEnd();
    Buffer remainder(mData.GetEnd(), size);

    ssize_t len = 1;
    while (remainder && len > 0) {
      len = ::read(mFd, const_cast<char*>(remainder.get()), size);
      if (len < 0) {
        die("Read error");
      }
      size -= len;
      mData.Extend(remainder.Split(len));
    }
  }

  /* File descriptor to read from. */
  int mFd;
  bool mNeedClose;

  /* Part of data that was read from the file descriptor but not returned with
   * ReadLine yet. */
  Buffer mData;
  /* Buffer representation of mRawBuf */
  Buffer mBuf;
  /* read() buffer */
  char mRawBuf[4096];
};

MOZ_BEGIN_EXTERN_C

/* Function declarations for all the replace_malloc _impl functions.
 * See memory/build/replace_malloc.c */
#define MALLOC_DECL(name, return_type, ...) \
  return_type name##_impl(__VA_ARGS__);
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC
#include "malloc_decls.h"

#define MALLOC_DECL(name, return_type, ...) return_type name(__VA_ARGS__);
#define MALLOC_FUNCS MALLOC_FUNCS_JEMALLOC
#include "malloc_decls.h"

MOZ_END_EXTERN_C

template <unsigned Base = 10>
size_t parseNumber(Buffer aBuf) {
  if (!aBuf) {
    die("Malformed input");
  }

  size_t result = 0;
  for (const char *c = aBuf.get(), *end = aBuf.GetEnd(); c < end; c++) {
    result *= Base;
    if ((*c >= '0' && *c <= '9')) {
      result += *c - '0';
    } else if (Base == 16 && *c >= 'a' && *c <= 'f') {
      result += *c - 'a' + 10;
    } else if (Base == 16 && *c >= 'A' && *c <= 'F') {
      result += *c - 'A' + 10;
    } else {
      die("Malformed input");
    }
  }
  return result;
}

static size_t percent(size_t a, size_t b) {
  if (!b) {
    return 0;
  }
  return size_t(round(double(a) / double(b) * 100.0));
}

class Distribution {
 public:
  // Default constructor used for array initialisation.
  Distribution()
      : mMaxSize(0),
        mNextSmallest(0),
        mShift(0),
        mArrayOffset(0),
        mArraySlots(0),
        mTotalRequests(0),
        mRequests{0} {}

  Distribution(size_t max_size, size_t next_smallest, size_t bucket_size)
      : mMaxSize(max_size),
        mNextSmallest(next_smallest),
        mShift(CeilingLog2(bucket_size)),
        mArrayOffset(1 + next_smallest),
        mArraySlots((max_size - next_smallest) >> mShift),
        mTotalRequests(0),
        mRequests{
            0,
        } {
    MOZ_ASSERT(mMaxSize);
    MOZ_RELEASE_ASSERT(mArraySlots <= MAX_NUM_BUCKETS);
  }

  Distribution& operator=(const Distribution& aOther) = default;

  void addRequest(size_t request) {
    MOZ_ASSERT(mMaxSize);

    mRequests[(request - mArrayOffset) >> mShift]++;
    mTotalRequests++;
  }

  void printDist(platform_handle_t std_err) {
    MOZ_ASSERT(mMaxSize);

    // The translation to turn a slot index into a memory request size.
    const size_t array_offset_add = (1 << mShift) + mNextSmallest;

    FdPrintf(std_err, "\n%zu-bin Distribution:\n", mMaxSize);
    FdPrintf(std_err, "   request   :  count percent\n");
    size_t range_start = mNextSmallest + 1;
    for (size_t j = 0; j < mArraySlots; j++) {
      size_t range_end = (j << mShift) + array_offset_add;
      FdPrintf(std_err, "%5zu - %5zu: %6zu %6zu%%\n", range_start, range_end,
               mRequests[j], percent(mRequests[j], mTotalRequests));
      range_start = range_end + 1;
    }
  }

  size_t maxSize() const { return mMaxSize; }

 private:
  static constexpr size_t MAX_NUM_BUCKETS = 16;

  // If size is zero this distribution is uninitialised.
  size_t mMaxSize;
  size_t mNextSmallest;

  // Parameters to convert a size into a slot number.
  unsigned mShift;
  unsigned mArrayOffset;

  // The number of slots.
  unsigned mArraySlots;

  size_t mTotalRequests;
  size_t mRequests[MAX_NUM_BUCKETS];
};


/* Class to handle dispatching the replay function calls to replace-malloc. */
class Replay {
 public:
  Replay() {
    mStdErr = fileno(stderr);
  }

  void enableSlopCalculation() { mCalculateSlop = true; }
  void enableMemset() { mDoMemset = true; }

  MemSlot& operator[](size_t index) const { return mSlots[index]; }

  void malloc(Buffer& aArgs, Buffer& aResult) {
    MemSlot& aSlot = SlotForResult(aResult);
    mOps++;
    size_t size = parseNumber(aArgs);
    aSlot.mPtr = ::malloc_impl(size);
    if (aSlot.mPtr) {
      aSlot.mRequest = size;
      MaybeCommit(aSlot);
      if (mCalculateSlop) {
        mTotalRequestedSize += size;
        mTotalAllocatedSize += ::malloc_usable_size_impl(aSlot.mPtr);
      }
    }
  }

  void posix_memalign(Buffer& aArgs, Buffer& aResult) {
    MemSlot& aSlot = SlotForResult(aResult);
    mOps++;
    size_t alignment = parseNumber(aArgs.SplitChar(','));
    size_t size = parseNumber(aArgs);
    void* ptr;
    if (::posix_memalign_impl(&ptr, alignment, size) == 0) {
      aSlot.mPtr = ptr;
      aSlot.mRequest = size;
      MaybeCommit(aSlot);
      if (mCalculateSlop) {
        mTotalRequestedSize += size;
        mTotalAllocatedSize += ::malloc_usable_size_impl(aSlot.mPtr);
      }
    } else {
      aSlot.mPtr = nullptr;
    }
  }

  void aligned_alloc(Buffer& aArgs, Buffer& aResult) {
    MemSlot& aSlot = SlotForResult(aResult);
    mOps++;
    size_t alignment = parseNumber(aArgs.SplitChar(','));
    size_t size = parseNumber(aArgs);
    aSlot.mPtr = ::aligned_alloc_impl(alignment, size);
    if (aSlot.mPtr) {
      aSlot.mRequest = size;
      MaybeCommit(aSlot);
      if (mCalculateSlop) {
        mTotalRequestedSize += size;
        mTotalAllocatedSize += ::malloc_usable_size_impl(aSlot.mPtr);
      }
    }
  }

  void calloc(Buffer& aArgs, Buffer& aResult) {
    MemSlot& aSlot = SlotForResult(aResult);
    mOps++;
    size_t num = parseNumber(aArgs.SplitChar(','));
    size_t size = parseNumber(aArgs);
    aSlot.mPtr = ::calloc_impl(num, size);
    if (aSlot.mPtr) {
      aSlot.mRequest = num * size;
      MaybeCommit(aSlot);
      if (mCalculateSlop) {
        mTotalRequestedSize += num * size;
        mTotalAllocatedSize += ::malloc_usable_size_impl(aSlot.mPtr);
      }
    }
  }

  void realloc(Buffer& aArgs, Buffer& aResult) {
    MemSlot& aSlot = SlotForResult(aResult);
    mOps++;
    Buffer dummy = aArgs.SplitChar('#');
    if (dummy) {
      die("Malformed input");
    }
    size_t slot_id = parseNumber(aArgs.SplitChar(','));
    size_t size = parseNumber(aArgs);
    MemSlot& old_slot = (*this)[slot_id];
    void* old_ptr = old_slot.mPtr;
    old_slot.mPtr = nullptr;
    aSlot.mPtr = ::realloc_impl(old_ptr, size);
    if (aSlot.mPtr) {
      aSlot.mRequest = size;
      MaybeCommit(aSlot);
      if (mCalculateSlop) {
        mTotalRequestedSize += size;
        mTotalAllocatedSize += ::malloc_usable_size_impl(aSlot.mPtr);
      }
    }
  }

  void free(Buffer& aArgs, Buffer& aResult) {
    if (aResult) {
      die("Malformed input");
    }
    mOps++;
    Buffer dummy = aArgs.SplitChar('#');
    if (dummy) {
      die("Malformed input");
    }
    size_t slot_id = parseNumber(aArgs);
    MemSlot& slot = (*this)[slot_id];
    ::free_impl(slot.mPtr);
    slot.mPtr = nullptr;
  }

  void memalign(Buffer& aArgs, Buffer& aResult) {
    MemSlot& aSlot = SlotForResult(aResult);
    mOps++;
    size_t alignment = parseNumber(aArgs.SplitChar(','));
    size_t size = parseNumber(aArgs);
    aSlot.mPtr = ::memalign_impl(alignment, size);
    if (aSlot.mPtr) {
      aSlot.mRequest = size;
      MaybeCommit(aSlot);
      if (mCalculateSlop) {
        mTotalRequestedSize += size;
        mTotalAllocatedSize += ::malloc_usable_size_impl(aSlot.mPtr);
      }
    }
  }

  void valloc(Buffer& aArgs, Buffer& aResult) {
    MemSlot& aSlot = SlotForResult(aResult);
    mOps++;
    size_t size = parseNumber(aArgs);
    aSlot.mPtr = ::valloc_impl(size);
    if (aSlot.mPtr) {
      aSlot.mRequest = size;
      MaybeCommit(aSlot);
      if (mCalculateSlop) {
        mTotalRequestedSize += size;
        mTotalAllocatedSize += ::malloc_usable_size_impl(aSlot.mPtr);
      }
    }
  }

  void jemalloc_stats(Buffer& aArgs, Buffer& aResult) {
    if (aArgs || aResult) {
      die("Malformed input");
    }
    mOps++;
    jemalloc_stats_t stats;
    // Using a variable length array here is a GCC & Clang extension. But it
    // allows us to place this on the stack and not alter jemalloc's profiling.
    const size_t num_bins = ::jemalloc_stats_num_bins();
    const size_t MAX_NUM_BINS = 100;
    if (num_bins > MAX_NUM_BINS) {
      die("Exceeded maximum number of jemalloc stats bins");
    }
    jemalloc_bin_stats_t bin_stats[MAX_NUM_BINS] = {{0}};
    ::jemalloc_stats_internal(&stats, bin_stats);


    size_t num_objects = 0;
    size_t num_sloppy_objects = 0;
    size_t total_allocated = 0;
    size_t total_slop = 0;
    size_t large_slop = 0;
    size_t large_used = 0;
    size_t huge_slop = 0;
    size_t huge_used = 0;
    size_t bin_slop[MAX_NUM_BINS] = {0};

    for (size_t slot_id = 0; slot_id < mNumUsedSlots; slot_id++) {
      MemSlot& slot = mSlots[slot_id];
      if (slot.mPtr) {
        size_t used = ::malloc_usable_size_impl(slot.mPtr);
        size_t slop = used - slot.mRequest;
        total_allocated += used;
        total_slop += slop;
        num_objects++;
        if (slop) {
          num_sloppy_objects++;
        }

        if (used <=
            (stats.subpage_max ? stats.subpage_max : stats.quantum_wide_max)) {
          // We know that this is an inefficient linear search, but there's a
          // small number of bins and this is simple.
          for (unsigned i = 0; i < num_bins; i++) {
            auto& bin = bin_stats[i];
            if (used == bin.size) {
              bin_slop[i] += slop;
              break;
            }
          }
        } else if (used <= stats.large_max) {
          large_slop += slop;
          large_used += used;
        } else {
          huge_slop += slop;
          huge_used += used;
        }
      }
    }

    // This formula corresponds to the calculation of wasted (from committed and
    // the other parameters) within jemalloc_stats()
    size_t committed = stats.allocated + stats.waste + stats.pages_dirty +
                       stats.bookkeeping + stats.bin_unused;

    FdPrintf(mStdErr, "\n");
    FdPrintf(mStdErr, "Objects:          %9zu\n", num_objects);
    FdPrintf(mStdErr, "Slots:            %9zu\n", mNumUsedSlots);
    FdPrintf(mStdErr, "Ops:              %9zu\n", mOps);
    FdPrintf(mStdErr, "mapped:           %9zu\n", stats.mapped);
    FdPrintf(mStdErr, "committed:        %9zu\n", committed);
    FdPrintf(mStdErr, "allocated:        %9zu\n", stats.allocated);
    FdPrintf(mStdErr, "waste:            %9zu\n", stats.waste);
    FdPrintf(mStdErr, "dirty:            %9zu\n", stats.pages_dirty);
    FdPrintf(mStdErr, "fresh:            %9zu\n", stats.pages_fresh);
    FdPrintf(mStdErr, "madvised:         %9zu\n", stats.pages_madvised);
    FdPrintf(mStdErr, "bookkeep:         %9zu\n", stats.bookkeeping);
    FdPrintf(mStdErr, "bin-unused:       %9zu\n", stats.bin_unused);
    FdPrintf(mStdErr, "quantum-max:      %9zu\n", stats.quantum_max);
    FdPrintf(mStdErr, "quantum-wide-max: %9zu\n", stats.quantum_wide_max);
    FdPrintf(mStdErr, "subpage-max:      %9zu\n", stats.subpage_max);
    FdPrintf(mStdErr, "large-max:        %9zu\n", stats.large_max);
    if (mCalculateSlop) {
      size_t slop = mTotalAllocatedSize - mTotalRequestedSize;
      FdPrintf(mStdErr,
               "Total slop for all allocations: %zuKiB/%zuKiB (%zu%%)\n",
               slop / 1024, mTotalAllocatedSize / 1024,
               percent(slop, mTotalAllocatedSize));
    }
    FdPrintf(mStdErr, "Live sloppy objects: %zu/%zu (%zu%%)\n",
             num_sloppy_objects, num_objects,
             percent(num_sloppy_objects, num_objects));
    FdPrintf(mStdErr, "Live sloppy bytes: %zuKiB/%zuKiB (%zu%%)\n",
             total_slop / 1024, total_allocated / 1024,
             percent(total_slop, total_allocated));

    FdPrintf(mStdErr, "\n%8s %11s %10s %8s %9s %9s %8s\n", "bin-size",
             "unused (c)", "total (c)", "used (c)", "non-full (r)", "total (r)",
             "used (r)");
    for (unsigned i = 0; i < num_bins; i++) {
      auto& bin = bin_stats[i];
      MOZ_ASSERT(bin.size);
      FdPrintf(mStdErr, "%8zu %8zuKiB %7zuKiB %7zu%% %12zu %9zu %7zu%%\n",
               bin.size, bin.bytes_unused / 1024, bin.bytes_total / 1024,
               percent(bin.bytes_total - bin.bytes_unused, bin.bytes_total),
               bin.num_non_full_runs, bin.num_runs,
               percent(bin.num_runs - bin.num_non_full_runs, bin.num_runs));
    }

    FdPrintf(mStdErr, "\n%5s %8s %9s %7s\n", "bin", "slop", "used", "percent");
    for (unsigned i = 0; i < num_bins; i++) {
      auto& bin = bin_stats[i];
      size_t used = bin.bytes_total - bin.bytes_unused;
      FdPrintf(mStdErr, "%5zu %8zu %9zu %6zu%%\n", bin.size, bin_slop[i], used,
               percent(bin_slop[i], used));
    }
    FdPrintf(mStdErr, "%5s %8zu %9zu %6zu%%\n", "large", large_slop, large_used,
             percent(large_slop, large_used));
    FdPrintf(mStdErr, "%5s %8zu %9zu %6zu%%\n", "huge", huge_slop, huge_used,
             percent(huge_slop, huge_used));

    print_distributions(stats, bin_stats);
  }

 private:
  /*
   * Create and print frequency distributions of memory requests.
   */
  void print_distributions(jemalloc_stats_t& stats,
                           jemalloc_bin_stats_t* bin_stats) {
    const size_t num_bins = ::jemalloc_stats_num_bins();

    // We compute distributions for all of the bins for small allocations
    // (num_bins) plus two more distributions for larger allocations.
    Distribution dists[num_bins + 2];

    unsigned last_size = 0;
    unsigned num_dists = 0;
    for (unsigned i = 0; i < num_bins; i++) {
      auto& bin = bin_stats[i];
      auto& dist = dists[num_dists++];

      MOZ_ASSERT(bin.size);
      if (bin.size <= 16) {
        // 1 byte buckets.
        dist = Distribution(bin.size, last_size, 1);
      } else if (bin.size <= stats.quantum_max) {
        // 4 buckets, (4 bytes per bucket with a 16 byte quantum).
        dist = Distribution(bin.size, last_size, stats.quantum / 4);
      } else if (bin.size <= stats.quantum_wide_max) {
        // 8 buckets, (32 bytes per bucket with a 256 byte quantum-wide).
        dist = Distribution(bin.size, last_size, stats.quantum_wide / 8);
      } else {
        // 16 buckets.
        dist = Distribution(bin.size, last_size, (bin.size - last_size) / 16);
      }
      last_size = bin.size;
    }

    // 16 buckets.
    dists[num_dists] = Distribution(stats.page_size, last_size,
                                    (stats.page_size - last_size) / 16);
    num_dists++;

    // Buckets are 1/4 of the page size (12 buckets).
    dists[num_dists] =
        Distribution(stats.page_size * 4, stats.page_size, stats.page_size / 4);
    num_dists++;

    MOZ_RELEASE_ASSERT(num_dists <= num_bins + 2);

    for (size_t slot_id = 0; slot_id < mNumUsedSlots; slot_id++) {
      MemSlot& slot = mSlots[slot_id];
      if (slot.mPtr) {
        for (size_t i = 0; i < num_dists; i++) {
          if (slot.mRequest <= dists[i].maxSize()) {
            dists[i].addRequest(slot.mRequest);
            break;
          }
        }
      }
    }

    for (unsigned i = 0; i < num_dists; i++) {
      dists[i].printDist(mStdErr);
    }
  }


 private:
  MemSlot& SlotForResult(Buffer& aResult) {
    /* Parse result value and get the corresponding slot. */
    Buffer dummy = aResult.SplitChar('=');
    Buffer dummy2 = aResult.SplitChar('#');
    if (dummy || dummy2) {
      die("Malformed input");
    }

    size_t slot_id = parseNumber(aResult);
    mNumUsedSlots = std::max(mNumUsedSlots, slot_id + 1);

    return mSlots[slot_id];
  }

  void MaybeCommit(MemSlot& aSlot) {
    if (mDoMemset) {
      // Write any byte, 0x55 isn't significant.
      memset(aSlot.mPtr, 0x55, aSlot.mRequest);
    }
  }

  platform_handle_t mStdErr;
  size_t mOps = 0;

  // The number of slots that have been used. It is used to iterate over slots
  // without accessing those we haven't initialised.
  size_t mNumUsedSlots = 0;

  MemSlotList mSlots;
  size_t mTotalRequestedSize = 0;
  size_t mTotalAllocatedSize = 0;
  // Whether to calculate slop for all allocations over the runtime of a
  // process.
  bool mCalculateSlop = false;
  bool mDoMemset = false;

};

MOZ_RUNINIT static Replay replay;

int main(int argc, const char* argv[]) {
  size_t first_pid = 0;
  const char* filename = nullptr;

  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    if (strcmp(option, "-s") == 0) {
      // Do accounting to calculate allocation slop.
      replay.enableSlopCalculation();
    } else if (strcmp(option, "-c") == 0) {
      // Touch memory as we allocate it.
      replay.enableMemset();
    } else if (!filename) {
      // Assume the only other possiability is a single file name.
      filename = option;
    } else {
      fprintf(stderr, "Unknown command line option: %s\n", option);
      return EXIT_FAILURE;
    }
  }

  FdReader reader(0);
  if (filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
      perror(filename);
      exit(1);
    }
    reader = FdReader(fd, true);
  }

  /* Read log from reader and dispatch function calls to the Replay instance.
   * The log format is essentially:
   *   <pid> <tid> <function>([<args>])[=<result>]
   * <args> is a comma separated list of arguments.
   *
   * The logs are expected to be preprocessed so that allocations are
   * attributed a tracking slot. The input is trusted not to have crazy
   * values for these slot numbers.
   *
   * <result>, as well as some of the args to some of the function calls are
   * such slot numbers.
   */
  while (true) {
    Buffer line = reader.ReadLine();

    if (!line) {
      break;
    }

    size_t pid = parseNumber(line.SplitChar(' '));
    if (!first_pid) {
      first_pid = pid;
    }

    /* The log may contain data for several processes, only entries for the
     * very first that appears are treated. */
    if (first_pid != pid) {
      continue;
    }

    /* The log contains thread ids for manual analysis, but we just ignore them
     * for now. */
    parseNumber(line.SplitChar(' '));

    Buffer func = line.SplitChar('(');
    Buffer args = line.SplitChar(')');

    if (func == Buffer("jemalloc_stats")) {
      replay.jemalloc_stats(args, line);
    } else if (func == Buffer("free")) {
      replay.free(args, line);
    } else if (func == Buffer("malloc")) {
      replay.malloc(args, line);
    } else if (func == Buffer("posix_memalign")) {
      replay.posix_memalign(args, line);
    } else if (func == Buffer("aligned_alloc")) {
      replay.aligned_alloc(args, line);
    } else if (func == Buffer("calloc")) {
      replay.calloc(args, line);
    } else if (func == Buffer("realloc")) {
      replay.realloc(args, line);
    } else if (func == Buffer("memalign")) {
      replay.memalign(args, line);
    } else if (func == Buffer("valloc")) {
      replay.valloc(args, line);
    } else {
      die("Malformed input");
    }
  }

  return 0;
}
