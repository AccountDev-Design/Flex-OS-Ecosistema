#pragma once
// #############################################################
//  CONSTRUCTOR DE IMAGENES flex-app-v1 PARA LAS PRUEBAS
//  ------------------------------------------------------------
//  Ensambla a mano bytecode FLXB v1: opcodes, tabla de funciones,
//  pool de constantes y cabecera. Lo comparten test_appvm.cpp (que
//  prueba la maquina) y test_apphost.cpp (que prueba el gestor), asi
//  que las dos baterias hablan exactamente del mismo formato.
//
//  El SDK escribe estos mismos bytes desde su ensamblador; sdk/test
//  lo comprueba contra el firmware.
// #############################################################
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <vector>
#pragma pop_macro("min")
#pragma pop_macro("max")
#include <stdint.h>

// ---- Opcodes (los mismos numeros del formato) ---------------------------
enum : uint8_t {
  OP_NOP = 0x00, OP_PUSH_I8 = 0x01, OP_PUSH_I32 = 0x02, OP_DUP = 0x03, OP_DROP = 0x04,
  OP_SWAP = 0x05, OP_OVER = 0x06,
  OP_LD_LOCAL = 0x10, OP_ST_LOCAL = 0x11, OP_LD_GLOBAL = 0x12, OP_ST_GLOBAL = 0x13,
  OP_LD8 = 0x18, OP_LD8U = 0x19, OP_LD16 = 0x1A, OP_LD16U = 0x1B, OP_LD32 = 0x1C,
  OP_ST8 = 0x1D, OP_ST16 = 0x1E, OP_ST32 = 0x1F,
  OP_KLD8U = 0x20, OP_KLD16U = 0x21, OP_KLD32 = 0x22,
  OP_ADD = 0x30, OP_SUB = 0x31, OP_MUL = 0x32, OP_DIV = 0x33, OP_MOD = 0x34, OP_NEG = 0x35,
  OP_AND = 0x36, OP_OR = 0x37, OP_XOR = 0x38, OP_NOT = 0x39, OP_SHL = 0x3A, OP_SHR = 0x3B,
  OP_USHR = 0x3C, OP_MIN = 0x3D, OP_MAX = 0x3E, OP_ABS = 0x3F,
  OP_EQ = 0x40, OP_NE = 0x41, OP_LT = 0x42, OP_LE = 0x43, OP_GT = 0x44, OP_GE = 0x45,
  OP_JMP = 0x50, OP_JZ = 0x51, OP_JNZ = 0x52, OP_CALL = 0x53,
  OP_RET = 0x54, OP_RETV = 0x55, OP_HALT = 0x56,
  OP_SYS = 0x60, OP_YIELD = 0x70, OP_TRAP = 0x7F
};

// ---- Constructor de imagenes FLXB --------------------------------------
struct Func { uint32_t off; uint8_t args, locals; };

struct Image {
  std::vector<uint8_t> code;
  std::vector<uint8_t> konst;
  std::vector<Func> funcs;
  uint32_t mem = 256;
  uint16_t globals = 4, stack = 32, frames = 32, calldepth = 8;
  uint16_t h[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

  void b(uint8_t v){ code.push_back(v); }
  void i8(uint8_t op, int8_t v){ b(op); b((uint8_t)v); }
  void u8(uint8_t op, uint8_t v){ b(op); b(v); }
  void u16(uint8_t op, uint16_t v){ b(op); b(v & 0xFF); b((v >> 8) & 0xFF); }
  void i32(int32_t v){ b(OP_PUSH_I32); for(int k = 0; k < 4; k++) b((uint8_t)((uint32_t)v >> (8 * k))); }
  size_t here() const { return code.size(); }
  void patchRel(size_t at, size_t target){
    int16_t rel = (int16_t)((int32_t)target - (int32_t)(at + 3));
    code[at + 1] = (uint8_t)(rel & 0xFF);
    code[at + 2] = (uint8_t)((rel >> 8) & 0xFF);
  }

  std::vector<uint8_t> build() const {
    std::vector<uint8_t> out(48, 0);
    out[0] = 'F'; out[1] = 'L'; out[2] = 'X'; out[3] = 'B';
    auto p16 = [&](size_t o, uint16_t v){ out[o] = v & 0xFF; out[o + 1] = (v >> 8) & 0xFF; };
    auto p32 = [&](size_t o, uint32_t v){ for(int k = 0; k < 4; k++) out[o + k] = (uint8_t)(v >> (8 * k)); };
    p16(4, 1); p16(6, 0);
    p32(8, (uint32_t)code.size());
    p32(12, (uint32_t)konst.size());
    p32(16, mem);
    p16(20, globals); p16(22, stack); p16(24, frames); p16(26, calldepth);
    p16(28, (uint16_t)funcs.size()); p16(30, 0);
    for(int i = 0; i < 4; i++) p16(32 + i * 2, h[i]);
    for(const Func& f : funcs){
      size_t o = out.size();
      out.resize(o + 8, 0);
      for(int k = 0; k < 4; k++) out[o + k] = (uint8_t)(f.off >> (8 * k));
      out[o + 4] = f.args; out[o + 5] = f.locals;
    }
    out.insert(out.end(), konst.begin(), konst.end());
    out.insert(out.end(), code.begin(), code.end());
    return out;
  }
};

