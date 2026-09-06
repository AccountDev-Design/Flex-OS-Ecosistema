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
  OT(10,0,1,2), OT(11.97,0,1,2), OB(14,0,1,1), OT(14.28,7.63,1,2),
  OT(16.34,0,1,7), OSY(19.2,0,1), OT(19.6,6.17,1,3), OT(21.55,7.76,1,3),
  OT(23.46,7.51,1,3), OSY(24.4,0,2), OT(25.84,8.45,1,2), OT(27.76,0,1,2),
  OT(30.12,6.65,1,3), OSY(30.6,0,1), OB(32.2,0,1,1), OT(32.77,8.51,1,3),
  OT(36.02,0,1,8), OB(37.4,0,1,1), OT(38.29,6.03,1,2), OT(41.27,6.15,1,4),
  OB(42.6,0,1,1), OT(43.67,7.73,1,2), OT(45.89,0,1,3), OSY(47.8,0,2),
  OT(47.93,6.48,1,3), OT(50.24,7.36,1,4), OT(52.58,0,1,3), OB(54,0,1,1),
  OT(54.96,6.61,1,3), OT(58.02,8.24,1,3), OSY(59.2,0,2), OT(60.94,8.31,1,2),
  OT(63.89,0,1,4), OB(65.4,0,1,2), OT(66.22,6.96,1,3), OT(69.22,6.24,1,2),
  OSY(70.6,0,1), OB(72.2,0,1,1), OT(72.37,7.35,1,3), OT(74.96,0,1,5),
  OB(77.4,0,1,2), OT(77.7,5.63,1,4), OT(79.8,8.05,1,2), OT(81.8,8.01,1,2),
  OSY(82.6,0,2), OT(84.28,7.52,1,3), OT(86.83,6.75,1,4), OB(88.8,0,1,1),
  OT(89.05,6.91,1,2), OT(91.27,0,1,2), OT(93.38,6.26,1,3), OB(94,0,2,1),
  OT(95.41,8.53,1,3), OSY(96,0,1), OT(97.54,7.37,1,3), OO(99,2,0),
  OT(100.61,0,1,8), OO(103,2,0), OT(103.36,7.33,1,3),
  // ---------------------------------------------------------------
  // S2  107-163 CUBO por el TECHO (y=9), verde
  // ---------------------------------------------------------------
  OPG(107.5,4.5,0), OT(108,7.97,1,2), OT(110.4,7.87,1,2), OB(113,8,1,1),
  OT(115.77,0,1,4), OD(118.2,9,2), OT(122.12,0,1,3), OB(124.4,8,2,1),
  OT(124.51,0,1,7), OD(126.4,9,1), OT(126.64,0,1,3), OT(129.43,0,1,6),
  OB(131.6,8,2,1), OD(133.6,9,1), OT(135.31,5.95,1,3), OB(138.8,8,2,1),
  OT(139.79,0,1,2), OD(140.8,9,1), OT(142.47,0,1,8), OB(146,7,1,2),
  OT(147.27,6.95,1,3), OT(149.44,0,1,4), OD(151.2,9,1), OT(153.64,5.65,1,4),
  OT(155.92,0,1,5), OT(158.41,0,1,2), OT(160.93,0,1,3), OPG(162.5,4.5,1),
  // ---------------------------------------------------------------
  // S3  163-223 CUBO normal, magenta
  // ---------------------------------------------------------------
  OT(164,0,1,2), OT(166.78,0,1,5), OT(168.93,6.79,1,3), OSY(169,0,2),
  OT(171.84,0,1,8), OT(173.81,7.96,1,2), OSY(175.2,0,1), OT(176.38,7.02,1,3),
  OB(176.8,0,1,1), OT(178.81,0,1,6), OT(181.75,8.13,1,3), OSY(182,0,3),
  OT(184.94,6.46,1,4), OT(187.55,0,1,8), OSY(189.2,0,1), OT(189.45,7.37,1,2),
  OB(190.8,0,1,1), OT(191.66,8.29,1,4), OT(193.92,0,1,5), OT(195.87,6.28,1,3),
  OSY(196,0,1), OB(197.6,0,1,1), OT(198.77,6.22,1,4), OT(201.91,7.2,1,2),
  OB(202.8,0,2,1), OSY(204.8,0,1), OT(204.97,6.75,1,3), OO(205.5,2.2,0),
  OT(208.24,8.12,1,4), OSY(210,0,2), OT(210.48,7.87,1,2), OT(212.87,0,1,6),
  OT(216.14,6.19,1,3), OT(218.06,0,1,2), OT(219.97,6.13,1,3), OPS(222.5,4),
  // ---------------------------------------------------------------
  // S4  223-337 NAVE (techo a 10)
  // ---------------------------------------------------------------
  OB(230,6.5,2,3.5), OB(238.42,0,2,3.5), OPG(250.5,5,0), OB(257,0,2,3.5),
  OB(265.35,6.5,2,3.5), OPG(278.5,5,1), OB(285,6.5,2,3.5), OB(293.04,0,2,3.5),
  OPG(303,5,0), OB(310,0,2,3.5), OC(320,1.4,0), OB(325,6.5,2,3.5),
  OPG(335.5,4.5,1),
  // ---------------------------------------------------------------
  // S5  337-445 CUBO normal, indigo -> magenta
  // ---------------------------------------------------------------
  OPC(337.5,4.5), OT(338,8.1,1,4), OT(341.08,0,1,7), OT(343.81,0,1,7),
  OB(345,0,2,1), OT(346.39,5.69,1,2), OSY(347,0,1), OT(349.63,5.97,1,4),
  OB(352.2,0,1,1), OT(352.53,8.22,1,3), OT(355.09,6.27,1,2), OSY(357.4,0,2),
  OT(358.35,6.93,1,3), OT(361.32,5.84,1,4), OSY(363.6,0,1), OT(363.92,6.11,1,2),
  OT(366.04,6.47,1,3), OT(368.08,7.55,1,2), OB(368.8,0,1,2), OT(370.6,8.26,1,2),
  OB(370.8,5.4,2,1), OCH(370.8,6.4,3.2), OD(370.8,5.4,2), OT(373.37,6.85,1,4),
  OSY(374,0,2), OT(376.18,0,1,6), OT(379.02,7.94,1,2), OT(381.43,6.44,1,4),
  OT(383.72,6.04,1,3), OO(384,1.9,0), OT(385.67,7.76,1,4), OO(387.6,4.1,0),
  OT(388.92,0,1,5), OC(390.6,6,1), OT(391.29,7.57,1,4), OT(393.52,7.85,1,2),
  OT(395.66,6.38,1,2), OSY(396,0,1), OT(398.48,7.69,1,4), OSY(401.2,0,1),
  OT(401.22,8.18,1,2), OT(403.42,0,1,4), OT(406.2,6.82,1,3), OB(406.4,0,1,1),
  OT(409.09,5.93,1,4), OSY(411.6,0,1), OT(412.14,7.86,1,2), OB(413.2,0,1,1),
  OT(414.8,5.67,1,3), OT(417.47,7.18,1,2), OSY(418.4,0,2), OT(420.04,8.29,1,2),
  OT(422.16,6.94,1,4), OT(424.51,6.14,1,3), OB(424.6,0,1,1), OT(426.5,7.88,1,4),
  OB(426.6,5.4,2,1), OCH(426.6,6.4,3.2), OD(426.6,5.4,2), OT(428.57,6.71,1,3),
  OB(429.8,0,2,1), OT(431.31,7.06,1,4), OSY(431.8,0,1), OT(433.58,0,1,2),
  OT(435.8,6.75,1,3), OB(437,0,1,2), OT(438.32,6.02,1,3), OT(440.47,0,1,6),
  OT(443.47,0,1,4),
  // ---------------------------------------------------------------
  // S6  445-505 CUBO, dos vueltas de gravedad (rojo)
  // ---------------------------------------------------------------
  OPG(445.5,4.5,0), OT(446,7.82,1,2), OT(448.05,0,1,8), OT(451.03,0,1,3),
  OD(452,8,1), OT(454.32,6.84,1,3), OB(457.2,7,2,1), OD(459.2,8,1),
  OT(459.28,0,1,2), OT(462.57,0,1,3), OT(464.73,0,1,8), OT(466.75,0,1,3),
  OPG(469.5,4,1), OT(469.78,7.92,1,2), OT(472.82,0,1,2), OB(475,0,1,2),
  OT(475.72,8.18,1,2), OT(477.84,6.48,1,3), OB(480.2,0,2,1), OT(481.14,6.26,1,4),
  OSY(482.2,0,1), OT(483.53,0,1,5), OT(486.7,5.89,1,3), OT(489.17,6.96,1,3),
  OPG(489.5,4,0), OT(492.01,6.7,1,2), OT(495.29,8.23,1,2), OT(497.85,0,1,2),
  OT(501.1,0,1,5), OPG(503,4,1), OT(503.7,6.83,1,2),
  // ---------------------------------------------------------------
  // S7  505-557 CUBO normal con orbes
  // ---------------------------------------------------------------
  OT(506,7.22,1,4), OT(508.05,6.77,1,3), OB(509,0,1,1), OT(510.82,0,1,7),
  OT(512.95,0,1,8), OB(514.2,0,1,1), OT(515.3,5.85,1,2), OT(517.77,7.7,1,4),
  OO(519,2.1,0), OB(519.4,0,1,2), OT(520.78,0,1,3), OT(523.23,5.6,1,4),
  OSY(524.6,0,1), OB(526.2,0,1,1), OT(526.52,7.68,1,2), OT(528.72,0,1,8),
  OT(530.88,8.21,1,2), OSY(531.4,0,2), OT(532.96,7.88,1,2), OO(534,2.1,0),
  OT(535.6,0,1,2), OSY(537.6,0,1), OT(538.57,8.3,1,2), OB(539.2,0,1,1),
  OT(540.87,0,1,8), OT(543.59,6.91,1,3), OSY(544.4,0,3), OT(545.87,7.1,1,2),
  OO(548,2.1,0), OT(548.46,7.6,1,3), OT(550.77,7.51,1,4), OT(553.31,6.04,1,4),
  // ---------------------------------------------------------------
  // S8  557-671 NAVE larga, pinchos continuos en los bordes
  // ---------------------------------------------------------------
  OPS(557,4.5), OU(566,5.62), OU(572.9,4.49), OU(580.01,4.46),
  OU(587.25,3.78), OU(595.14,2.33), OU(601.51,5.32), OU(608.02,5.49),
  OU(615.48,4.79), OC(615.5,2,2), OU(622.21,5.06), OU(629.83,4.56),
  OU(636.79,5.54), OU(644.79,2.43), OU(651.47,4.06), OU(657.52,3.5),
  OPC(670.5,4.5),
  // ---------------------------------------------------------------
  // S9  671-782 CUBO normal, arcoiris
  // ---------------------------------------------------------------
  OT(672,0,1,3), OT(674.63,0,1,7), OT(677.93,7.62,1,3), OB(678,0,1,1),
  OT(680.57,7.76,1,2), OSY(683.2,0,3), OT(683.82,6.7,1,3), OT(686.85,0,1,4),
  OT(689.06,0,1,4), OB(690.4,0,1,2), OT(691.5,7.05,1,2), OT(693.49,6.26,1,4),
  OB(695.6,0,1,2), OT(695.91,8.16,1,2), OT(698.41,0,1,7), OT(700.4,6.95,1,2),
  OB(700.8,0,1,1), OT(702.35,0,1,2), OT(705.44,7.94,1,2), OSY(706,0,2),
  OT(708.63,0,1,5), OT(710.67,6.71,1,2), OB(712.2,0,2,1), OT(713.35,6.08,1,3),
  OSY(714.2,0,1), OT(715.34,6.8,1,2), OT(717.35,0,1,4), OSY(719.4,0,2),
  OT(720.33,6.85,1,4), OT(723.28,6.98,1,3), OB(725.6,0,2,1), OT(726.35,8.49,1,2),
  OSY(727.6,0,1), OT(729.33,0,1,4), OT(731.76,5.77,1,3), OB(732.8,0,1,1),
  OT(734.44,5.69,1,4), OT(737.72,7.07,1,2), OSY(738,0,1), OB(739.6,0,1,1),
  OT(740.55,6.31,1,2), OT(743.57,6.91,1,2), OB(744.8,0,1,2), OT(746.16,5.94,1,3),
  OT(748.92,8.32,1,3), OSY(750,0,1), OB(751.6,0,1,1), OT(752.18,8.54,1,3),
  OT(754.3,6.49,1,2), OSY(756.8,0,2), OT(757.03,6.35,1,3), OT(759.8,0,1,4),
  OT(762.87,5.88,1,2), OB(763,0,1,2), OT(765.91,5.61,1,2), OSY(768.2,0,3),
  OT(768.96,8.15,1,2), OT(771.74,0,1,3), OT(774.98,0,1,7), OT(777.27,0,1,6),
  OT(780.37,7.82,1,2),
  // ---------------------------------------------------------------
  // S10 782-837 CUBO por el TECHO (y=9)
  // ---------------------------------------------------------------
  OPG(782,4.5,0), OT(783,7.7,1,3), OT(785.45,0,1,8), OT(788.29,0,1,6),
  OB(789,8,1,1), OT(791.41,8.19,1,4), OD(794.2,9,1), OT(794.31,0,1,3),
  OT(796.78,0,1,3), OD(799.4,9,2), OT(804.21,0,1,4), OD(805.6,9,2),
  OT(810.31,0,1,3), OD(811.8,9,1), OB(813.4,8,1,1), OT(813.45,0,1,7),
  OT(816.07,7.46,1,4), OB(818.6,8,2,1), OD(820.6,9,1), OT(820.64,0,1,5),
  OT(823.45,8.01,1,2), OD(825.8,9,3), OT(826.74,0,1,5), OT(831.7,0,1,8),
  OT(834.47,8.01,1,3), OPG(836.5,4.5,1),
  // ---------------------------------------------------------------
  // S11 837-900 Cierre
  // ---------------------------------------------------------------
  OT(838,7.96,1,2), OT(840.74,6.97,1,2), OSY(843,0,3), OT(843.95,7.72,1,2),
  OT(846.45,0,1,3), OT(848.39,6.17,1,2), OB(850.2,0,1,2), OT(850.91,6.98,1,2),
  OT(853.74,0,1,5), OSY(855.4,0,1), OT(855.73,6.55,1,2), OB(857,0,1,1),
  OT(857.91,7.81,1,3), OT(860.97,0,1,6), OSY(862.2,0,1), OB(863.8,0,1,1),
  OT(863.88,7.7,1,2), OT(866.52,5.69,1,4), OT(868.77,0,1,7), OT(871.99,0,1,4),
  OT(874.43,0,1,4), OT(876.59,6.53,1,4), OB(879,0,1,2), OT(879.2,8.26,1,2),
  OL(880,3,2), OT(881.45,0,1,4), OT(884.31,7.49,1,2), OT(886.43,5.87,1,4),
  OT(889.04,0,1,6), OT(891.37,6.75,1,2), OT(893.55,0,1,8), OT(896.11,5.81,1,4),
  OT(898.28,0,1,2),
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
  QX(30), QX(57), QX(85), QX(111), QX(137.5), QX(165), 
  QX(192.5), QX(219), QX(246), QX(273), QX(300), QX(327), 
  QX(354), QX(381), QX(408), QX(435), QX(462), QX(489), 
  QX(516), QX(543), QX(570), QX(597), QX(624), QX(649.5), 
  QX(677), QX(705), QX(731.5), QX(759), QX(786), QX(815), 
  QX(840), QX(867), 
};
static const int JL_CHK_N = (int)(sizeof(JL_CHK) / sizeof(JL_CHK[0]));
