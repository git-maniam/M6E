/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CRYPTOX_H
#define CRYPTOX_H

#define XP_MIN_SIGNATURE_LEN_IN_BYTES 256

#define CryptoX_Result int
#define CryptoX_Success 0
#define CryptoX_Error (-1)

#if defined(MAR_NSS)

#  include "cert.h"
#  include "keyhi.h"
#  include "cryptohi.h"

#  define CryptoX_InvalidHandleValue NULL
#  define CryptoX_ProviderHandle void*
#  define CryptoX_SignatureHandle VFYContext*
#  define CryptoX_PublicKey SECKEYPublicKey*
#  define CryptoX_Certificate CERTCertificate*

#  ifdef __cplusplus
extern "C" {
#  endif
CryptoX_Result NSS_LoadPublicKey(const unsigned char* certData,
                                 unsigned int certDataSize,
                                 SECKEYPublicKey** publicKey);
CryptoX_Result NSS_VerifyBegin(VFYContext** ctx,
                               SECKEYPublicKey* const* publicKey);
CryptoX_Result NSS_VerifySignature(VFYContext* const* ctx,
                                   const unsigned char* signature,
                                   unsigned int signatureLen);
#  ifdef __cplusplus
}  // extern "C"
#  endif

#  define CryptoX_InitCryptoProvider(CryptoHandle) CryptoX_Success
#  define CryptoX_VerifyBegin(CryptoHandle, SignatureHandle, PublicKey) \
    NSS_VerifyBegin(SignatureHandle, PublicKey)
#  define CryptoX_FreeSignatureHandle(SignatureHandle) \
    VFY_DestroyContext(*SignatureHandle, PR_TRUE)
#  define CryptoX_VerifyUpdate(SignatureHandle, buf, len) \
    VFY_Update(*SignatureHandle, (const unsigned char*)(buf), len)
#  define CryptoX_LoadPublicKey(CryptoHandle, certData, dataSize, publicKey) \
    NSS_LoadPublicKey(certData, dataSize, publicKey)
#  define CryptoX_VerifySignature(hash, publicKey, signedData, len) \
    NSS_VerifySignature(hash, (const unsigned char*)(signedData), len)
#  define CryptoX_FreePublicKey(key) SECKEY_DestroyPublicKey(*key)
#  define CryptoX_FreeCertificate(cert) CERT_DestroyCertificate(*cert)

#else

#  define CryptoX_InvalidHandleValue NULL
#  define CryptoX_ProviderHandle void*
#  define CryptoX_SignatureHandle void*
#  define CryptoX_PublicKey void*
#  define CryptoX_Certificate void*

// Forward-declare Objective-C functions implemented in MacVerifyCrypto.mm.
#  ifdef __cplusplus
extern "C" {
#  endif
CryptoX_Result CryptoMac_InitCryptoProvider();
CryptoX_Result CryptoMac_VerifyBegin(CryptoX_SignatureHandle* aInputData);
CryptoX_Result CryptoMac_VerifyUpdate(CryptoX_SignatureHandle* aInputData,
                                      void* aBuf, unsigned int aLen);
CryptoX_Result CryptoMac_LoadPublicKey(const unsigned char* aCertData,
                                       unsigned int aDataSize,
                                       CryptoX_PublicKey* aPublicKey);
CryptoX_Result CryptoMac_VerifySignature(CryptoX_SignatureHandle* aInputData,
                                         CryptoX_PublicKey* aPublicKey,
                                         const unsigned char* aSignature,
                                         unsigned int aSignatureLen);
void CryptoMac_FreeSignatureHandle(CryptoX_SignatureHandle* aInputData);
void CryptoMac_FreePublicKey(CryptoX_PublicKey* aPublicKey);
#  ifdef __cplusplus
}  // extern "C"
#  endif

#  define CryptoX_InitCryptoProvider(aProviderHandle) \
    CryptoMac_InitCryptoProvider()
#  define CryptoX_VerifyBegin(aCryptoHandle, aInputData, aPublicKey) \
    CryptoMac_VerifyBegin(aInputData)
#  define CryptoX_VerifyUpdate(aInputData, aBuf, aLen) \
    CryptoMac_VerifyUpdate(aInputData, aBuf, aLen)
#  define CryptoX_LoadPublicKey(aProviderHandle, aCertData, aDataSize, \
                                aPublicKey)                            \
    CryptoMac_LoadPublicKey(aCertData, aDataSize, aPublicKey)
#  define CryptoX_VerifySignature(aInputData, aPublicKey, aSignature, \
                                  aSignatureLen)                      \
    CryptoMac_VerifySignature(aInputData, aPublicKey, aSignature, aSignatureLen)
#  define CryptoX_FreeSignatureHandle(aInputData) \
    CryptoMac_FreeSignatureHandle(aInputData)
#  define CryptoX_FreePublicKey(aPublicKey) CryptoMac_FreePublicKey(aPublicKey)
#  define CryptoX_FreeCertificate(aCertificate)

#endif

#endif
