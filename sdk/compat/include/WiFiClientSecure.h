// Pixlpal compat: the TLS client HTTPClient takes. Pixlpal does TLS itself (verified, its CA bundle).
#pragma once
class WiFiClient { public: virtual ~WiFiClient() {} };
class WiFiClientSecure : public WiFiClient {
public:
    void useBuiltinCACertBundle(){}
    void setInsecure(){}
    void setCACert(const char*){}
    void setTimeout(unsigned){}
    void setHandshakeTimeout(unsigned){}
};
