#include "webapp/ui/helper/camera_container.h"


using namespace aergo::default_modules::frontend_module::webapp::ui::helper;



MJpegResource::~MJpegResource()
{
    beingDeleted();
    std::lock_guard<std::mutex> lk(m_);
    cont_ = nullptr;
}



void MJpegResource::pushJpeg(std::vector<uint8_t>&& jpeg)
{
    std::unique_lock<std::mutex> lk(m_);
    latest_ = std::move(jpeg);
    auto cont = cont_;                     // copy under lock
    lk.unlock();

    // Wake a waiting continuation (if any)
    if (cont) cont->haveMoreData();
}



void MJpegResource::handleRequest(const Wt::Http::Request& req, Wt::Http::Response& resp)
{
    std::vector<uint8_t> bytes;

    // Is this the first call or a resumed one?
    auto* c = req.continuation();

    if (!c) {
      // First call: set headers once
      resp.addHeader("Cache-Control", "no-store, max-age=0");
      resp.addHeader("Pragma", "no-cache");
      resp.setMimeType("multipart/x-mixed-replace;boundary=frame");

      // Prepare to wait for the first frame
      std::lock_guard<std::mutex> lk(m_);
      cont_ = resp.createContinuation();
      cont_->waitForMoreData();
      return;
    }

    // Resumed call: send one frame (latest)
    {
      std::lock_guard<std::mutex> lk(m_);
      bytes = std::move(latest_); // move the current frame
    }

    if (!bytes.empty()) {
      auto& out = resp.out();
      out << "--frame\r\n"
          << "Content-Type: image/jpeg\r\n"
          << "Content-Length: " << bytes.size() << "\r\n\r\n";
      out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      out << "\r\n";
    }

    // Arm the next wait
    {
      std::lock_guard<std::mutex> lk(m_);
      cont_ = resp.createContinuation();
      cont_->waitForMoreData();
    }
}



void MJpegResource::handleAbort(const Wt::Http::Request& req)
{
    std::lock_guard<std::mutex> lk(m_);
    cont_ = nullptr;
}



CameraContainer::CameraContainer()
{
    setStyleClass("camera-container");

    res_ = std::make_shared<MJpegResource>();

    // create img element
    img_ = addWidget(std::make_unique<Wt::WImage>(Wt::WLink(res_)));
    img_->setAlternateText("Camera");
    img_->setStyleClass("camera-image");
}



void CameraContainer::updateFrame(std::vector<uint8_t>&& jpeg_data)
{
    res_->pushJpeg(std::move(jpeg_data));
}