#pragma once
// Objetivo simulado. Se puede cambiar con -DFLEXOS_TEST_TARGET_xxx para
// compilar el perfil de memoria de cada placa (ver tests/host/Makefile).
#if defined(FLEXOS_TEST_TARGET_S3)
  #define CONFIG_IDF_TARGET_ESP32S3 1
#elif defined(FLEXOS_TEST_TARGET_ESP32)
  #define CONFIG_IDF_TARGET_ESP32 1
#else
  #define CONFIG_IDF_TARGET_ESP32P4 1
#endif
