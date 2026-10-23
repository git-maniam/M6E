/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PosixSerialPlatformService.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "PosixSerialParityDecodeStream.h"
#include "Serial.h"
#include "SerialLogging.h"
#include "mozilla/AsyncPlatformPipes.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/SyncRunnable.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsString.h"
#include "nsThreadUtils.h"

#  include <CoreFoundation/CFNumber.h>
#  include <IOKit/IOKitLib.h>
#  include <IOKit/serial/IOSerialKeys.h>
#  include <IOKit/serial/ioss.h>
#  include <IOKit/usb/IOUSBLib.h>

#  include "MacBluetoothServiceClassId.h"
#  ifndef kIOMainPortDefault
#    define kIOMainPortDefault kIOMasterPortDefault
#  endif

#ifdef TCGETS2
// Support non-standard baud rates through TCGETS2/TCSETS2
#  define LINUX_NSTD_BAUD 1
#endif

#ifdef LINUX_NSTD_BAUD
#  include <asm/termbits.h>
#else
#  include <termios.h>
#endif


namespace mozilla::dom {

constexpr int kPollTimeoutMs = 100;
// Maximum total time to spend in a single Write() call before giving up.
// Prevents indefinite blocking when a serial port's buffer is full (e.g.
// due to flow control), which would block all other serial ports.
constexpr int kWriteTimeoutMs = 5000;
constexpr size_t kDeviceNameBufferSize = 256;

// Returns Ok if aDevpath refers to a real serial device by attempting
// to open it and issuing a TIOCMGET ioctl, and errno otherwise.
// This filters out phantom ttyS* entries and non-serial tty devices.
static Result<Ok, int> IsRealSerialPort(const char* aDevpath) {
  int fd = open(aDevpath, O_RDWR | O_NONBLOCK | O_NOCTTY);
  if (fd < 0) {
    int openErrno = errno;
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("IsRealSerialPort: open(%s, O_RDWR|O_NONBLOCK|O_NOCTTY) failed: "
             "errno=%d (%s)",
             aDevpath, openErrno, strerror(openErrno)));
    return Err(openErrno);
  }
  int status;
  bool isReal = ioctl(fd, TIOCMGET, &status) == 0;
  // Capture errno before close(), which may overwrite it.
  int ioctlErrno = errno;
  close(fd);
  if (isReal) {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("IsRealSerialPort: %s accepted (TIOCMGET status=0x%x)", aDevpath,
             status));
    return Ok();
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("IsRealSerialPort: TIOCMGET on %s failed: errno=%d (%s)", aDevpath,
           ioctlErrno, strerror(ioctlErrno)));
  return Err(ioctlErrno);
}

// macOS exposes built-in serial ports used for WiFi debugging and kernel
// debugging via IOKit. They are not useful targets for WebSerial, so
// hide them from enumeration. Bluetooth ports are intentionally not filtered.
static bool IsMacOSSystemSerialPort(const char* aDevicePath) {
  static constexpr const char* kSystemPortPaths[] = {
      "/dev/tty.wlan-debug",
      "/dev/tty.debug-console",
  };
  for (const char* p : kSystemPortPaths) {
    if (strcmp(aDevicePath, p) == 0) {
      return true;
    }
  }
  return false;
}

PosixSerialPlatformService::PosixSerialPlatformService()
    : mNotificationPort(nullptr),
      mAddedIterator(0),
      mRemovedIterator(0)
{
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p] created", this));
}

PosixSerialPlatformService::~PosixSerialPlatformService() {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p] destroyed", this));
}

nsresult PosixSerialPlatformService::Init() { return StartMonitoring(); }

void PosixSerialPlatformService::Shutdown() {
  if (IsShutdown()) {
    return;
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("PosixSerialPlatformService[%p]::Shutdown (closing %u open ports)",
           this, mOpenPorts.Count()));

  SerialPlatformService::Shutdown();

  if (mAddedIterator) {
    IOObjectRelease(mAddedIterator);
    mAddedIterator = 0;
  }

  if (mRemovedIterator) {
    IOObjectRelease(mRemovedIterator);
    mRemovedIterator = 0;
  }

  if (mNotificationPort) {
    CFRunLoopSourceRef runLoopSource =
        IONotificationPortGetRunLoopSource(mNotificationPort);
    if (runLoopSource) {
      CFRunLoopRemoveSource(CFRunLoopGetMain(), runLoopSource,
                            kCFRunLoopDefaultMode);
      MOZ_LOG(gWebSerialLog, LogLevel::Debug,
              ("PosixSerialPlatformService[%p]::Shutdown removed run "
               "loop source from main run loop",
               this));
    }
    IONotificationPortDestroy(mNotificationPort);
    mNotificationPort = nullptr;
  }

  RefPtr<PosixSerialPlatformService> self = this;
  SyncRunnable::DispatchToThread(
      IOThread(), NS_NewRunnableFunction(
                      "PosixSerialPlatformService::Shutdown:IOCleanup", [self] {
                        self->mOpenPorts.Clear();
                      }));

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("PosixSerialPlatformService[%p]::Shutdown complete", this));
}

nsresult PosixSerialPlatformService::EnumeratePortsImpl(
    SerialPortList& aPorts, bool* aLikelyAccessDenied) {
  aPorts.Clear();

  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("PosixSerialPlatformService[%p]::EnumeratePorts starting enumeration",
       this));

  io_iterator_t serialPortIterator;
  CFMutableDictionaryRef classesToMatch =
      IOServiceMatching(kIOSerialBSDServiceValue);

  if (classesToMatch == nullptr) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::EnumeratePorts IOServiceMatching "
             "failed",
             this));
    return NS_ERROR_FAILURE;
  }

  // The call to IOServiceGetMatchingServices consumes a ref to classesToMatch.
  if (IOServiceGetMatchingServices(kIOMainPortDefault, classesToMatch,
                                   &serialPortIterator) != KERN_SUCCESS) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::EnumeratePorts "
             "IOServiceGetMatchingServices failed",
             this));
    return NS_ERROR_FAILURE;
  }

  io_object_t serialService;
  while ((serialService = IOIteratorNext(serialPortIterator))) {
    IPCSerialPortInfo info;
    if (ExtractDeviceInfo(serialService, info)) {
      MOZ_LOG(
          gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::EnumeratePorts found port: "
           "path=%s, friendlyName=%s, VID=0x%04x, PID=0x%04x, BT:%s",
           this, NS_ConvertUTF16toUTF8(info.path()).get(),
           NS_ConvertUTF16toUTF8(info.friendlyName()).get(),
           info.usbVendorId().valueOr(0), info.usbProductId().valueOr(0),
           info.bluetoothServiceClassId().isSome()
               ? NS_ConvertUTF16toUTF8(info.bluetoothServiceClassId().value())
                     .get()
               : "none"));

      aPorts.AppendElement(std::move(info));
    }

    IOObjectRelease(serialService);
  }

  IOObjectRelease(serialPortIterator);


  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::EnumeratePorts found %zu ports",
           this, aPorts.Length()));

  return NS_OK;
}

int PosixSerialPlatformService::FindPortFd(const nsString& aPortId) {
  AssertIsOnIOThread();
  if (auto entry = mOpenPorts.Lookup(aPortId)) {
    return entry.Data().get();
  }
  return -1;
}

// Returns whether the conversion succeeded
static bool ConvertBaudRate(uint32_t aBaudRate, speed_t& aSpeed) {
#define BAUDRATE_TO_SPEED_CASE(x) \
  case x:                         \
    aSpeed = B##x;                \
    return true;

  switch (aBaudRate) {
    BAUDRATE_TO_SPEED_CASE(0)
    BAUDRATE_TO_SPEED_CASE(50)
    BAUDRATE_TO_SPEED_CASE(75)
    BAUDRATE_TO_SPEED_CASE(110)
    BAUDRATE_TO_SPEED_CASE(134)
    BAUDRATE_TO_SPEED_CASE(150)
    BAUDRATE_TO_SPEED_CASE(200)
    BAUDRATE_TO_SPEED_CASE(300)
    BAUDRATE_TO_SPEED_CASE(600)
    BAUDRATE_TO_SPEED_CASE(1200)
    BAUDRATE_TO_SPEED_CASE(1800)
    BAUDRATE_TO_SPEED_CASE(2400)
    BAUDRATE_TO_SPEED_CASE(4800)
    BAUDRATE_TO_SPEED_CASE(9600)
    BAUDRATE_TO_SPEED_CASE(19200)
    BAUDRATE_TO_SPEED_CASE(38400)
    BAUDRATE_TO_SPEED_CASE(57600)
    BAUDRATE_TO_SPEED_CASE(115200)
#ifdef B230400
    BAUDRATE_TO_SPEED_CASE(230400)
#endif
#ifdef B460800
    BAUDRATE_TO_SPEED_CASE(460800)
#endif
#ifdef B576000
    BAUDRATE_TO_SPEED_CASE(576000)
#endif
#ifdef B921600
    BAUDRATE_TO_SPEED_CASE(921600)
#endif
  }
#undef BAUDRATE_TO_SPEED_CASE

  return false;
}

nsresult PosixSerialPlatformService::ConfigurePort(
    int aFd, const IPCSerialOptions& aOptions) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::ConfigurePort fd=%d, baudRate=%u, "
           "dataBits=%u, stopBits=%u, parity=%u, flowControl=%u",
           this, aFd, aOptions.baudRate(), aOptions.dataBits(),
           aOptions.stopBits(), static_cast<uint8_t>(aOptions.parity()),
           static_cast<uint8_t>(aOptions.flowControl())));

#ifdef LINUX_NSTD_BAUD
  struct termios2 tty;
  if (ioctl(aFd, TCGETS2, &tty) < 0) {
#else
  struct termios tty;
  if (tcgetattr(aFd, &tty) != 0) {
#endif
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::ConfigurePort tcgetattr/ioctl "
             "failed: "
             "errno=%d",
             this, errno));
    return NS_ERROR_FAILURE;
  }

  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~HUPCL;

  tty.c_cflag &= ~CSIZE;
  switch (aOptions.dataBits()) {
    case 7:
      tty.c_cflag |= CS7;
      break;
    case 8:
      tty.c_cflag |= CS8;
      break;
    default:
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("PosixSerialPlatformService[%p]::ConfigurePort invalid dataBits: %u",
           this, aOptions.dataBits()));
      return NS_ERROR_INVALID_ARG;
  }

  switch (aOptions.stopBits()) {
    case 1:
      tty.c_cflag &= ~CSTOPB;
      break;
    case 2:
      tty.c_cflag |= CSTOPB;
      break;
    default:
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("PosixSerialPlatformService[%p]::ConfigurePort invalid stopBits: %u",
           this, aOptions.stopBits()));
      return NS_ERROR_INVALID_ARG;
  }

  switch (aOptions.parity()) {
    case ParityType::None:
      tty.c_cflag &= ~PARENB;
      tty.c_iflag |= IGNPAR;
      tty.c_iflag &= ~(INPCK | PARMRK);
      break;
    case ParityType::Even:
      tty.c_cflag |= PARENB;
      tty.c_cflag &= ~PARODD;
      tty.c_iflag &= ~IGNPAR;
      tty.c_iflag |= (INPCK | PARMRK);
      break;
    case ParityType::Odd:
      tty.c_cflag |= (PARENB | PARODD);
      tty.c_iflag &= ~IGNPAR;
      tty.c_iflag |= (INPCK | PARMRK);
      break;
  }

  switch (aOptions.flowControl()) {
    case FlowControlType::None:
      tty.c_cflag &= ~CRTSCTS;
      break;
    case FlowControlType::Hardware:
      tty.c_cflag |= CRTSCTS;
      break;
  }

  // Set flags for raw operation
  tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);
  tty.c_oflag &= ~OPOST;
  tty.c_iflag &= ~(IGNBRK | BRKINT | ISTRIP | INLCR | IGNCR | ICRNL | IXON |
                   IXOFF | IXANY);

  // VMIN=1: the terminal driver requires at least 1 byte before completing
  // a read.  VMIN=0 also works on Linux, because O_NONBLOCK takes precedence
  // and reading with no bytes available still fails with EAGAIN, but not on
  // macOS where that case returns 0 which PlatformPipeReader treats as EOF.
  // POSIX allows either behavior, so we use VMIN=1 instead.
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 0;

  speed_t speed;
  // The IOSSIOSPEED ioctl() is only available on Mac.
  bool need_speedioctl = false;
  if (ConvertBaudRate(aOptions.baudRate(), speed)) {
    if (cfsetispeed(&tty, speed) != 0) {
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("PosixSerialPlatformService[%p]::ConfigurePort cfsetispeed failed: "
           "errno=%d",
           this, errno));
      return NS_ERROR_FAILURE;
    }
    if (cfsetospeed(&tty, speed) != 0) {
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("PosixSerialPlatformService[%p]::ConfigurePort cfsetospeed failed: "
           "errno=%d",
           this, errno));
      return NS_ERROR_FAILURE;
    }
  } else {
    // Attempt to use a custom baud rate
    MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
            ("PosixSerialPlatformService[%p]::ConfigurePort attempting to use "
             "custom baudRate: %u",
             this, aOptions.baudRate()));
#ifdef LINUX_NSTD_BAUD
    tty.c_cflag &= ~CBAUD;
    tty.c_cflag |= CBAUDEX;
    tty.c_ispeed = aOptions.baudRate();
    tty.c_ospeed = aOptions.baudRate();
#else
    // cfsetispeed() and cfsetospeed() sometimes work for custom baud rates
    // on Mac, but the ioctl is more reliable. But we have to set it after
    // setting everything else or it will get overwritten.
    need_speedioctl = true;
#endif
  }
#ifdef LINUX_NSTD_BAUD
  if (ioctl(aFd, TCSETS2, &tty) < 0) {
#else
  if (tcsetattr(aFd, TCSANOW, &tty) != 0) {
#endif
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::ConfigurePort tcsetattr/ioctl "
             "failed: "
             "errno=%d",
             this, errno));
    return NS_ERROR_FAILURE;
  }

  if (need_speedioctl) {
    speed = aOptions.baudRate();
    if (ioctl(aFd, IOSSIOSPEED, &speed) == -1) {
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("PosixSerialPlatformService[%p]::ConfigurePort IOSSIOSPEED "
               "ioctl failed to set baudRate %u: "
               "errno=%d",
               this, aOptions.baudRate(), errno));
      return NS_ERROR_FAILURE;
    }
  }

  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("PosixSerialPlatformService[%p]::ConfigurePort configuration successful",
       this));
  return NS_OK;
}

nsresult PosixSerialPlatformService::OpenImpl(
    const nsString& aPortId, const IPCSerialOptions& aOptions) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Open portId=%s, baudRate=%u", this,
           NS_ConvertUTF16toUTF8(aPortId).get(), aOptions.baudRate()));
  AssertIsOnIOThread();

  // Reject path traversal attempts (e.g. "/dev/tty/../../../etc/passwd").
  if (aPortId.Find(u"/.."_ns) != kNotFound) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Open rejected portId '%s': "
             "path traversal detected",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_INVALID_ARG;
  }

  // Validate portId is a serial device path. This prevents a compromised
  // content process from using a crafted portId to open arbitrary files.
  if (IsRealSerialPort(NS_ConvertUTF16toUTF8(aPortId).get()).isErr()) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Open rejected invalid portId "
             "'%s': not a serial device path",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_INVALID_ARG;
  }

  if (mOpenPorts.Contains(aPortId)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Open port already open: %s", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_FILE_IS_LOCKED;
  }

  NS_ConvertUTF16toUTF8 path(aPortId);
  int fd = open(path.get(), O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd < 0) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("PosixSerialPlatformService[%p]::Open open() failed for %s: errno=%d",
         this, path.get(), errno));
    if (errno == EACCES) {
      return NS_ERROR_FILE_ACCESS_DENIED;
    } else if (errno == ENOENT) {
      return NS_ERROR_FILE_NOT_FOUND;
    } else if (errno == EBUSY) {
      return NS_ERROR_FILE_IS_LOCKED;
    } else {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  mozilla::UniqueFileHandle handle(fd);
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Open opened fd=%d", this, fd));

  if (ioctl(fd, TIOCEXCL) < 0) {
    MOZ_LOG(gWebSerialLog, LogLevel::Warning,
            ("PosixSerialPlatformService[%p]::Open TIOCEXCL failed: errno=%d",
             this, errno));
  }

  nsresult rv = ConfigurePort(fd, aOptions);
  if (NS_FAILED(rv)) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("PosixSerialPlatformService[%p]::Open ConfigurePort failed: 0x%08x",
         this, static_cast<uint32_t>(rv)));
    return rv;
  }

#ifdef LINUX_NSTD_BAUD
  ioctl(fd, TCFLSH, TCIOFLUSH);
#else
  tcflush(fd, TCIOFLUSH);
#endif

  mOpenPorts.InsertOrUpdate(aPortId, std::move(handle));
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Open successfully opened port %s",
           this, NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult PosixSerialPlatformService::CloseImpl(const nsString& aPortId) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Close portId=%s", this,
           NS_ConvertUTF16toUTF8(aPortId).get()));
  AssertIsOnIOThread();

  if (!mOpenPorts.Remove(aPortId)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Close port not found: %s", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Close successfully closed port %s",
           this, NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult PosixSerialPlatformService::WriteImpl(const nsString& aPortId,
                                               Span<const uint8_t> aData) {
  int fd = FindPortFd(aPortId);
  if (fd < 0) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Write port not found: %s", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (aData.IsEmpty()) {
    MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
            ("PosixSerialPlatformService[%p]::Write empty data", this));
    return NS_OK;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
          ("PosixSerialPlatformService[%p]::Write writing %zu bytes", this,
           aData.Length()));

  size_t totalWritten = 0;
  const uint8_t* buffer = aData.Elements();
  size_t remaining = aData.Length();
  int totalPollTimeMs = 0;

  while (remaining > 0) {
    ssize_t bytesWritten = write(fd, buffer + totalWritten, remaining);

    if (bytesWritten < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN) {
        if (totalPollTimeMs >= kWriteTimeoutMs) {
          MOZ_LOG(gWebSerialLog, LogLevel::Error,
                  ("PosixSerialPlatformService[%p]::Write timed out after %dms "
                   "with %zu bytes remaining",
                   this, totalPollTimeMs, remaining));
          return NS_ERROR_NET_TIMEOUT;
        }

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        int pollResult = poll(&pfd, 1, kPollTimeoutMs);
        if (pollResult < 0) {
          if (errno == EINTR) {
            continue;
          }
          MOZ_LOG(
              gWebSerialLog, LogLevel::Error,
              ("PosixSerialPlatformService[%p]::Write poll failed: errno=%d",
               this, errno));
          return NS_ERROR_FAILURE;
        }
        if (pollResult == 0) {
          totalPollTimeMs += kPollTimeoutMs;
          continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
          MOZ_LOG(gWebSerialLog, LogLevel::Error,
                  ("PosixSerialPlatformService[%p]::Write poll error: "
                   "revents=0x%x",
                   this, pfd.revents));
          return NS_ERROR_FAILURE;
        }
        continue;
      }
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("PosixSerialPlatformService[%p]::Write write() failed: errno=%d",
               this, errno));
      return NS_ERROR_FAILURE;
    }

    if (bytesWritten == 0) {
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("PosixSerialPlatformService[%p]::Write write() returned 0", this));
      return NS_ERROR_FAILURE;
    }

    totalWritten += bytesWritten;
    remaining -= bytesWritten;
    totalPollTimeMs = 0;

    if (remaining > 0) {
      MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
              ("PosixSerialPlatformService[%p]::Write partial write: %zd "
               "bytes, %zu "
               "remaining",
               this, bytesWritten, remaining));
    }
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
          ("PosixSerialPlatformService[%p]::Write successfully wrote %zu bytes",
           this, totalWritten));
  return NS_OK;
}

nsresult PosixSerialPlatformService::DrainImpl(const nsString& aPortId) {
  int fd = FindPortFd(aPortId);
  if (fd < 0) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Drain port not found: %s", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Drain draining transmit buffers",
           this));

#ifdef LINUX_NSTD_BAUD
  if (ioctl(fd, TCSBRK, 1) < 0) {
#else
  if (tcdrain(fd) < 0) {
#endif
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Drain tcdrain() failed: errno=%d",
             this, errno));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Drain successfully drained buffers",
           this));
  return NS_OK;
}

nsresult PosixSerialPlatformService::FlushImpl(const nsString& aPortId,
                                               bool aReceive) {
  int fd = FindPortFd(aPortId);
  if (fd < 0) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Flush port not found: %s", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  int queue = aReceive ? TCIFLUSH : TCOFLUSH;
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Flush discarding %s buffers", this,
           aReceive ? "receive" : "transmit"));

#ifdef LINUX_NSTD_BAUD
  if (ioctl(fd, TCFLSH, queue) < 0) {
#else
  if (tcflush(fd, queue) < 0) {
#endif
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::Flush tcflush() failed: errno=%d",
             this, errno));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::Flush successfully flushed %s "
           "buffers",
           this, aReceive ? "receive" : "transmit"));
  return NS_OK;
}

nsresult PosixSerialPlatformService::SetSignalsImpl(
    const nsString& aPortId, const IPCSerialOutputSignals& aSignals) {
  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("PosixSerialPlatformService[%p]::SetSignals portId=%s, DTR=%s, RTS=%s, "
       "Break=%s",
       this, NS_ConvertUTF16toUTF8(aPortId).get(),
       aSignals.dataTerminalReady().isSome()
           ? (aSignals.dataTerminalReady().value() ? "true" : "false")
           : "unchanged",
       aSignals.requestToSend().isSome()
           ? (aSignals.requestToSend().value() ? "true" : "false")
           : "unchanged",
       aSignals.breakSignal().isSome()
           ? (aSignals.breakSignal().value() ? "true" : "false")
           : "unchanged"));

  int fd = FindPortFd(aPortId);
  if (fd < 0) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::SetSignals port not found: %s",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (aSignals.dataTerminalReady().isSome() ||
      aSignals.requestToSend().isSome()) {
    int status;
    if (ioctl(fd, TIOCMGET, &status) < 0) {
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("PosixSerialPlatformService[%p]::SetSignals failure to get "
               "status on %d: errno=%d",
               this, fd, errno));
      return NS_ERROR_FAILURE;
    }
    MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
            ("PosixSerialPlatformService[%p]::SetSignals got status on %d: "
             "status=0x%x",
             this, fd, uint32_t(status)));

    // The spec requires that we set DTR first, then RTS, then break.
    // We could do this with TIOCMBIS or TIOCMBIC to set or clear
    // individual flags, but that seemed to return ENOTTY on Mac.
    // So instead we somewhat clunkily get the current flags, toggle
    // the bit, then set the flags for each thing we want to change.
    if (aSignals.dataTerminalReady().isSome()) {
      if (aSignals.dataTerminalReady().value()) {
        status |= TIOCM_DTR;
      } else {
        status &= ~TIOCM_DTR;
      }
      if (ioctl(fd, TIOCMSET, &status) < 0) {
        MOZ_LOG(gWebSerialLog, LogLevel::Error,
                ("PosixSerialPlatformService[%p]::SetSignals set DTR status on "
                 "%d failed: "
                 "newStatus=%x errno=%d",
                 this, fd, uint32_t(status), errno));
        return NS_ERROR_FAILURE;
      }
    }

    if (aSignals.requestToSend().isSome()) {
      if (aSignals.requestToSend().value()) {
        status |= TIOCM_RTS;
      } else {
        status &= ~TIOCM_RTS;
      }
      if (ioctl(fd, TIOCMSET, &status) < 0) {
        MOZ_LOG(gWebSerialLog, LogLevel::Error,
                ("PosixSerialPlatformService[%p]::SetSignals set RTS status on "
                 "%d failed: "
                 "newStatus=%x errno=%d",
                 this, fd, uint32_t(status), errno));
        return NS_ERROR_FAILURE;
      }
    }
  }

  if (aSignals.breakSignal().isSome()) {
    int cmd = aSignals.breakSignal().value() ? TIOCSBRK : TIOCCBRK;
    if (ioctl(fd, cmd) < 0) {
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("PosixSerialPlatformService[%p]::SetSignals ioctl %s on %d failed: "
           "errno=%d",
           this, aSignals.breakSignal().value() ? "TIOCSBRK" : "TIOCCBRK", fd,
           errno));
      return NS_ERROR_FAILURE;
    }
  }

  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("PosixSerialPlatformService[%p]::SetSignals signals set successfully",
       this));
  return NS_OK;
}

nsresult PosixSerialPlatformService::GetSignalsImpl(
    const nsString& aPortId, IPCSerialInputSignals& aSignals) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::GetSignals portId=%s", this,
           NS_ConvertUTF16toUTF8(aPortId).get()));

  int fd = FindPortFd(aPortId);
  if (fd < 0) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::GetSignals port not found: %s",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  int status;
  if (ioctl(fd, TIOCMGET, &status) < 0) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("PosixSerialPlatformService[%p]::GetSignals ioctl TIOCMGET failed: "
         "errno=%d",
         this, errno));
    return NS_ERROR_FAILURE;
  }

  aSignals = IPCSerialInputSignals{
      (status & TIOCM_CAR) != 0,  // dataCarrierDetect
      (status & TIOCM_CTS) != 0,  // clearToSend
      (status & TIOCM_RNG) != 0,  // ringIndicator
      (status & TIOCM_DSR) != 0   // dataSetReady
  };

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::GetSignals DCD=%d, CTS=%d, RI=%d, "
           "DSR=%d",
           this, aSignals.dataCarrierDetect(), aSignals.clearToSend(),
           aSignals.ringIndicator(), aSignals.dataSetReady()));

  return NS_OK;
}

nsresult PosixSerialPlatformService::GetReadStreamImpl(
    const nsString& aPortId, uint32_t aBufferSize, bool aDetectParityErrors,
    nsIAsyncInputStream** aStream) {
  AssertIsOnIOThread();
  int fd = FindPortFd(aPortId);
  if (fd < 0) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::GetReadStream port not found: %s",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }
  UniqueFileHandle readHandle = DuplicateFileHandle(fd);
  if (!readHandle) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::GetReadStream dup failed for "
             "port '%s': errno=%d",
             this, NS_ConvertUTF16toUTF8(aPortId).get(), errno));
    return NS_ERROR_FAILURE;
  }
  RefPtr<PlatformPipeReader> reader =
      MakeRefPtr<PlatformPipeReader>(std::move(readHandle), aBufferSize);
  if (aDetectParityErrors) {
    // PARMRK is enabled (see ConfigurePort): wrap the reader to decode the
    // marker bytes and surface parity errors.
    RefPtr decoder = MakeRefPtr<PosixSerialParityDecodeStream>(reader);
    decoder.forget(aStream);
  } else {
    reader.forget(aStream);
  }
  return NS_OK;
}

nsresult PosixSerialPlatformService::StartMonitoring() {
  return InitializeMacOS();
}

nsresult PosixSerialPlatformService::InitializeMacOS() {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::InitializeMacOS setting up IOKit "
           "notifications",
           this));

  mNotificationPort = IONotificationPortCreate(kIOMainPortDefault);
  if (!mNotificationPort) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::InitializeMacOS "
             "IONotificationPortCreate failed",
             this));
    return NS_ERROR_FAILURE;
  }

  CFRunLoopAddSource(CFRunLoopGetMain(),
                     IONotificationPortGetRunLoopSource(mNotificationPort),
                     kCFRunLoopDefaultMode);
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::InitializeMacOS added run loop "
           "source to main run loop",
           this));

  CFMutableDictionaryRef matchingDict =
      IOServiceMatching(kIOSerialBSDServiceValue);
  if (!matchingDict) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("PosixSerialPlatformService[%p]::InitializeMacOS IOServiceMatching "
         "failed",
         this));
    return NS_ERROR_FAILURE;
  }

  // The call to IOServiceAddMatchingNotification consumes a ref to
  // matchingDict.
  kern_return_t kr = IOServiceAddMatchingNotification(
      mNotificationPort, kIOMatchedNotification, matchingDict,
      DeviceAddedCallback, this, &mAddedIterator);
  if (kr != KERN_SUCCESS) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::InitializeMacOS "
             "IOServiceAddMatchingNotification for added devices failed: %d",
             this, kr));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::InitializeMacOS draining initial "
           "added device iterator",
           this));
  OnDeviceAdded(mAddedIterator, true);

  // The call to IOServiceAddMatchingNotification consumes a ref to
  // matchingDict2.
  CFMutableDictionaryRef matchingDict2 =
      IOServiceMatching(kIOSerialBSDServiceValue);
  if (!matchingDict2) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("PosixSerialPlatformService[%p]::InitializeMacOS IOServiceMatching "
         "failed for removed devices",
         this));
    return NS_ERROR_FAILURE;
  }

  kr = IOServiceAddMatchingNotification(
      mNotificationPort, kIOTerminatedNotification, matchingDict2,
      DeviceRemovedCallback, this, &mRemovedIterator);
  if (kr != KERN_SUCCESS) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("PosixSerialPlatformService[%p]::InitializeMacOS "
             "IOServiceAddMatchingNotification for removed devices failed: %d",
             this, kr));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::InitializeMacOS draining initial "
           "removed device iterator",
           this));
  OnDeviceRemoved(mRemovedIterator, true);

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("PosixSerialPlatformService[%p]::InitializeMacOS monitoring started",
           this));
  return NS_OK;
}


bool PosixSerialPlatformService::ExtractDeviceInfo(
    io_service_t device, IPCSerialPortInfo& portInfo) {
  if (!device) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Debug,
        ("PosixSerialPlatformService[%p]::ExtractDeviceInfo device is null, "
         "device may be terminated",
         this));
    return false;
  }

  CFTypeRef pathRef = IORegistryEntryCreateCFProperty(
      device, CFSTR(kIODialinDeviceKey), kCFAllocatorDefault, 0);

  auto cleanupPath = MakeScopeExit([&]() {
    if (pathRef) CFRelease(pathRef);
  });

  if (!pathRef) {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("PosixSerialPlatformService[%p]::ExtractDeviceInfo failed to get "
             "device path, device may be terminated",
             this));
    return false;
  }

  if (CFGetTypeID(pathRef) != CFStringGetTypeID()) {
    MOZ_LOG(gWebSerialLog, LogLevel::Warning,
            ("PosixSerialPlatformService[%p]::ExtractDeviceInfo device path is "
             "not a string",
             this));
    return false;
  }

  char devicePath[PATH_MAX];
  if (!CFStringGetCString((CFStringRef)pathRef, devicePath, sizeof(devicePath),
                          kCFStringEncodingUTF8)) {
    return false;
  }

  if (IsMacOSSystemSerialPort(devicePath)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("PosixSerialPlatformService[%p]::ExtractDeviceInfo filtering "
             "macOS system port: %s",
             this, devicePath));
    return false;
  }

  NS_ConvertUTF8toUTF16 path(devicePath);
  portInfo.id() = path;
  portInfo.path() = path;

  CFTypeRef baseNameRef = IORegistryEntryCreateCFProperty(
      device, CFSTR(kIOTTYBaseNameKey), kCFAllocatorDefault, 0);
  auto cleanupBaseName = MakeScopeExit([&]() {
    if (baseNameRef) CFRelease(baseNameRef);
  });

  if (baseNameRef) {
    if (CFGetTypeID(baseNameRef) == CFStringGetTypeID()) {
      char baseName[kDeviceNameBufferSize];
      if (CFStringGetCString((CFStringRef)baseNameRef, baseName,
                             sizeof(baseName), kCFStringEncodingUTF8)) {
        NS_ConvertUTF8toUTF16 friendlyName(baseName);
        portInfo.friendlyName() = friendlyName;
      }
    }
  }

  // We search for these properties on all ancestors, because the registry tree
  // can have a serial service with a USB service as its ancestor.
  CFTypeRef vendorIdRef = IORegistryEntrySearchCFProperty(
      device, kIOServicePlane, CFSTR("idVendor"), kCFAllocatorDefault,
      kIORegistryIterateRecursively | kIORegistryIterateParents);
  CFTypeRef productIdRef = IORegistryEntrySearchCFProperty(
      device, kIOServicePlane, CFSTR("idProduct"), kCFAllocatorDefault,
      kIORegistryIterateRecursively | kIORegistryIterateParents);

  auto cleanupVendorIdAndProductId = MakeScopeExit([&]() {
    if (vendorIdRef) CFRelease(vendorIdRef);
    if (productIdRef) CFRelease(productIdRef);
  });

  if (vendorIdRef && productIdRef &&
      CFGetTypeID(vendorIdRef) == CFNumberGetTypeID() &&
      CFGetTypeID(productIdRef) == CFNumberGetTypeID()) {
    SInt32 vendorId, productId;
    if (CFNumberGetValue((CFNumberRef)vendorIdRef, kCFNumberSInt32Type,
                         &vendorId) &&
        CFNumberGetValue((CFNumberRef)productIdRef, kCFNumberSInt32Type,
                         &productId)) {
      portInfo.usbVendorId() = Some(static_cast<uint16_t>(vendorId));
      portInfo.usbProductId() = Some(static_cast<uint16_t>(productId));
    }
  }

  // Detect Bluetooth serial ports by walking the parent chain in the
  // IOService plane and looking for a class whose name contains "Bluetooth"
  // (e.g. IOBluetoothSerialClient).
  bool isBluetooth = false;
  io_iterator_t parentIterator = 0;
  if (IORegistryEntryCreateIterator(
          device, kIOServicePlane,
          kIORegistryIterateRecursively | kIORegistryIterateParents,
          &parentIterator) == KERN_SUCCESS) {
    io_object_t parent;
    while (!isBluetooth && (parent = IOIteratorNext(parentIterator))) {
      io_name_t className;
      if (IOObjectGetClass(parent, className) == KERN_SUCCESS &&
          strstr(className, "Bluetooth")) {
        isBluetooth = true;
      }
      IOObjectRelease(parent);
    }
    IOObjectRelease(parentIterator);
  }
  if (isBluetooth) {
    // Try to find the actual service class UUID by correlating the
    // IOBluetoothSerialClient's RFCOMM channel to the matching SDP record on
    // the IOBluetoothDevice ancestor. If that fails for any reason (missing
    // registry properties, no SDP record, framework error), fall back to
    // SPP, which is correct for the overwhelmingly common case.
    Maybe<nsString> resolvedUuid = LookupMacBluetoothServiceClassId(device);
    portInfo.bluetoothServiceClassId() =
        Some(resolvedUuid.valueOr(nsString(kBluetoothSerialPortProfileUUID)));
    // Bluetooth ports should not also report USB IDs even if a USB Bluetooth
    // adapter shows up as a USB ancestor in the registry.
    portInfo.usbVendorId() = Nothing();
    portInfo.usbProductId() = Nothing();
  }

  CFTypeRef productNameRef = IORegistryEntrySearchCFProperty(
      device, kIOServicePlane, CFSTR(kUSBProductString), kCFAllocatorDefault,
      kIORegistryIterateRecursively | kIORegistryIterateParents);
  auto cleanupProductName = MakeScopeExit([&]() {
    if (productNameRef) CFRelease(productNameRef);
  });

  if (productNameRef) {
    if (CFGetTypeID(productNameRef) == CFStringGetTypeID()) {
      char productName[kDeviceNameBufferSize];
      if (CFStringGetCString((CFStringRef)productNameRef, productName,
                             sizeof(productName), kCFStringEncodingUTF8)) {
        NS_ConvertUTF8toUTF16 friendlyName(productName);
        portInfo.friendlyName() = friendlyName;
      }
    }
  }

  if (portInfo.friendlyName().IsEmpty()) {
    portInfo.friendlyName() = path;
  }

  return true;
}

void PosixSerialPlatformService::DeviceAddedCallback(void* context,
                                                     io_iterator_t iterator) {
  PosixSerialPlatformService* service =
      static_cast<PosixSerialPlatformService*>(context);
  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("PosixSerialPlatformService[%p]::DeviceAddedCallback invoked", service));
  service->OnDeviceAdded(iterator, false);
}

void PosixSerialPlatformService::DeviceRemovedCallback(void* context,
                                                       io_iterator_t iterator) {
  PosixSerialPlatformService* service =
      static_cast<PosixSerialPlatformService*>(context);
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::DeviceRemovedCallback invoked",
           service));
  service->OnDeviceRemoved(iterator, false);
}

void PosixSerialPlatformService::OnDeviceAdded(io_iterator_t iterator,
                                               bool aSkipNotify) {
  io_service_t device;
  int deviceCount = 0;
  while ((device = IOIteratorNext(iterator))) {
    deviceCount++;
    IPCSerialPortInfo portInfo;
    if (ExtractDeviceInfo(device, portInfo)) {
      MOZ_LOG(gWebSerialLog, LogLevel::Debug,
              ("PosixSerialPlatformService[%p]::OnDeviceAdded skip=%d device "
               ": path=%s, friendlyName=%s, VID=0x%04x, PID=0x%04x, BT:%s",
               this, aSkipNotify, NS_ConvertUTF16toUTF8(portInfo.path()).get(),
               NS_ConvertUTF16toUTF8(portInfo.friendlyName()).get(),
               portInfo.usbVendorId().valueOr(0),
               portInfo.usbProductId().valueOr(0),
               portInfo.bluetoothServiceClassId().isSome()
                   ? NS_ConvertUTF16toUTF8(
                         portInfo.bluetoothServiceClassId().value())
                         .get()
                   : "none"));
      if (!aSkipNotify) {
        NotifyPortConnected(portInfo);
      }
    }

    IOObjectRelease(device);
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::OnDeviceAdded processed %d devices "
           "(skipNotify=%d)",
           this, deviceCount, aSkipNotify));
}

void PosixSerialPlatformService::OnDeviceRemoved(io_iterator_t iterator,
                                                 bool aSkipNotify) {
  io_service_t device;
  int deviceCount = 0;
  while ((device = IOIteratorNext(iterator))) {
    deviceCount++;
    IPCSerialPortInfo portInfo;
    if (ExtractDeviceInfo(device, portInfo)) {
      MOZ_LOG(
          gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::OnDeviceRemoved skip=%d "
           "device: path=%s",
           this, aSkipNotify, NS_ConvertUTF16toUTF8(portInfo.path()).get()));
      if (!aSkipNotify) {
        NotifyPortDisconnected(portInfo.path());
      }
    } else {
      MOZ_LOG(gWebSerialLog, LogLevel::Warning,
              ("PosixSerialPlatformService[%p]::OnDeviceRemoved failed to "
               "extract device info, device may have been terminated",
               this));
    }

    IOObjectRelease(device);
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("PosixSerialPlatformService[%p]::OnDeviceRemoved processed %d "
           "devices (skipNotify=%d)",
           this, deviceCount, aSkipNotify));
}

already_AddRefed<SerialPlatformService>
SerialPlatformService::GetInstanceImpl() {
  return MakeAndAddRef<PosixSerialPlatformService>();
}

}  // namespace mozilla::dom
