#include "ida/SessionFormat.h"

#include "ida/EffectChain.h"
#include "ida/Meter.h"
#include "ida/MixerGraphState.h"
#include "ida/Phrase.h"
#include "ida/PluginDescriptor.h"
#include "ida/Position.h"
#include "ida/Promotion.h"
#include "ida/Rational.h"
#include "ida/RepetitionRules.h"
#include "ida/TapePool.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

namespace ida::persistence
{

namespace
{
    // Version 2 (2026-05-16) introduces structural-sharing refs: a child entry
    // in the "children" array is either a full constituent object or a
    // `{ "ref": <id> }` object that aliases an earlier emission of the same
    // ChildPtr. Version 1 sessions emitted each shared Phrase multiple times
    // and lost pointer-identity on reload (verse × 3 became three distinct
    // Phrases sharing one id, which the shared-instance invariant rejects on
    // the next edit). v1 files are not migrated — re-save under v2.
    constexpr int currentVersion = 2;

    // --- error reporting ------------------------------------------------------

    [[noreturn]] void fail (const std::string& message)
    {
        throw std::runtime_error ("ida::persistence::SessionFormat: " + message);
    }

    juce::DynamicObject::Ptr makeObject() { return new juce::DynamicObject(); }

    juce::var objectVar (juce::DynamicObject::Ptr obj) { return juce::var (obj.get()); }

    const juce::DynamicObject& requireObject (const juce::var& value, const char* context)
    {
        if (auto* obj = value.getDynamicObject())
            return *obj;
        fail (std::string ("expected an object for ") + context);
    }

    juce::var requireProperty (const juce::var& value, const char* name)
    {
        const auto& obj = requireObject (value, name);
        if (! obj.hasProperty (juce::Identifier (name)))
            fail (std::string ("missing property: ") + name);
        return obj.getProperty (juce::Identifier (name));
    }

    juce::var optionalProperty (const juce::var& value, const char* name)
    {
        if (auto* obj = value.getDynamicObject())
            if (obj->hasProperty (juce::Identifier (name)))
                return obj->getProperty (juce::Identifier (name));
        return juce::var();
    }

    // --- Rational <-> "n/d" string -------------------------------------------

    juce::var rationalToVar (Rational r)
    {
        return juce::String (r.numerator()) + "/" + juce::String (r.denominator());
    }

    Rational rationalFromVar (const juce::var& v, const char* context)
    {
        if (! v.isString())
            fail (std::string ("expected a rational string for ") + context);
        const auto str = v.toString();
        const auto slash = str.indexOfChar ('/');
        if (slash <= 0 || slash == str.length() - 1)
            fail (std::string ("malformed rational \"") + str.toStdString() + "\" for " + context);
        const auto num = str.substring (0, slash).getLargeIntValue();
        const auto den = str.substring (slash + 1).getLargeIntValue();
        return Rational (num, den);
    }

    juce::var positionToVar (Position p)        { return rationalToVar (p.wholeNotes()); }
    Position    positionFromVar (const juce::var& v, const char* ctx)
    {
        return Position (rationalFromVar (v, ctx));
    }

    int requireInt (const juce::var& v, const char* context)
    {
        if (! v.isInt() && ! v.isInt64() && ! v.isDouble())
            fail (std::string ("expected an integer for ") + context);
        return static_cast<int> (static_cast<juce::int64> (v));
    }

    juce::int64 requireInt64 (const juce::var& v, const char* context)
    {
        if (! v.isInt() && ! v.isInt64() && ! v.isDouble())
            fail (std::string ("expected an integer for ") + context);
        return static_cast<juce::int64> (v);
    }

    // --- enums ----------------------------------------------------------------

    const char* anchorToString (AnchorToParent a)
    {
        switch (a)
        {
            case AnchorToParent::Start:  return "Start";
            case AnchorToParent::End:    return "End";
            case AnchorToParent::Both:   return "Both";
            case AnchorToParent::Locked: return "Locked";
            case AnchorToParent::Free:   return "Free";
        }
        return "Free";
    }

    AnchorToParent anchorFromString (const juce::String& s)
    {
        if (s == "Start")  return AnchorToParent::Start;
        if (s == "End")    return AnchorToParent::End;
        if (s == "Both")   return AnchorToParent::Both;
        if (s == "Locked") return AnchorToParent::Locked;
        if (s == "Free")   return AnchorToParent::Free;
        fail (std::string ("unknown anchor \"") + s.toStdString() + "\"");
    }

    const char* mutationToString (Mutation m)
    {
        switch (m)
        {
            case Mutation::Identical:           return "Identical";
            case Mutation::VariedAutomatically: return "VariedAutomatically";
            case Mutation::Layered:             return "Layered";
            case Mutation::Decaying:            return "Decaying";
            case Mutation::EvolvingByRule:      return "EvolvingByRule";
        }
        return "Identical";
    }

    Mutation mutationFromString (const juce::String& s)
    {
        if (s == "Identical")           return Mutation::Identical;
        if (s == "VariedAutomatically") return Mutation::VariedAutomatically;
        if (s == "Layered")             return Mutation::Layered;
        if (s == "Decaying")            return Mutation::Decaying;
        if (s == "EvolvingByRule")      return Mutation::EvolvingByRule;
        fail (std::string ("unknown mutation \"") + s.toStdString() + "\"");
    }

    const char* entranceToString (EntranceCharacter e)
    {
        switch (e)
        {
            case EntranceCharacter::Pickup:      return "Pickup";
            case EntranceCharacter::Downbeat:    return "Downbeat";
            case EntranceCharacter::Unspecified: return "Unspecified";
        }
        return "Unspecified";
    }

    EntranceCharacter entranceFromString (const juce::String& s)
    {
        if (s == "Pickup")      return EntranceCharacter::Pickup;
        if (s == "Downbeat")    return EntranceCharacter::Downbeat;
        if (s == "Unspecified") return EntranceCharacter::Unspecified;
        fail (std::string ("unknown entrance \"") + s.toStdString() + "\"");
    }

    const char* exitToString (ExitCharacter e)
    {
        switch (e)
        {
            case ExitCharacter::Resolution:  return "Resolution";
            case ExitCharacter::HandOff:     return "HandOff";
            case ExitCharacter::Unspecified: return "Unspecified";
        }
        return "Unspecified";
    }

    ExitCharacter exitFromString (const juce::String& s)
    {
        if (s == "Resolution")  return ExitCharacter::Resolution;
        if (s == "HandOff")     return ExitCharacter::HandOff;
        if (s == "Unspecified") return ExitCharacter::Unspecified;
        fail (std::string ("unknown exit \"") + s.toStdString() + "\"");
    }

    const char* grammarKindToString (GrammaticalLink::Kind k)
    {
        switch (k)
        {
            case GrammaticalLink::Kind::CallAndResponse:    return "CallAndResponse";
            case GrammaticalLink::Kind::StatementVariation: return "StatementVariation";
            case GrammaticalLink::Kind::ThemeDevelopment:   return "ThemeDevelopment";
            case GrammaticalLink::Kind::TensionRelease:     return "TensionRelease";
            case GrammaticalLink::Kind::Punctuation:        return "Punctuation";
        }
        return "CallAndResponse";
    }

    GrammaticalLink::Kind grammarKindFromString (const juce::String& s)
    {
        if (s == "CallAndResponse")    return GrammaticalLink::Kind::CallAndResponse;
        if (s == "StatementVariation") return GrammaticalLink::Kind::StatementVariation;
        if (s == "ThemeDevelopment")   return GrammaticalLink::Kind::ThemeDevelopment;
        if (s == "TensionRelease")     return GrammaticalLink::Kind::TensionRelease;
        if (s == "Punctuation")        return GrammaticalLink::Kind::Punctuation;
        fail (std::string ("unknown grammatical link kind \"") + s.toStdString() + "\"");
    }

    const char* pluginFormatToString (PluginFormat f)
    {
        switch (f)
        {
            case PluginFormat::Vst3:        return "Vst3";
            case PluginFormat::AudioUnit:   return "AudioUnit";
            case PluginFormat::AudioUnitV3: return "AudioUnitV3";
            case PluginFormat::Clap:        return "Clap";
        }
        return "Vst3";
    }

    PluginFormat pluginFormatFromString (const juce::String& s)
    {
        if (s == "Vst3")        return PluginFormat::Vst3;
        if (s == "AudioUnit")   return PluginFormat::AudioUnit;
        if (s == "AudioUnitV3") return PluginFormat::AudioUnitV3;
        if (s == "Clap")        return PluginFormat::Clap;
        fail (std::string ("unknown plugin format \"") + s.toStdString() + "\"");
    }

    // --- variant dimensions ---------------------------------------------------

    juce::var triggerToVar (const Trigger& t)
    {
        auto obj = makeObject();
        if (std::holds_alternative<trigger::FreeRunning> (t))      obj->setProperty ("kind", "FreeRunning");
        else if (std::holds_alternative<trigger::OnDemand> (t))    obj->setProperty ("kind", "OnDemand");
        else if (auto* n = std::get_if<trigger::EveryNBars> (&t))
        { obj->setProperty ("kind", "EveryNBars"); obj->setProperty ("bars", n->bars); }
        else if (auto* a = std::get_if<trigger::AfterConstituent> (&t))
        { obj->setProperty ("kind", "AfterConstituent"); obj->setProperty ("ref", a->reference.value()); }
        else if (auto* p = std::get_if<trigger::AtPosition> (&t))
        { obj->setProperty ("kind", "AtPosition"); obj->setProperty ("pos", positionToVar (p->position)); }
        else if (auto* pr = std::get_if<trigger::Probabilistic> (&t))
        { obj->setProperty ("kind", "Probabilistic"); obj->setProperty ("chance", rationalToVar (pr->chancePerCycle)); }
        return objectVar (obj);
    }

    Trigger triggerFromVar (const juce::var& v)
    {
        const auto kind = requireProperty (v, "kind").toString();
        if (kind == "FreeRunning") return trigger::FreeRunning {};
        if (kind == "OnDemand")    return trigger::OnDemand {};
        if (kind == "EveryNBars")  return trigger::EveryNBars { requireInt (requireProperty (v, "bars"), "trigger.bars") };
        if (kind == "AfterConstituent")
            return trigger::AfterConstituent { ConstituentId (requireInt64 (requireProperty (v, "ref"), "trigger.ref")) };
        if (kind == "AtPosition")
            return trigger::AtPosition { positionFromVar (requireProperty (v, "pos"), "trigger.pos") };
        if (kind == "Probabilistic")
            return trigger::Probabilistic { rationalFromVar (requireProperty (v, "chance"), "trigger.chance") };
        fail (std::string ("unknown trigger kind \"") + kind.toStdString() + "\"");
    }

    juce::var cardinalityToVar (const Cardinality& c)
    {
        auto obj = makeObject();
        if (std::holds_alternative<cardinality::Once> (c))               obj->setProperty ("kind", "Once");
        else if (auto* n = std::get_if<cardinality::NTimes> (&c))
        { obj->setProperty ("kind", "NTimes"); obj->setProperty ("count", n->count); }
        else if (std::holds_alternative<cardinality::UntilSilenced> (c)) obj->setProperty ("kind", "UntilSilenced");
        else if (auto* u = std::get_if<cardinality::UntilConstituentStarts> (&c))
        { obj->setProperty ("kind", "UntilConstituentStarts"); obj->setProperty ("ref", u->reference.value()); }
        else if (std::holds_alternative<cardinality::UntilNextDownbeat> (c)) obj->setProperty ("kind", "UntilNextDownbeat");
        else if (std::holds_alternative<cardinality::Forever> (c))       obj->setProperty ("kind", "Forever");
        return objectVar (obj);
    }

    Cardinality cardinalityFromVar (const juce::var& v)
    {
        const auto kind = requireProperty (v, "kind").toString();
        if (kind == "Once")              return cardinality::Once {};
        if (kind == "NTimes")            return cardinality::NTimes { requireInt (requireProperty (v, "count"), "cardinality.count") };
        if (kind == "UntilSilenced")     return cardinality::UntilSilenced {};
        if (kind == "UntilConstituentStarts")
            return cardinality::UntilConstituentStarts { ConstituentId (requireInt64 (requireProperty (v, "ref"), "cardinality.ref")) };
        if (kind == "UntilNextDownbeat") return cardinality::UntilNextDownbeat {};
        if (kind == "Forever")           return cardinality::Forever {};
        fail (std::string ("unknown cardinality kind \"") + kind.toStdString() + "\"");
    }

    juce::var phaseToVar (const Phase& p)
    {
        auto obj = makeObject();
        if (std::holds_alternative<phase::Free> (p)) obj->setProperty ("kind", "Free");
        else if (auto* q = std::get_if<phase::QuantizedToGrid> (&p))
        { obj->setProperty ("kind", "QuantizedToGrid"); obj->setProperty ("division", rationalToVar (q->division)); }
        else if (auto* s = std::get_if<phase::SynchronizedTo> (&p))
        { obj->setProperty ("kind", "SynchronizedTo"); obj->setProperty ("ref", s->reference.value());
          obj->setProperty ("offset", rationalToVar (s->offset)); }
        else if (auto* l = std::get_if<phase::PhaseLocked> (&p))
        { obj->setProperty ("kind", "PhaseLocked"); obj->setProperty ("ref", l->reference.value());
          obj->setProperty ("offset", rationalToVar (l->offset)); }
        return objectVar (obj);
    }

    Phase phaseFromVar (const juce::var& v)
    {
        const auto kind = requireProperty (v, "kind").toString();
        if (kind == "Free") return phase::Free {};
        if (kind == "QuantizedToGrid")
            return phase::QuantizedToGrid { rationalFromVar (requireProperty (v, "division"), "phase.division") };
        if (kind == "SynchronizedTo")
            return phase::SynchronizedTo { ConstituentId (requireInt64 (requireProperty (v, "ref"), "phase.ref")),
                                           rationalFromVar (requireProperty (v, "offset"), "phase.offset") };
        if (kind == "PhaseLocked")
            return phase::PhaseLocked { ConstituentId (requireInt64 (requireProperty (v, "ref"), "phase.ref")),
                                        rationalFromVar (requireProperty (v, "offset"), "phase.offset") };
        fail (std::string ("unknown phase kind \"") + kind.toStdString() + "\"");
    }

    juce::var terminationToVar (const Termination& t)
    {
        auto obj = makeObject();
        if (std::holds_alternative<termination::HardCut> (t)) obj->setProperty ("kind", "HardCut");
        else if (std::holds_alternative<termination::CompleteCurrentCycle> (t)) obj->setProperty ("kind", "CompleteCurrentCycle");
        else if (auto* f = std::get_if<termination::FadeOverBars> (&t))
        { obj->setProperty ("kind", "FadeOverBars"); obj->setProperty ("bars", rationalToVar (f->bars)); }
        else if (std::holds_alternative<termination::ContinueUntilNaturalEnd> (t)) obj->setProperty ("kind", "ContinueUntilNaturalEnd");
        else if (auto* h = std::get_if<termination::HandOff> (&t))
        { obj->setProperty ("kind", "HandOff"); obj->setProperty ("next", h->next.value()); }
        return objectVar (obj);
    }

    Termination terminationFromVar (const juce::var& v)
    {
        const auto kind = requireProperty (v, "kind").toString();
        if (kind == "HardCut")              return termination::HardCut {};
        if (kind == "CompleteCurrentCycle") return termination::CompleteCurrentCycle {};
        if (kind == "FadeOverBars")
            return termination::FadeOverBars { rationalFromVar (requireProperty (v, "bars"), "termination.bars") };
        if (kind == "ContinueUntilNaturalEnd") return termination::ContinueUntilNaturalEnd {};
        if (kind == "HandOff")
            return termination::HandOff { ConstituentId (requireInt64 (requireProperty (v, "next"), "termination.next")) };
        fail (std::string ("unknown termination kind \"") + kind.toStdString() + "\"");
    }

    juce::var rulesToVar (const RepetitionRules& r)
    {
        auto obj = makeObject();
        obj->setProperty ("trigger",     triggerToVar (r.trigger));
        obj->setProperty ("cardinality", cardinalityToVar (r.cardinality));
        obj->setProperty ("phase",       phaseToVar (r.phase));
        obj->setProperty ("mutation",    mutationToString (r.mutation));
        obj->setProperty ("termination", terminationToVar (r.termination));
        return objectVar (obj);
    }

    RepetitionRules rulesFromVar (const juce::var& v)
    {
        RepetitionRules out;
        out.trigger     = triggerFromVar     (requireProperty (v, "trigger"));
        out.cardinality = cardinalityFromVar (requireProperty (v, "cardinality"));
        out.phase       = phaseFromVar       (requireProperty (v, "phase"));
        out.mutation    = mutationFromString (requireProperty (v, "mutation").toString());
        out.termination = terminationFromVar (requireProperty (v, "termination"));
        return out;
    }

    // --- sub-objects ----------------------------------------------------------

    juce::var meterToVar (const Meter& m)
    {
        auto obj = makeObject();
        obj->setProperty ("beats", m.beatsPerBar());
        obj->setProperty ("unit",  m.beatUnit());
        return objectVar (obj);
    }

    Meter meterFromVar (const juce::var& v)
    {
        return Meter (requireInt (requireProperty (v, "beats"), "meter.beats"),
                      requireInt (requireProperty (v, "unit"),  "meter.unit"));
    }

    juce::var tempoMapToVar (const TempoMap& tm)
    {
        juce::Array<juce::var> breakpoints;
        for (const auto& bp : tm.breakpoints())
        {
            auto pt = makeObject();
            pt->setProperty ("in",  rationalToVar (bp.input));
            pt->setProperty ("out", rationalToVar (bp.output));
            breakpoints.add (objectVar (pt));
        }
        auto obj = makeObject();
        obj->setProperty ("breakpoints", breakpoints);
        return objectVar (obj);
    }

    TempoMap tempoMapFromVar (const juce::var& v)
    {
        const auto& bps = requireProperty (v, "breakpoints");
        if (! bps.isArray()) fail ("tempoMap.breakpoints must be an array");
        std::vector<TempoMap::Breakpoint> out;
        out.reserve (static_cast<std::size_t> (bps.size()));
        for (int i = 0; i < bps.size(); ++i)
        {
            const auto& bp = bps[i];
            out.push_back ({ rationalFromVar (requireProperty (bp, "in"),  "tempoMap.in"),
                             rationalFromVar (requireProperty (bp, "out"), "tempoMap.out") });
        }
        return TempoMap (std::move (out));
    }

    juce::var tapeReferenceToVar (const TapeReference& ref)
    {
        auto obj = makeObject();
        obj->setProperty ("tape", ref.tape.value());
        obj->setProperty ("in",   rationalToVar (ref.tapeIn));
        obj->setProperty ("out",  rationalToVar (ref.tapeOut));
        return objectVar (obj);
    }

    TapeReference tapeReferenceFromVar (const juce::var& v)
    {
        return TapeReference (TapeId (requireInt64 (requireProperty (v, "tape"), "tape.tape")),
                              rationalFromVar (requireProperty (v, "in"),  "tape.in"),
                              rationalFromVar (requireProperty (v, "out"), "tape.out"));
    }

    juce::var phraseMetadataToVar (const PhraseMetadata& m)
    {
        auto obj = makeObject();
        obj->setProperty ("role",           juce::String (m.role));
        obj->setProperty ("intent",         juce::String (m.intent));
        obj->setProperty ("entrance",       entranceToString (m.entrance));
        obj->setProperty ("exit",           exitToString (m.exit));
        obj->setProperty ("isRoleFillable", m.isRoleFillable);

        juce::Array<juce::var> links;
        for (const auto& g : m.grammaticalLinks)
        {
            auto link = makeObject();
            link->setProperty ("kind",   grammarKindToString (g.kind));
            link->setProperty ("target", g.target.value());
            links.add (objectVar (link));
        }
        obj->setProperty ("links", links);
        return objectVar (obj);
    }

    PhraseMetadata phraseMetadataFromVar (const juce::var& v)
    {
        PhraseMetadata out;
        out.role           = requireProperty (v, "role").toString().toStdString();
        out.intent         = requireProperty (v, "intent").toString().toStdString();
        out.entrance       = entranceFromString (requireProperty (v, "entrance").toString());
        out.exit           = exitFromString     (requireProperty (v, "exit").toString());
        out.isRoleFillable = bool (requireProperty (v, "isRoleFillable"));

        const auto& links = requireProperty (v, "links");
        if (! links.isArray()) fail ("phrase.links must be an array");
        for (int i = 0; i < links.size(); ++i)
        {
            const auto& link = links[i];
            out.grammaticalLinks.push_back (
                GrammaticalLink { grammarKindFromString (requireProperty (link, "kind").toString()),
                                  ConstituentId (requireInt64 (requireProperty (link, "target"),
                                                               "link.target")) });
        }
        return out;
    }

    const char* archivalModeToString (ArchivalMode m) noexcept
    {
        switch (m)
        {
            case ArchivalMode::DeterminismContract: return "DeterminismContract";
            case ArchivalMode::WetCapture:          return "WetCapture";
            case ArchivalMode::VersionPinning:      return "VersionPinning";
        }
        return "VersionPinning"; // unreachable; spec-default fallback
    }

    ArchivalMode archivalModeFromString (const juce::String& s)
    {
        if (s == "DeterminismContract") return ArchivalMode::DeterminismContract;
        if (s == "WetCapture")          return ArchivalMode::WetCapture;
        if (s == "VersionPinning")      return ArchivalMode::VersionPinning;
        fail (("Unknown archivalMode: " + s).toStdString());
        return ArchivalMode::VersionPinning; // unreachable; fail() throws
    }

    juce::var versionPinningRecordToVar (const VersionPinningRecord& r)
    {
        auto obj = makeObject();
        obj->setProperty ("uniqueId",                  juce::String (r.uniqueId));
        obj->setProperty ("version",                   juce::String (r.version));
        obj->setProperty ("stateBlobSha256",           juce::String (r.stateBlobSha256));
        obj->setProperty ("oversamplingRate",          int (r.oversamplingRate));
        obj->setProperty ("declaredInternalStateHash", juce::String (r.declaredInternalStateHash));
        return objectVar (obj);
    }

    VersionPinningRecord versionPinningRecordFromVar (const juce::var& v)
    {
        VersionPinningRecord r;
        r.uniqueId                  = requireProperty (v, "uniqueId").toString().toStdString();
        r.version                   = requireProperty (v, "version").toString().toStdString();
        r.stateBlobSha256           = requireProperty (v, "stateBlobSha256").toString().toStdString();
        r.oversamplingRate          = std::uint32_t (int (requireProperty (v, "oversamplingRate")));
        r.declaredInternalStateHash = requireProperty (v, "declaredInternalStateHash").toString().toStdString();
        return r;
    }

    juce::var pluginDescriptorToVar (const PluginDescriptor& d)
    {
        auto obj = makeObject();
        obj->setProperty ("format",       pluginFormatToString (d.format));
        obj->setProperty ("uniqueId",     juce::String (d.uniqueId));
        obj->setProperty ("version",      juce::String (d.version));
        obj->setProperty ("name",         juce::String (d.name));
        obj->setProperty ("manufacturer", juce::String (d.manufacturer));
        obj->setProperty ("filePath",     juce::String (d.filePath));
        return objectVar (obj);
    }

    PluginDescriptor pluginDescriptorFromVar (const juce::var& v)
    {
        PluginDescriptor d;
        d.format       = pluginFormatFromString (requireProperty (v, "format").toString());
        d.uniqueId     = requireProperty (v, "uniqueId").toString().toStdString();
        d.version      = requireProperty (v, "version").toString().toStdString();
        d.name         = requireProperty (v, "name").toString().toStdString();
        d.manufacturer = requireProperty (v, "manufacturer").toString().toStdString();
        d.filePath     = requireProperty (v, "filePath").toString().toStdString();
        return d;
    }

    juce::var effectChainEntryToVar (const EffectChainEntry& e)
    {
        auto obj = makeObject();

        // Discriminant first — readers consult this before deciding which payload
        // fields to look for. Wire string is the canonical name of the enum.
        obj->setProperty ("kind",
            juce::String (e.kind == EffectChainSlotKind::Empty    ? "Empty"
                        : e.kind == EffectChainSlotKind::Internal ? "Internal"
                        :                                            "Plugin"));

        obj->setProperty ("displayName", juce::String (e.displayName));
        obj->setProperty ("bypassed",    e.bypassed);

        switch (e.kind)
        {
            case EffectChainSlotKind::Empty:
                // No payload beyond kind + displayName + bypassed.
                break;

            case EffectChainSlotKind::Internal:
                obj->setProperty ("internalId", juce::String (internalFxIdToString (e.internalId)));
                // archivalMode + state + persistedSnapshot are Plugin-only.
                break;

            case EffectChainSlotKind::Plugin:
                obj->setProperty ("plugin",       pluginDescriptorToVar (e.descriptor));
                obj->setProperty ("state",        juce::String (e.stateBase64));
                obj->setProperty ("archivalMode", juce::String (archivalModeToString (e.archivalMode)));
                if (e.persistedSnapshot.has_value())
                    obj->setProperty ("persistedSnapshot", versionPinningRecordToVar (*e.persistedSnapshot));
                break;
        }

        return objectVar (obj);
    }

    EffectChainEntry effectChainEntryFromVar (const juce::var& v)
    {
        EffectChainEntry e;

        // Forward-compat: pre-union JSON had no `kind` field. Every entry it
        // could encode was a plugin (Internal did not exist). Default missing
        // `kind` to Plugin so old sessions load without migration.
        EffectChainSlotKind kind = EffectChainSlotKind::Plugin;
        if (auto* obj = v.getDynamicObject(); obj != nullptr && obj->hasProperty ("kind"))
        {
            const auto s = obj->getProperty ("kind").toString().toStdString();
            if      (s == "Empty")    kind = EffectChainSlotKind::Empty;
            else if (s == "Internal") kind = EffectChainSlotKind::Internal;
            else if (s == "Plugin")   kind = EffectChainSlotKind::Plugin;
            else fail ("effectChainEntry.kind unknown value \"" + s + "\"");
        }
        e.kind = kind;

        // Common fields. `displayName` is required on every kind; older sessions
        // already encode it. `bypassed` defaults to false when absent.
        if (auto* obj = v.getDynamicObject(); obj != nullptr)
        {
            if (obj->hasProperty ("displayName"))
                e.displayName = obj->getProperty ("displayName").toString().toStdString();
            else if (kind != EffectChainSlotKind::Empty)
                fail ("effectChainEntry missing required `displayName` for non-Empty slot");

            if (obj->hasProperty ("bypassed"))
                e.bypassed = bool (obj->getProperty ("bypassed"));
        }

        switch (kind)
        {
            case EffectChainSlotKind::Empty:
                break; // no further payload

            case EffectChainSlotKind::Internal:
            {
                if (auto* obj = v.getDynamicObject(); obj != nullptr && obj->hasProperty ("internalId"))
                    e.internalId = internalFxIdFromString (
                        obj->getProperty ("internalId").toString().toStdString());
                else
                    fail ("effectChainEntry of kind Internal missing required `internalId`");
                break;
            }

            case EffectChainSlotKind::Plugin:
            {
                e.descriptor  = pluginDescriptorFromVar (requireProperty (v, "plugin"));
                e.stateBase64 = requireProperty (v, "state").toString().toStdString();
                // archivalMode + persistedSnapshot are M8 additions. Sessions
                // serialized before M8 do not carry them — default archivalMode
                // to VersionPinning and leave the optional snapshot empty.
                if (auto* obj = v.getDynamicObject(); obj != nullptr)
                {
                    if (obj->hasProperty ("archivalMode"))
                        e.archivalMode = archivalModeFromString (
                            obj->getProperty ("archivalMode").toString());
                    if (obj->hasProperty ("persistedSnapshot"))
                        e.persistedSnapshot = versionPinningRecordFromVar (
                            obj->getProperty ("persistedSnapshot"));
                }
                break;
            }
        }

        return e;
    }

    juce::var effectChainToVar (const EffectChain& chain)
    {
        juce::Array<juce::var> entries;
        for (const auto& entry : chain.entries())
            entries.add (effectChainEntryToVar (entry));
        auto obj = makeObject();
        obj->setProperty ("entries", entries);
        return objectVar (obj);
    }

    EffectChain effectChainFromVar (const juce::var& v)
    {
        const auto& entries = requireProperty (v, "entries");
        if (! entries.isArray()) fail ("effectChain.entries must be an array");
        EffectChain out;
        for (int i = 0; i < entries.size(); ++i)
            out = out.withAppended (effectChainEntryFromVar (entries[i]));
        return out;
    }

    // --- Constituent ----------------------------------------------------------

    // Maps the id of a Constituent already emitted in this serialize pass to
    // the raw pointer at which it was first seen. The pointer is kept so the
    // serializer can sanity-check, on every repeat encounter of an id, that
    // it really is the same allocation — the shared-instance invariant from
    // promotion::enforceSharedInstancesAreShared, mirrored here so a corrupt
    // in-memory tree cannot produce a JSON that lies about its sharing.
    using SerializeSeen = std::unordered_map<std::int64_t, const Constituent*>;

    juce::var refVar (std::int64_t id)
    {
        auto obj = makeObject();
        obj->setProperty ("ref", id);
        return objectVar (obj);
    }

    juce::var constituentToVar (const Constituent& c, SerializeSeen& seen)
    {
        auto obj = makeObject();
        obj->setProperty ("id",     c.id().value());
        obj->setProperty ("in",     positionToVar (c.conceptualIn()));
        obj->setProperty ("out",    positionToVar (c.conceptualOut()));
        obj->setProperty ("anchor", anchorToString (c.anchor()));
        obj->setProperty ("name",   juce::String (c.name()));
        obj->setProperty ("rules",  rulesToVar (c.repetitionRules()));

        if (c.localMeter())     obj->setProperty ("meter",    meterToVar     (*c.localMeter()));
        if (c.localTempoMap())  obj->setProperty ("tempoMap", tempoMapToVar  (*c.localTempoMap()));
        if (c.phraseMetadata()) obj->setProperty ("phrase",   phraseMetadataToVar (*c.phraseMetadata()));
        if (c.tapeReference())  obj->setProperty ("tape",     tapeReferenceToVar  (*c.tapeReference()));
        if (c.effectChain())    obj->setProperty ("effects",  effectChainToVar    (*c.effectChain()));

        juce::Array<juce::var> kids;
        for (const auto& child : c.children())
        {
            const auto childId = child->id().value();
            auto [it, inserted] = seen.insert ({ childId, child.get() });
            if (! inserted)
            {
                if (it->second != child.get())
                    fail ("shared-instance invariant: id "
                          + std::to_string (childId)
                          + " reached via two distinct allocations during serialization");
                kids.add (refVar (childId));
                continue;
            }
            kids.add (constituentToVar (*child, seen));
        }
        obj->setProperty ("children", kids);

        return objectVar (obj);
    }

    // Mirror of SerializeSeen on the load side: id → the ChildPtr that
    // already materialized for that id, so a `{ "ref": id }` entry can share
    // the exact same allocation rather than minting a new one. This is what
    // restores pointer-identity for verse × 3 after a save / load round-trip.
    using DeserializeSeen = std::unordered_map<std::int64_t, Constituent::ChildPtr>;

    Constituent::ChildPtr childPtrFromVar (const juce::var& v, DeserializeSeen& seen);

    Constituent constituentFromVar (const juce::var& v, DeserializeSeen& seen)
    {
        Constituent c (
            ConstituentId (requireInt64 (requireProperty (v, "id"), "constituent.id")),
            positionFromVar (requireProperty (v, "in"),  "constituent.in"),
            positionFromVar (requireProperty (v, "out"), "constituent.out"));

        c = c.withAnchor (anchorFromString (requireProperty (v, "anchor").toString()))
             .withName   (requireProperty (v, "name").toString().toStdString())
             .withRepetitionRules (rulesFromVar (requireProperty (v, "rules")));

        if (auto meter = optionalProperty (v, "meter"); ! meter.isVoid())
            c = c.withLocalMeter (meterFromVar (meter));
        if (auto tm = optionalProperty (v, "tempoMap"); ! tm.isVoid())
            c = c.withLocalTempoMap (tempoMapFromVar (tm));
        if (auto phrase = optionalProperty (v, "phrase"); ! phrase.isVoid())
            c = c.withPhraseMetadata (phraseMetadataFromVar (phrase));
        if (auto tape = optionalProperty (v, "tape"); ! tape.isVoid())
            c = c.withTapeReference (tapeReferenceFromVar (tape));
        if (auto effects = optionalProperty (v, "effects"); ! effects.isVoid())
            c = c.withEffectChain (effectChainFromVar (effects));

        const auto& kids = requireProperty (v, "children");
        if (! kids.isArray()) fail ("constituent.children must be an array");
        for (int i = 0; i < kids.size(); ++i)
            c = c.withChildAdded (childPtrFromVar (kids[i], seen));

        return c;
    }

    Constituent::ChildPtr childPtrFromVar (const juce::var& v, DeserializeSeen& seen)
    {
        if (auto ref = optionalProperty (v, "ref"); ! ref.isVoid())
        {
            const auto refId = requireInt64 (ref, "child.ref");
            auto it = seen.find (refId);
            if (it == seen.end())
                fail ("child ref to unknown id " + std::to_string (refId)
                      + " — refs must follow the first emission of that id");
            return it->second;
        }
        auto child = std::make_shared<const Constituent> (constituentFromVar (v, seen));
        seen.insert ({ child->id().value(), child });
        return child;
    }

    // --- mixer routing-graph snapshot (routing-graph Phase 5) -----------------

    constexpr int currentMixerGraphVersion = 1;

    const char* signalTypeToString (SignalType t) noexcept
    {
        switch (t)
        {
            case SignalType::Audio: return "Audio";
            case SignalType::Midi:  return "Midi";
            case SignalType::Video: return "Video";
            case SignalType::File:  return "File";
        }
        return "Audio";
    }
    SignalType signalTypeFromString (const juce::String& s)
    {
        if (s == "Audio") return SignalType::Audio;
        if (s == "Midi")  return SignalType::Midi;
        if (s == "Video") return SignalType::Video;
        if (s == "File")  return SignalType::File;
        fail (("Unknown signalType: " + s).toStdString());
        return SignalType::Audio;
    }

    const char* tapeModeToString (TapeMode m) noexcept
    {
        switch (m)
        {
            case TapeMode::CommitToTape:   return "CommitToTape";
            case TapeMode::NonDestructive: return "NonDestructive";
            case TapeMode::NoTape:         return "NoTape";
        }
        return "NoTape";
    }
    TapeMode tapeModeFromString (const juce::String& s)
    {
        if (s == "CommitToTape")   return TapeMode::CommitToTape;
        if (s == "NonDestructive") return TapeMode::NonDestructive;
        if (s == "NoTape")         return TapeMode::NoTape;
        fail (("Unknown tapeMode: " + s).toStdString());
        return TapeMode::NoTape;
    }

    const char* mixerBusKindToString (MixerBusKind k) noexcept
    { return k == MixerBusKind::FxReturn ? "FxReturn" : "Bus"; }
    MixerBusKind mixerBusKindFromString (const juce::String& s)
    {
        if (s == "FxReturn") return MixerBusKind::FxReturn;
        if (s == "Bus")      return MixerBusKind::Bus;
        fail (("Unknown busKind: " + s).toStdString());
        return MixerBusKind::Bus;
    }

    const char* terminalKindToString (MixerTerminalKind k) noexcept
    { return k == MixerTerminalKind::HardwareOutput ? "HardwareOutput" : "Tape"; }
    MixerTerminalKind terminalKindFromString (const juce::String& s)
    {
        if (s == "HardwareOutput") return MixerTerminalKind::HardwareOutput;
        if (s == "Tape")           return MixerTerminalKind::Tape;
        fail (("Unknown terminal: " + s).toStdString());
        return MixerTerminalKind::Tape;
    }

    juce::var mainOutToVar (const MixerMainOut& m)
    {
        auto obj = makeObject();
        obj->setProperty ("kind", m.kind == MixerMainOut::Kind::Bus ? "Bus" : "Terminal");
        if (m.kind == MixerMainOut::Kind::Bus)
        {
            obj->setProperty ("busId", juce::int64 (m.busId));
        }
        else
        {
            obj->setProperty ("terminal", terminalKindToString (m.terminal));
            if (m.terminal == MixerTerminalKind::Tape)
                obj->setProperty ("tapeId", juce::int64 (m.tapeId));
            else if (m.terminal == MixerTerminalKind::HardwareOutput && m.hardwareOutPair != 0)
                obj->setProperty ("hardwareOutPair", m.hardwareOutPair);
        }
        return objectVar (obj);
    }
    MixerMainOut mainOutFromVar (const juce::var& v)
    {
        MixerMainOut m;
        const auto kind = requireProperty (v, "kind").toString();
        if (kind == "Bus")
        {
            m.kind = MixerMainOut::Kind::Bus;
            m.busId = requireInt64 (requireProperty (v, "busId"), "mainOut.busId");
        }
        else if (kind == "Terminal")
        {
            m.kind = MixerMainOut::Kind::Terminal;
            m.terminal = terminalKindFromString (requireProperty (v, "terminal").toString());
            if (m.terminal == MixerTerminalKind::Tape)
            {
                // Back-compat default 1 (primary) for pre-tapeId documents.
                if (const auto t = optionalProperty (v, "tapeId"); ! t.isVoid())
                    m.tapeId = requireInt64 (t, "mainOut.tapeId");
                else
                    m.tapeId = 1;
            }
            else if (m.terminal == MixerTerminalKind::HardwareOutput)
            {
                // Back-compat default 0 (outputs [0,1]) for pre-pair documents.
                if (const auto p = optionalProperty (v, "hardwareOutPair"); ! p.isVoid())
                    m.hardwareOutPair = requireInt (p, "mainOut.hardwareOutPair");
            }
        }
        else fail (("Unknown mainOut.kind: " + kind).toStdString());
        return m;
    }

    juce::var sendsToVar (const std::vector<MixerSend>& sends)
    {
        juce::Array<juce::var> arr;
        for (const auto& s : sends)
        {
            auto obj = makeObject();
            obj->setProperty ("busId", juce::int64 (s.busId));
            obj->setProperty ("level", double (s.level));
            arr.add (objectVar (obj));
        }
        return arr;
    }
    std::vector<MixerSend> sendsFromVar (const juce::var& v)
    {
        std::vector<MixerSend> out;
        if (v.isVoid()) return out;
        if (! v.isArray()) fail ("sends must be an array");
        for (int i = 0; i < v.size(); ++i)
        {
            MixerSend s;
            s.busId = requireInt64 (requireProperty (v[i], "busId"), "send.busId");
            s.level = float (double (requireProperty (v[i], "level")));
            out.push_back (s);
        }
        return out;
    }

    juce::var busStateToVar (const MixerBusState& b)
    {
        auto obj = makeObject();
        obj->setProperty ("busId",        juce::int64 (b.busId));
        obj->setProperty ("channelCount", b.channelCount);
        obj->setProperty ("name",         juce::String (b.name));
        obj->setProperty ("kind",         mixerBusKindToString (b.kind));
        obj->setProperty ("mainOut",      mainOutToVar (b.mainOut));
        obj->setProperty ("sends",        sendsToVar (b.sends));
        obj->setProperty ("inserts",      effectChainToVar (b.inserts));
        return objectVar (obj);
    }
    MixerBusState busStateFromVar (const juce::var& v)
    {
        MixerBusState b;
        b.busId        = requireInt64 (requireProperty (v, "busId"), "bus.busId");
        b.channelCount = requireInt (requireProperty (v, "channelCount"), "bus.channelCount");
        b.name         = requireProperty (v, "name").toString().toStdString();
        b.kind         = mixerBusKindFromString (requireProperty (v, "kind").toString());
        b.mainOut      = mainOutFromVar (requireProperty (v, "mainOut"));
        b.sends        = sendsFromVar (optionalProperty (v, "sends"));
        b.inserts      = effectChainFromVar (requireProperty (v, "inserts"));
        return b;
    }

    juce::var inputChannelToVar (const InputChannelState& c)
    {
        auto src = makeObject();
        src->setProperty ("left",   c.source.left);
        src->setProperty ("right",  c.source.right);
        src->setProperty ("stereo", c.source.stereo);
        auto obj = makeObject();
        obj->setProperty ("channelId",     juce::int64 (c.channelId));
        obj->setProperty ("signalType",    signalTypeToString (c.signalType));
        obj->setProperty ("inputSourceId", juce::int64 (c.inputSourceId));
        obj->setProperty ("source",        objectVar (src));
        obj->setProperty ("tapeMode",      tapeModeToString (c.tapeMode));
        obj->setProperty ("mainOut",       mainOutToVar (c.mainOut));
        obj->setProperty ("sends",         sendsToVar (c.sends));
        obj->setProperty ("inserts",       effectChainToVar (c.inserts));
        if (c.preFaderSends)
            obj->setProperty ("preFaderSends", true);
        return objectVar (obj);
    }
    InputChannelState inputChannelFromVar (const juce::var& v)
    {
        InputChannelState c;
        c.channelId     = requireInt64 (requireProperty (v, "channelId"), "channel.channelId");
        c.signalType    = signalTypeFromString (requireProperty (v, "signalType").toString());
        c.inputSourceId = requireInt64 (requireProperty (v, "inputSourceId"), "channel.inputSourceId");
        const auto src  = requireProperty (v, "source");
        c.source.left   = requireInt (requireProperty (src, "left"),  "source.left");
        c.source.right  = requireInt (requireProperty (src, "right"), "source.right");
        c.source.stereo = bool (requireProperty (src, "stereo"));
        c.tapeMode      = tapeModeFromString (requireProperty (v, "tapeMode").toString());
        c.mainOut       = mainOutFromVar (requireProperty (v, "mainOut"));
        c.sends         = sendsFromVar (optionalProperty (v, "sends"));
        c.inserts       = effectChainFromVar (requireProperty (v, "inserts"));
        if (const auto p = optionalProperty (v, "preFaderSends"); ! p.isVoid())
            c.preFaderSends = bool (p);
        return c;
    }

    juce::var outputChannelToVar (const OutputChannelState& c)
    {
        auto obj = makeObject();
        obj->setProperty ("channelId",  juce::int64 (c.channelId));
        obj->setProperty ("signalType", signalTypeToString (c.signalType));
        obj->setProperty ("sends",      sendsToVar (c.sends));
        obj->setProperty ("inserts",    effectChainToVar (c.inserts));
        if (c.preFaderSends)
            obj->setProperty ("preFaderSends", true);
        // Slice E3: persist the main-out manifest. Only emit when non-
        // default — Bus + master is the addChannel baseline; emitting it
        // unconditionally would bloat every channel's snapshot.
        if (c.mainOutKind == OutputChannelMainOutKind::HardwareOutput)
        {
            obj->setProperty ("mainOutKind", juce::String ("HardwareOutput"));
            obj->setProperty ("hardwareOutPair", c.hardwareOutPair);
        }
        else if (c.mainOutBus != 0)
        {
            obj->setProperty ("mainOutBus", juce::int64 (c.mainOutBus));
        }
        return objectVar (obj);
    }
    OutputChannelState outputChannelFromVar (const juce::var& v)
    {
        OutputChannelState c;
        c.channelId  = requireInt64 (requireProperty (v, "channelId"), "channel.channelId");
        c.signalType = signalTypeFromString (requireProperty (v, "signalType").toString());
        c.sends      = sendsFromVar (optionalProperty (v, "sends"));
        c.inserts    = effectChainFromVar (requireProperty (v, "inserts"));
        if (const auto p = optionalProperty (v, "preFaderSends"); ! p.isVoid())
            c.preFaderSends = bool (p);
        if (const auto k = optionalProperty (v, "mainOutKind"); ! k.isVoid())
        {
            const auto s = k.toString();
            if (s == "HardwareOutput") c.mainOutKind = OutputChannelMainOutKind::HardwareOutput;
            // anything else (or absence) stays at the default Bus.
        }
        if (const auto p = optionalProperty (v, "hardwareOutPair"); ! p.isVoid())
            c.hardwareOutPair = requireInt (p, "channel.hardwareOutPair");
        if (const auto b = optionalProperty (v, "mainOutBus"); ! b.isVoid())
            c.mainOutBus = requireInt64 (b, "channel.mainOutBus");
        return c;
    }

    template <typename BusVec, typename ChannelVec, typename ChannelToVar>
    juce::String serializeMixerDoc (const BusVec& buses, const ChannelVec& channels,
                                    std::int64_t nextBusId, std::int64_t nextChannelId,
                                    ChannelToVar channelToVar)
    {
        juce::Array<juce::var> busArr;
        for (const auto& b : buses) busArr.add (busStateToVar (b));
        juce::Array<juce::var> chArr;
        for (const auto& c : channels) chArr.add (channelToVar (c));
        auto root = makeObject();
        root->setProperty ("version",       currentMixerGraphVersion);
        root->setProperty ("buses",         busArr);
        root->setProperty ("channels",      chArr);
        root->setProperty ("nextBusId",     juce::int64 (nextBusId));
        root->setProperty ("nextChannelId", juce::int64 (nextChannelId));
        return juce::JSON::toString (objectVar (root));
    }

    juce::var parseMixerDoc (const juce::String& json)
    {
        juce::var parsed;
        const auto result = juce::JSON::parse (json, parsed);
        if (result.failed())
            fail ("invalid mixer graph JSON: " + result.getErrorMessage().toStdString());
        if (! parsed.isObject()) fail ("mixer graph document must be a JSON object");
        const auto version = requireInt (requireProperty (parsed, "version"), "version");
        if (version != currentMixerGraphVersion)
            fail ("unsupported mixer graph version: " + std::to_string (version));
        return parsed;
    }
}

juce::String serializeSession (const Constituent& root)
{
    auto top = makeObject();
    top->setProperty ("version", currentVersion);
    SerializeSeen seen;
    seen.insert ({ root.id().value(), &root });
    top->setProperty ("root", constituentToVar (root, seen));
    return juce::JSON::toString (objectVar (top), /*allOnOneLine*/ false);
}

std::shared_ptr<const Constituent> deserializeSession (const juce::String& json)
{
    juce::var document;
    const auto result = juce::JSON::parse (json, document);
    if (result.failed())
        fail ("invalid JSON: " + result.getErrorMessage().toStdString());
    if (! document.isObject())
        fail ("document root must be an object");

    const auto version = requireInt (requireProperty (document, "version"), "version");
    if (version != currentVersion)
        fail ("unsupported session version: " + std::to_string (version)
              + " (this build reads version " + std::to_string (currentVersion) + ")");

    DeserializeSeen seen;
    auto root = std::make_shared<const Constituent> (
        constituentFromVar (requireProperty (document, "root"), seen));

    // Loud post-load check: any inconsistency in how the document encoded
    // sharing (e.g. two full constituent objects with the same id instead of a
    // def + ref pair) surfaces here rather than at the next edit.
    promotion::enforceSharedInstancesAreShared (*root);

    return root;
}

juce::String serializeMixerGraphState (const InputMixerGraphState& s)
{
    return serializeMixerDoc (s.buses, s.channels, s.nextBusId, s.nextChannelId,
                              [] (const InputChannelState& c) { return inputChannelToVar (c); });
}

juce::String serializeMixerGraphState (const OutputMixerGraphState& s)
{
    return serializeMixerDoc (s.buses, s.channels, s.nextBusId, s.nextChannelId,
                              [] (const OutputChannelState& c) { return outputChannelToVar (c); });
}

InputMixerGraphState deserializeInputMixerGraphState (const juce::String& json)
{
    const auto root = parseMixerDoc (json);
    InputMixerGraphState s;
    if (const auto buses = optionalProperty (root, "buses"); buses.isArray())
        for (int i = 0; i < buses.size(); ++i) s.buses.push_back (busStateFromVar (buses[i]));
    if (const auto chans = optionalProperty (root, "channels"); chans.isArray())
        for (int i = 0; i < chans.size(); ++i) s.channels.push_back (inputChannelFromVar (chans[i]));
    if (const auto n = optionalProperty (root, "nextBusId");     ! n.isVoid()) s.nextBusId = requireInt64 (n, "nextBusId");
    if (const auto n = optionalProperty (root, "nextChannelId"); ! n.isVoid()) s.nextChannelId = requireInt64 (n, "nextChannelId");
    return s;
}

OutputMixerGraphState deserializeOutputMixerGraphState (const juce::String& json)
{
    const auto root = parseMixerDoc (json);
    OutputMixerGraphState s;
    if (const auto buses = optionalProperty (root, "buses"); buses.isArray())
        for (int i = 0; i < buses.size(); ++i) s.buses.push_back (busStateFromVar (buses[i]));
    if (const auto chans = optionalProperty (root, "channels"); chans.isArray())
        for (int i = 0; i < chans.size(); ++i) s.channels.push_back (outputChannelFromVar (chans[i]));
    if (const auto n = optionalProperty (root, "nextBusId");     ! n.isVoid()) s.nextBusId = requireInt64 (n, "nextBusId");
    if (const auto n = optionalProperty (root, "nextChannelId"); ! n.isVoid()) s.nextChannelId = requireInt64 (n, "nextChannelId");
    return s;
}

namespace
{
    // Wire-stable tokens for TapeColorMode (TAPECOLOR Slice 2). Changing
    // these strings is an on-disk format break — projects saved with the
    // old token will fail to load.
    const char* tapeColorModeToken (TapeColorMode mode) noexcept
    {
        switch (mode)
        {
            case TapeColorMode::None:        return "None";
            case TapeColorMode::BeforeWrite: return "BeforeWrite";
            case TapeColorMode::AfterRead:   return "AfterRead";
        }
        return "None"; // unreachable; defensive
    }

    TapeColorMode tapeColorModeFromString (const juce::String& s)
    {
        if (s == "None")        return TapeColorMode::None;
        if (s == "BeforeWrite") return TapeColorMode::BeforeWrite;
        if (s == "AfterRead")   return TapeColorMode::AfterRead;
        fail (std::string ("unknown tape_color \"") + s.toStdString() + "\"");
    }
}

juce::String serializeTapePool (const TapePool& pool)
{
    juce::Array<juce::var> tapeArr;
    for (const auto& t : pool.tapes())
    {
        auto obj = makeObject();
        obj->setProperty ("id",         juce::int64 (t.id.value()));
        obj->setProperty ("name",       juce::String (t.name));
        obj->setProperty ("tape_color", juce::String (tapeColorModeToken (t.tapeColor)));
        tapeArr.add (objectVar (obj));
    }
    auto root = makeObject();
    root->setProperty ("tapes", tapeArr);
    return juce::JSON::toString (objectVar (root));
}

TapePool deserializeTapePool (const juce::String& json)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);
    if (result.failed())
        fail ("invalid tape pool JSON: " + result.getErrorMessage().toStdString());
    if (! parsed.isObject())
        fail ("tape pool document must be a JSON object");

    const auto tapes = requireProperty (parsed, "tapes");
    if (! tapes.isArray() || tapes.size() == 0)
        fail ("tape pool must carry a non-empty tapes array");

    std::vector<TapeDescriptor> descriptors;
    descriptors.reserve (static_cast<std::size_t> (tapes.size()));
    for (int i = 0; i < tapes.size(); ++i)
    {
        const auto& entry = tapes[i];

        // tape_color is optional for back-compat: projects saved before
        // TAPECOLOR Slice 2 have no such field and must reload as None.
        TapeColorMode mode = TapeColorMode::None;
        if (const auto tc = optionalProperty (entry, "tape_color"); ! tc.isVoid())
            mode = tapeColorModeFromString (tc.toString());

        descriptors.push_back (TapeDescriptor {
            TapeId (requireInt64 (requireProperty (entry, "id"), "tape.id")),
            requireProperty (entry, "name").toString().toStdString(),
            mode });
    }
    return TapePool (std::move (descriptors));
}

juce::String serializePhraseChannelMap (
    const std::vector<std::pair<std::int64_t, std::int64_t>>& entries)
{
    juce::Array<juce::var> arr;
    for (const auto& [cid, chId] : entries)
    {
        auto obj = makeObject();
        obj->setProperty ("constituent_id",     juce::int64 (cid));
        obj->setProperty ("output_channel_id",  juce::int64 (chId));
        arr.add (objectVar (obj));
    }
    auto root = makeObject();
    root->setProperty ("entries", arr);
    return juce::JSON::toString (objectVar (root));
}

std::vector<std::pair<std::int64_t, std::int64_t>> deserializePhraseChannelMap (
    const juce::String& json)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);
    if (result.failed())
        fail ("invalid phrase-channel map JSON: " + result.getErrorMessage().toStdString());
    if (! parsed.isObject())
        fail ("phrase-channel map document must be a JSON object");

    const auto entries = requireProperty (parsed, "entries");
    if (! entries.isArray())
        fail ("phrase-channel map must carry an entries array");

    std::vector<std::pair<std::int64_t, std::int64_t>> out;
    out.reserve (static_cast<std::size_t> (entries.size()));
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        out.emplace_back (
            requireInt64 (requireProperty (entry, "constituent_id"),    "constituent_id"),
            requireInt64 (requireProperty (entry, "output_channel_id"), "output_channel_id"));
    }
    return out;
}

} // namespace ida::persistence
