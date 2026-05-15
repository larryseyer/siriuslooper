#include "sirius/SessionFormat.h"

#include "sirius/EffectChain.h"
#include "sirius/Meter.h"
#include "sirius/Phrase.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/RepetitionRules.h"
#include "sirius/TapeReference.h"
#include "sirius/TempoMap.h"

#include <stdexcept>
#include <string>
#include <variant>

namespace sirius::persistence
{

namespace
{
    constexpr int currentVersion = 1;

    // --- error reporting ------------------------------------------------------

    [[noreturn]] void fail (const std::string& message)
    {
        throw std::runtime_error ("sirius::persistence::SessionFormat: " + message);
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

    juce::var pluginDescriptorToVar (const PluginDescriptor& d)
    {
        auto obj = makeObject();
        obj->setProperty ("format",       pluginFormatToString (d.format));
        obj->setProperty ("uniqueId",     juce::String (d.uniqueId));
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
        d.name         = requireProperty (v, "name").toString().toStdString();
        d.manufacturer = requireProperty (v, "manufacturer").toString().toStdString();
        d.filePath     = requireProperty (v, "filePath").toString().toStdString();
        return d;
    }

    juce::var effectChainEntryToVar (const EffectChainEntry& e)
    {
        auto obj = makeObject();
        obj->setProperty ("plugin",      pluginDescriptorToVar (e.descriptor));
        obj->setProperty ("displayName", juce::String (e.displayName));
        obj->setProperty ("state",       juce::String (e.stateBase64));
        obj->setProperty ("bypassed",    e.bypassed);
        return objectVar (obj);
    }

    EffectChainEntry effectChainEntryFromVar (const juce::var& v)
    {
        EffectChainEntry e;
        e.descriptor  = pluginDescriptorFromVar (requireProperty (v, "plugin"));
        e.displayName = requireProperty (v, "displayName").toString().toStdString();
        e.stateBase64 = requireProperty (v, "state").toString().toStdString();
        e.bypassed    = bool (requireProperty (v, "bypassed"));
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

    juce::var constituentToVar (const Constituent& c)
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
            kids.add (constituentToVar (*child));
        obj->setProperty ("children", kids);

        return objectVar (obj);
    }

    Constituent constituentFromVar (const juce::var& v)
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
            c = c.withChildAdded (std::make_shared<const Constituent> (constituentFromVar (kids[i])));

        return c;
    }
}

juce::String serializeSession (const Constituent& root)
{
    auto top = makeObject();
    top->setProperty ("version", currentVersion);
    top->setProperty ("root", constituentToVar (root));
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
        fail ("unsupported session version: " + std::to_string (version));

    return std::make_shared<const Constituent> (
        constituentFromVar (requireProperty (document, "root")));
}

} // namespace sirius::persistence
