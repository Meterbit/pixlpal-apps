/**
 * @file glu_Model.h
 * @brief Glucose Display {14,2}: a continuous glucose monitor's latest reading, its trend and the last hours, large
 *        enough to read across a room, with alarms on Pixlpal and on the family's phones. No Arduino, so it can be
 *        tested on a PC.
 *
 * NOT A MEDICAL DEVICE. A second screen for the CGM app; treatment decisions follow the CGM app and its own alarms.
 *
 * Sources (checked 11 November 2026):
 *  - Nightscout (open source, the user's own site): GET /api/v1/entries/sgv.json?count=N&find[date][$gt]=ms
 *    [{"sgv": 123, "date": 1794400000000, "direction": "Flat", "type": "sgv"}], newest first;
 *    GET /api/v1/status.json {"name", "version", "settings": {"units": "mg/dl" | "mmol", "thresholds": {"bgHigh",
 *    "bgTargetTop", "bgTargetBottom", "bgLow"}}}; GET /api/v2/properties/iob,cob,upbat (Loop, AAPS, OpenAPS).
 *    A token (Admin Tools > Subjects, role "readable") goes in ?token=; an API secret as API-SECRET: sha1 hex.
 *  - Dexcom Share (unofficial, as pydexcom uses it; Share must be on in the Dexcom app): POST
 *    General/AuthenticatePublisherAccount {"accountName", "password", "applicationId"} -> "account id";
 *    General/LoginPublisherAccountById {"accountId", "password", "applicationId"} -> "session id";
 *    Publisher/ReadPublisherLatestGlucoseValues?sessionId=&minutes=1440&maxCount=288
 *    [{"WT": "Date(1691455258000)", "Value": 85, "Trend": "Flat"}]. Errors {"Code": "SessionIdNotFound"...}.
 *  - LibreLinkUp (unofficial; the follower app's account): POST /llu/auth/login {"email", "password"} ->
 *    {"status": 0, "data": {"user": {"id"}, "authTicket": {"token"}}} or {"data": {"redirect": true, "region"}};
 *    GET /llu/connections, /llu/connections/{patientId}/graph with "Account-Id: sha256(user id)".
 */
#ifndef GLU_MODEL_H
#define GLU_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "ntf_Notify.h"

#define GLU_NVS_KEY         "gluSet"
#define GLU_SETTINGS_MAGIC  0x474C5501u
#define GLU_HISTORY         300         /**< 24 hours of 5-minute readings, and a little room */
#define GLU_PATIENTS        4           /**< LibreLinkUp people followed */
#define GLU_MMOL            18.0182f    /**< mg/dL per mmol/L */

enum Glu_Source_t : uint8_t { GLU_NIGHTSCOUT = 0, GLU_DEXCOM, GLU_LIBRE, GLU_SOURCES };
enum Glu_Units_t : uint8_t { GLU_MGDL = 0, GLU_MMOLL, GLU_UNITS_AUTO };
enum Glu_Dexcom_Region_t : uint8_t { GLU_DX_US = 0, GLU_DX_OUS, GLU_DX_JP };
/** Dexcom's numbering. */
enum Glu_Trend_t : uint8_t {
    GLU_TREND_NONE = 0, GLU_DOUBLE_UP, GLU_SINGLE_UP, GLU_45_UP, GLU_FLAT, GLU_45_DOWN, GLU_SINGLE_DOWN, GLU_DOUBLE_DOWN,
    GLU_NOT_COMPUTABLE, GLU_OUT_OF_RANGE, GLU_TRENDS
};
/** From the most to the least urgent. */
enum Glu_Alarm_t : uint8_t {
    GLU_ALARM_NONE = 0, GLU_URGENT_LOW, GLU_URGENT_LOW_SOON, GLU_LOW, GLU_FALLING_FAST, GLU_STALE, GLU_URGENT_HIGH, GLU_HIGH,
    GLU_RISING_FAST, GLU_ALARMS
};
enum Glu_Range_t : uint8_t { GLU_RANGE_URGENT_LOW = 0, GLU_RANGE_LOW, GLU_RANGE_IN, GLU_RANGE_HIGH, GLU_RANGE_URGENT_HIGH };

struct Glu_Reading_t {
    uint32_t t;                         /**< Unix */
    uint16_t mgdl;
    uint8_t trend;                      /**< Glu_Trend_t */
};

struct Glu_History_t {
    int count;
    Glu_Reading_t r[GLU_HISTORY];       /**< oldest first */
};

/** What a Nightscout site also says (Loop, AAPS, OpenAPS, the uploader). */
struct Glu_Extra_t {
    bool hasIob, hasCob, hasBattery;
    float iob, cob;
    uint8_t battery;
};

/** From Nightscout's status. */
struct Glu_Site_t {
    bool known;
    uint8_t units;                      /**< GLU_MGDL or GLU_MMOLL */
    uint16_t low, targetLow, targetHigh, high;  /**< mg/dL; 0 not given */
    char name[32];
    char version[16];
};

struct Glu_Settings_t {
    uint32_t magic;
    uint8_t source;                     /**< Glu_Source_t */
    uint8_t units;                      /**< Glu_Units_t (AUTO: Nightscout's, else mg/dL) */
    char name[16];                      /**< whose readings: "ADA", or "" */
    // Nightscout.
    char nsUrl[96];                     /**< "https://ada.example.com" */
    char nsToken[64];                   /**< secret */
    char nsSecretSha1[41];              /**< the API secret, hashed on arrival (never kept in clear) */
    uint8_t useSiteTargets;             /**< take the targets from the Nightscout site */
    // Dexcom Share.
    char dxUser[64];
    char dxPass[64];                    /**< secret */
    uint8_t dxRegion;                   /**< Glu_Dexcom_Region_t */
    // LibreLinkUp.
    char lluEmail[64];
    char lluPass[64];                   /**< secret */
    uint8_t lluPatient;                 /**< which of the people followed */
    // Ranges, mg/dL.
    uint16_t urgentLow, low, high, urgentHigh, targetLow, targetHigh;
    // Alarms.
    uint8_t alarmOn[GLU_ALARMS];        /**< by Glu_Alarm_t */
    uint8_t staleMinutes;               /**< no reading for this long: an alarm */
    uint8_t lowSnooze, highSnooze;      /**< minutes */
    uint8_t sound;                      /**< on Pixlpal */
    uint8_t phoneAlerts;                /**< to ntfy / Telegram / WhatsApp */
    uint8_t quietHighs;                 /**< no high alarms in the quiet hours (lows always sound) */
    uint16_t quietFrom, quietTo;        /**< minutes after local midnight */
    uint8_t nightDim;                   /**< a dim, red-free screen in the quiet hours */
    uint8_t graphHours;                 /**< 3, 6, 12 or 24 */
    uint8_t hour24;
    Ntf_Config_t notify;
};

void glu_Settings_Default(Glu_Settings_t& s);
void glu_Settings_Clamp(Glu_Settings_t& s);

/** @brief The units to show: the setting, or the Nightscout site's for AUTO. */
uint8_t glu_Units(const Glu_Settings_t& s, const Glu_Site_t* site);
/** @brief The ranges in effect (the site's targets when asked and given). */
void glu_Ranges(const Glu_Settings_t& s, const Glu_Site_t* site, uint16_t& urgentLow, uint16_t& low, uint16_t& high, uint16_t& urgentHigh);
uint8_t glu_Range(uint16_t mgdl, uint16_t urgentLow, uint16_t low, uint16_t high, uint16_t urgentHigh);

/** @brief "123" or "6.8". */
void glu_Value_Text(char* dest, size_t size, uint16_t mgdl, uint8_t units);
/** @brief "+4", "-0.3", "+0"... (@p delta mg/dL). */
void glu_Delta_Text(char* dest, size_t size, int delta, uint8_t units);
const char* glu_Units_Name(uint8_t units);
/** @brief Trend words: "steady", "rising quickly"... */
const char* glu_Trend_Name(uint8_t trend);
/** @brief Nightscout's / Dexcom's "FortyFiveUp", "NOT COMPUTABLE"...; GLU_TREND_NONE if unknown. */
uint8_t glu_Trend_From_Text(const char* text);
/** @brief LibreLinkUp's TrendArrow 1-5 (1 falling ... 5 rising). */
uint8_t glu_Trend_From_Libre(int arrow);
const char* glu_Alarm_Name(uint8_t alarm);
/** @brief What the screen says for the alarm: "URGENT LOW". */
const char* glu_Alarm_Text(uint8_t alarm);

/** @brief Adds a reading in time order: an older copy (within 60 s) is replaced, readings over a day old are dropped. */
bool glu_History_Add(Glu_History_t& h, const Glu_Reading_t& r, uint32_t now);
const Glu_Reading_t* glu_Latest(const Glu_History_t& h);
/**
 * @brief The change over the last five minutes, mg/dL (scaled from the readings 4-11 minutes before the latest); false
 *        when there is no earlier reading near enough.
 */
bool glu_Delta(const Glu_History_t& h, int& delta);
/** @brief The trend from the readings when the CGM gave none (mg/dL a minute over the last 15 minutes). */
uint8_t glu_Trend_From_History(const Glu_History_t& h);
/** @brief The reading projected @p minutes ahead from the last 15 minutes' slope; false with too few readings. */
bool glu_Project(const Glu_History_t& h, int minutes, int& mgdl);

struct Glu_Stats_t {
    int readings;
    float hours;                        /**< the time the readings cover */
    float inRange, below, above;        /**< percent */
    float urgentBelow;
    float mean;                         /**< mg/dL */
    float gmi;                          /**< %, the glucose management indicator */
    float cv;                           /**< %, the coefficient of variation */
    int lows;                           /**< times it went below the low line */
};
/** @brief The last @p hours from @p now, against the target range. */
void glu_Stats(const Glu_History_t& h, uint32_t now, int hours, uint16_t targetLow, uint16_t targetHigh, uint16_t urgentLow,
               Glu_Stats_t& out);

/** The alarms' memory. */
struct Glu_Alarm_State_t {
    uint8_t active;                     /**< Glu_Alarm_t sounding (or snoozed), NONE */
    uint32_t since;
    uint32_t snoozedUntil[GLU_ALARMS];
    uint32_t sentAt[GLU_ALARMS];        /**< last phone message for it */
    uint8_t latched[GLU_ALARMS];        /**< true while the condition holds (with hysteresis) */
};

/**
 * @brief The alarm that should sound now: the most urgent condition switched on and not snoozed (urgent low is never
 *        silenced by the quiet hours). Lows clear 5 mg/dL above their line, highs 10 below.
 * @param minute  local minutes after midnight (for the quiet hours).
 * @return The alarm sounding, NONE.
 */
uint8_t glu_Alarm_Check(const Glu_Settings_t& s, const Glu_Site_t* site, const Glu_History_t& h, uint32_t now, int minute,
                        Glu_Alarm_State_t& st);
/** @brief Snoozes the alarm sounding, for its kind's minutes (urgent low: 15 at most). */
void glu_Alarm_Snooze(const Glu_Settings_t& s, Glu_Alarm_State_t& st, uint32_t now, int minutes);
/** @brief A phone message is due for @p alarm (the first, then every 5 minutes for urgent lows, 15 for others). */
bool glu_Alarm_Send_Due(const Glu_Alarm_State_t& st, uint8_t alarm, uint32_t now);
bool glu_In_Quiet(const Glu_Settings_t& s, int minute);

/** @brief When to ask again: just after the next reading is due (@p every seconds after the last), at least 30 s away. */
uint32_t glu_Next_Poll(uint32_t lastReading, uint32_t now, uint32_t every);

// The sources' answers (host-tested) -----------------------------------------------------------------------------------
/** @brief Nightscout's entries: readings added; the count, -1 if not JSON. */
int glu_Parse_Ns_Entries(const char* body, size_t length, uint32_t now, Glu_History_t& h);
bool glu_Parse_Ns_Status(const char* body, size_t length, Glu_Site_t& site);
/** @brief /api/v2/properties: IOB, COB, the uploader's battery (any may be missing). */
bool glu_Parse_Ns_Properties(const char* body, size_t length, Glu_Extra_t& extra);
/** @brief Dexcom Share's readings. -1 if not a list (an error: @p code gets its Code). */
int glu_Parse_Dexcom(const char* body, size_t length, uint32_t now, Glu_History_t& h, char* code, size_t codeSize);
/** @brief Dexcom's quoted id: "\"abc-...\"" to abc-... False if not one (or all zeros). */
bool glu_Parse_Dexcom_Id(const char* body, size_t length, char* id, size_t size);
/** @brief "11/11/2026 1:05:00 PM" (LibreLinkUp's FactoryTimestamp, UTC) to Unix; 0 if not one. */
uint32_t glu_Parse_Libre_Time(const char* text);

struct Glu_Libre_Login_t {
    int status;                         /**< the answer's status: 0 ok, 2 wrong password, 4 terms to accept... */
    bool redirect;
    char region[8];
    char userId[48];
    char token[1024];
};
bool glu_Parse_Libre_Login(const char* body, size_t length, Glu_Libre_Login_t& out);

struct Glu_Libre_Patient_t { char id[48]; char name[32]; };
/** @brief /llu/connections: the people followed, and the latest reading of @p pick. -1 if not an answer. */
int glu_Parse_Libre_Connections(const char* body, size_t length, uint32_t now, uint8_t pick, Glu_Libre_Patient_t* people,
                                int maxPeople, Glu_History_t& h);
/** @brief /llu/connections/{id}/graph: the last 12 hours. -1 if not an answer. */
int glu_Parse_Libre_Graph(const char* body, size_t length, uint32_t now, Glu_History_t& h);

// Hashes (Nightscout's API-SECRET, LibreLinkUp's Account-Id) ------------------------------------------------------------
void glu_Sha1_Hex(const char* text, char out[41]);
void glu_Sha256_Hex(const char* text, char out[65]);

#endif
