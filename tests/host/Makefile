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
        $(BUILD)/test_vault $(BUILD)/test_weather $(BUILD)/test_flexlink $(BUILD)/test_flexphone $(BUILD)/test_flexlink_transport
TOOLS := $(BUILD)/jpegcheck

.PHONY: all run all-boards fixtures clean ino
all: run

$(BUILD):
	@mkdir -p $(BUILD)

# --- nucleo portable: con sanitizers ---
$(BUILD)/test_jpeg: test_jpeg.cpp $(JPEG_SRC) $(ROOT)/FlexOS_JPEG.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -pthread -o $@ test_jpeg.cpp $(JPEG_SRC)

$(BUILD)/test_browser: test_browser.cpp $(CORE_SRC) $(ROOT)/FlexOS_Browser.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_browser.cpp $(CORE_SRC)

# --- Flex Link / Flex Phone: protocolo y modelo del telefono ---
# Nucleo PURO (sin Arduino), asi que se compila igual que el del
# navegador: con AddressSanitizer y UndefinedBehavior Sanitizer.
# Aqui es obligatorio -- estos dos ficheros parsean bytes que llegan
# por el aire desde un telefono, y un desbordamiento en el
# reensamblador no puede quedarse en "parece que funciona".
FLNKSRC := $(ROOT)/FlexOS_FlexLink.cpp
FLPSRC  := $(ROOT)/FlexOS_FlexPhone.cpp

$(BUILD)/test_flexlink: test_flexlink.cpp $(FLNKSRC) $(ROOT)/FlexOS_FlexLink.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_flexlink.cpp $(FLNKSRC)

$(BUILD)/test_flexphone: test_flexphone.cpp $(FLPSRC) $(FLNKSRC) \
                         $(ROOT)/FlexOS_FlexPhone.h $(ROOT)/FlexOS_FlexLink.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_flexphone.cpp $(FLPSRC) $(FLNKSRC)

# El transporte: maquina de estados del enlace. En el PC la capacidad
# es NONE (no hay radio), igual que en el P4 sin el C6 preparado, asi
# que la prueba fuerza la capacidad para poder ejercitarla entera.
FLPLINK := $(ROOT)/FlexOS_FlexPhone_Link.cpp
$(BUILD)/test_flexlink_transport: test_flexlink_transport.cpp $(FLPLINK) $(FLPSRC) $(FLNKSRC) \
                         $(ROOT)/FlexOS_FlexPhone_Link.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_flexlink_transport.cpp $(FLPLINK) $(FLPSRC) $(FLNKSRC)

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
WXFLAGS := -Iwxstub -Istub -I$(ROOT) -DARDUINO=200 -Wno-format-truncation
$(BUILD)/test_weather: test_weather.cpp $(ROOT)/FlexOS_Weather.cpp $(ROOT)/FlexOS_Weather.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) $(WXFLAGS) -o $@ test_weather.cpp

VAULTFLAGS := -Ivaultstub -Iinostub -I$(ROOT) -DARDUINO=200 -Wno-format-truncation
$(BUILD)/test_vault: test_vault.cpp $(ROOT)/FlexOS_Vault.cpp $(ROOT)/FlexOS_Vault.h $(ROOT)/FlexOS_FS.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) $(VAULTFLAGS) -o $@ test_vault.cpp $(ROOT)/FlexOS_Vault.cpp -lcrypto

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
INO_SRC  := $(ROOT)/FlexOS_Ultra.ino
# Los PUENTES son cabeceras que el .ino incluye (Store, Account, Navegador,
# OTA): sin listarlos como dependencia, editar uno no rehacia la prueba y
# `make` daba por buena la version anterior.
INO_BRIDGES := $(wildcard $(ROOT)/FlexOS_*_Bridge.h)
INOFLAGS := -Iinostub -I$(ROOT) -DARDUINO=200
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
ino: ino_compile.cpp $(INO_SRC) $(INO_BRIDGES) check_protos.py check_wiring.py | $(BUILD)
	@python3 check_protos.py $(INO_SRC)
	@python3 check_wiring.py $(INO_SRC)
	@$(CXX) $(CXXSTD) -fsyntax-only $(INOWARN) $(INOFLAGS) -x c++ ino_compile.cpp
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
$(BUILD)/test_ino: ino_compile.cpp ino_extern_stubs.cpp $(INO_SRC) $(INO_BRIDGES) $(JPEG_SRC) \
                   $(FLNKSRC) $(FLPSRC) $(FLPLINK) $(CORE_SRC) | $(BUILD)
	@$(CXX) $(CXXSTD) -O1 -g $(INOWARN) $(SAN) $(INOFLAGS) -x c++ -c ino_compile.cpp -o $(BUILD)/ino_compile.o
	@$(CXX) $(CXXSTD) -O1 -g $(INOWARN) $(SAN) $(INOFLAGS) -c ino_extern_stubs.cpp -o $(BUILD)/ino_extern_stubs.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) -I$(ROOT) -c $(JPEG_SRC) -o $(BUILD)/ino_jpeg.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) -I$(ROOT) -c $(FLNKSRC) -o $(BUILD)/ino_flexlink.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) -I$(ROOT) -c $(FLPSRC)  -o $(BUILD)/ino_flexphone.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) -I$(ROOT) -c $(FLPLINK) -o $(BUILD)/ino_flexphonelink.o
	@$(CXX) $(CXXSTD) -O1 -g $(WARN) $(SAN) -I$(ROOT) -c $(CORE_SRC) -o $(BUILD)/ino_brcore.o
	@$(CXX) $(SAN) -o $@ $(BUILD)/ino_compile.o $(BUILD)/ino_extern_stubs.o $(BUILD)/ino_jpeg.o \
	        $(BUILD)/ino_flexlink.o $(BUILD)/ino_flexphone.o $(BUILD)/ino_flexphonelink.o \
	        $(BUILD)/ino_brcore.o

# La tabla de particiones de la version USB. No es codigo, pero una tabla
# mal cuadrada no falla al compilar: falla al grabar, o peor, arranca y se
# lleva por delante los archivos del usuario.
.PHONY: parts
parts:
	@python3 test_parts.py $(ROOT)/flexos_ultra_usb.csv

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
	@$(BUILD)/test_vault
	@echo
	@$(BUILD)/test_weather
	@echo
	@$(BUILD)/test_jpeg ../fixtures
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
	@echo "Herramienta lista: $(BUILD)/jpegcheck (la usa server/test/e2e.test.js)."

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
