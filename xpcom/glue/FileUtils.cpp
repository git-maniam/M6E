/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/FileUtils.h"

#include "mozilla/BaseProfilerMarkers.h"
#include "mozilla/MemUtils.h"
#include "nscore.h"
#include "private/pprio.h"
#include "prmem.h"

#  include <fcntl.h>
#  include <limits.h>
#  include <mach-o/fat.h>
#  include <mach-o/loader.h>
#  include <mach/machine.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>

// Functions that are not to be used in standalone glue must be implemented
// within this #if block
#if defined(MOZILLA_INTERNAL_API)

#  include "nsString.h"

bool mozilla::fallocate(PRFileDesc* aFD, int64_t aLength) {
#  if defined(HAVE_POSIX_FALLOCATE)
  return posix_fallocate(PR_FileDesc2NativeHandle(aFD), 0, aLength) == 0;
#  else
  int fd = PR_FileDesc2NativeHandle(aFD);
  fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, aLength};
  // Try to get a continous chunk of disk space
  int ret = fcntl(fd, F_PREALLOCATE, &store);
  if (ret == -1) {
    // OK, perhaps we are too fragmented, allocate non-continuous
    store.fst_flags = F_ALLOCATEALL;
    ret = fcntl(fd, F_PREALLOCATE, &store);
    if (ret == -1) {
      return false;
    }
  }
  return ftruncate(fd, aLength) == 0;
#  endif
}

void mozilla::ReadAheadLib(nsIFile* aFile) {
  nsAutoCString nativePath;
  if (!aFile || NS_FAILED(aFile->GetNativePath(nativePath))) {
    return;
  }
  ReadAheadLib(nativePath.get());
}

void mozilla::ReadAheadFile(nsIFile* aFile, const size_t aOffset,
                            const size_t aCount, mozilla::filedesc_t* aOutFd) {
  nsAutoCString nativePath;
  if (!aFile || NS_FAILED(aFile->GetNativePath(nativePath))) {
    return;
  }
  ReadAheadFile(nativePath.get(), aOffset, aCount, aOutFd);
}

mozilla::PathString mozilla::GetLibraryName(mozilla::pathstr_t aDirectory,
                                            const char* aLib) {
  char* temp = PR_GetLibraryName(aDirectory, aLib);
  if (!temp) {
    return ""_ns;
  }
  nsAutoCString libname(temp);
  PR_FreeLibraryName(temp);
  return libname;
}

mozilla::PathString mozilla::GetLibraryFilePathname(mozilla::pathstr_t aName,
                                                    PRFuncPtr aAddr) {
  char* temp = PR_GetLibraryFilePathname(aName, aAddr);
  if (!temp) {
    return ""_ns;
  }
  nsAutoCString path(temp);
  PR_Free(temp);  // PR_GetLibraryFilePathname() uses PR_Malloc().
  return path;
}

#endif  // defined(MOZILLA_INTERNAL_API)

#if defined(LINUX) && !defined(ANDROID)

static const unsigned int bufsize = 4096;

#  ifdef __LP64__
typedef Elf64_Ehdr Elf_Ehdr;
typedef Elf64_Phdr Elf_Phdr;
static const unsigned char ELFCLASS = ELFCLASS64;
typedef Elf64_Off Elf_Off;
#  else
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf32_Phdr Elf_Phdr;
static const unsigned char ELFCLASS = ELFCLASS32;
typedef Elf32_Off Elf_Off;
#  endif

#else

#  if defined(__i386__)
static const uint32_t CPU_TYPE = CPU_TYPE_X86;
#  elif defined(__x86_64__)
static const uint32_t CPU_TYPE = CPU_TYPE_X86_64;
#  elif defined(__ppc__)
static const uint32_t CPU_TYPE = CPU_TYPE_POWERPC;
#  elif defined(__ppc64__)
static const uint32_t CPU_TYPE = CPU_TYPE_POWERPC64;
#  elif defined(__aarch64__)
static const uint32_t CPU_TYPE = CPU_TYPE_ARM64;
#  else
#    error Unsupported CPU type
#  endif

#  ifdef __LP64__
#    undef LC_SEGMENT
#    define LC_SEGMENT LC_SEGMENT_64
#    undef MH_MAGIC
#    define MH_MAGIC MH_MAGIC_64
#    define cpu_mach_header mach_header_64
#    define segment_command segment_command_64
#  else
#    define cpu_mach_header mach_header
#  endif

class ScopedMMap {
 public:
  explicit ScopedMMap(const char* aFilePath) : buf(nullptr) {
    fd = open(aFilePath, O_RDONLY);
    if (fd < 0) {
      return;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
      return;
    }
    size = st.st_size;
    buf = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED) {
      buf = nullptr;
    }
  }
  ~ScopedMMap() {
    if (buf) {
      munmap(buf, size);
    }
    if (fd >= 0) {
      close(fd);
    }
  }
  operator char*() { return buf; }
  int getFd() { return fd; }

 private:
  int fd;
  char* buf;
  size_t size;
};
#endif

void mozilla::ReadAhead(mozilla::filedesc_t aFd, const size_t aOffset,
                        const size_t aCount) {
#if   defined(LINUX)

  (void)posix_fadvise(aFd, aOffset, aCount, POSIX_FADV_WILLNEED);

#else

  struct radvisory ra;
  ra.ra_offset = aOffset;
  ra.ra_count = aCount;
  // The F_RDADVISE fcntl is equivalent to Linux' readahead() system call.
  fcntl(aFd, F_RDADVISE, &ra);

#endif
}

void mozilla::ReadAheadLib(mozilla::pathstr_t aFilePath) {
  if (!aFilePath) {
    return;
  }


  AUTO_BASE_PROFILER_MARKER_TEXT("ReadAheadLib", OTHER, {},
                                 aFilePath
  );

#if   defined(LINUX) && !defined(ANDROID)
  int fd = open(aFilePath, O_RDONLY);
  if (fd < 0) {
    return;
  }

  union {
    char buf[bufsize];
    Elf_Ehdr ehdr;
  } elf;
  // Read ELF header (ehdr) and program header table (phdr).
  // We check that the ELF magic is found, that the ELF class matches
  // our own, and that the program header table as defined in the ELF
  // headers fits in the buffer we read.
  ssize_t bytesRead = read(fd, elf.buf, bufsize);
  if ((bytesRead < static_cast<ssize_t>(sizeof(Elf_Ehdr))) ||
      (memcmp(elf.buf, ELFMAG, 4)) ||
      (elf.ehdr.e_ident[EI_CLASS] != ELFCLASS) ||
      // Upcast e_phentsize so the multiplication is done in the same precision
      // as the subsequent addition, to satisfy static analyzers and avoid
      // issues with abnormally large program header tables. The program header
      // table must lie within the bytes we actually read, not just the buffer.
      (elf.ehdr.e_phoff +
           (static_cast<Elf_Off>(elf.ehdr.e_phentsize) * elf.ehdr.e_phnum) >=
       static_cast<Elf_Off>(bytesRead))) {
    close(fd);
    return;
  }
  // The program header table contains segment definitions. One such
  // segment type is PT_LOAD, which describes how the dynamic loader
  // is going to map the file in memory. We use that information to
  // find the biggest offset from the library that will be mapped in
  // memory.
  Elf_Phdr* phdr = (Elf_Phdr*)&elf.buf[elf.ehdr.e_phoff];
  Elf_Off end = 0;
  for (int phnum = elf.ehdr.e_phnum; phnum; phdr++, phnum--) {
    if ((phdr->p_type == PT_LOAD) && (end < phdr->p_offset + phdr->p_filesz)) {
      end = phdr->p_offset + phdr->p_filesz;
    }
  }
  // Let the kernel read ahead what the dynamic loader is going to
  // map in memory soon after.
  if (end > 0) {
    ReadAhead(fd, 0, end);
  }
  close(fd);
#else
  ScopedMMap buf(aFilePath);
  char* base = buf;
  if (!base) {
    return;
  }

  // An OSX binary might either be a fat (universal) binary or a
  // Mach-O binary. A fat binary actually embeds several Mach-O
  // binaries. If we have a fat binary, find the offset where the
  // Mach-O binary for our CPU type can be found.
  struct fat_header* fh = (struct fat_header*)base;

  if (OSSwapBigToHostInt32(fh->magic) == FAT_MAGIC) {
    uint32_t nfat_arch = OSSwapBigToHostInt32(fh->nfat_arch);
    struct fat_arch* arch = (struct fat_arch*)&buf[sizeof(struct fat_header)];
    for (; nfat_arch; arch++, nfat_arch--) {
      if (OSSwapBigToHostInt32(arch->cputype) == CPU_TYPE) {
        base += OSSwapBigToHostInt32(arch->offset);
        break;
      }
    }
    if (base == buf) {
      return;
    }
  }

  // Check Mach-O magic in the Mach header
  struct cpu_mach_header* mh = (struct cpu_mach_header*)base;
  if (mh->magic != MH_MAGIC) {
    return;
  }

  // The Mach header is followed by a sequence of load commands.
  // Each command has a header containing the command type and the
  // command size. LD_SEGMENT commands describes how the dynamic
  // loader is going to map the file in memory. We use that
  // information to find the biggest offset from the library that
  // will be mapped in memory.
  char* cmd = &base[sizeof(struct cpu_mach_header)];
  uint32_t end = 0;
  for (uint32_t ncmds = mh->ncmds; ncmds; ncmds--) {
    struct segment_command* sh = (struct segment_command*)cmd;
    if (sh->cmd != LC_SEGMENT) {
      continue;
    }
    if (end < sh->fileoff + sh->filesize) {
      end = sh->fileoff + sh->filesize;
    }
    cmd += sh->cmdsize;
  }
  // Let the kernel read ahead what the dynamic loader is going to
  // map in memory soon after.
  if (end > 0) {
    ReadAhead(buf.getFd(), base - buf, end);
  }
#endif
}

void mozilla::ReadAheadFile(mozilla::pathstr_t aFilePath, const size_t aOffset,
                            const size_t aCount, mozilla::filedesc_t* aOutFd) {
  if (!aFilePath) {
    if (aOutFd) {
      *aOutFd = -1;
    }
    return;
  }
  int fd = open(aFilePath, O_RDONLY);
  if (aOutFd) {
    *aOutFd = fd;
  }
  if (fd < 0) {
    return;
  }
  size_t count;
  if (aCount == SIZE_MAX) {
    struct stat st;
    if (fstat(fd, &st) < 0) {
      if (!aOutFd) {
        close(fd);
      }
      return;
    }
    count = st.st_size;
  } else {
    count = aCount;
  }
  ReadAhead(fd, aOffset, count);
  if (!aOutFd) {
    close(fd);
  }
}
