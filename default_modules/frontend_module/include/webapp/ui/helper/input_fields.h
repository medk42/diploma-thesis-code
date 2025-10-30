#pragma once

#include "parameter_value.h"

#include <Wt/WLineEdit.h>
#include <Wt/WSignal.h>
#include <string>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iomanip>
#include <Wt/WContainerWidget.h>
#include <Wt/WCheckBox.h>
#include <Wt/WApplication.h>
#include <Wt/WLength.h>
#include <Wt/WJavaScript.h>

#include <Wt/WContainerWidget.h>
#include <Wt/WComboBox.h>
#include <optional>
#include <vector>
#include <string>
#include <cmath>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    /// @brief Value may initialize as invalid or valid. If invalid, first successful user input or setValue() makes it valid.
    /// Any valid user input or setValue() call replaces the current value and emits changed().
    /// When a value is valid, it remains valid until resetValue() is called. 
    /// Result of resetValue() can be an invalid value (e.g. no enum choice selected) or no value (e.g. for numeric types or empty string <- these can be valid).
    class IParamInput
    {
    public:
        virtual ~IParamInput() = default;

        virtual bool hasValue() const = 0;                 // whether parameter has a valid value
        virtual value_opt_t value() const = 0;             // returns parameter value or nullopt if not valid
        virtual void resetValue() = 0;                     // reset to invalid value (or empty if no invalid state)
        virtual void setValue(const value_t& value) = 0;   // programmatic set (no signal emitted), value may not be accepted (type mismatch or out of range), invalid value will not replace valid value
        virtual Wt::Signal<value_t>& changed() = 0;        // signal change of value, value is guaranteed to be valid until reset
    };


    // ---------- Traits ----------
    template <typename T> struct NumTraits;

    // int64_t
    template <> struct NumTraits<int64_t> {
        static inline const char* inputType()   { return "text"; }    // reliable minus on mobile
        static inline const char* inputMode()   { return "numeric"; }
        static inline const char* pattern()     { return "-?[0-9]*"; }
        static int64_t parse(const std::string& s) {
            size_t pos = 0; long long v = std::stoll(s, &pos);
            if (pos != s.size()) throw std::invalid_argument("junk");
            return static_cast<int64_t>(v);
        }
        static std::string format(int64_t v) { return std::to_string(v); }
        static int64_t lowest()  { return std::numeric_limits<int64_t>::lowest(); }
        static int64_t highest() { return std::numeric_limits<int64_t>::max(); }
    };

    // double
    template <> struct NumTraits<double> {
        static inline const char* inputType()   { return "text"; }    // shows decimal keypad
        static inline const char* inputMode()   { return "decimal"; }
        // accept dot or comma; optional minus; optional decimals
        static inline const char* pattern()     { return "-?[0-9]*([\\.,][0-9]+)?"; }
        static double parse(std::string s) {
            std::replace(s.begin(), s.end(), ',', '.'); // be lenient to locale
            size_t pos = 0; double v = std::stod(s, &pos);
            if (pos != s.size()) throw std::invalid_argument("junk");
            return v;
        }
        static std::string format(double v) {
            std::ostringstream oss; oss << std::setprecision(15) << std::noshowpoint << std::defaultfloat << v;
            return oss.str();
        }
        static double lowest()  { return -std::numeric_limits<double>::infinity(); }
        static double highest() { return  std::numeric_limits<double>::infinity(); }
    };

    // ---------- Widget ----------
    template <typename T>
    class NumericLineEdit : public Wt::WLineEdit, public IParamInput {
    public:
        NumericLineEdit(bool has_value, T value, bool has_min, T minV, bool has_max, T maxV)
            : has_value_(has_value), value_(value), has_min_(has_min), has_max_(has_max),
            min_(has_min ? minV : NumTraits<T>::lowest()),
            max_(has_max ? maxV : NumTraits<T>::highest())
        {
            // Ensure min <= max
            if (has_min_ && has_max_ && min_ > max_) { has_min_ = has_max_ = false; }

            // Mobile keyboard + native hints
            this->setAttributeValue("type",       NumTraits<T>::inputType());
            this->setAttributeValue("inputmode",  NumTraits<T>::inputMode());
            this->setAttributeValue("pattern",    NumTraits<T>::pattern());

            this->addStyleClass("input-numeric");   // your CSS

            this->enterPressed().connect(this, &NumericLineEdit::validateAndEmit);
            this->blurred().connect(this,      &NumericLineEdit::validateAndEmit);

            if (has_value) setText(toStr(clamp(value_)));
            if (clamp(value_) != value_) has_value_ = false; // out of range, so no valid value
        }

        bool hasValue() const override { return has_value_; }
        value_opt_t value() const override { return hasValue() ? value_opt_t(value_) : std::nullopt; }
        void resetValue() override { has_value_ = false; setText(""); }

        // programmatic set (no emit)
        void setValue(const value_t& value) override {
            if (!std::holds_alternative<T>(value)) {
                return;
            }

            T v = std::get<T>(value);

            value_ = clamp(v);
            has_value_ = true;
            setText(toStr(value_));
        }

        // signal with T
        Wt::Signal<value_t>& changed() override { return changed_; }

    private:
        static std::string toStr(T v) { return NumTraits<T>::format(v); }

        T clamp(T v) const {
            if (has_min_ && v < min_) v = min_;
            if (has_max_ && v > max_) v = max_;
            return v;
        }

        void validateAndEmit() {
            try {
                T v = NumTraits<T>::parse(text().toUTF8());
                v = clamp(v);
                value_ = v;
                has_value_ = true;
                setText(toStr(v));
                changed_.emit(v);
            } catch (...) {
                // revert gracefully
                if (has_value_) setText(toStr(value_));
                else setText("");
            }
        }

        // state
        bool has_value_{false}, has_min_{false}, has_max_{false};
        T value_{};
        T min_{}, max_{};
        Wt::Signal<value_t> changed_;
    };



    class TextLineEdit : public Wt::WLineEdit, public IParamInput {
    public:
        TextLineEdit(std::string default_value)
            : value_(default_value)
        {
            // Mobile keyboard + native hints
            this->setAttributeValue("type",       "text");
            this->setAttributeValue("inputmode",  "text");

            this->addStyleClass("input-text");   // your CSS

            this->enterPressed().connect(this, &TextLineEdit::Emit);
            this->blurred().connect(this,      &TextLineEdit::Emit);

            setText(value_);
        }

        bool hasValue() const override { return true; }
        value_opt_t value() const override { return value_; }
        void resetValue() override { value_ = ""; setText(""); }

        // programmatic set (no emit)
        void setValue(const value_t& value) override {
            if (!std::holds_alternative<std::string>(value)) {
                return;
            }

            std::string v = std::get<std::string>(value);

            value_ = v;
            setText(value_);
        }

        // signal with T
        Wt::Signal<value_t>& changed() override { return changed_; }

    private:

        void Emit() {
            value_ = text().toUTF8();
            changed_.emit(value_);
        }

        // state
        std::string value_;
        Wt::Signal<value_t> changed_;
    };




    // ToggleCheckbox.h

    class ToggleCheckbox : public Wt::WCheckBox, public IParamInput {
    public:
        explicit ToggleCheckbox(bool on = false) {
            addStyleClass(on ? "input-checkbox checked" : "input-checkbox");
            setText("");          // label elsewhere
            setInline(true);
            setChecked(on);

            clicked().connect([this]{
                changed_.emit(isChecked());
                if (isChecked()) addStyleClass("checked");
                else             removeStyleClass("checked");
            });
        }

        bool hasValue() const override { return true; }
        value_opt_t value() const override { return isChecked(); }
        void resetValue() override { setValue(false); }

        void setValue(const value_t& value) override {
            if (!std::holds_alternative<bool>(value)) {
                return;
            }

            bool v = std::get<bool>(value);
            setChecked(v);
            if (v) addStyleClass("checked");
            else   removeStyleClass("checked");
        }

        Wt::Signal<value_t>& changed() override { return changed_; }

    private:
        Wt::Signal<value_t> changed_;
    };



    class NumericSlider : public Wt::WContainerWidget {
    public:
        explicit NumericSlider(double min, double max, double value)
        : min_(min), max_(max) {

            if (max_ <= min_) max_ = min_ + 1.0;
            value_ = clamp(value, min_, max_);
            step_ = autoStep(min_, max_);

            addStyleClass("ns");       // container
            addStyleClass("ns--flex"); // stretch to fill available width

            // Track
            track_ = addNew<Wt::WContainerWidget>();
            track_->addStyleClass("ns__track");
            track_->setAttributeValue("tabindex", "-1"); // not keyboard-focusable

            // Fill (progress)
            fill_ = track_->addNew<Wt::WContainerWidget>();
            fill_->addStyleClass("ns__fill");

            // Thumb
            thumb_ = track_->addNew<Wt::WContainerWidget>();
            thumb_->addStyleClass("ns__thumb");
            thumb_->setAttributeValue("tabindex", "-1");

                    // after creating track_ / thumb_ in the ctor:
            track_->setPositionScheme(Wt::PositionScheme::Relative);           // reference for absolute children
            thumb_->setPositionScheme(Wt::PositionScheme::Absolute);           // so setOffsets() writes 'left:'

            // Signals from browser with normalized [0..1] ratio
            jsMove_   = std::make_unique<Wt::JSignal<double>>(this, "move");
            jsCommit_ = std::make_unique<Wt::JSignal<double>>(this, "commit");

            jsMove_->connect(this, [this](double r){ setValueFromRatio(r, /*emitInput*/true,  /*emitChange*/false); });
            jsCommit_->connect(this, [this](double r){ setValueFromRatio(r, /*emitInput*/true,  /*emitChange*/true ); });

            // Initial paint
            paintFromValue();
        }

        // API
        void setValue(double v) {
            value_ = clamp(v, min_, max_);
            paintFromValue();
        }

        // During-drag updates
        Wt::Signal<double>& input()   { return input_; }   // fires continuously while dragging
        Wt::Signal<double>& changed() { return changed_; } // fires on release / programmatic set

        double value() const { return value_; }

    private:
        
        bool clientJsInstalled_ = false;

        // Widgets
        Wt::WContainerWidget *track_{nullptr}, *fill_{nullptr}, *thumb_{nullptr};

        // Data
        double min_{0}, max_{1}, value_{0.5};
        double step_;

        // Signals
        std::unique_ptr<Wt::JSignal<double>> jsMove_, jsCommit_;
        Wt::Signal<double> input_, changed_;

        static double clamp(double v, double a, double b) {
            return v < a ? a : (v > b ? b : v);
        }

        // "Nice" automatic step for doubles: ~200 steps across the range
        double autoStep(double min, double max) const {
            const double target = (max - min) / 200.0;
            if (target <= 0) return 0.0;
            double pow10 = std::pow(10.0, std::floor(std::log10(target)));
            double base = target / pow10; // in [1..10)
            double nice = (base < 1.5) ? 1.0 : (base < 3.5) ? 2.0 : (base < 7.5) ? 5.0 : 10.0;
            return nice * pow10;
        }

        double toRatio(double v) const { return (v - min_) / (max_ - min_); }
        double fromRatio(double r) const { return min_ + r * (max_ - min_); }

        double snap(double v) const {
            double k = std::round((v - min_) / step_);
            return clamp(min_ + k * step_, min_, max_);
        }

        void paintFromValue() {
            const double r = toRatio(value_);
            fill_->setWidth(Wt::WLength(r * 100.0, Wt::LengthUnit::Percentage));
            // thumb is centered via CSS transform; just set left as %
            thumb_->setOffsets(Wt::WLength(r * 100.0, Wt::LengthUnit::Percentage), Wt::Side::Left);
        }

        void setValueFromRatio(double r, bool emitInputSig, bool emitChangeSig) {
            r = clamp(r, 0.0, 1.0);
            double v = snap(fromRatio(r));
            if (v == value_)
            {
                if (emitInputSig)  input_.emit(value_);
                if (emitChangeSig) changed_.emit(value_);
                return;
            }
            value_ = v;
            paintFromValue();
            if (emitInputSig)  input_.emit(value_);
            if (emitChangeSig) changed_.emit(value_);
        }

        void installClientHandlers() {
            // Attach pointer/touch listeners that compute a ratio and call our JSignals.
            auto* app = Wt::WApplication::instance();
            const std::string tId = track_->id();
            const std::string jsMoveCall   = jsMove_->createCall({"r"});
            const std::string jsCommitCall = jsCommit_->createCall({"r"});

            std::string js =
            "(function(){"
            "  const track = document.getElementById('" + tId + "');"
            "  if(!track) return;"
            "  const rectOf = () => track.getBoundingClientRect();"
            "  const clamp = (x,a,b)=>Math.min(b,Math.max(a,x));"
            "  let dragging=false;"
            "  const ratioFromClientX = (cx)=>{ const r=rectOf(); return (cx - r.left)/r.width; };"
            "  const fireMove   = (r)=>{ var rr=clamp(r,0,1); var r=rr; " + jsMoveCall   + "; };"
            "  const fireCommit = (r)=>{ var rr=clamp(r,0,1); var r=rr; " + jsCommitCall + "; };"
            "  const onDown=(e)=>{ dragging=true; const p=(e.touches?e.touches[0].clientX:e.clientX); fireMove(ratioFromClientX(p)); e.preventDefault(); };"
            "  const onMove=(e)=>{ if(!dragging) return; const p=(e.touches?e.touches[0].clientX:e.clientX); fireMove(ratioFromClientX(p)); e.preventDefault(); };"
            "  const onUp  =(e)=>{ if(!dragging) return; dragging=false; const p=(e.changedTouches?e.changedTouches[0].clientX:e.clientX); fireCommit(ratioFromClientX(p)); e.preventDefault(); };"
            "  track.addEventListener('mousedown', onDown);"
            "  document.addEventListener('mousemove', onMove);"
            "  document.addEventListener('mouseup', onUp);"
            "  track.addEventListener('touchstart', onDown, {passive:false});"
            "  document.addEventListener('touchmove', onMove, {passive:false});"
            "  document.addEventListener('touchend', onUp, {passive:false});"
            "  track.setAttribute('tabindex','-1');"
            "  track.style.touchAction='none';"
            "})();";

            app->doJavaScript(js);
        }

    protected:
        void load() override {
            Wt::WContainerWidget::load();        // keep base behavior
            if (!clientJsInstalled_) {
            clientJsInstalled_ = true;
            installClientHandlers();            // your JS attach function
            }
        }
    };



    class SliderLineEdit : public Wt::WContainerWidget, public IParamInput
    {
    public:
        SliderLineEdit(double min, double max, double value)
        : min_(min), max_(max)
        {
            addStyleClass("input-slider");

            if (max <= min) max = min + 1.0;
            value = (value < min) ? min : ((value > max) ? max : value);
            
            slider_ = addNew<NumericSlider>(min, max, value);
            line_edit_ = addNew<NumericLineEdit<double>>(true, value, true, min, true, max);

            slider_->input().connect(this, [this](double v) { 
                line_edit_->setValue(v); 
            });
            slider_->changed().connect(this, [this](double v) { 
                line_edit_->setValue(v); 
                changed_.emit(v);
            });

            line_edit_->changed().connect(this, [this](value_t v) {
                slider_->setValue(std::get<double>(v)); 
                changed_.emit(v);
            });
        }

        bool hasValue() const override { return true; }
        
        value_opt_t value() const override { return slider_->value(); }

        void resetValue() override { setValue((min_ + max_) / 2); }

        void setValue(const value_t& value) override {
            if (!std::holds_alternative<double>(value)) {
                return;
            }

            double v = std::get<double>(value);
            slider_->setValue(v);
            line_edit_->setValue(slider_->value());
        }

        Wt::Signal<value_t>& changed() override { return changed_; }

    private:
        NumericSlider* slider_{nullptr};
        NumericLineEdit<double>* line_edit_{nullptr};

        Wt::Signal<value_t> changed_;

        double min_, max_;
    };



    class EnumSelect : public Wt::WContainerWidget, public IParamInput
    {
    public:
        EnumSelect(const std::vector<std::string>& options,
                std::optional<std::string> val = std::nullopt,
                std::string placeholder = "empty")
        {
            addStyleClass("enum-select");
            setAttributeValue("data-placeholder", placeholder);

            select_ = addNew<Wt::WComboBox>();
            select_->setInline(true);
            select_->setNoSelectionEnabled(true); // allow “no value” state

            int index;
            if (val)
            {
                auto it = std::find(options.begin(), options.end(), *val);
                index = (it != options.end()) ? static_cast<int>(std::distance(options.begin(), it)) : -1;
            }
            else
            {
                index = -1;
            }
           

            setOptions(options);
            
            select_->setCurrentIndex(index);

            select_->changed().connect(this, [this] {
                updateClasses();
                changed_.emit(select_->currentIndex());
            });
            
            updateClasses();
        }

        bool hasValue() const override { return select_->currentIndex() >= 0; }

        value_opt_t value() const override { return hasValue() ? value_opt_t(select_->currentIndex()) : std::nullopt; }

        void resetValue() override { select_->setCurrentIndex(-1); updateClasses(); }

        void setValue(const value_t& value) override {
            if (!std::holds_alternative<int>(value)) {
                return;
            }
            int v = std::get<int>(value);

            if (v < 0 || v >= select_->count())
            {
                return;
            }

            select_->setCurrentIndex(v);
            updateClasses();
        }
        Wt::Signal<value_t>& changed() override { return changed_; }

        void setOptions(const std::vector<std::string>& opts) {
            select_->clear();
            for (auto& s : opts) select_->addItem(s);
            select_->setEnabled(!opts.empty());
            select_->setCurrentIndex(-1);             // start as “no value”
            updateClasses();
        }

    private:
        Wt::WComboBox* select_{nullptr};
        Wt::Signal<value_t> changed_;

        void updateClasses() {
            toggle("has-value", hasValue());
            toggle("disabled", !select_->isEnabled());
            toggle("invalid", !hasValue());
        }
        void toggle(const char* cls, bool on) {
            if (on) addStyleClass(cls); else removeStyleClass(cls);
        }
    };


    class CustomValue : public Wt::WContainerWidget, public IParamInput
    {
    public:
        CustomValue(std::string empty_text, std::string value_name)
        : empty_text_(empty_text), value_name_(value_name)
        {
            has_value_ = false;
            addStyleClass("input-custom");
            addStyleClass("input-custom-empty");

            clickable_area_ = addNew<Wt::WContainerWidget>();
            clickable_area_->addStyleClass("input-custom-clickable");

            value_text_ = addNew<Wt::WText>(empty_text_);
            value_text_->addStyleClass("input-custom-value-empty");

            remove_text_ = addNew<Wt::WText>("✕");
            remove_text_->addStyleClass("input-custom-remove");
            remove_text_->hide();

            clickable_area_->clicked().connect([this]{
                std::cout << "CustomValue clicked, has_value_=" << has_value_ << std::endl;
                if (!has_value_) value_requested_.emit(true);
            });

            remove_text_->clicked().connect([this]{
                std::cout << "CustomValue remove clicked, has_value_=" << has_value_ << std::endl;
                if (has_value_) value_requested_.emit(false);
            });
        }

        bool hasValue() const override { return true; }

        value_opt_t value() const override { return has_value_; }

        void resetValue() override { setValue(false); }

        void setValue(const value_t& value) override {
            if (!std::holds_alternative<bool>(value)) {
                return;
            }

            bool v = std::get<bool>(value);
            
            if (v)
            {
                has_value_ = true;
                value_text_->setText(value_name_);
                remove_text_->show();

                clickable_area_->removeStyleClass("input-custom-clickable");
                clickable_area_->addStyleClass("input-custom-click-disabled");
                
                addStyleClass("input-custom-set");
                removeStyleClass("input-custom-empty");

                value_text_->addStyleClass("input-custom-value-set");
                value_text_->removeStyleClass("input-custom-value-empty");
            }
            else
            {
                has_value_ = false;
                value_text_->setText(empty_text_);
                remove_text_->hide();

                clickable_area_->removeStyleClass("input-custom-click-disabled");
                clickable_area_->addStyleClass("input-custom-clickable");

                removeStyleClass("input-custom-set");
                addStyleClass("input-custom-empty");
                
                value_text_->removeStyleClass("input-custom-value-set");
                value_text_->addStyleClass("input-custom-value-empty");
            }
        }
        Wt::Signal<value_t>& changed() override { return value_requested_; }

    private:
        std::string empty_text_;
        std::string value_name_;

        Wt::Signal<value_t> value_requested_;  // true = request value, false = request removal
        bool has_value_;

        Wt::WText* value_text_{nullptr};
        Wt::WText* remove_text_{nullptr};
        Wt::WContainerWidget* clickable_area_{nullptr};
    };
}