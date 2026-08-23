// #############################################################
// ##  FLEX OS -- JUMPER: DATOS DEL NIVEL
// ##  ----------------------------------------------------------
// ##  Solo DATOS. Ni una linea de dibujo ni de fisica: el motor
// ##  vive en FlexOS_Jumper.h. Se separa porque son tablas
// ##  constantes que van a FLASH tal cual (PROGMEM implicito en
// ##  el P4: la flash esta mapeada, un `static const` no se copia
// ##  a RAM) y mezclarlas con el motor solo haria ilegible a los
// ##  dos.
// ##
// ##  UNIDADES  (todo entero, nada de coma flotante en la tabla)
// ##    x  -> CUARTOS de bloque desde el inicio del nivel.
// ##          El nivel mide 900 bloques = 3600 cuartos, asi que
// ##          entra de sobra en uint16_t.
// ##    y  -> MEDIOS bloques sobre la superficie del suelo base,
// ##          positivo hacia arriba (int8_t: -64..+63 bloques).
// ##    a/b-> parametro por tipo; casi siempre ancho/alto en
// ##          MEDIOS bloques, o un contador de repeticiones.
// ##
// ##  Un objeto ocupa 6 bytes. El nivel entero (obstaculos,
// ##  suelo, techo, color) cabe holgadamente en flash y NUNCA se
// ##  copia a RAM: el motor lee la tabla in situ y solo recorre
// ##  la ventana visible (indices de inicio/fin por camara).
// ##
// ##  CALIBRACION (medida sobre el video de referencia):
// ##    · velocidad constante 10.4 bloques/s (sin cambios de
// ##      velocidad: la curva de porcentaje del video es recta),
// ##    · longitud 900 bloques -> 100% a los 86.54 s.
// #############################################################
#pragma once
#include <stdint.h>

// ---- Geometria del nivel ------------------------------------------------
#define JL_LEN_BLK      900        // longitud total en bloques
#define JL_SPEED_BLK    10.4f      // bloques por segundo (1x de Geometry Dash)

// ---- Tipos de objeto ----------------------------------------------------
enum JmpObjType : uint8_t {
  JO_BLOCK = 1,   // solido.        a = ancho, b = alto (medios bloques)
  JO_SPIKE,       // pincho ARRIBA. a = cuantos seguidos (1 bloque cada uno)
  JO_SPIKED,      // pincho ABAJO (cuelga de y). a = cuantos seguidos
  JO_SPIKEL,      // pincho hacia la IZQUIERDA. b = cuantos apilados
  JO_SPIKER,      // pincho hacia la DERECHA.   b = cuantos apilados
  JO_BULLET,      // bloque 1x1 con morro (los del pasillo de nave)
  JO_ORB,         // a = 0 amarillo (salto), 1 azul (invierte gravedad)
  JO_PGRAV,       // portal gravedad. a = 0 invierte (amarillo), 1 normal (azul)
  JO_PSHIP,       // portal nave (rosa)
  JO_PCUBE,       // portal cubo (verde)
  JO_COIN,        // moneda secreta. a = indice 0..2
  JO_CHAIN,       // cadena decorativa: sube desde y, b medios bloques
  JO_PLATE,       // plataforma fina decorada. a = ancho (medios bloques)
  JO_TOWER,       // columna DECORATIVA (sin colision). a = ancho, b = alto
};

struct JmpObj { uint16_t x; int8_t y; uint8_t t; uint8_t a; uint8_t b; };

// Macros de escritura: se escribe en BLOQUES (con decimales) y quedan
// enteros en la tabla. Nada de esto sobrevive al compilador.
#define QX(v)   ((uint16_t)((v) * 4.0f  + 0.5f))
#define HY(v)   ((int8_t)  ((v) * 2.0f  + ((v) < 0 ? -0.5f : 0.5f)))
#define HN(v)   ((uint8_t) ((v) * 2.0f  + 0.5f))

#define OB(x,y,w,h)  { QX(x), HY(y), JO_BLOCK,  HN(w), HN(h) }
#define OS(x,n)      { QX(x), HY(0), JO_SPIKE,  (uint8_t)(n), 0 }
#define OSY(x,y,n)   { QX(x), HY(y), JO_SPIKE,  (uint8_t)(n), 0 }
#define OD(x,y,n)    { QX(x), HY(y), JO_SPIKED, (uint8_t)(n), 0 }
#define OL(x,y,n)    { QX(x), HY(y), JO_SPIKEL, 0, (uint8_t)(n) }
#define OR_(x,y,n)   { QX(x), HY(y), JO_SPIKER, 0, (uint8_t)(n) }
#define OU(x,y)      { QX(x), HY(y), JO_BULLET, 2, 2 }
#define OO(x,y,k)    { QX(x), HY(y), JO_ORB,    (uint8_t)(k), 0 }
#define OPG(x,y,k)   { QX(x), HY(y), JO_PGRAV,  (uint8_t)(k), 0 }
#define OPS(x,y)     { QX(x), HY(y), JO_PSHIP,  0, 0 }
#define OPC(x,y)     { QX(x), HY(y), JO_PCUBE,  0, 0 }
#define OC(x,y,i)    { QX(x), HY(y), JO_COIN,   (uint8_t)(i), 0 }
#define OCH(x,y,l)   { QX(x), HY(y), JO_CHAIN,  0, HN(l) }
#define OP_(x,y,w)   { QX(x), HY(y), JO_PLATE,  HN(w), 1 }
#define OT(x,y,w,h)  { QX(x), HY(y), JO_TOWER,  HN(w), HN(h) }

// #############################################################
// ##  OBSTACULOS  (ordenados por x -- el motor lo da por hecho)
// #############################################################
static const JmpObj JL_OBJ[] = {
  // ---------------------------------------------------------------
  // S1  0-107   CUBO normal, morado -> magenta
  // ---------------------------------------------------------------
  OT(10,8.38,1,4), OT(13.8,6.83,1,3), OB(14,0,1,1), OB(16.96,5.6,1,1),
  OT(18.67,7.66,1,4), OSY(19.2,0,2), OT(22.08,0,1,2), OT(22.2,0,1,3),
  OSY(25.4,0,1), OB(27,0,1,1), OT(27.07,0,1,7), OB(32.2,0,1,1),
  OT(32.42,0,1,4), OT(34.72,0,1,6), OCH(35.66,5.8,3), OSY(37.4,0,3),
  OT(39.6,0,1,4), OP_(41.4,3.2,2), OSY(44.6,0,2), OT(44.99,7.1,1,4),
  OT(47.6,0,1,5), OT(49.86,6.28,1,2), OB(50.8,0,1,2), OP_(53.34,3.6,2),
  OT(54.09,0,1,2), OB(56,0,1,2), OT(58.23,0,1,6), OT(58.77,0,1,3),
  OB(61.2,0,1,1), OT(62.65,6.32,1,4), OT(64.06,0,1,5), OSY(66.4,0,3),
  OT(67.26,7.94,1,3), OB(70.4,5.4,2,1), OCH(70.4,5.4,3.2), OD(70.4,5.4,2),
  OT(70.68,0,1,8), OSY(73.6,0,1), OB(75.2,0,1,1), OT(75.48,0,1,7),
  OT(77.2,0,1,7), OCH(79.71,5.8,3), OSY(80.4,0,1), OT(83.91,0,1,7),
  OT(84.76,0,1,3), OSY(85.6,0,1), OT(88.12,0,1,4), OT(90.02,0,1,2),
  OSY(90.8,0,1), OB(92.4,0,1,1), OCH(94.04,5.8,3), OT(94.4,0,1,6),
  OB(97.6,0,1,2), OT(98.46,0,1,8), OO(99,2,0), OB(100.86,5.6,1,1),
  OO(103,2,0), OT(103.15,6.84,1,3),
  // ---------------------------------------------------------------
  // S2  107-163 CUBO por el TECHO (y=9), verde
  // ---------------------------------------------------------------
  OPG(107.5,4.5,0), OT(112,0,1,2), OB(113,8,2,1), OD(115,9,1),
  OB(120.2,8,2,1), OD(122.2,9,1), OT(126,0,1,3), OD(127.4,9,2),
  OD(133.6,9,3), OT(140,0,1,3), OD(140.8,9,1), OB(146,8,2,1),
  OD(148,9,1), OD(153.2,9,1), OT(154,0,1,2), OB(154.8,8,1,1),
  OPG(162.5,4.5,1),
  // ---------------------------------------------------------------
  // S3  163-223 CUBO normal, magenta
  // ---------------------------------------------------------------
  OT(164,7.21,1,2), OT(167.96,0,1,4), OSY(169,0,3), OT(171.76,0,1,7),
  OP_(173,3.2,3), OT(175.58,0,1,3), OB(176.2,0,1,2), OB(179.29,5.4,2,1),
  OCH(179.29,5.4,3.2), OD(179.29,5.4,2), OT(179.97,0,1,5), OSY(181.4,0,1),
  OT(183.45,8.1,1,4), OB(184.19,5.4,2,1), OCH(184.19,5.4,3.2), OD(184.19,5.4,2),
  OSY(186.6,0,1), OT(188.07,6.77,1,3), OB(188.2,0,1,1), OT(190.2,6.2,1,4),
  OT(191.8,0,1,5), OSY(193.4,0,2), OB(196.82,5.4,2,1), OCH(196.82,5.4,3.2),
  OD(196.82,5.4,2), OT(196.96,0,1,5), OB(199.6,0,1,2), OT(201.79,7.35,1,3),
  OT(201.99,0,1,5), OSY(204.8,0,1), OO(205.5,2.2,0), OT(206.22,0,1,7),
  OB(206.4,0,1,1), OT(208.4,0,1,4), OT(211.06,0,1,7), OSY(211.6,0,1),
  OB(213.2,0,1,1), OT(214.44,0,1,2), OT(215.2,0,1,4), OT(219.22,6.91,1,3),
  OPS(222.5,4),
  // ---------------------------------------------------------------
  // S4  223-337 NAVE (techo a 10)
  // ---------------------------------------------------------------
  OB(230,6.5,2,3.5), OB(238.49,0,2,3.5), OPG(250.5,5,0), OB(257,0,2,3.5),
  OB(265.98,6.5,2,3.5), OPG(278.5,5,1), OB(285,0,2,3.5), OPG(303,5,0),
  OB(310,6.5,2,3.5), OC(320,1.4,0), OB(325,0,2,3.5), OPG(335.5,4.5,1),
  // ---------------------------------------------------------------
  // S5  337-445 CUBO normal, indigo -> magenta
  // ---------------------------------------------------------------
  OPC(337.5,4.5), OT(338,6.7,1,3), OT(342.02,0,1,6), OSY(345,0,3),
  OT(346.86,0,1,8), OB(349,5.6,1,1), OT(351.86,8.19,1,4), OSY(352.2,0,2),
  OB(355.2,5.6,1,1), OT(356.72,0,1,6), OB(358.4,0,1,1), OT(360.3,0,1,2),
  OT(360.61,0,1,4), OSY(363.6,0,1), OB(365.2,0,1,1), OT(365.52,0,1,6),
  OT(367.2,0,1,3), OT(369.48,0,1,7), OSY(370.4,0,2), OT(373.4,0,1,4),
  OT(374.37,0,1,4), OT(379.55,0,1,2), OO(384,1.9,0), OT(384.17,0,1,2),
  OT(387.38,0,1,8), OO(387.6,4.1,0), OC(390.6,6,1), OT(392.34,0,1,7),
  OSY(396,0,1), OT(397.49,7.2,1,2), OB(397.6,0,1,1), OT(399.6,6.2,1,4),
  OT(402.52,0,1,4), OB(402.8,0,1,1), OT(405.84,0,1,7), OCH(406.36,5.8,3),
  OSY(408,0,3), OT(411.7,8.12,1,4), OT(412,0,1,6), OB(415.2,0,2,1),
  OT(415.44,0,1,6), OSY(417.2,0,1), OT(419.2,0,1,7), OCH(420.22,5.8,3),
  OB(422.4,0,2,1), OSY(424.4,0,1), OT(424.53,6.58,1,2), OP_(426.4,3.2,2),
  OCH(428.16,5.8,3), OSY(429.6,0,1), OT(432.5,0,1,5), OT(433.15,0,1,4),
  OB(434.8,0,2,1), OT(436.38,0,1,2), OSY(436.8,0,1), OT(438.8,0,1,4),
  OT(439.59,6.13,1,3),
  // ---------------------------------------------------------------
  // S6  445-505 CUBO, dos vueltas de gravedad (rojo)
  // ---------------------------------------------------------------
  OPG(445.5,4.5,0), OT(446,8.1,1,4), OT(451.05,0,1,7), OB(452,6,1,2),
  OT(455.57,0,1,7), OD(457.2,8,1), OT(459.83,5.69,1,2), OT(465.14,5.97,1,4),
  OPG(469.5,4,1), OT(469.91,0,1,6), OCH(474.99,5.8,3), OSY(475,0,2),
  OT(479.19,0,1,5), OT(482.92,0,1,6), OT(486.24,0,1,6), OPG(489.5,4,0),
  OT(490.97,8.08,1,2), OT(495.36,0,1,7), OT(499.34,0,1,2), OT(502.9,6.47,1,3),
  OPG(503,4,1),
  // ---------------------------------------------------------------
  // S7  505-557 CUBO normal con orbes
  // ---------------------------------------------------------------
  OT(506,0,1,5), OB(509,0,1,2), OT(510.51,6.83,1,3), OT(511.56,0,1,5),
  OSY(514.2,0,3), OCH(515.02,5.8,3), OT(518.2,6.2,1,4), OO(519,2.1,0),
  OT(520.14,0,1,3), OSY(521.4,0,3), OT(524.68,0,1,7), OT(525.4,0,1,7),
  OSY(528.6,0,3), OT(529.32,0,1,6), OT(532.6,0,1,4), OT(534,7.94,1,2),
  OO(534,2.1,0), OB(535.8,0,1,2), OT(538,6.44,1,4), OT(538.01,0,1,7),
  OSY(541,0,3), OT(541.81,0,1,5), OT(545,0,1,5), OT(545.05,0,1,6),
  OO(548,2.1,0), OT(549.63,7.38,1,2), OT(553.45,0,1,4),
  // ---------------------------------------------------------------
  // S8  557-671 NAVE larga, pinchos continuos en los bordes
  // ---------------------------------------------------------------
  OPS(557,4.5), OU(566,4.11), OU(573.68,4.76), OU(581.12,5.35),
  OU(588.91,4.14), OU(595.68,2.21), OU(603.55,4.64), OU(610.48,2.9),
  OC(615.5,2,2), OU(618.36,2.38), OU(625.69,3.18), OU(633.29,4.22),
  OU(640.45,3.72), OU(647.36,3.22), OU(653.99,4.62), OU(660.72,4.34),
  OPC(670.5,4.5),
  // ---------------------------------------------------------------
  // S9  671-782 CUBO normal, arcoiris
  // ---------------------------------------------------------------
  OT(672,7.61,1,2), OT(677.22,5.74,1,3), OSY(678,0,2), OT(681.3,0,1,2),
  OP_(681.4,3.2,3), OSY(684.2,0,1), OT(686.6,5.93,1,2), OT(686.74,0,1,6),
  OSY(689.4,0,2), OT(691.47,7.29,1,3), OB(692.4,5.4,2,1), OCH(692.4,5.4,3.2),
  OD(692.4,5.4,2), OT(695.24,0,1,7), OB(695.6,0,1,1), OB(697.87,5.4,2,1),
  OCH(697.87,5.4,3.2), OD(697.87,5.4,2), OT(698.52,0,1,2), OSY(700.8,0,1),
  OT(702.4,0,1,2), OB(702.4,0,1,1), OB(704.4,5.4,2,1), OCH(704.4,5.4,3.2),
  OD(704.4,5.4,2), OB(707.6,0,1,2), OT(707.76,0,1,8), OT(710.21,0,1,7),
  OT(711.64,6.88,1,2), OSY(712.8,0,1), OT(715.13,0,1,6), OT(716.35,6.02,1,4),
  OSY(718,0,1), OT(721.63,0,1,7), OB(723.2,0,1,1), OT(724.88,0,1,4),
  OB(726.16,5.4,2,1), OCH(726.16,5.4,3.2), OD(726.16,5.4,2), OSY(728.4,0,3),
  OT(730.14,0,1,3), OT(732.4,0,1,7), OCH(734.4,5.8,3), OB(735.6,0,1,1),
  OT(737.98,0,1,4), OT(739.71,0,1,8), OB(740.8,0,1,2), OT(743.16,0,1,4),
  OT(743.83,0,1,4), OSY(746,0,2), OT(749,0,1,6), OT(749.16,6.62,1,2),
  OSY(752.2,0,3), OT(752.87,0,1,6), OT(756.2,0,1,7), OT(757.34,7.11,1,3),
  OB(759.4,0,2,1), OSY(761.4,0,1), OT(761.87,7.06,1,4), OT(763.4,6.2,1,4),
  OT(765.65,0,1,2), OSY(766.6,0,2), OT(769.34,6.75,1,3), OB(769.99,5.6,1,1),
  OT(773.53,6.02,1,3), OT(777.12,0,1,6),
  // ---------------------------------------------------------------
  // S10 782-837 CUBO por el TECHO (y=9)
  // ---------------------------------------------------------------
  OPG(782,4.5,0), OT(783,0,1,4), OT(787.78,0,1,6), OD(789,9,2),
  OT(790,0,1,2), OCH(792.52,5.8,3), OB(795.2,7,1,2), OT(795.97,0,1,8),
  OB(800.4,8,2,1), OT(800.86,0,1,3), OD(802.4,9,1), OT(806,0,1,2),
  OT(806.24,6.84,1,3), OD(807.6,9,3), OT(809.56,6.17,1,4), OT(813.71,0,1,7),
  OD(814.8,9,2), OT(817.16,0,1,4), OT(820.39,0,1,6), OD(821,9,1),
  OT(822,0,1,2), OT(824.33,8.38,1,4), OB(826.2,7,1,2), OT(829.5,0,1,3),
  OT(834.52,6.21,1,2), OPG(836.5,4.5,1),
  // ---------------------------------------------------------------
  // S11 837-900 Cierre
  // ---------------------------------------------------------------
  OT(838,0,1,3), OT(842.47,7.72,1,2), OB(843,0,1,2), OT(846.32,0,1,7),
  OT(846.81,0,1,2), OB(848.2,0,1,2), OT(850.52,0,1,6), OT(850.83,5.72,1,4),
  OSY(853.4,0,1), OT(854.8,0,1,5), OT(855.81,0,1,7), OSY(858.6,0,1),
  OCH(860.01,5.8,3), OB(860.2,0,1,1), OT(862.2,0,1,6), OT(863.44,0,1,8),
  OB(865.4,0,1,2), OT(867.71,7.18,1,4), OB(867.86,5.6,1,1), OT(871.18,0,1,7),
  OT(874.47,8.38,1,2), OB(879,0,1,2), OT(879,5,1,3), OT(879.47,0,1,3),
  OL(880,3,2), OCH(882.93,5.8,3), OT(884,0,1,4), OT(887.22,0,1,7),
  OT(890,0,1,4), OT(891.22,0,1,5), OT(894.85,7.01,1,2), OT(896,0,1,4),
  OT(899.58,0,1,3),
};
static const int JL_OBJ_N = (int)(sizeof(JL_OBJ) / sizeof(JL_OBJ[0]));

// #############################################################
// ##  SUELO Y TECHO  (tramos; fuera de un tramo no hay techo)
// #############################################################
struct JmpSeg { uint16_t x0, x1; int8_t y; };

// Escalones del suelo: el video sube y baja la linea del suelo en varios
// tramos. Solo los saltos claros; por defecto el suelo esta a 0.
static const JmpSeg JL_FLOOR[] = {
  { QX(0),   QX(900), HY(0) },
};
static const int JL_FLOOR_N = (int)(sizeof(JL_FLOOR) / sizeof(JL_FLOOR[0]));

// Techos solidos. En los tramos de gravedad invertida el techo ES el suelo
// del jugador; en los de nave acota el pasillo.
static const JmpSeg JL_CEIL[] = {
  { QX(105), QX(166), HY(9)  },   // S2  tramo verde por el techo
  { QX(222), QX(340), HY(10) },   // S4  pasillo de nave
  { QX(444), QX(506), HY(8)  },   // S6  tramo rojo
  { QX(556), QX(674), HY(9)  },   // S8  pasillo de nave largo
  { QX(780), QX(840), HY(9)  },   // S10 tramo por el techo
};
static const int JL_CEIL_N = (int)(sizeof(JL_CEIL) / sizeof(JL_CEIL[0]));

// Tramos con pinchos continuos en el borde (los pasillos de nave del video).
// b = 0 pinchos en el suelo, 1 en el techo.
static const JmpSeg JL_EDGE[] = {
  { QX(558), QX(672), 0 },
  { QX(558), QX(672), 1 },
};
static const int JL_EDGE_N = (int)(sizeof(JL_EDGE) / sizeof(JL_EDGE[0]));

// #############################################################
// ##  DISPARADORES DE COLOR
// ##  ----------------------------------------------------------
// ##  Cada uno guarda: posicion X, paleta destino (tono/saturacion/
// ##  valor), duracion de mezcla y la intensidad de la pulsacion
// ##  luminosa del fondo. El tono va en GRADOS ACUMULADOS: el
// ##  tramo final del video es una rueda de color continua que da
// ##  casi dos vueltas, y guardarlo acumulado hace que interpolar
// ##  entre dos disparadores gire por el lado correcto.
// #############################################################
struct JmpBg { uint16_t x; uint16_t hue; uint8_t sat, val; uint16_t blend; uint8_t pulse; };

static const JmpBg JL_BG[] = {
  // Cada fila dice: DESDE x, mezcla la paleta anterior con esta a lo largo de
  // `blend`. Con blend=0 el cambio es seco (los cortes de tramo del video);
  // con blend>0 es una transicion, y encadenando disparadores lejanos se
  // obtiene la rueda de color continua del ultimo tercio.
  //   x                 hue  sat  val   mezcla     pulso
  { QX(0),              276, 255, 196,  0,          40 },  // morado de salida
  { QX(10),             313, 255, 194,  QX(86),     40 },  // -> magenta (10%)
  { QX(107),            128, 254, 183,  0,          55 },  // verde        (corte)
  { QX(163),            314, 255, 195,  0,          50 },  // magenta      (corte)
  { QX(224),            226, 255, 193,  0,          60 },  // azul, nave   (corte)
  { QX(250),            120, 255, 192,  0,          60 },  // verde        (corte)
  { QX(278),            228, 255, 192,  0,          60 },  // azul         (corte)
  { QX(303),            120, 255, 192,  0,          60 },  // verde        (corte)
  { QX(337),            253, 214, 187,  0,          45 },  // indigo, vuelve el cubo
  { QX(345),            272, 255, 180,  QX(55),     45 },
  { QX(400),            298, 255, 150,  QX(37),     45 },
  { QX(452),            357, 245, 201,  0,          65 },  // rojo         (corte)
  { QX(460),            351, 255, 183,  QX(45),     50 },
  { QX(505),            294, 255, 176,  QX(50),     50 },
  { QX(555),            225, 255, 189,  QX(10),     60 },  // azul del portal de nave
  // Rueda de color: el tono va ACUMULADO (>360) para que la interpolacion gire
  // siempre hacia adelante. Son casi dos vueltas completas, como en el video.
  { QX(565),            480, 255, 168,  QX(119),    55 },  // azul -> ... -> verde
  { QX(684),            720, 255, 200,  QX(120),    55 },  // verde -> ... -> rojo
  { QX(804),            925, 255, 120,  QX(74),     45 },  // rojo -> ... -> azul
  { QX(878),            940, 255,  58,  QX(18),     30 },  // cierre azul oscuro
};
static const int JL_BG_N = (int)(sizeof(JL_BG) / sizeof(JL_BG[0]));

// #############################################################
// ##  PUNTOS DE CONTROL DEL MODO PRACTICA
// ##  Uno cada ~30 bloques y siempre justo DESPUES de cada portal,
// ##  para que reanudar respete forma, gravedad, paleta y camara.
// #############################################################
static const uint16_t JL_CHK[] = {
  QX(30),  QX(60),  QX(90),  QX(112), QX(140), QX(166),
  QX(195), QX(226), QX(254), QX(282), QX(307), QX(341),
  QX(370), QX(400), QX(430), QX(449), QX(473), QX(493),
  QX(507), QX(535), QX(561), QX(590), QX(620), QX(650),
  QX(675), QX(705), QX(735), QX(765), QX(786), QX(812),
  QX(840), QX(866),
};
static const int JL_CHK_N = (int)(sizeof(JL_CHK) / sizeof(JL_CHK[0]));
