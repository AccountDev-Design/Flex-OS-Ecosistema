// #############################################################
// ##  FlexOS_AI.cpp  ·  FLEX INTELLIGENCE  ·  MOTOR Y CLIENTE
// ##  Ver FlexOS_AI.h para el contrato. Aqui, el como.
// #############################################################

#include "FlexOS_AI.h"
#include "FlexOS_Cowork.h"          // CW_KIND_* : el mismo vocabulario de tareas

#include <string.h>
#include <stdio.h>

#if FLEXOS_AI_NET
  #include <Arduino.h>
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  #include <HTTPClient.h>
  #include "esp_heap_caps.h"
  // vTaskDelay: la espera del socket cede la CPU en vez de girar en
  // vacio. En el core de Arduino-ESP32 estas dos cabeceras llegan por
  // Arduino.h, pero no en todas las versiones -- incluirlas aqui hace
  // que el modulo no dependa de ese detalle.
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
#endif

// =============================================================
//  ESTADO
//  -------------------------------------------------------------
//  La CA raiz vive aparte de AiConfig porque son 2 KB y la
//  configuracion se copia por valor en varios sitios de la UI.
//  Copiar 2 KB de PEM para pintar una fila de Ajustes seria
//  gratuito solo en apariencia.
// =============================================================
static AiConfig gAi;
static char     gAiCa[AI_CA_MAX] = "";
static size_t   gAiCaLen = 0;

// Consentimiento de Flex Vault: un item, o 0 = ninguno.
static uint16_t gAiVaultItem = 0;

AiConfig* aiConfig(){ return &gAi; }

void aiConfigDefaults(){
  memset(&gAi, 0, sizeof(gAi));
  // De fabrica: activada como funcion, pero SIN servidor, SIN token y
  // SIN ningun permiso de salida. Asi la app se puede abrir y explicar
  // que le falta, en vez de no existir hasta que alguien la configure.
  gAi.enabled     = true;
  gAi.perms       = 0;
  gAi.autoCorrect = false;      // autocorregir solo se activa a mano
  gAi.localOnly   = false;
  gAi.openMode    = AI_OPEN_PANEL;
  gAiVaultItem    = 0;
}

bool aiValidUrl(const char* url){
  if(!url) return false;
  size_t l = strlen(url);
  if(l < 12 || l >= AI_URL_MAX) return false;
  if(strncmp(url, "https://", 8) != 0) return false;   // http:// no vale, y no es negociable
  const char* host = url + 8;
  if(*host == 0 || *host == '/') return false;         // https:// a secas, sin host
  for(const char* p = url; *p; p++){
    unsigned char c = (unsigned char)*p;
    if(c < 0x21 || c > 0x7E) return false;             // espacios y control: fuera
  }
  return true;
}

bool aiConfigured(){
  return gAi.enabled && !gAi.localOnly &&
         aiValidUrl(gAi.url) && gAi.devId[0] != 0 && gAi.token[0] != 0;
}

bool aiHasRootCa(){ return gAiCaLen > 0; }
const char* aiRootCa(){ return gAiCa; }
size_t aiRootCaLen(){ return gAiCaLen; }

void aiSetRootCa(const char* pem){
  if(!pem){ gAiCa[0] = 0; gAiCaLen = 0; return; }
  size_t l = strlen(pem);
  if(l >= AI_CA_MAX) l = AI_CA_MAX - 1;
  memcpy(gAiCa, pem, l);
  gAiCa[l] = 0;
  // Una CA que no parezca un PEM no se guarda como valida: es mejor
  // decir "sin certificado" que intentar conectar con basura y
  // culpar despues a la red.
  gAiCaLen = (strstr(gAiCa, "-----BEGIN CERTIFICATE-----") != NULL) ? l : 0;
  if(gAiCaLen == 0) gAiCa[0] = 0;
}

const char* aiErrorText(uint8_t err){
  switch(err){
    case AI_ERR_NONE:     return "Listo";
    case AI_ERR_NOCFG:    return "Sin configurar";
    case AI_ERR_NONET:    return "Sin conexi\xC3\xB3n";
    case AI_ERR_TLS:      return "Falta el certificado del servidor";
    case AI_ERR_HTTP:     return "El servidor respondi\xC3\xB3 con un error";
    case AI_ERR_AUTH:     return "Token rechazado";
    case AI_ERR_PARSE:    return "Respuesta ilegible";
    case AI_ERR_TIMEOUT:  return "El servidor tard\xC3\xB3 demasiado";
    case AI_ERR_TOOBIG:   return "El contenido es demasiado grande";
    case AI_ERR_MEM:      return "Sin memoria";
    case AI_ERR_DENIED:   return "Permiso desactivado";
    case AI_ERR_DISABLED: return "Flex Intelligence est\xC3\xA1 desactivada";
    default:              return "Error desconocido";
  }
}

// =============================================================
//  LISTA BLANCA DE ACCIONES
// =============================================================
bool aiActionNeedsConfirm(uint8_t act){
  // SHOW_TEXT es la unica que no cambia nada del dispositivo. Todo lo
  // demas escribe, sustituye o abre algo: confirmacion visible.
  return act != AI_ACT_NONE && act != AI_ACT_SHOW_TEXT;
}

const char* aiActionName(uint8_t act){
  switch(act){
    case AI_ACT_SHOW_TEXT:    return "Ver resultado";
    case AI_ACT_CREATE_NOTE:  return "Crear nota";
    case AI_ACT_REPLACE_TEXT: return "Aplicar";
    case AI_ACT_APPEND_NOTE:  return "A\xC3\xB1" "adir a la nota";
    case AI_ACT_OPEN_APP:     return "Abrir";
    default:                  return "";
  }
}

// Tabla nombre -> codigo. Es la lista blanca ENTERA: si el servidor
// manda "delete_file", "run", "http_get" o cualquier otra cosa, no
// esta aqui y se convierte en "enseña el texto y no hagas nada".
struct AiActName { const char* name; uint8_t act; };
static const AiActName AI_ACTS[] = {
  { "show_text",    AI_ACT_SHOW_TEXT    },
  { "create_note",  AI_ACT_CREATE_NOTE  },
  { "replace_text", AI_ACT_REPLACE_TEXT },
  { "append_note",  AI_ACT_APPEND_NOTE  },
  { "open_app",     AI_ACT_OPEN_APP     },
};
#define AI_ACTS_N ((int)(sizeof(AI_ACTS) / sizeof(AI_ACTS[0])))

uint8_t aiActionFromName(const char* s, size_t n){
  if(!s || n == 0) return AI_ACT_NONE;
  for(int i = 0; i < AI_ACTS_N; i++){
    size_t l = strlen(AI_ACTS[i].name);
    if(l == n && strncmp(s, AI_ACTS[i].name, l) == 0) return AI_ACTS[i].act;
  }
  return AI_ACT_NONE;
}

// =============================================================
//  CONSENTIMIENTO DE FLEX VAULT
// =============================================================
void aiVaultConsentGrant(uint16_t itemId){ gAiVaultItem = itemId; }
void aiVaultConsentClear(){ gAiVaultItem = 0; }
bool aiVaultConsentHas(uint16_t itemId){ return itemId != 0 && gAiVaultItem == itemId; }
uint16_t aiVaultConsentItem(){ return gAiVaultItem; }

// =============================================================
//  JSON: ESCRITURA
// =============================================================
size_t aiJsonEscape(char* out, size_t cap, const char* s, size_t n){
  if(!out || cap == 0) return 0;
  size_t o = 0;
  if(!s){ out[0] = 0; return 0; }
  for(size_t i = 0; i < n && s[i]; i++){
    unsigned char c = (unsigned char)s[i];
    const char* esc = NULL;
    char buf[8];
    switch(c){
      case '"':  esc = "\\\""; break;
      case '\\': esc = "\\\\"; break;
      case '\n': esc = "\\n";  break;
      case '\r': esc = "\\r";  break;
      case '\t': esc = "\\t";  break;
      default:
        if(c < 0x20){
          // Los de control van en \u00XX. Un byte de control crudo
          // dentro de una cadena JSON es invalido y hay servidores
          // que rechazan la peticion entera por uno solo.
          snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
          esc = buf;
        }
        break;
    }
    if(esc){
      size_t el = strlen(esc);
      if(o + el >= cap) break;
      memcpy(out + o, esc, el); o += el;
    } else {
      if(o + 1 >= cap) break;
      out[o++] = (char)c;
    }
  }
  out[o] = 0;
  return o;
}

// Nombre de tarea que entiende el servidor. Se manda un verbo y no el
// numero de CW_KIND_*: un numero ata el protocolo al orden del enum,
// y anadir una tarea manana renumeraria las de un servidor ya
// desplegado.
static const char* aiKindSlug(uint8_t kind){
  switch(kind){
    case CW_KIND_CORRECT:   return "correct";
    case CW_KIND_SUMMARY:   return "summarize";
    case CW_KIND_TRANSLATE: return "translate";
    case CW_KIND_TONE:      return "tone";
    case CW_KIND_EXPLAIN:   return "explain";
    case CW_KIND_ANALYZE:   return "analyze";
    case CW_KIND_IMAGE:     return "image";
    case CW_KIND_FIND:      return "find";
    default:                return "";
  }
}

// Que permiso hace falta para cada tarea.
static uint8_t aiKindPerm(uint8_t kind){
  if(kind == CW_KIND_IMAGE) return AI_PERM_IMAGE;
  if(kind == CW_KIND_FIND)  return AI_PERM_FILES;
  return AI_PERM_TEXT;
}

size_t aiBuildRequest(char* out, size_t cap, uint8_t kind,
                      const char* text, size_t textLen, const char* extra){
  if(!out || cap < 64) return 0;
  out[0] = 0;
  const char* slug = aiKindSlug(kind);
  if(!slug[0]) return 0;
  if(!gAi.enabled || gAi.localOnly) return 0;
  if((gAi.perms & aiKindPerm(kind)) == 0) return 0;   // sin permiso no se construye nada

  // Campo a campo con snprintf acotado y comprobando el retorno: si
  // algo no cabe se devuelve 0 y no se consulta, en vez de mandar un
  // JSON truncado que el servidor rechazaria de todas formas.
  size_t o = 0;
  int w = snprintf(out + o, cap - o, "{\"v\":1,\"device\":\"");
  if(w < 0 || (size_t)w >= cap - o) return 0;
  o += (size_t)w;
  o += aiJsonEscape(out + o, cap - o, gAi.devId, strlen(gAi.devId));
  w = snprintf(out + o, cap - o, "\",\"task\":\"%s\",\"text\":\"", slug);
  if(w < 0 || (size_t)w >= cap - o) return 0;
  o += (size_t)w;

  size_t esc = aiJsonEscape(out + o, cap - o, text, textLen);
  // Un texto que no cabe entero no se manda a medias: media nota
  // resumida es un resultado equivocado, no un resultado parcial.
  if(text && textLen > 0 && esc == 0) return 0;
  o += esc;

  if(extra && extra[0]){
    w = snprintf(out + o, cap - o, "\",\"extra\":\"");
    if(w < 0 || (size_t)w >= cap - o) return 0;
    o += (size_t)w;
    o += aiJsonEscape(out + o, cap - o, extra, strlen(extra));
  }
  w = snprintf(out + o, cap - o, "\"}");
  if(w < 0 || (size_t)w >= cap - o) return 0;
  o += (size_t)w;

  // EL TOKEN NO ESTA AQUI. Va en la cabecera Authorization: los
  // cuerpos de peticion acaban en los registros del servidor mucho
  // mas a menudo que las cabeceras de autorizacion.
  return o;
}

// =============================================================
//  JSON: LECTURA
//  -------------------------------------------------------------
//  Lector acotado, no un arbol: busca las claves que conoce en el
//  primer nivel y copia su valor. Mismo criterio que el lector de
//  Noticias -- estos bytes vienen de la red, asi que cada copia
//  esta acotada por el tamano del DESTINO, no por lo que diga el
//  origen.
// =============================================================

// Copia el valor de cadena que empieza en `p` (justo tras la comilla
// de apertura) hasta la comilla de cierre, deshaciendo los escapes.
// Devuelve el puntero tras la comilla final, o NULL si no la hay.
static const char* aiReadStr(const char* p, const char* end, char* out, size_t cap, size_t* wrote){
  size_t o = 0;
  while(p < end){
    char c = *p++;
    if(c == '"'){ if(out && cap) out[o < cap ? o : cap - 1] = 0; if(wrote) *wrote = o; return p; }
    if(c == '\\'){
      if(p >= end) break;
      char e = *p++;
      char r;
      switch(e){
        case 'n': r = '\n'; break;
        case 'r': r = '\r'; break;
        case 't': r = '\t'; break;
        case 'b': r = '\b'; break;
        case 'f': r = '\f'; break;
        case 'u': {
          // \uXXXX. Solo se traduce el tramo ASCII; el resto se
          // sustituye por '?'. Es honesto: la fuente 5x7 del sistema
          // no dibuja mas que latino, asi que fingir que se decodifica
          // UTF-16 completo solo produciria bytes que nadie ve.
          if(p + 4 > end){ p = end; r = '?'; break; }
          unsigned v = 0;
          for(int k = 0; k < 4; k++){
            char h = p[k];
            v <<= 4;
            if(h >= '0' && h <= '9')      v |= (unsigned)(h - '0');
            else if(h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
            else if(h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
            else { v = '?'; break; }
          }
          p += 4;
          r = (v >= 0x20 && v < 0x7F) ? (char)v : '?';
          break;
        }
        default: r = e; break;                 // \" \\ \/ y cualquier otro: literal
      }
      if(out && o + 1 < cap) out[o++] = r;
      else if(!out) o++;
      continue;
    }
    if(out && o + 1 < cap) out[o++] = c;
    else if(!out) o++;
    // Si el valor es mas largo que el destino se sigue LEYENDO (para
    // encontrar la comilla de cierre y no desincronizar el parser)
    // pero ya no se copia. Truncar y seguir es correcto; salirse del
    // buffer, no.
  }
  if(out && cap) out[cap - 1] = 0;
  return NULL;
}

// Busca "clave" en el primer nivel del objeto y devuelve el puntero al
// primer caracter de su valor, o NULL. No monta arbol: salta cadenas
// enteras para no confundir una clave con un texto que la contenga.
static const char* aiFindKey(const char* body, const char* end, const char* key){
  size_t kl = strlen(key);
  const char* p = body;
  int depth = 0;
  while(p < end){
    char c = *p;
    if(c == '"'){
      const char* q = p + 1;
      const char* after = aiReadStr(q, end, NULL, 0, NULL);
      if(!after) return NULL;
      // Solo cuenta como clave si esta en el primer nivel y le sigue ':'
      if(depth == 1 && (size_t)(after - 1 - q) == kl && strncmp(q, key, kl) == 0){
        const char* v = after;
        while(v < end && (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')) v++;
        if(v < end && *v == ':'){
          v++;
          while(v < end && (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')) v++;
          return v;
        }
      }
      p = after;
      continue;
    }
    if(c == '{' || c == '[') depth++;
    else if(c == '}' || c == ']') depth--;
    p++;
  }
  return NULL;
}

bool aiParseResponse(const char* body, size_t n, AiResult* res){
  if(!res) return false;
  memset(res, 0, sizeof(*res));
  res->action = AI_ACT_NONE;
  if(!body || n == 0){ res->err = AI_ERR_PARSE; return false; }
  const char* end = body + n;

  // "error" primero: si el servidor dice que fallo, lo que venga en
  // "text" no es un resultado.
  const char* pe = aiFindKey(body, end, "error");
  if(pe && *pe == '"'){
    aiReadStr(pe + 1, end, res->detail, sizeof(res->detail), NULL);
    if(res->detail[0]){
      // Un servidor puede distinguir "token invalido" del resto. Se
      // reconoce por prefijo para poder ofrecer "revisa tu token" en
      // vez de un generico "error del servidor".
      res->err = (strstr(res->detail, "auth") || strstr(res->detail, "token") ||
                  strstr(res->detail, "unauthorized")) ? AI_ERR_AUTH : AI_ERR_HTTP;
      return false;
    }
  }

  const char* pt = aiFindKey(body, end, "text");
  if(pt && *pt == '"'){
    size_t w = 0;
    aiReadStr(pt + 1, end, res->text, sizeof(res->text), &w);
    res->textLen = (w < sizeof(res->text)) ? w : sizeof(res->text) - 1;
  }

  const char* pa = aiFindKey(body, end, "action");
  if(pa && *pa == '"'){
    char nameBuf[32];
    size_t w = 0;
    aiReadStr(pa + 1, end, nameBuf, sizeof(nameBuf), &w);
    if(w >= sizeof(nameBuf)) w = sizeof(nameBuf) - 1;
    res->action = aiActionFromName(nameBuf, w);
  }

  const char* pg = aiFindKey(body, end, "arg");
  if(pg && *pg == '"') aiReadStr(pg + 1, end, res->arg, sizeof(res->arg), NULL);

  if(res->textLen == 0 && res->action == AI_ACT_NONE){
    res->err = AI_ERR_PARSE;
    return false;
  }
  // LA REGLA. Una accion desconocida NO es un error del servidor y
  // tampoco es una invitacion a improvisar: el resultado se enseña y
  // no se ejecuta nada.
  if(res->action == AI_ACT_NONE) res->action = AI_ACT_SHOW_TEXT;
  res->needsConfirm = aiActionNeedsConfirm(res->action);
  res->err = AI_ERR_NONE;
  return true;
}

// =============================================================
//  CLIENTE HTTPS
//  -------------------------------------------------------------
//  Corre SIEMPRE en la tarea de fondo de Cowork. No dibuja, no toca
//  ningun framebuffer y no llama a nada del .ino: por eso puede
//  bloquear tranquilamente en el socket sin afectar al presenter.
// =============================================================
#if FLEXOS_AI_NET

static uint8_t aiDoQuery(const char* reqBody, size_t reqLen, AiResult* res){
  if(WiFi.status() != WL_CONNECTED) return AI_ERR_NONET;
  // SIN CA RAIZ NO SE CONECTA. Ni setInsecure() ni un interruptor en
  // Ajustes para saltarselo: por este socket va el texto del usuario y
  // su token. Ver la cabecera de FlexOS_AI.h.
  if(!aiHasRootCa()) return AI_ERR_TLS;

  char* resp = (char*)heap_caps_malloc(AI_RESP_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!resp) return AI_ERR_MEM;

  uint8_t rc = AI_ERR_HTTP;
  {
    WiFiClientSecure sec;
    HTTPClient       http;
    sec.setCACert(aiRootCa());
    sec.setTimeout(AI_HTTP_TIMEOUT_MS / 1000);
    if(!http.begin(sec, gAi.url)){
      heap_caps_free(resp);
      return AI_ERR_TLS;
    }
    http.setTimeout(AI_HTTP_TIMEOUT_MS);
    http.setConnectTimeout(AI_HTTP_TIMEOUT_MS);
    http.useHTTP10(true);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    // El token va AQUI, no en el cuerpo (ver aiBuildRequest).
    {
      char auth[AI_TOKEN_MAX + 16];
      snprintf(auth, sizeof(auth), "Bearer %s", gAi.token);
      http.addHeader("Authorization", auth);
    }
    int code = http.POST((uint8_t*)reqBody, reqLen);
    if(code == 401 || code == 403){
      rc = AI_ERR_AUTH;
    } else if(code != 200){
      rc = (code <= 0) ? AI_ERR_NONET : AI_ERR_HTTP;
    } else {
      int len = http.getSize();
      WiFiClient* st = http.getStreamPtr();
      size_t got = 0;
      uint32_t t0 = millis();
      while(st && got + 1 < AI_RESP_MAX && (len < 0 || got < (size_t)len)){
        int av = st->available();
        if(av > 0){
          size_t room = AI_RESP_MAX - 1 - got;
          int r = st->read((uint8_t*)resp + got, av > (int)room ? room : (size_t)av);
          if(r > 0){ got += (size_t)r; t0 = millis(); }
        } else {
          if(!st->connected() && st->available() == 0) break;
          if(millis() - t0 > AI_HTTP_TIMEOUT_MS){ rc = AI_ERR_TIMEOUT; break; }
          vTaskDelay(pdMS_TO_TICKS(10));      // cede CPU: nunca espera activa
        }
      }
      if(rc != AI_ERR_TIMEOUT){
        resp[got] = 0;
        rc = aiParseResponse(resp, got, res) ? (uint8_t)AI_ERR_NONE
                                             : (res->err ? res->err : (uint8_t)AI_ERR_PARSE);
      }
    }
    http.end();
  }
  heap_caps_free(resp);
  return rc;
}

uint8_t aiQuery(uint8_t kind, const char* text, size_t textLen,
                const char* extra, AiResult* res){
  if(!res) return AI_ERR_MEM;
  memset(res, 0, sizeof(*res));
  if(!gAi.enabled) return AI_ERR_DISABLED;
  if(gAi.localOnly) return AI_ERR_DENIED;
  if((gAi.perms & aiKindPerm(kind)) == 0) return AI_ERR_DENIED;
  if(!aiConfigured()) return AI_ERR_NOCFG;

  char* req = (char*)heap_caps_malloc(AI_REQ_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!req) return AI_ERR_MEM;
  size_t rl = aiBuildRequest(req, AI_REQ_MAX, kind, text, textLen, extra);
  if(rl == 0){ heap_caps_free(req); return AI_ERR_TOOBIG; }
  uint8_t rc = aiDoQuery(req, rl, res);
  // El cuerpo de la peticion lleva texto del usuario: se pisa antes de
  // devolverlo al montones, no solo se libera.
  memset(req, 0, AI_REQ_MAX);
  heap_caps_free(req);
  return rc;
}

uint8_t aiSelfTest(){
  AiResult r;
  if(!gAi.enabled) return AI_ERR_DISABLED;
  if(!aiConfigured()) return AI_ERR_NOCFG;
  if(!aiHasRootCa()) return AI_ERR_TLS;
  if((gAi.perms & AI_PERM_TEXT) == 0) return AI_ERR_DENIED;
  return aiQuery(CW_KIND_ANALYZE, "ping", 4, "selftest", &r);
}

#else   // ---- sin red (pruebas de host, o placa que no es la P4) ----

uint8_t aiQuery(uint8_t kind, const char* text, size_t textLen,
                const char* extra, AiResult* res){
  (void)kind; (void)text; (void)textLen; (void)extra;
  if(res){ memset(res, 0, sizeof(*res)); res->err = AI_ERR_NONET; }
  return AI_ERR_NONET;
}
uint8_t aiSelfTest(){ return AI_ERR_NONET; }

#endif  // FLEXOS_AI_NET
