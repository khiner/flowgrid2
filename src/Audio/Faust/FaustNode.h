#pragma once

// An audio graph node that uses Faust to generate audio, not to be confused with Faust's graph UI nodes (in `FaustGraphs`).

#include "Audio/Graph/AudioGraphNode.h"

class dsp;

struct FaustNode : AudioGraphNode {
    FaustNode(ComponentArgs &&, ID dsp_id = 0);

    void OnSampleRateChanged() override;

    ID GetDspId() const;
    void SetDsp(TransientStore &, ID);

private:
    std::unique_ptr<MaNode> CreateNode(ID dsp_id = 0);
};
