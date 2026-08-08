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

BINS := $(BUILD)/test_jpeg $(BUILD)/test_browser $(BUILD)/test_app $(BUILD)/test_bridge

.PHONY: all run all-boards fixtures clean
all: run

$(BUILD):
	@mkdir -p $(BUILD)

# --- nucleo portable: con sanitizers ---
$(BUILD)/test_jpeg: test_jpeg.cpp $(JPEG_SRC) $(ROOT)/FlexOS_JPEG.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_jpeg.cpp $(JPEG_SRC)

$(BUILD)/test_browser: test_browser.cpp $(CORE_SRC) $(ROOT)/FlexOS_Browser.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ test_browser.cpp $(CORE_SRC)

# --- codigo de dispositivo: con el entorno simulado ---
# Sin sanitizers a proposito: el simulador define las funciones de
# FreeRTOS y de WiFi que el modulo espera, y ASan no aporta nada aqui
# que no cubra ya el contador de reservas del propio test.
$(BUILD)/test_app: test_app.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC) | $(BUILD)
	$(CXX) $(COMMON) $(DEVFLAGS) -o $@ test_app.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC)

$(BUILD)/test_bridge: test_bridge.cpp $(ROOT)/FlexOS_Browser_Bridge.h $(APP_SRC) $(CORE_SRC) $(JPEG_SRC) | $(BUILD)
	$(CXX) $(COMMON) $(DEVFLAGS) -Wno-unused-function -o $@ test_bridge.cpp $(APP_SRC) $(CORE_SRC) $(JPEG_SRC)

run: $(BINS)
	@echo
	@$(BUILD)/test_jpeg ../fixtures
	@echo
	@$(BUILD)/test_browser
	@echo
	@$(BUILD)/test_app
	@echo
	@$(BUILD)/test_bridge
	@echo
	@echo "Todas las pruebas de host han pasado."

# El codigo de dispositivo cambia de presupuestos y de capacidades segun
# la placa, asi que se compila y se ejecuta en los tres perfiles.
all-boards:
	@for b in "" S3 ESP32; do \
	  echo "=================== perfil: $${b:-P4} ==================="; \
	  $(MAKE) --no-print-directory clean-dev BOARD=$$b >/dev/null; \
	  $(MAKE) --no-print-directory $(BUILD)/test_app $(BUILD)/test_bridge BOARD=$$b || exit 1; \
	  $(BUILD)/test_app || exit 1; \
	  $(BUILD)/test_bridge || exit 1; \
	done
	@$(MAKE) --no-print-directory clean-dev
	@echo "Los tres perfiles de placa compilan y pasan."

clean-dev:
	@rm -f $(BUILD)/test_app $(BUILD)/test_bridge

fixtures:
	cd ../tools && npm install --silent && node make-fixtures.js

clean:
	rm -rf $(BUILD)
