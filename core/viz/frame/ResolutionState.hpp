#pragma once
#include <array>

#include "viz/frame/Quality.hpp"

// The render resolution the app holds (core/api/geode_perf_api.cpp, geode_perf_*_set): the shell sends what Android
// says (the display's panel and current mode, the stage view's size) and the user's four Performance settings as void
// events, and each one derives the Descriptor once. Everything the screen shows or sizes from it reads the Descriptor
// as published: the window buffer, the steps the tab offers and the one selected, the pixels a pick draws on this
// screen, the preset the values are. Host-tested in core/tests/quality_test.cpp.
namespace geode::viz::frame {

// The ints geode_perf_resolution_read writes, in this order.
enum DescriptorField : int {
    kFieldResolution = 0,  // the stored resolution
    kFieldPreset,          // the QualityMode these values are, or -1 for Custom
    kFieldSelected,        // the index into the steps of the one on show, or -1
    kFieldBufferWidth,     // the window buffer for the view (0: no view yet)
    kFieldBufferHeight,
    kFieldDrawnWidth,      // what the resolution draws full screen on the current mode (0: mode unknown)
    kFieldDrawnHeight,
    kFieldModeWidth,       // the current mode
    kFieldModeHeight,
    kFieldStepCount,
    kFieldSteps,  // kMaxResolutionChoices steps, ascending, Native last
    kDescriptorInts = kFieldSteps + kMaxResolutionChoices,
};

using Descriptor = std::array<int, kDescriptorInts>;

struct Panel {
    int width = 0;
    int height = 0;
    int modeWidth = 0;
    int modeHeight = 0;
    bool operator==(const Panel& o) const {
        return width == o.width && height == o.height && modeWidth == o.modeWidth && modeHeight == o.modeHeight;
    }
};

struct View {
    int width = 0;
    int height = 0;
    bool operator==(const View& o) const { return width == o.width && height == o.height; }
};

struct Settings {
    int resolution = kNativeResolution;
    int fpsCap = 0;
    float marchScale = 1.0f;
    bool postFx = true;
    bool operator==(const Settings& o) const {
        return resolution == o.resolution && fpsCap == o.fpsCap && marchScale == o.marchScale && postFx == o.postFx;
    }
};

class ResolutionState {
public:
    ResolutionState() { derive(); }

    void setPanel(const Panel& panel) {
        if (panel == panel_) return;
        panel_ = panel;
        derive();
    }

    void setView(const View& view) {
        if (view == view_) return;
        view_ = view;
        derive();
    }

    void setSettings(const Settings& settings) {
        if (settings == settings_) return;
        settings_ = settings;
        derive();
    }

    const Panel& panel() const { return panel_; }
    const Descriptor& descriptor() const { return descriptor_; }

private:
    void derive() {
        Descriptor d{};
        const int panelShort = shortSide(panel_.width, panel_.height);
        d[kFieldResolution] = settings_.resolution;
        d[kFieldPreset] = modeOf(settings_.resolution, settings_.fpsCap, settings_.marchScale, settings_.postFx, panelShort);
        int steps[kMaxResolutionChoices];
        const int count = resolutionChoices(panel_.width, panel_.height, steps, kMaxResolutionChoices);
        d[kFieldStepCount] = count;
        int selected = -1;
        const int drawn = drawnShortSide(settings_.resolution, panelShort);
        for (int i = 0; i < count; ++i) {
            d[kFieldSteps + i] = steps[i];
            if (selected < 0 && drawnShortSide(steps[i], panelShort) == drawn) selected = i;
        }
        d[kFieldSelected] = selected;
        const BufferSize buffer =
            bufferSize(settings_.resolution, panel_.modeWidth, panel_.modeHeight, view_.width, view_.height);
        d[kFieldBufferWidth] = buffer.width;
        d[kFieldBufferHeight] = buffer.height;
        const BufferSize full =
            bufferSize(settings_.resolution, panel_.modeWidth, panel_.modeHeight, panel_.modeWidth, panel_.modeHeight);
        d[kFieldDrawnWidth] = full.width;
        d[kFieldDrawnHeight] = full.height;
        d[kFieldModeWidth] = panel_.modeWidth;
        d[kFieldModeHeight] = panel_.modeHeight;
        descriptor_ = d;
    }

    Panel panel_;
    View view_;
    Settings settings_;
    Descriptor descriptor_{};
};

}  // namespace geode::viz::frame
