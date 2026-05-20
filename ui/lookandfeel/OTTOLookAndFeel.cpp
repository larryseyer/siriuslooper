/**
 * @file OTTOLookAndFeel.cpp
 * @brief Implementation of OTTO custom LookAndFeel
 */

#include "OTTOLookAndFeel.h"
#include "OTTOBinaryData.h"

namespace otto {

// Static member initialization
juce::Typeface::Ptr OTTOLookAndFeel::robotoTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::orbitronTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::robotoCondensedTypeface_ = nullptr;
// PCv6-01: Player Card v6 elegant typography statics. Loaded lazily by
// loadCustomFonts() and reset to nullptr on the last instance destructor (same
// lifetime model as Roboto / Orbitron — see ~OTTOLookAndFeel below).
juce::Typeface::Ptr OTTOLookAndFeel::bricolageRegularTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::bricolageMediumTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::bricolageSemiBoldTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::bricolageBoldTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::bricolageExtraBoldTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::jetBrainsMonoRegularTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::jetBrainsMonoMediumTypeface_ = nullptr;
juce::Typeface::Ptr OTTOLookAndFeel::jetBrainsMonoSemiBoldTypeface_ = nullptr;
juce::Image OTTOLookAndFeel::faderCapImage_;
int OTTOLookAndFeel::instanceCount_ = 0;

// =============================================================================
// Constructor
// =============================================================================

OTTOLookAndFeel::OTTOLookAndFeel() {
    ++instanceCount_;
    setupColours();
    setupFonts();
    loadFaderCapImage();
}

OTTOLookAndFeel::~OTTOLookAndFeel() {
    --instanceCount_;
    if (instanceCount_ == 0) {
        // Clear static resources to avoid leak detection on shutdown
        robotoTypeface_ = nullptr;
        orbitronTypeface_ = nullptr;
        robotoCondensedTypeface_ = nullptr;
        // PCv6-01: release Player Card v6 typefaces alongside Roboto / Orbitron.
        bricolageRegularTypeface_ = nullptr;
        bricolageMediumTypeface_ = nullptr;
        bricolageSemiBoldTypeface_ = nullptr;
        bricolageBoldTypeface_ = nullptr;
        bricolageExtraBoldTypeface_ = nullptr;
        jetBrainsMonoRegularTypeface_ = nullptr;
        jetBrainsMonoMediumTypeface_ = nullptr;
        jetBrainsMonoSemiBoldTypeface_ = nullptr;
        faderCapImage_ = juce::Image();
    }
}

// =============================================================================
// Color Setup
// =============================================================================

void OTTOLookAndFeel::setupColours() {
    // Window colors
    setColour(juce::ResizableWindow::backgroundColourId,
              Colours::backgroundPrimary);

    // Label colors
    setColour(juce::Label::textColourId, Colours::textPrimary);
    setColour(juce::Label::backgroundColourId,
              juce::Colour(0x00000000)); // Transparent

    // TextButton colors
    setColour(juce::TextButton::buttonColourId, Colours::backgroundSecondary);
    setColour(juce::TextButton::buttonOnColourId, Colours::buttonFace);
    setColour(juce::TextButton::textColourOffId, Colours::buttonTextNormal);
    setColour(juce::TextButton::textColourOnId, Colours::buttonTextActive);

    // Slider colors
    setColour(juce::Slider::backgroundColourId, Colours::sliderBackground);
    setColour(juce::Slider::trackColourId, Colours::sliderTrack);
    setColour(juce::Slider::thumbColourId, Colours::sliderThumb);
    setColour(juce::Slider::rotarySliderFillColourId, Colours::sliderTrack);
    setColour(juce::Slider::rotarySliderOutlineColourId,
              Colours::backgroundDarker);

    // ComboBox colors
    setColour(juce::ComboBox::backgroundColourId, Colours::backgroundSecondary);
    setColour(juce::ComboBox::textColourId, Colours::textPrimary);
    setColour(juce::ComboBox::outlineColourId, Colours::border);
    setColour(juce::ComboBox::arrowColourId, Colours::textSecondary);

    // PopupMenu colors — read from centralized MenuColors so the JUCE
    // LookAndFeel surface and the touch popup share one palette. Spec per
    // continue.md "Global Menu Design Tokens / Color Palette".
    {
        const auto menuColours = getMenuColors();
        setColour(juce::PopupMenu::backgroundColourId, menuColours.background);
        setColour(juce::PopupMenu::textColourId, menuColours.foreground);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, menuColours.accent);
        setColour(juce::PopupMenu::highlightedTextColourId, menuColours.highlightText);
        setColour(juce::PopupMenu::headerTextColourId, menuColours.sectionHeader);
    }

    // ScrollBar colors
    setColour(juce::ScrollBar::thumbColourId, Colours::sliderThumb);
    setColour(juce::ScrollBar::trackColourId, Colours::backgroundDarker);

    // TextEditor colors
    setColour(juce::TextEditor::backgroundColourId, Colours::backgroundDarker);
    setColour(juce::TextEditor::textColourId, Colours::textPrimary);
    setColour(juce::TextEditor::highlightColourId, Colours::selection);
    setColour(juce::TextEditor::outlineColourId, Colours::border);
    setColour(juce::TextEditor::focusedOutlineColourId, Colours::focusOutline);

    // ListBox colors
    setColour(juce::ListBox::backgroundColourId, Colours::backgroundSecondary);
    setColour(juce::ListBox::textColourId, Colours::textPrimary);
}

// =============================================================================
// Font Setup
// =============================================================================

void OTTOLookAndFeel::loadRobotoTypeface() {
    if (robotoTypeface_ == nullptr) {
        robotoTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::RobotoVariableFont_wdthwght_ttf,
            OTTOBinaryData::RobotoVariableFont_wdthwght_ttfSize);
    }
}

juce::Typeface::Ptr OTTOLookAndFeel::getRobotoTypeface() {
    loadRobotoTypeface();
    return robotoTypeface_;
}

void OTTOLookAndFeel::loadOrbitronTypeface() {
    if (orbitronTypeface_ == nullptr) {
        orbitronTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::OrbitronVariableFont_wght_ttf,
            OTTOBinaryData::OrbitronVariableFont_wght_ttfSize);
    }
}

void OTTOLookAndFeel::loadRobotoCondensedTypeface() {
    if (robotoCondensedTypeface_ == nullptr) {
        robotoCondensedTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::RobotoCondensedVariableFont_wght_ttf,
            OTTOBinaryData::RobotoCondensedVariableFont_wght_ttfSize);
    }
}

juce::Typeface::Ptr OTTOLookAndFeel::getRobotoCondensedTypeface() {
    loadRobotoCondensedTypeface();
    return robotoCondensedTypeface_;
}

// =============================================================================
// Menu Design Tokens — single source of truth for both juce::PopupMenu LnF
// overrides AND TouchScrollableMenuPopup. See OTTOLookAndFeel.h "Menu Design
// Tokens" comment block and continue.md spec section "Global Menu Design
// Tokens".
// =============================================================================

juce::Font OTTOLookAndFeel::getMenuItemFont() {
    return juce::Font(juce::FontOptions(getRobotoCondensedTypeface())
                          .withHeight(Sizing::kMenuItemFontSize));
}

juce::Font OTTOLookAndFeel::getMenuHeaderFont() {
    return juce::Font(juce::FontOptions(getRobotoCondensedTypeface())
                          .withHeight(Sizing::kMenuHeaderFontSize)
                          .withStyle("Bold"));
}

juce::Font OTTOLookAndFeel::getMenuShortcutFont() {
    return juce::Font(juce::FontOptions(getRobotoCondensedTypeface())
                          .withHeight(Sizing::kMenuShortcutFontSize));
}

OTTOLookAndFeel::MenuColors OTTOLookAndFeel::getMenuColors() {
    return MenuColors{
        juce::Colour(0xFF1A1A1A), // background — opaque near-black
        juce::Colour(0xFFEEEEEE), // foreground — warm off-white
        juce::Colour(0xFFFFFFFF), // highlightText — pure white over accent
        juce::Colour(0x66EEEEEE), // disabledText — 40% alpha foreground
        juce::Colour(0x33FFFFFF), // separator — 20% alpha white
        juce::Colour(0x99FFFFFF), // sectionHeader — 60% alpha white
        juce::Colour(0xB3EEEEEE), // submenuArrow — 70% alpha foreground
        juce::Colour(0x80EEEEEE), // shortcutText — 50% alpha foreground
        Colours::playActive,      // accent — OTTO green (tick, focus, gradient anchor)
    };
}

// =============================================================================
// PCv6-01: Player Card v6 Elegant Typography
// =============================================================================
//
// Bricolage Grotesque (sans, weights 400/500/600/700/800) + JetBrains Mono
// (mono, weights 400/500/600) ship as 8 static-weight ttf binaries inside
// OTTOPluginAssets / OTTO_iOS_Assets. The 8 typefaces are loaded once via
// `loadCustomFonts()` (idempotent) and stashed in static juce::Typeface::Ptr
// members so all OTTOLookAndFeel instances share them. This mirrors the
// Roboto / Orbitron lifetime model already in use and keeps font I/O off the
// audio thread (loaders fire from the constructor / `setupFonts()` only).
//
// Weight clamping happens at the accessor boundary: out-of-range CSS weights
// snap to the nearest available static weight rather than failing or
// returning nullptr. If a binary-data load fails (corrupted / missing asset),
// `getBricolageTypeface` falls through to Roboto and `getJetBrainsMonoTypeface`
// falls through to nullptr (JUCE then resolves system monospace).

void OTTOLookAndFeel::loadCustomFonts() {
    // Bricolage Grotesque — 5 weights
    if (bricolageRegularTypeface_ == nullptr) {
        bricolageRegularTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::BricolageGrotesqueRegular_ttf,
            OTTOBinaryData::BricolageGrotesqueRegular_ttfSize);
    }
    if (bricolageMediumTypeface_ == nullptr) {
        bricolageMediumTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::BricolageGrotesqueMedium_ttf,
            OTTOBinaryData::BricolageGrotesqueMedium_ttfSize);
    }
    if (bricolageSemiBoldTypeface_ == nullptr) {
        bricolageSemiBoldTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::BricolageGrotesqueSemiBold_ttf,
            OTTOBinaryData::BricolageGrotesqueSemiBold_ttfSize);
    }
    if (bricolageBoldTypeface_ == nullptr) {
        bricolageBoldTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::BricolageGrotesqueBold_ttf,
            OTTOBinaryData::BricolageGrotesqueBold_ttfSize);
    }
    if (bricolageExtraBoldTypeface_ == nullptr) {
        bricolageExtraBoldTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::BricolageGrotesqueExtraBold_ttf,
            OTTOBinaryData::BricolageGrotesqueExtraBold_ttfSize);
    }

    // JetBrains Mono — 3 weights
    if (jetBrainsMonoRegularTypeface_ == nullptr) {
        jetBrainsMonoRegularTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::JetBrainsMonoRegular_ttf,
            OTTOBinaryData::JetBrainsMonoRegular_ttfSize);
    }
    if (jetBrainsMonoMediumTypeface_ == nullptr) {
        jetBrainsMonoMediumTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::JetBrainsMonoMedium_ttf,
            OTTOBinaryData::JetBrainsMonoMedium_ttfSize);
    }
    if (jetBrainsMonoSemiBoldTypeface_ == nullptr) {
        jetBrainsMonoSemiBoldTypeface_ = juce::Typeface::createSystemTypefaceFor(
            OTTOBinaryData::JetBrainsMonoSemiBold_ttf,
            OTTOBinaryData::JetBrainsMonoSemiBold_ttfSize);
    }
}

juce::Typeface::Ptr OTTOLookAndFeel::getBricolageTypeface(int weight) {
    loadCustomFonts();

    // Clamp to nearest available weight (400/500/600/700/800).
    // Halfway midpoints round up toward the heavier weight per the elegant
    // mockup's preference for slightly bolder type at the same nominal weight.
    juce::Typeface::Ptr resolved;
    if (weight <= 450)        resolved = bricolageRegularTypeface_;     // 400
    else if (weight <= 550)   resolved = bricolageMediumTypeface_;      // 500
    else if (weight <= 650)   resolved = bricolageSemiBoldTypeface_;    // 600
    else if (weight <= 750)   resolved = bricolageBoldTypeface_;        // 700
    else                       resolved = bricolageExtraBoldTypeface_;   // 800

    if (resolved != nullptr)
        return resolved;

    // Bricolage failed to load — fall back to Roboto so the elegant card stays
    // legible rather than rendering a missing-typeface box.
    loadRobotoTypeface();
    return robotoTypeface_;
}

juce::Font OTTOLookAndFeel::getBricolage(int weight, float heightInPoints) {
    auto tf = getBricolageTypeface(weight);
    if (tf != nullptr)
        return juce::Font(juce::FontOptions(tf).withHeight(heightInPoints));
    // No typeface at all — let JUCE pick the system default at the requested
    // height. Better than a default-constructed Font with no size.
    return juce::Font(juce::FontOptions(heightInPoints));
}

juce::Typeface::Ptr OTTOLookAndFeel::getJetBrainsMonoTypeface(int weight) {
    loadCustomFonts();

    if (weight <= 450)        return jetBrainsMonoRegularTypeface_;    // 400
    else if (weight <= 550)   return jetBrainsMonoMediumTypeface_;     // 500
    else                       return jetBrainsMonoSemiBoldTypeface_;   // 600 / >600
}

juce::Font OTTOLookAndFeel::getJetBrainsMono(int weight, float heightInPoints) {
    auto tf = getJetBrainsMonoTypeface(weight);
    if (tf != nullptr)
        return juce::Font(juce::FontOptions(tf).withHeight(heightInPoints));
    // Mono load failed — let JUCE resolve a system monospace typeface via the
    // default-monospace FontOptions form. The elegant card's BPM digits stay
    // tabular even on the fallback.
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                         heightInPoints,
                                         juce::Font::plain));
}

void OTTOLookAndFeel::loadFaderCapImage() {
    if (faderCapImage_.isNull()) {
        faderCapImage_ = juce::ImageFileFormat::loadFrom(
            OTTOBinaryData::Fader_Knob_png,
            OTTOBinaryData::Fader_Knob_pngSize);
    }
}

juce::Typeface::Ptr OTTOLookAndFeel::getOrbitronTypeface() {
    loadOrbitronTypeface();
    return orbitronTypeface_;
}

juce::Typeface::Ptr OTTOLookAndFeel::getTypefaceForFont(const juce::Font& font) {
    // Ensure Roboto is loaded
    loadRobotoTypeface();

    // If Roboto loaded successfully, use it for all fonts
    if (robotoTypeface_ != nullptr) {
        return robotoTypeface_;
    }

    // Fallback to default behavior if Roboto failed to load
    return LookAndFeel_V4::getTypefaceForFont(font);
}

void OTTOLookAndFeel::setupFonts() {
    // Load Roboto typeface for cross-platform consistency
    loadRobotoTypeface();

    // PCv6-01: load Player Card v6 typefaces (Bricolage Grotesque + JetBrains
    // Mono) at LookAndFeel construction time so first-paint of the elegant
    // card hits cached typefaces rather than a synchronous binary-data load.
    // Idempotent — repeated calls during multiple LookAndFeel instantiation
    // (editor open/close cycles) are no-ops.
    loadCustomFonts();

    if (robotoTypeface_ != nullptr) {
        // Set Roboto as the default typeface for ALL JUCE components
        // This affects labels, buttons, combo boxes, etc. that don't have explicit fonts
        setDefaultSansSerifTypeface(robotoTypeface_);

        // Create fonts using Roboto typeface with FontOptions
        // Typography hierarchy: Caption < Body < Heading < Title < Large
        captionFont_ = juce::Font(juce::FontOptions(robotoTypeface_)
            .withHeight(Sizing::kFontSizeCaption));
        defaultFont_ = juce::Font(juce::FontOptions(robotoTypeface_)
            .withHeight(Sizing::kFontSizeBody));
        headingFont_ = juce::Font(juce::FontOptions(robotoTypeface_)
            .withHeight(Sizing::kFontSizeHeading)
            );
        titleFont_ = juce::Font(juce::FontOptions(robotoTypeface_)
            .withHeight(Sizing::kFontSizeTitle)
            );
        largeFont_ = juce::Font(juce::FontOptions(robotoTypeface_)
            .withHeight(Sizing::kFontSizeLarge)
            );
    } else {
        // Fallback to system fonts if Roboto fails to load
        captionFont_ = juce::Font(juce::FontOptions(Sizing::kFontSizeCaption));
        defaultFont_ = juce::Font(juce::FontOptions(Sizing::kFontSizeBody));
        headingFont_ = juce::Font(
            juce::FontOptions(Sizing::kFontSizeHeading));
        titleFont_ = juce::Font(
            juce::FontOptions(Sizing::kFontSizeTitle));
        largeFont_ = juce::Font(
            juce::FontOptions(Sizing::kFontSizeLarge));
    }
}

// =============================================================================
// Typography Accessors
// =============================================================================

juce::Font OTTOLookAndFeel::getDefaultFont() const {
    return defaultFont_;
}

juce::Font OTTOLookAndFeel::getHeadingFont() const {
    return headingFont_;
}

juce::Font OTTOLookAndFeel::getCaptionFont() const {
    return captionFont_;
}

juce::Font OTTOLookAndFeel::getTitleFont() const {
    return titleFont_;
}

juce::Font OTTOLookAndFeel::getLargeFont() const {
    return largeFont_;
}

// =============================================================================
// Scaled Typography
// =============================================================================

juce::Font OTTOLookAndFeel::getScaledFont(float height,
                                           const juce::String& style) const {
    // Calculate target size as 70% of available height
    const float targetSize = height * 0.7f;

    // Determine base font and max size for the requested style
    float maxSize = Sizing::kFontSizeBody;
    bool isBold = false;

    if (style == "large") {
        maxSize = Sizing::kFontSizeLarge;
        isBold = true;
    } else if (style == "title") {
        maxSize = Sizing::kFontSizeTitle;
        isBold = true;
    } else if (style == "heading") {
        maxSize = Sizing::kFontSizeHeading;
        isBold = true;
    } else if (style == "caption") {
        maxSize = Sizing::kFontSizeCaption;
    }

    // Clamp to hierarchy size and minimum mobile size
    // Ensure upper bound is at least lower bound (caption size < min mobile size)
    const float clampedSize = juce::jlimit(
        Sizing::kMinMobileFontSize,
        juce::jmax(Sizing::kMinMobileFontSize, maxSize),
        targetSize);

    // Create font using Roboto typeface (ensure loaded)
    // Note: Variable fonts don't support .withStyle() when typeface is set
    auto options = juce::FontOptions(getRobotoTypeface()).withHeight(clampedSize);
    juce::ignoreUnused(isBold);  // Bold not supported with embedded typeface

    return juce::Font(options);
}

juce::Font OTTOLookAndFeel::getFontToFitBounds(
    const juce::Rectangle<float>& bounds,
    const juce::String& text,
    float maxFontSize) const {

    if (text.isEmpty() || bounds.isEmpty()) {
        return defaultFont_;
    }

    // Start with max size and reduce until text fits
    float fontSize = juce::jmin(maxFontSize, bounds.getHeight() * 0.9f);

    // Binary search for optimal font size
    float minSize = Sizing::kMinMobileFontSize;
    float maxSize = fontSize;

    while (maxSize - minSize > 0.5f) {
        const float midSize = (minSize + maxSize) * 0.5f;
        const juce::Font testFont(juce::FontOptions(getRobotoTypeface()).withHeight(midSize));

        // Use GlyphArrangement for text measurement (modern API)
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(testFont, text, 0.0f, 0.0f);
        const float textWidth = glyphs.getBoundingBox(0, -1, true).getWidth();

        if (textWidth <= bounds.getWidth()) {
            minSize = midSize;
        } else {
            maxSize = midSize;
        }
    }

    // Use the smaller of the two to ensure it fits
    return juce::Font(juce::FontOptions(getRobotoTypeface()).withHeight(minSize));
}

// =============================================================================
// Section Title Helpers (BUG-08 — Global ALL CAPS BOLD CENTERED rule)
// =============================================================================
//
// Font construction note: FontOptions is built WITHOUT passing
// getRobotoTypeface() directly. The OTTOLookAndFeel::getTypefaceForFont()
// override always resolves to Roboto for every font request anyway, so the
// resolver path picks up the typeface — and (critically) preserving the
// resolver path keeps `.withStyle("Bold")` honoured. When FontOptions is
// constructed with an explicit typeface, JUCE silently drops the bold style
// because the embedded Roboto variable typeface only carries the regular
// weight (see getScaledFont's "Bold not supported with embedded typeface"
// comment). The size + style construction below mirrors the working pattern
// used by DelayPanel / ReverbPanel / MasterFXPanel section-title call sites.

void OTTOLookAndFeel::applySectionTitleStyle(juce::Label& label,
                                              const juce::String& text) {
    // Bug 39: ALL CAPS is a display transform applied at the helper boundary.
    if (text.isNotEmpty()) {
        label.setText(text.toUpperCase(), juce::dontSendNotification);
    } else {
        label.setText(label.getText().toUpperCase(), juce::dontSendNotification);
    }

    label.setFont(juce::Font(juce::FontOptions(Sizing::kSectionTitleFontSize)
                              .withStyle("Bold")));
    label.setJustificationType(juce::Justification::centred);
    label.setComponentID("section-header");
}

void OTTOLookAndFeel::drawSectionTitle(juce::Graphics& g,
                                        juce::Rectangle<float> bounds,
                                        const juce::String& text) {
    g.setFont(juce::Font(juce::FontOptions(Sizing::kSectionTitleFontSize)
                          .withStyle("Bold")));
    g.drawText(text.toUpperCase(),
               bounds.toNearestInt(),
               juce::Justification::centred,
               true /* useEllipsesIfTooBig */);
}

// =============================================================================
// Sizing Helpers
// =============================================================================

float OTTOLookAndFeel::getMinTouchTarget(float scaleFactor) {
    return Sizing::kMinTouchTarget * scaleFactor;
}

bool OTTOLookAndFeel::meetsTouchTarget(const juce::Component& component,
                                        float scaleFactor) {
    const float minSize = getMinTouchTarget(scaleFactor);
    return static_cast<float>(component.getWidth()) >= minSize &&
           static_cast<float>(component.getHeight()) >= minSize;
}

// =============================================================================
// Button Drawing
// =============================================================================

void OTTOLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                            juce::Button& button,
                                            const juce::Colour& /*backgroundColour*/,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown) {
    const float scale = getScaleFactorFromGraphics(g);
    auto bounds = HiDPI::snapToPixel(button.getLocalBounds().toFloat(), scale);
    const float cornerRadius = Sizing::kButtonCornerRadius;
    const float alphaMultiplier = button.isEnabled() ? 1.0f : 0.5f;

    // Apply press scale animation (0.95 scale per UI spec section 10.3)
    if (shouldDrawButtonAsDown && button.isEnabled()) {
        constexpr float kPressScale = 0.95f;
        float shrinkAmount = bounds.getWidth() * (1.0f - kPressScale) * 0.5f;
        bounds = bounds.reduced(shrinkAmount);
    }

    // Determine button style from componentID
    const juce::String componentId = button.getComponentID().toLowerCase();
    const bool isPrimary = componentId.contains("primary");
    const bool isGhost = componentId.contains("ghost");
    const bool isIcon = componentId.contains("icon");
    const bool isDestructive = componentId.contains("destructive");
    const bool isMute = componentId.contains("mute");
    const bool isSolo = componentId.contains("solo");

    // Mute buttons: red when toggled on, gray when off
    if (isMute) {
        juce::Colour baseColour = button.getToggleState()
            ? Colours::muteActive   // Red when muted
            : Colours::bg3;         // Default gray

        if (shouldDrawButtonAsDown && button.isEnabled()) {
            baseColour = baseColour.darker(0.2f);
        } else if (shouldDrawButtonAsHighlighted && button.isEnabled()) {
            baseColour = baseColour.brighter(0.15f);
        }

        g.setColour(baseColour.withMultipliedAlpha(alphaMultiplier));
        g.fillRoundedRectangle(bounds.reduced(1.0f), cornerRadius);

        // Draw border
        g.setColour(Colours::borderDefault.withMultipliedAlpha(alphaMultiplier));
        drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);
        return;
    }

    // Solo buttons: yellow when toggled on, gray when off
    if (isSolo) {
        juce::Colour baseColour = button.getToggleState()
            ? Colours::soloActive   // Yellow when soloed
            : Colours::bg4;         // Default gray

        if (shouldDrawButtonAsDown && button.isEnabled()) {
            baseColour = baseColour.darker(0.2f);
        } else if (shouldDrawButtonAsHighlighted && button.isEnabled()) {
            baseColour = baseColour.brighter(0.15f);
        }

        g.setColour(baseColour.withMultipliedAlpha(alphaMultiplier));
        g.fillRoundedRectangle(bounds.reduced(1.0f), cornerRadius);

        // Draw border
        g.setColour(Colours::borderDefault.withMultipliedAlpha(alphaMultiplier));
        drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);
        return;
    }

    // Ghost and Icon buttons: transparent background unless hovered/pressed
    if (isGhost || isIcon) {
        if (shouldDrawButtonAsDown && button.isEnabled()) {
            g.setColour(Colours::bg5.withMultipliedAlpha(alphaMultiplier));
            g.fillRoundedRectangle(bounds, cornerRadius);
        } else if (shouldDrawButtonAsHighlighted && button.isEnabled()) {
            g.setColour(Colours::bg4.withMultipliedAlpha(alphaMultiplier));
            g.fillRoundedRectangle(bounds, cornerRadius);
        }
        return;
    }

    // Primary button: accent color fill, no border
    if (isPrimary) {
        juce::Colour baseColour = isDestructive ? Colours::buttonDestructive
                                                : Colours::buttonPrimary;

        if (shouldDrawButtonAsDown && button.isEnabled()) {
            baseColour = isDestructive ? Colours::buttonDestructiveActive
                                       : Colours::buttonPrimaryActive;
        } else if (shouldDrawButtonAsHighlighted && button.isEnabled()) {
            baseColour = isDestructive ? Colours::buttonDestructiveHover
                                       : Colours::buttonPrimaryHover;
        }

        g.setColour(baseColour.withMultipliedAlpha(alphaMultiplier));
        g.fillRoundedRectangle(bounds, cornerRadius);
        return;
    }

    // Check if button has a custom background color set (e.g., player color for note buttons)
    juce::Colour customBg = button.findColour(juce::TextButton::buttonColourId);
    bool hasCustomColor = (customBg != Colours::bg3 && customBg.getAlpha() > 0);

    if (hasCustomColor) {
        // Use the custom color (e.g., player color for selected note value buttons)
        juce::Colour baseColour = customBg;

        // Apply hover/press modulation
        if (shouldDrawButtonAsDown && button.isEnabled()) {
            baseColour = baseColour.darker(0.1f);
        } else if (shouldDrawButtonAsHighlighted && button.isEnabled()) {
            baseColour = baseColour.brighter(0.1f);
        }

        g.setColour(baseColour.withMultipliedAlpha(alphaMultiplier));
        g.fillRoundedRectangle(bounds.reduced(Sizing::kButtonBorderWidth * 0.5f), cornerRadius);

        // Border for custom colored buttons
        g.setColour(Colours::borderStrong.withMultipliedAlpha(alphaMultiplier));
        drawCrispRoundedRectangle(g, bounds, cornerRadius,
                                   Sizing::kButtonBorderWidth, scale);
        return;
    }

    // Secondary button (default): bg3 fill with border
    juce::Colour baseColour = button.getToggleState()
                                  ? Colours::bg5
                                  : Colours::bg3;

    if (shouldDrawButtonAsDown && button.isEnabled()) {
        baseColour = Colours::bg5;
    } else if (shouldDrawButtonAsHighlighted && button.isEnabled()) {
        baseColour = Colours::bg4;
    }

    // Draw background
    g.setColour(baseColour.withMultipliedAlpha(alphaMultiplier));
    g.fillRoundedRectangle(bounds.reduced(Sizing::kButtonBorderWidth * 0.5f), cornerRadius);

    // Draw border
    juce::Colour borderColour;
    if (button.getToggleState() || shouldDrawButtonAsDown) {
        borderColour = Colours::borderStrong;
    } else if (shouldDrawButtonAsHighlighted) {
        borderColour = Colours::borderStrong;
    } else {
        borderColour = Colours::borderDefault;
    }

    g.setColour(borderColour.withMultipliedAlpha(alphaMultiplier));
    drawCrispRoundedRectangle(g, bounds, cornerRadius,
                               Sizing::kButtonBorderWidth, scale);
}

void OTTOLookAndFeel::drawButtonText(juce::Graphics& g,
                                      juce::TextButton& button,
                                      bool shouldDrawButtonAsHighlighted,
                                      bool shouldDrawButtonAsDown) {
    const float alphaMultiplier = button.isEnabled() ? 1.0f : 0.5f;

    // Determine button style from componentID
    const juce::String componentId = button.getComponentID().toLowerCase();
    const bool isPrimary = componentId.contains("primary");
    const bool isGhost = componentId.contains("ghost");
    const bool isIcon = componentId.contains("icon");
    const bool isDestructive = componentId.contains("destructive");
    const bool isMute = componentId.contains("mute");
    const bool isBypass = componentId.contains("bypass");
    const bool isSolo = componentId.contains("solo");

    juce::Colour textColour;

    // Mute buttons: dark text on red background when active
    if (isMute) {
        textColour = button.getToggleState()
            ? Colours::textInverse      // Dark text on red background
            : Colours::textSecondary;   // Light text on gray background
    }
    // Bypass buttons: dark text on yellow background when active
    else if (isBypass) {
        textColour = button.getToggleState()
            ? Colours::textInverse      // Dark text on yellow background
            : Colours::textSecondary;   // Light text on gray background
    }
    // Solo buttons: dark text on yellow background when active
    else if (isSolo) {
        textColour = button.getToggleState()
            ? Colours::textInverse      // Dark text on yellow background
            : Colours::textSecondary;   // Light text on gray background
    }
    // Primary and destructive buttons use inverse (dark) text
    else if (isPrimary || isDestructive) {
        textColour = Colours::textInverse;
    }
    // Ghost and icon buttons
    else if (isGhost || isIcon) {
        if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted) {
            textColour = Colours::textPrimary;
        } else {
            textColour = Colours::textSecondary;
        }
    }
    // Secondary (default) button
    else {
        if (button.getToggleState() || shouldDrawButtonAsDown) {
            textColour = Colours::textPrimary;
        } else if (shouldDrawButtonAsHighlighted) {
            textColour = Colours::textPrimary;
        } else {
            textColour = Colours::textPrimary;
        }
    }

    g.setColour(textColour.withMultipliedAlpha(alphaMultiplier));
    g.setFont(defaultFont_);

    auto bounds = button.getLocalBounds().toFloat();

    // Apply press scale animation to text (0.95 scale per UI spec section 10.3)
    if (shouldDrawButtonAsDown && button.isEnabled()) {
        constexpr float kPressScale = 0.95f;
        float shrinkAmount = bounds.getWidth() * (1.0f - kPressScale) * 0.5f;
        bounds = bounds.reduced(shrinkAmount);
    }

    g.drawText(button.getButtonText(), bounds.toNearestInt(),
               juce::Justification::centred);
}

void OTTOLookAndFeel::drawToggleButton(juce::Graphics& g,
                                        juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted,
                                        bool shouldDrawButtonAsDown) {
    const float fontSize = juce::jmin(15.0f, static_cast<float>(button.getHeight()) * 0.75f);
    const float tickWidth = fontSize * 1.1f;

    drawTickBox(g, button, 4.0f,
                (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
                tickWidth, tickWidth,
                button.getToggleState(),
                button.isEnabled(),
                shouldDrawButtonAsHighlighted,
                shouldDrawButtonAsDown);

    g.setColour(button.findColour(juce::ToggleButton::textColourId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.setFont(juce::Font(juce::FontOptions(getRobotoTypeface()).withHeight(fontSize)));

    const int textX = static_cast<int>(tickWidth) + 10;
    g.drawFittedText(button.getButtonText(),
                     textX, 0,
                     button.getWidth() - textX - 2, button.getHeight(),
                     juce::Justification::centredLeft, 10);
}

void OTTOLookAndFeel::drawTickBox(juce::Graphics& g,
                                   juce::Component& component,
                                   float x, float y, float w, float h,
                                   bool ticked, bool isEnabled,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown) {
    juce::ignoreUnused(shouldDrawButtonAsDown);

    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;
    const auto boxBounds = juce::Rectangle<float>(x, y, w, h).reduced(1.0f);
    const float cornerRadius = 4.0f;

    // Background
    juce::Colour bgColour = ticked ? Colours::buttonFace : Colours::bg3;
    if (shouldDrawButtonAsHighlighted) {
        bgColour = bgColour.brighter(0.1f);
    }
    g.setColour(bgColour.withMultipliedAlpha(alphaMultiplier));
    g.fillRoundedRectangle(boxBounds, cornerRadius);

    // Border
    g.setColour(Colours::border.withMultipliedAlpha(alphaMultiplier));
    g.drawRoundedRectangle(boxBounds, cornerRadius, 1.0f);

    // Checkmark
    if (ticked) {
        g.setColour(Colours::textPrimary.withMultipliedAlpha(alphaMultiplier));
        const auto tick = juce::Path();
        const float tickX = boxBounds.getX() + boxBounds.getWidth() * 0.25f;
        const float tickY = boxBounds.getY() + boxBounds.getHeight() * 0.5f;
        const float tickW = boxBounds.getWidth() * 0.5f;
        const float tickH = boxBounds.getHeight() * 0.35f;

        juce::Path checkPath;
        checkPath.startNewSubPath(tickX, tickY);
        checkPath.lineTo(tickX + tickW * 0.4f, tickY + tickH);
        checkPath.lineTo(tickX + tickW, tickY - tickH * 0.5f);
        g.strokePath(checkPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }
}

// =============================================================================
// Slider Drawing Helpers
// =============================================================================

juce::Colour OTTOLookAndFeel::resolveSliderFillColor(juce::Slider& slider) {
    const juce::String componentId = slider.getComponentID().toLowerCase();

    const auto fillColorProp = slider.getProperties()["fillColor"];
    if (!fillColorProp.isVoid())
        return juce::Colour::fromString(fillColorProp.toString());

    const auto playerProp = slider.getProperties()["playerIndex"];
    if (!playerProp.isVoid())
        return Colours::getPlayerColour(static_cast<int>(playerProp));

    if (componentId.contains("player_")) {
        const int startIdx = componentId.indexOf("player_") + 7;
        const int playerIndex = componentId.substring(startIdx, startIdx + 1).getIntValue();
        if (playerIndex >= 1 && playerIndex <= 8)
            return Colours::getPlayerColour(playerIndex - 1);
    }

    return Colours::accent;
}

void OTTOLookAndFeel::drawSliderTrack(juce::Graphics& g,
                                       juce::Rectangle<float> trackBounds,
                                       float sliderPos, bool isVertical,
                                       juce::Colour fillColour, bool isMasterGreyMode,
                                       float alphaMultiplier, float trackRadius,
                                       float /*scale*/) {
    if (isMasterGreyMode && isVertical) {
        juce::Rectangle<float> aboveBounds = trackBounds;
        aboveBounds.setBottom(sliderPos);
        if (aboveBounds.getHeight() > 0.0f) {
            g.setColour(Colours::masterFaderDark.withMultipliedAlpha(alphaMultiplier));
            g.fillRoundedRectangle(aboveBounds, trackRadius);
        }
        juce::Rectangle<float> belowBounds = trackBounds;
        belowBounds.setTop(sliderPos);
        if (belowBounds.getHeight() > 0.0f) {
            g.setColour(Colours::masterFaderLight.withMultipliedAlpha(alphaMultiplier));
            g.fillRoundedRectangle(belowBounds, trackRadius);
        }
        return;
    }

    // Track background with subtle gradient for 3D depth
    juce::Colour trackTop = Colours::bg4.darker(0.15f);
    juce::Colour trackBottom = Colours::bg4.brighter(0.05f);
    juce::ColourGradient trackGradient;
    if (isVertical) {
        trackGradient = juce::ColourGradient(
            trackTop.withMultipliedAlpha(alphaMultiplier),
            trackBounds.getX(), trackBounds.getY(),
            trackBottom.withMultipliedAlpha(alphaMultiplier),
            trackBounds.getRight(), trackBounds.getY(), false);
    } else {
        trackGradient = juce::ColourGradient(
            trackTop.withMultipliedAlpha(alphaMultiplier),
            trackBounds.getX(), trackBounds.getY(),
            trackBottom.withMultipliedAlpha(alphaMultiplier),
            trackBounds.getX(), trackBounds.getBottom(), false);
    }
    g.setGradientFill(trackGradient);
    g.fillRoundedRectangle(trackBounds, trackRadius);

    // Filled portion showing current value
    juce::Rectangle<float> filledBounds = trackBounds;
    if (isVertical)
        filledBounds.setTop(sliderPos);
    else
        filledBounds.setWidth(sliderPos - trackBounds.getX());

    if (filledBounds.getWidth() > 0.0f && filledBounds.getHeight() > 0.0f) {
        g.setColour(fillColour.withMultipliedAlpha(alphaMultiplier));
        g.fillRoundedRectangle(filledBounds, trackRadius);
    }
}

void OTTOLookAndFeel::drawFaderCapThumb(juce::Graphics& g,
                                         float sliderPos, int x, int y,
                                         int width, int height,
                                         float faderCapHeight, bool isVertical,
                                         float scale) {
    if (!faderCapImage_.isNull()) {
        const float imageAspect = static_cast<float>(faderCapImage_.getWidth()) /
                                  static_cast<float>(faderCapImage_.getHeight());
        if (isVertical) {
            const float capWidth = faderCapHeight * imageAspect;
            const float clampedPos = juce::jlimit(
                static_cast<float>(y) + faderCapHeight * 0.5f,
                static_cast<float>(y + height) - faderCapHeight * 0.5f, sliderPos);
            const float capX = HiDPI::snapToPixel(
                static_cast<float>(x) + (static_cast<float>(width) - capWidth) * 0.5f, scale);
            g.drawImage(faderCapImage_,
                        {capX, HiDPI::snapToPixel(clampedPos - faderCapHeight * 0.5f, scale),
                         capWidth, faderCapHeight},
                        juce::RectanglePlacement::centred);
        } else {
            const float capWidth = faderCapHeight;
            const float capHeightH = capWidth * imageAspect;
            const float clampedPos = juce::jlimit(
                static_cast<float>(x) + capWidth * 0.5f,
                static_cast<float>(x + width) - capWidth * 0.5f, sliderPos);
            const float capX = HiDPI::snapToPixel(clampedPos - capWidth * 0.5f, scale);
            const float capY = HiDPI::snapToPixel(
                static_cast<float>(y) + (static_cast<float>(height) - capHeightH) * 0.5f, scale);
            auto capBounds = juce::Rectangle<float>(capX, capY, capWidth, capHeightH);

            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(
                juce::MathConstants<float>::halfPi,
                capBounds.getCentreX(), capBounds.getCentreY()));
            g.drawImage(faderCapImage_,
                        capBounds.withSizeKeepingCentre(capHeightH, capWidth),
                        juce::RectanglePlacement::centred);
            g.restoreState();
        }
        return;
    }

    // Fallback: simple rectangular cap
    constexpr float capRadius = 3.0f;
    constexpr float fallbackAspect = 0.67f;
    juce::Rectangle<float> capBounds;

    if (isVertical) {
        const float capWidth = faderCapHeight * fallbackAspect;
        const float clampedPos = juce::jlimit(
            static_cast<float>(y) + faderCapHeight * 0.5f,
            static_cast<float>(y + height) - faderCapHeight * 0.5f, sliderPos);
        capBounds = {HiDPI::snapToPixel(
                         static_cast<float>(x) + (static_cast<float>(width) - capWidth) * 0.5f, scale),
                     HiDPI::snapToPixel(clampedPos - faderCapHeight * 0.5f, scale),
                     capWidth, faderCapHeight};
    } else {
        const float capWidth = faderCapHeight;
        const float capHeightH = capWidth * fallbackAspect;
        const float clampedPos = juce::jlimit(
            static_cast<float>(x) + capWidth * 0.5f,
            static_cast<float>(x + width) - capWidth * 0.5f, sliderPos);
        capBounds = {HiDPI::snapToPixel(clampedPos - capWidth * 0.5f, scale),
                     HiDPI::snapToPixel(
                         static_cast<float>(y) + (static_cast<float>(height) - capHeightH) * 0.5f, scale),
                     capWidth, capHeightH};
    }

    g.setColour(Colours::sliderCapColor);
    g.fillRoundedRectangle(capBounds, capRadius);
    g.setColour(Colours::sliderCapBorder);
    g.drawRoundedRectangle(capBounds, capRadius, 1.0f);
}

void OTTOLookAndFeel::drawStandardThumb(juce::Graphics& g, juce::Slider& slider,
                                         float sliderPos, int x, int y,
                                         int width, int height,
                                         float thumbSize, juce::Colour fillColour,
                                         juce::Colour thumbBaseColour,
                                         bool isEnabled, float alphaMultiplier,
                                         float scale) {
    const bool isVertical = (slider.getSliderStyle() == juce::Slider::LinearVertical ||
                             slider.getSliderStyle() == juce::Slider::LinearBarVertical);
    juce::Rectangle<float> thumbBounds;
    if (isVertical) {
        thumbBounds = {HiDPI::snapToPixel(
                           static_cast<float>(x) +
                           (static_cast<float>(width) - thumbSize) * 0.5f, scale),
                       HiDPI::snapToPixel(sliderPos - thumbSize * 0.5f, scale),
                       thumbSize, thumbSize};
    } else {
        thumbBounds = {HiDPI::snapToPixel(sliderPos - thumbSize * 0.5f, scale),
                       HiDPI::snapToPixel(
                           static_cast<float>(y) +
                           (static_cast<float>(height) - thumbSize) * 0.5f, scale),
                       thumbSize, thumbSize};
    }

    // Subtle glow when hovering/dragging
    if (isEnabled && slider.isMouseOverOrDragging()) {
        const float glowSize = thumbSize * 1.4f;
        auto glowBounds = thumbBounds.withSizeKeepingCentre(glowSize, glowSize);
        juce::ColourGradient glowGradient(
            fillColour.withAlpha(slider.isMouseButtonDown() ? 0.35f : 0.2f),
            glowBounds.getCentreX(), glowBounds.getCentreY(),
            fillColour.withAlpha(0.0f),
            glowBounds.getCentreX(), glowBounds.getY(), true);
        g.setGradientFill(glowGradient);
        g.fillEllipse(glowBounds);
    }

    // Thumb shadow for depth
    if (isEnabled) {
        const float shadowOffset = HiDPI::getScaledStrokeWidth(2.0f, scale);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillEllipse(thumbBounds.translated(0.0f, shadowOffset));
    }

    // Thumb with subtle gradient for 3D effect
    juce::ColourGradient thumbGradient(
        thumbBaseColour.brighter(0.15f).withMultipliedAlpha(alphaMultiplier),
        thumbBounds.getCentreX(), thumbBounds.getY(),
        thumbBaseColour.darker(0.1f).withMultipliedAlpha(alphaMultiplier),
        thumbBounds.getCentreX(), thumbBounds.getBottom(), false);
    g.setGradientFill(thumbGradient);
    g.fillEllipse(thumbBounds);

    // Inner highlight (subtle white arc at top)
    if (isEnabled) {
        auto highlightBounds = thumbBounds.reduced(thumbSize * 0.15f);
        highlightBounds = highlightBounds.withBottom(thumbBounds.getCentreY());
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillEllipse(highlightBounds);
    }

    // Thumb border in fill color
    g.setColour(fillColour.withMultipliedAlpha(alphaMultiplier));
    const float thumbBorderWidth = HiDPI::getScaledStrokeWidth(2.0f, scale);
    g.drawEllipse(thumbBounds.reduced(thumbBorderWidth * 0.5f), thumbBorderWidth);
}

// =============================================================================
// Slider Drawing
// =============================================================================

void OTTOLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                        int x, int y, int width, int height,
                                        float sliderPos,
                                        float /*minSliderPos*/,
                                        float /*maxSliderPos*/,
                                        juce::Slider::SliderStyle style,
                                        juce::Slider& slider) {
    const float scale = getScaleFactorFromGraphics(g);
    const bool isVertical = style == juce::Slider::LinearVertical ||
                            style == juce::Slider::LinearBarVertical;
    const bool isEnabled = slider.isEnabled();
    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;

    const auto trackWidthProp = slider.getProperties()["sliderTrackWidth"];
    const float trackWidth = !trackWidthProp.isVoid()
        ? static_cast<float>(trackWidthProp)
        : Sizing::kSliderTrackWidth;
    const float trackRadius = trackWidth * 0.5f;
    const float thumbSize = Sizing::kSliderThumbSize;
    const juce::Colour fillColour = resolveSliderFillColor(slider);

    // Check for master grey mode and fader cap properties
    const auto masterGreyProp = slider.getProperties()["masterGreyMode"];
    const bool isMasterGreyMode = !masterGreyProp.isVoid() && static_cast<bool>(masterGreyProp);
    const auto faderCapProp = slider.getProperties()["faderCap"];
    const bool isFaderCap = !faderCapProp.isVoid() && static_cast<bool>(faderCapProp);

    // Calculate fader cap height for track insets
    const float trackDimension = isVertical ? static_cast<float>(height) : static_cast<float>(width);
    const auto minCapProp = slider.getProperties()["minFaderCapHeight"];
    const float minCapHeight = !minCapProp.isVoid() ? static_cast<float>(minCapProp) : 28.0f;
    const float faderCapHeight = isFaderCap ? juce::jlimit(minCapHeight, 80.0f, trackDimension * 0.22f) : 0.0f;
    const float trackInsetTop = isFaderCap ? faderCapHeight * 0.55f : 0.0f;
    const float trackInsetBottom = isFaderCap ? faderCapHeight * 0.35f : 0.0f;

    // Calculate track bounds centered in available space (pixel-aligned)
    juce::Rectangle<float> trackBounds;
    if (isVertical) {
        const float trackX = HiDPI::snapToPixel(
            static_cast<float>(x) + (static_cast<float>(width) - trackWidth) * 0.5f, scale);
        trackBounds = {trackX, static_cast<float>(y) + trackInsetTop,
                       trackWidth, static_cast<float>(height) - trackInsetTop - trackInsetBottom};
    } else {
        const float trackY = HiDPI::snapToPixel(
            static_cast<float>(y) + (static_cast<float>(height) - trackWidth) * 0.5f, scale);
        trackBounds = {static_cast<float>(x) + trackInsetBottom, trackY,
                       static_cast<float>(width) - trackInsetTop - trackInsetBottom, trackWidth};
    }
    trackBounds = HiDPI::snapToPixel(trackBounds, scale);

    drawSliderTrack(g, trackBounds, sliderPos, isVertical, fillColour,
                    isMasterGreyMode, alphaMultiplier, trackRadius, scale);

    // Draw thumb
    if (isFaderCap) {
        drawFaderCapThumb(g, sliderPos, x, y, width, height, faderCapHeight, isVertical, scale);
    } else {
        juce::Colour thumbBaseColour = Colours::sliderThumb;
        if (isEnabled && slider.isMouseOverOrDragging()) {
            thumbBaseColour = slider.isMouseButtonDown() ? Colours::sliderDragging
                                                          : Colours::sliderHover;
        }
        drawStandardThumb(g, slider, sliderPos, x, y, width, height,
                          thumbSize, fillColour, thumbBaseColour,
                          isEnabled, alphaMultiplier, scale);
    }
}

void OTTOLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                        int x, int y, int width, int height,
                                        float sliderPosProportional,
                                        float rotaryStartAngle,
                                        float rotaryEndAngle,
                                        juce::Slider& slider) {
    const float scale = getScaleFactorFromGraphics(g);
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const bool isEnabled = slider.isEnabled();
    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;
    const juce::Colour fillColour = resolveSliderFillColor(slider);

    // Default: rotary fills its allotted bounds so it never shrinks to a
    // dot-in-a-box. A component can opt back into the legacy 56 px cap by
    // setting slider.getProperties().set("enforceRotaryCap", true).
    const float maxDiameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const bool enforceCap =
        static_cast<bool>(slider.getProperties().getWithDefault("enforceRotaryCap", false));
    const float diameter = enforceCap ? juce::jmin(Sizing::kRotaryKnobSize, maxDiameter)
                                      : maxDiameter;
    const float radius = diameter * 0.5f;
    const float centreX = HiDPI::snapToPixel(bounds.getCentreX(), scale);
    const float centreY = HiDPI::snapToPixel(bounds.getCentreY(), scale);

    // Arc dimensions per spec: 4px thickness, 270 deg span
    const float arcThickness = HiDPI::getScaledStrokeWidth(Sizing::kRotaryArcThickness, scale);
    const float arcRadius = radius - arcThickness * 1.5f;
    const float knobRadius = radius * 0.55f;

    // Background arc (unfilled portion)
    juce::Path bgArcPath;
    bgArcPath.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(Colours::knobArcBackground.withMultipliedAlpha(alphaMultiplier));
    g.strokePath(bgArcPath, juce::PathStrokeType(
        arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Current angle from normalized position
    const float angle = rotaryStartAngle +
                        sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Value arc (filled portion) in player/accent color
    juce::Path valueArcPath;
    valueArcPath.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                               0.0f, rotaryStartAngle, angle, true);
    g.setColour(fillColour.withMultipliedAlpha(alphaMultiplier));
    g.strokePath(valueArcPath, juce::PathStrokeType(
        arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Center knob with subtle gradient
    const auto knobBounds = juce::Rectangle<float>(
        centreX - knobRadius, centreY - knobRadius,
        knobRadius * 2.0f, knobRadius * 2.0f);
    const juce::ColourGradient knobGradient(
        Colours::bg3.brighter(0.1f).withMultipliedAlpha(alphaMultiplier),
        knobBounds.getX(), knobBounds.getY(),
        Colours::bg3.darker(0.05f).withMultipliedAlpha(alphaMultiplier),
        knobBounds.getX(), knobBounds.getBottom(), false);
    g.setGradientFill(knobGradient);
    g.fillEllipse(knobBounds);

    // Knob border (subtle)
    g.setColour(Colours::borderDefault.withMultipliedAlpha(alphaMultiplier * 0.5f));
    drawCrispEllipse(g, knobBounds, 1.0f, scale);

    // Center dot indicator (shows current position)
    const float dotRadius = knobRadius * 0.15f;
    const float dotDistance = knobRadius * 0.55f;
    const float angleOffset = angle - juce::MathConstants<float>::halfPi;
    const float dotX = centreX + std::cos(angleOffset) * dotDistance;
    const float dotY = centreY + std::sin(angleOffset) * dotDistance;
    g.setColour(Colours::textPrimary.withMultipliedAlpha(alphaMultiplier));
    g.fillEllipse(HiDPI::snapToPixel(dotX - dotRadius, scale),
                  HiDPI::snapToPixel(dotY - dotRadius, scale),
                  HiDPI::snapToPixel(dotRadius * 2.0f, scale),
                  HiDPI::snapToPixel(dotRadius * 2.0f, scale));

    // Interaction feedback on the knob
    if (isEnabled && slider.isMouseOverOrDragging()) {
        const float feedbackAlpha = slider.isMouseButtonDown() ? 0.15f : 0.08f;
        g.setColour(juce::Colours::white.withAlpha(feedbackAlpha));
        g.fillEllipse(knobBounds);
    }
}

int OTTOLookAndFeel::getSliderThumbRadius(juce::Slider& slider) {
    // For hardware-style fader caps, return larger radius to cover full cap width
    const auto faderCapProp = slider.getProperties()["faderCap"];
    const bool isFaderCap = !faderCapProp.isVoid() && static_cast<bool>(faderCapProp);

    if (isFaderCap) {
        // Fader cap is 36px wide - return half for circular hit area
        return 18;
    }

    // Standard circular thumb: return half the thumb size
    return static_cast<int>(Sizing::kSliderThumbSize * 0.5f);
}

// =============================================================================
// Label Drawing
// =============================================================================

void OTTOLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited()) {
        const juce::Font font = getLabelFont(label);
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(font);

        const auto textArea = label.getBorderSize().subtractedFrom(
            label.getLocalBounds());

        // BUG-08: defensive centred-justification enforcement for any label
        // tagged as a section title. Per Bug 88's weakest-strongest cascade,
        // the LookAndFeel layer is the strongest authority on the title
        // convention — a future call site that forgets applySectionTitleStyle
        // but still tags the label with the "section-header" /
        // "sectionHeader" ComponentID still renders centred.
        const juce::String componentId = label.getComponentID().toLowerCase();
        const bool isSectionTitle = componentId.contains("section-header")
                                  || componentId.contains("sectionheader");
        const juce::Justification justification = isSectionTitle
            ? juce::Justification::centred
            : label.getJustificationType();

        // Use GlyphArrangement for proper text rendering at all scales
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText(font, label.getText(),
                             static_cast<float>(textArea.getX()),
                             static_cast<float>(textArea.getY()),
                             static_cast<float>(textArea.getWidth()),
                             static_cast<float>(textArea.getHeight()),
                             justification,
                             juce::jmax(1, static_cast<int>(
                                 static_cast<float>(textArea.getHeight()) /
                                 font.getHeight())),
                             label.getMinimumHorizontalScale());

        glyphs.draw(g);
    }
}

juce::Font OTTOLookAndFeel::getLabelFont(juce::Label& label) {
    return getTypographyFont(label, 1.0f);
}

juce::Font OTTOLookAndFeel::getTypographyFont(juce::Label& label,
                                               float scaleFactor) const {
    const juce::String componentId = label.getComponentID().toLowerCase();
    const juce::Font currentFont = label.getFont();
    const bool isBold = currentFont.isBold();
    const float labelHeight = static_cast<float>(label.getHeight());

    // Determine typography level from component ID or font style
    if (componentId.contains("large-light")) {
        // Tempo display tier — Orbitron geometric/futuristic face on the
        // top-row BPM readout. Sized at 70% of label height like before so
        // the visual footprint matches the surrounding transport chrome.
        const float targetSize = labelHeight * 0.7f * scaleFactor;
        const float clampedSize = juce::jlimit(
            Sizing::kMinMobileFontSize,
            juce::jmax(Sizing::kMinMobileFontSize, Sizing::kFontSizeLarge),
            targetSize);
        return juce::Font(juce::FontOptions(getOrbitronTypeface())
            .withHeight(clampedSize));
    }

    if (componentId.contains("large")) {
        return getScaledFont(labelHeight * scaleFactor, "large");
    }

    if (componentId.contains("title")) {
        return getScaledFont(labelHeight * scaleFactor, "title");
    }

    // BUG-08: section-header / sectionHeader ComponentIDs route to the
    // heading typography tier even without the heading keyword in the ID
    // (the section-title helper sets the ID literally as "section-header").
    if (componentId.contains("heading")
        || componentId.contains("section-header")
        || componentId.contains("sectionheader")
        || isBold) {
        return getScaledFont(labelHeight * scaleFactor, "heading");
    }

    if (componentId.contains("caption") || componentId.contains("small")) {
        return getScaledFont(labelHeight * scaleFactor, "caption");
    }

    // Default: body text, scaled to fit with minimum mobile size
    const float targetSize = juce::jmax(
        Sizing::kMinMobileFontSize,
        juce::jmin(Sizing::kFontSizeBody, labelHeight * 0.7f * scaleFactor));

    return juce::Font(juce::FontOptions(getRobotoTypeface()).withHeight(targetSize));
}

// =============================================================================
// ComboBox Drawing
// =============================================================================

void OTTOLookAndFeel::drawComboBox(juce::Graphics& g,
                                    int width, int height,
                                    bool isButtonDown,
                                    int /*buttonX*/, int /*buttonY*/,
                                    int /*buttonW*/, int /*buttonH*/,
                                    juce::ComboBox& box) {
    const float scale = getScaleFactorFromGraphics(g);
    const auto bounds = HiDPI::snapToPixel(
        juce::Rectangle<int>(0, 0, width, height).toFloat(), scale);
    const float cornerRadius = Sizing::kButtonCornerRadius;

    juce::Colour bgColour = box.findColour(juce::ComboBox::backgroundColourId);

    // Transparent-overlay short-circuit: PlayerCard's ModeDropdown and
    // EnergyDropdown set backgroundColourId to transparentBlack so the
    // painted banner / energy-name area shows through. Painting any solid
    // surface here (flat OR gradient) destroys those overlays. Leave the
    // closed-combo body untouched in that case so the underlying paint
    // helper remains the visible chrome. The expanded popup still picks up
    // the gradient via drawPopupMenuBackgroundWithOptions below.
    if (bgColour.isTransparent()) {
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);
        return;
    }

    if (isButtonDown) {
        bgColour = bgColour.darker(0.1f);
    }

    // DROP-01: radial-gradient body. Accent comes from the "otto-accent"
    // Component property stamped by PlayerCard on embedded ComboBoxes;
    // dropdowns outside a PlayerCard fall back to the neutral default. The
    // accent's brightness is folded INTO the gradient via paintDropdownGradient
    // so the outline + chrome stay consistent with the legacy paint.
    juce::Colour accent = resolveDropdownAccent(&box);
    if (isButtonDown) {
        accent = accent.darker(0.1f);
    }
    paintDropdownGradient(g, bounds.reduced(1.0f), accent, cornerRadius);

    // Border (scale-aware)
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);
}

// =============================================================================
// Special Component Drawing - Mute/Solo Buttons
// =============================================================================

void OTTOLookAndFeel::drawMuteButton(juce::Graphics& g,
                                      juce::Rectangle<float> bounds,
                                      bool isActive,
                                      bool isHighlighted,
                                      bool isDown,
                                      bool isEnabled) {
    const float scale = getScaleFactorFromGraphics(g);
    bounds = HiDPI::snapToPixel(bounds, scale);
    const float cornerRadius = Sizing::kButtonCornerRadius;
    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;

    // Choose color based on mute state
    juce::Colour baseColour = isActive ? Colours::muteActive
                                       : Colours::muteInactive;

    // Apply interaction state modifications
    if (isDown && isEnabled) {
        baseColour = baseColour.darker(0.2f);
    } else if (isHighlighted && isEnabled) {
        baseColour = baseColour.brighter(0.15f);
    }

    // Draw button background with gradient
    const juce::ColourGradient gradient(
        baseColour.brighter(0.1f).withMultipliedAlpha(alphaMultiplier),
        bounds.getX(), bounds.getY(),
        baseColour.darker(0.1f).withMultipliedAlpha(alphaMultiplier),
        bounds.getX(), bounds.getBottom(),
        false);

    g.setGradientFill(gradient);
    g.fillRoundedRectangle(bounds.reduced(1.0f), cornerRadius);

    // Draw border (scale-aware)
    g.setColour(Colours::border.withMultipliedAlpha(alphaMultiplier));
    drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);

    // Draw "M" text
    g.setColour((isActive ? Colours::textPrimary : Colours::textMuted)
                    .withMultipliedAlpha(alphaMultiplier));
    g.setFont(juce::Font(juce::FontOptions(getRobotoTypeface())
                             .withHeight(bounds.getHeight() * 0.5f)
                             ));
    g.drawText("M", bounds, juce::Justification::centred);
}

void OTTOLookAndFeel::drawSoloButton(juce::Graphics& g,
                                      juce::Rectangle<float> bounds,
                                      bool isActive,
                                      bool isHighlighted,
                                      bool isDown,
                                      bool isEnabled) {
    const float scale = getScaleFactorFromGraphics(g);
    bounds = HiDPI::snapToPixel(bounds, scale);
    const float cornerRadius = Sizing::kButtonCornerRadius;
    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;

    // Choose color based on solo state
    juce::Colour baseColour = isActive ? Colours::soloActive
                                       : Colours::soloInactive;

    // Apply interaction state modifications
    if (isDown && isEnabled) {
        baseColour = baseColour.darker(0.2f);
    } else if (isHighlighted && isEnabled) {
        baseColour = baseColour.brighter(0.15f);
    }

    // Draw button background with gradient
    const juce::ColourGradient gradient(
        baseColour.brighter(0.1f).withMultipliedAlpha(alphaMultiplier),
        bounds.getX(), bounds.getY(),
        baseColour.darker(0.1f).withMultipliedAlpha(alphaMultiplier),
        bounds.getX(), bounds.getBottom(),
        false);

    g.setGradientFill(gradient);
    g.fillRoundedRectangle(bounds.reduced(1.0f), cornerRadius);

    // Draw border (scale-aware)
    g.setColour(Colours::border.withMultipliedAlpha(alphaMultiplier));
    drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);

    // Draw "S" text - use dark text on yellow for better contrast
    const juce::Colour textColour = isActive ? Colours::textBlack
                                             : Colours::textMuted;
    g.setColour(textColour.withMultipliedAlpha(alphaMultiplier));
    g.setFont(juce::Font(juce::FontOptions(getRobotoTypeface())
                             .withHeight(bounds.getHeight() * 0.5f)
                             ));
    g.drawText("S", bounds, juce::Justification::centred);
}

// =============================================================================
// Special Component Drawing - Transport Buttons
// =============================================================================

void OTTOLookAndFeel::drawPlayButton(juce::Graphics& g,
                                      juce::Rectangle<float> bounds,
                                      bool isPlaying,
                                      bool isHighlighted,
                                      bool isDown,
                                      bool isEnabled,
                                      juce::Colour iconOverride) {
    const float scale = getScaleFactorFromGraphics(g);
    bounds = HiDPI::snapToPixel(bounds, scale);
    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;
    juce::ignoreUnused(isHighlighted, isDown);

    // No background or border - just the icon
    // Larger icon size (0.7) to match tempo/time sig display height (~32-36pt)
    const float iconSize = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.7f;
    const float centreX = HiDPI::snapToPixel(bounds.getCentreX(), scale);
    const float centreY = HiDPI::snapToPixel(bounds.getCentreY(), scale);

    juce::Path playIcon;
    // Triangle pointing right, slightly offset to visual center
    playIcon.addTriangle(
        centreX - iconSize * 0.35f, centreY - iconSize * 0.5f,
        centreX - iconSize * 0.35f, centreY + iconSize * 0.5f,
        centreX + iconSize * 0.55f, centreY);

    // Green when playing, gray when stopped — override wins when set
    const juce::Colour defaultIcon = isPlaying ? Colours::playActive
                                               : Colours::transportInactive;
    const juce::Colour iconColour  = iconOverride.isTransparent() ? defaultIcon
                                                                  : iconOverride;
    g.setColour(iconColour.withMultipliedAlpha(alphaMultiplier));
    g.fillPath(playIcon);
}

void OTTOLookAndFeel::drawPauseButton(juce::Graphics& g,
                                       juce::Rectangle<float> bounds,
                                       bool isPaused,
                                       bool isHighlighted,
                                       bool isDown,
                                       bool isEnabled,
                                       juce::Colour iconOverride) {
    const float scale = getScaleFactorFromGraphics(g);
    bounds = HiDPI::snapToPixel(bounds, scale);
    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;
    juce::ignoreUnused(isHighlighted, isDown);

    // No background or border - just the icon
    // Larger icon size (0.7) to match tempo/time sig display height (~32-36pt)
    const float iconHeight = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.7f;
    const float barWidth = HiDPI::snapToPixel(iconHeight * 0.3f, scale);
    const float gap = HiDPI::snapToPixel(barWidth * 0.6f, scale);
    const float centreX = HiDPI::snapToPixel(bounds.getCentreX(), scale);
    const float centreY = HiDPI::snapToPixel(bounds.getCentreY(), scale);

    // Green when paused (indicating it will resume), gray otherwise — override wins
    const juce::Colour defaultIcon = isPaused ? Colours::playActive
                                              : Colours::transportInactive;
    const juce::Colour iconColour  = iconOverride.isTransparent() ? defaultIcon
                                                                  : iconOverride;
    g.setColour(iconColour.withMultipliedAlpha(alphaMultiplier));

    // Left bar
    g.fillRoundedRectangle(HiDPI::snapToPixel(centreX - gap - barWidth, scale),
                           HiDPI::snapToPixel(centreY - iconHeight * 0.5f, scale),
                           barWidth, iconHeight, 2.0f);
    // Right bar
    g.fillRoundedRectangle(HiDPI::snapToPixel(centreX + gap, scale),
                           HiDPI::snapToPixel(centreY - iconHeight * 0.5f, scale),
                           barWidth, iconHeight, 2.0f);
}

void OTTOLookAndFeel::drawTransportBanner(juce::Graphics& g,
                                          juce::Rectangle<float> bounds,
                                          bool isPlaying,
                                          bool isHovered,
                                          bool isEnabled,
                                          juce::Colour accent) {
    const float scale  = getScaleFactorFromGraphics(g);
    bounds = HiDPI::snapToPixel(bounds, scale);
    const float radius = 8.0f;
    const float alpha  = isEnabled ? 1.0f : 0.5f;

    if (isPlaying) {
        // Active glow — accent-tinted soft shadow under the pill
        const juce::Colour glow = accent.withAlpha(0.45f * alpha);
        juce::Path glowPath;
        glowPath.addRoundedRectangle(bounds, radius);
        juce::DropShadow(glow, 18, juce::Point<int>(0, 0)).drawForPath(g, glowPath);

        // Background — accent face matching FsmButtonStyle armed state
        juce::ColourGradient grad(
            accent.interpolatedWith(juce::Colours::white, 0.30f).withMultipliedAlpha(alpha),
            bounds.getX(), bounds.getY(),
            accent.interpolatedWith(juce::Colours::black, 0.30f).withMultipliedAlpha(alpha),
            bounds.getX(), bounds.getBottom(), false);
        grad.addColour(0.7, accent.withMultipliedAlpha(alpha));
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, radius);

        // Border — accent mixed with white
        g.setColour(accent.interpolatedWith(juce::Colours::white, 0.40f).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

        // Inset highlight + shadow hairlines (FsmButtonStyle armed insets)
        g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.40f * alpha));
        g.fillRect(bounds.getX() + 1.0f, bounds.getY() + 1.0f, bounds.getWidth() - 2.0f, 1.0f);
        g.setColour(juce::Colour::fromFloatRGBA(0.0f, 0.0f, 0.0f, 0.30f * alpha));
        g.fillRect(bounds.getX() + 1.0f, bounds.getBottom() - 2.0f, bounds.getWidth() - 2.0f, 1.0f);
    } else {
        // Idle — accent-tinted dark gradient, deeper tint when hovered
        const float topMix = isHovered ? 0.24f : 0.18f;
        const float botMix = isHovered ? 0.12f : 0.08f;
        juce::ColourGradient grad(
            Colours::bg3.interpolatedWith(accent, topMix).withMultipliedAlpha(alpha),
            bounds.getX(), bounds.getY(),
            Colours::bg2.interpolatedWith(accent, botMix).withMultipliedAlpha(alpha),
            bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, radius);

        // Border — soft line tinted with accent
        const juce::Colour line = juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, isHovered ? 0.18f : 0.10f)
                                      .interpolatedWith(accent, 0.30f);
        g.setColour(line.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
    }

    // Top hairline — runs across the upper edge inside the rounded corners.
    // Identical to PlayerCard mode-banner top hairline.
    const float hairInset = 8.0f;
    const float hairY     = bounds.getY() + 1.0f;
    juce::ColourGradient hair(
        juce::Colours::transparentBlack,
        bounds.getX() + hairInset, hairY,
        juce::Colours::transparentBlack,
        bounds.getRight() - hairInset, hairY, false);
    hair.addColour(0.5,
                   accent.interpolatedWith(juce::Colours::white, 0.40f)
                         .withAlpha(0.55f * alpha));
    g.setGradientFill(hair);
    g.fillRect(bounds.getX() + hairInset, hairY,
               bounds.getWidth() - 2.0f * hairInset, 1.0f);
}

void OTTOLookAndFeel::drawStopButton(juce::Graphics& g,
                                      juce::Rectangle<float> bounds,
                                      bool isPlaying,
                                      bool isHighlighted,
                                      bool isDown,
                                      bool isEnabled) {
    const float scale = getScaleFactorFromGraphics(g);
    bounds = HiDPI::snapToPixel(bounds, scale);
    const float cornerRadius = Sizing::kButtonCornerRadius;
    const float alphaMultiplier = isEnabled ? 1.0f : 0.5f;

    // Background
    juce::Colour bgColour = Colours::backgroundSecondary;
    if (isDown && isEnabled) {
        bgColour = bgColour.darker(0.15f);
    } else if (isHighlighted && isEnabled) {
        bgColour = bgColour.brighter(0.1f);
    }

    g.setColour(bgColour.withMultipliedAlpha(alphaMultiplier));
    g.fillRoundedRectangle(bounds.reduced(1.0f), cornerRadius);

    // Border (scale-aware)
    g.setColour(Colours::border.withMultipliedAlpha(alphaMultiplier));
    drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);

    // Draw stop icon (square) - pixel-aligned
    const float iconSize = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.35f;
    const float centreX = HiDPI::snapToPixel(bounds.getCentreX(), scale);
    const float centreY = HiDPI::snapToPixel(bounds.getCentreY(), scale);

    // Red when playing (to indicate "stop"), gray when stopped (per UI spec)
    juce::Colour iconColour = isPlaying ? Colours::stopActive : Colours::transportInactive;
    if (isDown && isPlaying) {
        iconColour = iconColour.brighter(0.2f);
    }
    g.setColour(iconColour.withMultipliedAlpha(alphaMultiplier));
    g.fillRoundedRectangle(HiDPI::snapToPixel(centreX - iconSize * 0.5f, scale),
                           HiDPI::snapToPixel(centreY - iconSize * 0.5f, scale),
                           HiDPI::snapToPixel(iconSize, scale),
                           HiDPI::snapToPixel(iconSize, scale), 2.0f);
}

// =============================================================================
// Special Component Drawing - Pattern Grid Cells
// =============================================================================

void OTTOLookAndFeel::drawPatternCell(juce::Graphics& g,
                                       juce::Rectangle<float> bounds,
                                       bool isActive,
                                       bool isCurrent,
                                       int phraseIndex,
                                       bool isHighlighted,
                                       bool isDown,
                                       bool isEnabled) {
    const float scale = getScaleFactorFromGraphics(g);
    bounds = HiDPI::snapToPixel(bounds, scale);
    const float cornerRadius = 4.0f;  // Slightly smaller for grid cells
    const float alphaMultiplier = isEnabled ? 1.0f : 0.4f;

    // Get phrase color
    const juce::Colour phraseColour = Colours::getPhraseColour(phraseIndex);

    // Determine base color
    juce::Colour baseColour;
    if (isActive) {
        baseColour = phraseColour;
    } else {
        // Inactive cells show a subtle hint of the phrase color
        baseColour = Colours::backgroundDarker.interpolatedWith(phraseColour, 0.15f);
    }

    // Apply interaction state
    if (isDown && isEnabled) {
        baseColour = baseColour.darker(0.2f);
    } else if (isHighlighted && isEnabled) {
        baseColour = baseColour.brighter(0.15f);
    }

    // Draw cell background
    g.setColour(baseColour.withMultipliedAlpha(alphaMultiplier));
    g.fillRoundedRectangle(bounds.reduced(1.0f), cornerRadius);

    // Draw current beat indicator (bright border when this step is playing)
    if (isCurrent && isEnabled) {
        g.setColour(Colours::textPrimary);
        const float currentStroke = HiDPI::getScaledStrokeWidth(2.5f, scale);
        drawCrispRoundedRectangle(g, bounds.reduced(1.5f), cornerRadius,
                                   currentStroke, scale);
    } else {
        // Normal border (scale-aware)
        g.setColour(Colours::border.withMultipliedAlpha(alphaMultiplier * 0.7f));
        drawCrispRoundedRectangle(g, bounds, cornerRadius, 1.0f, scale);
    }

    // Draw active indicator dot in center for active cells
    if (isActive && isEnabled) {
        const float dotSize = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.2f;
        const float centreX = HiDPI::snapToPixel(bounds.getCentreX(), scale);
        const float centreY = HiDPI::snapToPixel(bounds.getCentreY(), scale);

        // Slightly brighter dot for emphasis
        g.setColour(phraseColour.brighter(0.3f));
        g.fillEllipse(HiDPI::snapToPixel(centreX - dotSize * 0.5f, scale),
                      HiDPI::snapToPixel(centreY - dotSize * 0.5f, scale),
                      HiDPI::snapToPixel(dotSize, scale),
                      HiDPI::snapToPixel(dotSize, scale));
    }
}

// =============================================================================
// =============================================================================
// ComboBox and PopupMenu Drawing
// =============================================================================

void OTTOLookAndFeel::drawPopupMenuBackground(juce::Graphics& g,
                                               int width, int height) {
    // Legacy LookAndFeelMethods entry point. JUCE's MenuWindow now invokes
    // drawPopupMenuBackgroundWithOptions instead, but any direct callers (e.g.
    // a LookAndFeel-derived class chaining through this method) still land
    // here. Route through the WithOptions form with a default Options so the
    // gradient + border paint stays the single source of truth.
    drawPopupMenuBackgroundWithOptions(g, width, height, juce::PopupMenu::Options());
}

void OTTOLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                                          int width, int height,
                                                          const juce::PopupMenu::Options& options) {
    const float scale = getScaleFactorFromGraphics(g);
    const auto bounds = HiDPI::snapToPixel(
        juce::Rectangle<int>(0, 0, width, height).toFloat(), scale);

    // DROP-01: radial gradient matching the closed-combo body. Accent is
    // resolved from the popup's target component (the originating ComboBox
    // when launched via getOptionsForComboBoxPopupMenu, which JUCE's
    // LookAndFeel_V4 default wires via withTargetComponent(box)). Falls back
    // to neutral when no target component is supplied (right-click menus,
    // OverflowMenuButton popups outside a PlayerCard).
    const juce::Colour accent = resolveDropdownAccent(options.getTargetComponent());
    paintDropdownGradient(g, bounds, accent, Sizing::kButtonCornerRadius);

    // Subtle border (preserved from legacy paint)
    g.setColour(Colours::borderDefault);
    drawCrispRoundedRectangle(g, bounds, Sizing::kButtonCornerRadius, 1.0f, scale);
}

juce::Colour OTTOLookAndFeel::resolveDropdownAccent(juce::Component* originatingComponent) {
    static const juce::Identifier kAccentId("otto-accent");
    for (juce::Component* c = originatingComponent; c != nullptr; c = c->getParentComponent()) {
        const auto& props = c->getProperties();
        if (props.contains(kAccentId)) {
            const auto value = props[kAccentId].toString();
            if (value.isNotEmpty()) {
                return juce::Colour::fromString(value);
            }
        }
    }
    return Colours::bg2;
}

void OTTOLookAndFeel::paintDropdownGradient(juce::Graphics& g,
                                             juce::Rectangle<float> bounds,
                                             juce::Colour accent,
                                             float cornerRadius) {
    if (bounds.isEmpty()) {
        return;
    }

    // Sprint 35 (2026-05-03): BUG-220's α=0.15 halo with the centre 10% outside
    // the rect was visually indistinguishable from solid bg0 on iPad — operator
    // reported "all dropdown menus look black, the gradient is gone". Pull the
    // halo centre INSIDE the rect (upper-left corner, 12% inset) and bump the
    // accent alpha to 0.40 so the player accent reads as a clear corner glow.
    // Radius scaled to 0.95× the max edge so the halo decays across most of
    // the rect, preserving readable bg0 in the lower-right where menu items
    // sit. Net: visible per-player identity + readable text, without the
    // BUG-216 full-rect wash.
    g.setColour(Colours::bg0);
    g.fillRoundedRectangle(bounds, cornerRadius);

    const float radius = juce::jmax(bounds.getWidth(), bounds.getHeight()) * 0.95f;
    const float cx = bounds.getX() + bounds.getWidth()  * 0.12f;
    const float cy = bounds.getY() + bounds.getHeight() * 0.12f;

    juce::ColourGradient grad(accent.withAlpha(0.40f), cx, cy,
                               Colours::bg0.withAlpha(0.0f), cx + radius, cy + radius,
                               /*isRadial=*/true);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, cornerRadius);
}

void OTTOLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                         const juce::Rectangle<int>& area,
                                         bool isSeparator,
                                         bool isActive,
                                         bool isHighlighted,
                                         bool isTicked,
                                         bool /*hasSubMenu*/,
                                         const juce::String& text,
                                         const juce::String& /*shortcutKeyText*/,
                                         const juce::Drawable* /*icon*/,
                                         const juce::Colour* textColour) {
    const float scale = getScaleFactorFromGraphics(g);
    const auto menuColours = getMenuColors();

    if (isSeparator) {
        // Separator: 1px line centred in the separator block, full-width
        // minus the standard horizontal padding gutter.
        const int hPad = static_cast<int>(Sizing::kMenuHorizontalPadding);
        const auto separatorBounds = area.reduced(hPad, 0).toFloat();
        const float lineHeight = HiDPI::getScaledStrokeWidth(1.0f, scale);
        g.setColour(menuColours.separator);
        g.fillRect(separatorBounds.withHeight(lineHeight)
                       .withY(HiDPI::snapToPixel(separatorBounds.getCentreY(), scale)));
        return;
    }

    // Highlight background: accent gradient anchor — for nested popups, the
    // accent fill keeps the cursor anchor unambiguous. Caller-supplied
    // textColour still wins for semantic overrides (e.g. destructive red).
    const bool highlighted = isHighlighted && isActive;
    if (highlighted) {
        g.setColour(menuColours.accent);
        const float vPad = Sizing::kMenuVerticalPadding * 0.5f;
        g.fillRoundedRectangle(area.reduced(static_cast<int>(Sizing::kMenuHorizontalPadding * 0.3f),
                                            static_cast<int>(vPad)).toFloat(),
                               Sizing::kButtonCornerRadius);
    }

    juce::Colour finalTextColour = menuColours.foreground;
    if (highlighted) {
        finalTextColour = menuColours.highlightText;
    }
    if (textColour != nullptr) {
        finalTextColour = *textColour;
    }
    if (!isActive) {
        finalTextColour = menuColours.disabledText;
    }

    g.setColour(finalTextColour);
    g.setFont(getMenuItemFont());

    // Reserve tick column on the right so labels and tick marks never overlap.
    const int tickCol = static_cast<int>(Sizing::kMenuTickColumnWidth);
    const int hPad = static_cast<int>(Sizing::kMenuHorizontalPadding);
    const auto textBounds = area.withTrimmedLeft(hPad).withTrimmedRight(tickCol);
    g.drawText(text, textBounds, juce::Justification::centredLeft);

    if (isTicked) {
        const float tickSize = 10.0f;
        const float tickX = HiDPI::snapToPixel(
            static_cast<float>(area.getRight()) - Sizing::kMenuTickColumnWidth * 0.5f, scale);
        const float tickY = HiDPI::snapToPixel(
            static_cast<float>(area.getCentreY()), scale);

        juce::Path tickPath;
        tickPath.startNewSubPath(tickX - tickSize * 0.5f, tickY);
        tickPath.lineTo(tickX - tickSize * 0.15f, tickY + tickSize * 0.35f);
        tickPath.lineTo(tickX + tickSize * 0.5f, tickY - tickSize * 0.35f);

        g.setColour(highlighted ? menuColours.highlightText : menuColours.accent);
        const float tickStroke = HiDPI::getScaledStrokeWidth(2.0f, scale);
        g.strokePath(tickPath, juce::PathStrokeType(tickStroke));
    }
}

// =============================================================================
// PopupMenu Section Header (Sprint 30 BUG-03)
// =============================================================================
//
// Bold rendering for `juce::PopupMenu::addSectionHeader` items. JUCE's default
// implementation in LookAndFeel_V2 calls `getPopupMenuFont().boldened()`, which
// reduces to `withStyle("Bold")`. Bug 197 — when the embedded Roboto variable
// font is carried explicitly in FontOptions, `withStyle("Bold")` is silently
// dropped, so section headers across the app rendered at regular weight,
// visually indistinguishable from per-item rows.
//
// The fix mirrors `applySectionTitleStyle` / `drawSectionTitle`: build the font
// from the size-form FontOptions (no explicit typeface) so `getTypefaceForFont`
// resolves the typeface during paint and the variable font's weight axis
// honours the Bold style.
//
// UPPERCASE casing is intentionally NOT applied here — kit-picker emits
// `manufacturer.toUpperCase()` so the bold-uppercase convention is scoped to
// that popup; OverflowMenuButton's `Factory` / `User` headers stay in their
// original case (matching JUCE's intended default behaviour, which Bug 197 had
// been silently regressing).

void OTTOLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g,
                                                  const juce::Rectangle<int>& area,
                                                  const juce::String& sectionName) {
    // Spec per continue.md "Typography / Section header": 12pt bold,
    // ALL CAPS, +0.5px letter-spacing, 60% alpha foreground.
    const auto menuColours = getMenuColors();
    g.setFont(getMenuHeaderFont());
    g.setColour(menuColours.sectionHeader);

    juce::AttributedString attr;
    attr.setText(sectionName.toUpperCase());
    attr.setFont(getMenuHeaderFont());
    attr.setColour(menuColours.sectionHeader);
    attr.setJustification(juce::Justification::bottomLeft);

    const int hPad = static_cast<int>(Sizing::kMenuHorizontalPadding);
    juce::Rectangle<int> textRect(area.getX() + hPad,
                                   area.getY(),
                                   area.getWidth() - hPad * 2,
                                   area.getHeight());
    attr.draw(g, textRect.toFloat());
}

// =============================================================================
// PopupMenu Sizing
// =============================================================================

void OTTOLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text,
                                                  bool isSeparator,
                                                  int /*standardMenuItemHeight*/,
                                                  int& idealWidth,
                                                  int& idealHeight) {
    if (isSeparator) {
        idealWidth = static_cast<int>(Sizing::kMenuMinWidth);
        idealHeight = static_cast<int>(Sizing::kMenuSeparatorBlockHeight);
        return;
    }

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(getMenuItemFont(), text, 0.0f, 0.0f);
    const float textWidth = glyphs.getBoundingBox(0, -1, true).getWidth();

    // Reserve horizontal padding (both sides) plus the tick column on the
    // right so a ticked row never overlaps its label.
    idealWidth = static_cast<int>(textWidth)
                 + static_cast<int>(Sizing::kMenuTextHorizontalPadding)
                 + static_cast<int>(Sizing::kMenuTickColumnWidth);

    idealHeight = static_cast<int>(Sizing::kMenuRowHeight);
}

juce::PopupMenu::Options OTTOLookAndFeel::getOptionsForComboBoxPopupMenu(
    juce::ComboBox& box,
    juce::Label& currentValueLabel) {
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, currentValueLabel);

    // Menu row height
    int itemHeight = static_cast<int>(Sizing::kMenuRowHeight);

    // Single column layout enables vertical finger scrolling
    // JUCE handles native momentum scrolling when items exceed screen space
    options = options.withMaximumNumColumns(1)
                     .withMinimumNumColumns(1)
                     .withMinimumWidth(box.getWidth())
                     .withStandardItemHeight(itemHeight);

    return options;
}

// =============================================================================
// Popup Menu Scroll Arrows — suppressed on every platform
// =============================================================================
// iOS-first menu unification: every user-facing popup is a
// TouchScrollableMenuPopup whose ListBox handles overflow via finger /
// scroll-wheel drag. The "mouse mimics finger" rule means no chevron
// affordance on desktop either. Any residual juce::PopupMenu rendering
// (purely internal JUCE paths) inherits this same no-op so there's a
// single visual contract for popup overflow across surfaces.

void OTTOLookAndFeel::drawPopupMenuUpDownArrowWithOptions(juce::Graphics& g,
                                                           int width, int height,
                                                           bool isScrollUpArrow,
                                                           const juce::PopupMenu::Options& options) {
    juce::ignoreUnused(g, width, height, isScrollUpArrow, options);
}

void OTTOLookAndFeel::drawPopupMenuUpDownArrow(juce::Graphics& g,
                                                int width, int height,
                                                bool isScrollUpArrow) {
    // Legacy path — delegate to the Options version so both JUCE code paths
    // render an identical affordance.
    const juce::PopupMenu::Options defaultOptions;
    drawPopupMenuUpDownArrowWithOptions(g, width, height, isScrollUpArrow, defaultOptions);
}

// =============================================================================
// Font Accessors (ensure Roboto is used everywhere)
// =============================================================================

juce::Font OTTOLookAndFeel::getPopupMenuFont() {
    // Roboto Condensed at the menu item size — single source of truth for
    // every JUCE-native popup font query.
    return getMenuItemFont();
}

juce::Font OTTOLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    juce::ignoreUnused(box);
    // Closed-combo label uses the same Roboto Condensed face as menu items
    // so the trigger and the popup share one typographic identity. The
    // label IS the click target (no chevron) per user direction.
    return getMenuItemFont();
}

void OTTOLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    // Sprint 30 BUG-05: claim the full combo width (minus a 4 px horizontal
    // safety margin) for the value-display label rather than honoring JUCE
    // V4's default 30 px right-side arrow reservation. OTTO's drawComboBox
    // paints no arrow chrome, so the reservation is wasted space that lets
    // long stereo-pair labels such as `63-64` ellipsize at the
    // CompactFaderStrip::kStripWidth = 60 footer width. Centring matches
    // the open-popup item paint convention so the closed-combo readout and
    // the dropdown rows display the same string in the same place.
    constexpr int horizontalMargin = 4;
    label.setBounds(horizontalMargin, 1,
                    juce::jmax(0, box.getWidth() - 2 * horizontalMargin),
                    box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centred);
    // Bug 180 precedent (renderer-side variant): allow addFittedText to
    // scale the glyph run down to 0.5x before the JUCE fitted-text path
    // falls back to an ellipsis. At kStripWidth=60 the longest pair label
    // `63-64` rendered in Roboto kFontSizeBody (15.0f) auto-fits well above
    // 0.5 — the floor exists so future strip-width regressions degrade by
    // shrinking text, never by ellipsizing it.
    label.setMinimumHorizontalScale(0.5f);
}

juce::Font OTTOLookAndFeel::getTextButtonFont(juce::TextButton& /*button*/, int buttonHeight) {
    // Scale font to 50% of button height, capped at body size
    float fontSize = juce::jmin(static_cast<float>(buttonHeight) * 0.5f, Sizing::kFontSizeBody);
    fontSize = juce::jmax(fontSize, Sizing::kMinMobileFontSize);
    return juce::Font(juce::FontOptions(getRobotoTypeface()).withHeight(fontSize));
}

juce::Font OTTOLookAndFeel::getAlertWindowMessageFont() {
    return juce::Font(juce::FontOptions(getRobotoTypeface()).withHeight(Sizing::kFontSizeBody));
}

juce::Font OTTOLookAndFeel::getAlertWindowTitleFont() {
    return juce::Font(juce::FontOptions(getRobotoTypeface()).withHeight(Sizing::kFontSizeTitle));
}

// =============================================================================
// Focus Indication
// =============================================================================

void OTTOLookAndFeel::drawFocusOutline(juce::Graphics& g,
                                        int width, int height,
                                        const juce::Path& /*path*/) {
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    const float scale = getScaleFactorFromGraphics(g);

    g.setColour(Colours::focusOutline);
    drawCrispRoundedRectangle(g, bounds.reduced(1.0f),
                               Sizing::kButtonCornerRadius, 2.0f, scale);
}

// =============================================================================
// HiDPI Rendering Helpers
// =============================================================================

float OTTOLookAndFeel::getScaleFactorFromGraphics(const juce::Graphics& g) {
    // Get scale from the graphics context's internal context
    // The physical pixel scale is encoded in the context's transform
    const auto& context = g.getInternalContext();
    return static_cast<float>(context.getPhysicalPixelScaleFactor());
}

void OTTOLookAndFeel::drawCrispLine(juce::Graphics& g,
                                     float x1, float y1,
                                     float x2, float y2,
                                     float scaleFactor) {
    // Snap coordinates to pixel boundaries for crisp 1px lines
    const float offset = HiDPI::getStrokeOffset(scaleFactor);
    const float strokeWidth = HiDPI::getScaledStrokeWidth(1.0f, scaleFactor);

    // For horizontal lines, snap Y; for vertical lines, snap X
    const bool isHorizontal = std::abs(y2 - y1) < 0.1f;
    const bool isVertical = std::abs(x2 - x1) < 0.1f;

    float snappedX1 = x1;
    float snappedY1 = y1;
    float snappedX2 = x2;
    float snappedY2 = y2;

    if (isHorizontal) {
        snappedY1 = HiDPI::snapToPixel(y1, scaleFactor) + offset;
        snappedY2 = snappedY1;
    } else if (isVertical) {
        snappedX1 = HiDPI::snapToPixel(x1, scaleFactor) + offset;
        snappedX2 = snappedX1;
    }

    g.drawLine(snappedX1, snappedY1, snappedX2, snappedY2, strokeWidth);
}

void OTTOLookAndFeel::drawCrispRoundedRectangle(juce::Graphics& g,
                                                 juce::Rectangle<float> bounds,
                                                 float cornerRadius,
                                                 float lineWidth,
                                                 float scaleFactor) {
    // Snap bounds to pixel boundaries
    const auto snappedBounds = HiDPI::snapToPixel(bounds, scaleFactor);

    // Scale stroke width appropriately
    const float scaledStroke = HiDPI::getScaledStrokeWidth(lineWidth, scaleFactor);

    // Inset by half stroke width to keep stroke within bounds
    const auto insetBounds = snappedBounds.reduced(scaledStroke * 0.5f);

    g.drawRoundedRectangle(insetBounds, cornerRadius, scaledStroke);
}

void OTTOLookAndFeel::drawCrispEllipse(juce::Graphics& g,
                                        juce::Rectangle<float> bounds,
                                        float lineWidth,
                                        float scaleFactor) {
    // Snap bounds to pixel boundaries
    const auto snappedBounds = HiDPI::snapToPixel(bounds, scaleFactor);

    // Scale stroke width appropriately
    const float scaledStroke = HiDPI::getScaledStrokeWidth(lineWidth, scaleFactor);

    // Inset by half stroke width to keep stroke within bounds
    const auto insetBounds = snappedBounds.reduced(scaledStroke * 0.5f);

    g.drawEllipse(insetBounds, scaledStroke);
}

} // namespace otto
