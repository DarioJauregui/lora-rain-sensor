#include "RainCrypto.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main(){
 uint8_t root[32]{},key[32],nonce[12]{},aad[12]{},plain[100]{},cipher[100],out[100],tag[16];
 assert(raincrypto::derive(root,key));assert(memcmp(root,key,32)!=0);
 for(int i=0;i<100;++i)plain[i]=i;
 assert(raincrypto::crypt(true,key,nonce,aad,12,plain,cipher,tag));assert(memcmp(plain,cipher,100)!=0);
 assert(raincrypto::crypt(false,key,nonce,aad,12,cipher,out,tag));assert(memcmp(plain,out,100)==0);
 cipher[1]^=1;assert(!raincrypto::crypt(false,key,nonce,aad,12,cipher,out,tag));cipher[1]^=1;
 aad[0]^=1;assert(!raincrypto::crypt(false,key,nonce,aad,12,cipher,out,tag));aad[0]^=1;
 nonce[0]^=1;assert(!raincrypto::crypt(false,key,nonce,aad,12,cipher,out,tag));nonce[0]^=1;
 tag[0]^=1;assert(!raincrypto::crypt(false,key,nonce,aad,12,cipher,out,tag));tag[0]^=1;
 root[0]=1;assert(raincrypto::derive(root,key));assert(!raincrypto::crypt(false,key,nonce,aad,12,cipher,out,tag));
 std::cout<<"HKDF separation and AES-GCM roundtrip/tampering tests passed\n";
}
