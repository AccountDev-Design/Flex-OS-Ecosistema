// #############################################################
// ##  FlexOS_Spell.cpp  ·  CORRECTOR ORTOGRAFICO LOCAL
// ##  Ver FlexOS_Spell.h para el contrato y el porque de cada
// ##  decision. Aqui solo esta el como.
// #############################################################

#include "FlexOS_Spell.h"

#include <string.h>

// PROGMEM solo existe en el core de Arduino. En el PC (pruebas de
// host) el modulo se compila igual, con la macro vacia: en el ESP32
// el acceso a flash es transparente para lectura de bytes, asi que el
// MISMO codigo vale en los dos sitios sin pgm_read_byte.
#ifdef ARDUINO
  #include <pgmspace.h>
#else
  #ifndef PROGMEM
    #define PROGMEM
  #endif
#endif

// =============================================================
//  1) DICCIONARIOS
//  -------------------------------------------------------------
//  Un solo blob por idioma, palabras separadas por '\n'. Se guarda
//  asi -- y no como array de punteros -- porque un `const char*` son
//  4 bytes de tabla POR PALABRA: con ~1300 entradas eso serian 5,2 KB
//  de flash gastados solo en punteros, mas de lo que ocupa medio
//  diccionario. Recorrer un blob es igual de rapido (memoria lineal,
//  amiga de la cache) y cuesta cero bytes de indice.
//
//  Estan en MINUSCULAS y CON SUS TILDES: la forma con tilde es la
//  correcta y es la que el corrector propone. La comparacion pliega,
//  asi que escribir sin tilde encuentra la palabra igual (y entonces
//  la sugerencia es justamente ponerle la tilde).
// =============================================================

static const char SP_ES[] PROGMEM =
  "abajo\nabierto\nabrir\nacabar\naceptar\nacento\nacceso\nacci\xC3\xB3n\naceptado\nacordar\n"
  "actual\nadelante\nadem\xC3\xA1s\nadentro\nagua\nahora\najuste\najustes\nalgo\nalguien\n"
  "alguno\nalli\nalmacenamiento\nalto\namarillo\namigo\namor\nancho\nandar\nantes\n"
  "a\xC3\xB1o\napagar\napenas\naplicaci\xC3\xB3n\naplicar\naprender\naqui\narchivo\narreglar\narriba\n"
  "asunto\natr\xC3\xA1s\naudio\naumentar\nautom\xC3\xA1tico\navanzar\naviso\nayer\nayuda\nayudar\n"
  "azul\nbajar\nbajo\nbarra\nbastante\nbater\xC3\xAD" "a\nbien\nblanco\nbloquear\nbonito\n"
  "borrar\nbot\xC3\xB3n\nbrillo\nbuscador\nbuscar\nbueno\ncaber\ncada\ncaja\ncalcular\n"
  "calculadora\ncalendario\ncalle\ncalor\ncamara\ncambiar\ncambio\ncamino\ncampo\ncancelar\n"
  "cansado\ncantidad\ncapacidad\ncar\xC3\xA1" "cter\ncarga\ncargar\ncarpeta\ncasa\ncaso\ncausa\n"
  "cerca\ncerrar\ncielo\ncien\ncierto\ncifra\ncinco\ncirculo\nciudad\nclaro\n"
  "clave\ncodigo\ncolor\ncomenzar\ncomer\ncomida\ncomo\ncompartir\ncompleto\ncomprar\n"
  "comprobar\ncomputadora\ncomun\ncon\nconectar\nconexi\xC3\xB3n\nconfiar\nconfigurar\nconocer\nconseguir\n"
  "consulta\ncontacto\ncontar\ncontenido\ncontinuar\ncontra\ncontrol\ncopia\ncopiar\ncorreo\n"
  "correcto\ncorregir\ncorrer\ncortar\ncosa\ncrear\ncreer\ncuadro\ncual\ncuando\n"
  "cuanto\ncuarto\ncuenta\ncuerpo\ncuidado\ncumplir\ndar\ndatos\ndeber\ndecidir\n"
  "decir\ndedo\ndejar\ndelante\ndemasiado\ndentro\ndepende\nderecha\ndesde\ndesactivar\n"
  "descargar\ndescanso\ndescubrir\ndesde\ndesear\ndespu\xC3\xA9s\ndetalle\ndetener\nd\xC3\xAD" "a\ndibujar\n"
  "dibujo\ndiferente\ndificil\ndigital\ndinero\ndirecci\xC3\xB3n\ndisco\ndise\xC3\xB1o\ndispositivo\ndistancia\n"
  "documento\nd\xC3\xB3nde\ndormir\ndos\nduda\ndurante\nduro\neditar\neducaci\xC3\xB3n\nefecto\n"
  "ejemplo\nel\nella\nellos\nempezar\nempujar\nen\nencender\nencima\nencontrar\n"
  "energ\xC3\xAD" "a\nenlace\nentender\nentonces\nentrada\nentrar\nentre\nenviar\nequipo\nerror\n"
  "escala\nescribir\nescritorio\nescuchar\nespacio\nespa\xC3\xB1ol\nesperar\nestado\nestar\neste\n"
  "estilo\nestudiar\nevitar\nexacto\nexisten\nexplicar\nextra\nf\xC3\xA1" "cil\nfalta\nfaltar\n"
  "familia\nfavor\nfavorito\nfecha\nfin\nfinal\nfirma\nfoco\nfondo\nforma\n"
  "formato\nfoto\nfrase\nfrio\nfuente\nfuera\nfuerte\nfuncionar\nfunci\xC3\xB3n\nfuturo\n"
  "gente\nglobal\ngracias\ngrande\ngrupo\nguardar\ngustar\nhaber\nhablar\nhacer\n"
  "hacia\nhasta\nhecho\nhistorial\nhola\nhombre\nhora\nhoy\nhueco\nidea\n"
  "idioma\nigual\nimagen\nimportante\nimprimir\nincluir\ninformaci\xC3\xB3n\ninicio\ninstalar\nintentar\n"
  "inter\xC3\xA9s\ninternet\nintroducir\nir\nizquierda\njuego\njugar\njunto\nlado\nlargo\n"
  "leer\nlejos\nlengua\nlento\nletra\nlibre\nlibro\nlimpiar\nl\xC3\xADnea\nlinterna\n"
  "lista\nllamar\nllave\nllegar\nllenar\nllevar\nlocal\nlograr\nluego\nlugar\n"
  "luz\nmadre\nmal\nmandar\nmanera\nma\xC3\xB1" "ana\nmano\nmantener\nm\xC3\xA1quina\nmarcar\n"
  "m\xC3\xA1s\nmateria\nmayor\nmedida\nmedio\nmejor\nmemoria\nmenos\nmensaje\nmenu\n"
  "mes\nmeter\nmiedo\nmientras\nminuto\nmirar\nmismo\nmitad\nmodo\nmomento\n"
  "mostrar\nmover\nmucho\nmujer\nmundo\nm\xC3\xBAsica\nmuy\nnada\nnadie\nnecesitar\n"
  "negro\nni\xC3\xB1" "o\nnivel\nnoche\nnombre\nnormal\nnorte\nnosotros\nnota\nnotas\n"
  "noticia\nnoticias\nnuevo\nn\xC3\xBAmero\nnunca\nobjeto\nocupar\nocurrir\noferta\noficina\n"
  "ofrecer\no\xC3\xADr\nolvidar\nopci\xC3\xB3n\nordenar\norden\noscuro\notro\npadre\np\xC3\xA1gina\n"
  "pagar\npalabra\npantalla\npapel\npara\nparecer\npared\nparte\npasar\npaso\n"
  "pausa\npedir\npeligro\npensar\npeque\xC3\xB1o\nperder\nperfecto\nperiodo\npermiso\npermitir\n"
  "pero\npersona\npesar\npeso\npieza\npintar\nplan\nplaza\npoco\npoder\n"
  "poner\nporque\nposible\npr\xC3\xA1" "ctica\nprecio\npreferir\npregunta\npreparar\npresentar\nprimero\n"
  "principal\nprobar\nproblema\nproceso\nproducto\nprograma\npronto\nproponer\npropio\nproteger\n"
  "prueba\npueblo\npuerta\npunto\nquedar\nquerer\nqui\xC3\xA9n\nquitar\nr\xC3\xA1pido\nraz\xC3\xB3n\n"
  "real\nrecibir\nreciente\nrecordar\nreducir\nregistro\nregresar\nreiniciar\nrelacion\nreloj\n"
  "repetir\nresolver\nrespuesta\nresultado\nresumen\nresumir\nretirar\nreunir\nrevisar\nr\xC3\xADo\n"
  "rojo\nropa\nruido\nsaber\nsacar\nsalida\nsalir\nsalud\nsecci\xC3\xB3n\nseguir\n"
  "segundo\nseguro\nselecci\xC3\xB3n\nseleccionar\nsemana\nsentir\nse\xC3\xB1" "al\nse\xC3\xB1" "or\nsentido\nser\n"
  "servicio\nservidor\nsiempre\nsiguiente\nsilencio\nsimple\nsin\nsistema\nsitio\nsobre\n"
  "sol\nsolo\nsonido\nsubir\nsuficiente\nsuma\nsuponer\ntabla\ntal\ntama\xC3\xB1o\n"
  "tambi\xC3\xA9n\ntampoco\ntan\ntarde\ntarea\ntareas\ntarjeta\nteclado\ntecla\nt\xC3\xA9" "cnico\n"
  "tel\xC3\xA9" "fono\ntema\ntemprano\ntener\nterminar\ntexto\ntiempo\ntienda\ntierra\ntipo\n"
  "tocar\ntodo\ntomar\ntono\ntrabajar\ntrabajo\ntraducir\ntraducci\xC3\xB3n\ntraer\ntranquilo\n"
  "tratar\ntres\ntriste\n\xC3\xBAltimo\n\xC3\xBAnico\nunir\nusar\nuso\nusuario\nutilizar\n"
  "valor\nvarios\nvelocidad\nvender\nvenir\nventana\nver\nverdad\nverde\nvez\n"
  "viaje\nvida\nviejo\nviento\nvista\nvivir\nvolver\nvoz\nvuelta\nya\n"
  "zona\n";

static const char SP_EN[] PROGMEM =
  "able\nabout\nabove\naccept\naccess\naccount\nacross\nact\nadd\nadjust\n"
  "after\nagain\nagainst\nago\nagree\nahead\nall\nallow\nalmost\nalone\n"
  "along\nalready\nalso\nalways\namong\namount\nanalyze\nand\nanother\nanswer\n"
  "any\nanyone\nappear\napply\narea\naround\narrive\nask\nassume\nattach\n"
  "audio\navailable\navoid\naway\nback\nbattery\nbecause\nbecome\nbefore\nbegin\n"
  "behind\nbelieve\nbelow\nbeside\nbest\nbetter\nbetween\nbeyond\nbig\nblack\n"
  "block\nblue\nboard\nbook\nboth\nbottom\nbreak\nbring\nbrowser\nbuild\n"
  "button\nbuy\ncall\ncamera\ncan\ncancel\ncannot\ncard\ncare\ncarry\n"
  "case\ncatch\ncause\ncenter\nchange\ncharge\ncheck\nchoose\nclean\nclear\n"
  "click\nclock\nclose\ncode\ncold\ncollect\ncolor\ncome\ncommand\ncommon\n"
  "company\ncompare\ncomplete\ncomputer\nconfirm\nconnect\nconsider\ncontact\ncontain\ncontent\n"
  "continue\ncontrol\ncopy\ncorrect\ncost\ncould\ncount\ncountry\ncover\ncreate\n"
  "current\ncustom\ncut\ndark\ndata\ndate\nday\ndecide\ndefault\ndelete\n"
  "deliver\ndepend\ndescribe\ndesign\ndetail\ndetect\ndevice\ndifferent\ndifficult\ndigital\n"
  "direct\ndisable\ndisplay\ndocument\ndoes\ndone\ndoor\ndouble\ndown\ndownload\n"
  "draw\ndrive\ndrop\nduring\neach\nearly\neasy\nedit\neffect\neither\n"
  "email\nempty\nenable\nend\nenergy\nengine\nenough\nenter\nentire\nerror\n"
  "even\nevent\never\nevery\nexact\nexample\nexcept\nexist\nexit\nexpect\n"
  "explain\nexport\nextra\nface\nfact\nfail\nfamily\nfar\nfast\nfeature\n"
  "feel\nfew\nfield\nfile\nfill\nfilter\nfind\nfine\nfinish\nfirst\n"
  "fit\nfix\nfolder\nfollow\nfont\nfood\nforce\nform\nformat\nforward\n"
  "free\nfriend\nfrom\nfull\nfunction\ngame\ngather\ngeneral\nget\ngive\n"
  "global\ngood\ngreat\ngreen\ngroup\ngrow\nguide\nhalf\nhand\nhandle\n"
  "happen\nhappy\nhard\nhave\nhead\nhear\nheart\nheavy\nhelp\nhere\n"
  "hide\nhigh\nhold\nhome\nhope\nhour\nhouse\nhowever\nhuman\nicon\n"
  "idea\nimage\nimport\nimportant\ninclude\nincrease\nindex\ninput\ninsert\ninside\n"
  "install\ninstead\nintend\ninterface\ninternet\ninto\nissue\nitem\njoin\njump\n"
  "just\nkeep\nkey\nkeyboard\nkind\nknow\nlanguage\nlarge\nlast\nlate\n"
  "later\nlearn\nleast\nleave\nleft\nlength\nless\nlet\nletter\nlevel\n"
  "library\nlife\nlight\nlike\nlimit\nline\nlink\nlist\nlisten\nlittle\n"
  "live\nload\nlocal\nlock\nlong\nlook\nloop\nlose\nlove\nlow\n"
  "machine\nmain\nmake\nmanage\nmany\nmark\nmatch\nmatter\nmaybe\nmean\n"
  "measure\nmemory\nmenu\nmessage\nmethod\nmiddle\nmight\nmind\nminute\nmiss\n"
  "mode\nmodel\nmodule\nmoment\nmoney\nmonth\nmore\nmorning\nmost\nmove\n"
  "much\nmusic\nmust\nname\nnear\nneed\nnetwork\nnever\nnew\nnews\n"
  "next\nnice\nnight\nnone\nnormal\nnote\nnothing\nnotice\nnow\nnumber\n"
  "object\noffer\noffice\noften\nokay\nold\nonce\nonly\nopen\noption\n"
  "order\nother\noutput\nover\nown\npage\npaper\nparent\npart\npass\n"
  "password\npast\npattern\npause\npeople\nperfect\nperform\nperhaps\nperiod\nperson\n"
  "phone\nphoto\npick\npicture\npiece\nplace\nplan\nplay\nplease\npoint\n"
  "policy\npower\npractice\nprepare\npresent\npress\nprevent\nprevious\nprint\nprivate\n"
  "problem\nprocess\nproduct\nprogram\nprogress\nproject\nproper\nprotect\nprovide\npublic\n"
  "pull\npush\nput\nquality\nquestion\nquick\nquiet\nquit\nradio\nraise\n"
  "range\nrate\nread\nready\nreal\nreason\nreceive\nrecent\nrecord\nreduce\n"
  "refer\nrefresh\nregion\nreject\nrelease\nremain\nremember\nremove\nrepeat\nreplace\n"
  "reply\nreport\nrequest\nrequire\nreset\nresolve\nresource\nrespond\nresult\nreturn\n"
  "review\nright\nroom\nround\nrule\nrun\nsafe\nsame\nsample\nsave\n"
  "scale\nscreen\nsearch\nsecond\nsection\nsecure\nsee\nseem\nselect\nsend\n"
  "sense\nserver\nservice\nsession\nset\nsetting\nsettings\nseveral\nshare\nshort\n"
  "should\nshow\nside\nsign\nsignal\nsimple\nsince\nsingle\nsize\nsleep\n"
  "slow\nsmall\nsome\nsoon\nsort\nsound\nsource\nspace\nspeak\nspecial\n"
  "speed\nspend\nstage\nstandard\nstart\nstate\nstatus\nstay\nstep\nstill\n"
  "stop\nstorage\nstore\nstory\nstream\nstrong\nstudy\nstyle\nsubject\nsuccess\n"
  "such\nsuggest\nsupport\nsure\nsurface\nswitch\nsystem\ntable\ntake\ntalk\n"
  "target\ntask\nteam\ntell\ntemplate\nterm\ntest\ntext\nthan\nthank\n"
  "that\ntheir\nthem\nthen\nthere\nthese\nthey\nthing\nthink\nthis\n"
  "those\nthough\nthread\nthrough\nthrow\ntime\ntitle\ntoday\ntogether\ntoken\n"
  "tone\ntool\ntop\ntotal\ntouch\ntoward\ntranslate\ntry\nturn\ntype\n"
  "under\nunderstand\nunit\nunless\nuntil\nupdate\nupload\nupon\nusage\nuse\n"
  "user\nusual\nvalue\nversion\nvery\nview\nvisit\nvoice\nwait\nwalk\n"
  "want\nwarn\nwatch\nwater\nway\nweek\nweight\nwelcome\nwell\nwhat\n"
  "when\nwhere\nwhether\nwhich\nwhile\nwhite\nwhole\nwhy\nwide\nwill\n"
  "window\nwithin\nwithout\nword\nwork\nworld\nwould\nwrite\nwrong\nyear\n"
  "yellow\nyes\nyour\nzone\n";

// Terminos del propio ecosistema y del mundo del hardware. Se buscan
// SIEMPRE, sea cual sea el idioma: "FlexOS" no deja de ser correcto
// porque el usuario escriba en ingles. Aqui es donde entran los
// nombres que un diccionario general marcaria en rojo.
static const char SP_TECH[] PROGMEM =
  "flexos\nflex\nesp32\narduino\nlittlefs\npsram\nsram\nnvs\nwifi\nbluetooth\n"
  "usb\nhttps\nhttp\njson\nhtml\ncss\nurl\napi\ntoken\nfirmware\n"
  "hardware\nsoftware\nkernel\ndriver\nbuffer\nframebuffer\npixel\nsensor\nservo\ngpio\n"
  "i2c\nspi\nuart\npwm\nadc\ndac\nrgb\nlcd\noled\ndsi\n"
  "mipi\nrtos\nfreertos\ncowork\nvault\nota\nsdk\nide\ncpu\nram\n"
  "byte\nbit\nhex\nlog\ndebug\nbuild\ncommit\nrepo\nscript\nshell\n"
  "python\njavascript\ntypescript\nsketch\nlibrer\xC3\xAD" "a\ncompilar\ndepurar\npin\nplaca\nchip\n";

// =============================================================
//  2) CALIBRACION DEL PRESUPUESTO
//  -------------------------------------------------------------
//  Una "prueba" es evaluar UNA palabra del diccionario contra la
//  tecleada: el filtro barato de longitud y, si lo pasa, la
//  distancia acotada. Medido en el peor caso (palabra de 12 letras,
//  todas las candidatas pasando el filtro) sale en torno a 1,2 us
//  por prueba en el P4 a 360 MHz. Se redondea a 1 prueba/us -- a la
//  BAJA en pruebas por microsegundo, o sea conservador: si la placa
//  fuera mas lenta se gasta menos tiempo del pedido, nunca mas.
// =============================================================
#define FLEX_SPELL_PROBES_PER_US 1

// Techo duro. Ni con un presupuesto enorme se recorre mas que esto:
// es la red de seguridad contra un llamante que pase un numero
// disparatado.
#define FLEX_SPELL_PROBES_MAX 60000

// Distancia maxima que se considera una correccion. Mas alla de 2 ya
// no es "se me fue el dedo", es otra palabra.
#define FLEX_SPELL_MAXD 2

// =============================================================
//  3) ESTADO
// =============================================================
static int  gSpLang = FLEX_SPELL_ES;
static char gSpUser[FLEX_SPELL_USER_MAX][FLEX_SPELL_USER_LEN];
static int  gSpUserN = 0;

void flexSpellBegin(){
  gSpUserN = 0;
  for(int i = 0; i < FLEX_SPELL_USER_MAX; i++) gSpUser[i][0] = 0;
}
void flexSpellSetLang(int lang){
  if(lang == FLEX_SPELL_ES || lang == FLEX_SPELL_EN || lang == FLEX_SPELL_BOTH) gSpLang = lang;
}
int flexSpellLang(){ return gSpLang; }

// =============================================================
//  4) PLEGADO UTF-8 -> ASCII MINUSCULA
//  -------------------------------------------------------------
//  Mismo criterio que kbFoldCh() del teclado: las vocales acentuadas
//  y la enye se pliegan a su letra base. Asi "p\xC3\xA1gina" y "pagina" son
//  la misma palabra para buscar, y la diferencia entre las dos pasa a
//  ser justo lo que el corrector propone arreglar.
// =============================================================
static char spFoldCh(const char** ps){
  const char* s = *ps;
  unsigned char c = (unsigned char)s[0];
  if(c == 0) return 0;
  if(c < 0x80){ *ps = s + 1; return (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); }
  if(c == 0xC3 && s[1]){
    unsigned char d = (unsigned char)s[1];
    *ps = s + 2;
    if((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) return 'a';
    if((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) return 'e';
    if((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) return 'i';
    if((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) return 'o';
    if((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) return 'u';
    if(d == 0x87 || d == 0xA7) return 'c';                    // C/c cedilla
    if(d == 0x91 || d == 0xB1) return 'n';                    // N/n con virgulilla
    return '?';
  }
  s++;
  while(((unsigned char)*s & 0xC0) == 0x80) s++;              // otro multibyte: se salta entero
  *ps = s;
  return '?';
}

int flexSpellFold(const char* word, char* out, size_t n){
  if(!out || n == 0) return 0;
  out[0] = 0;
  if(!word) return 0;
  size_t o = 0;
  const char* p = word;
  while(*p && o + 1 < n){
    char c = spFoldCh(&p);
    if(c == 0) break;
    out[o++] = c;
  }
  out[o] = 0;
  return (int)o;
}

// Igual que flexSpellFold pero sobre una palabra del blob, que
// termina en '\n' o en 0 en vez de solo en 0.
static int spFoldEntry(const char* entry, char* out, size_t n){
  if(!out || n == 0) return 0;
  size_t o = 0;
  const char* p = entry;
  while(*p && *p != '\n' && o + 1 < n){
    char c = spFoldCh(&p);
    if(c == 0) break;
    out[o++] = c;
  }
  out[o] = 0;
  return (int)o;
}

// Longitud en bytes de la entrada del blob que empieza en `entry`.
static size_t spEntryLen(const char* entry){
  const char* p = entry;
  while(*p && *p != '\n') p++;
  return (size_t)(p - entry);
}

// =============================================================
//  5) VECINDAD DE TECLAS
//  -------------------------------------------------------------
//  El teclado de Flex OS es QWERTY con las filas desplazadas como en
//  un teclado fisico. Dos teclas son vecinas si estan pegadas en la
//  misma fila o si en filas contiguas sus columnas no se separan mas
//  de una posicion, contando el desplazamiento real de cada fila.
//  Esto es lo unico que distingue "guardsr" (la 'a' esta pegada a la
//  's': casi seguro un dedo desviado) de "guardxr" (no lo esta).
// =============================================================
static const char* const SP_ROWS[3] = {
  "qwertyuiop",
  "asdfghjkl\xC3\xB1",     // la enye no se busca aqui: el plegado ya la paso a 'n'
  "zxcvbnm"
};
// Desplazamiento horizontal de cada fila, en "medias teclas". Es lo
// que hace que la 's' (fila 1, col 1) sea vecina de la 'w' (fila 0,
// col 1) y de la 'e' (fila 0, col 2), pero no de la 'r'.
static const int SP_ROW_OFF[3] = { 0, 1, 2 };

static bool spKeyPos(char c, int &row, int &col){
  for(int r = 0; r < 3; r++){
    const char* s = SP_ROWS[r];
    for(int i = 0; s[i]; i++){
      if((unsigned char)s[i] >= 0x80) break;     // fin de la parte ASCII de la fila
      if(s[i] == c){ row = r; col = i; return true; }
    }
  }
  return false;
}

bool flexSpellNeighbors(char a, char b){
  if(a == b) return false;
  int ra, ca, rb, cb;
  if(!spKeyPos(a, ra, ca) || !spKeyPos(b, rb, cb)) return false;
  int dr = ra - rb; if(dr < 0) dr = -dr;
  if(dr > 1) return false;
  // Columnas comparadas en la misma escala (medias teclas), asi el
  // desplazamiento entre filas cuenta de verdad.
  int xa = ca * 2 + SP_ROW_OFF[ra];
  int xb = cb * 2 + SP_ROW_OFF[rb];
  int dx = xa - xb; if(dx < 0) dx = -dx;
  return (dr == 0) ? (dx <= 2) : (dx <= 2);
}

// =============================================================
//  6) DISTANCIA DE DAMERAU-LEVENSHTEIN ACOTADA
//  -------------------------------------------------------------
//  Tres filas (la anterior a la anterior hace falta para la
//  TRANSPOSICION: es lo que convierte "laterna"/"linterna" o
//  "recibir"/"reicbir" en distancia 1 en vez de 2).
//
//  El acotado no es un adorno: en cuanto el minimo de la fila supera
//  maxd, la respuesta ya no puede bajar de ahi, asi que se corta.
//  Eso es lo que permite recorrer ~1300 palabras en milisegundos.
//
//  Los costes van en DECIMAS para poder cobrar 0,5 por una
//  sustitucion entre teclas vecinas sin usar coma flotante. maxd se
//  escala igual al entrar.
// =============================================================
#define SP_COST_FULL  10
#define SP_COST_NEAR   5

int flexSpellDist(const char* a, const char* b, int maxd, bool near){
  if(!a || !b) return maxd + 1;
  int la = (int)strlen(a), lb = (int)strlen(b);
  if(la > FLEX_SPELL_WORD_MAX || lb > FLEX_SPELL_WORD_MAX) return maxd + 1;
  int diff = la - lb; if(diff < 0) diff = -diff;
  if(diff > maxd) return maxd + 1;                       // ni borrando todo se llega
  if(la == 0) return lb <= maxd ? lb : maxd + 1;
  if(lb == 0) return la <= maxd ? la : maxd + 1;

  const int lim = maxd * SP_COST_FULL;
  // +1 columna por el prefijo vacio; +2 de holgura para no rozar el borde.
  int prev2[FLEX_SPELL_WORD_MAX + 2];
  int prev1[FLEX_SPELL_WORD_MAX + 2];
  int cur  [FLEX_SPELL_WORD_MAX + 2];

  for(int j = 0; j <= lb; j++) prev1[j] = j * SP_COST_FULL;
  for(int j = 0; j <= lb + 1; j++) prev2[j] = lim + SP_COST_FULL;   // fila -1: inalcanzable

  for(int i = 1; i <= la; i++){
    cur[0] = i * SP_COST_FULL;
    int rowMin = cur[0];
    for(int j = 1; j <= lb; j++){
      int sub;
      if(a[i - 1] == b[j - 1]) sub = 0;
      else if(near && flexSpellNeighbors(a[i - 1], b[j - 1])) sub = SP_COST_NEAR;
      else sub = SP_COST_FULL;

      int v = prev1[j - 1] + sub;                        // sustituir / coincidir
      int d = prev1[j] + SP_COST_FULL;                   // borrar de a
      int ins = cur[j - 1] + SP_COST_FULL;               // insertar en a
      if(d < v) v = d;
      if(ins < v) v = ins;
      // TRANSPOSICION (Damerau): las dos letras cruzadas.
      if(i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]){
        int tr = prev2[j - 2] + SP_COST_FULL;
        if(tr < v) v = tr;
      }
      cur[j] = v;
      if(v < rowMin) rowMin = v;
    }
    if(rowMin > lim) return maxd + 1;                    // corte: ya no baja de aqui
    for(int j = 0; j <= lb; j++){ prev2[j] = prev1[j]; prev1[j] = cur[j]; }
  }
  int r = prev1[lb];
  if(r > lim) return maxd + 1;
  // Vuelta a unidades enteras redondeando hacia ARRIBA: media
  // sustitucion vecina sigue siendo "una edicion" a la hora de
  // decidir si algo es corregible, pero puntua mejor (ver spScore).
  return (r + SP_COST_FULL - 1) / SP_COST_FULL;
}

// La misma distancia, pero devolviendo el coste en decimas: es lo que
// permite que "guardsr"->"guardar" (vecina, 5) puntue mejor que
// "guardor"->"guardar" si esta no lo fuera. Se usa solo para ordenar.
static int spDistTenths(const char* a, const char* b, int maxd){
  if(!a || !b) return maxd * SP_COST_FULL + 1;
  int la = (int)strlen(a), lb = (int)strlen(b);
  if(la > FLEX_SPELL_WORD_MAX || lb > FLEX_SPELL_WORD_MAX) return maxd * SP_COST_FULL + 1;
  int diff = la - lb; if(diff < 0) diff = -diff;
  if(diff > maxd) return maxd * SP_COST_FULL + 1;
  if(la == 0) return lb * SP_COST_FULL;
  if(lb == 0) return la * SP_COST_FULL;

  const int lim = maxd * SP_COST_FULL;
  int prev2[FLEX_SPELL_WORD_MAX + 2];
  int prev1[FLEX_SPELL_WORD_MAX + 2];
  int cur  [FLEX_SPELL_WORD_MAX + 2];
  for(int j = 0; j <= lb; j++) prev1[j] = j * SP_COST_FULL;
  for(int j = 0; j <= lb + 1; j++) prev2[j] = lim + SP_COST_FULL;

  for(int i = 1; i <= la; i++){
    cur[0] = i * SP_COST_FULL;
    int rowMin = cur[0];
    for(int j = 1; j <= lb; j++){
      int sub;
      if(a[i - 1] == b[j - 1]) sub = 0;
      else if(flexSpellNeighbors(a[i - 1], b[j - 1])) sub = SP_COST_NEAR;
      else sub = SP_COST_FULL;
      int v = prev1[j - 1] + sub;
      int d = prev1[j] + SP_COST_FULL;
      int ins = cur[j - 1] + SP_COST_FULL;
      if(d < v) v = d;
      if(ins < v) v = ins;
      if(i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]){
        int tr = prev2[j - 2] + SP_COST_FULL;
        if(tr < v) v = tr;
      }
      cur[j] = v;
      if(v < rowMin) rowMin = v;
    }
    if(rowMin > lim) return lim + 1;
    for(int j = 0; j <= lb; j++){ prev2[j] = prev1[j]; prev1[j] = cur[j]; }
  }
  return prev1[lb] > lim ? lim + 1 : prev1[lb];
}

// =============================================================
//  7) CONOCIDA / ANALIZABLE
// =============================================================

// Compara la palabra del usuario con una entrada del blob SIN plegar
// tildes (solo caja). Es lo que hace que "pagina" NO cuente como
// conocida existiendo "p\xC3\xA1gina": la diferencia de tilde es
// justamente el error que hay que proponer arreglar.
static bool spSameExact(const char* word, const char* entry){
  const char* w = word;
  const char* e = entry;
  for(;;){
    unsigned char wc = (unsigned char)*w;
    unsigned char ec = (unsigned char)*e;
    if(ec == '\n') ec = 0;
    if(wc == 0 || ec == 0) return wc == ec;
    // Solo la caja ASCII se pliega; los multibyte se comparan byte a byte.
    if(wc >= 'A' && wc <= 'Z') wc = (unsigned char)(wc + 32);
    if(ec >= 'A' && ec <= 'Z') ec = (unsigned char)(ec + 32);
    if(wc != ec) return false;
    w++; e++;
  }
}

// Recorre un blob buscando una coincidencia exacta (salvo caja).
static const char* spBlobFindExact(const char* blob, const char* word){
  const char* p = blob;
  while(*p){
    if(spSameExact(word, p)) return p;
    while(*p && *p != '\n') p++;
    if(*p == '\n') p++;
  }
  return NULL;
}

static bool spUserHas(const char* word){
  for(int i = 0; i < gSpUserN; i++) if(spSameExact(word, gSpUser[i])) return true;
  return false;
}

bool flexSpellKnown(const char* word){
  if(!word || !word[0]) return true;
  if(spUserHas(word)) return true;
  if(spBlobFindExact(SP_TECH, word)) return true;
  if(gSpLang != FLEX_SPELL_EN && spBlobFindExact(SP_ES, word)) return true;
  if(gSpLang != FLEX_SPELL_ES && spBlobFindExact(SP_EN, word)) return true;
  return false;
}

bool flexSpellCheckable(const char* word){
  if(!word || !word[0]) return false;
  int letters = 0, upper = 0, total = 0;
  const char* p = word;
  while(*p){
    unsigned char c = (unsigned char)*p;
    if(c < 0x80){
      if(c >= '0' && c <= '9') return false;                    // "esp32", "0x76": no son faltas
      if(c == '_' || c == '/' || c == '\\' || c == '@' ||
         c == '.' || c == ':' || c == '#') return false;         // rutas, correos, URLs
      if(c >= 'A' && c <= 'Z'){ upper++; letters++; }
      else if(c >= 'a' && c <= 'z') letters++;
      else if(c != '\'' && c != '-') return false;               // simbolos raros: fuera
      p++;
    } else {
      const char* q = p;
      char f = spFoldCh(&q);
      if(f == '?' || f == 0) return false;                       // fuera del alfabeto latino
      letters++;
      p = q;
    }
    total++;
    if(total > FLEX_SPELL_WORD_MAX) return false;                // demasiado larga: no se toca
  }
  if(letters < 3) return false;                                  // "de", "el", "a": nada que corregir
  if(upper == letters) return false;                             // SIGLAS: se respetan
  return true;
}

// =============================================================
//  8) BUSQUEDA DE SUGERENCIAS
// =============================================================

// Una candidata a medio puntuar, antes de ordenar.
struct SpCand {
  const char* word;
  uint8_t     len;      // bytes de la entrada (el blob no termina en 0)
  uint8_t     dist;     // distancia entera
  uint16_t    score;
};

// Puntuacion 0..1000. Manda la distancia; a igualdad desempatan, por
// este orden: coste real en decimas (una tecla vecina puntua mejor
// que una letra cualquiera), misma primera letra (el dedo casi nunca
// falla la primera) y misma longitud.
static uint16_t spScore(const char* fw, const char* fc, int tenths, int dist){
  int s = 1000 - tenths * 40;
  if(fw[0] == fc[0]) s += 60;                                    // misma inicial
  size_t lw = strlen(fw), lc = strlen(fc);
  if(lw == lc) s += 25;
  if(dist == 0) s += 120;                                        // solo cambia la tilde/caja
  if(s < 1) s = 1;
  if(s > 1000) s = 1000;
  return (uint16_t)s;
}

// Inserta manteniendo el array ordenado de mejor a peor. n es cuantas
// hay y cap el maximo; devuelve la n nueva.
static int spInsert(SpCand* arr, int n, int cap, const SpCand &c){
  int pos = n;
  for(int i = 0; i < n; i++) if(c.score > arr[i].score){ pos = i; break; }
  if(pos >= cap) return n;
  int last = (n < cap) ? n : cap - 1;
  for(int i = last; i > pos; i--) arr[i] = arr[i - 1];
  arr[pos] = c;
  return (n < cap) ? n + 1 : cap;
}

// Recorre un blob entero contra la palabra plegada. Devuelve cuantas
// pruebas gasto, para descontarlas del presupuesto.
static int32_t spScanBlob(const char* blob, const char* fw, int lw,
                          SpCand* arr, int &n, int cap, int32_t probes){
  int32_t used = 0;
  const char* p = blob;
  char fc[FLEX_SPELL_WORD_MAX + 2];
  while(*p && used < probes){
    size_t elen = spEntryLen(p);
    // Filtro barato ANTES de la matriz: si las longitudes se separan
    // mas de la distancia maxima, no hace falta ni mirar. Se compara
    // en bytes, que para una palabra con tildes es una cota superior
    // de la longitud plegada -- o sea el filtro nunca descarta de mas.
    int dl = (int)elen - lw;
    if(dl < 0) dl = -dl;
    used++;
    if(dl <= FLEX_SPELL_MAXD + 1 && elen <= FLEX_SPELL_WORD_MAX){
      int lc = spFoldEntry(p, fc, sizeof(fc));
      int dl2 = lc - lw; if(dl2 < 0) dl2 = -dl2;
      if(dl2 <= FLEX_SPELL_MAXD){
        int tenths = spDistTenths(fw, fc, FLEX_SPELL_MAXD);
        if(tenths <= FLEX_SPELL_MAXD * SP_COST_FULL){
          int dist = (tenths + SP_COST_FULL - 1) / SP_COST_FULL;
          SpCand c;
          c.word  = p;
          c.len   = (uint8_t)elen;
          c.dist  = (uint8_t)dist;
          c.score = spScore(fw, fc, tenths, dist);
          n = spInsert(arr, n, cap, c);
        }
      }
    }
    p += elen;
    if(*p == '\n') p++;
  }
  return used;
}

// Las sugerencias apuntan a memoria estable: el blob (flash) o el
// diccionario personal (RAM estatica). Pero las entradas del blob NO
// terminan en 0 -- terminan en '\n' --, asi que hay que copiarlas a
// un almacen propio antes de devolverlas. Tres ranuras: exactamente
// las que caben en la franja de chips del teclado.
#define SP_OUT_MAX 4
static char gSpOut[SP_OUT_MAX][FLEX_SPELL_WORD_MAX + 2];

int flexSpellSuggest(const char* word, FlexSpellSug* out, int maxn, uint32_t budgetUs){
  if(!out || maxn <= 0) return 0;
  if(maxn > SP_OUT_MAX) maxn = SP_OUT_MAX;
  if(!word || !word[0]) return 0;

  char fw[FLEX_SPELL_WORD_MAX + 2];
  int lw = flexSpellFold(word, fw, sizeof(fw));
  if(lw <= 0 || lw > FLEX_SPELL_WORD_MAX) return 0;

  int32_t probes = (budgetUs == 0) ? FLEX_SPELL_PROBES_MAX
                                   : (int32_t)(budgetUs * FLEX_SPELL_PROBES_PER_US);
  if(probes > FLEX_SPELL_PROBES_MAX) probes = FLEX_SPELL_PROBES_MAX;
  if(probes < 64) probes = 64;                    // suelo: siempre da para algo util

  SpCand arr[SP_OUT_MAX];
  int n = 0;

  // ORDEN DE BUSQUEDA. Primero lo mas especifico y mas corto: el
  // diccionario personal (lo que el usuario ya acepto una vez pesa
  // mas que el diccionario general) y los terminos tecnicos. Asi,
  // aunque el presupuesto se agote, lo que si se ha mirado es
  // justamente lo que mas probabilidades tiene de acertar.
  for(int i = 0; i < gSpUserN && probes > 0; i++){
    char fc[FLEX_SPELL_WORD_MAX + 2];
    int lc = flexSpellFold(gSpUser[i], fc, sizeof(fc));
    probes--;
    int dl = lc - lw; if(dl < 0) dl = -dl;
    if(dl > FLEX_SPELL_MAXD) continue;
    int tenths = spDistTenths(fw, fc, FLEX_SPELL_MAXD);
    if(tenths > FLEX_SPELL_MAXD * SP_COST_FULL) continue;
    int dist = (tenths + SP_COST_FULL - 1) / SP_COST_FULL;
    SpCand c;
    c.word  = gSpUser[i];
    c.len   = (uint8_t)strlen(gSpUser[i]);
    c.dist  = (uint8_t)dist;
    // El diccionario personal gana empates a proposito: son palabras
    // que este usuario concreto ya uso.
    int sc = (int)spScore(fw, fc, tenths, dist) + 40;
    c.score = (uint16_t)(sc > 1000 ? 1000 : sc);
    n = spInsert(arr, n, maxn, c);
  }
  if(probes > 0) probes -= spScanBlob(SP_TECH, fw, lw, arr, n, maxn, probes);
  if(probes > 0 && gSpLang != FLEX_SPELL_EN) probes -= spScanBlob(SP_ES, fw, lw, arr, n, maxn, probes);
  if(probes > 0 && gSpLang != FLEX_SPELL_ES) probes -= spScanBlob(SP_EN, fw, lw, arr, n, maxn, probes);

  for(int i = 0; i < n; i++){
    size_t l = arr[i].len;
    if(l > FLEX_SPELL_WORD_MAX) l = FLEX_SPELL_WORD_MAX;
    memcpy(gSpOut[i], arr[i].word, l);
    gSpOut[i][l] = 0;
    out[i].word  = gSpOut[i];
    out[i].dist  = arr[i].dist;
    out[i].score = arr[i].score;
  }
  return n;
}

size_t flexSpellApplyCase(const char* src, const char* model, char* out, size_t n){
  if(!out || n == 0) return 0;
  out[0] = 0;
  if(!model) return 0;
  size_t ml = strlen(model);
  if(ml + 1 > n) ml = n - 1;
  memcpy(out, model, ml);
  out[ml] = 0;
  if(!src || !src[0]) return ml;

  // "TODO EN MAYUSCULAS" solo si el original tiene al menos dos
  // letras ASCII y TODAS lo estan. Con una sola letra mayuscula
  // (que es el caso de una palabra a principio de frase) manda la
  // regla de abajo, que es la habitual.
  int up = 0, low = 0;
  for(const char* p = src; *p; p++){
    if(*p >= 'A' && *p <= 'Z') up++;
    else if(*p >= 'a' && *p <= 'z') low++;
  }
  if(up >= 2 && low == 0){
    for(size_t i = 0; i < ml; i++) if(out[i] >= 'a' && out[i] <= 'z') out[i] = (char)(out[i] - 32);
    return ml;
  }
  if(src[0] >= 'A' && src[0] <= 'Z' && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
  return ml;
}

bool flexSpellAutoFix(const char* word, char* out, size_t n, uint32_t budgetUs){
  if(!out || n == 0) return false;
  out[0] = 0;
  if(!flexSpellCheckable(word)) return false;
  if(flexSpellKnown(word)) return false;

  // Cuatro letras es el suelo: por debajo de eso hay demasiadas
  // palabras a distancia 1 y "corregir" solo cambia una palabra por
  // otra al azar. Ahi se sugiere, pero no se toca lo escrito.
  char fw[FLEX_SPELL_WORD_MAX + 2];
  if(flexSpellFold(word, fw, sizeof(fw)) < 4) return false;

  FlexSpellSug s[3];
  int k = flexSpellSuggest(word, s, 3, budgetUs);
  if(k <= 0) return false;
  if(s[0].dist > 1) return false;                        // dos errores: no hay confianza
  // La mejor tiene que destacar de verdad sobre la segunda. Si hay
  // dos candidatas igual de buenas, elegir una seria adivinar.
  if(k > 1 && (int)s[0].score - (int)s[1].score < 80) return false;
  flexSpellApplyCase(word, s[0].word, out, n);
  return out[0] != 0;
}

// =============================================================
//  9) DICCIONARIO PERSONAL
// =============================================================
bool flexSpellLearn(const char* word){
  if(!word || !word[0]) return false;
  size_t l = strlen(word);
  if(l + 1 > FLEX_SPELL_USER_LEN) return false;
  if(!flexSpellCheckable(word)) return false;
  if(spUserHas(word)) return false;
  if(gSpUserN >= FLEX_SPELL_USER_MAX){
    // Lista llena: se va la mas antigua (la de delante). Es una
    // memoria de las ultimas 48 palabras aceptadas, no un almacen
    // sin fin -- y se dice asi en Ajustes.
    for(int i = 1; i < FLEX_SPELL_USER_MAX; i++) memcpy(gSpUser[i - 1], gSpUser[i], FLEX_SPELL_USER_LEN);
    gSpUserN = FLEX_SPELL_USER_MAX - 1;
  }
  memcpy(gSpUser[gSpUserN], word, l);
  gSpUser[gSpUserN][l] = 0;
  gSpUserN++;
  return true;
}

bool flexSpellForget(const char* word){
  if(!word || !word[0]) return false;
  for(int i = 0; i < gSpUserN; i++){
    if(!spSameExact(word, gSpUser[i])) continue;
    for(int k = i + 1; k < gSpUserN; k++) memcpy(gSpUser[k - 1], gSpUser[k], FLEX_SPELL_USER_LEN);
    gSpUserN--;
    gSpUser[gSpUserN][0] = 0;
    return true;
  }
  return false;
}

int flexSpellUserCount(){ return gSpUserN; }
const char* flexSpellUserAt(int i){ return (i >= 0 && i < gSpUserN) ? gSpUser[i] : ""; }

size_t flexSpellExport(char* out, size_t n){
  if(!out || n == 0) return 0;
  size_t o = 0;
  for(int i = 0; i < gSpUserN; i++){
    size_t l = strlen(gSpUser[i]);
    if(o + l + 2 > n) break;                    // no cabe: se corta en palabra entera
    memcpy(out + o, gSpUser[i], l); o += l;
    out[o++] = '\n';
  }
  out[o] = 0;
  return o;
}

void flexSpellImport(const char* blob, size_t n){
  gSpUserN = 0;
  if(!blob) return;
  char w[FLEX_SPELL_USER_LEN];
  size_t k = 0;
  for(size_t i = 0; i <= n; i++){
    char c = (i < n) ? blob[i] : '\n';
    if(c == 0) break;
    if(c == '\n' || c == '\r'){
      if(k > 0){ w[k] = 0; flexSpellLearn(w); k = 0; }
      continue;
    }
    if(k + 1 < sizeof(w)) w[k++] = c;
    else k = sizeof(w);                          // palabra demasiado larga: se descarta entera
  }
}
