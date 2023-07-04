#pragma once

#include "AudioGraphNode.h"
#include "Core/Container/AdjacencyList.h"
#include "Core/Primitive/Float.h"
#include "Project/Audio/Faust/FaustNode.h" // xxx should not depend on any specific node types

struct ma_device;
struct ma_node_graph;

// Corresponds to `ma_node_graph`.
struct AudioGraph : Component, Field::ChangeListener {
    AudioGraph(ComponentArgs &&);
    ~AudioGraph();

    void OnFieldChanged() override;

    void Update();

    static void AudioCallback(ma_device *, void *output, const void *input, Count frame_count);

    ma_node_graph *Get() const;

    struct InputNode : AudioGraphNode {
        using AudioGraphNode::AudioGraphNode;

        void DoInit() override;
        void DoUninit() override;
    };

    struct Nodes : Component {
        Nodes(ComponentArgs &&);
        ~Nodes();

        // Iterate over all children, converting each element from a `Component *` to a `Node *`.
        // Usage: `for (const Node *node : Nodes) ...`
        struct Iterator : std::vector<Component *>::const_iterator {
            Iterator(auto it) : std::vector<Component *>::const_iterator(it) {}
            const AudioGraphNode *operator*() const { return static_cast<const AudioGraphNode *>(std::vector<Component *>::const_iterator::operator*()); }
            AudioGraphNode *operator*() { return static_cast<AudioGraphNode *>(std::vector<Component *>::const_iterator::operator*()); }
        };
        Iterator begin() const { return Children.cbegin(); }
        Iterator end() const { return Children.cend(); }

        Iterator begin() { return Children.begin(); }
        Iterator end() { return Children.end(); }

        void Init();
        void Update();
        void Uninit();

        const AudioGraph *Graph;

        // `ma_data_source_node` whose `ma_data_source` is a `ma_audio_buffer_ref` pointing directly to the input buffer.
        // todo configurable data source
        Prop(InputNode, Input);
        Prop(FaustNode, Faust);
        Prop(AudioGraphNode, Output);

    private:
        void Render() const override;
    };

    struct Style : Component {
        using Component::Component;

        struct Matrix : Component {
            using Component::Component;

            Prop_(Float, CellSize, "?The size of each matrix cell, as a multiple of line height.", 1, 1, 3);
            Prop_(Float, CellGap, "?The gap between matrix cells.", 1, 0, 10);
            Prop_(Float, LabelSize, "?The space provided for the label, as a multiple of line height.\n(Use Style->ImGui->InnerItemSpacing->X for spacing between labels and cells.)", 6, 3, 8);

        protected:
            void Render() const override;
        };

        Prop(Matrix, Matrix);
    };

    Prop(Nodes, Nodes);
    Prop(AdjacencyList, Connections);
    Prop(Style, Style);

private:
    void Init();
    void Uninit();
    void UpdateConnections();

    void Render() const override;
    void RenderConnections() const;
};