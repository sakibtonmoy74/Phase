#include "FixtureTemplates.h"

namespace pl
{

int suggestedDefault (const juce::String& attribute)
{
    const auto a = baseAttribute (attribute);
    if (a == "Pan" || a == "Tilt" || a == "Zoom" || a == "Focus") return 128;
    if (a == "Shutter") return 255;
    return 0;
}

namespace cellLayouts
{
    std::vector<std::pair<float, float>> grid (int cx, int cy)
    {
        std::vector<std::pair<float, float>> p;
        for (int y = 0; y < cy; ++y) for (int x = 0; x < cx; ++x) p.push_back ({ (float) x, (float) y });
        return p;
    }

    std::vector<std::pair<float, float>> snake (int cx, int cy)
    {
        std::vector<std::pair<float, float>> p;
        for (int y = 0; y < cy; ++y)
            for (int i = 0; i < cx; ++i) p.push_back ({ (float) (y % 2 == 0 ? i : cx - 1 - i), (float) y });
        return p;
    }

    std::vector<std::pair<float, float>> columns (int cx, int cy)
    {
        std::vector<std::pair<float, float>> p;
        for (int x = 0; x < cx; ++x) for (int y = 0; y < cy; ++y) p.push_back ({ (float) x, (float) y });
        return p;
    }

    std::vector<std::pair<float, float>> ring (int count, bool clockwise, int rings)
    {
        std::vector<std::pair<float, float>> p;
        rings = juce::jlimit (1, juce::jmax (1, count), rings);
        // split the pixels over the rings in proportion to their size (outer ring first)
        int left = count;
        float totalWeight = 0.0f;
        for (int r = 0; r < rings; ++r) totalWeight += (float) (rings - r);
        for (int r = 0; r < rings; ++r)
        {
            const int n = r == rings - 1 ? left : juce::jmax (1, juce::roundToInt ((float) count * (float) (rings - r) / totalWeight));
            left -= n;
            const float radius = juce::jmax (0.5f, (float) count / juce::MathConstants<float>::twoPi) * (float) (rings - r) / (float) rings;
            for (int i = 0; i < n; ++i)
            {
                float a = juce::MathConstants<float>::twoPi * (float) i / (float) n;
                if (! clockwise) a = -a;
                p.push_back ({ radius * std::sin (a), -radius * std::cos (a) });
            }
        }
        return p;
    }

    std::vector<std::pair<float, float>> rings (const std::vector<int>& perRing, bool clockwise)
    {
        std::vector<std::pair<float, float>> p;
        const int n = (int) perRing.size();
        if (n == 0) return p;
        // ring spacing: at least one pixel apart, and wide enough that the outer ring's pixels don't touch
        const float outer = juce::jmax ((float) n - (perRing.back() == 1 ? 1.0f : 0.0f),
                                        (float) perRing.front() / juce::MathConstants<float>::twoPi);
        for (int r = 0; r < n; ++r)
        {
            const int count = juce::jmax (1, perRing[(size_t) r]);
            const float steps = (float) n - (perRing.back() == 1 ? 1.0f : 0.0f);
            const float radius = count == 1 && r == n - 1 ? 0.0f
                               : outer * (steps - (float) r) / juce::jmax (1.0f, steps);
            for (int i = 0; i < count; ++i)
            {
                float a = juce::MathConstants<float>::twoPi * (float) i / (float) count;
                if (! clockwise) a = -a;
                p.push_back ({ radius * std::sin (a), -radius * std::cos (a) });
            }
        }
        return p;
    }
}

namespace templates
{
    const std::vector<Info>& all()
    {
        static const std::vector<Info> list {
            { "dimmer",     "Dimmer",                 "1 channel" },
            { "dimmer16",   "Dimmer 16-bit",          "Coarse + fine" },
            { "rgb",        "LED Par RGB",            "Dimmer, R, G, B, Strobe" },
            { "rgbw",       "LED Par RGBW",           "Dimmer, R, G, B, W, Strobe" },
            { "rgbwauv",    "LED Par RGBWA+UV",       "Dimmer, R, G, B, W, Amber, UV, Strobe" },
            { "spot",       "Moving Head Spot",       "16-bit Pan/Tilt, colour wheel, 2 gobos, prism, focus, zoom, iris, frost" },
            { "profile",    "Moving Head Profile CMY","16-bit Pan/Tilt, CMY, CTO, wheel, gobo, prism, focus, zoom, iris" },
            { "wash",       "Moving Head Wash RGBW",  "16-bit Pan/Tilt, 16-bit dimmer, RGBW, CTO, zoom" },
            { "beam",       "Moving Head Beam",       "16-bit Pan/Tilt, colour wheel, gobo, prism, focus, frost" },
            { "bar",        "LED Bar (pixels)",       "Dimmer + RGB per pixel", true },
            { "barrgbw",    "LED Bar RGBW (pixels)",  "Dimmer + RGBW per pixel", true },
            { "panel",      "Pixel Panel / Matrix",   "RGB per pixel, cells across x down", true, true },
            { "ring",       "LED Ring (pixels)",      "RGB pixels in a circle", true },
            { "strobe",     "Strobe",                 "Dimmer, strobe rate" },
            { "blinder",    "Blinder (cells)",        "One dimmer per cell", true },
            { "fog",        "Fog / Haze machine",     "Output, fan" },
        };
        return list;
    }

    FixtureType make (const juce::String& id, int cellsX, int cellsY)
    {
        struct D { const char* a; bool fine = false; };
        FixtureType t;
        t.id = "user." + juce::Uuid().toDashedString();
        t.manufacturer = "Generic";
        cellsX = juce::jlimit (1, 256, cellsX);
        cellsY = juce::jlimit (1, 256, cellsY);

        auto add = [&t] (std::initializer_list<D> defs, int cell = 0)
        {
            for (auto& d : defs)
                t.attributes.push_back ({ d.a, 0, d.fine ? 1 : 0, suggestedDefault (d.a), cell });
        };
        auto movingBase = [&] { add ({ { "Pan", true }, { "Tilt", true }, { "PanTiltSpeed" } }); };

        juce::String name;
        for (auto& i : all()) if (i.id == id) name = i.name;
        t.name = name.isNotEmpty() ? name : "New fixture";

        if (id == "dimmer")        add ({ { "Dimmer" } });
        else if (id == "dimmer16") add ({ { "Dimmer", true } });
        else if (id == "rgb")      add ({ { "Dimmer" }, { "ColorR" }, { "ColorG" }, { "ColorB" }, { "Strobe" } });
        else if (id == "rgbw")     add ({ { "Dimmer" }, { "ColorR" }, { "ColorG" }, { "ColorB" }, { "ColorW" }, { "Strobe" } });
        else if (id == "rgbwauv")  add ({ { "Dimmer" }, { "ColorR" }, { "ColorG" }, { "ColorB" }, { "ColorW" }, { "ColorA" }, { "ColorUV" }, { "Strobe" } });
        else if (id == "spot")
        {
            movingBase();
            add ({ { "Dimmer", true }, { "Shutter" }, { "ColorWheel" }, { "Gobo1" }, { "Gobo1Rot" }, { "Gobo2" },
                   { "Prism" }, { "PrismRot" }, { "Focus" }, { "Zoom" }, { "Iris" }, { "Frost" }, { "Control" } });
        }
        else if (id == "profile")
        {
            movingBase();
            add ({ { "Dimmer", true }, { "Shutter" }, { "ColorC" }, { "ColorM" }, { "ColorY" }, { "CTO" }, { "ColorWheel" },
                   { "Gobo1" }, { "Gobo1Rot" }, { "Prism" }, { "PrismRot" }, { "Focus" }, { "Zoom" }, { "Iris" }, { "Frost" }, { "Control" } });
        }
        else if (id == "wash")
        {
            movingBase();
            add ({ { "Dimmer", true }, { "Shutter" }, { "ColorR" }, { "ColorG" }, { "ColorB" }, { "ColorW" }, { "CTO" }, { "Zoom" }, { "Control" } });
        }
        else if (id == "beam")
        {
            movingBase();
            add ({ { "Dimmer" }, { "Shutter" }, { "ColorWheel" }, { "Gobo1" }, { "Prism" }, { "PrismRot" }, { "Focus" }, { "Frost" }, { "Control" } });
        }
        else if (id == "bar" || id == "barrgbw" || id == "panel" || id == "blinder")
        {
            t.cellsX = cellsX;
            t.cellsY = id == "panel" ? cellsY : 1;
            if (id != "panel") add ({ { "Dimmer" } });
            if (id == "blinder")      add ({ { "Dimmer" } }, 1);
            else if (id == "barrgbw") add ({ { "ColorR" }, { "ColorG" }, { "ColorB" }, { "ColorW" } }, 1);
            else                      add ({ { "ColorR" }, { "ColorG" }, { "ColorB" } }, 1);
            t.repeatCellOne();
            t.name << " " << (t.cellsY > 1 ? juce::String (t.cellsX) + "x" + juce::String (t.cellsY) : juce::String (t.cellsX) + "px");
        }
        else if (id == "ring")
        {
            t.cellsX = cellsX;
            add ({ { "ColorR" }, { "ColorG" }, { "ColorB" } }, 1);
            t.repeatCellOne();
            t.setCellLayout (cellLayouts::ring (cellsX));
            t.name << " " << juce::String (cellsX) << "px";
        }
        else if (id == "strobe")   add ({ { "Dimmer" }, { "Strobe" } });
        else if (id == "fog")      add ({ { "Control" }, { "Raw" } });
        else                       add ({ { "Dimmer" } });

        t.autoNumber();
        return t;
    }
}

} // namespace pl
