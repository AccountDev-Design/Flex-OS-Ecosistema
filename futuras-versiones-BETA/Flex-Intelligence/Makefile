CXX      ?= g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
            -fsanitize=address,undefined -fno-omit-frame-pointer
MOD      := firmware-modules
HOSTSTUB := ../../tests/host/inostub
BUILD    := build
BINS     := $(BUILD)/test_spell $(BUILD)/test_cowork $(BUILD)/test_ai

.PHONY: all run clean
all: run

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_spell: tests/test_spell.cpp $(MOD)/FlexOS_Spell.cpp $(MOD)/FlexOS_Spell.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(MOD) -o $@ tests/test_spell.cpp $(MOD)/FlexOS_Spell.cpp

$(BUILD)/test_cowork: tests/test_cowork.cpp $(MOD)/FlexOS_Cowork.cpp $(MOD)/FlexOS_Cowork.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(MOD) -o $@ tests/test_cowork.cpp $(MOD)/FlexOS_Cowork.cpp

$(BUILD)/test_ai: tests/test_ai.cpp $(MOD)/FlexOS_AI.cpp $(MOD)/FlexOS_AI.h $(MOD)/FlexOS_Cowork.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(MOD) -I$(HOSTSTUB) -o $@ tests/test_ai.cpp $(MOD)/FlexOS_AI.cpp $(MOD)/FlexOS_Cowork.cpp

run: $(BINS)
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/test_spell
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/test_cowork
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/test_ai

clean:
	rm -rf $(BUILD)
