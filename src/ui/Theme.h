#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

namespace namrig::theme
{

// One coherent token system. Rules:
//  * accent appears ONLY on interactive value (arcs, thumbs, selection)
//  * green ONLY in the meter target zone; red ONLY for clip/error
//  * hierarchy is carried by the surface steps + two text levels
namespace colours
{
// Surface steps spread wide enough to read as levels at a glance; the
// accent is strong but no longer the only high-contrast element.
inline const juce::Colour background{0xff121116}; // window ground
inline const juce::Colour panel{0xff26242b};      // section surface
inline const juce::Colour raised{0xff34313a};     // buttons, combos
inline const juce::Colour inset{0xff0d0c10};      // meter wells, text entry
inline const juce::Colour outline{0xff474250};    // hairlines
inline const juce::Colour track{0xff4a4552};      // knob/slider tracks

inline const juce::Colour textPrimary{0xffedebf2};   // values, names
inline const juce::Colour textSecondary{0xff9c98a6}; // captions, headers

inline const juce::Colour accent{0xff6d9de2};      // interactive value only
inline const juce::Colour accentDim{0xff41597f};   // selection fills

inline const juce::Colour ok{0xff4e9b70};    // meter target zone only
inline const juce::Colour error{0xffd9776c}; // clip, load failures
} // namespace colours

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        using namespace colours;

        setDefaultSansSerifTypeface(regular());

        setColour(juce::ResizableWindow::backgroundColourId, background);
        setColour(juce::TooltipWindow::backgroundColourId, raised);
        setColour(juce::TooltipWindow::textColourId, textPrimary);
        setColour(juce::TooltipWindow::outlineColourId, outline);

        setColour(juce::Label::textColourId, textPrimary);

        setColour(juce::Slider::rotarySliderFillColourId, accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, track);
        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::trackColourId, accent);
        setColour(juce::Slider::backgroundColourId, track);
        setColour(juce::Slider::textBoxTextColourId, textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, accentDim);

        setColour(juce::TextButton::buttonColourId, raised);
        setColour(juce::TextButton::buttonOnColourId, accentDim);
        setColour(juce::TextButton::textColourOffId, textPrimary);
        setColour(juce::TextButton::textColourOnId, textPrimary);
        setColour(juce::ComboBox::backgroundColourId, raised);
        setColour(juce::ComboBox::outlineColourId, outline);
        setColour(juce::ComboBox::textColourId, textPrimary);
        setColour(juce::ComboBox::arrowColourId, textSecondary);
        setColour(juce::ComboBox::buttonColourId, raised);

        setColour(juce::PopupMenu::backgroundColourId, raised);
        setColour(juce::PopupMenu::textColourId, textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accentDim);
        setColour(juce::PopupMenu::highlightedTextColourId, textPrimary);

        setColour(juce::TextEditor::backgroundColourId, inset);
        setColour(juce::TextEditor::textColourId, textPrimary);
        setColour(juce::TextEditor::outlineColourId, outline);
        setColour(juce::TextEditor::focusedOutlineColourId, accent);
        setColour(juce::TextEditor::highlightColourId, accentDim);
        setColour(juce::CaretComponent::caretColourId, accent);

        setColour(juce::ToggleButton::textColourId, textPrimary);
        setColour(juce::ToggleButton::tickColourId, accent);
        setColour(juce::ToggleButton::tickDisabledColourId, track);

        setColour(juce::ScrollBar::thumbColourId, track);
    }

    // Flat knob: thin track arc, accent value arc, dot thumb. No bevels.
    // Embedded Barlow: consistent rendering on every platform, no system
    // font dependence. Bold requests map to SemiBold; Medium is available
    // for emphasis in between.
    static juce::Typeface::Ptr regular()
    {
        static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
            BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize);
        return t;
    }
    static juce::Typeface::Ptr medium()
    {
        static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
            BinaryData::BarlowMedium_ttf, BinaryData::BarlowMedium_ttfSize);
        return t;
    }
    static juce::Typeface::Ptr semiBold()
    {
        static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
            BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize);
        return t;
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override
    {
        return font.isBold() ? semiBold() : regular();
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        using namespace colours;
        const auto bounds =
            juce::Rectangle<int>{x, y, width, height}.toFloat().reduced(6.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f - 4.0f;
        const auto centre = bounds.getCentre();
        const float angle =
            rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float lineW = 5.0f; // constant weight: same pen on every knob
        const float arcRadius = radius - lineW / 2.0f;
        const bool enabled = slider.isEnabled();

        juce::Path back;
        back.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                           rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(enabled ? track : track.withAlpha(0.5f));
        g.strokePath(back, juce::PathStrokeType{lineW, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded});

        if (enabled)
        {
            // The value arc shows how much the control is DOING. Controls
            // whose effect grows downward (low-pass filters: open = off)
            // set the "reverseFill" property and fill from the top end.
            const bool reverse = slider.getProperties()["reverseFill"];
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                reverse ? angle : rotaryStartAngle,
                                reverse ? rotaryEndAngle : angle, true);
            g.setColour(accent);
            g.strokePath(value, juce::PathStrokeType{lineW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded});
        }

        const auto thumb = centre.getPointOnCircumference(arcRadius, angle);
        g.setColour(enabled ? accent : textSecondary);
        g.fillEllipse(juce::Rectangle<float>{lineW * 1.8f, lineW * 1.8f}.withCentre(thumb));

        // Value in the knob's centre: number and control are one object.
        g.setColour(enabled ? textPrimary : textSecondary);
        const float textSize = juce::jlimit(11.0f, 15.0f, radius * 0.42f);
        g.setFont(juce::FontOptions{textSize});
        g.drawText(slider.getTextFromValue(slider.getValue()),
                   juce::Rectangle<float>{arcRadius * 1.7f, textSize + 2.0f}.withCentre(centre),
                   juce::Justification::centred);
    }

    // Labels (including slider value boxes) never get outlines — the
    // stock path copies an outline colour at construction, before this
    // LookAndFeel attaches, so we neutralize it at draw time instead.
    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        if (!label.isBeingEdited())
        {
            const float alpha = label.isEnabled() ? 1.0f : 0.5f;
            g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
            g.setFont(getLabelFont(label));
            const auto area = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
            g.drawFittedText(label.getText(), area, label.getJustificationType(),
                             juce::jmax(1, area.getHeight() / 16),
                             label.getMinimumHorizontalScale());
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto colour = backgroundColour;
        if (isDown)
            colour = colours::accentDim;
        else if (isHighlighted)
            colour = colour.brighter(0.08f);
        g.setColour(colour);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(colours::outline);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }
};

} // namespace namrig::theme
