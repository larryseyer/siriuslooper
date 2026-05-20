#pragma once

/**
 * @file OTTOLookAndFeel.h
 * @brief Custom LookAndFeel for OTTO plugin
 *
 * Inherits from juce::LookAndFeel_V4 and provides:
 * - Dark theme with proper contrast ratios (WCAG AA)
 * - Touch-friendly sizing (minimum 44pt touch targets)
 * - Vector-based rendering (no raster images)
 * - Responsive scaling support
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include "OTTOColours.h"

namespace otto {

// =============================================================================
// HiDPI Utilities
// =============================================================================

/**
 * @brief High DPI / Retina display utilities
 *
 * Provides scale factor detection and pixel-perfect rendering helpers
 * for crisp vector graphics at all display densities.
 *
 * Usage:
 *   // Get scale factor for a component
 *   float scale = HiDPI::getScaleFactor(component);
 *
 *   // Align coordinates to physical pixels for crisp lines
 *   float x = HiDPI::snapToPixel(rawX, scale);
 *
 *   // Get stroke width that appears consistent across DPIs
 *   float stroke = HiDPI::getScaledStrokeWidth(1.0f, scale);
 */
namespace HiDPI {

/**
 * @brief Get the display scale factor for a component
 * @param component The component to query (uses peer if available)
 * @return Scale factor (1.0 = standard, 2.0 = Retina/2x, etc.)
 *
 * On macOS Retina displays, returns 2.0.
 * On Windows with 150% scaling, returns 1.5.
 * Falls back to main display scale if component has no peer.
 */
inline float getScaleFactor(const juce::Component* component) {
    if (component != nullptr) {
        if (auto* peer = component->getPeer()) {
            return static_cast<float>(peer->getPlatformScaleFactor());
        }
        // Try to get from parent window
        if (auto* topLevel = component->getTopLevelComponent()) {
            if (auto* topPeer = topLevel->getPeer()) {
                return static_cast<float>(topPeer->getPlatformScaleFactor());
            }
        }
    }
    // Fall back to main display
    const auto* display = juce::Desktop::getInstance()
                              .getDisplays()
                              .getPrimaryDisplay();
    return display != nullptr ? static_cast<float>(display->scale) : 1.0f;
}

/**
 * @brief Get scale factor for the main display
 * @return Scale factor of primary display
 */
inline float getMainDisplayScaleFactor() {
    const auto* display = juce::Desktop::getInstance()
                              .getDisplays()
                              .getPrimaryDisplay();
    return display != nullptr ? static_cast<float>(display->scale) : 1.0f;
}

/**
 * @brief Snap a coordinate to the nearest physical pixel boundary
 * @param value The coordinate value in points
 * @param scaleFactor Display scale factor
 * @return Value snapped to physical pixel boundary
 *
 * For crisp 1px lines, draw at snapped coordinates + 0.5/scale offset.
 * This ensures strokes fall exactly on pixel boundaries.
 */
inline float snapToPixel(float value, float scaleFactor) {
    const float pixelSize = 1.0f / scaleFactor;
    return std::round(value * scaleFactor) * pixelSize;
}

/**
 * @brief Snap a rectangle to physical pixel boundaries
 * @param rect The rectangle in points
 * @param scaleFactor Display scale factor
 * @return Rectangle with coordinates snapped to pixels
 */
inline juce::Rectangle<float> snapToPixel(const juce::Rectangle<float>& rect,
                                           float scaleFactor) {
    return {
        snapToPixel(rect.getX(), scaleFactor),
        snapToPixel(rect.getY(), scaleFactor),
        snapToPixel(rect.getWidth(), scaleFactor),
        snapToPixel(rect.getHeight(), scaleFactor)
    };
}

/**
 * @brief Get optimal stroke offset for crisp 1px lines
 * @param scaleFactor Display scale factor
 * @return Offset to add to snapped coordinates
 *
 * For a 1pt stroke to appear crisp, the stroke center should
 * fall exactly between two physical pixels. This offset achieves that.
 */
inline float getStrokeOffset(float scaleFactor) {
    return 0.5f / scaleFactor;
}

/**
 * @brief Get a stroke width that renders crisply at the given scale
 * @param baseWidth Desired stroke width in points
 * @param scaleFactor Display scale factor
 * @return Adjusted stroke width for crisp rendering
 *
 * Ensures strokes are at least 1 physical pixel wide and
 * snap to whole pixel boundaries when possible.
 */
inline float getScaledStrokeWidth(float baseWidth, float scaleFactor) {
    // Minimum stroke is 1 physical pixel
    const float minStroke = 1.0f / scaleFactor;
    // Round to nearest physical pixel for crispness
    const float scaledWidth = std::round(baseWidth * scaleFactor) / scaleFactor;
    return std::max(minStroke, scaledWidth);
}

/**
 * @brief Check if running on a HiDPI display
 * @param scaleFactor Scale factor to check
 * @return True if scale > 1.0 (HiDPI/Retina)
 */
inline bool isHiDPI(float scaleFactor) {
    return scaleFactor > 1.0f;
}

/**
 * @brief Convert points to physical pixels
 * @param points Value in points
 * @param scaleFactor Display scale factor
 * @return Value in physical pixels
 */
inline float pointsToPixels(float points, float scaleFactor) {
    return points * scaleFactor;
}

/**
 * @brief Convert physical pixels to points
 * @param pixels Value in physical pixels
 * @param scaleFactor Display scale factor
 * @return Value in points
 */
inline float pixelsToPoints(float pixels, float scaleFactor) {
    return pixels / scaleFactor;
}

} // namespace HiDPI

// =============================================================================
// Platform Constants
// =============================================================================

/**
 * @brief Platform-specific constants for cross-platform parity
 *
 * Touch target sizing is UNIFIED across all platforms (44pt minimum).
 * Only touch device detection differs between platforms.
 */
namespace Platform {

/// Minimum touch target size (unified across all platforms per Apple HIG)
constexpr float kTouchTargetMin = 44.0f;

/// Whether the current platform is a touch device
#if JUCE_IOS
constexpr bool kIsTouchDevice = true;
#else
constexpr bool kIsTouchDevice = false;
#endif

} // namespace Platform

// =============================================================================
// Sizing Constants
// =============================================================================

/**
 * @brief Touch-friendly sizing constants
 *
 * All interactive elements should meet minimum touch target sizes.
 * These are base values - actual rendering may scale based on display DPI.
 */
namespace Sizing {

// =============================================================================
// Touch Targets (Apple HIG / Material Design guideline)
// =============================================================================

constexpr float kMinTouchTarget = 44.0f;  // Minimum interactive element size
constexpr float kComfortableTouchTarget = 48.0f;  // Comfortable touch target

// =============================================================================
// Button Sizing
// =============================================================================

constexpr float kButtonHeightStandard = 44.0f;  // Default button height
constexpr float kButtonHeightSmall = 36.0f;     // Compact/secondary actions
constexpr float kButtonHeightLarge = 56.0f;     // Primary CTAs
constexpr float kButtonCornerRadius = 8.0f;     // Rounded corners
constexpr float kPhraseTabCornerRadius = 6.0f;  // PhraseTab inner-button radius (Players + Patterns + DesktopLayout rows)
constexpr float kButtonBorderWidth = 1.0f;      // Border thickness
constexpr float kButtonPadding = 16.0f;         // Horizontal padding
constexpr float kButtonMinWidth = 80.0f;        // Minimum width with text

// =============================================================================
// Slider Sizing
// =============================================================================

constexpr float kSliderTrackWidth = 2.0f;       // Track thickness
constexpr float kSliderTrackRadius = 1.0f;      // Track corner radius (half of width)
constexpr float kSliderThumbSize = 24.0f;       // Visual thumb diameter
constexpr float kSliderThumbHitArea = 44.0f;    // Touch target (expanded)

// =============================================================================
// Knob (Rotary) Sizing
// =============================================================================

constexpr float kRotaryKnobSize = 56.0f;        // Knob visual diameter
constexpr float kRotaryKnobHitArea = 64.0f;     // Touch target
constexpr float kRotaryArcThickness = 4.0f;     // Value arc stroke width
constexpr float kRotaryIndicatorWidth = 3.0f;   // Position indicator width
constexpr float kRotaryArcAngle = 270.0f;       // Arc span in degrees

// =============================================================================
// Typography (points)
// =============================================================================

constexpr float kFontSizeCaption = 11.0f;   // xs: Captions, timestamps
constexpr float kFontSizeBody = 15.0f;      // base: Body text, values
constexpr float kFontSizeHeading = 18.0f;   // lg: Section headers
constexpr float kFontSizeTitle = 24.0f;     // xl: Panel titles
constexpr float kFontSizeLarge = 32.0f;     // 2xl: Tempo display
constexpr float kMinMobileFontSize = 13.0f; // Minimum for readability

// =============================================================================
// Spacing (based on 4px grid)
// =============================================================================

constexpr float kSpacingSmall = 4.0f;       // space-1: Tight gaps
constexpr float kSpacingMedium = 8.0f;      // space-2: Standard inline gaps
constexpr float kSpacingLarge = 16.0f;      // space-4: Between elements
constexpr float kSpacingSection = 24.0f;    // space-5: Section padding

// BUG-09 (Sprint 31): vertical gap between the SpectrumDisplay's bottom edge
// and the BeatProgressBar's top edge inside TransportBar. Pinned as a named
// constant per Bug 192 (centralise sizing constants) so all three layout
// helpers (layoutCompact / layoutMedium / layoutFull) share one value and a
// future tuning pass touches a single line.
constexpr float kSpectrumBeatBarGap = 4.0f;

// =============================================================================
// Mixer Strip Sizing
// =============================================================================

constexpr float kStripWidth = 60.0f;        // Mixer strip width
constexpr float kStripPadding = 4.0f;       // Padding inside strip
constexpr float kStripCornerRadius = 4.0f;  // Strip corner radius
constexpr float kKnobRowHeight = 52.0f;     // Height for knob rows
constexpr float kKnobSize = 36.0f;          // Standard knob diameter
constexpr float kStripNameHeight = 24.0f;   // Height for strip name label

// =============================================================================
// Tab and Menu Sizing
// =============================================================================
//
// Menu design tokens — single source of truth for every dropdown / popup /
// submenu / context menu / ComboBox popup in OTTO. Both the JUCE
// LookAndFeel menu overrides AND the touch popup component
// (TouchScrollableMenuPopup) read from these constants. Spec values per
// continue.md "Global Menu Design Tokens" section. iOS-first: the touch
// popup is the surviving menu surface across all platforms; the JUCE
// LookAndFeel overrides remain only for any residual JUCE-internal popup
// paths (e.g. native ComboBox internals) and read the same tokens.

constexpr float kTabHeight = 44.0f;                    // Tab bar tab height
constexpr float kMenuRowHeight = 28.0f;                // Item height (28px desktop pro-audio standard)
constexpr float kMenuSectionHeaderHeight = 24.0f;      // Section header row height
constexpr float kMenuMinWidth = 180.0f;                // Minimum popup width
constexpr float kMenuMaxWidth = 360.0f;                // Maximum popup width (truncate beyond with ellipsis)
constexpr float kMenuHorizontalPadding = 14.0f;        // Per-side horizontal item padding
constexpr float kMenuVerticalPadding = 6.0f;           // Per-side vertical item padding (28 = 6+14pt baseline+6)
constexpr float kMenuTextHorizontalPadding = 28.0f;    // Total left+right text padding (= 2 * kMenuHorizontalPadding)
constexpr float kMenuSeparatorBlockHeight = 9.0f;      // Separator block (4px space + 1px line + 4px space)
constexpr float kMenuTickColumnWidth = 22.0f;          // Reserved column for tick / checkmark
constexpr float kMenuArrowColumnWidth = 18.0f;         // Reserved column for accordion expand arrow
constexpr float kMenuIconColumnWidth = 20.0f;          // Reserved column for item icon (when present)
constexpr float kMenuIconTextGap = 8.0f;               // Gap between icon and text
constexpr float kMenuItemFontSize = 14.0f;             // Roboto Condensed regular weight
constexpr float kMenuHeaderFontSize = 12.0f;           // Roboto Condensed bold, ALL CAPS, +0.5px tracking
constexpr float kMenuShortcutFontSize = 13.0f;         // Roboto Condensed regular, 50% alpha
constexpr float kMenuCornerGlowAlpha = 0.40f;          // OPERATOR-LOCKED corner glow strength (paintDropdownGradient)

// =============================================================================
// Three-Dot ("Kebab" / Horizontal-Ellipsis) Menu Affordance
// =============================================================================
//
// Canonical glyph geometry for every interactive 3-dot menu trigger in the
// app. Sites: OverflowMenuButton (vertical kebab "⋮", opens preset/settings
// menu), PatternNavigator::ActionButton (horizontal ellipsis "⋯", opens
// pattern action menu), KitPicker::ActionButton (horizontal ellipsis "⋯",
// opens kit action menu). All three sites paint via g.fillEllipse — the
// SAME pixel-radius and centre-to-centre spacing renders consistently
// regardless of axis (vertical vs horizontal). Sprint 29 BUG-03 picked the
// smaller (Patterns-row) reference as the canonical size; the prior
// OverflowMenuButton bespoke path-build (radius 2.5, spacing 8.0 in a 44x44
// DrawableButton::ImageFitted canvas) rendered visibly larger on iPhone
// portrait and was the primary user-flagged inconsistency.
//
// Per Bug 192, sibling components with identical visual primitives share
// only the SIZING CONSTANTS, not the paint code — each site keeps its own
// paint context (axis, colours, hover state, background fill).

constexpr float kThreeDotAffordanceDotRadius = 1.6f;    // Per-dot pixel radius
constexpr float kThreeDotAffordanceDotSpacing = 5.0f;   // Centre-to-centre spacing

// =============================================================================
// Slider Popup Sizing (iOS)
// =============================================================================

constexpr float kSliderPopupThumbDiameter = 48.0f;  // Touch slider thumb
constexpr float kSliderPopupTrackWidth = 80.0f;     // Touch slider track width

// =============================================================================
// Snapshot Button Sizing
// =============================================================================

constexpr float kSnapshotButtonHeight = 36.0f;      // Snapshot button height
constexpr float kSnapshotButtonMinWidth = 44.0f;    // Minimum snapshot button width

// =============================================================================
// Player Card Sizing
// =============================================================================

constexpr float kPlayerBadgeSize = 56.0f;           // Player badge diameter

// Disabled-state alpha for the player badge (Sprint 31 BUG-10). The badge is
// the always-live re-enable affordance per Bug 172, so it must read as the
// player's hue at reduced intensity rather than generic grey. Per Bug 192 the
// constant lives here so both the colour helper and any future per-site paint
// site reference one source of truth.
constexpr float kPlayerDisabledAlpha = 0.5f;

// =============================================================================
// Mixer Effect-Panel Header Switch Column
// =============================================================================
//
// Canonical width clamp for the LEFT switch column shared by every Mixer
// effect-panel header row (CompressorPanel, DelayPanel, ReverbPanel). Each
// panel stacks its on/off + auxiliary toggle vertically in this left column,
// with the right side filled by panel-specific content (gain-reduction
// meter on Compressor, sync-rate buttons on Delay, IR drop-down on Reverb).
//
// Min width fits "Compressor Enabled" / "Delay Enabled" / "Reverb Enabled"
// without text clip on iPhone-narrow widths; max width caps the column on
// wide tablet panels so the right-side content still dominates the row.
// Per Bug 192 (sibling components share the SIZING CONSTANTS, not the paint
// code), each panel keeps its own resized() implementation but reads from
// these shared constants — future bumps land in one place.

constexpr int kEffectPanelSwitchColumnMinWidth = 140;
constexpr int kEffectPanelSwitchColumnMaxWidth = 180;

// =============================================================================
// Section Title Convention (BUG-08 — Global ALL CAPS BOLD CENTERED rule)
// =============================================================================
//
// Audio-pro industry-standard title treatment for every panel / window /
// section header in OTTO (FabFilter / UAD / Waves convention). Apply via
// OTTOLookAndFeel::applySectionTitleStyle(juce::Label&, text) for
// Label-backed titles, or OTTOLookAndFeel::drawSectionTitle(g, bounds, text)
// for direct-paint sites. Per Bug 192, the constants here are the single
// source of truth — per-site paint code reads them, never inlines a literal.
// Per Bug 88, drawLabel defensively enforces centred justification for any
// label whose ComponentID contains "section-header" / "sectionHeader" /
// "heading", so a future site that forgets the helper still renders correctly.
// Per Bug 39, ALL CAPS is a DISPLAY transform applied at the helper boundary;
// stable identifiers / enum values stay in their authored case.

constexpr float kSectionTitleFontSize = kFontSizeHeading;  // 18 pt — heading tier
constexpr int   kSectionTitleRowHeight = 28;               // Canonical title-row height

// =============================================================================
// Border Radius Constants
// =============================================================================

constexpr float kBorderRadiusSmall = 3.0f;          // Small rounded corners
constexpr float kBorderRadiusMedium = 4.0f;         // Medium rounded corners
constexpr float kBorderRadiusLarge = 6.0f;          // Large rounded corners

} // namespace Sizing

// =============================================================================
// OTTOLookAndFeel
// =============================================================================

/**
 * @brief Custom LookAndFeel for OTTO plugin
 *
 * Provides consistent styling across all JUCE components with:
 * - Dark theme optimized for studio use
 * - Touch-friendly sizing for iOS/tablet use
 * - Vector graphics rendering for crisp display at any scale
 *
 * Usage:
 *   // In your editor constructor:
 *   setLookAndFeel(&lookAndFeel_);
 *
 *   // In destructor:
 *   setLookAndFeel(nullptr);
 */
class OTTOLookAndFeel : public juce::LookAndFeel_V4 {
public:
    OTTOLookAndFeel();
    ~OTTOLookAndFeel() override;

    // =========================================================================
    // Typography
    // =========================================================================

    /**
     * @brief Get the Roboto typeface for custom font creation
     * @return Shared typeface pointer (nullptr if loading failed)
     *
     * Use this when creating fonts outside of OTTOLookAndFeel.
     * Example: juce::Font(OTTOLookAndFeel::getRobotoTypeface()).withHeight(13.0f)
     */
    static juce::Typeface::Ptr getRobotoTypeface();

    /**
     * @brief Get the Orbitron typeface for display text
     * @return Shared typeface pointer (nullptr if loading failed)
     *
     * Orbitron is a geometric/futuristic font used for splash screen tagline.
     */
    static juce::Typeface::Ptr getOrbitronTypeface();

    /**
     * @brief Get the Roboto Condensed typeface for menu text
     * @return Shared typeface pointer (nullptr if loading failed)
     *
     * Roboto Condensed is the OTTO menu typeface. Used by every dropdown,
     * popup, submenu, ComboBox popup, and context menu in the app. The
     * condensed letterforms hit a denser visual rhythm at 14pt than the
     * default Roboto, matching desktop pro-audio menu conventions.
     */
    static juce::Typeface::Ptr getRobotoCondensedTypeface();

    // =========================================================================
    // Menu Design Tokens (single source of truth)
    // =========================================================================
    //
    // Both the JUCE LookAndFeel menu overrides AND the touch popup
    // (TouchScrollableMenuPopup) read fonts/colors via these accessors. No
    // caller may construct juce::Font for menu text directly — always go
    // through getMenuItemFont() / getMenuHeaderFont() / getMenuShortcutFont().
    // No caller may pick menu colors via raw 0xFF... literals — always go
    // through getMenuColors().

    /**
     * @brief Roboto Condensed at 14pt, regular weight. Use for menu items.
     */
    static juce::Font getMenuItemFont();

    /**
     * @brief Roboto Condensed at 12pt, bold weight. Use for section headers.
     *        Caller is responsible for upper-casing the displayed text and
     *        applying letter-spacing (+0.5 px tracking) at draw time.
     */
    static juce::Font getMenuHeaderFont();

    /**
     * @brief Roboto Condensed at 13pt, regular weight. Use for shortcut /
     *        accelerator text painted alongside menu items at 50% alpha.
     */
    static juce::Font getMenuShortcutFont();

    /**
     * @brief Centralized menu color palette. Returned by value so callers
     *        cannot cache by reference and accidentally observe stale state
     *        if the LookAndFeel ever swaps its theme. Spec per continue.md
     *        "Global Menu Design Tokens / Color Palette".
     */
    struct MenuColors {
        juce::Colour background;        // 0xFF1A1A1A — opaque popup body
        juce::Colour foreground;        // 0xFFEEEEEE — item text
        juce::Colour highlightText;     // 0xFFFFFFFF — text over accent gradient
        juce::Colour disabledText;      // 0x66EEEEEE — 40% alpha foreground
        juce::Colour separator;         // 0x33FFFFFF — 20% alpha white, 1px line
        juce::Colour sectionHeader;     // 0x99FFFFFF — 60% alpha white
        juce::Colour submenuArrow;      // 0xB3EEEEEE — 70% alpha foreground
        juce::Colour shortcutText;      // 0x80EEEEEE — 50% alpha foreground
        juce::Colour accent;            // OTTO accent color (tick, focus, gradient)
    };
    static MenuColors getMenuColors();

    // =========================================================================
    // Player Card v6 Elegant Typography (PCv6-01)
    // =========================================================================
    //
    // The Player Card v6 elegant design (mockup
    // `.superpowers/brainstorm/1633-1777421708/content/04-player-card-elegant.html`
    // line 7) specifies a two-family font stack:
    //   - Bricolage Grotesque — sans, weights 400/500/600/700/800
    //   - JetBrains Mono     — monospace, weights 400/500/600
    //
    // Both ship as 8 static-weight ttf files in OTTOPluginAssets +
    // OTTO_iOS_Assets binary-data targets (PCv6-01 CMake change). Loading
    // happens once on first access via `loadCustomFonts()` (called from
    // `setupFonts()` and idempotent — repeated calls are no-ops).
    //
    // Weight parameter accepts the "CSS weight" integers (400/500/600/700/800).
    // Out-of-range values clamp to the nearest available weight; values that
    // fail typeface load fall back to Roboto (sans) or system monospace.

    /**
     * @brief Get a Bricolage Grotesque typeface at a given CSS weight.
     * @param weight CSS-style weight: 400 (Regular), 500 (Medium),
     *               600 (SemiBold), 700 (Bold), 800 (ExtraBold).
     *               Out-of-range values clamp to the nearest available weight.
     * @return Shared typeface pointer (Roboto fallback if loading failed).
     */
    static juce::Typeface::Ptr getBricolageTypeface(int weight);

    /**
     * @brief Get a Bricolage Grotesque juce::Font at a given weight + height.
     * @param weight  CSS weight (400/500/600/700/800), clamped to nearest.
     * @param heightInPoints Optional font height; defaults to body size.
     * @return juce::Font built on the resolved Bricolage typeface.
     */
    static juce::Font getBricolage(int weight,
                                    float heightInPoints = Sizing::kFontSizeBody);

    /**
     * @brief Get a JetBrains Mono typeface at a given CSS weight.
     * @param weight CSS-style weight: 400 (Regular), 500 (Medium),
     *               600 (SemiBold). Out-of-range values clamp.
     * @return Shared typeface pointer (system monospace fallback if failed).
     */
    static juce::Typeface::Ptr getJetBrainsMonoTypeface(int weight);

    /**
     * @brief Get a JetBrains Mono juce::Font at a given weight + height.
     * @param weight CSS weight (400/500/600), clamped to nearest available.
     * @param heightInPoints Optional font height; defaults to body size.
     * @return juce::Font built on the resolved JetBrains Mono typeface.
     */
    static juce::Font getJetBrainsMono(int weight,
                                        float heightInPoints = Sizing::kFontSizeBody);

    /**
     * @brief Override to provide Roboto typeface for ALL font requests
     *
     * This ensures every font in the application uses Roboto regardless
     * of how it was created. JUCE calls this method whenever a font
     * needs a typeface.
     */
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    /**
     * @brief Get the default font for body text
     */
    juce::Font getDefaultFont() const;

    /**
     * @brief Get font for headings
     */
    juce::Font getHeadingFont() const;

    /**
     * @brief Get font for captions/labels
     */
    juce::Font getCaptionFont() const;

    /**
     * @brief Get font for titles
     */
    juce::Font getTitleFont() const;

    /**
     * @brief Get large display font (e.g., BPM display)
     */
    juce::Font getLargeFont() const;

    // =========================================================================
    // Scaled Typography (for responsive sizing)
    // =========================================================================

    /**
     * @brief Get scaled font based on component height
     * @param height Available height for text
     * @param style Font style: "caption", "body", "heading", "title", "large"
     * @return Font sized appropriately for the given height
     *
     * Uses 70% of height as max font size, clamped to hierarchy sizes.
     * Ensures minimum 12pt for mobile readability.
     */
    juce::Font getScaledFont(float height, const juce::String& style) const;

    /**
     * @brief Get font scaled to fit within bounds
     * @param bounds Rectangle the text must fit within
     * @param text The text to measure
     * @param maxFontSize Maximum font size to use
     * @return Font sized to fit the text within bounds
     */
    juce::Font getFontToFitBounds(const juce::Rectangle<float>& bounds,
                                   const juce::String& text,
                                   float maxFontSize) const;

    // =========================================================================
    // Section Title Helpers (BUG-08 — Global ALL CAPS BOLD CENTERED rule)
    // =========================================================================
    //
    // Single source-of-truth for the audio-pro industry-standard title
    // convention (FabFilter / UAD / Waves) — every titled control group reads
    // its title as a centered, bolded, all-caps banner above the grouped
    // controls. Two helper forms cover the two paint paths used in OTTO:
    //
    //   (1) applySectionTitleStyle(juce::Label&, text) — for Label-backed
    //       titles (DelayPanel SYNC MODE, ReverbPanel IMPULSE RESPONSE,
    //       SynthParamPanel KICK / SNARE / HI-HAT, etc.)
    //   (2) drawSectionTitle(Graphics&, bounds, text) — for direct paint
    //       sites where a Label component would be overkill (MasterFXPanel,
    //       LimiterPanel, EffectsPanel header bars).
    //
    // Per Bug 192, the casing + weight + alignment rule is centralised here;
    // per-site paint code does NOT encode the rule. Per Bug 88, drawLabel
    // defensively enforces centred justification at draw time for any label
    // whose ComponentID contains "section-header" / "sectionHeader" — so a
    // future site that forgets the helper still renders correctly. Per Bug
    // 39, ALL CAPS is a DISPLAY transform applied at the helper boundary;
    // stable identifiers / enum values stay in their authored case.

    /**
     * @brief Apply ALL CAPS BOLD CENTERED section-title style to a Label.
     * @param label  Label to style with the canonical section-title look.
     * @param text   Text to set on the label (uppercased before assignment).
     *               Pass empty string to leave existing label text untouched
     *               (style still applied).
     *
     * Sets four properties at once:
     *   - Text (uppercased) — Bug 39 display-transform boundary.
     *   - Font (Bold, kSectionTitleFontSize tier).
     *   - JustificationType (centred).
     *   - ComponentID ("section-header") — drawLabel enforces centring
     *     defensively for any label with this ID.
     */
    static void applySectionTitleStyle(juce::Label& label,
                                        const juce::String& text = {});

    /**
     * @brief Paint a section title directly onto a Graphics context.
     * @param g       Graphics context. Colour is taken from the current
     *                setColour() state so the call site retains colour
     *                cascade control (Bug 88 weakest-strongest cascade).
     * @param bounds  Rectangle to paint within. Title is centred horizontally
     *                AND vertically within bounds.
     * @param text    Text to paint (uppercased before drawing).
     */
    static void drawSectionTitle(juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  const juce::String& text);

    // =========================================================================
    // Sizing Helpers
    // =========================================================================

    /**
     * @brief Get minimum touch target size (44pt scaled)
     * @param scaleFactor Display scale factor (1.0 = standard, 2.0 = Retina)
     */
    static float getMinTouchTarget(float scaleFactor = 1.0f);

    /**
     * @brief Check if a component meets minimum touch target size
     */
    static bool meetsTouchTarget(const juce::Component& component,
                                  float scaleFactor = 1.0f);

    // =========================================================================
    // Button Drawing
    // =========================================================================
    //
    // Button states rendered:
    // - Normal: Subtle gradient background
    // - Hover: Brighter gradient
    // - Pressed: Inverted gradient (pushed-in effect)
    // - Disabled: 50% alpha
    // - Toggle On: Uses buttonFace color with brighter border
    // - Toggle Off: Uses background color
    //
    // All buttons use rounded corners and gradient fills for depth.
    // Minimum recommended size: 44x44 points for touch accessibility.

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawTickBox(juce::Graphics& g,
                     juce::Component& component,
                     float x, float y, float w, float h,
                     bool ticked, bool isEnabled,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    // =========================================================================
    // Slider Drawing
    // =========================================================================
    //
    // Linear sliders:
    // - Horizontal and vertical orientations supported
    // - Track with filled portion showing current value
    // - Touch-friendly thumb (44pt minimum) with gradient for depth
    // - States: Normal, Hover, Dragging, Disabled
    //
    // Rotary knobs:
    // - Arc visualization showing value range and current position
    // - Center knob with gradient fill
    // - Position indicator line
    // - Clear visual feedback for all interaction states

    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    int getSliderThumbRadius(juce::Slider& slider) override;

    // =========================================================================
    // Label Drawing
    // =========================================================================
    //
    // Typography hierarchy applied based on label properties:
    // - Title: Large labels or labels with "title" in component ID
    // - Heading: Bold labels or labels with "heading" in component ID
    // - Body: Default labels (minimum 12pt for mobile readability)
    // - Caption: Small labels or labels with "caption" in component ID
    //
    // Labels automatically scale text to fit bounds while maintaining
    // minimum readable size (12pt on mobile devices).

    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    juce::Font getLabelFont(juce::Label& label) override;

    /**
     * @brief Get the appropriate font for a label based on typography hierarchy
     * @param label The label to get font for
     * @param scaleFactor Optional scale factor for DPI adjustment
     * @return Font matching the label's style requirements
     */
    juce::Font getTypographyFont(juce::Label& label,
                                  float scaleFactor = 1.0f) const;

    // =========================================================================
    // Special Component Drawing
    // =========================================================================
    //
    // Mute/Solo Buttons:
    // - Mute: Red when active, gray when inactive
    // - Solo: Yellow when active, gray when inactive
    // - Set button componentID to "mute" or "solo" for automatic styling
    //
    // Transport Buttons:
    // - Play: Green triangle when playing, gray when stopped
    // - Pause: Green double bars
    // - Stop: Red square
    // - Set button componentID to "play", "pause", or "stop"
    //
    // Pattern Grid Cells:
    // - Toggle buttons with beat indicators
    // - Shows phrase color when active, subtle when inactive
    // - Set button componentID to "pattern_cell" and use properties for phrase/beat

    /**
     * @brief Draw a mute button with colored states
     * @param g Graphics context
     * @param bounds Button bounds
     * @param isActive True if muted
     * @param isHighlighted True if mouse over
     * @param isDown True if mouse pressed
     * @param isEnabled True if button is enabled
     */
    void drawMuteButton(juce::Graphics& g,
                        juce::Rectangle<float> bounds,
                        bool isActive,
                        bool isHighlighted,
                        bool isDown,
                        bool isEnabled);

    /**
     * @brief Draw a solo button with colored states
     * @param g Graphics context
     * @param bounds Button bounds
     * @param isActive True if soloed
     * @param isHighlighted True if mouse over
     * @param isDown True if mouse pressed
     * @param isEnabled True if button is enabled
     */
    void drawSoloButton(juce::Graphics& g,
                        juce::Rectangle<float> bounds,
                        bool isActive,
                        bool isHighlighted,
                        bool isDown,
                        bool isEnabled);

    /**
     * @brief Draw a transport play button
     * @param g Graphics context
     * @param bounds Button bounds
     * @param isPlaying True if currently playing
     * @param isHighlighted True if mouse over
     * @param isDown True if mouse pressed
     * @param isEnabled True if button is enabled
     */
    void drawPlayButton(juce::Graphics& g,
                        juce::Rectangle<float> bounds,
                        bool isPlaying,
                        bool isHighlighted,
                        bool isDown,
                        bool isEnabled,
                        juce::Colour iconOverride = juce::Colour());

    /**
     * @brief Draw a transport pause button
     * @param g Graphics context
     * @param bounds Button bounds
     * @param isPaused True if currently paused
     * @param isHighlighted True if mouse over
     * @param isDown True if mouse pressed
     * @param isEnabled True if button is enabled
     * @param iconOverride If non-transparent, replaces the default green/gray icon color
     */
    void drawPauseButton(juce::Graphics& g,
                         juce::Rectangle<float> bounds,
                         bool isPaused,
                         bool isHighlighted,
                         bool isDown,
                         bool isEnabled,
                         juce::Colour iconOverride = juce::Colour());

    /**
     * @brief Paint the mode-banner chrome behind the play/pause icon.
     *
     * Mirrors the PlayerCard mode banner + FsmButtonStyle armed-state gradient
     * so the top transport button visually anchors to the same family as the
     * Page Tabs / Phrase Tabs / Energy Levels buttons and the PlayerCard mode
     * row.
     */
    void drawTransportBanner(juce::Graphics& g,
                             juce::Rectangle<float> bounds,
                             bool isPlaying,
                             bool isHovered,
                             bool isEnabled,
                             juce::Colour accent = Colours::playActive);

    /**
     * @brief Draw a transport stop button
     * @param g Graphics context
     * @param bounds Button bounds
     * @param isPlaying True if transport is playing (shows red to indicate stop action)
     * @param isHighlighted True if mouse over
     * @param isDown True if mouse pressed
     * @param isEnabled True if button is enabled
     */
    void drawStopButton(juce::Graphics& g,
                        juce::Rectangle<float> bounds,
                        bool isPlaying,
                        bool isHighlighted,
                        bool isDown,
                        bool isEnabled);

    /**
     * @brief Draw a pattern grid cell
     * @param g Graphics context
     * @param bounds Cell bounds
     * @param isActive True if this step is active
     * @param isCurrent True if this is the currently playing step
     * @param phraseIndex Phrase index (0-7) for color
     * @param isHighlighted True if mouse over
     * @param isDown True if mouse pressed
     * @param isEnabled True if button is enabled
     */
    void drawPatternCell(juce::Graphics& g,
                         juce::Rectangle<float> bounds,
                         bool isActive,
                         bool isCurrent,
                         int phraseIndex,
                         bool isHighlighted,
                         bool isDown,
                         bool isEnabled);

    // =========================================================================
    // ComboBox Drawing
    // =========================================================================

    void drawComboBox(juce::Graphics& g,
                      int width, int height,
                      bool isButtonDown,
                      int buttonX, int buttonY,
                      int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawPopupMenuBackground(juce::Graphics& g,
                                  int width, int height) override;

    /**
     * @brief DROP-01 (Sprint 35): radial-gradient popup background
     *
     * Modern WithOptions entry point that JUCE's MenuWindow calls. Reads
     * `options.getTargetComponent()` to find the originating ComboBox and
     * resolves an "otto-accent" property from that component (set by
     * PlayerCard on every embedded ComboBox). Paints a radial gradient
     * matching the PlayerCard ambient-bloom visual language: inner colour
     * = accent at low alpha, outer colour = Colours::bg0. Falls back to
     * the neutral default (Colours::bg2 → Colours::bg0) when no accent is
     * resolvable (i.e. dropdowns outside a PlayerCard).
     *
     * juce::PopupMenu is detached (Bug 66) — the popup window is NOT a
     * child of the ComboBox — so the resolver MUST work from the target
     * component supplied via `getOptionsForComboBoxPopupMenu`'s default
     * `withTargetComponent(box)`, not from the popup's own component
     * tree.
     *
     * iOS-first menu LnF unification: every user-facing OTTO popup now
     * routes through TouchScrollableMenuPopup / TouchMenuPresenter (a
     * Component owned by the trigger, not a detached top-level window).
     * These juce::PopupMenu LookAndFeel overrides remain for any
     * residual JUCE-internal popup paths (no top-level OTTO call site
     * still constructs a juce::PopupMenu) and read fonts / colors /
     * dimensions from the same `OTTOLookAndFeel::getMenu*` accessors
     * that the touch popup uses, so the two surfaces look identical.
     */
    void drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                             int width, int height,
                                             const juce::PopupMenu::Options& options) override;

    void drawPopupMenuItem(juce::Graphics& g,
                           const juce::Rectangle<int>& area,
                           bool isSeparator,
                           bool isActive,
                           bool isHighlighted,
                           bool isTicked,
                           bool hasSubMenu,
                           const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;

    void getIdealPopupMenuItemSize(const juce::String& text,
                                    bool isSeparator,
                                    int standardMenuItemHeight,
                                    int& idealWidth,
                                    int& idealHeight) override;

    /**
     * @brief Bold rendering for popup-menu section headers.
     *
     * Sprint 30 BUG-03: JUCE's default section-header path calls
     * `getPopupMenuFont().boldened()`, which is `withStyle("Bold")` under
     * the hood. Bug 197 — when the embedded Roboto variable font is
     * carried explicitly in FontOptions, `withStyle("Bold")` is silently
     * dropped, so section headers across the app rendered at regular
     * weight, visually indistinguishable from per-item rows.
     *
     * The override constructs the font via the size-form FontOptions
     * (no explicit typeface) so `getTypefaceForFont()` resolves the
     * typeface during paint and the variable font's weight axis honors
     * the requested Bold style. Same Bug 197 escape pattern as
     * `applySectionTitleStyle` / `drawSectionTitle`.
     *
     * UPPERCASE casing is NOT applied here — that's a per-popup
     * decision left at the call site (e.g., KitPicker emits
     * `manufacturer.toUpperCase()`). OverflowMenuButton's "Factory" /
     * "User" headers stay in their original case but become bold (which
     * is JUCE's intended default behavior, latent-broken by Bug 197).
     */
    void drawPopupMenuSectionHeader(juce::Graphics& g,
                                     const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;

    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(
        juce::ComboBox& box,
        juce::Label& currentValueLabel) override;

    /**
     * @brief Platform-conditional scroll affordance for overflowing popup menus
     *
     * Desktop: render a subtle accent-colored chevron on a tinted strip so
     * users see more items exist beyond the viewport.
     *
     * iOS (BUG-01, Sprint 29): suppress the chevron entirely. Touch users
     * scroll via finger pan / flick on the menu body, which JUCE's MenuWindow
     * supports independently of what the LookAndFeel paints.
     */
    void drawPopupMenuUpDownArrow(juce::Graphics& g,
                                   int width, int height,
                                   bool isScrollUpArrow) override;

    void drawPopupMenuUpDownArrowWithOptions(juce::Graphics& g,
                                              int width, int height,
                                              bool isScrollUpArrow,
                                              const juce::PopupMenu::Options& options) override;

    // =========================================================================
    // Font Accessors (ensure Roboto is used everywhere)
    // =========================================================================

    juce::Font getPopupMenuFont() override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;

    /**
     * @brief Position the ComboBox's internal value-display label.
     *
     * Sprint 30 BUG-05: JUCE's default LookAndFeel_V4 reserves a
     * fixed 30 px on the right of the ComboBox for the dropdown
     * arrow chrome. OTTO's `drawComboBox` deliberately renders no
     * arrow (channel-strip footer is too narrow to host one — see
     * `CompactFaderStrip::kStripWidth = 60`), so the 30 px
     * reservation is wasted space that pushes long pair labels
     * such as `63-64` past the visible label area and lets JUCE's
     * fitted-text path ellipsize the rendered string.
     *
     * The override claims the full strip width (minus a small
     * 4 px horizontal margin so the text doesn't kiss the rounded
     * border) and pins the label's minimum horizontal scale to
     * 0.5 so `addFittedText` can scale long labels down to fit
     * without falling back to an ellipsis. Bug 180 (auto-fit font
     * pattern) precedent at the renderer side rather than the
     * call-site, since ComboBox owns its internal Label and the
     * "empty button text + paintOverChildren" form does not apply
     * to ComboBox.
     *
     * Centred justification matches the dropdown menu items'
     * paint convention (`drawPopupMenuItem`) so the closed-combo
     * label and the open-popup item read as the same value.
     */
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;

    // =========================================================================
    // Focus Indication
    // =========================================================================

    void drawFocusOutline(juce::Graphics& g,
                          int width, int height,
                          const juce::Path& path);

    // =========================================================================
    // HiDPI Rendering Helpers
    // =========================================================================

    /**
     * @brief Get scale factor from graphics context
     * @param g Graphics context
     * @return Display scale factor
     *
     * Uses the graphics context's current transform to detect scale.
     */
    static float getScaleFactorFromGraphics(const juce::Graphics& g);

    /**
     * @brief Draw a crisp 1pt line aligned to physical pixels
     * @param g Graphics context
     * @param x1, y1 Start point
     * @param x2, y2 End point
     * @param scaleFactor Display scale factor
     *
     * Snaps coordinates and uses appropriate stroke width for crisp rendering.
     */
    static void drawCrispLine(juce::Graphics& g,
                               float x1, float y1,
                               float x2, float y2,
                               float scaleFactor);

    /**
     * @brief Draw a crisp rounded rectangle outline
     * @param g Graphics context
     * @param bounds Rectangle bounds
     * @param cornerRadius Corner radius
     * @param lineWidth Stroke width in points
     * @param scaleFactor Display scale factor
     *
     * Snaps to pixel boundaries for crisp rendering at any scale.
     */
    static void drawCrispRoundedRectangle(juce::Graphics& g,
                                           juce::Rectangle<float> bounds,
                                           float cornerRadius,
                                           float lineWidth,
                                           float scaleFactor);

    /**
     * @brief Draw a crisp ellipse outline
     * @param g Graphics context
     * @param bounds Ellipse bounds
     * @param lineWidth Stroke width in points
     * @param scaleFactor Display scale factor
     */
    static void drawCrispEllipse(juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  float lineWidth,
                                  float scaleFactor);

private:
    /**
     * @brief Initialize color scheme from OTTOColours
     */
    void setupColours();

    /**
     * @brief DROP-01 (Sprint 35): resolve dropdown radial-gradient accent
     *
     * Returns the player-accent colour stamped on the originating component
     * (or its parent chain) under the property key "otto-accent". PlayerCard
     * sets this property on every embedded ComboBox during construction (and
     * refreshes it in `setPlayerIndex`) so dropdowns inherit the per-card
     * accent without parent traversal in the common case. Walks the parent
     * chain as a fallback so popups attached to non-ComboBox widgets sitting
     * inside a PlayerCard still pick up the card's accent.
     *
     * Returns Colours::bg2 when no accent is found — the neutral default for
     * dropdowns outside any PlayerCard (Sounds / Mixer / Phrase / Preferences).
     */
    static juce::Colour resolveDropdownAccent(juce::Component* originatingComponent);

    /**
     * @brief DROP-01 (Sprint 35): paint the shared dropdown radial gradient
     *
     * Single source of truth for the closed-ComboBox body and open-popup
     * background. Inner colour = `accent` at alpha 0.55 anchored upper-left,
     * outer colour = Colours::bg0. Matches the PlayerCard ambient-bloom
     * visual language so an open popup over an orange card reads as a
     * tinted continuation of the card surface rather than a flat overlay.
     */
    static void paintDropdownGradient(juce::Graphics& g,
                                       juce::Rectangle<float> bounds,
                                       juce::Colour accent,
                                       float cornerRadius);

    /**
     * @brief Setup default fonts with Roboto typeface
     */
    void setupFonts();

    /**
     * @brief Load the Roboto typeface from binary data
     */
    static void loadRobotoTypeface();

    /**
     * @brief Load the Orbitron typeface from binary data
     */
    static void loadOrbitronTypeface();

    /**
     * @brief Load the Roboto Condensed typeface from binary data
     */
    static void loadRobotoCondensedTypeface();

    /**
     * @brief Load the 8 Player Card v6 typefaces from binary data (PCv6-01).
     *
     * Idempotent — repeated calls are no-ops once typefaces are loaded.
     * Called from `setupFonts()` so every OTTOLookAndFeel instance gets the
     * Bricolage Grotesque + JetBrains Mono families ready for `getBricolage()`
     * / `getJetBrainsMono()` accessors. Static members so the 8 typefaces are
     * shared across LookAndFeel instances (same lifetime model as Roboto /
     * Orbitron).
     */
    static void loadCustomFonts();

    /**
     * @brief Load the fader cap image from binary data
     */
    static void loadFaderCapImage();

    // =========================================================================
    // Slider Drawing Helpers (extracted for Rule -1 compliance)
    // =========================================================================

    /** Resolve fill color from slider properties: fillColor > playerIndex > componentID > accent */
    static juce::Colour resolveSliderFillColor(juce::Slider& slider);

    /** Draw the track (background + filled portion) for a linear slider */
    void drawSliderTrack(juce::Graphics& g,
                         juce::Rectangle<float> trackBounds,
                         float sliderPos, bool isVertical,
                         juce::Colour fillColour, bool isMasterGreyMode,
                         float alphaMultiplier, float trackRadius, float scale);

    /** Draw fader cap thumb (image or fallback rectangle) */
    void drawFaderCapThumb(juce::Graphics& g,
                           float sliderPos, int x, int y, int width, int height,
                           float faderCapHeight, bool isVertical, float scale);

    /** Draw standard circular thumb with glow, shadow, and gradient */
    void drawStandardThumb(juce::Graphics& g, juce::Slider& slider,
                           float sliderPos, int x, int y, int width, int height,
                           float thumbSize, juce::Colour fillColour,
                           juce::Colour thumbBaseColour,
                           bool isEnabled, float alphaMultiplier, float scale);

    // Roboto typeface (shared across all instances)
    static juce::Typeface::Ptr robotoTypeface_;

    // Orbitron typeface for display text (shared across all instances)
    static juce::Typeface::Ptr orbitronTypeface_;

    // Roboto Condensed typeface for menu text (shared across all instances).
    // Sole typeface for every dropdown / popup / submenu / context menu /
    // ComboBox popup. Loaded lazily by `loadRobotoCondensedTypeface()`.
    static juce::Typeface::Ptr robotoCondensedTypeface_;

    // Player Card v6 typefaces (PCv6-01) — shared across all instances.
    // 5 Bricolage Grotesque weights (400/500/600/700/800) + 3 JetBrains Mono
    // weights (400/500/600). Loaded lazily by `loadCustomFonts()`.
    static juce::Typeface::Ptr bricolageRegularTypeface_;     // weight 400
    static juce::Typeface::Ptr bricolageMediumTypeface_;      // weight 500
    static juce::Typeface::Ptr bricolageSemiBoldTypeface_;    // weight 600
    static juce::Typeface::Ptr bricolageBoldTypeface_;        // weight 700
    static juce::Typeface::Ptr bricolageExtraBoldTypeface_;   // weight 800
    static juce::Typeface::Ptr jetBrainsMonoRegularTypeface_;  // weight 400
    static juce::Typeface::Ptr jetBrainsMonoMediumTypeface_;   // weight 500
    static juce::Typeface::Ptr jetBrainsMonoSemiBoldTypeface_; // weight 600

    // Instance counter for cleanup
    static int instanceCount_;

    // Fader cap image (shared across all instances)
    static juce::Image faderCapImage_;

    // Cached fonts
    juce::Font captionFont_;
    juce::Font defaultFont_;
    juce::Font headingFont_;
    juce::Font titleFont_;
    juce::Font largeFont_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OTTOLookAndFeel)
};

} // namespace otto
