// Pixlpal compat: a USB keyboard and mouse, as pxl_input passes them on.
#pragma once
#include <stdint.h>
#include <stdbool.h>
enum Mtb_Usb_Input_Type_t : uint8_t { MTB_USB_KEY_DOWN = 0, MTB_USB_KEY_UP, MTB_USB_MOUSE_MOVE, MTB_USB_MOUSE_BUTTONS, MTB_USB_MOUSE_WHEEL, MTB_USB_MEDIA_KEY };
#define MTB_USB_MOD_CTRL    0x11
#define MTB_USB_MOD_SHIFT   0x22
#define MTB_USB_MOD_ALT     0x44
#define MTB_USB_MOD_GUI     0x88
struct Mtb_Usb_Input_t { uint8_t type; uint8_t key; uint8_t modifiers; char ascii; uint8_t buttons; int16_t dx, dy, wheel; uint16_t media; };
bool mtb_Usb_Input_Open(bool alsoKnob);
void mtb_Usb_Input_Close(void);
bool mtb_Usb_Input_Read(Mtb_Usb_Input_t& out, uint32_t timeoutMs);
bool mtb_Usb_Keyboard_Connected(void);
bool mtb_Usb_Mouse_Connected(void);
