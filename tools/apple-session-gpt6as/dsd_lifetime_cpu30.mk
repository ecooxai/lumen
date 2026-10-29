# Supplemental project regression target; use with -f Makefile -f this file.
$(BUILD)/tests/test_declarative_shadow_lifetime.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_declarative_shadow_lifetime: $(call OBJ,tests/test_declarative_shadow_lifetime.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
