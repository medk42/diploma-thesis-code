#include "webapp/ui/helper/scene_container.h"

#include <Wt/WServer.h>

#include <sstream>
#include <iomanip>
#include <iostream>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;

enum : uint8_t
{
    OP_ADD_BOX = 1,
    OP_UPDATE_BOX = 2,
    OP_REMOVE = 3
};

// Simple LE writers
static inline void w32(std::vector<char> &b, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(char((v >> (8 * i)) & 0xFF));
}
static inline void wf(std::vector<char> &b, float f)
{
    uint32_t v;
    static_assert(sizeof(float) == 4, "");
    std::memcpy(&v, &f, 4);
    w32(b, v);
}



// void SceneSocket::sendAddBox(uint32_t id, float x, float y, float z, float sx, float sy, float sz)
// {
//     // Frame layout:
//     // [u32 magic 'SCN1'][u32 seq][u16 count=1]
//     //   [u8 op][u32 id][6*f32 x,y,z,sx,sy,sz]
//     std::vector<char> b;
//     w32(b, 0x314E4353u); // 'SCN1' LE
//     w32(b, ++seq_);
//     b.push_back(1);
//     b.push_back(0); // u16 count LE (1)
//     b.push_back(OP_ADD_BOX);
//     w32(b, id);
//     wf(b, x);
//     wf(b, y);
//     wf(b, z);
//     wf(b, sx);
//     wf(b, sy);
//     wf(b, sz);
//     sendFrame(std::move(b));
// }

// ----------------- SceneContainer (widget + JS) -----------------

SceneContainer::SceneContainer()
{
    setId("scene-container");
    setStyleClass("scene-container");

    socket_ = std::make_unique<SceneSocket>();

    auto *app = Wt::WApplication::instance();
    app->doJavaScript("window.CounterWS_URL = " + Wt::WString(socket_->url()).jsStringLiteral() + ";");
    app->require("/static/scene_frontend.js");

    // TODO request all modules to register shapes
    // TODO modules will call announce() at startup (without registering), SceneContainer will 
    // request registration on modules that it doesn't yet know
    // TODO if scene container receives update from a module that it doesn't know, it requests registration
    // TODO registration will include the current scene state for that module
}