#!/usr/bin/env bash
# Pruebas de host de Flex OS Ultra (ESP-IDF): la logica portable de los
# componentes, compilada para el PC con AddressSanitizer + UBSan y, aparte,
# ThreadSanitizer para las pruebas con hilos. Donde hay una version Arduino de
# la misma logica, se extrae su codigo (ref/extract_arduino.py) y se compara
# el resultado bit a bit.
#   tests/host/run.sh
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..
OUT=${OUT:-build}
mkdir -p "$OUT"

# Referencias extraidas de FlexOS_Ultra/ (solo lectura)
python3 ref/extract_arduino.py "$OUT/ref_wall.inc" \
    FlexOS_Ultra_Gfx.h:rgb565,DIV255,mix565,isqrt32,hLine,hLineA,rowsInClip,fillCircleA,TC,wallGradLut,wallTxLut,wallLutReady,wallpaperEnsureLut \
    FlexOS_Ultra_Wallpaper.h:WLUT_N,WLUT_SHIFT,wlut,WLUT_DMAX,wlutRMax,wlutDisc,wlutRing,wallRadial,wallDisc,wallDiag3,wallCorners4,wallFlexOriginal,wallAurora,wallNocturno,wallHalo,wallOnyx,wallOceano,wallVioleta,wallNaturaleza,lum565,onColor,gWallPalOk,gWallAcc,gWallAcc2,wallPaletteBuild

python3 ref/extract_arduino.py "$OUT/ref_glass.inc" \
    FlexOS_Ultra_Gfx.h:rgb565,isqrt32 \
    FlexOS_Ultra_Theme.h:glassBuf,glLine,un565,pk565,glassLuma,GLB_RMAX,GLB_STRIP,glbRecip,glbSR,glbRing,glassBlur,glInset,GLASS_TINT_DIFF_MAX,GLASS_LVL_DEF,GLASS_LVL_STEP,gGlassLvl,gGlR,gGlSpec,glassLevelApply,gGlMinMix,glassTintMix,glassShadeRow

python3 ref/extract_arduino.py "$OUT/ref_touch.inc" \
    FlexOS_Ultra_Types.h:Touch,SUSPEND_ON,SUSP_TAP_WINDOW_MS,SUSP_TAP_GAP_MS,SUSP_TAP_MAX_MS,SUSP_TAP_FRAMES \
    FlexOS_Ultra_HAL.h:gtFingers,gtFingersMs \
    FlexOS_Ultra_Touch.h:T,gTouchSwallow,gTouchSwallowSeenMs,KIOSK_ON,kioskOn,kioskExX,KIOSK_BADGE_X,KIOSK_BADGE_Y,KIOSK_BADGE_S,KIOSK_EXIT_PAD,kioskInExcluded,kioskInExit,gSuspOn,gTouchPinchUsed,gTouchOwnsTwoFinger,gEpAct,gEpT0,gEpRun2,gEpHad2,gEpHad3,gTap2Ms,gTap1Ms,gSuspSwallow,gTouchHeld,suspGestureUpdate,tDoRelease,flexPollTouch \
    FlexOS_Ultra_AppFramework.h:touchDropAll

python3 ref/extract_arduino.py "$OUT/ref_home.inc" \
    FlexOS_Ultra_Types.h:HOME_WG_MAX,HOME_WG_MAX_V1,HomeWidget,WG_NONE,WgDesc \
    FlexOS_Ultra_Prefs.h:HOME_EMPTY,HOME_PKG_BASE,gAppFav,gAppHidden,gAppLock,homeIsPkg,homePkgSlot,appIsFav,appIsHidden \
    FlexOS_Ultra_Home.h:HOME_LEGACY_PAGES,HOME_PAGES_MAX,HOME_COLS_MAX,HOME_ROWS_MAX,HOME_STRIDE,HOME_TOTAL,HOME_HDR_Y,HOME_HDR_H,HOME_GY0,HOME_ROWSTEP,homeOrder,gHomePage,gHomePageN,gHomeMain,gHomeCols,gHomeIconSz,homeSlotCount,homeIdx,gHomeWg,gHomeWgN,homeGrid,homeDotsY \
    FlexOS_Ultra_Widgets.h:WG_REG,wgRect,homeCellMask,homeHdrMask,homeWgFits,homeWgFreeOfWidgets,homeWgSizeOk,homeWgPlaceOk,homeWgNormalize \
    FlexOS_Ultra_Home.h:homeFirstFree,homePageAppendQuiet,homeFirstFreeGrow,gHomePkgSeen,homePkgSeen,homePkgMarkSeen,homeOrderNormalize \
    FlexOS_Ultra_Prefs.h:appCanHide \
    FlexOS_Ultra_AppDrawer.h:drwFavToggle,drwHideToggle \
    FlexOS_Ultra_Widgets.h:homeWgSpotFor,wgSizeLimits,wgCanResize,homeWgAt,homeWgRemove,homeWgToPage \
    FlexOS_Ultra_Home.h:edSlot,edSlotAt,homeLayoutCellAt,edMove,HOME_BAND_BOT_MAX,homeBandBot

python3 ref/extract_arduino.py "$OUT/ref_recents.inc" \
    FlexOS_Ultra_AppSwitcher.h:SW_MAX,SW_THUMB_MAX,TH_W,TH_H,AppTask,swTasks,swCount,swThumbTrim,captureThumb,swPush,swDropCard

python3 ref/extract_arduino.py "$OUT/ref_drawer.inc" \
    FlexOS_Ultra_DeX.h:dexLower,dexMatch \
    FlexOS_Ultra_PkgApps.h:pkgAppFold,pkgAppNameCmp \
    FlexOS_Ultra_AppDrawer.h:DRW_QMAX,drwCells,drwN,drwAnyPkg,drwQuery,drwQLen,drwShowHid,drwFilter

python3 ref/extract_arduino.py "$OUT/ref_home_load.inc" \
    FlexOS_Ultra_Types.h:HOME_WG_MAX,HOME_WG_MAX_V1,HomeWidget,WG_NONE,WgDesc \
    FlexOS_Ultra_Prefs.h:HOME_EMPTY,HOME_PKG_BASE,gAppFav,gAppHidden,gAppLock,homeIsPkg,homePkgSlot,appIsFav,appIsHidden \
    FlexOS_Ultra_Home.h:HOME_LEGACY_PAGES,HOME_LEGACY_SLOTS,HOME_LEGACY_TOTAL,HOME_PAGES_MAX,HOME_COLS_MAX,HOME_ROWS_MAX,HOME_STRIDE,HOME_TOTAL,HOME_HDR_Y,HOME_HDR_H,HOME_GY0,HOME_ROWSTEP,APPREG_VER,HOME_FACTORY,APPREG_V1_N,APPREG_MAP_V1,APPREG_V2_N,APPREG_MAP_V2,homeOrder,gHomePage,gHomePageN,gHomeMain,gHomeCols,gHomeIconSz,gHomeLabels,gHomeLocked,gHomeDots,gHomePinch,gHomeReduce,homeSlotCount,homeIdx,gHomeWg,gHomeWgN,HOME_WG_BLOB,HOME_WG_BLOB_V1,homeGrid \
    FlexOS_Ultra_Widgets.h:WG_REG,wgRect,homeCellMask,homeHdrMask,homeWgFits,homeWgFreeOfWidgets,homeWgSizeOk,homeWgPlaceOk,homeWgNormalize,homeWgFactory,homeWgSerialize,homeWgParse,homeWgDeserialize,homeWgDeserializeV1 \
    FlexOS_Ultra_AppFramework.h:drawerRegistryDefaults,drawerRegistryAdopt \
    FlexOS_Ultra_Home.h:homeFirstFree,homePageAppendQuiet,homeFirstFreeGrow,gHomePkgSeen,homePkgSeen,homePkgMarkSeen,homeOrderNormalize,homeOrderLoad

# Teclado del sistema: en dos partes porque mapaActivo (puntero a fila) lo
# declara a mano ref_kb_main.cpp entre las dos.
python3 ref/extract_arduino.py "$OUT/ref_kb_a.inc" \
    FlexOS_Ultra_Types.h:KB_SIZE_CONFIG_ON,KB_MULTITOUCH_ON,KB_TOOLBAR_ON,KB_SETTINGS_ON,KB_AUTOCOMPLETE_ON,KB_ANIM_POLISH_ON \
    FlexOS_Ultra_Prefs.h:KB_SIZE_COMPACT,KB_SIZE_NORMAL,KB_SIZE_BIG,gKbSize,gKbFastType,gKbToolbar,gKbPredict,gKbSpell,gKbEmojiSug,gKbHiCon,gKbOpacity,gKbStyle,gKbFontSc,gKbLpMs,gKbFxMs,KB_SYMS,gKbSym,KB_SC_MAX,KB_SC_ABR,KB_SC_EXP,gKbScAbr,gKbScExp,kbShortcutsDefaults,kbPrefsNormalize \
    FlexOS_Ultra_Keyboard.h:KB_COLS,KB_ROWS,LAYOUT_ES,LAYOUT_EN,LAYOUT_NUM,LAYOUT_EMOJI
python3 ref/extract_arduino.py "$OUT/ref_kb_b.inc" \
    FlexOS_Ultra_Keyboard.h:kbShift,kbResolveKey,kbKW,kbApplySize,KB_KW,KB_KH,KB_GAP,KB_X,kbExtrasOn,kbToolbarH,kbChipsWant,kbChipsH,kbTopH,kbBotReserve,kbRowsTop,KB_Y,kbPanelTop,kbToolbarY,kbChipsY,kbFuncY,KB_FKEYS,KB_FW,kbFKeyW,kbFKeyX,kbFRowHit,kbSizeCheck,kbFontSize,kbFontDy,kbRadius,kbCellAt,KB_SYM_POOL_N,KB_SYM_POOL,kbSymAt,kbFxCell,kbFxT0,kbFxStart,kbFxActive,kbFxLevel,KB_DICT_ES,KB_DICT_EN,KB_DICT_ES_N,KB_DICT_EN_N,kbFoldCh,kbStartsWith,kbSameWord,kbDictHas,KB_EMOSUG_N,KB_EMOSUG_W,KB_EMOSUG_E,kbSuggest,kbCurrentWord,utf8Prev,kbGetVariants,kbIsVowelCell,kbLayerLabel \
    FlexOS_Ultra_Lock.h:lsuPin \
    FlexOS_Ultra_Power.h:utf8Count,lsuPassAppend \
    FlexOS_Ultra_KeyboardSettings.h:kbsScField,kbsScA,kbsAppendField,kbsBackField

# Panel rapido: en dos partes porque qpCtlAvail (la disponibilidad) la fija la
# prueba entre las dos (ref_qs_main.cpp).
python3 ref/extract_arduino.py "$OUT/ref_qs_a.inc" \
    FlexOS_Ultra_Types.h:QsCtl,QpItem \
    FlexOS_Ultra_QuickPanel.h:QP_MX,QP_CONT_W,QP_GAP,QP_CW,qpSpanW,qpColX,QP_HDR_H,QP_FOOT_H,QP_VIEW_Y0,QP_VIEW_Y1,QP_VIEW_H,QP_RH1,QP_RH2,QP_VGAP,QP_GPAD,QP_TROW,QP_TCIRC,QP_HANDLE_H,QP_GROWS_MIN,QP_GROWS_MAX,qpGroupH,QSID_WIFI,QT_TOGGLE,QSZ_1x1,QSZ_2x1,QSZ_4x1,QSZ_2x2,QOR_H,QOR_V,QCAT_CONN,QP_CAT_NAME,QS_REG,qpCtl
python3 ref/extract_arduino.py "$OUT/ref_qs_b.inc" \
    FlexOS_Ultra_QuickPanel.h:qpCtlShown,qpFirstSize,qpSizeAllowed,qpNextSize,QP_CFG_VER,QP_MAX_ITEMS,QP_BLOB_N,QP_NVS_NS,QP_NVS_KEY,qpIt,qpN,qpGrows,qpLoaded,qpEdIt,qpEdN,qpEdGrows,QpDef,QP_FACTORY,QP_FACTORY_N,qpFactory,qpNormalize,qpAdoptNew,qpSerialize,qpDeserialize,qpLoad,QB_ITEM,QpBlock,qpBlk,qpBlkN,qpTiles,qpTileN,qpContentH,qpGroupBlk,qpLaySrc,qpLayN,qpLayGroupPx,qpLayEdit,qpTotalRows,qpGroupInnerH,qpGroupMinPx,qpGroupMaxPx,qpLayout,QP_TCOLW,qpTileCenter,QPM_PANEL,qpMode,qpScrollF,qpGH,qpGScrollF,qpGAnim,qpGFrom,qpGT0,qpEdRejectF,qpEdRejectMs,qpCatIds,qpCatN \
    FlexOS_Ultra_QuickPanelGlass.h:qpRelayout,qpScrollMax,qpGroupSnap,qpCatBuild \
    FlexOS_Ultra_QuickPanelEdit.h:qpEditRemove,qpEditMove,qpEditAdd

# Avisos: modelo de la isla (historial) y cola del banner
python3 ref/extract_arduino.py "$OUT/ref_ntf.inc" \
    FlexOS_Ultra_Types.h:DetectedModule,NOTIF_MAX,Notification,gNotifs,gNotifCount \
    FlexOS_Ultra_Notif.h:notifKeyOf,notifFindKey,notifRemove,notifPush \
    FlexOS_FlexPhone_Overlay.h:FPN_SRC_PHONE,FPB_HIDDEN,FPB_QUEUE,fpbState,fpbRefresh,fpbCur,fpbQueue,fpbQueueN,fpbMore,fpbRank,fpbEnqueue,fpbSameSys,fpbOffer,fpbMsgInit,fpbPush,fpbPushSystemKeyed \
    FlexOS_Ultra_Media.h:sysNotify,mediaNotify

python3 ref/extract_arduino.py "$OUT/ref_homecfg.inc" \
    FlexOS_Ultra_Types.h:HOME_WG_MAX,HOME_WG_MAX_V1,HomeWidget,WG_NONE,WgDesc \
    FlexOS_Ultra_Prefs.h:HOME_EMPTY,HOME_PKG_BASE,gAppFav,gAppHidden,gAppLock,homeIsPkg,homePkgSlot,appIsFav,appIsHidden \
    FlexOS_Ultra_Gfx.h:isqrt32 \
    FlexOS_Ultra_Home.h:HOME_LEGACY_PAGES,HOME_PAGES_MAX,HOME_COLS_MAX,HOME_ROWS_MAX,HOME_STRIDE,HOME_TOTAL,HOME_HDR_Y,HOME_HDR_H,HOME_GY0,HOME_ROWSTEP,homeOrder,gHomePage,gHomePageN,gHomeMain,gHomeCols,gHomeIconSz,homeSlotCount,homeIdx,gHomeWg,gHomeWgN,homeGrid,homeDotsY \
    FlexOS_Ultra_Widgets.h:WG_REG,wgRect,homeCellMask,homeHdrMask,homeWgFits,homeWgFreeOfWidgets,homeWgSizeOk,homeWgPlaceOk,homeWgNormalize,homeWgFactory,homeWgSpotFor,homeWgAdd \
    FlexOS_Ultra_AppFramework.h:drawerRegistryDefaults \
    FlexOS_Ultra_Home.h:homeFirstFree,homePageAppendQuiet,homeFirstFreeGrow,gHomePkgSeen,homePkgSeen,homePkgMarkSeen,homeOrderNormalize \
    FlexOS_Ultra_HomeCfg.h:HC_PICK_MAX,hcPickPath,hcPickN,homePageItems,homePageAdd,homePageDelete,homePageSwap,homeSetMain,homeSetGrid,homeResetLayout,HC_GRID_OPTS,HC_GRID_C,HC_GRID_R,hcGridIdx,hcScanImages,HPZ_MIN_START_D2,HPZ_MIN_OPEN_D2,HPZ_MIN_MS,HPZ_MAX_MS,HPZ_ABS_PX,hpzState,hpzIdA,hpzD2Ref,hpzT0,hpzSwallow,hpzSwallowing,hpzReset,hpzUpdate

python3 ref/extract_arduino.py "$OUT/ref_session.inc" \
    FlexOS_Ultra_Types.h:SessHdr \
    FlexOS_Ultra_Session.h:SESS_MAGIC,SESS_IO_MAX,gSessIo,crc32u,sessWrite,sessRead \
    FlexOS_Ultra_AppFramework.h:SESS_IDLE_MS,SESS_MAXWAIT_MS,gAppState,gAppSeenMs,gSessDirtyApp,gSessDirtyMs,appHooks,gSessNeedSave,appSaveSession,sessMarkDirty,sessFlushNow,sessAutosaveTick,appTerminate

python3 ref/extract_arduino.py "$OUT/ref_mem.inc" \
    FlexOS_Ultra_AppFramework.h:gAppState,gAppSeenMs,appHooks,appBgBusy \
    FlexOS_Ultra_Core.h:gAppMem,gAppMemKnown,gAppShed,appMemCommit,appMemReleased,appMemForget,appMemHas,appMemBytes,memShedApp,memShedAll,memAdmitApp,memAlertTick,appEnforceMemoryBudget

python3 ref/extract_arduino.py "$OUT/ref_boot.inc" \
    FlexOS_Ultra_Home.h:resetReasonStr

python3 ref/extract_arduino.py "$OUT/ref_calc.inc" \
    FlexOS_Ultra_AppsBasic.h:calcDisp,calcAcc,calcOp,calcFresh,calcErr,calcFinite,calcFmtTo,calcFmt,calcLine,calcCompute,calcKey,calcKeyFromLabel,CalcSessV1,CALC_SESS_VER,CALC_SESS_PATH,calcSaveSess,calcLoadSess

python3 ref/extract_arduino.py "$OUT/ref_fsn.inc" \
    FlexOS_FS.h:FLEXFS_PATH_MAX,FLEXFS_NAME_MAX,FLEXFS_DIR_TRASH \
    FlexOS_FS.cpp:baseName,joinPath,parentDir,extOf,encodeTrashName,flexFsTrashOrigin,flexFsStem,flexFsFmtSize,flexFsRename,flexFsTrash,flexFsRestore,flexFsNewName

python3 ref/extract_arduino.py "$OUT/ref_fk.inc" \
    FlexOS_FS.h:FLEXFS_NAME_MAX \
    FlexOS_Ultra_FileKit.h:FK_MENU_W,FK_MENU_RH,FK_MENU_PAD,fkMenuCloud,fkMenuRows,fkMenuOn,fkMenuX,fkMenuGeom,fkMenuHit,fkAskOn,fkAskGeom,fkAskTick,FK_TRASH_TOP,FK_TRASH_RH,fkTrashN,fkTrashMaxScroll,fkTextBox,fkNameBuf,fkNameAppend,fkNameBack

python3 ref/extract_arduino.py "$OUT/ref_ned.inc" \
    FlexOS_Ultra_Font.h:utf8Decode,cpZeroWidth,cpFold,nextCP \
    FlexOS_Ultra_Keyboard.h:kbCurrentWord,noteCur,noteSelA,noteMenu,noteEditorScroll,noteEditorCursorFollow,kbToastMs,kbToastTxt,utf8Prev,utf8Next,isWordByte,noteHasSel,noteClearSel,noteDeleteSel,noteInsert,noteBackspace,selectWordAt,selectAllTxt,noteTxtBot,curSX,NOTE_LH,NOTE_TOP,noteEditorWalk,noteEditorMaxScroll,noteEditorClampScroll,noteEditorEnsureCursor,noteDrawText,noteLayoutHit,noteMenuHit

python3 ref/extract_arduino.py "$OUT/ref_clip.inc" \
    FlexOS_Ultra_Keyboard.h:clipboard,clipSavePinned,clipLoadPinned,clipCount,clipPush,clipDel,clipTogglePin,clipClearUnpinned

python3 ref/extract_arduino.py "$OUT/ref_notes.inc" \
    FlexOS_FS.h:FLEXFS_PATH_MAX \
    FlexOS_Ultra_AppNotes.h:NOTE_COLS,NOTE_CARD_TOP,noteListN,noteView,noteScroll,notePath,noteCardH,noteCardRect,noteMaxScroll,noteFabRect,NOTE_SESS_VER,NOTE_SESS_PATH,NoteSessV3,gNoteSess,noteSaveSess,noteLoadSess

python3 ref/extract_arduino.py "$OUT/ref_paint.inc" \
    FlexOS_FS.h:FLEXFS_PATH_MAX,FLEXPAINT_MAGIC0,FLEXPAINT_MAGIC1,FLEXPAINT_MAGIC2,FLEXPAINT_MAGIC3,FlexPaintHdr,FlexPaintStroke \
    FlexOS_FS.cpp:paintReadHdr,flexPaintCreate,flexPaintHeader,flexPaintAppend,flexPaintReplay,flexPaintClear,flexPaintUndo \
    FlexOS_Ultra_AppPaint.h:P_TOP,P_BOT,PAINT_CX,PAINT_CW,PAINT_CH,PAINT_MAX_PTS,PAINT_COLS,PAINT_CARD_TOP,P_PAL,P_SIZES,paintView,paintListN,paintScroll,paintPath,pColor,pSizeIx,pStroke,pStrokeN,pPrevX,pDocW,pDocX,pDocDispW,pDocScale,paintDocLayout,paintDocHit,paintDocCoord,paintDocRadius,paintFlushStroke,paintCanvasTick,paintCardRect,paintMaxScroll,paintFabRect,PAINT_SESS_VER,PAINT_SESS_PATH,PaintSessV1,paintSaveSess,paintLoadSess

python3 ref/extract_arduino.py "$OUT/ref_raster.inc" \
    FlexOS_Ultra_Gfx.h:DIV255,mix565,px,pxA,hLine,AA_EPS,aaClipBox,fillCircleAA,strokeSegAA

python3 ref/extract_arduino.py "$OUT/ref_set.inc" \
    FlexOS_Ultra_Prefs.h:AUTOLOCK_NOPT,AUTOLOCK_OPTS,AUTOLOCK_NAMES,AUTOLOCK_DEFAULT_IDX,AUTOLOCK_DEFAULT_MS,gAutoLockMs,autoLockIdx,autoLockName,autoLockNormalize,LW_CLOCK,LW_WEATHER,LW_CAL,LW_NOTIF \
    FlexOS_Ultra_Session.h:NLANG,LANG_ENDONYM \
    FlexOS_Ultra_AppSettings.h:RI_GLOBE,SET_CARD_X,SET_CARD_W,DP_X,DP_W,SET_LIST_TOP,SET_LIST_BOT,DLIST_TOP,DLIST_BOT,PAGE_BG,SET_CARD_BG,SET_CARD_GLASS,SET_TXT_HI,SET_TXT_LO,SET_TXT_MUTE,SET_CHEV,SET_SIDE_SUB,SET_NAVPILL,SET_CATCARD_H,SET_CATCARD_GAP,SET_BACK_W,SET_ROW_MAX,SET_SESS_VER,SET_SESS_PATH,SET_CAT,SET_SUB,SET_DESC,SET_ACCENT,setSel,setListScroll,setRowY0,setCatY0,langNameCur,settingsDateTimeStr,buildUptime,setRowCard,drawInfoLine,drawDeviceInfo,settingsDetailContent,settingsDrawListHead,settingsListContent,settingsDrawDetailHead,setBandTop,setBandBot,setMaxScroll,settingsRenderDetailOnly,settingsRowAction,SetSessV1,setSaveSess,setLoadSess

python3 ref/extract_arduino.py "$OUT/ref_kbs.inc" \
    FlexOS_Ultra_Prefs.h:KB_SIZE_COMPACT,KB_SIZE_NORMAL,KB_SIZE_BIG,gKbSize,gKbFastType,gKbToolbar,gKbPredict,gKbSpell,gKbEmojiSug,gKbHiCon,gKbOpacity,gKbStyle,gKbFontSc,gKbLpMs,gKbFxMs,KB_SYMS,gKbSym,KB_SC_MAX,KB_SC_ABR,KB_SC_EXP,gKbScAbr,gKbScExp,kbShortcutsDefaults \
    FlexOS_Ultra_Keyboard.h:KB_SYM_POOL_N,KB_SYM_POOL,kbSymAt \
    FlexOS_Ultra_KeyboardSettings.h:KBS_MAIN,KBS_DESIGN,KBS_TOUCH,KBS_SYMS,KBS_SHORT,KBS_SCEDIT,KBS_ABOUT,KBS_LANG,KBS_ROW_MAX,kbsPage,kbsScroll,kbsRowY0,kbsRet,kbsScSel,kbsScField,kbsScA,kbsDragY0,kbsDragging,KBS_TOP,KBS_BOT,kbsBg,kbsCard,kbsRow,kbsSection,kbsSizeName,kbsStyleName,kbsFontName,kbsFxName,kbsOnOff,kbsAppendField,kbsBackField,kbsContent,kbsTitle,kbsGo,kbsResetDefaults,kbsRowAction,kbsTick

python3 ref/extract_arduino.py "$OUT/ref_fsscan.inc" \
    FlexOS_FS.cpp:baseName,joinPath,flexFsCount,dirSizeRec,flexFsDirSize,cmpEntry,flexFsList,flexFsCatSize,bigWalk,flexFsLargest

python3 ref/extract_arduino.py "$OUT/ref_stor_ui.inc" \
    FlexOS_Ultra_AppFramework.h:uiBox,uiW,uiH,uiTop,uiPad,uiGap,uiFontFit,uiFontH,uiLineH,uiTitle

python3 ref/extract_arduino.py "$OUT/ref_stor.inc" \
    FlexOS_Ultra_AppStorage.h:ALM_BIG_MAX,almBig,almBigN,almCat,almVerY0,ALM_CAT_NAME,ALM_CAT_COL,simpBar,almScan,almFolderIcon,ALM_SCR_MAIN,almScreen,almPhY0,almDetY0,almOptX0,almPhoneState,almPhoneQuota,almPhoneCard,almRenderMain,ALMD_VY0,ALMD_VY1,ALMD_SEC_N,ALMD_SEC_H,almDetScroll,almDetSig,almDetMs,almDetDrag,almDetTouching,almDetStartY,ALMD_BW,ALMD_BX,ALMD_BH,almDetSecTop,almDetContentH,almDetMaxScroll,almDetRow,almDetNote,almDetTitle,almBootCause,almTopApps,almDetSecSig,almDetDrawSection,almDetailTick,almTick

python3 ref/extract_arduino.py "$OUT/ref_dc.inc" \
    FlexOS_Ultra_DeviceCare.h:DCS_APPTITLE,DCH,dct,DC_HIST_MAGIC,DC_HIST_VER,DC_HIST_MAX,DC_HIST_PATH,DC_EV_DIAG,DcRec,DcHistFile,dcHist,dcHistN,dcHistDirty,dcHistLoaded,dcLastScore,dcLastCheck,dcHistLoad,dcHistSave,dcHistAdd,dcBootAbnormal,dcBootReason,DCH_MEM,DCL_OK,DcMetric,dcMetricSet,dcCollect,dcScoreFrom,dcLevelColor,dcScoreColor,dcScoreWord,dcHistContentH,DC_HIT_MAX,DcHit,dcHits,dcHitN,dcHitClear,dcHitAdd,dcHitTest,dcFallOn,DCI_STATUS,dcGlyphPhone,dcSectionIcon,dcBtnGlyphPlay,dcBtnGlyphRetry,dcBtnGlyphCheck,dcBtnGlyphPower,dcScoreRing,dcRowLed,DCB_PLAY,dcButton,DCH_HERO,dcHomeScore,dcHomeScoreOk,DcGrid,dcGrid,dcHomeLayout,dcDrawTile,DC_RING_MS,dcHomeCard,dcHomeCardGeom,dcRenderHome,DCH_RUNDIAG,dcRenderStatus,dcHistIcon,dcRenderHist \
    FlexOS_Ultra_DeviceTests.h:DG_SYS,DC_DIAG_STEP_MS,dcDiagStage,dcDiagPost,dcResSys,dcResSysOk,dcResImuMask,dcResImuAvail,dcResScreen,dcResTouch,dcResFinal,dcResFinalOk,DcSysRow,DC_SYS_ROWS,dcSysRow,dcSysSet,dcRunSystemTest,dcRunImuTest,dcResultCompute,DCP_BLACK,dcScrStep,DCH_SCR_YES,dcScrTestRender,DC_TT_COLS,DC_TT_ROWS,dcTtCell,dcTtHit,dcTtLastX,dcTouchTestGeom,dcTouchTestRender,dcDiagRender,dcResultRender,dcHealthRender,dcOptimRender
python3 ref/extract_arduino.py "$OUT/ref_dc_ui.inc" \
    FlexOS_Ultra_AppFramework.h:uiBox,uiW,uiH,uiTop,uiPad,uiGap,uiFontFit,uiFontH,uiLineH
python3 ref/extract_arduino.py "$OUT/ref_dc_clk.inc" \
    FlexOS_Ultra_Clock.h:clkCivilFromDays FlexOS_Ultra_Session.h:MO_SHORT

python3 ref/extract_arduino.py "$OUT/ref_files_fs.inc" \
    FlexOS_FS.cpp:baseName,flexFsStem

python3 ref/extract_arduino.py "$OUT/ref_files_inc.inc" \
    FlexOS_Ultra_AppFiles.h:FILES_MAX,FILES_SEG_Y,FILES_SEG_H,FILES_TOP,FILES_VP_TOP,FILES_RH,filesList,filesN,filesDir,filesSelIdx,filesScroll,filesDragY0,filesDragging,filesMulti,filesMask,filesCloud,filesCkBox,filesCkOpen,filesCkHost,filesDrawSeg,filesSegHit,filesIsLibraryDir,filesReload,filesPathOf,filesHasUp,filesRowY,filesMaxScroll,filesRender,filesGoUp,filesOpenEntry,filesMenuAction,filesCkLeave,filesCloudTick,filesTick

python3 ref/extract_arduino.py "$OUT/ref_layout.inc" \
    FlexOS_Ultra_Clock.h:daysInMonth \
    FlexOS_Ultra_AppFramework.h:uiBox,uiW,uiH,uiTop,uiPad,uiGap,uiFontFit,uiFontH,uiLineH

python3 ref/extract_arduino.py "$OUT/ref_chrono_t.inc" \
    FlexOS_Ultra_Types.h:CRONO_MAX_LAPS,CronoLap,gCronoSt,gCronoAccum,gCronoT0,gCronoLapBase,gCronoLaps,gCronoNLaps,gCronoLap0,gCronoBest,gCronoWorst,gCronoLapsDirty,gCronoBtnDirty,gCronoDialDirty,gCronoBarDirty,gRelojTab,gCronoCapX,gCronoCapWDrawn,gCronoCapSec,gCronoCapStDrawn,gCronoCapOn,gCronoCard,gCronoCardT0,gCronoCardBg

python3 ref/extract_arduino.py "$OUT/ref_chrono_ui.inc" \
    FlexOS_Ultra_AppFramework.h:uiBox,uiW,uiH,uiTop,uiPad,uiGap,uiFontFit,uiFontH,uiLineH,UI_FADE_MS,UI_SEC_MAX,UiSec,uiSecs,uiSecN,uiFading,uiSection,RELOJ_TABS_H,RELOJ_TAB_N,relojBody,relojTabsVisible,relojDrawTabs,relojTabsTouch,appRelojHoraRender

python3 ref/extract_arduino.py "$OUT/ref_chrono.inc" \
    FlexOS_Ultra_AppChrono.h:CRONO_CAP_Y,CRONO_CAP_H,CRONO_CAP_ICON,CRONO_CAP_PADL,CRONO_CAP_GAP,CRONO_CAP_PADR,CRONO_CARD_X,CRONO_CARD_W,CRONO_CARD_Y,CRONO_CARD_H,CRONO_CARD_RAD,CRONO_CARD_ANIM,CRONO_BAND_T,CRONO_BAND_B,CRONO_BAND_H,CRONO_DYN_T,CRONO_DYN_B,CRONO_DYN_H,CRONO_ACCENT,CRONO_DISC,CRONO_CARDBG,CRONO_ONACC,cronoElapsed,cronoLapElapsed,cronoActive,cronoCurLapNo,cronoFmt,cronoRecalcExtremes,cronoStart,cronoPause,cronoReset,cronoLapMark,cronoActionLeft,cronoActionRight,cronoLabelLeft,cronoLabelRight,cronoEase,cronoLerp,cronoGlyph,cronoCapsuleW,cronoCapsuleRight,cronoCapsuleDraw,cronoBarClock,cronoBarSurface,cronoCapsuleTick,CronoLayout,gCL,cronoLayout,cronoFitBig,cronoDrawDial,cronoDrawLaps,cronoDrawButtons,cronoTabTouch,cronoCardDynText,cronoCardStatic,cronoCardCompose,cronoOverlayTouch

SRCS=(test_main.c test_flex_kv.c test_misc.c test_wallpaper.c test_glass.c test_home.c test_touch_arb.c test_drawer.c test_kb.c test_recents.c test_qs.c test_notif.c test_poweroff.c test_safeboot.c test_reset.c test_oobe.c test_kiosk.c test_homecfg.c test_session.c test_mem_model.c test_boot.c test_calc.c test_layout.c test_fsnames.c test_fsops.c test_fk.c test_ned.c test_clip.c test_notes.c test_paint.c test_raster.c test_set.c test_kbs.c test_chrono.c test_fsscan.c test_stor.c test_files.c test_jumper.c test_dc.c
      $ROOT/components/flex_storage/src/flex_kv.c
      $ROOT/components/flex_storage/src/flex_fs_path.c
      $ROOT/components/flex_storage/src/flex_fs_wipe.c
      $ROOT/components/flex_storage/src/flex_fs_names.c
      $ROOT/components/flex_storage/src/flex_fs_ops.c
      $ROOT/components/flex_ui/src/theme/flex_wallpaper.c
      $ROOT/components/flex_ui/src/theme/flex_glass_math.c
      $ROOT/components/flex_ui/src/shell/flex_home_model.c
      $ROOT/components/flex_ui/src/shell/flex_session_model.c
      $ROOT/components/flex_ui/src/shell/flex_mem_model.c
      $ROOT/components/flex_ui/src/shell/flex_boot_model.c
      $ROOT/components/flex_ui/src/apps/flex_calc_model.c
      $ROOT/components/flex_ui/src/apps/flex_cal_model.c
      $ROOT/components/flex_ui/src/apps/flex_ned.c
      $ROOT/components/flex_ui/src/apps/flex_note_model.c
      $ROOT/components/flex_ui/src/apps/flex_paint_model.c
      $ROOT/components/flex_ui/src/widgets/flex_raster.c
      $ROOT/components/flex_ui/src/apps/flex_set_model.c
      $ROOT/components/flex_ui/src/apps/flex_kbs_model.c
      $ROOT/components/flex_ui/src/apps/flex_chrono_model.c
      $ROOT/components/flex_ui/src/apps/flex_stor_model.c
      $ROOT/components/flex_ui/src/apps/flex_files_model.c
      $ROOT/components/flex_ui/src/apps/flex_dc_model.c
      $ROOT/components/flex_ui/src/apps/flex_dc_screens.c
      $ROOT/components/flex_ui/src/i18n/flex_i18n_data.c
      $ROOT/components/flex_ui/src/widgets/flex_layout.c
      $ROOT/components/flex_ui/src/widgets/flex_clip.c
      $ROOT/components/flex_ui/src/widgets/flex_fk_model.c
      $ROOT/components/flex_ui/src/shell/flex_drawer_model.c
      $ROOT/components/flex_ui/src/shell/flex_recents_model.c
      $ROOT/components/flex_ui/src/shell/flex_qs_model.c
      $ROOT/components/flex_ui/src/shell/flex_notif_model.c
      $ROOT/components/flex_ui/src/shell/flex_oobe_model.c
      $ROOT/components/flex_ui/src/shell/flex_kiosk_model.c
      $ROOT/components/flex_power/src/flex_poweroff_model.c
      $ROOT/components/flex_system/src/flex_safeboot_model.c
      $ROOT/components/flex_system/src/flex_reset_model.c
      $ROOT/components/flex_ui/src/widgets/flex_kb_layout.c
      $ROOT/components/flex_touch/src/flex_touch_arb.c
      stub_cfg.c)
INC=(-I. -I$ROOT/components/flex_games/include -I$ROOT/components/flex_ui/src/i18n -I$ROOT/components/flex_storage/include -I$ROOT/components/flex_storage/src
     -I$ROOT/components/flex_display/src -I$ROOT/components/flex_ui/src/theme
     -I$ROOT/components/flex_ui/src/shell -I$ROOT/components/flex_ui/src/apps -I$ROOT/components/flex_portable/include -I$ROOT/components/flex_ui/src/widgets -I$ROOT/components/flex_touch/include -I$ROOT/components/flex_power/include -I$ROOT/components/flex_system/include -I../../sim/stubs/include)
CFLAGS=(-O1 -g -Wall -Wextra -Werror -pthread -ffp-contract=off)

build() {   # $1 = sufijo, resto = flags de sanitizador
    local sfx=$1; shift
    local refs=()
    for r in wall glass home home_load touch drawer kb recents qs ntf homecfg session mem boot calc layout fsnames fk ned clip notes paint raster set kbs chrono fsscan stor files dc; do
        c++ -std=gnu++17 -O1 -g -w "$@" -Iref -I"$OUT" -I$ROOT/components/flex_portable/include -I$ROOT/components/flex_ui/src/apps -I$ROOT/components/flex_ui/src/widgets -I$ROOT/components/flex_ui/src/theme -I$ROOT/components/flex_storage/include -I../../sim/stubs/include -ffp-contract=off -c ref/ref_${r}_main.cpp -o "$OUT/ref_${r}_$sfx.o"
        refs+=("$OUT/ref_${r}_$sfx.o")
    done
    c++ -std=gnu++17 -O1 -g -w "$@" -I$ROOT/components/flex_portable/include -c $ROOT/components/flex_portable/src/FlexOS_FlexLink.cpp -o "$OUT/flexlink_$sfx.o"
    refs+=("$OUT/flexlink_$sfx.o")
    c++ -std=gnu++17 -O1 -g -w "$@" -I$ROOT/components/flex_portable/include -c $ROOT/components/flex_portable/src/FlexOS_Mem.cpp -o "$OUT/flexmem_$sfx.o"
    c++ -std=gnu++17 -O1 -g -w "$@" -Iref -I$ROOT/components/flex_portable/include -c ref/test_mem_core.cpp -o "$OUT/testmemcore_$sfx.o"
    c++ -std=gnu++17 -O1 -g -w "$@" -I$ROOT/components/flex_portable/include -c $ROOT/components/flex_portable/src/FlexOS_Media.cpp -o "$OUT/flexmedia_$sfx.o"
    # Jumper de Arduino tal cual sobre el motor de dibujo de Arduino extraido (components/flex_games)
    c++ -std=gnu++17 -O1 -g -w "$@" -I$ROOT/components/flex_games/include -I$ROOT/components/flex_games/src -I$ROOT/components/flex_storage/include -I../../sim/stubs/include -c $ROOT/components/flex_games/src/flex_jumper.cpp -o "$OUT/flexjumper_$sfx.o"
    refs+=("$OUT/flexmem_$sfx.o" "$OUT/testmemcore_$sfx.o" "$OUT/flexmedia_$sfx.o" "$OUT/flexjumper_$sfx.o")
    cc -std=gnu11 "${CFLAGS[@]}" "$@" "${INC[@]}" "${SRCS[@]}" "${refs[@]}" -lstdc++ -lm -o "$OUT/host_$sfx"
}
build asan -fsanitize=address,undefined -fno-sanitize-recover=all
"$OUT/host_asan"
build tsan -fsanitize=thread
"$OUT/host_tsan"

# Clave del sistema: la implementacion de ESP-IDF y la de Arduino
# (FlexOS_Ultra/FlexOS_Passcode.cpp, solo lectura) sobre la MISMA NVS en memoria
# (el doble de Preferences del arnes Arduino): lo que guarda una lo abre la otra.
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all)

# Codigo de ARRANQUE del firmware tal cual (no el modelo), sobre una NVS falsa
# que separa la cache de ajustes de lo grabado: un corte entre arranques solo
# conserva lo grabado.
FWINC=(-Ifw/include -I$ROOT/components/flex_system/include -I$ROOT/components/flex_storage/include
       -I$ROOT/components/flex_core/include -I../../sim/stubs/include)
cc -std=gnu11 "${CFLAGS[@]}" "${SAN[@]}" "${FWINC[@]}" test_boot_fw.c \
    $ROOT/components/flex_system/src/flex_safeboot.c $ROOT/components/flex_system/src/flex_safeboot_model.c \
    -o "$OUT/boot_fw_asan"
"$OUT/boot_fw_asan"
PINC=(-I$ROOT/components/flex_security/include -I$ROOT/components/flex_storage/include -I../../sim/stubs/include)
cc -std=gnu11 "${CFLAGS[@]}" "${SAN[@]}" -DFLEX_HOST_TEST "${PINC[@]}" \
    -c $ROOT/components/flex_security/src/flex_passcode.c -o "$OUT/flex_passcode_asan.o"
c++ -std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-format-truncation -DARDUINO=200 "${SAN[@]}" \
    -I$ROOT/../tests/host/inostub -I$ROOT/../FlexOS_Ultra "${PINC[@]}" \
    test_passcode.cpp $ROOT/../FlexOS_Ultra/FlexOS_Passcode.cpp "$OUT/flex_passcode_asan.o" -lcrypto \
    -o "$OUT/passcode_asan"
"$OUT/passcode_asan"
