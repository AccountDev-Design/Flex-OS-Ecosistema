#!/usr/bin/env bash
# #############################################################
#  Certificado de PRUEBAS para el backend de Flex Intelligence
#  ------------------------------------------------------------
#  Genera un certificado autofirmado valido para la IP de tu PC en
#  la red local. Ese mismo PEM es el que se pega en Flex
#  Intelligence -> Ajustes -> Certificado raiz.
#
#  POR QUE HACE FALTA: el firmware valida TLS de verdad y no tiene
#  ningun interruptor para saltarselo. Sin un certificado que
#  valide, no se conecta -- y eso es lo correcto: por ese socket
#  van el texto del usuario y su token.
#
#  ESTO ES PARA PROBAR EN TU RED. Para algo permanente, usa un
#  certificado de una CA de verdad.
# #############################################################
set -euo pipefail

IP="${1:-}"
if [ -z "$IP" ]; then
  echo "Uso: bash tools/make-cert.sh <IP-de-tu-PC>"
  echo "Ejemplo: bash tools/make-cert.sh 192.168.1.50"
  echo
  echo "Tu IP en la red local suele salir con:  hostname -I"
  exit 1
fi

mkdir -p certs
# subjectAltName con la IP: el firmware valida el nombre del
# certificado contra el host de la URL, y una URL con IP necesita la IP
# en el SAN. Sin esto, el certificado es correcto y aun asi la conexion
# falla -- y el mensaje que se ve es un generico "no valida", que manda
# a buscar el problema al sitio equivocado.
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout certs/server.key -out certs/server.crt \
  -days 365 -sha256 \
  -subj "/CN=$IP" \
  -addext "subjectAltName=IP:$IP"

chmod 600 certs/server.key
echo
echo "Listo:"
echo "  certs/server.crt   -> FLEXAI_TLS_CERT  (y este es el PEM que se pega en el dispositivo)"
echo "  certs/server.key   -> FLEXAI_TLS_KEY"
echo
echo "Contenido para Flex Intelligence -> Ajustes -> Certificado raiz:"
echo "-----------------------------------------------------------------"
cat certs/server.crt
