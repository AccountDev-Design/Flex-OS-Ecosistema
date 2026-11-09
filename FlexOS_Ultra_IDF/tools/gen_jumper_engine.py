#!/usr/bin/env python3
"""Genera components/flex_games/src/jumper_engine.inc con el MOTOR DE DIBUJO de Arduino que usa Jumper, extraido TAL CUAL de
FlexOS_Ultra/ (primitivas de FlexOS_Ultra_Gfx.h y la fuente Outfit de FlexOS_Ultra_Font.h / FlexOS_Ultra_Text.h). Asi el juego se ve
exactamente igual que en Arduino y las pruebas de host (tests/host/test_jumper.c) comparan contra el MISMO codigo.

  python3 tools/gen_jumper_engine.py            # escribe el .inc
  python3 tools/gen_jumper_engine.py --check    # falla si el .inc no es lo que sale hoy de los originales (lo usa run_tests.sh)
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "components" / "flex_games" / "src" / "jumper_engine.inc"
EXTRACT = ROOT / "tests" / "host" / "ref" / "extract_arduino.py"

SPEC = [
    ("FlexOS_Ultra_Gfx.h", "gClipY0,gClipX0,gClipLY0,gLand,rgb565,DIV255,mix565,isqrt32,putPhys,putPhysA,px,pxA,hLine,hLineA,vLine,"
                           "fillSpanLand,fillSpanLandA,rowsInClip,fillRect,fillRectA,rrInset,fillRoundRect,fillRoundRectA,drawRoundRect,"
                           "fillCircle,fillCircleA,drawCircle,AA_EPS,aaClipBox,fillCircleAA,strokeSegAA"),
    ("FlexOS_Ultra_Text.h", "FONT5x7,GLYPH_INVQ,GLYPH_INVEXCL,GLYPH_MIDDOT,ACC_NONE,mapCP,drawGlyphRaw,drawAccent,drawGlyphSmooth"),
    ("FlexOS_Ultra_Font.h", "FONT_LINEH,FONT_ASC,FG,FBM,fontIdx,FONT_HPS,FONT_CAPOFF,fgPix,fontSc,utf8Decode,cpZeroWidth,cpFold,nextCP,"
                            "GLYPH_COLS_MAX,gGlyphCx,gGlyphFx,drawGlyphScaled,textW,drawTextA,drawText,drawTextC,drawTextCA,drawTextR"),
]


# el contenedor de la app (FlexOS_Ultra_AppGames.h): su ciclo de vida, tal cual
GAMES = ("FlexOS_Ultra_AppGames.h", "gamesEnter,gamesTick,gamesSuspend,gamesResume,gamesCloseApp")
OUT2 = ROOT / "components" / "flex_games" / "src" / "jumper_games.inc"


def build_games() -> str:
    with tempfile.NamedTemporaryFile(suffix=".cpp", delete=False) as t:
        tmp = t.name
    subprocess.run([sys.executable, str(EXTRACT), tmp, f"{GAMES[0]}:{GAMES[1]}"], check=True)
    return ("// GENERADO por tools/gen_jumper_engine.py desde FlexOS_Ultra/FlexOS_Ultra_AppGames.h (no editar a mano).\n"
            + pathlib.Path(tmp).read_text())


def build() -> str:
    parts = []
    for fname, names in SPEC:
        with tempfile.NamedTemporaryFile(suffix=".cpp", delete=False) as t:
            tmp = t.name
        subprocess.run([sys.executable, str(EXTRACT), tmp, f"{fname}:{names}"], check=True)
        parts.append(f"// ---- {fname}: {names}\n" + pathlib.Path(tmp).read_text())
    return ("// GENERADO por tools/gen_jumper_engine.py desde FlexOS_Ultra/ (no editar a mano; run_tests.sh lo comprueba).\n"
            "// Motor de dibujo de Arduino que usa Jumper: primitivas, fuente Outfit antialiasada y fuente 5x7.\n" + "\n".join(parts))


if __name__ == "__main__":
    text, games = build(), build_games()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != text or not OUT2.exists() or OUT2.read_text() != games:
            print("jumper_engine.inc / jumper_games.inc NO coinciden con lo que sale de los originales Arduino: regenerar con tools/gen_jumper_engine.py")
            sys.exit(1)
        print("OK: jumper_engine.inc y jumper_games.inc identicos a lo extraido de FlexOS_Ultra/")
    else:
        OUT.write_text(text)
        OUT2.write_text(games)
        print(f"escrito {OUT} ({len(text)} B) y {OUT2} ({len(games)} B)")
