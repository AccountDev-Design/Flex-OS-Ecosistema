#ifndef FLEXOS_RUNTIME_H
#define FLEXOS_RUNTIME_H

#include <Arduino.h>
#include "FlexOS_Package.h"

#define FLEXUI_MAX_COMPONENTS 32

enum FlexUiComponentType : uint8_t {
  FLEXUI_TEXT = 1,
  FLEXUI_BUTTON = 2
};

enum FlexUiTextStyle : uint8_t {
  FLEXUI_STYLE_BODY = 0,
  FLEXUI_STYLE_TITLE,
  FLEXUI_STYLE_CAPTION
};

enum FlexUiActionType : uint8_t {
  FLEXUI_ACTION_NONE = 0,
  FLEXUI_ACTION_NOTIFY,
  FLEXUI_ACTION_NAVIGATE
};

struct FlexUiAction {
  FlexUiActionType type;
  char value[128];
};

struct FlexUiComponent {
  FlexUiComponentType type;
  FlexUiTextStyle style;
  char id[41];
  char text[161];
  int16_t x, y, width, height;
  FlexUiAction action;
};

struct FlexUiTheme {
  uint32_t background;
  uint32_t surface;
  uint32_t text;
  uint32_t accent;
};

struct FlexRuntimeApp {
  FlexPkgInfo package;
  char entryPath[384];
  char screenId[41];
  char screenTitle[81];
  FlexUiTheme theme;
  FlexUiComponent components[FLEXUI_MAX_COMPONENTS];
  uint8_t componentCount;
  bool loaded;
};

bool flexRuntimeLoad(const char* packageId, FlexRuntimeApp* app);
bool flexRuntimeNavigate(FlexRuntimeApp* app, const char* screenId);
bool flexRuntimeHit(const FlexRuntimeApp* app, int x, int y, FlexUiAction* action);
void flexRuntimeUnload(FlexRuntimeApp* app);
const char* flexRuntimeError();

#endif
