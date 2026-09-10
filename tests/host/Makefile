# #############################################################
#  Pruebas de host del navegador de Flex OS
#  ------------------------------------------------------------
#  Compilan y ejecutan EN EL PC el mismo codigo que va a la placa.
#  No hace falta arduino-cli ni el core de ESP32.
#
#    make            todas las pruebas (perfil Ultra/P4)
#    make all-boards las mismas, en los tres perfiles de placa
#    make fixtures   regenera las imagenes de prueba (necesita Node)
#    make clean
#
#  Las pruebas se compilan con AddressSanitizer y UndefinedBehavior
#  Sanitizer siempre que se puede: el codigo bajo prueba maneja bytes
#  que vienen de la red, y un desbordamiento ahi no puede quedar en
#  "parece que funciona".
# #############################################################

ROOT     := ../..
# CARPETA CANONICA DEL SKETCH P4. FlexOS_Ultra/ es la que se abre en el Arduino
# IDE y la unica copia del codigo del P4 que hay en el repositorio (ver
# docs/REPOSITORY_CONSOLIDATION_PLAN.md). En la raiz solo quedan los sketches de
# Flex OS Ultra S3 y Flex OS Pro con los modulos que ESOS dos necesitan.
ULTRA    := $(ROOT)/FlexOS_Ultra
# Ruta de busqueda: primero el sketch P4, despues la raiz. Los pocos modulos que
# existen en los dos sitios (OTA, FS, navegador, JPEG) son byte a byte
# identicos, y check_shared.py falla la bateria si alguna vez dejan de serlo.
INC      := -I$(ULTRA) -I$(ROOT)
BUILD    := build
CXX      ?= g++
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wno-unused-parameter
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
COMMON   := $(CXXSTD) -O1 -g $(WARN)

# El codigo de dispositivo (FlexOS_BrowserApp.cpp) necesita el entorno
# Arduino simulado de stub/. Ver stub/README.md.
DEVFLAGS := -DARDUINO=200 -Istub

# Perfil de placa: vacio = ESP32-P4 (Ultra).
#   make BOARD=S3     -> Flex OS Ultra S3
#   make BOARD=ESP32  -> Flex OS Pro
BOARD ?=
ifeq ($(BOARD),S3)
  DEVFLAGS += -DFLEXOS_TEST_TARGET_S3
endif
ifeq ($(BOARD),ESP32)
  DEVFLAGS += -DFLEXOS_TEST_TARGET_ESP32
endif

CORE_SRC := $(ROOT)/FlexOS_Browser.cpp
JPEG_SRC := $(ROOT)/FlexOS_JPEG.cpp
APP_SRC  := $(ROOT)/FlexOS_BrowserApp.cpp

BINS := $(BUILD)/test_jpeg $(BUILD)/test_browser $(BUILD)/test_app $(BUILD)/test_bridge $(BUILD)/test_net \
        $(BUILD)/test_vault $(BUILD)/test_weather $(BUILD)/test_flexlink $(BUILD)/test_flexphone $(BUILD)/test_flexlink_transport $(BUILD)/test_flexlink_vectors \
        $(BUILD)/test_media $(BUILD)/test_mem $(BUILD)/test_fall \
        $(BUILD)/test_appvm $(BUILD)/test_appgrant $(BUILD)/test_apphost $(BUILD)/test_flexpkg \
        $(BUILD)/test_vector $(BUILD)/test_qr $(BUILD)/test_httpshare
TOOLS := $(BUILD)/jpegcheck $(BUILD)/flexpkgcheck $(BUILD)/flexapprun

.PHONY: all run all-boards fixtures clean ino
all: run

$(BUILD):
	@mkdir -p $(BUILD)

# --- nucleo portable: con sanitizers ---
$(BUILD)/test_jpeg: test_jpeg.cpp $(JPEG_SRC) $(ROOT)/FlexOS_JPEG.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -pthread -o $@ test_jpeg.cpp $(JPEG_SRC)

$(BUILD)/test_browser: test_browser.cpp $(CORE_SRC) $(ROOT)/FlexOS_Browser.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_browser.cpp $(CORE_SRC)

# --- Medios: clasificacion, demultiplexado de AVI/MJPEG, WAV e indice ---
# Nucleo PURO (no toca Arduino ni el sistema de archivos): se compila
# igual que el del navegador, CON sanitizers. Aqui es obligatorio --
# este modulo recorre cabeceras RIFF que vienen de una tarjeta que
# puede traer cualquier cosa, y un desplazamiento mal calculado sobre
# un AVI corrupto no puede quedarse en "parece que funciona".
MEDIA_SRC := $(ULTRA)/FlexOS_Media.cpp

$(BUILD)/test_media: test_media.cpp $(MEDIA_SRC) $(ULTRA)/FlexOS_Media.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_media.cpp $(MEDIA_SRC)

# --- Memoria: presupuesto, presion, fragmentacion y avisos ---
# Nucleo PURO (no toca Arduino ni el SDK): recibe una medida ya tomada y
# decide. CON sanitizers, como el resto del nucleo portable. Aqui es donde
# se fijan los cortes de 10/6/5 MB y la regla del bloque contiguo: si
# cambian sin querer, esta bateria lo dice antes que la placa.
MEM_SRC := $(ULTRA)/FlexOS_Mem.cpp

$(BUILD)/test_mem: test_mem.cpp $(MEM_SRC) $(ULTRA)/FlexOS_Mem.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_mem.cpp $(MEM_SRC)

# --- Deteccion de caidas: maquina de estados y Confidence Score ---
# Nucleo PURO, igual que el de memoria: recibe una muestra ya medida del
# BNO085 y decide. CON sanitizers, y con motivo: aqui es donde se fija que
# caminar, dar un golpe en la mesa o dejar el aparato encima NO cuenten
# como caida. Comprobar eso a mano obliga a tirar el aparato al suelo; el
# PC lo hace con senales sinteticas y sin romper nada.
FALL_SRC := $(ULTRA)/FlexOS_FallDetect.cpp

$(BUILD)/test_fall: test_fall.cpp $(FALL_SRC) $(ULTRA)/FlexOS_FallDetect.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) $(INC) -o $@ test_fall.cpp $(FALL_SRC)
# --- FLEX VECTOR PRO: motor vectorial, QR y servidor HTTP -------------------
# Los tres son nucleo PURO (no tocan Arduino, ni el framebuffer, ni el sistema
# de archivos): se compilan igual que el del navegador, CON sanitizers.
#
# Aqui es obligatorio, y por tres motivos distintos:
#   test_vector   el motor maneja indices de un pool compartido, listas de
#                 bordes y aritmetica de matrices. Un desbordamiento ahi
#                 corrompe el documento del usuario en silencio.
#   test_qr       el codificador hace aritmetica en GF(256), entrelazado de
#                 bloques y colocacion en zigzag. La prueba VUELVE A LEER el
#                 codigo generado: es la unica forma honesta de saber que un
#                 telefono lo leeria.
#   test_httpshare el analizador recibe bytes de cualquiera que este en la
#                 misma Wi-Fi. Mismo criterio que el navegador.
VECSRC  := $(ULTRA)/FlexOS_Vector.cpp
QRSRC   := $(ULTRA)/FlexOS_QR.cpp
HTTPSRC := $(ULTRA)/FlexOS_HttpShare.cpp

# -lz es SOLO de la prueba: descomprime con zlib el PNG que genera el
# firmware y compara los pixeles uno a uno. El firmware NO enlaza zlib --
# su compresor es propio y cabe en FlexOS_Vector.cpp.
$(BUILD)/test_vector: test_vector.cpp $(VECSRC) $(ULTRA)/FlexOS_Vector.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_vector.cpp $(VECSRC) -lz

$(BUILD)/test_qr: test_qr.cpp $(QRSRC) $(ULTRA)/FlexOS_QR.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_qr.cpp $(QRSRC)

$(BUILD)/test_httpshare: test_httpshare.cpp $(HTTPSRC) $(ULTRA)/FlexOS_HttpShare.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_httpshare.cpp $(HTTPSRC)

# --- PLATAFORMA DE APLICACIONES flex-app-v1 ---------------------------------
# Cuatro baterias, todas con sanitizers. Aqui es OBLIGATORIO: este codigo
# ejecuta bytecode que viene de internet y decide si un paquete firmado por un
# tercero puede tocar el sistema. Un desbordamiento aqui no puede quedarse en
# "parece que funciona".
#
#   test_appvm     la maquina: validador, trampas, limites y presupuesto
#   test_appgrant  permisos firmados: cada campo del grant, por separado
#   test_apphost   el gestor: estados, permiso por llamada, fugas, orientacion
#   test_flexpkg   el paquete: validacion + instalacion transaccional real
#
# cJSON se compila desde tests/host/vendor (MIT). En la placa lo trae el core
# de ESP32; se versiona aqui para que la prueba recorra el MISMO codigo.
VMSRC   := $(ULTRA)/FlexOS_AppVM.cpp
GRANTSRC:= $(ULTRA)/FlexOS_AppGrant.cpp
HOSTSRC := $(ULTRA)/FlexOS_AppHost.cpp
PKGCORE := $(ULTRA)/FlexOS_PkgCore.cpp
PKGSRC  := $(ULTRA)/FlexOS_Package.cpp
CJSON   := vendor/cJSON/cJSON.c
FSSTUB  := fsstub/fsstub.cpp
PKGFLAGS:= -Ifsstub -Ivendor/cJSON -Iinostub $(INC) -DARDUINO=200 -Wno-format-truncation

$(BUILD)/test_appvm: test_appvm.cpp vmimage.h $(VMSRC) $(ULTRA)/FlexOS_AppVM.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_appvm.cpp $(VMSRC)

$(BUILD)/test_appgrant: test_appgrant.cpp pkgbuild.h $(GRANTSRC) $(ULTRA)/FlexOS_AppGrant.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_appgrant.cpp $(GRANTSRC) -lcrypto

$(BUILD)/test_apphost: test_apphost.cpp vmimage.h pkgbuild.h $(HOSTSRC) $(VMSRC) $(GRANTSRC) \
                       $(ULTRA)/FlexOS_AppHost.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) $(INC) -o $@ test_apphost.cpp $(HOSTSRC) $(VMSRC) $(GRANTSRC) -lcrypto

# FlexOS_Runtime.cpp (el runtime declarativo de siempre) entra tambien: es la
# unica forma honesta de comprobar que una app flex-ui-1 ya publicada SIGUE
# abriendose despues de todo este trabajo.
UISRC := $(ULTRA)/FlexOS_Runtime.cpp
$(BUILD)/test_flexpkg: test_flexpkg.cpp pkgbuild.h $(PKGCORE) $(PKGSRC) $(GRANTSRC) $(UISRC) \
                       $(ULTRA)/FlexOS_PkgCore.h $(ULTRA)/FlexOS_Package.h fsstub/FS.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) $(PKGFLAGS) -o $@ test_flexpkg.cpp $(PKGCORE) $(PKGSRC) $(GRANTSRC) \
	       $(UISRC) $(FSSTUB) $(CJSON) -lcrypto

# Herramienta: valida un .flexpkg con el NUCLEO DEL FIRMWARE. La usa
# sdk/test/sdk.test.js para comprobar que lo que empaqueta el SDK es
# exactamente lo que la placa acepta.
$(BUILD)/flexpkgcheck: flexpkgcheck.cpp $(PKGCORE) $(GRANTSRC) | $(BUILD)
	$(CXX) $(CXXSTD) -O2 $(WARN) $(PKGFLAGS) -o $@ flexpkgcheck.cpp $(PKGCORE) $(GRANTSRC) $(CJSON) -lcrypto

# Herramienta: ejecuta un .flxb con el GESTOR REAL del firmware y un
# anfitrion de mentira. Permite probar una app sin placa; la usa
# sdk/test/sdk.test.js con la app de ejemplo.
$(BUILD)/flexapprun: flexapprun.cpp pkgbuild.h $(HOSTSRC) $(VMSRC) $(GRANTSRC) | $(BUILD)
	$(CXX) $(CXXSTD) -O2 $(WARN) $(INC) -o $@ flexapprun.cpp $(HOSTSRC) $(VMSRC) $(GRANTSRC) -lcrypto

# --- Flex Link / Flex Phone: protocolo y modelo del telefono ---
# Nucleo PURO (sin Arduino), asi que se compila igual que el del
# navegador: con AddressSanitizer y UndefinedBehavior Sanitizer.
# Aqui es obligatorio -- estos dos ficheros parsean bytes que llegan
# por el aire desde un telefono, y un desbordamiento en el
# reensamblador no puede quedarse en "parece que funciona".
FLNKSRC := $(ULTRA)/FlexOS_FlexLink.cpp
FLPSRC  := $(ULTRA)/FlexOS_FlexPhone.cpp

$(BUILD)/test_flexlink: test_flexlink.cpp $(FLNKSRC) $(ULTRA)/FlexOS_FlexLink.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_flexlink.cpp $(FLNKSRC)

$(BUILD)/test_flexphone: test_flexphone.cpp $(FLPSRC) $(FLNKSRC) \
                         $(ULTRA)/FlexOS_FlexPhone.h $(ULTRA)/FlexOS_FlexLink.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_flexphone.cpp $(FLPSRC) $(FLNKSRC)

# El transporte: maquina de estados del enlace. En el PC la capacidad
# es NONE (no hay radio), igual que en el P4 sin el C6 preparado, asi
# que la prueba fuerza la capacidad para poder ejercitarla entera.
FLPLINK := $(ULTRA)/FlexOS_FlexPhone_Link.cpp
$(BUILD)/test_flexlink_transport: test_flexlink_transport.cpp $(FLPLINK) $(FLPSRC) $(FLNKSRC) \
                         $(ULTRA)/FlexOS_FlexPhone_Link.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_flexlink_transport.cpp $(FLPLINK) $(FLPSRC) $(FLNKSRC)

# VECTORES DORADOS. Fijan los bytes exactos del protocolo para que el
# firmware y la app Android no puedan divergir en silencio: el mismo
# vector esta en FlexLinkTest.kt. Ver la cabecera del .cpp.
$(BUILD)/test_flexlink_vectors: test_flexlink_vectors.cpp $(FLPSRC) $(FLNKSRC) | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_flexlink_vectors.cpp $(FLPSRC) $(FLNKSRC)

# --- Flex Vault: cripto y almacen cifrado ---
# Se compila el modulo REAL (FlexOS_Vault.cpp) con OpenSSL como
# backend (en la placa es mbedTLS con el acelerador del P4) y con un
# sistema de archivos y una NVS en memoria, que viven en el propio
# test. CON sanitizers: aqui se manejan longitudes, desplazamientos y
# buffers de bloque, y un desbordamiento en esa aritmetica no puede
# quedarse en "parece que funciona".
# El motor meteorologico: mismo patron que la boveda -- su propia NVS en
# memoria (wxstub), porque aqui SI hay que releer lo que se escribio (la cache
# del pronostico son ~1,3 KB, muy por encima de lo que guarda vaultstub).
WXFLAGS := -Iwxstub -Istub $(INC) -DARDUINO=200 -Wno-format-truncation
$(BUILD)/test_weather: test_weather.cpp $(ULTRA)/FlexOS_Weather.cpp $(ULTRA)/FlexOS_Weather.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) $(WXFLAGS) -o $@ test_weather.cpp

VAULTFLAGS := -Ivaultstub -Iinostub $(INC) -DARDUINO=200 -Wno-format-truncation
$(BUILD)/test_vault: test_vault.cpp $(ULTRA)/FlexOS_Vault.cpp $(ULTRA)/FlexOS_Vault.h $(ROOT)/FlexOS_FS.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) $(VAULTFLAGS) -o $@ test_vault.cpp $(ULTRA)/FlexOS_Vault.cpp -lcrypto

# --- codigo de dispositivo: con el entorno simulado ---
# Sin sanitizers a proposito: el simulador define las funciones de
# FreeRTOS y de WiFi que el modulo espera, y ASan no aporta nada aqui
# que no cubra ya el contador de reservas del propio test.
$(BUILD)/test_app: test_app.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC) | $(BUILD)
	$(CXX) $(COMMON) $(DEVFLAGS) -o $@ test_app.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC)

$(BUILD)/test_bridge: test_bridge.cpp $(ROOT)/FlexOS_Browser_Bridge.h $(APP_SRC) $(CORE_SRC) $(JPEG_SRC) | $(BUILD)
	$(CXX) $(COMMON) $(DEVFLAGS) -Wno-unused-function -o $@ test_bridge.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC)

# Transporte de verdad: sockets TCP POSIX, hilos y semaforos reales.
# Es la unica prueba de host que ejercita wsHandshake/wsReadMessage y la
# tarea de red completa, asi que necesita -pthread.
$(BUILD)/test_net: test_net.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC) | $(BUILD)
	$(CXX) $(COMMON) $(DEVFLAGS) -pthread -o $@ test_net.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC)

# Herramienta que usa la prueba de extremo a extremo del servicio para
# comprobar que los JPEG que envia son decodificables POR EL FIRMWARE.
$(BUILD)/jpegcheck: jpegcheck.cpp $(JPEG_SRC) $(ROOT)/FlexOS_JPEG.h | $(BUILD)
	$(CXX) $(CXXSTD) -O2 $(WARN) -o $@ jpegcheck.cpp $(JPEG_SRC)

.PHONY: tools
tools: $(TOOLS)

# --- COMPILACION DEL SKETCH COMPLETO (sin placa) ---
# Compila FlexOS_Ultra.ino entero contra los dobles de inostub/. No
# ejecuta nada: sirve para que un error de tipos o de nombres en el
# sketch salte aqui, sin arduino-cli ni el core de ESP32.
INO_SRC  := $(ULTRA)/FlexOS_Ultra.ino
# Los PUENTES son cabeceras que el .ino incluye (Store, Account, Navegador,
# OTA): sin listarlos como dependencia, editar uno no rehacia la prueba y
# `make` daba por buena la version anterior.
INO_BRIDGES := $(wildcard $(ULTRA)/FlexOS_*_Bridge.h)
# Los MODULOS del sketch. FlexOS_Ultra.ino es solo el orquestador: interfaces,
# apps y utilidades viven en FlexOS_Ultra_*.h y el .ino las incluye en orden.
# Van aqui por el mismo motivo que los puentes: tocar un modulo TIENE que
# rehacer la prueba, o `make` daria por buena la version anterior del sketch.
INO_PARTS := $(wildcard $(ULTRA)/FlexOS_Ultra_*.h)
INOFLAGS := -Iinostub $(INC) -DARDUINO=200
# -Wmaybe-uninitialized: falso positivo del ARNES. Los dobles de I2C devuelven
# siempre "sin datos", asi que al inlinear gtPollMulti el compilador ve la rama
# imposible en la que se usa el buffer sin rellenar -- el codigo real hace
# "if(!gtRd(...)) break;" justo antes. Con el driver de verdad no aparece.
INOWARN  := $(WARN) -Wno-unused-function -Wno-unused-variable \
            -Wno-misleading-indentation -Wno-format-truncation \
            -Wno-maybe-uninitialized

# Tres comprobaciones, no una:
#   1. check_protos.py reproduce la regla del AUTO-PROTOTIPADO del IDE de
#      Arduino (todo tipo que salga en una firma tiene que estar definido antes
#      de la primera funcion). g++ no la aplica, asi que sin esto el sketch
#      puede compilar aqui y fallar en el IDE con "does not name a type".
#   3. g++ compila el sketch entero.
#   2. check_wiring.py comprueba el CABLEADO: que todo estado se despache en
#      loop() y que los ganchos obligatorios (quien llama a quien) existan de
#      verdad. Hace falta porque una funcion nueva puede estar escrita,
#      compilar y pasar las pruebas... y no llamarse desde ningun sitio:
#      -Wunused-function NO avisa de una static que tenga prototipo previo, y
#      aqui casi todas lo tienen. Ese fallo deja al sistema atrapado en un
#      estado que nadie despacha.
# El driver del IMU es una unidad de traduccion aparte que SI va a la
# placa y que ninguna otra bateria toca (habla por el Wire real). Se
# comprueba aqui, contra los dobles de Arduino: un error de tipos o de
# nombre en el protocolo SHTP salta en el PC y no en el aparato.
BNO_SRC := $(ULTRA)/FlexOS_BNO085.cpp

ino: ino_compile.cpp $(INO_SRC) $(INO_PARTS) $(INO_BRIDGES) $(BNO_SRC) check_protos.py check_wiring.py check_shared.py check_stack.py | $(BUILD)
	@python3 check_shared.py $(ROOT)
	@python3 check_protos.py $(INO_SRC)
	@python3 check_wiring.py $(INO_SRC)
	@python3 check_stack.py $(ROOT)
	@$(CXX) $(CXXSTD) -fsyntax-only $(INOWARN) $(INOFLAGS) -x c++ ino_compile.cpp
	@$(CXX) $(CXXSTD) -fsyntax-only $(WARN) $(INOFLAGS) $(BNO_SRC)
	@echo "FlexOS_Ultra.ino compila."

# El mismo TU, pero ENLAZADO y EJECUTADO: main() comprueba el reloj del sistema,
# la maquina de gestos del panel rapido y el teclado global: partes que son
# logica pura y por tanto verificables de verdad en el PC. Los
# modulos hermanos (OTA, FS, navegador) entran como dobles: ya tienen sus
# propias pruebas.
#
# CON SANITIZERS: un desbordamiento no puede quedarse en "parece que funciona".
# Mismo criterio que el resto de pruebas del nucleo portable.
# Flex Phone entra en test_ino como CODIGO REAL, no como doble. Es a
# proposito: el puente (FlexOS_FlexPhone_Bridge.h) es lo unico de esta
# funcion que no tiene prueba propia, y enlazarlo contra los modulos de
# verdad hace que un cambio de firma o una llamada mal cableada salte
# aqui. Los modulos ademas no tocan Arduino, asi que no necesitan dobles.
# La plataforma de apps (VM, grants, gestor y nucleo del paquete) entra en
# test_ino como CODIGO REAL, no como doble: es logica portable y asi el puente
# del runtime se enlaza contra lo que de verdad va a la placa. Un cambio de
# firma o una llamada mal cableada salta aqui.
$(BUILD)/test_ino: ino_compile.cpp ino_extern_stubs.cpp $(INO_SRC) $(INO_PARTS) $(INO_BRIDGES) $(JPEG_SRC) \
                   $(FLNKSRC) $(FLPSRC) $(FLPLINK) $(CORE_SRC) $(MEDIA_SRC) $(MEM_SRC) $(FALL_SRC) \
                   $(VMSRC) $(GRANTSRC) $(HOSTSRC) $(PKGCORE) \
                   $(VECSRC) $(QRSRC) $(HTTPSRC) | $(BUILD)
	@$(CXX) $(CXXSTD) -O1 -g $(INOWARN) $(SAN) $(INOFLAGS) -x c++ -c ino_compile.cpp -o $(BUILD)/ino_compile.o
	@$(CXX) $(CXXSTD) -O1 -g $(INOWARN) $(SAN) $(INOFLAGS) -c ino_extern_stubs.cpp -o $(BUILD)/ino_extern_stubs.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(JPEG_SRC) -o $(BUILD)/ino_jpeg.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(FLNKSRC) -o $(BUILD)/ino_flexlink.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(FLPSRC)  -o $(BUILD)/ino_flexphone.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(FLPLINK) -o $(BUILD)/ino_flexphonelink.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(CORE_SRC) -o $(BUILD)/ino_brcore.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(MEDIA_SRC) -o $(BUILD)/ino_media.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(MEM_SRC)   -o $(BUILD)/ino_mem.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(FALL_SRC)  -o $(BUILD)/ino_fall.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(VMSRC)     -o $(BUILD)/ino_appvm.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(GRANTSRC)  -o $(BUILD)/ino_appgrant.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(HOSTSRC)   -o $(BUILD)/ino_apphost.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(PKGFLAGS) -c $(PKGCORE) -o $(BUILD)/ino_pkgcore.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(VECSRC)  -o $(BUILD)/ino_vector.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(QRSRC)   -o $(BUILD)/ino_qr.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) $(INC) -c $(HTTPSRC) -o $(BUILD)/ino_http.o
	@$(CC) -O1 -g -w -Ivendor/cJSON -c $(CJSON) -o $(BUILD)/ino_cjson.o
	@$(CXX) $(SAN) -o $@ $(BUILD)/ino_compile.o $(BUILD)/ino_extern_stubs.o $(BUILD)/ino_jpeg.o \
	        $(BUILD)/ino_flexlink.o $(BUILD)/ino_flexphone.o $(BUILD)/ino_flexphonelink.o \
	        $(BUILD)/ino_brcore.o $(BUILD)/ino_media.o $(BUILD)/ino_mem.o $(BUILD)/ino_fall.o \
	        $(BUILD)/ino_appvm.o $(BUILD)/ino_appgrant.o $(BUILD)/ino_apphost.o \
	        $(BUILD)/ino_pkgcore.o $(BUILD)/ino_cjson.o \
	        $(BUILD)/ino_vector.o $(BUILD)/ino_qr.o $(BUILD)/ino_http.o -lcrypto

# La tabla de particiones de la version USB. No es codigo, pero una tabla
# mal cuadrada no falla al compilar: falla al grabar, o peor, arranca y se
# lleva por delante los archivos del usuario.
.PHONY: parts
parts:
	@python3 test_parts.py $(ULTRA)/flexos_ultra_usb.csv

run: ino parts $(BUILD)/test_ino $(BINS) $(TOOLS)
	@echo
	@$(BUILD)/test_ino
	@echo
	@$(BUILD)/test_flexlink
	@echo
	@$(BUILD)/test_flexphone
	@echo
	@$(BUILD)/test_flexlink_transport
	@echo
	@$(BUILD)/test_flexlink_vectors
	@echo
	@$(BUILD)/test_vault
	@echo
	@$(BUILD)/test_weather
	@echo
	@$(BUILD)/test_jpeg ../fixtures
	@echo
	@$(BUILD)/test_media
	@echo
	@$(BUILD)/test_mem
	@echo
	@$(BUILD)/test_fall
	@echo
	@$(BUILD)/test_appvm
	@echo
	@$(BUILD)/test_appgrant
	@echo
	@$(BUILD)/test_apphost
	@echo
	@$(BUILD)/test_flexpkg
	@echo
	@$(BUILD)/test_vector
	@echo
	@$(BUILD)/test_qr
	@echo
	@$(BUILD)/test_httpshare
	@echo
	@$(BUILD)/test_browser
	@echo
	@$(BUILD)/test_app
	@echo
	@$(BUILD)/test_bridge
	@echo
	@$(BUILD)/test_net ../fixtures
	@echo
	@echo "Todas las pruebas de host han pasado."
	@echo "Herramientas listas: $(BUILD)/jpegcheck (server/test/e2e.test.js)"
	@echo "                    $(BUILD)/flexpkgcheck y $(BUILD)/flexapprun (sdk/test/sdk.test.js)."

# El codigo de dispositivo cambia de presupuestos y de capacidades segun
# la placa, asi que se compila y se ejecuta en los tres perfiles.
all-boards:
	@for b in "" S3 ESP32; do \
	  echo "=================== perfil: $${b:-P4} ==================="; \
	  $(MAKE) --no-print-directory clean-dev BOARD=$$b >/dev/null; \
	  $(MAKE) --no-print-directory $(BUILD)/test_app $(BUILD)/test_bridge $(BUILD)/test_net BOARD=$$b || exit 1; \
	  $(BUILD)/test_app || exit 1; \
	  $(BUILD)/test_bridge || exit 1; \
	  $(BUILD)/test_net ../fixtures || exit 1; \
	done
	@$(MAKE) --no-print-directory clean-dev
	@echo "Los tres perfiles de placa compilan y pasan."

clean-dev:
	@rm -f $(BUILD)/test_app $(BUILD)/test_bridge $(BUILD)/test_net

fixtures:
	cd ../tools && npm install --silent && node make-fixtures.js

clean:
	rm -rf $(BUILD)
