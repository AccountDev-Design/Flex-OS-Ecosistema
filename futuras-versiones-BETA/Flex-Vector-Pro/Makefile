# #############################################################
#  FLEX VECTOR PRO (BETA archivada)  ·  pruebas de los modulos portables
#  ------------------------------------------------------------
#  Esta bateria es la MISMA que corria en tests/host/ cuando la app
#  formaba parte del firmware activo. Sigue pasando aqui porque el motor
#  vectorial es nucleo PURO: no toca Arduino, ni el framebuffer, ni el
#  sistema de archivos.
#
#  FlexOS_HttpShare y FlexOS_QR ya NO viven aqui: Flex Web Server los
#  promovio al firmware activo (FlexOS_Ultra/) y sus pruebas corren en
#  tests/host/ (test_httpshare, test_qr) con el resto de la bateria.
#
#  Lo que NO se puede comprobar desde aqui es la interfaz: vivia dentro
#  de FlexOS_Ultra.ino y sus pruebas estan en
#  tests/ino_compile_testVectorPro.cpp.inc, que es un fragmento para
#  pegar de vuelta, no una unidad de traduccion.
#
#  Uso:  make        (compila y ejecuta la del motor vectorial)
#        make clean
# #############################################################
CXX      ?= g++
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wno-unused-parameter
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
COMMON   := $(CXXSTD) -O1 -g $(WARN)
MOD      := firmware-modules
BUILD    := build
BINS     := $(BUILD)/test_vector

.PHONY: all run clean
all: run

$(BUILD):
	mkdir -p $(BUILD)

# -lz es SOLO de la prueba: descomprime con zlib el PNG que genera el
# firmware y compara los pixeles uno a uno. El firmware NO enlaza zlib --
# su compresor es propio y cabe en FlexOS_Vector.cpp.
$(BUILD)/test_vector: tests/test_vector.cpp $(MOD)/FlexOS_Vector.cpp $(MOD)/FlexOS_Vector.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ tests/test_vector.cpp $(MOD)/FlexOS_Vector.cpp -lz

run: $(BINS)
	@$(BUILD)/test_vector

clean:
	rm -rf $(BUILD)
