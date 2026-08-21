#include "FlexOS_Runtime.h"

#include <FS.h>
#include <LittleFS.h>
#include <cJSON.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

static char gRuntimeError[128] = "Correcto";

static void fail(const char* text){ snprintf(gRuntimeError, sizeof(gRuntimeError), "%s", text ? text : "Error del runtime"); }

static bool readExact(File& f, void* dst, size_t n){
  uint8_t* p = static_cast<uint8_t*>(dst);
  size_t done = 0;
  while(done < n){
    size_t got = f.read(p + done, n - done);
    if(!got) return false;
    done += got;
  }
  return true;
}

static bool copyString(cJSON* obj, const char* key, char* out, size_t cap, bool required = true){
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if(!v){ if(!required){ out[0] = 0; return true; } return false; }
  if(!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= cap) return false;
  memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
  return true;
}

static bool number(cJSON* obj, const char* key, int& out, int lo, int hi, bool required = true){
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
  if(!v){ if(!required){ out = 0; return true; } return false; }
  if(!cJSON_IsNumber(v) || v->valuedouble < lo || v->valuedouble > hi || v->valuedouble != (double)(int)v->valuedouble) return false;
  out = (int)v->valuedouble;
  return true;
}

static bool color(const char* s, uint32_t& out){
  if(!s || strlen(s) != 7 || s[0] != '#') return false;
  uint32_t value = 0;
  for(int i = 1; i < 7; i++){
    char c = s[i];
    int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
    if(v < 0) return false;
    value = (value << 4) | (uint32_t)v;
  }
  out = value;
  return true;
}

static bool parseTheme(cJSON* root, FlexUiTheme& out){
  out.background = 0x0B0D16; out.surface = 0x151827; out.text = 0xF5F7FF; out.accent = 0x806BFF;
  cJSON* t = cJSON_GetObjectItemCaseSensitive(root, "theme");
  if(!t) return true;
  if(!cJSON_IsObject(t)) return false;
  const char* keys[4] = {"background", "surface", "text", "accent"};
  uint32_t* vals[4] = {&out.background, &out.surface, &out.text, &out.accent};
  for(int i = 0; i < 4; i++){
    cJSON* v = cJSON_GetObjectItemCaseSensitive(t, keys[i]);
    if(v && (!cJSON_IsString(v) || !color(v->valuestring, *vals[i]))) return false;
  }
  return true;
}

static bool parseAction(cJSON* owner, FlexUiAction& out, uint16_t permissions){
  out.type = FLEXUI_ACTION_NONE; out.value[0] = 0;
  cJSON* a = cJSON_GetObjectItemCaseSensitive(owner, "action");
  if(!a) return true;
  if(!cJSON_IsObject(a)) return false;
  char type[24];
  if(!copyString(a, "type", type, sizeof(type))) return false;
  if(!strcmp(type, "notify")){
    if(!(permissions & FLEXPERM_NOTIFICATIONS) || !copyString(a, "message", out.value, sizeof(out.value))) return false;
    out.type = FLEXUI_ACTION_NOTIFY;
    return true;
  }
  if(!strcmp(type, "navigate")){
    if(!copyString(a, "screen", out.value, sizeof(out.value))) return false;
    out.type = FLEXUI_ACTION_NAVIGATE;
    return true;
  }
  return false;
}

static bool parseComponent(cJSON* raw, FlexUiComponent& out, uint16_t permissions){
  if(!cJSON_IsObject(raw)) return false;
  memset(&out, 0, sizeof(out));
  char type[20], style[20];
  if(!copyString(raw, "type", type, sizeof(type)) || !copyString(raw, "id", out.id, sizeof(out.id)) ||
     !copyString(raw, "text", out.text, sizeof(out.text))) return false;
  int x, y, w = 0, h = 0;
  if(!number(raw, "x", x, 0, 479) || !number(raw, "y", y, 0, 799) ||
     !number(raw, "width", w, 1, 480)) return false;
  out.x = (int16_t)x; out.y = (int16_t)y; out.width = (int16_t)w;
  if(!strcmp(type, "text")){
    out.type = FLEXUI_TEXT;
    if(!copyString(raw, "style", style, sizeof(style), false)) return false;
    out.style = !strcmp(style, "title") ? FLEXUI_STYLE_TITLE : !strcmp(style, "caption") ? FLEXUI_STYLE_CAPTION : FLEXUI_STYLE_BODY;
    out.height = 0;
    return true;
  }
  if(!strcmp(type, "button")){
    if(!number(raw, "height", h, 32, 160)) return false;
    out.type = FLEXUI_BUTTON; out.height = (int16_t)h;
    return parseAction(raw, out.action, permissions);
  }
  return false;
}

static bool loadScreen(FlexRuntimeApp* app, const char* wanted, bool useStart){
  File f = LittleFS.open(app->entryPath, "r");
  if(!f || f.size() < 2 || f.size() > (size_t)app->package.memoryKB * 1024u){ if(f) f.close(); fail("Entrypoint ausente o demasiado grande"); return false; }
  size_t n = f.size();
  char* raw = (char*)malloc(n + 1);
  if(!raw){ f.close(); fail("Sin memoria para abrir la app"); return false; }
  bool ok = readExact(f, raw, n); f.close(); raw[n] = 0;
  cJSON* root = ok ? cJSON_ParseWithLength(raw, n) : nullptr;
  if(!root || !cJSON_IsObject(root)){ if(root) cJSON_Delete(root); free(raw); fail("JSON flex-ui-1 invalido"); return false; }
  cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  cJSON* screens = cJSON_GetObjectItemCaseSensitive(root, "screens");
  char target[41];
  if(useStart){
    if(!copyString(root, "startScreen", target, sizeof(target))){ cJSON_Delete(root); free(raw); fail("Falta startScreen"); return false; }
  } else snprintf(target, sizeof(target), "%s", wanted ? wanted : "");
  if(!cJSON_IsNumber(schema) || schema->valuedouble != 1.0 || !cJSON_IsArray(screens) || !parseTheme(root, app->theme)) ok = false;
  cJSON* selected = nullptr;
  if(ok){
    int count = cJSON_GetArraySize(screens);
    if(count < 1 || count > 16) ok = false;
    for(int i = 0; ok && i < count; i++){
      cJSON* s = cJSON_GetArrayItem(screens, i);
      cJSON* id = s ? cJSON_GetObjectItemCaseSensitive(s, "id") : nullptr;
      if(cJSON_IsString(id) && id->valuestring && !strcmp(id->valuestring, target)){ selected = s; break; }
    }
    if(!selected) ok = false;
  }
  if(ok){
    cJSON* comps = cJSON_GetObjectItemCaseSensitive(selected, "components");
    if(!copyString(selected, "id", app->screenId, sizeof(app->screenId)) ||
       !copyString(selected, "title", app->screenTitle, sizeof(app->screenTitle)) || !cJSON_IsArray(comps)) ok = false;
    int count = ok ? cJSON_GetArraySize(comps) : 0;
    if(count < 0 || count > FLEXUI_MAX_COMPONENTS) ok = false;
    app->componentCount = 0;
    for(int i = 0; ok && i < count; i++){
      if(!parseComponent(cJSON_GetArrayItem(comps, i), app->components[i], app->package.permissions)) ok = false;
      else app->componentCount++;
    }
  }
  cJSON_Delete(root); free(raw);
  if(!ok){ fail("Pantalla o componente flex-ui-1 no compatible"); return false; }
  app->loaded = true;
  snprintf(gRuntimeError, sizeof(gRuntimeError), "Correcto");
  return true;
}

} // namespace

bool flexRuntimeLoad(const char* packageId, FlexRuntimeApp* app){
  if(!packageId || !app){ fail("Solicitud de runtime invalida"); return false; }
  memset(app, 0, sizeof(*app));
  if(!flexPkgGet(packageId, &app->package) || !flexPkgEntryPath(packageId, app->entryPath, sizeof(app->entryPath))){
    fail("La aplicacion instalada no tiene entrypoint"); return false;
  }
  return loadScreen(app, nullptr, true);
}

bool flexRuntimeNavigate(FlexRuntimeApp* app, const char* screenId){
  if(!app || !app->loaded || !screenId || !screenId[0]){ fail("Destino de navegacion invalido"); return false; }
  return loadScreen(app, screenId, false);
}

bool flexRuntimeHit(const FlexRuntimeApp* app, int x, int y, FlexUiAction* action){
  if(action){ action->type = FLEXUI_ACTION_NONE; action->value[0] = 0; }
  if(!app || !app->loaded) return false;
  for(int i = app->componentCount - 1; i >= 0; i--){
    const FlexUiComponent& c = app->components[i];
    if(c.type != FLEXUI_BUTTON) continue;
    if(x >= c.x && x < c.x + c.width && y >= c.y && y < c.y + c.height){
      if(action) *action = c.action;
      return true;
    }
  }
  return false;
}

void flexRuntimeUnload(FlexRuntimeApp* app){ if(app) memset(app, 0, sizeof(*app)); }
const char* flexRuntimeError(){ return gRuntimeError; }
