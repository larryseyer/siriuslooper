#pragma once

namespace ida
{

/// Severity of a `Notification`. The four levels are the V5 §8.6 contract.
///
/// **Lives in `core/` (not `engine/`) so headers under JUCE-free libraries
/// — `host/` in particular — can name the enumerators without pulling the
/// engine layer into their dependency graph.** The concrete
/// `NotificationBus` (in `engine/`) re-exports this enum from its own
/// header so existing call sites continue to write `ida::NotificationLevel`
/// unchanged.
enum class NotificationLevel
{
    Info,
    Degradation,
    Warning,
    Error
};

/// Truthfulness-channel category. One SPSC ring per category prevents
/// priority inversion. The list mirrors V5 §8.6 exactly — do not extend
/// without updating the white paper and the per-category storage in
/// `NotificationBus`.
///
/// Lives in `core/` for the same dependency-inversion reason as
/// `NotificationLevel` above. `engine/include/ida/NotificationBus.h`
/// re-exports the enumerators so call sites keep writing
/// `ida::Category::PluginEvent` (etc.) unchanged.
enum class Category
{
    DiskPressure,
    CpuPressure,
    RamPressure,
    DeviceEvent,
    PluginEvent,
    ClockEvent,
    NetworkEvent,
    StateRepair,
    TapeRotation
};

/// Dependency-inverted port for "thing that can receive a notification
/// post" (M7 S4). The engine's `NotificationBus` is the production
/// implementation; tests inject lightweight recorders.
///
/// This mirrors the `IEffectChainHost` port-and-adapter shape introduced
/// in M7 S3: `host/` collaborators that need to emit notifications take an
/// `INotificationSink*` and stay JUCE-free + engine-free. The engine
/// owns the concrete `NotificationBus`, implements this interface, and
/// hands a pointer to the host-side collaborator at message-thread
/// construction time.
///
/// `post(...)` must be `noexcept`, allocation-free, and lock-free per the
/// same V5 §8.6 contract the bus already satisfies — implementations that
/// cannot meet it are not safe to use from the audio thread or from
/// supervisor/watchdog threads that interact with audio-thread state.
class INotificationSink
{
public:
    virtual ~INotificationSink() = default;

    /// Audio-thread / supervisor-thread safe post. Returns true if the
    /// notification was accepted; false if the underlying ring was full or
    /// the sink rejected the post for any reason. `message` may be null —
    /// implementations must treat null as an empty string.
    virtual bool post (NotificationLevel level,
                       Category          category,
                       const char*       message) noexcept = 0;
};

} // namespace ida
