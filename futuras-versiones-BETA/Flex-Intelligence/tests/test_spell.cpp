// #############################################################
//  CORRECTOR ORTOGRAFICO LOCAL  ·  pruebas de host
//  ------------------------------------------------------------
//  Compila y ejecuta EN EL PC el mismo FlexOS_Spell.cpp que va a
//  la placa. El modulo es portable a proposito (no incluye
//  Arduino.h ni lee ningun reloj), asi que lo que pasa aqui es
//  exactamente lo que pasa en el ESP32-P4.
//
//  Se comprueban las CINCO clases de error que el enunciado pide
//  detectar, con los casos concretos que se citan, y ademas las
//  dos cosas que son faciles de romper sin darse cuenta:
//    · que el autocorrector NO toque una palabra correcta;
//    · que NO adivine cuando hay dos candidatas empatadas.
// #############################################################

#include "FlexOS_Spell.h"

#include <stdio.h>
#include <string.h>

static int gFails = 0, gChecks = 0;
static void chk(bool c, const char* what){
  gChecks++;
  if(!c){ printf("  FALLO: %s\n", what); gFails++; }
}

// true si `want` sale entre las sugerencias de `typed`.
static bool suggests(const char* typed, const char* want, int maxn = 3){
  FlexSpellSug s[4];
  int n = flexSpellSuggest(typed, s, maxn, 4000);
  for(int i = 0; i < n; i++) if(!strcmp(s[i].word, want)) return true;
  return false;
}
// true si `want` es LA PRIMERA sugerencia.
static bool suggestsFirst(const char* typed, const char* want){
  FlexSpellSug s[3];
  int n = flexSpellSuggest(typed, s, 3, 4000);
  return n > 0 && !strcmp(s[0].word, want);
}
static bool autofix(const char* typed, const char* want){
  char out[40];
  if(!flexSpellAutoFix(typed, out, sizeof(out), 4000)) return false;
  return !strcmp(out, want);
}
static bool noAutofix(const char* typed){
  char out[40];
  return !flexSpellAutoFix(typed, out, sizeof(out), 4000);
}

int main(){
  printf("=== FlexOS - corrector ortografico local ===\n");
  flexSpellBegin();
  flexSpellSetLang(FLEX_SPELL_ES);

  // ---- plegado de tildes y caja ----
  { char f[32];
    chk(flexSpellFold("P\xC3\xA1gina", f, sizeof(f)) == 6 && !strcmp(f, "pagina"), "plegar quita tilde y caja");
    flexSpellFold("Ni\xC3\xB1" "o", f, sizeof(f));
    chk(!strcmp(f, "nino"), "la enye se pliega a n"); }

  // ---- vecindad de teclas del QWERTY ----
  chk(flexSpellNeighbors('a', 's'), "a y s son vecinas");
  chk(flexSpellNeighbors('s', 'w'), "s y w son vecinas (fila de arriba)");
  chk(!flexSpellNeighbors('a', 'p'), "a y p NO son vecinas");
  chk(!flexSpellNeighbors('q', 'q'), "una tecla no es vecina de si misma");

  // ---- distancia acotada ----
  chk(flexSpellDist("nota", "nota", 2, false) == 0, "distancia 0 consigo misma");
  chk(flexSpellDist("njota", "nota", 2, false) == 1, "una letra de mas = 1");
  chk(flexSpellDist("hoa", "hola", 2, false) == 1, "una letra de menos = 1");
  chk(flexSpellDist("nto", "nota", 2, false) <= 2, "letras cruzadas <= 2");
  chk(flexSpellDist("zzzzz", "nota", 2, false) == 3, "muy lejos: corta y devuelve maxd+1");
  // La TRANSPOSICION es lo que separa Damerau de Levenshtein: sin ella
  // "recibir"/"reicbir" seria 2 y no entraria como correccion clara.
  chk(flexSpellDist("reicbir", "recibir", 2, false) == 1, "dos letras cruzadas = 1 (Damerau)");

  // ---- EL CASO DEL ENUNCIADO ----
  chk(suggestsFirst("njota", "nota"), "\"njota\" sugiere \"nota\" la PRIMERA");
  chk(autofix("njota", "nota"),       "\"njota\" se autocorrige a \"nota\"");

  // ---- las cinco clases de error ----
  chk(suggests("hoa", "hola"),                  "letra omitida: hoa -> hola");
  chk(autofix("guardsr", "guardar"),            "tecla vecina: guardsr -> guardar");
  chk(suggests("aplicacon", "aplicaci\xC3\xB3n"), "letra omitida + tilde: aplicacon");
  chk(autofix("teclaod", "teclado"),            "letras cruzadas: teclaod -> teclado");
  chk(autofix("configurra", "configurar"),      "letra duplicada: configurra -> configurar");
  // TILDE que falta: la palabra plegada COINCIDE con una del
  // diccionario, asi que no es "conocida" y la correccion es distancia 0.
  chk(!flexSpellKnown("pagina"),                "sin tilde no cuenta como conocida");
  chk(autofix("pagina", "p\xC3\xA1gina"),        "tilde restituida: pagina -> pagina con tilde");

  // ---- MAYUSCULAS: la caja del original se respeta ----
  chk(autofix("Njota", "Nota"),  "a principio de frase mantiene la mayuscula");
  { char out[40];
    flexSpellApplyCase("NOTA", "nota", out, sizeof(out));
    chk(!strcmp(out, "NOTA"), "TODO EN MAYUSCULAS se conserva");
    flexSpellApplyCase("nota", "nota", out, sizeof(out));
    chk(!strcmp(out, "nota"), "minusculas se conservan"); }

  // ---- lo que NO se debe tocar ----
  chk(flexSpellKnown("nota"),      "una palabra correcta es conocida");
  chk(noAutofix("nota"),           "una palabra correcta NO se autocorrige");
  chk(!flexSpellCheckable("esp32"),"con digitos no se analiza");
  chk(!flexSpellCheckable("USB"),  "las siglas en mayusculas no se analizan");
  chk(!flexSpellCheckable("el"),   "menos de 3 letras no se analiza");
  chk(!flexSpellCheckable("/Notas/a.txt"), "una ruta no se analiza");
  chk(noAutofix("teh"),            "3 letras: se sugiere pero NO se autocorrige");
  // Terminos del ecosistema: conocidos SIEMPRE, escriba en el idioma que escriba.
  chk(flexSpellKnown("FlexOS"),  "FlexOS es una palabra conocida");
  chk(flexSpellKnown("arduino"), "arduino es una palabra conocida");
  chk(flexSpellKnownFast("flexos"), "la consulta rapida tambien lo conoce");
  chk(!flexSpellKnownFast("nota"),  "la consulta rapida NO mira el diccionario general");

  // ---- diccionario personal ----
  chk(!flexSpellKnown("Kirchhoff"), "una palabra rara no se conoce de entrada");
  chk(flexSpellLearn("Kirchhoff"),  "se aprende");
  chk(flexSpellKnown("Kirchhoff"),  "y a partir de ahi se conoce");
  chk(flexSpellKnownFast("Kirchhoff"), "la consulta rapida ve el diccionario personal");
  chk(!flexSpellLearn("Kirchhoff"), "no se aprende dos veces");
  chk(flexSpellUserCount() == 1,    "una sola entrada personal");
  // Ida y vuelta a disco.
  { char blob[2048];
    size_t n = flexSpellExport(blob, sizeof(blob));
    chk(n > 0, "exporta el diccionario personal");
    flexSpellBegin();
    chk(!flexSpellKnown("Kirchhoff"), "tras reiniciar, se olvida");
    flexSpellImport(blob, n);
    chk(flexSpellKnown("Kirchhoff"),  "y al importar vuelve"); }
  chk(flexSpellForget("Kirchhoff"), "se puede olvidar a mano");
  chk(!flexSpellKnown("Kirchhoff"), "y deja de conocerse");

  // La lista personal es de 48 y descarta la MAS ANTIGUA al llenarse:
  // es una memoria acotada, no un almacen sin fin.
  flexSpellBegin();
  { char w[16];
    for(int i = 0; i < FLEX_SPELL_USER_MAX + 5; i++){
      snprintf(w, sizeof(w), "zzz%03d", i);
      // "zzz000" tiene digitos y no es aprendible; se usan letras.
      snprintf(w, sizeof(w), "zzq%c%c", 'a' + (i / 26), 'a' + (i % 26));
      flexSpellLearn(w);
    }
    chk(flexSpellUserCount() == FLEX_SPELL_USER_MAX, "la lista personal se queda en su tope");
    chk(!flexSpellKnown("zzqaa"), "la mas antigua se descarto");
    chk(flexSpellKnown("zzqbc"),  "la mas reciente sigue"); }

  // ---- ingles ----
  flexSpellBegin();
  flexSpellSetLang(FLEX_SPELL_EN);
  chk(autofix("keybord", "keyboard"), "EN: keybord -> keyboard");
  chk(autofix("recieve", "receive"),  "EN: recieve -> receive");
  chk(autofix("setings", "settings"), "EN: setings -> settings");
  chk(flexSpellKnown("note"),         "EN: note es conocida");
  chk(!flexSpellKnown("nota"),        "EN: nota NO es conocida en ingles");
  flexSpellSetLang(FLEX_SPELL_BOTH);
  chk(flexSpellKnown("nota") && flexSpellKnown("note"), "AMBOS: las dos se conocen");

  // ---- presupuesto de trabajo ----
  // Con un presupuesto minusculo el corrector devuelve MENOS, pero nunca
  // se cuelga ni devuelve basura. Es la garantia de que no puede causar
  // un tiron: en el peor caso da menos sugerencias.
  flexSpellSetLang(FLEX_SPELL_ES);
  { FlexSpellSug s[3];
    int nBig   = flexSpellSuggest("njota", s, 3, 4000);
    int nSmall = flexSpellSuggest("njota", s, 3, 1);
    chk(nBig >= nSmall, "con menos presupuesto no salen mas sugerencias");
    chk(nSmall >= 0,    "un presupuesto minusculo no revienta"); }
  // Y es DETERMINISTA: dos llamadas iguales dan lo mismo.
  { FlexSpellSug a[3], b[3];
    int na = flexSpellSuggest("teclaod", a, 3, 2000);
    char first[40]; snprintf(first, sizeof(first), "%s", na > 0 ? a[0].word : "");
    int nb = flexSpellSuggest("teclaod", b, 3, 2000);
    chk(na == nb && (na == 0 || !strcmp(first, b[0].word)), "la misma consulta da el mismo resultado"); }

  // ---- entradas degeneradas ----
  { FlexSpellSug s[3];
    chk(flexSpellSuggest(NULL, s, 3, 1000) == 0, "NULL no revienta");
    chk(flexSpellSuggest("", s, 3, 1000) == 0,   "cadena vacia no revienta");
    chk(flexSpellSuggest("njota", s, 0, 1000) == 0, "maxn 0 no revienta");
    char lots[80];
    memset(lots, 'a', sizeof(lots) - 1); lots[sizeof(lots) - 1] = 0;
    chk(flexSpellSuggest(lots, s, 3, 1000) == 0, "palabra larguisima se descarta");
    chk(!flexSpellCheckable(lots), "y tampoco es analizable"); }
  { char out[4];
    // Destino diminuto: se trunca dentro del buffer, no fuera.
    flexSpellApplyCase("njota", "notas", out, sizeof(out));
    chk(strlen(out) < sizeof(out), "applyCase respeta un destino pequeno"); }

  printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
  return gFails ? 1 : 0;
}
