/*
 * Collection of isolated functions used in different part of banjax
 *
 * Copyright (c) 2013 eQualit.ie under GNU AGPL v3.0 or later
 * 
 * Vmon: June 2013 Initial version
 *       Oct  2013 zmq stuff moved here for public use.
 */

#include <stdio.h>
//#include <cstdint>

#include <ts/ts.h>
#include <zmq.hpp>
#include <string>

#include<openssl/rand.h>
#include<openssl/evp.h>

using namespace std;

#include "util.h"
#include "exception.h"
/* Check if the ATS version is the right version for this plugin
   that is version 2.0 or higher for now
   */
int
check_ts_version()
{
  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 2.0 */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;

}

/**
 * Sends a message through zmq
 * @param mess the message to be sent
 * @param more true if we have additional messages to send
 */
void send_zmq_mess(zmq::socket_t& zmqsock, const std::string mess, bool more){
  zmq::message_t m(mess.size());
  memcpy((void*) m.data(), mess.c_str(), mess.size());
  if(more){
    zmqsock.send(m, ZMQ_SNDMORE);
  } else {
    zmqsock.send(m);  
  }
}

/**
 * encrypt and send a message throw zmq socket.
 *
 * This is NOT thread safe you need to use mutex
 * before calling
 * throw exception if it gets into error
 *
 * The message has this format
 * |IV|Cipher Text|TAG|
 *
 * IV is 12 and TAG is 16 Byte long
 */
void send_zmq_encrypted_message(zmq::socket_t& zmqsock, const string mess, uint8_t* encryption_key, bool more){

  size_t enc_length = mess.length()+ c_cipher_block_size - 1;
  uint8_t enc_out[c_gcm_iv_size +
                         enc_length + c_gcm_tag_size ];
  uint8_t gcm_tag[c_gcm_tag_size];
  
  //generate a random iv;
  if (!RAND_bytes(enc_out, c_gcm_iv_size)) {
    /* OpenSSL reports a failure, act accordingly */
    throw EncryptionException();
  }

  enc_length = gcm_encrypt(reinterpret_cast<const uint8_t*>(mess.data()), mess.length(), 
              encryption_key, enc_out,
                             enc_out + c_gcm_iv_size, gcm_tag);

  if (enc_length < mess.length() || enc_length > c_max_enc_length)
    throw EncryptionException();

  memcpy(enc_out + c_gcm_iv_size + enc_length, gcm_tag, c_gcm_tag_size);

  size_t enc_message_size = c_gcm_iv_size + enc_length + c_gcm_tag_size;
  zmq::message_t m(enc_message_size);
  memcpy((void*) m.data(), enc_out, enc_message_size);
  if(more){
    zmqsock.send(m, ZMQ_SNDMORE);
  } else {
    zmqsock.send(m);  
  }

}

/**
 * Uses AES256 to encrypt the data 
 *
 * @param iv is a buffer of size 12 bytes contatining iv
 * @param key is a buffer 32 bytes as we are using AES256
 * @param ciphertext should be buffer of size planitext_len + 16 - 1 
 * @param tag is a buffer 16 bytes. 
 */
size_t gcm_encrypt(const uint8_t *plaintext, size_t plaintext_len, 
            const uint8_t *key, const uint8_t *iv,
            uint8_t *ciphertext, uint8_t *tag)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int len = 0, ciphertext_len = 0;

    /* Create and initialise the context */
    if(!(ctx = EVP_CIPHER_CTX_new()))
      throw EncryptionException();

    /* Initialise the encryption operation. */
    /* Initialise key and IV */
    if(1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, iv))
        throw EncryptionException();

    /* Provide the message to be encrypted, and obtain the encrypted output.
     * EVP_EncryptUpdate can be called multiple times if necessary
     */
    if(plaintext)
    {
        if(1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len))
          throw EncryptionException();

        ciphertext_len = len;
    }

    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len))
      throw EncryptionException();

    /* Get the tag */
    if(1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag))
        throw EncryptionException();

    /* Clean up */
    EVP_CIPHER_CTX_free(ctx);

    return ciphertext_len;

}
