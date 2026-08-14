// #############################################################
// ##  FlexOS_Spell.h  ·  CORRECTOR ORTOGRAFICO LOCAL
// ##  ------------------------------------------------------
// ##  QUE ES
// ##  ------
// ##  Un corrector que corre ENTERO en la placa: diccionario
// ##  compacto en flash (PROGMEM), distancia de Damerau-
// ##  Levenshtein ACOTADA y coste de sustitucion rebajado
// ##  cuando las dos letras son vecinas en el teclado QWERTY.
// ##  Detecta lo que de verdad pasa al escribir con el dedo:
// ##
// ##    · letra de mas   ("njota"  -> "nota")
// ##    · letra de menos ("hoa"    -> "hola")
// ##    · letras cambiadas de sitio ("laterna" -> "linterna")
// ##    · tecla vecina   ("guardsr"-> "guardar")
// ##    · tildes y mayusculas ("pagina" -> "pagina" con tilde)
// ##
// ##  QUE NO ES
// ##  ---------
// ##  No es un modelo de lenguaje, no entiende el contexto y no
// ##  predice la palabra siguiente. La revision avanzada de una
// ##  frase entera la hace el servidor de Flex Intelligence, y
// ##  solo si el usuario lo pide. Aqui no hay red.
// ##
// ##  PRESUPUESTO DE TRABAJO
// ##  ----------------------
// ##  flexSpellSuggest() recibe un presupuesto en microsegundos y
// ##  CORTA cuando lo agota, devolviendo lo mejor que haya
// ##  encontrado hasta ese momento. Por eso nunca puede provocar
// ##  un tiron: en el peor caso da menos sugerencias, no un frame
// ##  perdido. El llamante (el teclado) le da ~2 ms y solo
// ##  cuando el usuario termina una palabra, no en cada letra.
// ##
// ##  COMO SE MIDE ESE PRESUPUESTO -- y esto importa: NO se lee un
// ##  reloj. El presupuesto se convierte a un numero de PRUEBAS de
// ##  diccionario con una constante calibrada (ver
// ##  FLEX_SPELL_PROBES_PER_US en el .cpp). Es deliberado:
// ##    · no hace falta micros() -> el modulo sigue siendo portable
// ##      y se prueba igual en el PC que en la placa;
// ##    · el resultado es DETERMINISTA -- la misma palabra da
// ##      siempre las mismas sugerencias, en la placa y en las
// ##      pruebas. Un corte por reloj real haria que la salida
// ##      dependiera de lo ocupada que estuviera la CPU, que es lo
// ##      ultimo que se quiere de un corrector.
// ##  La constante esta medida sobre el caso peor (palabra de 12
// ##  letras contra el diccionario entero) y con margen: si la
// ##  placa fuera mas lenta de lo medido, se gasta menos tiempo
// ##  del presupuestado, nunca mas.
// ##
// ##  PORTABLE: este modulo no incluye Arduino.h ni toca
// ##  hardware, asi que compila y se prueba en el PC igual que
// ##  en la placa (ver tests/host/test_spell.cpp).
// #############################################################

#ifndef FLEXOS_SPELL_H
#define FLEXOS_SPELL_H

#include <stdint.h>
#include <stddef.h>

// -------------------------------------------------------------
//  IDIOMAS
// -------------------------------------------------------------
#define FLEX_SPELL_ES   0
#define FLEX_SPELL_EN   1
#define FLEX_SPELL_BOTH 2

// Longitud maxima de palabra que el corrector mira. Mas larga que
// esto se da por buena sin analizar: son nombres propios, URLs o
// identificadores, y "corregirlos" solo estorba.
#define FLEX_SPELL_WORD_MAX 24

// Diccionario personal (palabras que el usuario acepto). Vive en
// RAM y se serializa a LittleFS por el llamante (ver flexSpellExport).
#define FLEX_SPELL_USER_MAX  48
#define FLEX_SPELL_USER_LEN  20

// Una sugerencia ya puntuada.
//   word  -> la palabra correcta, tal cual se escribe (con tildes)
//   dist  -> distancia de edicion respecto a lo tecleado
//   score -> 0..1000; mas alto = mas seguro. Lo usa el autocorrector.
struct FlexSpellSug {
  const char* word;
  uint8_t     dist;
  uint16_t    score;
};

// -------------------------------------------------------------
//  CICLO DE VIDA
// -------------------------------------------------------------

// Deja el corrector en su estado inicial (diccionario personal vacio).
// Es idempotente: llamarla dos veces no duplica nada.
void flexSpellBegin();

// Idioma activo: FLEX_SPELL_ES / _EN / _BOTH. Fuera de rango se ignora.
void flexSpellSetLang(int lang);
int  flexSpellLang();

// -------------------------------------------------------------
//  CONSULTA
// -------------------------------------------------------------

// true si la palabra esta en el diccionario del idioma activo, en la
// lista de terminos tecnicos o en el diccionario personal. La
// comparacion PLIEGA mayusculas y tildes, asi que "Hola", "hola" y
// "HOLA" son la misma palabra.
bool flexSpellKnown(const char* word);

// Version BARATA de la anterior: mira solo el diccionario personal y la
// lista de terminos tecnicos (unas 120 entradas), no los diccionarios
// generales. Existe para el subrayado de palabras dudosas del teclado,
// que se evalua por CADA palabra en CADA repintado del texto: ahi no se
// puede pagar un recorrido del diccionario completo. El teclado la
// combina con su propia lista, que ya tenia.
bool flexSpellKnownFast(const char* word);

// true si merece la pena analizar esta palabra. Devuelve false para
// palabras muy cortas (<3 letras), con digitos, con simbolos, en
// MAYUSCULAS completas (siglas) o mas largas que FLEX_SPELL_WORD_MAX.
// Es la guarda que evita subrayar "FlexOS" o "0x76".
bool flexSpellCheckable(const char* word);

// Hasta maxn sugerencias ordenadas de mejor a peor.
//   budgetUs = 0 -> sin limite (solo para pruebas y para el analisis
//                   de documento completo, que corre fuera del render)
// Devuelve cuantas escribio en out. Las cadenas apuntan al
// diccionario (flash) o al diccionario personal (RAM estatica): son
// validas mientras no se llame a flexSpellBegin().
int flexSpellSuggest(const char* word, FlexSpellSug* out, int maxn, uint32_t budgetUs);

// Autocorreccion: escribe en out la correccion SOLO si la confianza es
// alta (una sola candidata clara a distancia 1, palabra de 4 letras o
// mas, y la tecleada no existe en ningun diccionario). Devuelve false
// en cualquier otro caso -- que es lo normal. Nunca "arregla" una
// palabra correcta.
bool flexSpellAutoFix(const char* word, char* out, size_t n, uint32_t budgetUs);

// -------------------------------------------------------------
//  DICCIONARIO PERSONAL
// -------------------------------------------------------------

// Aprende una palabra (la que el usuario acepto). Devuelve false si no
// es aprendible o si ya estaba. Al llenarse se descarta la mas
// antigua: es una lista de 48, no un almacen sin fin.
bool flexSpellLearn(const char* word);
bool flexSpellForget(const char* word);
int  flexSpellUserCount();
const char* flexSpellUserAt(int i);

// Serializacion del diccionario personal: una palabra por linea. El
// llamante lo guarda donde quiera (en Flex OS: /System/spell.txt).
// flexSpellExport devuelve los bytes escritos (sin el 0 final).
size_t flexSpellExport(char* out, size_t n);
void   flexSpellImport(const char* blob, size_t n);

// -------------------------------------------------------------
//  UTILIDADES PUBLICAS (las usa el teclado y las pruebas)
// -------------------------------------------------------------

// Pliega una palabra a minusculas sin tildes (UTF-8 -> ASCII).
// Devuelve la longitud escrita en out.
int flexSpellFold(const char* word, char* out, size_t n);

// Distancia de Damerau-Levenshtein ACOTADA entre dos cadenas ya
// plegadas. Devuelve maxd+1 en cuanto sabe que se pasa del limite:
// esa salida temprana es lo que hace barato recorrer el diccionario.
// `near` = true aplica el coste rebajado de teclas vecinas.
int flexSpellDist(const char* a, const char* b, int maxd, bool near);

// true si las dos teclas son vecinas en el QWERTY (incluida la
// diagonal). Es lo que distingue "guardsr" (dedo a un milimetro) de
// "guardzr" (otra tecla del teclado).
bool flexSpellNeighbors(char a, char b);

// Copia `model` a `out` con la CAJA de `src`: si el usuario escribio
// "Njota", la sugerencia sale "Nota"; si escribio "NJOTA", sale
// "NOTA". Sin esto, aceptar una sugerencia a principio de frase
// destrozaria la mayuscula. Devuelve los bytes escritos.
size_t flexSpellApplyCase(const char* src, const char* model, char* out, size_t n);

#endif // FLEXOS_SPELL_H
