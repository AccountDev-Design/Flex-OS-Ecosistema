#!/usr/bin/env python3
# #############################################################
# ##  FlexOS · Flex Web Server · generador de FlexOS_WebUI.h
# #############################################################
#
# La web del movil se escribe aqui, en webui/, como archivos normales
# (index.html, app.css, app.js): se editan y se revisan como lo que son.
# El P4 los sirve desde la memoria del programa, asi que este script los
# convierte en tres cadenas de C++ dentro de ../FlexOS_WebUI.h.
#
#   python3 gen_header.py           regenera FlexOS_WebUI.h
#   python3 gen_header.py --check   falla si FlexOS_WebUI.h no esta al dia
#
# La bateria de pruebas (make -C tests/host ino) ejecuta --check: un
# cambio en la web que no se haya regenerado no llega a la placa sin que
# alguien se entere.
#
# Tambien comprueba lo que la politica de seguridad (CSP) del servidor
# exige: ni scripts ni estilos en linea, ni manejadores on*= en el HTML.
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'FlexOS_WebUI.h')
FILES = [
    ('FLEXWEB_INDEX_HTML', 'index.html'),
    ('FLEXWEB_APP_CSS', 'app.css'),
    ('FLEXWEB_APP_JS', 'app.js'),
]
DELIM = 'FXW'


def fail(msg):
    sys.stderr.write('gen_header: ' + msg + '\n')
    sys.exit(1)


def load(name):
    path = os.path.join(HERE, name)
    raw = open(path, 'rb').read()
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError as e:
        fail('%s no es UTF-8 valido (%s)' % (name, e))
    if '\r' in text:
        fail('%s tiene finales de linea CRLF' % name)
    if ')' + DELIM + '"' in text:
        fail('%s contiene el delimitador )%s" de la cadena en bruto' % (name, DELIM))
    if '\0' in text:
        fail('%s contiene un byte nulo' % name)
    return text


def check_csp(html):
    for m in re.finditer(r'<script\b([^>]*)>', html, re.I):
        if 'src=' not in m.group(1):
            fail('index.html: <script> en linea (la CSP solo admite script-src \'self\')')
    if re.search(r'<style\b', html, re.I):
        fail('index.html: <style> en linea (la CSP solo admite style-src \'self\')')
    if re.search(r'\sstyle\s*=', html, re.I):
        fail('index.html: atributo style= (la CSP lo bloquearia)')
    if re.search(r'\son[a-z]+\s*=', html, re.I):
        fail('index.html: manejador on*= en linea (la CSP lo bloquearia)')


def render():
    out = [
        '// #############################################################',
        '// ##  FlexOS · FLEX WEB SERVER · la web del movil',
        '// ##  GENERADO por webui/gen_header.py a partir de webui/.',
        '// ##  No se edita a mano: se edita webui/ y se regenera.',
        '// #############################################################',
        '#pragma once',
        '',
    ]
    total = 0
    for sym, name in FILES:
        text = load(name)
        if name == 'index.html':
            check_csp(text)
        n = len(text.encode('utf-8'))
        total += n
        out.append('// %s: %d bytes' % (name, n))
        out.append('static const char %s[] = R"%s(%s)%s";' % (sym, DELIM, text, DELIM))
        out.append('')
    out.append('#define FLEXWEB_UI_BYTES %d' % total)
    out.append('')
    return '\n'.join(out)


def main():
    text = render()
    if '--check' in sys.argv[1:]:
        cur = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if cur != text:
            fail('FlexOS_WebUI.h no coincide con webui/. Ejecuta: python3 FlexOS_Ultra/webui/gen_header.py')
        print('FlexOS_WebUI.h al dia con webui/.')
        return
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('FlexOS_WebUI.h generado (%d bytes de web).' % len(text.encode('utf-8')))


if __name__ == '__main__':
    main()
