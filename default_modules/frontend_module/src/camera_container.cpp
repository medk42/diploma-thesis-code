#include "webapp/ui/helper/camera_container.h"


using namespace aergo::default_modules::frontend_module::webapp::ui::helper;


CameraContainer::CameraContainer()
{
    setStyleClass("camera-container");

    res_ = std::make_shared<Wt::WMemoryResource>("image/jpeg");

    // create img element
    img_ = addWidget(std::make_unique<Wt::WImage>(Wt::WLink(res_)));
    img_->setAlternateText("Camera");
}



void CameraContainer::updateFrame(const std::vector<uint8_t>& jpeg_data)
{
    res_->setData(jpeg_data);
    res_->setChanged();
}