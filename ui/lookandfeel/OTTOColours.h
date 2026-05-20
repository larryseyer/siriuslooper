#pragma once

/**
 * @file OTTOColours.h
 * @brief OTTO color scheme definitions v2.0
 *
 * Complete color system following UI_SYSTEM_SPECIFICATIONS.md.
 * Dark theme with layered backgrounds, 4 player colors, and WCAG AA compliance.
 */

#include <juce_gui_basics/juce_gui_basics.h>

namespace otto {
namespace Colours {

// =============================================================================
// Background Layers (Layered Depth System)
// Darker = further back, lighter = elevated/interactive
// =============================================================================

inline const juce::Colour bg0{0xff010000};  // App edges (matches logo background)
inline const juce::Colour bg1{0xff010000};  // Main app background (matches logo background)
inline const juce::Colour bg2{0xff141414};  // Cards, panels, containers
inline const juce::Colour bg3{0xff1e1e1e};  // Modals, popovers, floating elements
inline const juce::Colour bg4{0xff282828};  // Hover states on interactive elements
inline const juce::Colour bg5{0xff333333};  // Pressed/active states
inline const juce::Colour bg6{0xff404040};  // Maximum elevation

// Menu-specific highlight — used by OTTOLookAndFeel::drawPopupMenuItem when an
// item is highlighted. Brighter than bg4 so nested PopupMenu highlights remain
// legible at every depth; paired with juce::Colours::white text in the highlight
// branch. Do not reuse as a generic layer — menu highlight is distinct from
// bg4 (hover), bg5 (pressed), bg6 (maximum elevation).
inline const juce::Colour menuHighlight{0xff555555};

// Legacy background names mapped to new layer system
inline const juce::Colour backgroundPrimary   = bg2;  // Was panel backgrounds
inline const juce::Colour backgroundSecondary = bg3;  // Was elevated surfaces
inline const juce::Colour backgroundDarker    = bg1;  // Was deepest backgrounds

// =============================================================================
// Border Colors
// =============================================================================

inline const juce::Colour borderSubtle{0xff1a1a1a};   // Barely visible, panel separation
inline const juce::Colour borderDefault{0xff2a2a2a};  // Standard component borders
inline const juce::Colour borderStrong{0xff404040};   // Emphasized borders, focus rings

// Legacy border names
inline const juce::Colour border  = borderSubtle;
inline const juce::Colour divider = borderDefault;

// =============================================================================
// Text Colors
// =============================================================================

inline const juce::Colour textPrimary{0xffffffff};    // Main content, headings
inline const juce::Colour textSecondary{0xffa0a0a0};  // Labels, descriptions, hints
inline const juce::Colour textDisabled{0xff606060};   // Disabled text, timestamps
inline const juce::Colour textInverse{0xff000000};    // Text on bright colored backgrounds

// Legacy text names
inline const juce::Colour textMuted = textDisabled;
inline const juce::Colour textIcon  = textDisabled;
inline const juce::Colour textBlack = textInverse;

// =============================================================================
// Accent Colors
// =============================================================================

inline const juce::Colour accent{0xff00d4aa};         // Primary actions, active states
inline const juce::Colour accentBright{0xff00ffd0};   // Glows, emphasis, beat pulse
inline const juce::Colour accentDim{0xff008866};      // Subtle accent applications

// =============================================================================
// Player Colors (8 unique colors for player identity)
// Chosen for visual distinction and colorblind accessibility
// =============================================================================

inline const juce::Colour player1{0xffff8c42};  // Orange
inline const juce::Colour player2{0xffff4f9a};  // Magenta
inline const juce::Colour player3{0xffb08aff};  // Purple
inline const juce::Colour player4{0xff4ade80};  // Green
inline const juce::Colour player5{0xffff9f43};  // Orange
inline const juce::Colour player6{0xffaa96da};  // Lavender Purple
inline const juce::Colour player7{0xff6bcb77};  // Leaf Green
inline const juce::Colour player8{0xff4d96ff};  // Sky Blue

// Player colors array for indexed access
inline const juce::Colour playerColors[] = {
    player1, player2, player3, player4,
    player5, player6, player7, player8
};

/**
 * @brief Get player color by index (0-3)
 */
inline juce::Colour getPlayerColour(int index) {
    return playerColors[static_cast<size_t>(index) % 8];
}

/**
 * @brief Get phrase color by index (0-3) - uses player colors
 */
inline juce::Colour getPhraseColour(int index) {
    return getPlayerColour(index);
}

// =============================================================================
// Player Muted State
// =============================================================================

inline const juce::Colour playerMuted{0xff3a3a3a};     // Replaces player color when muted
inline const juce::Colour playerMutedDim{0xff2a2a2a};  // Meter background when muted

// =============================================================================
// Player Disabled State
// =============================================================================

inline const juce::Colour disabledOverlay{0x80000000};       // Semi-transparent dark overlay (50% opacity)
inline const juce::Colour disabledElementAlpha{0x60ffffff};  // 38% opacity mask for beat grid/meters

// =============================================================================
// Semantic Colors (State Indicators)
// =============================================================================

inline const juce::Colour success{0xff4ade80};   // Confirmations, valid states
inline const juce::Colour warning{0xfffbbf24};   // Cautions, approaching limits
inline const juce::Colour error{0xfff87171};     // Errors, clip indicators, destructive

// =============================================================================
// Meter Colors
// =============================================================================

inline const juce::Colour meterLow{0xff5eff8a};       // -inf to -12dB (green)
inline const juce::Colour meterMid{0xffffc93c};       // -12dB to -3dB (yellow)
inline const juce::Colour meterHigh{0xffff8585};      // -3dB to 0dB (red)
inline const juce::Colour meterClip{0xffff0000};      // Above 0dB (bright red)
inline const juce::Colour meterBackground{0xff2d2d2d}; // Unfilled meter area

/**
 * @brief Get meter color for a dB value
 * @param dB Decibel value
 */
inline juce::Colour getMeterColour(float dB) {
    if (dB < -12.0f) return meterLow;
    if (dB < -3.0f)  return meterMid;
    if (dB < 0.0f)   return meterHigh;
    return meterClip;
}

// =============================================================================
// Transport Colors
// =============================================================================

inline const juce::Colour playActive{0xff4ade80};      // Green when playing
inline const juce::Colour stopActive{0xfff87171};      // Red for stop (when playing)
inline const juce::Colour transportInactive{0xff606060}; // Inactive transport buttons
inline const juce::Colour syncSearching{0xffffc107};   // Amber when searching for sync

// =============================================================================
// Action Colors
// =============================================================================

inline const juce::Colour deleteAction{0xffe53935};    // Red for delete actions
inline const juce::Colour duplicateAction{0xff43a047}; // Green for duplicate actions

// =============================================================================
// Output Type Colors
// =============================================================================

inline const juce::Colour midiOutputColor{0xff4a90d9};    // Blue for MIDI output

// =============================================================================
// Button Colors
// =============================================================================

// Primary button (accent)
inline const juce::Colour buttonPrimary{0xff00d4aa};
inline const juce::Colour buttonPrimaryHover{0xff00ffd0};
inline const juce::Colour buttonPrimaryActive{0xff008866};

// Secondary button
inline const juce::Colour buttonSecondary{0xff1e1e1e};
inline const juce::Colour buttonSecondaryHover{0xff282828};
inline const juce::Colour buttonSecondaryActive{0xff333333};

// Destructive button
inline const juce::Colour buttonDestructive{0xfff87171};
inline const juce::Colour buttonDestructiveHover{0xfffca5a5};
inline const juce::Colour buttonDestructiveActive{0xffdc2626};

// Legacy button colors mapped to new system
inline const juce::Colour buttonTextNormal  = textDisabled;
inline const juce::Colour buttonTextHover   = textSecondary;
inline const juce::Colour buttonTextActive  = textPrimary;
inline const juce::Colour buttonFace        = bg4;
inline const juce::Colour buttonHighlight   = bg5;
inline const juce::Colour buttonShadow      = bg2;
inline const juce::Colour buttonBorderNormal = borderDefault;
inline const juce::Colour buttonBorderHover  = borderStrong;
inline const juce::Colour buttonBorderActive = borderStrong;

// =============================================================================
// Mute/Solo/Fill Button Colors
// =============================================================================

inline const juce::Colour muteActive{0xfff87171};     // Red when muted
inline const juce::Colour muteInactive{0xff606060};   // Gray when not muted
inline const juce::Colour soloActive{0xfffbbf24};     // Yellow when soloed
inline const juce::Colour soloInactive{0xff606060};   // Gray when not soloed
inline const juce::Colour fillActive{0xff00d4aa};     // Accent color when fill is playing

// =============================================================================
// Slider/Knob Colors
// =============================================================================

inline const juce::Colour sliderTrack{0xff1a1a1a};    // Track background
inline const juce::Colour sliderFill{0xff00d4aa};     // Filled portion (accent)
inline const juce::Colour sliderThumb{0xffffffff};    // Thumb color
inline const juce::Colour sliderCapColor{0xff4a4a4a}; // Fader cap fill
inline const juce::Colour sliderCapBorder{0xff252525}; // Fader cap border
inline const juce::Colour knobArcBackground{0xff2a2a2a}; // Knob arc background
inline const juce::Colour knobCenter{0xff1e1e1e};     // Knob center fill
inline const juce::Colour masterFaderDark{0xff404040};   // Master fader track above knob
inline const juce::Colour masterFaderLight{0xff808080};  // Master fader track below knob

// Legacy slider colors
inline const juce::Colour sliderBackground = sliderTrack;
inline const juce::Colour sliderHover      = bg5;
inline const juce::Colour sliderDragging   = bg6;

// =============================================================================
// Focus & Selection
// =============================================================================

inline const juce::Colour focusOutline{0xff00d4aa};   // Accent focus ring
inline const juce::Colour selection{0xff00d4aa};      // Selected item highlight

} // namespace Colours
} // namespace otto
