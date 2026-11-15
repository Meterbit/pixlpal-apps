/**
 * @file nxt_Icons.h
 * @brief Now & Next's pictures: small SVGs (48 x 48) drawn by Pixlpal (mtb_Kit_Draw_Svg), and the words that choose
 *        each from a step's name ("Reading 15" shows the book).
 */
#ifndef NXT_ICONS_H
#define NXT_ICONS_H

struct Nxt_Icon_t { const char* words; const char* svg; };

#define NXT_SVG(body) "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'>" body "</svg>"

static const Nxt_Icon_t NXT_ICONS[] = {
  {"read reading book books library story phonics", NXT_SVG(
    "<path d='M4 10 Q14 6 24 11 L24 41 Q14 36 4 40 Z' fill='#4FC3F7'/><path d='M44 10 Q34 6 24 11 L24 41 Q34 36 44 40 Z' fill='#29B6F6'/>"
    "<path d='M8 16 Q14 14 20 17 M8 22 Q14 20 20 23 M8 28 Q14 26 20 29 M28 17 Q34 14 40 16 M28 23 Q34 20 40 22 M28 29 Q34 26 40 28' stroke='#FFFFFF' stroke-width='2' fill='none'/>")},
  {"write writing handwriting spelling english pencil draw", NXT_SVG(
    "<polygon points='10,38 14,26 34,6 42,14 22,34' fill='#FFCA28'/><polygon points='10,38 14,26 22,34' fill='#FFE0B2'/><polygon points='10,38 12,32 16,36' fill='#424242'/>"
    "<polygon points='34,6 42,14 39,17 31,9' fill='#EF5350'/>")},
  {"math maths mathematics numbers number counting count sums", NXT_SVG(
    "<rect x='3' y='8' width='13' height='32' rx='3' fill='#EF5350'/><rect x='18' y='8' width='13' height='32' rx='3' fill='#66BB6A'/><rect x='33' y='8' width='13' height='32' rx='3' fill='#42A5F5'/>"
    "<path d='M8 16 L10 14 L10 34 M23 17 Q25 13 27 17 Q27 21 22 33 L28 33 M37 15 L42 15 L39 22 Q44 23 42 30 Q39 35 36 31' stroke='#FFFFFF' stroke-width='2.5' fill='none' stroke-linecap='round'/>")},
  {"science experiment lab", NXT_SVG(
    "<path d='M19 6 L29 6 L29 18 L40 38 Q42 43 37 43 L11 43 Q6 43 8 38 L19 18 Z' fill='#B2EBF2'/><path d='M13 30 L35 30 L39 38 Q41 42 37 42 L11 42 Q7 42 9 38 Z' fill='#AB47BC'/>"
    "<circle cx='20' cy='36' r='2' fill='#FFFFFF'/><circle cx='27' cy='34' r='1.5' fill='#FFFFFF'/><rect x='17' y='4' width='14' height='4' rx='2' fill='#80DEEA'/>")},
  {"art painting paint craft colouring coloring drawing", NXT_SVG(
    "<path d='M24 5 Q44 5 44 22 Q44 30 36 30 Q31 30 32 35 Q33 43 24 43 Q4 43 4 24 Q4 5 24 5 Z' fill='#FFCC80'/>"
    "<circle cx='15' cy='16' r='4' fill='#EF5350'/><circle cx='26' cy='12' r='4' fill='#FFEE58'/><circle cx='36' cy='18' r='4' fill='#42A5F5'/><circle cx='13' cy='28' r='4' fill='#66BB6A'/><circle cx='22' cy='35' r='3.5' fill='#AB47BC'/>")},
  {"music singing sing song songs dance dancing", NXT_SVG(
    "<path d='M18 34 L18 10 L40 5 L40 29' stroke='#FFFFFF' stroke-width='3' fill='none'/><ellipse cx='13' cy='35' rx='6' ry='5' fill='#F06292'/><ellipse cx='35' cy='30' rx='6' ry='5' fill='#F06292'/>"
    "<path d='M18 15 L40 10' stroke='#FFFFFF' stroke-width='3'/>")},
  {"pe sport sports gym games play playtime football exercise", NXT_SVG(
    "<circle cx='24' cy='24' r='19' fill='#FFFFFF'/><polygon points='24,15 31,20 28,28 20,28 17,20' fill='#212121'/>"
    "<path d='M24 15 L24 6 M31 20 L40 16 M28 28 L33 37 M20 28 L15 37 M17 20 L8 16' stroke='#212121' stroke-width='2'/>")},
  {"snack lunch eat eating food dinner breakfast fruit milk", NXT_SVG(
    "<path d='M24 14 Q16 8 10 14 Q4 22 9 33 Q14 44 20 42 Q24 40 28 42 Q34 44 39 33 Q44 22 38 14 Q32 8 24 14 Z' fill='#EF5350'/>"
    "<path d='M24 14 Q24 8 28 4' stroke='#6D4C41' stroke-width='2.5' fill='none'/><path d='M26 10 Q33 4 38 8 Q33 12 26 10 Z' fill='#66BB6A'/>")},
  {"circle carpet group talk talking together register assembly sharing", NXT_SVG(
    "<circle cx='12' cy='14' r='5' fill='#FFB74D'/><path d='M4 32 Q4 21 12 21 Q20 21 20 32 Z' fill='#42A5F5'/><circle cx='36' cy='14' r='5' fill='#A1887F'/><path d='M28 32 Q28 21 36 21 Q44 21 44 32 Z' fill='#66BB6A'/>"
    "<circle cx='24' cy='24' r='5' fill='#FFCC80'/><path d='M16 42 Q16 31 24 31 Q32 31 32 42 Z' fill='#EF5350'/>")},
  {"computer computers ict coding tablet ipad screen", NXT_SVG(
    "<rect x='5' y='8' width='38' height='26' rx='3' fill='#90A4AE'/><rect x='9' y='12' width='30' height='18' fill='#4FC3F7'/>"
    "<rect x='20' y='34' width='8' height='5' fill='#78909C'/><rect x='13' y='39' width='22' height='3' rx='1.5' fill='#90A4AE'/>")},
  {"outside outdoor outdoors garden walk nature park forest", NXT_SVG(
    "<circle cx='24' cy='17' r='13' fill='#66BB6A'/><circle cx='15' cy='22' r='8' fill='#4CAF50'/><circle cx='33' cy='22' r='8' fill='#43A047'/><rect x='21' y='26' width='6' height='16' fill='#8D6E63'/>"
    "<rect x='4' y='41' width='40' height='3' rx='1.5' fill='#7CB342'/>")},
  {"rest quiet calm nap sleep relax relaxing meditation breathe", NXT_SVG(
    "<path d='M30 6 Q14 8 13 24 Q14 40 32 42 Q20 36 20 24 Q20 12 30 6 Z' fill='#FFF176'/>"
    "<path d='M33 12 L38 12 L33 18 L38 18 M38 22 L42 22 L38 27 L42 27' stroke='#FFFFFF' stroke-width='2' fill='none'/>")},
  {"wash washing hands toilet bathroom", NXT_SVG(
    "<path d='M24 4 Q36 20 36 29 Q36 41 24 41 Q12 41 12 29 Q12 20 24 4 Z' fill='#29B6F6'/><path d='M18 29 Q18 35 24 36' stroke='#E1F5FE' stroke-width='2.5' fill='none'/>")},
  {"tidy tidying clean cleaning pack packing away put", NXT_SVG(
    "<rect x='7' y='20' width='34' height='22' rx='2' fill='#A1887F'/><polygon points='5,14 43,14 41,20 7,20' fill='#8D6E63'/><rect x='19' y='26' width='10' height='4' rx='2' fill='#6D4C41'/>"
    "<circle cx='16' cy='10' r='4' fill='#EF5350'/><rect x='26' y='4' width='8' height='9' fill='#42A5F5'/>")},
  {"home hometime goodbye end", NXT_SVG(
    "<polygon points='24,5 44,23 39,23 39,42 9,42 9,23 4,23' fill='#FFB74D'/><polygon points='24,5 44,23 4,23' fill='#E57373'/>"
    "<rect x='20' y='30' width='8' height='12' fill='#6D4C41'/><rect x='12' y='27' width='6' height='6' fill='#E1F5FE'/><rect x='30' y='27' width='6' height='6' fill='#E1F5FE'/>")},
  {"bus trip travel journey", NXT_SVG(
    "<rect x='4' y='9' width='40' height='27' rx='4' fill='#FFCA28'/><rect x='8' y='13' width='9' height='9' fill='#E1F5FE'/><rect x='20' y='13' width='9' height='9' fill='#E1F5FE'/><rect x='32' y='13' width='8' height='9' fill='#E1F5FE'/>"
    "<circle cx='14' cy='37' r='5' fill='#424242'/><circle cx='34' cy='37' r='5' fill='#424242'/>")},
  {"geography world history topic globe map", NXT_SVG(
    "<circle cx='24' cy='24' r='19' fill='#42A5F5'/><path d='M14 12 Q20 10 22 16 Q18 20 20 26 Q14 28 12 22 Q10 16 14 12 Z M28 20 Q36 18 38 26 Q34 34 28 32 Q26 26 28 20 Z' fill='#66BB6A'/>")},
  {"french spanish language languages listening speaking", NXT_SVG(
    "<path d='M8 12 Q8 6 14 6 L34 6 Q40 6 40 12 L40 26 Q40 32 34 32 L22 32 L13 40 L14 32 Q8 32 8 26 Z' fill='#4DD0E1'/>"
    "<circle cx='17' cy='19' r='2.5' fill='#FFFFFF'/><circle cx='24' cy='19' r='2.5' fill='#FFFFFF'/><circle cx='31' cy='19' r='2.5' fill='#FFFFFF'/>")},
  {"choice choosing golden reward fun free star", NXT_SVG(
    "<polygon points='24,4 30,17 44,18 33,28 37,42 24,34 11,42 15,28 4,18 18,17' fill='#FFD54F'/>")},
};
static const int NXT_ICON_COUNT = (int)(sizeof(NXT_ICONS) / sizeof(NXT_ICONS[0]));
static const int NXT_ICON_DEFAULT = NXT_ICON_COUNT - 1;                       // the star

#endif
