#pragma once


#include <Wt/WContainerWidget.h>
#include <Wt/WMemoryResource.h>
#include <Wt/WImage.h>


namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class CameraContainer : public Wt::WContainerWidget
    {
    public:
        CameraContainer();

        void updateFrame(const std::vector<uint8_t>& jpeg_data);
    
    private:
        std::shared_ptr<Wt::WMemoryResource> res_;
        Wt::WImage* img_{ nullptr };
    };
}