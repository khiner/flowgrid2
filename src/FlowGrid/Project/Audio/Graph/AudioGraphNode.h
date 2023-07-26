#pragma once

#include "Core/Primitive/Bool.h"
#include "Core/Primitive/Enum.h"
#include "Core/Primitive/Float.h"
#include "Project/Audio/AudioIO.h"

using ma_node = void;

struct AudioGraph;

struct ma_node_graph;

enum WindowType_ {
    WindowType_Rectangular,
    // Cosine windows.
    WindowType_Hann,
    WindowType_Hamming,
    WindowType_Blackman,
    WindowType_BlackmanHarris,
    WindowType_Nuttall,
    WindowType_FlatTop,
    // Other windows, not parameterized.
    WindowType_Triangular,
    WindowType_Bartlett,
    WindowType_BartlettHann,
    WindowType_Bohman,
    WindowType_Parzen,
    // Other windows, parameterized.
    // We have implementations for these, but we're sticking with non-parameterized windows for now.
    // WindowType_Gaussian,
    // WindowType_Tukey,
    // WindowType_Taylor,
    // WindowType_Kaiser,
};

using WindowType = int;

// Corresponds to `ma_node`.
// This base `Node` can either be specialized or instantiated on its own.
struct AudioGraphNode : Component, Field::ChangeListener {
    AudioGraphNode(ComponentArgs &&);
    virtual ~AudioGraphNode();

    struct Listener {
        // This allows for the parent graph to respond to changes in the graph topology _after_ the node has updated its internal state.
        virtual void OnNodeConnectionsChanged(AudioGraphNode *) = 0;
    };
    inline void RegisterListener(Listener *listener) noexcept { Listeners.insert(listener); }
    inline void UnregisterListener(Listener *listener) noexcept { Listeners.erase(listener); }

    void OnFieldChanged() override;

    // If `Allow...ConnectionChange` returns `true`, users can dynamically change the input/output connections.
    // Nodes whose connections are managed and enforced by the `AudioGraph` return `false` (the graph endpoint node and device IO nodes).
    virtual bool AllowInputConnectionChange() const { return true; }
    virtual bool AllowOutputConnectionChange() const { return true; }
    inline bool CanConnectInput() const { return AllowInputConnectionChange() && InputBusCount() > 0; }
    inline bool CanConnectOutput() const { return AllowOutputConnectionChange() && OutputBusCount() > 0; }

    u32 InputBusCount() const;
    u32 OutputBusCount() const;
    inline u32 BusCount(IO io) const { return io == IO_In ? InputBusCount() : OutputBusCount(); }

    u32 InputChannelCount(u32 bus) const;
    u32 OutputChannelCount(u32 bus) const;
    inline u32 ChannelCount(IO io, u32 bus) const { return io == IO_In ? InputChannelCount(bus) : OutputChannelCount(bus); }

    // An `AudioGraphNode` may be composed of multiple inner `ma_node`s.
    // These return the graph-visible I/O nodes.
    ma_node *InputNode() const;
    ma_node *OutputNode() const;

    void ConnectTo(AudioGraphNode &);
    void DisconnectAll();

    // The graph is responsible for calling this method whenever the topology of the graph changes.
    // When this node is no longer connected to the output node (directly or indirectly), it is considered inactive.
    inline void SetActive(bool is_active) noexcept { IsActive = is_active; }

    void SetMuted(bool muted) {
        Muted.Set_(muted);
        UpdateOutputLevel();
    }

    // Should be called whenever the graph's sample rate changes.
    // At the very least, each node updates any active IO monitors based on the new sample rate.
    virtual void OnSampleRateChanged();

    // These getters delegate to Graph->Device.
    u32 GetSampleRate() const;
    u32 GetBufferSize() const;

    Prop_(Bool, Muted, "?Mute the node. This does not affect CPU load.", false);
    Prop(Float, OutputLevel, 1.0);
    Prop(Bool, SmoothOutputLevel, true);
    Prop(Float, SmoothOutputLevelMs, 30);

    Prop_(Bool, Monitor, "?Plot the node's most recent input/output buffer(s).", false);
    Prop_(
        Enum, WindowType, "?The window type used for the magnitude spectrum FFT.",
        {"Rectangular", "Hann", "Hamming", "Blackman", "Blackman-Harris", "Nuttall", "Flat-Top", "Triangular", "Bartlett", "Bartlett-Hann", "Bohman", "Parzen"},
        WindowType_BlackmanHarris
    );

    struct GainerNode;
    std::unique_ptr<GainerNode> Gainer;

    struct SplitterNode;
    std::vector<std::unique_ptr<SplitterNode>> Splitters;

    struct MonitorNode;
    std::unique_ptr<MonitorNode> InputMonitor, OutputMonitor;

    // These fields are derived from graph connections and are updated via `AudioGraph::UpdateConnections()`.
    bool IsActive{false}; // `true` means the audio device is on and there is a connection path from this node to the graph endpoint node (`OutputNode`).
    std::unordered_set<const AudioGraphNode *> InputNodes, OutputNodes; // Cache the current sets of input and output nodes.

protected:
    virtual void UpdateAll(); // Call corresponding MA setters for all fields.

    void Render() const override;

    MonitorNode *GetMonitor(IO) const;

    void UpdateOutputLevel();
    void UpdateGainer();
    void UpdateMonitor(IO);
    void UpdateMonitorWindowFunction(IO);

    void Uninit();

    const AudioGraph *Graph;
    ma_node *Node;
    std::unordered_set<Listener *> Listeners{};
};
