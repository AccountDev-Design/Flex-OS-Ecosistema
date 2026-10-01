#ifndef FLEXOS_CLOUD_TLS_H
#define FLEXOS_CLOUD_TLS_H

// #############################################################
//  FLEX OS · RAICES TLS DE FLEX CLOUD Y FLEX ACCOUNT
//  ------------------------------------------------------------
//  Toda peticion que lleve la credencial de Flex Account (validar la sesion,
//  Flex Cloud) valida el certificado del servidor contra ESTAS raices. Nunca
//  setInsecure(): quien controlase la red veria la credencial en claro y
//  podria suplantar al usuario.
//
//  Mismo criterio que el OTA (FLEXOS_OTA_ROOT_CA) y el clima, con una
//  diferencia: aqui el valor por defecto NO esta vacio. El servicio se aloja
//  detras de un proveedor cuya autoridad puede rotar (Cloudflare usa Google
//  Trust Services, Let's Encrypt y SSL.com), asi que se confia en las raices
//  de esas autoridades en lugar de fijar una sola.
//
//  Para fijar UNA raiz concreta en una version publicada:
//      -DFLEX_CLOUD_ROOT_CA="\"-----BEGIN CERTIFICATE-----\\n...\""
//
//  Coste: ~16 KB de flash para las once raices. mbedTLS las analiza al abrir
//  cada conexion y las libera al cerrarla; no quedan en RAM.
// #############################################################
const char* flexCloudRootCA();

#endif
