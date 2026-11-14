// Pixlpal compat: Arduino's HTTPClient, as the firmware's apps use it (GET and POST, the body as a stream), on the
// compat HTTP helper (pxl_http_*: only app.json's "net" hosts, answers up to 64 KB). Response headers: none.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "WString.h"
#include "WiFiClientSecure.h"
#define HTTP_CODE_OK 200
#define HTTP_CODE_NO_CONTENT 204
#define HTTP_CODE_MOVED_PERMANENTLY 301
#define HTTP_CODE_FOUND 302
#define HTTP_CODE_NOT_MODIFIED 304
#define HTTP_CODE_BAD_REQUEST 400
#define HTTP_CODE_PAYMENT_REQUIRED 402
#define HTTP_CODE_UNAUTHORIZED 401
#define HTTP_CODE_FORBIDDEN 403
#define HTTP_CODE_NOT_FOUND 404
#define HTTP_CODE_TOO_MANY_REQUESTS 429
#define HTTP_CODE_INTERNAL_SERVER_ERROR 500
#define HTTP_CODE_BAD_GATEWAY 502
#define HTTP_CODE_SERVICE_UNAVAILABLE 503
#define HTTP_CODE_GATEWAY_TIMEOUT 504
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_CONNECTION_LOST (-5)
#define HTTPC_ERROR_READ_TIMEOUT (-11)
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };
class Pxl_Body_Stream {
public:
    const char* data = nullptr; size_t length = 0, at = 0;
    int available(){ return (int)(length - at); }
    bool connected(){ return at < length; }
    int read(){ return at < length ? (uint8_t)data[at++] : -1; }
    size_t read(uint8_t* b, size_t n){ if (n > length - at) n = length - at; memcpy(b, data + at, n); at += n; return n; }
    size_t readBytes(char* b, size_t n){ return read((uint8_t*)b, n); }
    int peek(){ return at < length ? (uint8_t)data[at] : -1; }
    void setTimeout(unsigned long){}
};
typedef Pxl_Body_Stream Client;       // what http.getStream() is, as the firmware's apps name it
class HTTPClient {
public:
    ~HTTPClient(){ end(); }
    bool begin(WiFiClient&, const char* url){ return begin(url); }
    bool begin(WiFiClient&, const String& url){ return begin(url.c_str()); }
    bool begin(const char* url);
    bool begin(const String& url){ return begin(url.c_str()); }
    void addHeader(const char* name, const char* value);
    void addHeader(const String& name, const String& value){ addHeader(name.c_str(), value.c_str()); }
    void setUserAgent(const char*){}
    void setUserAgent(const String&){}
    void setTimeout(uint16_t){}
    void setConnectTimeout(int32_t){}
    void setReuse(bool){}
    void useHTTP10(bool){}
    void setFollowRedirects(followRedirects_t){}
    void collectHeaders(const char* const*, size_t){}
    String header(const char*){ return String(); }
    int GET();
    int POST(const uint8_t* body, size_t length);
    int POST(const String& body){ return POST((const uint8_t*)body.c_str(), body.length()); }
    int POST(const char* body){ return POST((const uint8_t*)body, strlen(body)); }
    int sendRequest(const char* method, const uint8_t* body, size_t length);
    int getSize(){ return (int)stream_.length; }
    Pxl_Body_Stream& getStream(){ return stream_; }
    Pxl_Body_Stream* getStreamPtr(){ return &stream_; }
    String getString(){ return String(body_ ? body_ : ""); }
    void end();
private:
    char url_[512] = "";
    char headers_[9][96] = {};
    int headerCount_ = 0;
    char* body_ = nullptr;
    Pxl_Body_Stream stream_;
};
