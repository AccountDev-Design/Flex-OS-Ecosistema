# #############################################################
#  FLEX VECTOR PRO (BETA archivada)  ·  pruebas de los modulos portables
#  ------------------------------------------------------------
#  Estas tres baterias son las MISMAS que corrian en tests/host/
#  cuando la app formaba parte del firmware activo. Siguen pasando
#  aqui porque los tres modulos son nucleo PURO: no tocan Arduino, ni
#  el framebuffer, ni el sistema de archivos.
#
#  Lo que NO se puede comprobar desde aqui es la interfaz: vivia dentro
#  de FlexOS_Ultra.ino y sus pruebas estan en
#  tests/ino_compile_testVectorPro.cpp.inc, que es un fragmento para
#  pegar de vuelta, no una unidad de traduccion.
#
#  Uso:  make        (compila y ejecuta las tres)
#        make clean
# #############################################################
CXX      ?= g++
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wno-unused-parameter
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
COMMON   := $(CXXSTD) -O1 -g $(WARN)
MOD      := firmware-modules
BUILD    := build
BINS     := $(BUILD)/test_vector $(BUILD)/test_qr $(BUILD)/test_httpshare

.PHONY: all run clean
all: run

$(BUILD):
	mkdir -p $(BUILD)

# -lz es SOLO de la prueba: descomprime con zlib el PNG que genera el
# firmware y compara los pixeles uno a uno. El firmware NO enlaza zlib --
# su compresor es propio y cabe en FlexOS_Vector.cpp.
$(BUILD)/test_vector: tests/test_vector.cpp $(MOD)/FlexOS_Vector.cpp $(MOD)/FlexOS_Vector.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ tests/test_vector.cpp $(MOD)/FlexOS_Vector.cpp -lz

$(BUILD)/test_qr: tests/test_qr.cpp $(MOD)/FlexOS_QR.cpp $(MOD)/FlexOS_QR.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ tests/test_qr.cpp $(MOD)/FlexOS_QR.cpp

$(BUILD)/test_httpshare: tests/test_httpshare.cpp $(MOD)/FlexOS_HttpShare.cpp $(MOD)/FlexOS_HttpShare.h | $(BUILD)
	$(CXX) $(COMMON) $(SAN) -o $@ tests/test_httpshare.cpp $(MOD)/FlexOS_HttpShare.cpp

run: $(BINS)
	@$(BUILD)/test_vector
	@echo
	@$(BUILD)/test_qr
	@echo
	@$(BUILD)/test_httpshare

clean:
	rm -rf $(BUILD)
