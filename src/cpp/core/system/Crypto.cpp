/*
 * Crypto.cpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */


#include <core/system/Crypto.hpp>

#include <fcntl.h>

#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <algorithm>

#include <boost/utility.hpp>

#include <core/Log.hpp>
#include <core/Error.hpp>

// openssl calls on lion are are all marked as deprecated
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

// openssl encrypt/decrypt constants
#define kEncrypt 1
#define kDecrypt 0

using namespace rstudio::core;

namespace rstudio {
namespace core {
namespace system {
namespace crypto {

namespace {

// NOTE: we've never see the error codepath in spite of trying to get it to
// return an error by tweaking params -- NULL params caused a crash rather
// than returning an error). this is presumably because we are calling
// such low level functions. we may want to look at the src code to see
// if there is a way to test/engineer an error (or remove the error
// checking if there is no way to get one)

Error lastCryptoError(const ErrorLocation& location)
{
   // get the error code
   unsigned long ec = ::ERR_get_error();
   if (ec == 0)
   {
      LOG_WARNING_MESSAGE("lastCrytpoError called with no pending error");
      return systemError(boost::system::errc::not_supported,
                         "lastCrytpoError called with no pending error",
                         location);
   }
   
   // get the error message (docs say max len is 120)
   const int ERR_BUFF_SIZE = 250; 
   char errorBuffer[ERR_BUFF_SIZE];
   ::ERR_error_string_n(ec, errorBuffer, ERR_BUFF_SIZE);
   
   // return the error
   return systemError(boost::system::errc::bad_message,
                      errorBuffer,
                      location);
} 
   
class BIOFreeAllScope : boost::noncopyable
{
public:
   BIOFreeAllScope(BIO* pMem)
      : pMem_(pMem)
   {
   }
   
   ~BIOFreeAllScope()
   {
      try
      {
         ::BIO_free_all(pMem_);
      }
      catch(...)
      {
      }
   }
private:
   BIO* pMem_;
};
   
} // anonymous namespace
   
void initialize()
{
   // load global error string table
   ::ERR_load_crypto_strings();
}
   
Error HMAC_SHA2(const std::string& data,
                const std::string& key,
                std::vector<unsigned char>* pHMAC)
{
   // copy data into vector
   std::vector<unsigned char> keyVector;
   std::copy(key.begin(), key.end(), std::back_inserter(keyVector));  
   
   // call core
   return HMAC_SHA2(data, keyVector, pHMAC);
}
   
Error HMAC_SHA2(const std::string& data,
                const std::vector<unsigned char>& key,
                std::vector<unsigned char>* pHMAC)
{
   // copy data into data vector
   std::vector<unsigned char> dataVector;
   std::copy(data.begin(), data.end(), std::back_inserter(dataVector));
   
   // perform the hash
   unsigned int md_len = 0;
   pHMAC->resize(EVP_MAX_MD_SIZE);
   unsigned char* pResult = ::HMAC(EVP_sha256(),
                                   &(key[0]),
                                   key.size(),
                                   &(dataVector[0]),
                                   dataVector.size(),
                                   &(pHMAC->operator[](0)),
                                   &md_len);
   if (pResult != NULL)
   {
      pHMAC->resize(md_len);
      return Success();
   }
   else
   {
      return lastCryptoError(ERROR_LOCATION);
   }
}

Error base64Encode(const std::vector<unsigned char>& data, 
                   std::string* pEncoded)
{
   return base64Encode(&(data[0]), data.size(), pEncoded);
}
   
Error base64Encode(const unsigned char* pData, 
                   int len, 
                   std::string* pEncoded)
{
   // allocate BIO
   BIO* pB64 = ::BIO_new(BIO_f_base64());
   if (pB64 == NULL)
      return lastCryptoError(ERROR_LOCATION);
      
   // no newlines
   BIO_set_flags(pB64, BIO_FLAGS_BASE64_NO_NL);
   
   // make sure it is freed prior to exit from the function
   BIOFreeAllScope freeB64Scope(pB64);
   
   // allocate memory stream 
   BIO* pMem = ::BIO_new(BIO_s_mem());
   if (pMem == NULL)
      return lastCryptoError(ERROR_LOCATION);
   
   // tie the stream to the b64 stream
   pB64 = ::BIO_push(pB64, pMem);
   
   // perform the encoding
   int written = ::BIO_write(pB64, pData, len); 
   if (written != len)
      return lastCryptoError(ERROR_LOCATION);
      
   // flush all writes
   int result = BIO_flush(pB64);
   if (result <= 0)
      return lastCryptoError(ERROR_LOCATION);
   
   // seek to beginning of memory stream
   result = BIO_seek(pMem, 0);
   if (result == -1)
      return lastCryptoError(ERROR_LOCATION);

   // read the memory stream
   std::vector<char> buffer(len *2); // plenty more than len * 1.37 + padding
   int bytesRead = ::BIO_read(pMem, &(buffer[0]), buffer.capacity());
   if (bytesRead < 0 && ::ERR_get_error() != 0)
      return lastCryptoError(ERROR_LOCATION);

   // copy to out param
   buffer.resize(bytesRead);
   pEncoded->assign(buffer.begin(), buffer.end());

   // return success
   return Success();
}
   
   
Error base64Decode(const std::string& data, 
                   std::vector<unsigned char>* pDecoded)
{
   // allocate b64 BIO
   BIO* pB64 = ::BIO_new(BIO_f_base64());
   if (pB64 == NULL)
      return lastCryptoError(ERROR_LOCATION);
   
   // no newlines
   BIO_set_flags(pB64, BIO_FLAGS_BASE64_NO_NL);
   
   // make sure it is freed prior to exit from the function
   BIOFreeAllScope freeB64Scope(pB64);
   
   // allocate buffer 
   BIO* pMem = BIO_new_mem_buf((void*)data.data(), data.length());
   if (pMem == NULL)
      return lastCryptoError(ERROR_LOCATION);
   
   // tie the stream to the b64 stream
   pB64 = ::BIO_push(pB64, pMem);
   
   // reserve adequate memory in the decoded buffer and read into it
   pDecoded->clear();
   pDecoded->resize(data.length());
   int bytesRead = ::BIO_read(pB64, 
                              &(pDecoded->operator[](0)), 
                              pDecoded->size());
   if (bytesRead < 0)
      return lastCryptoError(ERROR_LOCATION);
   
   // resize the out buffer to the number of bytes actually read
   pDecoded->resize(bytesRead);
   
   // return success
   return Success();
     
}

Error aesEncrypt(const std::vector<unsigned char>& data,
                 const std::vector<unsigned char>& key,
                 const std::vector<unsigned char>& iv,
                 std::vector<unsigned char>* pEncrypted)
{
   // allow enough space in output buffer for additional block
   pEncrypted->resize(data.size() + EVP_MAX_BLOCK_LENGTH);
   int outlen = 0;
   int bytesEncrypted = 0;

   EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
   EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, &key[0], &iv[0], kEncrypt);

   // perform the encryption
   if(!EVP_CipherUpdate(ctx, &(pEncrypted->operator[](0)), &outlen, &data[0], data.size()))
   {
      EVP_CIPHER_CTX_cleanup(ctx);
      return lastCryptoError(ERROR_LOCATION);
   }
   bytesEncrypted += outlen;

   // perform final flush including left-over padding
   if(!EVP_CipherFinal_ex(ctx, &(pEncrypted->operator[](outlen)), &outlen))
   {
      EVP_CIPHER_CTX_cleanup(ctx);
      return lastCryptoError(ERROR_LOCATION);
   }
   bytesEncrypted += outlen;

   EVP_CIPHER_CTX_cleanup(ctx);

   // resize the container to the amount of actual bytes encrypted (including padding)
   pEncrypted->resize(bytesEncrypted);

   return Success();
}

Error aesDecrypt(const std::vector<unsigned char>& data,
                 const std::vector<unsigned char>& key,
                 const std::vector<unsigned char>& iv,
                 std::vector<unsigned char>* pDecrypted)
{
   pDecrypted->resize(data.size());
   int outlen = 0;
   int bytesDecrypted = 0;

   EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
   EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, &key[0], &iv[0], kDecrypt);

   // perform the decryption
   if(!EVP_CipherUpdate(ctx, &(pDecrypted->operator[](0)), &outlen, &data[0], data.size()))
   {
      EVP_CIPHER_CTX_cleanup(ctx);
      return lastCryptoError(ERROR_LOCATION);
   }
   bytesDecrypted += outlen;

   // perform final flush
   if(!EVP_CipherFinal_ex(ctx, &(pDecrypted->operator[](outlen)), &outlen))
   {
      EVP_CIPHER_CTX_cleanup(ctx);
      return lastCryptoError(ERROR_LOCATION);
   }
   bytesDecrypted += outlen;

   EVP_CIPHER_CTX_cleanup(ctx);

   // resize the container to the amount of actual bytes decrypted (padding is removed)
   pDecrypted->resize(bytesDecrypted);

   return Success();
}

Error random(uint32_t numBytes,
             std::vector<unsigned char>* pOut)
{
   pOut->resize(numBytes);

   if (!RAND_bytes(&(pOut->operator[](0)), numBytes))
   {
      return lastCryptoError(ERROR_LOCATION);
   }

   return Success();
}

namespace {
RSA* s_pRSA;
std::string s_modulo;
std::string s_exponent;
}

core::Error rsaInit()
{
   const int KEY_SIZE = 1024;
   const int ENTROPY_BYTES = 4096;

   const BIGNUM *bn_n;
   const BIGNUM *bn_e;

   int rnd = ::open("/dev/urandom", O_RDONLY);
   if (rnd == -1)
      return systemError(errno, ERROR_LOCATION);

   char entropy[ENTROPY_BYTES];
   if (-1 == ::read(rnd, entropy, ENTROPY_BYTES))
   {
      ::close(rnd);
      return systemError(errno, ERROR_LOCATION);
   }
   ::close(rnd);

   RAND_seed(entropy, ENTROPY_BYTES);

   #if OPENSSL_VERSION_NUMBER < 0x10100000L
      s_pRSA = ::RSA_generate_key(KEY_SIZE, 0x10001, NULL, NULL);
      if (!s_pRSA)
         return lastCryptoError(ERROR_LOCATION);

      bn_n = s_pRSA->n;
      bn_e = s_pRSA->e;
   #else
      BIGNUM *bn = BN_new();
      BN_set_word(bn, RSA_F4);
 
      s_pRSA = RSA_new();
      int rc = ::RSA_generate_key_ex(s_pRSA, KEY_SIZE, bn, NULL);
      BN_clear_free(bn);
      if (rc != 1) {
        RSA_free(s_pRSA);
        return lastCryptoError(ERROR_LOCATION);
      }
   
      RSA_get0_key(s_pRSA, &bn_n, &bn_e, NULL);
   #endif

   char* n = BN_bn2hex(bn_n);
   s_modulo = n;
   OPENSSL_free(n);
   char* e = BN_bn2hex(bn_e);
   s_exponent = e;
   OPENSSL_free(e);

   return Success();
}

void rsaPublicKey(std::string* pExponent, std::string* pModulo)
{
   pModulo->assign(s_modulo.begin(), s_modulo.end());
   pExponent->assign(s_exponent.begin(), s_exponent.end());
}

core::Error rsaPrivateDecrypt(const std::string& cipherText, std::string* pPlainText)
{
   std::vector<unsigned char> cipherTextBytes;
   Error error = base64Decode(cipherText, &cipherTextBytes);
   if (error)
      return error;

   int size = RSA_size(s_pRSA);
   std::vector<unsigned char> plainTextBytes(size);
   int bytesRead = RSA_private_decrypt(cipherTextBytes.size(),
                                       &cipherTextBytes[0],
                                       &plainTextBytes[0],
                                       s_pRSA,
                                       RSA_PKCS1_PADDING);
   if (bytesRead == -1)
      return lastCryptoError(ERROR_LOCATION);

   plainTextBytes.resize(bytesRead);
   pPlainText->assign(plainTextBytes.begin(), plainTextBytes.end());

   return Success();
}

Error encryptDataAsBase64EncodedString(const std::string& input,
                                       const std::string& keyStr,
                                       std::string* pIv,
                                       std::string* pEncrypted)
{
   // copy data into vector
   std::vector<unsigned char> data;
   std::copy(input.begin(), input.end(), std::back_inserter(data));

   // copy key into vector
   std::vector<unsigned char> key;
   std::copy(keyStr.begin(), keyStr.end(), std::back_inserter(key));

   // create a random initialization vector for a little added security
   std::vector<unsigned char> iv;
   Error error = core::system::crypto::random(256, &iv);
   if (error)
      return error;

   // encrypt the input
   std::vector<unsigned char> encrypted;
   error = core::system::crypto::aesEncrypt(data, key, iv, &encrypted);
   if (error)
      return error;

   // base 64 encode the IV used for encryption
   error = core::system::crypto::base64Encode(iv, pIv);
   if (error)
      return error;

   // base 64 encode encrypted result
   return core::system::crypto::base64Encode(encrypted, pEncrypted);
}

Error decryptBase64EncodedString(const std::string& input,
                                 const std::string& keyStr,
                                 const std::string& ivStr,
                                 std::string* pDecrypted)
{
   // copy key into vector
   std::vector<unsigned char> key;
   std::copy(keyStr.begin(), keyStr.end(), std::back_inserter(key));

   // decode initialization vector
   std::vector<unsigned char> iv;
   Error error = core::system::crypto::base64Decode(ivStr, &iv);
   if (error)
      return error;

   // decode encrypted input
   std::vector<unsigned char> decoded;
   error = core::system::crypto::base64Decode(input, &decoded);
   if (error)
      return error;

   // decrypt decoded input
   std::vector<unsigned char> decrypted;
   error = core::system::crypto::aesDecrypt(decoded, key, iv, &decrypted);
   if (error)
      return error;

   // covert the decrypted bytes into the original string
   pDecrypted->reserve(decrypted.size());
   std::copy(decrypted.begin(), decrypted.end(), std::back_inserter(*pDecrypted));

   return Success();
}

                      
} // namespace crypto
} // namespace system
} // namespace core
} // namespace rstudio

