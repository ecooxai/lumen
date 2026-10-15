#include "../src/js/viewport_state.h"
#include <cassert>

int main() {
    JsViewportState state;
    assert(state.update(412, 800, 2) == 0);
    assert(state.update(412, 800, 2) == 0);
    assert(state.update(800, 412, 2) == JsViewportState::RESIZE);
    assert(state.update(800, 412, 2) == 0);
    assert(state.update(800, 412, 3) == JsViewportState::MEDIA);
    assert(state.update(412, 412, 2) == (JsViewportState::RESIZE | JsViewportState::MEDIA));
    assert(state.update(412, 250, 2) == JsViewportState::RESIZE);
    assert(state.update(412, 412, 2) == JsViewportState::RESIZE);
    for (int iteration = 0; iteration < 100000; iteration++)
        assert(state.update(412, 412, 2) == 0);
    assert(state.update(412, 412, 2, 0, 200) == JsViewportState::SCROLL);
    assert(state.update(412, 412, 2, 0, 200) == 0);
    assert(state.update(412, 412, 2, 20, 200) == JsViewportState::SCROLL);
    assert(state.update(500, 412, 3, 20, 0) == (JsViewportState::SCROLL | JsViewportState::RESIZE | JsViewportState::MEDIA));
    assert(state.update(412, 412, 2) == (JsViewportState::SCROLL | JsViewportState::RESIZE | JsViewportState::MEDIA));
    JsViewportState child;
    assert(child.update(100, 200, 1) == 0);
    assert(child.update(100, 201, 1) == JsViewportState::RESIZE);
    assert(state.update(412, 412, 2) == 0);
}
