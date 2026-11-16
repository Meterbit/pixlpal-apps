/**
 * @file sca_Tips.h
 * @brief Scam Alerts' own tips and their pictures (SVG, 48 x 48).
 */
#ifndef SCA_TIPS_H
#define SCA_TIPS_H

#define SCA_SVG(body) "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'>" body "</svg>"

enum { I_LOCK, I_CODE, I_LINK, I_CLOCK, I_SHIELD, I_EYE, I_GIFT, I_CARD, I_PHONE, I_SWAP, I_WARN, I_COUNT };

static const char* const SCA_ICONS[I_COUNT] = {
    // A padlock.
    SCA_SVG("<path d='M15 21 V15 A9 9 0 0 1 33 15 V21' stroke='#B0BEC5' stroke-width='4' fill='none'/>"
            "<rect x='9' y='21' width='30' height='22' rx='4' fill='#FFC107'/><circle cx='24' cy='30' r='3.5' fill='#5D4037'/><rect x='22.5' y='31' width='3' height='7' fill='#5D4037'/>"),
    // A message with a code in it.
    SCA_SVG("<path d='M6 8 H42 A3 3 0 0 1 45 11 V31 A3 3 0 0 1 42 34 H18 L9 42 V34 H6 A3 3 0 0 1 3 31 V11 A3 3 0 0 1 6 8 Z' fill='#4FC3F7'/>"
            "<circle cx='13' cy='21' r='3' fill='#FFFFFF'/><circle cx='21' cy='21' r='3' fill='#FFFFFF'/><circle cx='29' cy='21' r='3' fill='#FFFFFF'/><circle cx='37' cy='21' r='3' fill='#FFFFFF'/>"),
    // A chain link, crossed out.
    SCA_SVG("<path d='M20 28 L28 20' stroke='#B0BEC5' stroke-width='4'/><path d='M22 14 L26 10 A8 8 0 0 1 38 22 L34 26' stroke='#B0BEC5' stroke-width='4' fill='none'/>"
            "<path d='M26 34 L22 38 A8 8 0 0 1 10 26 L14 22' stroke='#B0BEC5' stroke-width='4' fill='none'/><path d='M8 8 L40 40' stroke='#F44336' stroke-width='5'/>"),
    // A clock, hurrying.
    SCA_SVG("<circle cx='26' cy='26' r='17' fill='#FFB300'/><circle cx='26' cy='26' r='13' fill='#FFF8E1'/><path d='M26 16 V26 L33 31' stroke='#5D4037' stroke-width='3.5' fill='none'/>"
            "<path d='M2 18 H8 M0 26 H7 M2 34 H8' stroke='#FFB300' stroke-width='3'/>"),
    // A shield with a tick.
    SCA_SVG("<path d='M24 3 L42 10 V23 C42 34 34 41 24 45 C14 41 6 34 6 23 V10 Z' fill='#26A69A'/><path d='M15 24 L22 31 L34 17' stroke='#FFFFFF' stroke-width='4.5' fill='none'/>"),
    // An eye.
    SCA_SVG("<path d='M2 24 Q24 2 46 24 Q24 46 2 24 Z' fill='#ECEFF1'/><circle cx='24' cy='24' r='9' fill='#42A5F5'/><circle cx='24' cy='24' r='4' fill='#102027'/><circle cx='27' cy='21' r='1.8' fill='#FFFFFF'/>"),
    // A present: a prize you didn't enter.
    SCA_SVG("<rect x='6' y='20' width='36' height='24' rx='2' fill='#EC407A'/><rect x='4' y='14' width='40' height='8' rx='2' fill='#F06292'/><rect x='21' y='14' width='6' height='30' fill='#FFD54F'/>"
            "<path d='M24 14 C18 4 10 8 14 14 Z M24 14 C30 4 38 8 34 14 Z' fill='#FFD54F'/>"),
    // A bank card.
    SCA_SVG("<rect x='3' y='10' width='42' height='28' rx='4' fill='#5C6BC0'/><rect x='3' y='16' width='42' height='6' fill='#283593'/><rect x='8' y='27' width='9' height='6' rx='1' fill='#FFD54F'/><rect x='21' y='29' width='18' height='3' fill='#C5CAE9'/>"),
    // A phone, ringing.
    SCA_SVG("<path d='M14 6 L20 6 L23 15 L18 19 C21 25 23 27 29 30 L33 25 L42 28 L42 34 C42 38 39 42 34 41 C19 38 10 29 7 14 C6 9 10 6 14 6 Z' fill='#66BB6A'/>"
            "<path d='M30 6 A12 12 0 0 1 42 18 M30 12 A6 6 0 0 1 36 18' stroke='#66BB6A' stroke-width='3' fill='none'/>"),
    // Money going back: two arrows.
    SCA_SVG("<path d='M6 16 H36' stroke='#66BB6A' stroke-width='5'/><path d='M32 8 L42 16 L32 24 Z' fill='#66BB6A'/>"
            "<path d='M42 32 H12' stroke='#FFA726' stroke-width='5'/><path d='M16 24 L6 32 L16 40 Z' fill='#FFA726'/>"),
    // A warning sign.
    SCA_SVG("<path d='M24 4 L46 42 H2 Z' fill='#FFC107'/><rect x='21.5' y='16' width='5' height='14' rx='2' fill='#212121'/><circle cx='24' cy='36' r='3' fill='#212121'/>"),
};

struct Sca_Tip_t { const char* headline; const char* body; uint8_t icon; };

static const Sca_Tip_t SCA_TIPS[] = {
    {"We will never ask for your PIN", "Not by phone, text, email or in person. Anyone who asks for your PIN or password is a fraudster.", I_LOCK},
    {"Your one-time code is yours alone", "Never read out or forward a code we send you. A caller who asks for it wants your money.", I_CODE},
    {"Think before you click", "Links in texts and emails can lead to fake bank pages. Type our address yourself, or use our app.", I_LINK},
    {"Rushed? Stop and think", "\"Act now or your account closes.\" Fraudsters hurry you so you don't think. Hang up and call us.", I_CLOCK},
    {"There is no \"safe account\"", "No bank will ever ask you to move your money to keep it safe. Anyone who does is a fraudster.", I_SHIELD},
    {"Cover your PIN", "Shield the keypad at cash machines and tills. Look for anything stuck on the card slot.", I_EYE},
    {"Too good to be true? It is", "Prizes you never entered, sure-fire investments, big returns fast: walk away.", I_GIFT},
    {"Keep your card in sight", "Don't let your card out of view when you pay. Lost it? Tell us at once and we'll stop it.", I_CARD},
    {"A call can show our number and still be fake", "If in doubt, hang up and call us back on the number on the back of your card.", I_PHONE},
    {"Money sent to you \"by mistake\"?", "Asked to send it back? Check with us first: it may be a stolen payment.", I_SWAP},
};
#define SCA_TIP_COUNT ((int)(sizeof(SCA_TIPS) / sizeof(SCA_TIPS[0])))

#endif
