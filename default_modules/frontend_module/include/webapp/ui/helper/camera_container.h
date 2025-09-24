#pragma once


#include <Wt/WContainerWidget.h>
#include <Wt/WImage.h>
#include <Wt/WResource.h>
#include <Wt/Http/Request.h>
#include <Wt/Http/Response.h>


#include <cstdint>


namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class MJpegResource : public Wt::WResource {
    public:
        MJpegResource() {}
        ~MJpegResource() override;

        void pushJpeg(std::vector<uint8_t>&& jpeg);
        void handleRequest(const Wt::Http::Request&, Wt::Http::Response&) override;
        void handleAbort(const Wt::Http::Request&) override;

    private:
        std::mutex m_;
        std::vector<uint8_t> latest_;
        Wt::Http::ResponseContinuation* cont_ = nullptr; // only one <img> per resource in your setup
    };


    class CameraContainer : public Wt::WContainerWidget
    {
    public:
        CameraContainer();

        void updateFrame(std::vector<uint8_t>&& jpeg_data);
    
    private:
        std::shared_ptr<MJpegResource> res_;
        Wt::WImage* img_{ nullptr };
    };
}