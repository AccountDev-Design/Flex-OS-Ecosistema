// GENERADO por tools/gen_jumper_engine.py desde FlexOS_Ultra/FlexOS_Ultra_AppGames.h (no editar a mano).
// GENERADO por extract_arduino.py desde FlexOS_Ultra/ (no editar)
// ---- FlexOS_Ultra_AppGames.h: gamesEnter
static void gamesEnter(){ jmpEnter(); }
// ---- FlexOS_Ultra_AppGames.h: gamesTick
static void gamesTick(){  jmpTick();  }
// ---- FlexOS_Ultra_AppGames.h: gamesSuspend
static void gamesSuspend(){
  jmpFlushSave();
  if(JG.screen == JS_PLAY) JG.screen = JS_PAUSE;
  jmpClearInput();
}
// ---- FlexOS_Ultra_AppGames.h: gamesResume
static void gamesResume(){
  gLand = true; JG.lastUs = micros(); JG.acc = 0; jNextFrameUs = JG.lastUs;
  if(JG.screen == JS_PAUSE) jmpPauseFrame(); else jmpTick();
}
// ---- FlexOS_Ultra_AppGames.h: gamesCloseApp
static void gamesCloseApp(){ jmpFlushSave(); jmpClearInput(); }
