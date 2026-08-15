// #############################################################
//  FLEX INTELLIGENCE  ·  cliente y protocolo  ·  pruebas de host
//  ------------------------------------------------------------
//  Se prueba TODO lo que no es la red: validacion de la URL,
//  permisos, construccion de la peticion y -- lo que mas importa
//  -- el lector de la respuesta, que mastica bytes que vienen de
//  internet.
//
//  Las dos garantias que esta bateria protege:
//    · el TOKEN no viaja nunca en el cuerpo de la peticion;
//    · una accion que NO este en la lista blanca no se ejecuta
//      jamas: se convierte en "enseña el texto y no hagas nada".
// #############################################################

#include "FlexOS_AI.h"
#include "FlexOS_Cowork.h"

#include <stdio.h>
#include <string.h>

static int gFails = 0, gChecks = 0;
static void chk(bool c, const char* what){
  gChecks++;
  if(!c){ printf("  FALLO: %s\n", what); gFails++; }
}

static const char* PEM =
  "-----BEGIN CERTIFICATE-----\nMIIBfakefake\n-----END CERTIFICATE-----\n";

int main(){
  printf("=== FlexOS - Flex Intelligence (cliente y protocolo) ===\n");
  aiConfigDefaults();
  AiConfig* c = aiConfig();

  // ---- de fabrica: sin servidor y sin permisos ----
  chk(!aiConfigured(),      "de fabrica esta SIN configurar");
  chk(c->perms == 0,        "y sin ningun permiso de salida");
  chk(c->enabled,           "pero la funcion existe (se puede abrir y explicar que falta)");
  chk(!c->autoCorrect,      "autocorregir esta APAGADO de fabrica");
  chk(!aiHasRootCa(),       "y sin certificado raiz");

  // ---- validacion de la URL: https o nada ----
  chk(!aiValidUrl("http://ia.ejemplo.com/v1"), "http:// se rechaza");
  chk(!aiValidUrl("ftp://x/y"),                "otro esquema se rechaza");
  chk(!aiValidUrl("https://"),                 "https sin host se rechaza");
  chk(!aiValidUrl("https:///ruta"),            "https sin host (con ruta) se rechaza");
  chk(!aiValidUrl("https://a b.com/x"),        "con espacios se rechaza");
  chk(!aiValidUrl(NULL),                       "NULL se rechaza");
  chk(!aiValidUrl("https://x"),                "demasiado corta se rechaza");
  chk(aiValidUrl("https://ia.ejemplo.com/v1/flex"), "una https con host se acepta");

  snprintf(c->url,   sizeof(c->url),   "https://ia.ejemplo.com/v1/flex");
  snprintf(c->devId, sizeof(c->devId), "flexos-p4-001");
  snprintf(c->token, sizeof(c->token), "tok_secreto_123");
  chk(aiConfigured(), "con URL, dispositivo y token ya esta configurado");

  // ---- certificado raiz ----
  aiSetRootCa("esto no es un certificado");
  chk(!aiHasRootCa(), "algo que no es PEM no cuenta como certificado");
  aiSetRootCa(PEM);
  chk(aiHasRootCa(),  "un PEM se acepta");
  chk(aiRootCaLen() > 0, "y se guarda entero");
  aiSetRootCa(NULL);
  chk(!aiHasRootCa(), "se puede quitar");
  aiSetRootCa(PEM);

  // ---- permisos: sin bit no se construye NADA ----
  char req[AI_REQ_MAX];
  c->perms = 0;
  chk(aiBuildRequest(req, sizeof(req), CW_KIND_SUMMARY, "hola", 4, NULL) == 0,
      "sin permiso de texto no se construye la peticion");
  c->perms = AI_PERM_TEXT;
  chk(aiBuildRequest(req, sizeof(req), CW_KIND_SUMMARY, "hola", 4, NULL) > 0,
      "con permiso de texto si");
  chk(aiBuildRequest(req, sizeof(req), CW_KIND_IMAGE, "x", 1, NULL) == 0,
      "una imagen necesita SU permiso, no el de texto");
  chk(aiBuildRequest(req, sizeof(req), CW_KIND_FIND, "x", 1, NULL) == 0,
      "buscar archivos necesita el permiso de archivos");
  c->perms = AI_PERM_TEXT | AI_PERM_IMAGE | AI_PERM_FILES;
  chk(aiBuildRequest(req, sizeof(req), CW_KIND_IMAGE, "x", 1, NULL) > 0, "con el suyo, si");

  // "Solo en el dispositivo" gana a todo lo demas.
  c->localOnly = true;
  chk(aiBuildRequest(req, sizeof(req), CW_KIND_SUMMARY, "hola", 4, NULL) == 0,
      "en modo solo local no sale nada");
  chk(!aiConfigured(), "y el servicio no cuenta como configurado");
  c->localOnly = false;
  c->enabled = false;
  chk(aiBuildRequest(req, sizeof(req), CW_KIND_SUMMARY, "hola", 4, NULL) == 0,
      "desactivada tampoco construye");
  c->enabled = true;

  // ---- EL TOKEN NO VIAJA EN EL CUERPO ----
  size_t n = aiBuildRequest(req, sizeof(req), CW_KIND_SUMMARY, "hola", 4, "es");
  chk(n > 0, "construye la peticion");
  chk(strstr(req, "tok_secreto_123") == NULL, "el TOKEN no aparece en el cuerpo");
  chk(strstr(req, "flexos-p4-001") != NULL,   "el identificador de dispositivo si");
  chk(strstr(req, "\"task\":\"summarize\"") != NULL, "la tarea va como verbo, no como numero");
  chk(strstr(req, "\"extra\":\"es\"") != NULL, "el extra viaja");
  chk(req[n] == 0 && strlen(req) == n,        "la longitud devuelta es la real");

  // ---- escapado JSON ----
  n = aiBuildRequest(req, sizeof(req), CW_KIND_CORRECT, "di \"hola\"\ny\ttab\\fin", 20, NULL);
  chk(n > 0, "construye con caracteres que hay que escapar");
  chk(strstr(req, "\\\"hola\\\"") != NULL, "escapa las comillas");
  chk(strstr(req, "\\n") != NULL,          "escapa el salto de linea");
  chk(strstr(req, "\\t") != NULL,          "escapa el tabulador");
  chk(strstr(req, "\\\\") != NULL,         "escapa la barra invertida");
  { char esc[64];
    char ctrl[4] = { 'a', 0x01, 'b', 0 };
    aiJsonEscape(esc, sizeof(esc), ctrl, 3);
    chk(strstr(esc, "\\u0001") != NULL, "los bytes de control van en \\uXXXX"); }

  // Un texto que NO CABE no se manda a medias: media nota resumida es un
  // resultado equivocado, no un resultado parcial.
  { static char big[AI_REQ_MAX * 2];
    memset(big, 'z', sizeof(big) - 1); big[sizeof(big) - 1] = 0;
    chk(aiBuildRequest(req, sizeof(req), CW_KIND_SUMMARY, big, sizeof(big) - 1, NULL) == 0,
        "un texto que no cabe entero se rechaza, no se trunca"); }
  { char tiny[32];
    chk(aiBuildRequest(tiny, sizeof(tiny), CW_KIND_SUMMARY, "hola", 4, NULL) == 0,
        "un destino diminuto se rechaza sin desbordarse"); }

  // ---- LISTA BLANCA DE ACCIONES ----
  chk(aiActionFromName("create_note", 11) == AI_ACT_CREATE_NOTE, "reconoce create_note");
  chk(aiActionFromName("show_text", 9)    == AI_ACT_SHOW_TEXT,   "reconoce show_text");
  chk(aiActionFromName("delete_file", 11) == AI_ACT_NONE, "delete_file NO esta en la lista");
  chk(aiActionFromName("run", 3)          == AI_ACT_NONE, "run NO esta en la lista");
  chk(aiActionFromName("create_note", 6)  == AI_ACT_NONE, "un prefijo no cuela");
  chk(aiActionFromName(NULL, 0)           == AI_ACT_NONE, "NULL no cuela");
  chk(!aiActionNeedsConfirm(AI_ACT_SHOW_TEXT), "mostrar texto no cambia nada: sin confirmar");
  chk(aiActionNeedsConfirm(AI_ACT_CREATE_NOTE),  "crear nota confirma");
  chk(aiActionNeedsConfirm(AI_ACT_REPLACE_TEXT), "sustituir texto confirma");
  chk(aiActionNeedsConfirm(AI_ACT_OPEN_APP),     "abrir una app confirma");

  // ---- lector de respuesta ----
  AiResult r;
  { const char* ok = "{\"text\":\"Resumen listo\",\"action\":\"create_note\",\"arg\":\"/Notas/r.txt\"}";
    chk(aiParseResponse(ok, strlen(ok), &r), "parsea una respuesta buena");
    chk(r.action == AI_ACT_CREATE_NOTE, "con su accion");
    chk(r.needsConfirm,                 "que pide confirmacion");
    chk(!strcmp(r.text, "Resumen listo"), "y su texto");
    chk(!strcmp(r.arg, "/Notas/r.txt"),   "y su destino");
    chk(r.err == AI_ERR_NONE,             "sin error"); }

  // LA REGLA: una accion desconocida NO se ejecuta y NO es un error.
  { const char* evil = "{\"text\":\"ok\",\"action\":\"delete_all_files\",\"arg\":\"/\"}";
    chk(aiParseResponse(evil, strlen(evil), &r), "una accion desconocida no rompe el parseo");
    chk(r.action == AI_ACT_SHOW_TEXT, "pero se degrada a 'solo mostrar'");
    chk(!r.needsConfirm,              "y por tanto no propone nada que confirmar"); }
  { const char* noact = "{\"text\":\"solo texto\"}";
    chk(aiParseResponse(noact, strlen(noact), &r), "sin accion tambien vale");
    chk(r.action == AI_ACT_SHOW_TEXT, "y se muestra"); }

  // Una clave dentro de un TEXTO no se confunde con la clave de verdad.
  { const char* tricky =
      "{\"text\":\"cuidado con \\\"action\\\": \\\"delete_file\\\" aqui\",\"action\":\"show_text\"}";
    chk(aiParseResponse(tricky, strlen(tricky), &r), "parsea un texto con claves escapadas dentro");
    chk(r.action == AI_ACT_SHOW_TEXT, "y NO se cree la clave falsa del texto"); }
  // Una clave anidada tampoco cuenta: solo el primer nivel.
  { const char* nested = "{\"meta\":{\"action\":\"open_app\"},\"text\":\"hola\"}";
    chk(aiParseResponse(nested, strlen(nested), &r), "parsea con objeto anidado");
    chk(r.action == AI_ACT_SHOW_TEXT, "una accion anidada no cuenta"); }

  // Errores del servidor.
  { const char* e1 = "{\"error\":\"invalid token\"}";
    chk(!aiParseResponse(e1, strlen(e1), &r), "un error del servidor no es un resultado");
    chk(r.err == AI_ERR_AUTH, "y un token invalido se reconoce como tal");
    const char* e2 = "{\"error\":\"rate limited\"}";
    chk(!aiParseResponse(e2, strlen(e2), &r), "otro error tambien falla");
    chk(r.err == AI_ERR_HTTP, "y se clasifica como error del servidor"); }
  { const char* junk = "esto no es json";
    chk(!aiParseResponse(junk, strlen(junk), &r), "basura se rechaza");
    chk(r.err == AI_ERR_PARSE, "con el estado real: respuesta ilegible");
    chk(!aiParseResponse(NULL, 0, &r), "NULL se rechaza");
    chk(!aiParseResponse("", 0, &r),   "vacio se rechaza"); }

  // Escapes unicode: el tramo ASCII se traduce y el resto queda marcado.
  { const char* uni = "{\"text\":\"A\\u0042C\\u00e9D\"}";
    chk(aiParseResponse(uni, strlen(uni), &r), "parsea escapes unicode");
    chk(strstr(r.text, "ABC") != NULL, "el tramo ASCII se decodifica"); }

  // Un texto MAS LARGO que el destino se trunca DENTRO del buffer.
  { static char big[AI_RES_TEXT_MAX * 3];
    size_t o = 0;
    o += (size_t)snprintf(big + o, sizeof(big) - o, "{\"text\":\"");
    for(size_t i = 0; i < AI_RES_TEXT_MAX * 2; i++) big[o++] = 'q';
    o += (size_t)snprintf(big + o, sizeof(big) - o, "\",\"action\":\"show_text\"}");
    chk(aiParseResponse(big, o, &r), "parsea un texto enorme");
    chk(r.textLen < AI_RES_TEXT_MAX, "truncado dentro del buffer");
    chk(r.action == AI_ACT_SHOW_TEXT, "y sigue leyendo la accion DESPUES del texto largo"); }

  // TRUNCAMIENTOS: la respuesta puede cortarse en cualquier byte.
  { const char* full = "{\"text\":\"abcdef\",\"action\":\"create_note\",\"arg\":\"/x\"}";
    for(size_t k = 0; k <= strlen(full); k++) aiParseResponse(full, k, &r);
    chk(true, "ningun truncamiento de la respuesta se sale de rango"); }
  // Comilla sin cerrar y objeto sin cerrar.
  { const char* bad1 = "{\"text\":\"sin cerrar";
    const char* bad2 = "{\"text\":";
    const char* bad3 = "{{{{{{{{";
    aiParseResponse(bad1, strlen(bad1), &r);
    aiParseResponse(bad2, strlen(bad2), &r);
    aiParseResponse(bad3, strlen(bad3), &r);
    chk(true, "JSON malformado no revienta"); }

  // ---- consentimiento de Flex Vault: un item, un uso ----
  chk(!aiVaultConsentHas(7), "sin consentimiento de entrada");
  aiVaultConsentGrant(7);
  chk(aiVaultConsentHas(7),  "concedido para el item 7");
  chk(!aiVaultConsentHas(8), "no sirve para otro item");
  chk(!aiVaultConsentHas(0), "el item 0 nunca cuenta");
  chk(aiVaultConsentItem() == 7, "se sabe cual es");
  aiVaultConsentClear();
  chk(!aiVaultConsentHas(7), "al cerrar la boveda caduca");

  // ---- textos de estado: nunca dicen 'listo' si no lo esta ----
  chk(strcmp(aiErrorText(AI_ERR_NOCFG), aiErrorText(AI_ERR_NONET)) != 0,
      "'sin configurar' y 'sin conexion' son estados DISTINTOS");
  chk(aiErrorText(AI_ERR_TLS)[0] != 0,  "falta de certificado tiene su propio texto");
  chk(aiErrorText(200)[0] != 0,         "un codigo desconocido tambien tiene texto");

  // Sin red compilada, aiQuery dice la verdad en vez de fingir.
  chk(aiQuery(CW_KIND_SUMMARY, "x", 1, NULL, &r) == AI_ERR_NONET,
      "sin pila de red, aiQuery devuelve 'sin conexion'");

  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
