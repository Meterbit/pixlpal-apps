/**
 * @file mtb_kit_body_reader.h
 * @brief A reader for deserializeJson() straight from an HTTP response stream (HTTPClient::getStream()).
 *
 * A TLS client's read() returns at once when nothing has arrived yet, which ArduinoJson would take for the end of the
 * body, so this waits for data (up to @p timeoutMs without any) and stops at @p maxBytes. It can also put one byte
 * back, skip to a token or a character, and count the bytes (some APIs bill on them).
 *
 * Use this type rather than an app's own: ArduinoJson compiles its whole parser once for every reader type, so one
 * shared type keeps one copy of it in flash.
 */
#ifndef MTB_KIT_BODY_READER_H
#define MTB_KIT_BODY_READER_H

#include <Arduino.h>
#include <Client.h>

class Mtb_Kit_Body_Reader_t {
public:
    /** @param maxBytes  0: no limit. */
    Mtb_Kit_Body_Reader_t(Client& stream, uint32_t timeoutMs, size_t maxBytes = 0) : stream(stream), timeoutMs(timeoutMs), maxBytes(maxBytes) {}

    int read(void){
        if (pushed >= 0){ const int c = pushed; pushed = -1; return c; }
        if (position >= length && !fill()) return -1;
        return buffer[position++];
    }

    size_t readBytes(char* dest, size_t size);

    /** @brief Puts @p c back; the next read() returns it. One byte only. */
    void unread(int c){ pushed = c; }

    /** @brief Reads up to and including @p token. False at the end of the body. */
    bool find(const char* token);

    /** @brief Reads up to and including @p target. False at the end of the body. */
    bool skipPast(char target);

    /** @brief The next character that is not JSON whitespace, or -1. */
    int next_Non_Space(void);

    /** @brief Body bytes received so far. */
    size_t bytesReceived(void) const { return total; }

private:
    bool fill(void);

    Client& stream;
    const uint32_t timeoutMs;
    const size_t maxBytes;
    uint8_t buffer[512];
    size_t position = 0;
    size_t length = 0;
    size_t total = 0;
    int pushed = -1;
};

#endif
