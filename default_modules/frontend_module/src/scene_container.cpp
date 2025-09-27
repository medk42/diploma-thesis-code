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

SceneSocket::SceneSocket() { setTakesUpdateLock(false); } // we don't touch widgets here
SceneSocket::~SceneSocket() { shutdown(); }

std::unique_ptr<Wt::WWebSocketConnection>
SceneSocket::handleConnect(const Wt::Http::Request &)
{
    auto c = std::make_unique<Wt::WWebSocketConnection>(this, Wt::WServer::instance()->ioService());
    {
        std::lock_guard<std::mutex> lk(m_);
        conn_ = c.get();
        sending_ = false;
        q_.clear();
        seq_ = 0;
    }
    // Optional: tune ping/timeout or max sizes
    c->setPingTimeout(30, 60);
    c->setMaximumReceivedSize(2 * 1024 * 1024, 32 * 1024 * 1024);

    // Backpressure: when a send finishes, push next
    c->done().connect([this](const Wt::AsioWrapper::error_code& ec) {
        std::lock_guard<std::mutex> lk(m_);
        sending_ = false;
        if (!ec) 
            sendNextLocked(); 
    });

    // If you want client->server messages (e.g., ACKs), implement:
    // c->handleMessage(const std::vector<char>&) override in a subclass; or connect to a signal if you wrap it.

    return c;
}

void SceneSocket::sendNextLocked()
{
    if (sending_ || !conn_ || q_.empty())
        return;
    sending_ = true;
    auto &front = q_.front();
    // send binary; returns false if previous still in flight (we guard above)
    bool queued = conn_->sendMessage(front);
    if (queued)
    {
        q_.pop_front(); // Wt copies the buffer; safe to pop
    }
    else
    {
        // Previous send not done yet; try later on done()
        sending_ = false;
    }
}

void SceneSocket::sendFrame(std::vector<char> &&frame)
{
    std::lock_guard<std::mutex> lk(m_);
    q_.emplace_back(std::move(frame));
    sendNextLocked();
}

void SceneSocket::sendAddBox(uint32_t id, float x, float y, float z, float sx, float sy, float sz)
{
    // Frame layout:
    // [u32 magic 'SCN1'][u32 seq][u16 count=1]
    //   [u8 op][u32 id][6*f32 x,y,z,sx,sy,sz]
    std::vector<char> b;
    w32(b, 0x314E4353u); // 'SCN1' LE
    w32(b, ++seq_);
    b.push_back(1);
    b.push_back(0); // u16 count LE (1)
    b.push_back(OP_ADD_BOX);
    w32(b, id);
    wf(b, x);
    wf(b, y);
    wf(b, z);
    wf(b, sx);
    wf(b, sy);
    wf(b, sz);
    sendFrame(std::move(b));
}

// ----------------- SceneContainer (widget + JS) -----------------

SceneContainer::SceneContainer(int w, int h)
{
    setId("scene-container");
    setStyleClass("scene-container");

    // DOM & styling
    static std::atomic<int> counter{0};
    containerId_ = "scene_" + std::to_string(++counter);
    // setInline(true);
    // setAttributeValue("style",
    //                   "position:relative;display:inline-block;"
    //                   "width:" +
    //                       std::to_string(w) + "px;"
    //                                           "height:" +
    //                       std::to_string(h) + "px;"
    //                                           "background:#111;");

    counterSocket_ = std::make_unique<CounterSocket>();

    auto *app = Wt::WApplication::instance();
    app->doJavaScript("window.CounterWS_URL = " + Wt::WString(counterSocket_->url()).jsStringLiteral() + ";");
    app->require("/static/scene_frontend.js");


    // // Per-session WS endpoint
    // socket_ = std::make_unique<SceneSocket>();
    // std::cout << "SceneContainer: WS URL " << socket_->url() << std::endl;

    // // Bootstrap Three + open WS
    // bootstrapThree(socket_->url());
}

void SceneContainer::bootstrapThree(const std::string &wsUrl)
{
    // Boot script: load three, then create scene and WS client
    std::string boot = R"JS(
    (function(){
      const cid = 'CID';
      const wsUrl = 'WSURL';
      const host = document.getElementById(cid);
      if (!window.__wtScenes) window.__wtScenes = {};

      // Create renderer
      const renderer = new THREE.WebGLRenderer({antialias:true});
      renderer.setPixelRatio(window.devicePixelRatio||1);
      renderer.setSize(host.clientWidth, host.clientHeight, false);
      host.appendChild(renderer.domElement);

      // Scene/camera/lights
      const scene = new THREE.Scene();
      scene.background = new THREE.Color(0x111111);
      const camera = new THREE.PerspectiveCamera(60, host.clientWidth/host.clientHeight, 0.1, 2000);
      camera.position.set(3,2,6); camera.lookAt(0,0,0);
      scene.add(new THREE.HemisphereLight(0xffffff, 0x404040, 0.7));
      const dir = new THREE.DirectionalLight(0xffffff, 0.8); dir.position.set(3,4,5); scene.add(dir);

      // Object table
      const objects = new Map(); // id -> Mesh

      function addBox(id, x,y,z, sx,sy,sz){
        const geo = new THREE.BoxGeometry(sx,sy,sz);
        const mat = new THREE.MeshStandardMaterial({color:0x6699ff, metalness:0.2, roughness:0.7});
        const m = new THREE.Mesh(geo, mat);
        m.position.set(x,y,z);
        scene.add(m);
        objects.set(id, m);
      }

      function updateBox(id, x,y,z, sx,sy,sz){
        const m = objects.get(id);
        if (!m) return; // or request resync
        m.position.set(x,y,z);
        m.scale.set(sx, sy, sz);
      }

      function removeObj(id){
        const m = objects.get(id);
        if (m){ scene.remove(m); m.geometry.dispose(); m.material.dispose(); objects.delete(id); }
      }

      // Parse binary frames (LE):
      // [u32 magic 'SCN1'][u32 seq][u16 count] then commands...
      // ADD_BOX: [u8 op=1][u32 id][6*f32]
      // UPDATE:  [u8 op=2][u32 id][6*f32]
      // REMOVE:  [u8 op=3][u32 id]
      function onFrame(buf){
        const dv = new DataView(buf);
        let off = 0;
        const magic = dv.getUint32(off, true); off+=4;
        const seq = dv.getUint32(off, true); off+=4;
        const count = dv.getUint16(off, true); off+=2;
        for (let i=0;i<count;i++){
          const op = dv.getUint8(off); off+=1;
          const id = dv.getUint32(off, true); off+=4;
          if (op===1 || op===2){
            const x=dv.getFloat32(off,true), y=dv.getFloat32(off+4,true), z=dv.getFloat32(off+8,true);
            const sx=dv.getFloat32(off+12,true), sy=dv.getFloat32(off+16,true), sz=dv.getFloat32(off+20,true);
            off+=24;
            if (op===1) addBox(id,x,y,z,sx,sy,sz);
            else updateBox(id,x,y,z,sx,sy,sz);
          } else if (op===3){
            removeObj(id);
          } else {
            console.warn('Unknown op', op);
          }
        }
      }

      // WS client
      const ws = new WebSocket(wsUrl);
      ws.binaryType = 'arraybuffer'; // receive binary, not blobs
      ws.onmessage = (ev)=>{
        if (ev.data instanceof ArrayBuffer) onFrame(ev.data);
        else if (ev.data && ev.data.arrayBuffer) { ev.data.arrayBuffer().then(onFrame); } // blob fallback
      };
      ws.onopen = ()=>console.log('WS open', wsUrl);
      ws.onclose = (e)=>console.log('WS closed', e.code, e.reason);
      ws.onerror = (e)=>console.error('WS error', e);

      function onResize(){
        const w = host.clientWidth, h = host.clientHeight;
        renderer.setSize(w,h,false); camera.aspect = w/h; camera.updateProjectionMatrix();
      }
      window.addEventListener('resize', onResize);

      (function animate(){ requestAnimationFrame(animate); renderer.render(scene, camera); })();
      onResize();
      window.__wtScenes[cid] = {scene,camera,renderer,objects,ws};
    })();
  )JS";

    // Fill placeholders
    auto s = boot.find("CID");
    boot.replace(s, 3, containerId_);
    auto t = boot.find("WSURL");
    boot.replace(t, 5, wsUrl);

    auto *app = Wt::WApplication::instance();

    // Ensure three.js is loaded first, then run boot script
    app->require("/static/three.min.js", "THREE");
    app->require("/static/OrbitControls.js");
    app->doJavaScript(boot);
}

uint32_t SceneContainer::addBox(float x, float y, float z, float sx, float sy, float sz)
{
    uint32_t id = nextId_++;
    socket_->sendAddBox(id, x, y, z, sx, sy, sz);
    return id;
}