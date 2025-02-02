// Copyright 2016 The Arken Platform Authors.
// All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <curl/curl.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <arken/net/httpclient.h>
#include <atomic>
#include <mutex>
#include <sstream>

static uint32_t   global_count{0};
static std::mutex global_mutex;

namespace arken {
namespace net {

uint64_t HttpClient::callback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  auto client = static_cast<HttpClient *>(userp);
  client->m_data.append(static_cast<const char *>(contents), realsize);
  return realsize;
}

HttpClient::HttpClient(const char * url)
{
  m_url           = url;
  m_status        = 0;
  m_failure       = false;
  m_verbose       = false;
  m_formdata      = false;
  m_sslVerifyPeer = false;
  m_sslVerifyHost = -1;
  m_sslVersion    = -1;
  m_useSsl        = -1;
}

HttpClient::~HttpClient() = default;

void HttpClient::appendHeader(const char * header)
{
  m_headers.push_back(header);
}

void HttpClient::setVerbose(bool verbose)
{
  m_verbose = verbose;
}

void HttpClient::setBody(string body)
{
  m_body = body;
}

string HttpClient::body()
{
  return m_body;
}

string HttpClient::performGet()
{
  return perform();
}

string HttpClient::performPost()
{
  return perform("POST");
}

string HttpClient::performPut()
{
  return perform("PUT");
}

string HttpClient::performDelete()
{
  return perform("DELETE");
}

int HttpClient::status()
{
  return m_status;
}

string HttpClient::data()
{
  return m_data;
}

string HttpClient::message()
{
  return m_message;
}

bool HttpClient::failure()
{
  return m_failure;
}

string HttpClient::perform(string method)
{
  curl_slist * list = nullptr;
  CURL       * curl;
  int          index;
  CURLcode     res;

  // init globlal
  global_mutex.lock();
  if( global_count == 0 ) {
    curl_global_init(CURL_GLOBAL_ALL);
  }
  global_count++;
  global_mutex.unlock();

  // init the curl session
  curl = curl_easy_init();

  // url
  curl_easy_setopt(curl, CURLOPT_URL, m_url.data());

  // example.com is redirected, so we tell libcurl to follow redirection
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

  // send all data to this function
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HttpClient::callback);

  // we pass our 'chunk' struct to the callback function
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)this);

  // headers
  curl_easy_setopt(curl, CURLOPT_HEADER, 1);

  // some servers don't like requests that are made without a user-agent field, so we provide one
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  // verbose
  if( m_verbose ) {
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  } else {
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
  }

  // https://curl.se/libcurl/c/CURLOPT_SSLCERT.html
  if( ! m_cert.empty()  ) {
    curl_easy_setopt(curl, CURLOPT_SSLCERT, m_cert.data());
  }

  // https://curl.se/libcurl/c/CURLOPT_SSLKEY.html
  if( ! m_certKey.empty()  ) {
    curl_easy_setopt(curl, CURLOPT_SSLKEY, m_certKey.data());
  }

  // https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
  // https://curl.haxx.se/docs/sslcerts.html
  if ( m_sslVerifyPeer ) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  } else {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  }

  // https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html
  if( m_sslVerifyHost > 0 ) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, m_sslVerifyHost);
  }

  // https://curl.se/libcurl/c/CURLOPT_USE_SSL.html
  if( m_useSsl > 0 ) {
    curl_easy_setopt(curl, CURLOPT_USE_SSL, m_useSsl);
  }

  // https://curl.se/libcurl/c/CURLOPT_SSLVERSION.html
  if( m_sslVersion > 0 ) {
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, m_sslVersion);
  }

  for(size_t i=0; i < m_headers.size(); i++) {
    list = curl_slist_append(list, m_headers[i].data());

    // multipart/form-data submission
    std::string header = m_headers[1].data();
    if (header.find("Content-Type: multipart/form-data") != std::string::npos) {
      m_formdata = true;
    }
  }

  // set our custom set of headers
  if( list ) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  }

  // POST PUT DELETE
  if( !method.empty() ) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.data());
  }

  // POST PUT
  if( method.equals("POST") || method.equals("PUT") || m_body.size() > 0) {
    //curl_easy_setopt(curl, CURLOPT_POST, 1);

    if( m_formdata ) { // multipart/form-data submission
      curl_mime *form = curl_mime_init(curl);
      curl_mimepart *field = NULL;

      std::string input = m_body.data();
      std::vector<std::string> lines;
      std::istringstream stream(input);
      std::string line;

      while (std::getline(stream, line)) {
        std::string input2 = line;
        std::array<std::string, 2> parts;
        size_t equal_pos = input2.find("=");
        parts[0] = input2.substr(0, equal_pos);
        parts[1] = input2.substr(equal_pos + 2, input2.size() - equal_pos - 3);

        field = curl_mime_addpart(form);
        curl_mime_data(field, parts[1].data(), CURL_ZERO_TERMINATED);
        curl_mime_name(field, parts[0].data());
      }

      curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    } else {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, m_body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, m_body.size());
    }
  }

  // perform
  res = curl_easy_perform(curl);

  // cleanup curl stuff
  curl_easy_cleanup(curl);

  /* Free the list */
  if( list ) {
    curl_slist_free_all(list);
  }

  // we're done with libcurl, so clean it up
  global_mutex.lock();
  global_count--;
  if( global_count == 0 ) {
    curl_global_cleanup();
  }
  global_mutex.unlock();

  // out of memory
  if ( m_failure ) {
    return {};
  }

  // check for errors
  if( res != CURLE_OK ) {
    m_failure = true;
    m_message = curl_easy_strerror(res);
    return {};
  }

  if( !m_data.empty() ) {
    // parse status
    index = m_data.lastIndexOf("HTTP");
    index = m_data.indexOf(" ", index);
    if( index > -1 ) {
      //m_status = m_data.mid(index+1, index+4).atoi();
      string status = m_data.mid(index + 1, index + 4);
      // TODO atoi in arken:string
      m_status = atoi(status);
    } else {
      m_status = 0;
    }

    // parse body TODO refactory return {}
    index = m_data.lastIndexOf("\r\n\r\n");
    if( index > 0 ) {
      index += 4;
      size_t size = (m_data.size()-index);
      if( size > 0 ) {
        return m_data.mid(index, size);
      } else {
        return {};
      }
    } else {
      return {};
    }
  } else {
    m_status = 0;
    return {};
  }
}

void HttpClient::setCert(string cert)
{
  m_cert = cert;
}

void HttpClient::setCertKey(string certKey)
{
  m_certKey = certKey;
}

void HttpClient::setSslVerifyPeer(bool sslVerifyPeer)
{
  m_sslVerifyPeer = sslVerifyPeer;
}

void HttpClient::setSslVerifyHost(long sslVerifyHost)
{
  m_sslVerifyHost = sslVerifyHost;
}

void HttpClient::setSslVersion(long sslVersion)
{
  m_sslVersion = sslVersion;
}

void HttpClient::setUseSsl(long useSsl)
{
  m_useSsl = useSsl;
}

} // namespace net
} // namespace arken