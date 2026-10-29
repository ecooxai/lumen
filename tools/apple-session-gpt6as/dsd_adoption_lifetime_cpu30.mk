# Narrow adoption/disposal control, separate from the frozen 122-check fixture.
$(BUILD)/tests/test_declarative_shadow_adoption_lifetime.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_declarative_shadow_adoption_lifetime: $(call OBJ,tests/test_declarative_shadow_adoption_lifetime.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
