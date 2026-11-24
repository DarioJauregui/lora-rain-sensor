#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <mbedtls/md.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/gcm.h>
namespace raincrypto {
inline bool derive(const uint8_t *pair,uint8_t *key){
  const uint8_t salt[]="LoraRainSensor protocol 3",info[]="management AES-256-GCM";
  return mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),salt,sizeof(salt)-1,pair,32,info,sizeof(info)-1,key,32)==0;
}
inline bool crypt(bool encrypt,const uint8_t *key,const uint8_t *nonce,const uint8_t *aad,size_t aadSize,
                  const uint8_t *in,uint8_t *out,uint8_t *tag){
  mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);
  int rc=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,256);
  if(!rc){if(encrypt)rc=mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,100,nonce,12,aad,aadSize,in,out,16,tag);
    else rc=mbedtls_gcm_auth_decrypt(&ctx,100,nonce,12,aad,aadSize,tag,16,in,out);}
  mbedtls_gcm_free(&ctx);return rc==0;
}
}
