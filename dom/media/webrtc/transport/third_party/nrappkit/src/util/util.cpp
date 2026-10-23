/**
   util.c


   Copyright (C) 2001-2003, Network Resonance, Inc.
   Copyright (C) 2006, Network Resonance, Inc.
   All Rights Reserved

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of Network Resonance, Inc. nor the name of any
      contributors to this software may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.


   ekr@rtfm.com  Wed Dec 26 17:19:36 2001
 */

#include <sys/uio.h>
#include <pwd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef OPENSSL
#include <openssl/evp.h>
#endif
#include <csi_platform.h>
#include "r_common.h"
#include "registry.h"
#include "util.h"
#include "r_log.h"

int nr_util_default_log_facility=LOG_COMMON;

int nr_bin2hex(UCHAR *in,int len,UCHAR *out)
  {
    while(len){
      sprintf((char*)out,"%.2x",in[0] & 0xff);

      in+=1;
      out+=2;

      len--;
    }

    return(0);
  }


