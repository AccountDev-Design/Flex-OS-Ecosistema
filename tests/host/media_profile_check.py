#!/usr/bin/env python3
"""
Perfil multimedia de Flex OS Ultra: lo que escribe/analiza el codigo Kotlin del telefono, juzgado por el
FIRMWARE (build/mediacheck: demux AVI, decodificador JPEG, parser y decodificador WAV del P4).

  media_profile_check.py <mediacheck> <carpeta> <items.json>

Reglas:
  expect=NONE  -> el analizador dice NONE Y el firmware lo abre y lo decodifica entero.
  hard=true    -> el fallo es de decodificacion: el analizador NO dice NONE Y el firmware lo rechaza.
  otro caso    -> solo se informa (veredicto de politica: tamano, ritmo, EXIF...).
"""
import array, json, math, os, subprocess, sys

mc, folder, items_path = sys.argv[1], sys.argv[2], sys.argv[3]
items = json.load(open(items_path))["items"]
checks = fails = 0

def check(ok, what):
    global checks, fails
    checks += 1
    if not ok:
        fails += 1
        print("   FALLO:", what)

def run(*args):
    r = subprocess.run([mc, *args], capture_output=True, text=True)
    try:
        return json.loads(r.stdout.strip().splitlines()[-1])
    except Exception:
        return {"ok": 0, "error": "sin respuesta (%s)" % r.stderr.strip()[:80]}

def sine_snr(pcm, rate, freq=440.0, amp=12000.0):
    sig = err = 0.0
    n = len(pcm)
    for i in range(200, n - 200):
        r = amp * math.sin(2 * math.pi * freq * i / rate)
        d = r - pcm[i]
        sig += r * r
        err += d * d
    return 10 * math.log10(sig / max(err, 1e-9))

for it in items:
    name, plan, hard, expect = it["file"], it["plan"], it["hard"], it["expect"]
    path = os.path.join(folder, name)
    ext = name.rsplit(".", 1)[-1].lower()
    info = ""
    accepted = None
    if ext == "avi":
        r = run("avi", path)
        accepted = bool(r.get("ok")) and r.get("bad", 1) == 0 and r.get("decoded", 0) == r.get("frames", -1) and r.get("decoded", 0) > 0 and r.get("thumb") == 1
        info = "frames=%s decoded=%s bad=%s %sx%s us=%s codec=%s" % (r.get("frames"), r.get("decoded"), r.get("bad"), r.get("w"), r.get("h"), r.get("us"), r.get("codec")) if r.get("ok") else str(r.get("error"))
    elif ext == "wav":
        raw = os.path.join(folder, name + ".raw")
        r = run("wav", path, raw)
        accepted = bool(r.get("ok")) and r.get("samples", 0) > 0
        info = "rate=%s ch=%s fmt=%s samples=%s dur=%sms" % (r.get("rate"), r.get("ch"), r.get("format"), r.get("samples"), r.get("dur")) if r.get("ok") else "error=%s" % r.get("error")
        if accepted and name.startswith("w_ima_"):
            pcm = array.array("h"); pcm.frombytes(open(raw, "rb").read())
            snr = sine_snr(pcm, r["rate"])
            info += " SNR=%.1fdB" % snr
            check(snr >= 24, "%s: SNR %.1f dB < 24 dB" % (name, snr))
            check(abs(r["samples"] - int(r["rate"] * 1.5)) <= 1, "%s: muestras %s != %s" % (name, r["samples"], int(r["rate"] * 1.5)))
        if accepted and name.startswith("conv_"):
            check(abs(r["dur"] - 1500) <= 10, "%s: duracion %s ms != 1500" % (name, r["dur"]))
            check(r["ch"] == 1, "%s: no es mono" % name)
        if accepted and name == "remux_mov_pcm.wav":
            pcm = array.array("h"); pcm.frombytes(open(raw, "rb").read())
            check(len(pcm) == 22050, "%s: %s muestras != 22050" % (name, len(pcm)))
    elif ext in ("jpg", "jpeg"):
        r = run("thumb", path)
        accepted = bool(r.get("ok"))
        info = "miniatura %sx%s" % (r.get("w"), r.get("h")) if accepted else str(r.get("error"))
    else:
        r = run("sniff", path)
        accepted = bool(r.get("ok")) and bool(r.get("playable"))
        info = "fmt=%s playable=%s" % (r.get("fmt"), r.get("playable"))
    verdict = "firmware: " + ("ACEPTA" if accepted else "RECHAZA")
    print("  %-34s analizador=%-11s %-18s %s" % (name, plan, verdict, info))
    if expect == "NONE":
        check(plan == "NONE", "%s: el analizador dijo %s (se esperaba NONE)" % (name, plan))
        check(accepted, "%s: el analizador dice NONE pero el firmware lo rechaza (%s)" % (name, info))
    if hard:
        check(plan != "NONE", "%s: el analizador dice NONE pero es un fallo de decodificacion" % name)
        check(not accepted, "%s: el firmware ACEPTA algo que el analizador marca como no decodificable" % name)
    if plan == "NONE" and expect != "NONE":
        # cualquier NONE (no solo los esperados) tiene que abrirse en el firmware
        check(accepted, "%s: NONE pero el firmware lo rechaza (%s)" % (name, info))

print("=== %d comprobaciones, %d fallos ===" % (checks, fails))
sys.exit(1 if fails else 0)
