#pragma once
#include <v8.h>
/* Native, isolate-private EventTarget brands shared by every realm. */
void js_event_target_install(v8::Local<v8::Context>, v8::Local<v8::Object>);
