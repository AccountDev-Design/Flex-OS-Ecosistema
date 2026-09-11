// #############################################################
//  FRAGMENTO ARCHIVADO  ·  testVectorPro()
//  ------------------------------------------------------------
//  ESTO NO COMPILA POR SI SOLO, Y NO PRETENDE HACERLO. Es el bloque
//  que vivia dentro de tests/host/ino_compile.cpp, que incluye
//  FlexOS_Ultra.ino ENTERO como una sola unidad de traduccion: las
//  comprobaciones de aqui tocan estaticas del sketch (gAppW, gLand,
//  T, vecTool, vecHeadBtnCX...) que no existen fuera de el.
//
//  Por eso se guarda como .inc y no como .cpp: para reactivarlo hay
//  que PEGARLO de vuelta en tests/host/ino_compile.cpp, declarar
//  testVectorPro() arriba con las demas y llamarlo desde main(),
//  despues de que FlexOS_Ultra_AppVector.h vuelva a la cadena de
//  modulos del .ino.
//
//  Las pruebas del MOTOR (test_vector.cpp), del QR (test_qr.cpp) y
//  del servidor HTTP (test_httpshare.cpp) si son portables y se
//  compilan con el Makefile de esta carpeta.
// #############################################################

// =============================================================
//  FLEX VECTOR PRO  ·  toque de interfaz, horizontal y traduccion
//  ------------------------------------------------------------
//  Tres cosas que en la placa fallaban y que aqui quedan fijadas:
//
//   1) UN BOTON NO SE PIERDE POR 3 PIXELES. T.tap del sistema exige
//      menos de 16 px Y menos de 550 ms; los botones de la cabecera
//      aceptan levantar el dedo dentro de la holgura, sin tope de
//      tiempo, y valen por donde se PULSO. Las cajas tactiles son de
//      49x49, por encima del minimo de 44x44.
//
//   2) HORIZONTAL. Con el lienzo apaisado la tira de herramientas se
//      pone de pie a la izquierda y el panel se acopla a la derecha:
//      aqui se comprueba que TODO cabe y que no queda zona muerta.
//
//   3) LA TRADUCCION AL PANEL. En horizontal Gfx.h gira 90 grados, asi
//      que la fila fisica es la COLUMNA logica. Si eso se rompe, lo que
//      se ve es una franja de basura -- justo el destello que hubo que
//      perseguir.
// =============================================================
// chk() no formatea. Aqui hace falta decir EN CUAL de las dos
// orientaciones fallo la comprobacion, asi que el texto se arma antes.
static char vpMsg[160];
static const char* vpSay(const char* mod, const char* what){
  snprintf(vpMsg, sizeof(vpMsg), "%s: %s", mod, what);
  return vpMsg;
}

static void testVectorPro(){
  printf("Flex Vector Pro: toque, horizontal y volcado\n");
  int oAW = gAppW, oAH = gAppH;
  bool oLand = gLand, oHosted = gHosted;
  int oState = gState, oApp = gAppId;
  Touch oT = T;

  gState = ST_APP; gAppId = IC_VECTOR;
  gLand = false; gHosted = false;
  gAppW = SCR_W; gAppH = SCR_H;
  vecMenuOn = false; vecPanel = VP_NONE; vecPenElem = -1; vecTool = VT_SELECT;
  vecMsg[0] = 0;

  // --- 1. el toque de interfaz aguanta la deriva del dedo -------------
  int bx = vecHeadBtnCX(0), by = vecHeadBtnCY();
  int ux = 0, uy = 0;
  tDown(bx, by, 1000);
  tMove(bx + 3, by + 2, 1120);
  tUp(1900, false);                       // 900 ms y 3 px: el sistema NO lo da por tap
  chk(!T.tap, "el arnes reproduce el caso real: T.tap viene apagado");
  chk(vecUiTapAt(&ux, &uy), "3 px de deriva y 900 ms siguen siendo un toque de boton");
  chk(ux == bx && uy == by, "el toque vale por donde se PULSO, no por donde acabo");

  tDown(bx, by, 2000);
  tMove(bx + 20, by, 2100);
  tUp(2200, false);
  chk(!vecUiTapAt(&ux, &uy), "20 px ya es un arrastre: no dispara el boton");

  tDown(bx, by, 3000);
  tMove(bx + 4, by + 4, 3050);
  chk(!vecUiTapAt(&ux, &uy), "con el dedo todavia abajo el boton aun no dispara");

  // --- 2. las cajas tactiles: 49x49 y sin solaparse -------------------
  chk(VEC_HBTN_HIT * 2 + 1 >= 44, "la caja tactil de la cabecera llega a 44x44");
  chk(VEC_HBTN_STEP >= VEC_HBTN_HIT * 2, "dos botones contiguos no se solapan");
  bool caben = true;
  for(int i = 0; i < VEC_HBTN_N; i++){
    int cx = vecHeadBtnCX(i), cy = vecHeadBtnCY();
    if(cy - VEC_HBTN_HIT < 0 || cy + VEC_HBTN_HIT >= VEC_HEAD_H) caben = false;
    if(cx - VEC_HBTN_HIT <= 60 || cx + VEC_HBTN_HIT >= VEC_W)    caben = false;
  }
  chk(caben, "las tres cajas caben en la cabecera y no pisan el chevron");

  // Las cuatro esquinas de la caja del menu abren el menu: es lo que
  // demuestra que la zona util es la caja entera y no solo el icono.
  bool esquinas = true;
  for(int dy = -VEC_HBTN_HIT; dy <= VEC_HBTN_HIT; dy += 2 * VEC_HBTN_HIT)
    for(int dx = -VEC_HBTN_HIT; dx <= VEC_HBTN_HIT; dx += 2 * VEC_HBTN_HIT){
      vecMenuOn = false;
      vecHeaderTouch(vecHeadBtnCX(2) + dx, vecHeadBtnCY() + dy);
      if(!vecMenuOn) esquinas = false;
    }
  chk(esquinas, "las cuatro esquinas de la caja del menu abren el menu");
  vecMenuOn = false;

  // Deshacer llega al motor desde la esquina de su caja (documento vacio
  // -> el aviso honesto, que es la prueba de que el toque se despacho).
  vecMsg[0] = 0;
  vecHeaderTouch(vecHeadBtnCX(0) - VEC_HBTN_HIT, vecHeadBtnCY() + VEC_HBTN_HIT);
  chk(!strcmp(vecMsg, "Nada que deshacer"), "deshacer responde en la esquina de su caja");
  vecMsg[0] = 0;

  // Las muestras de relleno y trazo son un atajo al panel que ya existe.
  int hx, hy, hw, hh, hs;
  vecHeadSwatch(&hx, &hy, &hw, &hh, &hs);
  chk(hy >= VEC_HEAD_R1 && hy + hh <= VEC_HEAD_H, "las muestras viven en la franja de control");
  vecPanel = VP_NONE;
  vecHeaderTouch(hx + hw / 2, hy + hh / 2);
  chk(vecPanel == VP_STYLE, "tocar las muestras abre Apariencia");
  vecPanel = VP_NONE;

  // --- 3. la tira de herramientas no tiene zonas muertas --------------
  for(int pasada = 0; pasada < 2; pasada++){
    bool land = (pasada == 1);
    gLand = land; gHosted = land;
    gAppW = land ? 800 : SCR_W;
    gAppH = land ? 480 : SCR_H;
    const char* mod = land ? "horizontal" : "vertical";
    chk(VEC_LAND == land, vpSay(mod, "la orientacion sale del lienzo logico"));

    // Cada celda cabe dentro de la tira y van en orden, sin montarse.
    bool orden = true, dentro = true;
    int prev = -100000;
    for(int i = 0; i < VT_N; i++){
      int tx, ty, st;
      vecToolCell(i, &tx, &ty, &st);
      int c = land ? ty : tx;
      if(c <= prev) orden = false;
      prev = c;
      if(!vecInTools(tx, ty)) dentro = false;
      if(land){ if(ty - st / 2 < VEC_TOP || ty + st / 2 > VEC_BOT) dentro = false; }
      else    { if(tx - st / 2 < 0 || tx + st / 2 > VEC_W)         dentro = false; }
    }
    chk(orden,  vpSay(mod, "las ocho herramientas van en orden"));
    chk(dentro, vpSay(mod, "las ocho caben dentro de la tira"));

    // El centro de cada celda selecciona SU herramienta.
    bool centros = true;
    for(int i = 0; i < VT_N; i++){
      int tx, ty, st;
      vecToolCell(i, &tx, &ty, &st);
      vecTool = VT_SELECT; vecPenElem = -1;
      vecToolsTouch(tx, ty);
      if(vecTool != i) centros = false;
    }
    chk(centros, vpSay(mod, "el centro de cada celda elige su herramienta"));

    // El hueco de la divisoria entre grupos NO es zona muerta.
    int ax, ay, bx2, by2, s2;
    vecToolCell(VEC_TOOL_GROUP - 1, &ax, &ay, &s2);
    vecToolCell(VEC_TOOL_GROUP, &bx2, &by2, &s2);
    vecTool = VT_TEXT; vecPenElem = -1;
    if(land) vecToolsTouch(VEC_TOOL_W / 2, (ay + by2) / 2);
    else     vecToolsTouch((ax + bx2) / 2, VEC_BOT + VEC_TOOL_H / 2);
    chk(vecTool == VEC_TOOL_GROUP - 1 || vecTool == VEC_TOOL_GROUP,
        vpSay(mod, "el hueco de la divisoria cae en una de las dos vecinas"));
    vecTool = VT_SELECT;

    // Un toque en la tira nunca lo ve el lienzo, y al reves.
    chk(!vecInCanvas(4, land ? VEC_TOP + 4 : VEC_BOT + 4),
        vpSay(mod, "la tira de herramientas no es lienzo"));
    chk(vecInCanvas(VEC_LEFT + 4, VEC_TOP + 4), vpSay(mod, "la esquina del lienzo si lo es"));

    // El panel acoplado cabe entero, y no pisa cabecera ni tira.
    int px_, py_, pw_, ph_;
    vecPanelRect(&px_, &py_, &pw_, &ph_);
    chk(px_ >= VEC_LEFT && px_ + pw_ <= VEC_W, vpSay(mod, "el panel cabe a lo ancho"));
    chk(py_ >= VEC_TOP && py_ + ph_ <= VEC_BOT, vpSay(mod, "el panel cabe a lo alto"));
    if(land){
      // Acoplado a la DERECHA: lo que lo define es que su borde derecho
      // esta pegado al margen, no que su izquierda pase de la mitad --
      // el panel conserva su ancho de diseno (456 px), porque los cuatro
      // paneles maquetan su contenido contra el y estrecharlo obligaria
      // a re-maquetarlos.
      chk(px_ + pw_ == VEC_W - 12,
          "horizontal: el panel se acopla al margen derecho, como el dock de Illustrator");
      chk(px_ >= VEC_LEFT + 8, "horizontal: el panel no pisa la tira de herramientas");
    }

    // El menu de la cabecera tampoco se sale de la ventana.
    int mx_, my_, mw_, mh_;
    vecMenuGeom(&mx_, &my_, &mw_, &mh_);
    chk(mx_ >= 0 && mx_ + mw_ <= VEC_W, vpSay(mod, "el menu cabe a lo ancho"));
    chk(my_ >= 0 && my_ + mh_ <= VEC_H - VEC_NAV, vpSay(mod, "el menu cabe a lo alto"));

    // El origen del documento cae en la esquina del LIENZO, no de la
    // pantalla: con la tira de pie a la izquierda son dos sitios distintos.
    vecZoom = 1.0f; vecPanX = 0.0f; vecPanY = 0.0f;
    chk(vecPixX(0.0f) == VEC_LEFT && vecPixY(0.0f) == VEC_TOP,
        vpSay(mod, "el origen del documento cae en la esquina del lienzo"));
    chk(vecDocX(VEC_LEFT) == 0.0f && vecDocY(VEC_TOP) == 0.0f,
        vpSay(mod, "y la vuelta coincide"));
  }

  // --- 4. la traduccion al panel, que es lo que evita la basura -------
  gLand = false; gHosted = false; gAppW = SCR_W; gAppH = SCR_H;
  int qx0, qy0, qx1, qy1;
  vecPhysRect(10, 20, 30, 40, &qx0, &qy0, &qx1, &qy1);
  chk(qx0 == 10 && qy0 == 20 && qx1 == 30 && qy1 == 40,
      "vertical: la traduccion al panel es la identidad");

  gLand = true; gHosted = true; gAppW = 800; gAppH = 480;
  vecPhysRect(100, 64, 199, 200, &qx0, &qy0, &qx1, &qy1);
  chk(qy0 == 100 && qy1 == 199, "horizontal: la FILA fisica es la columna logica");
  chk(qx0 == SCR_W - 1 - 200 && qx1 == SCR_W - 1 - 64,
      "horizontal: la columna fisica sale de la Y logica");
  chk(qx0 >= 0 && qx1 < SCR_W && qy0 >= 0 && qy1 < SCR_H,
      "el rectangulo traducido cae DENTRO del panel");

  // El recorte tiene que usar el que la primitiva mira de verdad: en
  // horizontal putPhys() solo consulta gClipY0/gClipY1, y ahi acota la X
  // LOGICA. Recortar por gClipX0/gClipX1 no haria nada.
  int sx0 = gClipX0, sx1 = gClipX1, sy0 = gClipY0, sy1 = gClipY1;
  vecClipRect(120, 70, 300, 400);
  chk(gClipY0 == 120 && gClipY1 == 300, "horizontal: gClipY0/Y1 acotan la X logica");
  gLand = false;
  vecClipRect(120, 70, 300, 400);
  chk(gClipX0 == 120 && gClipX1 == 300 && gClipY0 == 70 && gClipY1 == 400,
      "vertical: cada eje con su recorte");
  gClipX0 = sx0; gClipX1 = sx1; gClipY0 = sy0; gClipY1 = sy1;

  // --- 5. el barrido cubre el lienzo mas ancho posible ----------------
  chk(VEC_RAS_W >= SCR_W && VEC_RAS_W >= SCR_H,
      "el rasterizador se dimensiona al lado largo: en horizontal el lienzo es mas ancho que el panel");

  gAppW = oAW; gAppH = oAH; gLand = oLand; gHosted = oHosted;
  gState = oState; gAppId = oApp; T = oT;
  vecMenuOn = false; vecPanel = VP_NONE; vecTool = VT_SELECT; vecMsg[0] = 0;
  if(gFails) printf("  %d comprobacion(es) de Flex Vector Pro han fallado.\n", gFails);
  else       printf("  Flex Vector Pro: todas las comprobaciones pasan.\n");
}
