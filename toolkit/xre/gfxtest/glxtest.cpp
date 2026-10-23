/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//////////////////////////////////////////////////////////////////////////////
//
// Explanation: See bug 639842. Safely getting GL driver info on X11 is hard,
// because the only way to do that is to create a GL context and call
// glGetString(), but with bad drivers, just creating a GL context may crash.
//
// This file implements the idea to do that in a separate process.
//
// The only non-static function here is fire_glxtest_process(). It creates a
// pipe, publishes its 'read' end as the mozilla::widget::glxtest_pipe global
// variable, forks, and runs that GLX probe in the child process, which runs the
// childgltest() static function. This creates a X connection, a GLX context,
// calls glGetString, and writes that to the 'write' end of the pipe.

#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>


#include <vector>
#include <sys/wait.h>
#include "mozilla/GfxInfoUtils.h"
#include "mozilla/ScopeExit.h"


// stuff from gl.h
typedef uint8_t GLubyte;
typedef uint32_t GLenum;
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

// GLX_MESA_query_renderer
// clang-format off
#define GLX_RENDERER_VENDOR_ID_MESA                            0x8183
#define GLX_RENDERER_DEVICE_ID_MESA                            0x8184
#define GLX_RENDERER_VERSION_MESA                              0x8185
#define GLX_RENDERER_ACCELERATED_MESA                          0x8186
#define GLX_RENDERER_VIDEO_MEMORY_MESA                         0x8187
#define GLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA          0x8188
#define GLX_RENDERER_PREFERRED_PROFILE_MESA                    0x8189
#define GLX_RENDERER_OPENGL_CORE_PROFILE_VERSION_MESA          0x818A
#define GLX_RENDERER_OPENGL_COMPATIBILITY_PROFILE_VERSION_MESA 0x818B
#define GLX_RENDERER_OPENGL_ES_PROFILE_VERSION_MESA            0x818C
#define GLX_RENDERER_OPENGL_ES2_PROFILE_VERSION_MESA           0x818D
#define GLX_RENDERER_ID_MESA                                   0x818E
// clang-format on

// stuff from egl.h
typedef intptr_t EGLAttrib;
typedef int EGLBoolean;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLDeviceEXT;
typedef void* EGLDisplay;
typedef unsigned int EGLenum;
typedef int EGLint;
typedef void* EGLNativeDisplayType;
typedef void* EGLSurface;
typedef void* (*PFNEGLGETPROCADDRESS)(const char*);

#define EGL_NO_CONTEXT nullptr
#define EGL_NO_SURFACE nullptr
#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_NONE 0x3038
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_VENDOR 0x3053
#define EGL_EXTENSIONS 0x3055
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_OPENGL_API 0x30A2
#define EGL_DEVICE_EXT 0x322C
#define EGL_DRM_DEVICE_FILE_EXT 0x3233
#define EGL_DRM_RENDER_NODE_FILE_EXT 0x3377

// DRM fourcc format codes
#define DRM_FORMAT_XRGB8888 0x34325258
#define DRM_FORMAT_ARGB8888 0x34325241
#define DRM_FORMAT_NV12 0x3231564e
#define DRM_FORMAT_P010 0x30313050
#define DRM_FORMAT_YUV420 0x32315559
#define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)

// stuff from xf86drm.h
#define DRM_NODE_RENDER 2
#define DRM_NODE_MAX 3

typedef struct _drmPciDeviceInfo {
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t subvendor_id;
  uint16_t subdevice_id;
  uint8_t revision_id;
} drmPciDeviceInfo, *drmPciDeviceInfoPtr;

typedef struct _drmDevice {
  char** nodes;
  int available_nodes;
  int bustype;
  union {
    void* pci;
    void* usb;
    void* platform;
    void* host1x;
  } businfo;
  union {
    drmPciDeviceInfoPtr pci;
    void* usb;
    void* platform;
    void* host1x;
  } deviceinfo;
} drmDevice, *drmDevicePtr;

// Open libGL and load needed symbols
#  define LIBGL_FILENAME "libGL.so.1"
#  define LIBGLES_FILENAME "libGLESv2.so.2"
#  define LIBEGL_FILENAME "libEGL.so.1"
#  define LIBDRM_FILENAME "libdrm.so.2"


// childgltest is declared inside extern "C" so that the name is not mangled.
// The name is used in build/valgrind/x86_64-pc-linux-gnu.sup to suppress
// memory leak errors because we run it inside a short lived fork and we don't
// care about leaking memory
extern "C" {

#define PCI_FILL_IDENT 0x0001
#define PCI_FILL_CLASS 0x0020
#define PCI_BASE_CLASS_DISPLAY 0x03

static void get_pci_status() {
  log("GLX_TEST: get_pci_status start\n");

#if !defined(XP_FREEBSD) && !defined(XP_NETBSD) && !defined(XP_OPENBSD) && \
    !defined(XP_SOLARIS)
  if (access("/sys/bus/pci/", F_OK) != 0 &&
      access("/sys/bus/pci_express/", F_OK) != 0) {
    log("GLX_TEST: get_pci_status failed: cannot access /sys/bus/pci\n");
    return;
  }

  void* libpci = dlopen("libpci.so.3", RTLD_LAZY);
  if (!libpci) {
    libpci = dlopen("libpci.so", RTLD_LAZY);
  }
  if (!libpci) {
    record_warning("libpci missing");
    return;
  }
  auto release = mozilla::MakeScopeExit([&] { dlclose(libpci); });

  typedef struct pci_dev {
    struct pci_dev* next;
    uint16_t domain_16;
    uint8_t bus, dev, func;
    unsigned int known_fields;
    uint16_t vendor_id, device_id;
    uint16_t device_class;
  } pci_dev;

  typedef struct pci_access {
    unsigned int method;
    int writeable;
    int buscentric;
    char* id_file_name;
    int free_id_name;
    int numeric_ids;
    unsigned int id_lookup_mode;
    int debugging;
    void* error;
    void* warning;
    void* debug;
    pci_dev* devices;
  } pci_access;

  typedef pci_access* (*PCIALLOC)(void);
  PCIALLOC pci_alloc = cast<PCIALLOC>(dlsym(libpci, "pci_alloc"));

  typedef void (*PCIINIT)(pci_access*);
  PCIINIT pci_init = cast<PCIINIT>(dlsym(libpci, "pci_init"));

  typedef void (*PCICLEANUP)(pci_access*);
  PCICLEANUP pci_cleanup = cast<PCICLEANUP>(dlsym(libpci, "pci_cleanup"));

  typedef void (*PCISCANBUS)(pci_access*);
  PCISCANBUS pci_scan_bus = cast<PCISCANBUS>(dlsym(libpci, "pci_scan_bus"));

  typedef void (*PCIFILLINFO)(pci_dev*, int);
  PCIFILLINFO pci_fill_info = cast<PCIFILLINFO>(dlsym(libpci, "pci_fill_info"));

  if (!pci_alloc || !pci_cleanup || !pci_scan_bus || !pci_fill_info) {
    dlclose(libpci);
    record_warning("libpci missing methods");
    return;
  }

  pci_access* pacc = pci_alloc();
  if (!pacc) {
    record_warning("libpci alloc failed");
    return;
  }

  pci_init(pacc);
  pci_scan_bus(pacc);

  for (pci_dev* dev = pacc->devices; dev; dev = dev->next) {
    pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_CLASS);
    if (dev->device_class >> 8 == PCI_BASE_CLASS_DISPLAY && dev->vendor_id &&
        dev->device_id) {
      record_value("PCI_VENDOR_ID\n0x%04x\nPCI_DEVICE_ID\n0x%04x\n",
                   dev->vendor_id, dev->device_id);
    }
  }

  pci_cleanup(pacc);
#endif

  log("GLX_TEST: get_pci_status finished\n");
}

static void set_render_device_path(const char* render_device_path) {
  record_value("DRM_RENDERDEVICE\n%s\n", render_device_path);
}

static bool device_has_name(const drmDevice* device, const char* name) {
  for (size_t i = 0; i < DRM_NODE_MAX; i++) {
    if (!(device->available_nodes & (1 << i))) {
      continue;
    }
    if (strcmp(device->nodes[i], name) == 0) {
      return true;
    }
  }
  return false;
}

static bool get_render_name(const char* name) {
  void* libdrm = dlopen(LIBDRM_FILENAME, RTLD_LAZY);
  if (!libdrm) {
    record_warning("Failed to open libdrm");
    return false;
  }
  auto release = mozilla::MakeScopeExit([&] { dlclose(libdrm); });

  typedef int (*DRMGETDEVICES2)(uint32_t, drmDevicePtr*, int);
  DRMGETDEVICES2 drmGetDevices2 =
      cast<DRMGETDEVICES2>(dlsym(libdrm, "drmGetDevices2"));

  typedef void (*DRMFREEDEVICE)(drmDevicePtr*);
  DRMFREEDEVICE drmFreeDevice =
      cast<DRMFREEDEVICE>(dlsym(libdrm, "drmFreeDevice"));

  if (!drmGetDevices2 || !drmFreeDevice) {
    record_warning(
        "libdrm missing methods for drmGetDevices2 or drmFreeDevice");
    return false;
  }

  uint32_t flags = 0;
  int devices_len = drmGetDevices2(flags, nullptr, 0);
  if (devices_len < 0) {
    record_warning("drmGetDevices2 failed");
    return false;
  }
  drmDevice** devices = (drmDevice**)calloc(devices_len, sizeof(drmDevice*));
  if (!devices) {
    record_warning("Allocation error");
    return false;
  }
  devices_len = drmGetDevices2(flags, devices, devices_len);
  if (devices_len < 0) {
    free(devices);
    record_warning("drmGetDevices2 failed");
    return false;
  }

  const drmDevice* match = nullptr;
  for (int i = 0; i < devices_len; i++) {
    if (device_has_name(devices[i], name)) {
      match = devices[i];
      break;
    }
  }

  // Fallback path for split kms/render devices - if only one drm render node
  // exists it's most likely the one we're looking for.
  if (match && !(match->available_nodes & (1 << DRM_NODE_RENDER))) {
    match = nullptr;
    for (int i = 0; i < devices_len; i++) {
      if (devices[i]->available_nodes & (1 << DRM_NODE_RENDER)) {
        if (!match) {
          match = devices[i];
        } else {
          // more than one candidate found, stop trying.
          match = nullptr;
          break;
        }
      }
    }
    if (match) {
      record_warning(
          "DRM render node not clearly detectable. Falling back to using the "
          "only one that was found.");
    } else {
      record_warning("DRM device has no render node");
    }
  }

  bool result = false;
  if (!match) {
    record_warning("Cannot find DRM device");
  } else {
    set_render_device_path(match->nodes[DRM_NODE_RENDER]);
    record_value(
        "MESA_VENDOR_ID\n0x%04x\n"
        "MESA_DEVICE_ID\n0x%04x\n",
        match->deviceinfo.pci->vendor_id, match->deviceinfo.pci->device_id);
    result = true;
  }

  for (int i = 0; i < devices_len; i++) {
    drmFreeDevice(&devices[i]);
  }
  free(devices);
  return result;
}

static void query_egl_dmabuf_modifiers(EGLDisplay dpy,
                                       PFNEGLGETPROCADDRESS eglGetProcAddress) {
  typedef const char* (*PFNEGLQUERYSTRINGPROC)(EGLDisplay dpy, EGLint name);
  typedef EGLBoolean (*PFNEGLQUERYDMABUFMODIFIERSEXTPROC)(
      EGLDisplay dpy, EGLint format, EGLint max_modifiers, uint64_t* modifiers,
      EGLBoolean* external_only, EGLint* num_modifiers);

  PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT =
      [&]() -> PFNEGLQUERYDMABUFMODIFIERSEXTPROC {
    PFNEGLQUERYSTRINGPROC eglQueryString =
        cast<PFNEGLQUERYSTRINGPROC>(eglGetProcAddress("eglQueryString"));
    if (!eglQueryString) {
      return nullptr;
    }
    const char* extensions = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!extensions ||
        !strstr(extensions, "EGL_EXT_image_dma_buf_import_modifiers")) {
      return nullptr;
    }
    return cast<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>(
        eglGetProcAddress("eglQueryDmaBufModifiersEXT"));
  }();

  if (!eglQueryDmaBufModifiersEXT) {
    return;
  }

  struct {
    uint32_t format;
    const char* name;
  } formats[] = {
      {DRM_FORMAT_XRGB8888, "DMABUF_MODIFIERS_XRGB"},
      {DRM_FORMAT_ARGB8888, "DMABUF_MODIFIERS_ARGB"},
      {DRM_FORMAT_NV12, "DMABUF_MODIFIERS_NV12"},
      {DRM_FORMAT_P010, "DMABUF_MODIFIERS_P010"},
      {DRM_FORMAT_YUV420, "DMABUF_MODIFIERS_YUV420"},
  };

  for (const auto& fmt : formats) {
    EGLint numMods = 0;
    if (!eglQueryDmaBufModifiersEXT(dpy, static_cast<EGLint>(fmt.format), 0,
                                    nullptr, nullptr, &numMods) ||
        numMods <= 0) {
      continue;
    }

    std::vector<uint64_t> mods(numMods);
    EGLint n = numMods;
    if (!eglQueryDmaBufModifiersEXT(dpy, static_cast<EGLint>(fmt.format), n,
                                    mods.data(), nullptr, &n) ||
        n <= 0) {
      continue;
    }

    // Build the modifier list, skipping DRM_FORMAT_MOD_INVALID.
    // Count valid modifiers first to avoid emitting a key with no value,
    // which would confuse the line-based key-value parser.
    int validCount = 0;
    for (EGLint i = 0; i < n; i++) {
      if (mods[i] != DRM_FORMAT_MOD_INVALID) {
        validCount++;
      }
    }
    if (validCount == 0) {
      continue;
    }

    record_value("%s\n", fmt.name);
    bool first = true;
    for (EGLint i = 0; i < n; i++) {
      if (mods[i] == DRM_FORMAT_MOD_INVALID) {
        continue;
      }
      if (!first) {
        record_value(",");
      }
      record_value("%" PRIx64, mods[i]);
      first = false;
    }
    record_value("\n");
  }
}

static bool get_egl_gl_status(EGLDisplay dpy,
                              PFNEGLGETPROCADDRESS eglGetProcAddress) {
  typedef EGLBoolean (*PFNEGLCHOOSECONFIGPROC)(
      EGLDisplay dpy, EGLint const* attrib_list, EGLConfig* configs,
      EGLint config_size, EGLint* num_config);
  PFNEGLCHOOSECONFIGPROC eglChooseConfig =
      cast<PFNEGLCHOOSECONFIGPROC>(eglGetProcAddress("eglChooseConfig"));

  typedef EGLBoolean (*PFNEGLBINDAPIPROC)(EGLint api);
  PFNEGLBINDAPIPROC eglBindAPI =
      cast<PFNEGLBINDAPIPROC>(eglGetProcAddress("eglBindAPI"));

  typedef EGLContext (*PFNEGLCREATECONTEXTPROC)(
      EGLDisplay dpy, EGLConfig config, EGLContext share_context,
      EGLint const* attrib_list);
  PFNEGLCREATECONTEXTPROC eglCreateContext =
      cast<PFNEGLCREATECONTEXTPROC>(eglGetProcAddress("eglCreateContext"));

  typedef EGLBoolean (*PFNEGLDESTROYCONTEXTPROC)(EGLDisplay dpy,
                                                 EGLContext ctx);
  PFNEGLDESTROYCONTEXTPROC eglDestroyContext =
      cast<PFNEGLDESTROYCONTEXTPROC>(eglGetProcAddress("eglDestroyContext"));

  typedef EGLBoolean (*PFNEGLMAKECURRENTPROC)(
      EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext context);
  PFNEGLMAKECURRENTPROC eglMakeCurrent =
      cast<PFNEGLMAKECURRENTPROC>(eglGetProcAddress("eglMakeCurrent"));

  typedef const char* (*PFNEGLQUERYDEVICESTRINGEXTPROC)(EGLDeviceEXT device,
                                                        EGLint name);
  PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT =
      cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(
          eglGetProcAddress("eglQueryDeviceStringEXT"));

  typedef EGLBoolean (*PFNEGLQUERYDISPLAYATTRIBEXTPROC)(
      EGLDisplay dpy, EGLint name, EGLAttrib* value);
  PFNEGLQUERYDISPLAYATTRIBEXTPROC eglQueryDisplayAttribEXT =
      cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(
          eglGetProcAddress("eglQueryDisplayAttribEXT"));

  log("GLX_TEST: get_egl_gl_status start\n");

  if (!eglChooseConfig || !eglCreateContext || !eglDestroyContext ||
      !eglMakeCurrent || !eglQueryDeviceStringEXT) {
    record_warning("libEGL missing methods for GL test");
    return false;
  }

  typedef GLubyte* (*PFNGLGETSTRING)(GLenum);
  PFNGLGETSTRING glGetString =
      cast<PFNGLGETSTRING>(eglGetProcAddress("glGetString"));

#if defined(__arm__) || defined(__aarch64__)
  bool useGles = true;
#else
  bool useGles = false;
#endif

  std::vector<EGLint> attribs;
  attribs.push_back(EGL_RED_SIZE);
  attribs.push_back(8);
  attribs.push_back(EGL_GREEN_SIZE);
  attribs.push_back(8);
  attribs.push_back(EGL_BLUE_SIZE);
  attribs.push_back(8);
  if (useGles) {
    attribs.push_back(EGL_RENDERABLE_TYPE);
    attribs.push_back(EGL_OPENGL_ES2_BIT);
  }
  attribs.push_back(EGL_NONE);

  EGLConfig config;
  EGLint num_config;
  if (eglChooseConfig(dpy, attribs.data(), &config, 1, &num_config) ==
      EGL_FALSE) {
    record_warning("eglChooseConfig returned an error");
    return false;
  }

  EGLenum api = useGles ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
  if (eglBindAPI(api) == EGL_FALSE) {
    record_warning("eglBindAPI returned an error");
    return false;
  }

  EGLint ctx_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext ectx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attrs);
  if (!ectx) {
    EGLint ctx_attrs_fallback[] = {EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE};
    ectx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attrs_fallback);
    if (!ectx) {
      record_warning("eglCreateContext returned an error");
      return false;
    }
  }

  if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ectx) == EGL_FALSE) {
    eglDestroyContext(dpy, ectx);
    record_warning("eglMakeCurrent returned an error");
    return false;
  }
  eglDestroyContext(dpy, ectx);

  // Implementations disagree about whether eglGetProcAddress or dlsym
  // should be used for getting functions from the actual API, see
  // https://github.com/anholt/libepoxy/commit/14f24485e33816139398d1bd170d617703473738
  void* libgl = nullptr;
  if (!glGetString) {
    libgl = dlopen(LIBGL_FILENAME, RTLD_LAZY);
    if (!libgl) {
      libgl = dlopen(LIBGLES_FILENAME, RTLD_LAZY);
      if (!libgl) {
        record_warning(LIBGL_FILENAME " and " LIBGLES_FILENAME " missing");
        return false;
      }
    }

    glGetString = cast<PFNGLGETSTRING>(dlsym(libgl, "glGetString"));
    if (!glGetString) {
      dlclose(libgl);
      record_warning("libEGL, libGL and libGLESv2 are missing glGetString");
      return false;
    }
  }
  auto release = mozilla::MakeScopeExit([&] {
    if (libgl) {
      dlclose(libgl);
    }
  });

  const GLubyte* versionString = glGetString(GL_VERSION);
  const GLubyte* vendorString = glGetString(GL_VENDOR);
  const GLubyte* rendererString = glGetString(GL_RENDERER);

  if (versionString && vendorString && rendererString) {
    record_value("VENDOR\n%s\nRENDERER\n%s\nVERSION\n%s\nTFP\nTRUE\n",
                 vendorString, rendererString, versionString);
  } else {
    record_warning("EGL glGetString returned null");
    return false;
  }

  EGLDeviceEXT device;
  if (eglQueryDisplayAttribEXT(dpy, EGL_DEVICE_EXT, (EGLAttrib*)&device) ==
      EGL_TRUE) {
    const char* deviceExtensions =
        eglQueryDeviceStringEXT(device, EGL_EXTENSIONS);
    if (deviceExtensions &&
        strstr(deviceExtensions, "EGL_MESA_device_software")) {
      record_value("MESA_ACCELERATED\nFALSE\n");
    } else {
      const char* deviceString =
          eglQueryDeviceStringEXT(device, EGL_DRM_DEVICE_FILE_EXT);
      if (!deviceString || !get_render_name(deviceString)) {
        const char* renderNodeString =
            eglQueryDeviceStringEXT(device, EGL_DRM_RENDER_NODE_FILE_EXT);
        if (renderNodeString) {
          set_render_device_path(renderNodeString);
        }
      }
    }
  }

  log("GLX_TEST: get_egl_gl_status finished\n");
  return true;
}

static bool get_egl_status(EGLNativeDisplayType native_dpy,
                           bool aLoadModifiers) {
  log("GLX_TEST: get_egl_status start\n");

  EGLDisplay dpy = nullptr;

  typedef EGLBoolean (*PFNEGLTERMINATEPROC)(EGLDisplay dpy);
  PFNEGLTERMINATEPROC eglTerminate = nullptr;

  void* libegl = dlopen(LIBEGL_FILENAME, RTLD_LAZY);
  if (!libegl) {
    record_warning("libEGL missing");
    return false;
  }
  auto release = mozilla::MakeScopeExit([&] {
    if (dpy) {
      eglTerminate(dpy);
    }
    // Unload libegl here causes ASAN/MemLeaks failures on Linux
    // as libegl may not be meant to be unloaded runtime.
    // See 1304156 for reference.
    // dlclose(libegl);
  });

  PFNEGLGETPROCADDRESS eglGetProcAddress =
      cast<PFNEGLGETPROCADDRESS>(dlsym(libegl, "eglGetProcAddress"));

  if (!eglGetProcAddress) {
    record_warning("no eglGetProcAddress");
    return false;
  }

  typedef EGLDisplay (*PFNEGLGETDISPLAYPROC)(void* native_display);
  PFNEGLGETDISPLAYPROC eglGetDisplay =
      cast<PFNEGLGETDISPLAYPROC>(eglGetProcAddress("eglGetDisplay"));

  typedef EGLBoolean (*PFNEGLINITIALIZEPROC)(EGLDisplay dpy, EGLint* major,
                                             EGLint* minor);
  PFNEGLINITIALIZEPROC eglInitialize =
      cast<PFNEGLINITIALIZEPROC>(eglGetProcAddress("eglInitialize"));
  eglTerminate = cast<PFNEGLTERMINATEPROC>(eglGetProcAddress("eglTerminate"));

  if (!eglGetDisplay || !eglInitialize || !eglTerminate) {
    record_warning("libEGL missing methods");
    return false;
  }

  log("GLX_TEST: get_egl_status eglGetDisplay()\n");

  dpy = eglGetDisplay(native_dpy);
  if (!dpy) {
    record_warning("libEGL no display");
    return false;
  }

  log("GLX_TEST: get_egl_status eglInitialize()\n");

  EGLint major, minor;
  if (!eglInitialize(dpy, &major, &minor)) {
    record_warning("libEGL initialize failed");
    return false;
  }

  log("GLX_TEST: get_egl_status eglInitialize() OK\n");

  typedef const char* (*PFNEGLGETDISPLAYDRIVERNAMEPROC)(EGLDisplay dpy);
  PFNEGLGETDISPLAYDRIVERNAMEPROC eglGetDisplayDriverName =
      cast<PFNEGLGETDISPLAYDRIVERNAMEPROC>(
          eglGetProcAddress("eglGetDisplayDriverName"));
  if (eglGetDisplayDriverName) {
    log("GLX_TEST: get_egl_status eglGetDisplayDriverName()\n");
    const char* driDriver = eglGetDisplayDriverName(dpy);
    if (driDriver) {
      record_value("DRI_DRIVER\n%s\n", driDriver);
    }
  }

  bool ret = get_egl_gl_status(dpy, eglGetProcAddress);

  if (aLoadModifiers) {
    query_egl_dmabuf_modifiers(dpy, eglGetProcAddress);
  }

  log("GLX_TEST: get_egl_status finished with return: %d\n", ret);

  return ret;
}



int childgltest(bool aWayland) {
  log("GLX_TEST: childgltest start\n");

  // Get a list of all GPUs from the PCI bus.
  get_pci_status();

  // Finally write buffered data to the pipe.
  record_flush();

  log("GLX_TEST: childgltest finished\n");
  return EXIT_SUCCESS;
}

}  // extern "C"

int glxtest(bool aWayland, int aOutputFd) {
  output_pipe = aOutputFd;
  if (getenv("MOZ_AVOID_OPENGL_ALTOGETHER")) {
    const char* msg = "ERROR\nMOZ_AVOID_OPENGL_ALTOGETHER envvar set";
    [[maybe_unused]] ssize_t _ = write(output_pipe, msg, strlen(msg));
    exit(EXIT_FAILURE);
  }
  const char* env = getenv("MOZ_GFX_DEBUG");
  enable_logging = env && *env == '1';
  if (!enable_logging) {
    close_logging();
  }
  return childgltest(aWayland);
}
