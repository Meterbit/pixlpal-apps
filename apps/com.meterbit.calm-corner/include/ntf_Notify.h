/**
 * @file ntf_Notify.h
 * @brief Notifications to people's phones, straight from Pixlpal over the home Wi-Fi (no server of ours): ntfy (free,
 *        no account, ntfy.sh or a self-hosted server), a Telegram bot, and WhatsApp through CallMeBot (free for
 *        personal use). Messages wait in a queue and go out on a task of their own; a channel that fails is tried once
 *        more a minute later. Used by the Baby & Room Monitor.
 */
#ifndef NTF_NOTIFY_H
#define NTF_NOTIFY_H

#include <stdint.h>
#include <stddef.h>

enum Ntf_Channel_t : uint8_t { NTF_NTFY = 0, NTF_TELEGRAM, NTF_WHATSAPP, NTF_CHANNELS };
enum Ntf_State_t : uint8_t { NTF_OFF = 0, NTF_OK, NTF_FAILED, NTF_SENDING, NTF_READY };
enum Ntf_Priority_t : uint8_t { NTF_LOW = 0, NTF_NORMAL, NTF_HIGH, NTF_URGENT };

struct Ntf_Config_t {
    uint8_t on[NTF_CHANNELS];
    char ntfyServer[64];                /**< "ntfy.sh" or a self-hosted server (https) */
    char ntfyTopic[64];                 /**< Hard to guess: anyone who knows it can read it */
    char ntfyToken[64];                 /**< An access token for a protected topic, or "" (secret) */
    char telegramToken[64];             /**< "123456:ABC..." (secret) */
    char telegramChat[32];              /**< A chat or group ID: "123456789", "-1001234567890" */
    char whatsappPhone[24];             /**< "+2348012345678" */
    char whatsappKey[24];               /**< CallMeBot's key (secret) */
};

struct Ntf_Status_t {
    uint8_t state[NTF_CHANNELS];        /**< Ntf_State_t */
    char text[NTF_CHANNELS][72];        /**< "Sent at 02:14", "Telegram refused the bot token" */
    uint32_t sent;
    uint8_t queued;
};

/** @brief Starts the sending task. @p tags (ntfy's emoji tags, "baby_bottle") and @p userAgent are kept. */
bool ntf_Start(const Ntf_Config_t& config, const char* userAgent, const char* tags);
void ntf_Stop(void);
void ntf_Set_Config(const Ntf_Config_t& config);
/** @brief A message for every channel switched on and set up. False if none is, or the queue is full. */
bool ntf_Send(const char* title, const char* text, uint8_t priority);
/** @brief Copies the status if it changed since @p version (updated); false if not. */
bool ntf_Copy_Status(Ntf_Status_t& status, uint32_t& version);
/** @brief The channel is switched on and has what it needs. */
bool ntf_Ready(const Ntf_Config_t& config, uint8_t channel);
const char* ntf_Channel_Name(uint8_t channel);

/** @brief Checks of what the phone sends: a topic (letters, digits, - and _), a server name, a Telegram chat, a phone. */
bool ntf_Valid_Topic(const char* text);
bool ntf_Valid_Server(const char* text);
bool ntf_Valid_Chat(const char* text);
bool ntf_Valid_Phone(const char* text);

#endif
