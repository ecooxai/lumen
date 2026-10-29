$(BUILD)/tests/test_worker_async_lifecycle.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG -I$(BUILD)/src/js
$(BUILD)/tests/test_worker_async_lifecycle.cc.o: $(BUILD)/src/js/url_search_params.inc $(BUILD)/src/js/dom_exception.inc $(BUILD)/src/js/url_interface.inc $(BUILD)/src/js/file_reader.inc $(BUILD)/src/js/blob_file.inc $(BUILD)/src/js/headers.inc $(BUILD)/src/js/text_codec.inc $(BUILD)/src/js/web_streams.inc
$(BUILD)/test_worker_async_lifecycle: $(call OBJ,tests/test_worker_async_lifecycle.cc $(CORE_SRC) $(filter-out src/js/worker.cc,$(JS_SRC))) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
