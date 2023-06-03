#include "Audio.h"

#include "Sample.h" // Must be included before any Faust includes
#include "faust/dsp/llvm-dsp.h"

#include <range/v3/core.hpp>
#include <range/v3/view/transform.hpp>

#include "miniaudio.h"

#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"

#include "App/FileDialog/FileDialog.h"
#include "Faust/FaustBox.h"
#include "Faust/FaustParams.h"
#include "Helper/File.h"
#include "Helper/String.h"
#include "UI/Widgets.h"

static const std::string FaustDspFileExtension = ".dsp";

// todo implement for r8brain resampler
// todo I want to use this currently to support quality/fast resampling between _natively supported_ device sample rates.
//   Can I still use duplex mode in this case?
// #include "CDSPResampler.h"
// See https://github.com/avaneev/r8brain-free-src/issues/12 for resampling latency calculation
// static unique_ptr<r8b::CDSPResampler24> Resampler;
// int resampled_frames = Resampler->process(read_ptr, available_resample_read_frames, resampled_buffer);
// Set up resampler if needed.
// if (InStream->sample_rate != OutStream->sample_rate) {
// Resampler = make_unique<r8b::CDSPResampler24>(InStream->sample_rate, OutStream->sample_rate, 1024); // todo can we get max frame size here?
// }
// static ma_resampling_backend_vtable ResamplerVTable = {
//     ma_resampling_backend_get_heap_size__linear,
//     ma_resampling_backend_init__linear,
//     ma_resampling_backend_uninit__linear,
//     ma_resampling_backend_process__linear,
//     ma_resampling_backend_set_rate__linear,
//     ma_resampling_backend_get_input_latency__linear,
//     ma_resampling_backend_get_output_latency__linear,
//     ma_resampling_backend_get_required_input_frame_count__linear,
//     ma_resampling_backend_get_expected_output_frame_count__linear,
//     ma_resampling_backend_reset__linear,
// };

void Audio::Apply(const Action::AudioAction &action) const {
    using namespace Action;
    Match(
        action,
        [&](const SetGraphColorStyle &a) {
            switch (a.id) {
                case 0: return Faust.Graph.Style.ColorsDark();
                case 1: return Faust.Graph.Style.ColorsLight();
                case 2: return Faust.Graph.Style.ColorsClassic();
                case 3: return Faust.Graph.Style.ColorsFaust();
            }
        },
        [&](const SetGraphLayoutStyle &a) {
            switch (a.id) {
                case 0: return Faust.Graph.Style.LayoutFlowGrid();
                case 1: return Faust.Graph.Style.LayoutFaust();
            }
        },
        [&](const ShowOpenFaustFileDialog &) { file_dialog.Set({"Choose file", FaustDspFileExtension, ".", ""}); },
        [&](const ShowSaveFaustFileDialog &) { file_dialog.Set({"Choose file", FaustDspFileExtension, ".", "my_dsp", true, 1}); },
        [&](const ShowSaveFaustSvgFileDialog &) { file_dialog.Set({"Choose directory", ".*", ".", "faust_graph", true, 1}); },
        [&](const OpenFaustFile &a) { store::Set(Faust.Code, FileIO::read(a.path)); },
        [&](const SaveFaustFile &a) { FileIO::write(a.path, audio.Faust.Code); },
        [](const SaveFaustSvgFile &a) { SaveBoxSvg(a.path); },
    );
}

// static ma_resampler_config ResamplerConfig;
// static ma_resampler Resampler;

// todo draw debug info for all devices, not just current
//  void DrawDevices() {
//      for (const IO io : IO_All) {
//          const Count device_count = GetDeviceCount(io);
//          if (TreeNodeEx(std::format("{} devices ({})", Capitalize(to_string(io)), device_count).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
//              for (Count device_index = 0; device_index < device_count; device_index++) {
//                  auto *device = GetDevice(io, device_index);
//                  ShowDevice(*device);
//              }
//              TreePop();
//          }
//      }
//  }

// void ShowBufferPlots() {
//     for (IO io : IO_All) {
//         const bool is_in = io == IO_In;
//         if (TreeNode(Capitalize(to_string(io)).c_str())) {
//             const auto *area = is_in ? Areas[IO_In] : Areas[IO_Out];
//             if (!area) continue;

//             const auto *device = is_in ? InStream->device : OutStream->device;
//             const auto &layout = is_in ? InStream->layout : OutStream->layout;
//             const auto frame_count = is_in ? LastReadFrameCount : LastWriteFrameCount;
//             if (ImPlot::BeginPlot(device->name, {-1, 160})) {
//                 ImPlot::SetupAxes("Sample index", "Value");
//                 ImPlot::SetupAxisLimits(ImAxis_X1, 0, frame_count, ImGuiCond_Always);
//                 ImPlot::SetupAxisLimits(ImAxis_Y1, -1, 1, ImGuiCond_Always);
//                 for (int channel_index = 0; channel_index < layout.channel_count; channel_index++) {
//                     const auto &channel = layout.channels[channel_index];
//                     const char *channel_name = soundio_get_channel_name(channel);
//                     // todo Adapt the pointer casting to the sample format.
//                     //  Also, this works but very scary and I can't even justify why this seems to work so well,
//                     //  since the area pointer position gets updated in the separate read/write callbacks.
//                     //  Hrm.. are the start points of each channel area static after initializing the stream?
//                     //  If so, could just set those once on stream init and use them here!
//                     ImPlot::PlotLine(channel_name, (Sample *)area[channel_index].ptr, frame_count);
//                 }
//                 ImPlot::EndPlot();
//             }
//             TreePop();
//         }
//     }
// }

void Audio::Render() const {
    Update();
    TabsWindow::Render({Faust.Id}); // Exclude the Faust tab.

    static string PrevSelectedPath = "";
    if (PrevSelectedPath != file_dialog.SelectedFilePath) {
        const fs::path selected_path = string(file_dialog.SelectedFilePath);
        const string &extension = selected_path.extension();
        if (extension == FaustDspFileExtension) {
            if (file_dialog.SaveMode) Action::SaveFaustFile{selected_path}.q();
            else Action::OpenFaustFile{selected_path}.q();
        } else if (extension == ".svg") {
            if (file_dialog.SaveMode) Action::SaveFaustSvgFile{selected_path}.q();
        }
        PrevSelectedPath = selected_path;
    }
}

namespace FaustContext {
static dsp *Dsp = nullptr;
static std::unique_ptr<FaustParams> Ui;

static void Init() {
    if (Dsp || !audio.Faust.IsReady()) return;

    createLibContext();

    const char *libraries_path = fs::relative("../lib/faust/libraries").c_str();
    vector<const char *> argv = {"-I", libraries_path};
    if (std::is_same_v<Sample, double>) argv.push_back("-double");

    const int argc = argv.size();
    static int num_inputs, num_outputs;
    static string error_msg;
    const Box box = DSPToBoxes("FlowGrid", audio.Faust.Code, argc, argv.data(), &num_inputs, &num_outputs, error_msg);

    static llvm_dsp_factory *dsp_factory;
    if (box && error_msg.empty()) {
        static const int optimize_level = -1;
        dsp_factory = createDSPFactoryFromBoxes("FlowGrid", box, argc, argv.data(), "", error_msg, optimize_level);
    }
    if (!box && error_msg.empty()) error_msg = "`DSPToBoxes` returned no error but did not produce a result.";

    if (dsp_factory && error_msg.empty()) {
        Dsp = dsp_factory->createDSPInstance();
        if (!Dsp) error_msg = "Could not create Faust DSP.";
        else {
            Ui = std::make_unique<FaustParams>();
            Dsp->buildUserInterface(Ui.get());
            // `Dsp->Init` happens in the Faust graph node.
        }
    }

    const auto &ErrorLog = audio.Faust.Log.Error;
    if (!error_msg.empty()) Action::SetValue{ErrorLog.Path, error_msg}.q();
    else if (ErrorLog) Action::SetValue{ErrorLog.Path, ""}.q();

    OnBoxChange(box);
    OnUiChange(Ui.get());
}
static void Uninit() {
    OnBoxChange(nullptr);
    OnUiChange(nullptr);

    Ui = nullptr;
    if (Dsp) {
        delete Dsp;
        Dsp = nullptr;
        deleteAllDSPFactories(); // There should only be one factory, but using this instead of `deleteDSPFactory` avoids storing another file-scoped variable.
    }

    destroyLibContext();
}

static void Update() {
    const bool ready = audio.Faust.IsReady();
    const bool needs_restart = audio.Faust.NeedsRestart(); // Don't inline! Must run during every update.
    if (!Dsp && ready) {
        Init();
    } else if (Dsp && !ready) {
        Uninit();
    } else if (needs_restart) {
        Uninit();
        Init();
    }
}
} // namespace FaustContext

// todo support loopback mode? (think of use cases)

static ma_node_graph NodeGraph;
static ma_node_graph_config NodeGraphConfig;
static ma_audio_buffer_ref InputBuffer;

void AudioCallback(ma_device *device, void *output, const void *input, ma_uint32 frame_count) {
    ma_audio_buffer_ref_set_data(&InputBuffer, input, frame_count);
    ma_node_graph_read_pcm_frames(&NodeGraph, output, frame_count, nullptr);
    (void)device; // unused
}

// todo explicit re-scan action.
void Audio::Init() const {
    Device.Init(AudioCallback);
    Graph.Init();
    Device.Start();

    FaustContext::Init();
    NeedsRestart(); // xxx Updates cached values as side effect.
}

void Audio::Uninit() const {
    FaustContext::Uninit();
    Device.Stop();
    Graph.Uninit();
    Device.Uninit();
}

void Audio::Update() const {
    FaustContext::Update();

    const bool is_initialized = Device.IsStarted();
    const bool needs_restart = NeedsRestart(); // Don't inline! Must run during every update.
    if (Device.On && !is_initialized) {
        Init();
    } else if (!Device.On && is_initialized) {
        Uninit();
    } else if (needs_restart && is_initialized) {
        // todo no need to completely reset in many cases (like when only format has changed) - just modify as needed in `Device::Update`.
        // todo sample rate conversion is happening even when choosing a SR that is native to both intpu & output, if it's not the highest-priority SR.
        Uninit();
        Init();
    }

    Device.Update();

    if (Device.IsStarted()) Graph.Update();
}

bool Audio::NeedsRestart() const {
    static string PreviousInDeviceName = Device.InDeviceName, PreviousOutDeviceName = Device.OutDeviceName;
    static int PreviousInFormat = Device.InFormat, PreviousOutFormat = Device.OutFormat;
    static U32 PreviousInChannels = Device.InChannels, PreviousOutChannels = Device.OutChannels;
    static U32 PreviousSampleRate = Device.SampleRate;

    const bool needs_restart =
        PreviousInDeviceName != Device.InDeviceName ||
        PreviousOutDeviceName != Device.OutDeviceName ||
        PreviousInFormat != Device.InFormat || PreviousOutFormat != Device.OutFormat ||
        PreviousInChannels != Device.InChannels || PreviousOutChannels != Device.OutChannels ||
        PreviousSampleRate != Device.SampleRate;

    PreviousInDeviceName = Device.InDeviceName;
    PreviousOutDeviceName = Device.OutDeviceName;
    PreviousInFormat = Device.InFormat;
    PreviousOutFormat = Device.OutFormat;
    PreviousInChannels = Device.InChannels;
    PreviousOutChannels = Device.OutChannels;
    PreviousSampleRate = Device.SampleRate;

    return needs_restart;
}

void AudioGraph::Init() const {
    NodeGraphConfig = ma_node_graph_config_init(audio.Device.InChannels);
    int result = ma_node_graph_init(&NodeGraphConfig, nullptr, &NodeGraph);
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Failed to initialize node graph: {}", result));

    Nodes.Init();
    vector<Primitive> connections{};
    Count dest_count = 0;
    for (const auto *dest_node : Nodes) {
        if (!dest_node->IsDestination()) continue;
        for (const auto *source_node : Nodes) {
            if (!source_node->IsSource()) continue;
            const bool default_connected =
                (source_node == &Nodes.Input && dest_node == &Nodes.Faust) ||
                (source_node == &Nodes.Faust && dest_node == &Nodes.Output);
            connections.push_back(default_connected);
        }
        dest_count++;
    }
    Action::SetMatrix{Connections.Path, connections, dest_count}.q(true);
}

void AudioGraph::Update() const {
    Nodes.Update();

    // Setting up busses is idempotent.
    Count source_i = 0;
    for (const auto *source_node : Nodes) {
        if (!source_node->IsSource()) continue;
        ma_node_detach_output_bus(source_node->Get(), 0); // No way to just detach one connection.
        Count dest_i = 0;
        for (const auto *dest_node : Nodes) {
            if (!dest_node->IsDestination()) continue;
            if (Connections(dest_i, source_i)) {
                ma_node_attach_output_bus(source_node->Get(), 0, dest_node->Get(), 0);
            }
            dest_i++;
        }
        source_i++;
    }
}
void AudioGraph::Uninit() const {
    Nodes.Uninit();
    // ma_node_graph_uninit(&NodeGraph, nullptr); // Graph endpoint is already uninitialized in `Nodes.Uninit`.
}

void AudioGraph::Nodes::Init() const {
    Output.Set(ma_node_graph_get_endpoint(&NodeGraph)); // Output is present whenever the graph is running. todo Graph is a Node
    for (const auto *node : *this) node->Init();
}
void AudioGraph::Nodes::Update() const {
    for (const auto *node : *this) node->Update();
}
void AudioGraph::Nodes::Uninit() const {
    for (const auto *node : *this) node->Uninit();
}

// Output node is already allocated by the MA graph, so we don't need to track internal data for it.
void AudioGraph::InputNode::DoInit() const {
    int result = ma_audio_buffer_ref_init((ma_format) int(audio.Device.InFormat), audio.Device.InChannels, nullptr, 0, &InputBuffer);
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Failed to initialize input audio buffer: ", result));

    static ma_data_source_node Node{};
    static ma_data_source_node_config Config{};

    Config = ma_data_source_node_config_init(&InputBuffer);
    result = ma_data_source_node_init(&NodeGraph, &Config, nullptr, &Node);
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Failed to initialize the input node: ", result));

    Set(&Node);
}
void AudioGraph::InputNode::DoUninit() const {
    ma_data_source_node_uninit((ma_data_source_node *)Get(), nullptr);
    ma_audio_buffer_ref_uninit(&InputBuffer);
}

void FaustProcess(ma_node *node, const float **const_bus_frames_in, ma_uint32 *frame_count_in, float **bus_frames_out, ma_uint32 *frame_count_out) {
    // ma_pcm_rb_init_ex()
    // ma_deinterleave_pcm_frames()
    float **bus_frames_in = const_cast<float **>(const_bus_frames_in); // Faust `compute` expects a non-const buffer: https://github.com/grame-cncm/faust/pull/850
    if (FaustContext::Dsp) FaustContext::Dsp->compute(*frame_count_out, bus_frames_in, bus_frames_out);

    (void)node; // unused
    (void)frame_count_in; // unused
}

void AudioGraph::FaustNode::DoInit() const {
    if (!FaustContext::Dsp) return;

    FaustContext::Dsp->init(audio.Device.SampleRate);
    const Count in_channels = FaustContext::Dsp->getNumInputs();
    const Count out_channels = FaustContext::Dsp->getNumOutputs();
    if (in_channels == 0 && out_channels == 0) return;

    static ma_node_vtable vtable{};
    vtable = {FaustProcess, nullptr, ma_uint8(in_channels > 0 ? 1 : 0), ma_uint8(out_channels > 0 ? 1 : 0), 0};

    static ma_node_config config;
    config = ma_node_config_init();

    config.pInputChannels = &in_channels; // One input bus with N channels.
    config.pOutputChannels = &out_channels; // One output bus with M channels.
    config.vtable = &vtable;

    static ma_node_base node{};
    const int result = ma_node_init(&NodeGraph, &config, nullptr, &node);
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Failed to initialize the Faust node: {}", result));

    Set(&node);
}
bool AudioGraph::FaustNode::NeedsRestart() const {
    static dsp *PreviousDsp = FaustContext::Dsp;
    static U32 PreviousSampleRate = audio.Device.SampleRate;

    const bool needs_restart = FaustContext::Dsp != PreviousDsp || audio.Device.SampleRate != PreviousSampleRate;
    PreviousDsp = FaustContext::Dsp;
    PreviousSampleRate = audio.Device.SampleRate;

    return needs_restart;
}

using namespace ImGui;

void Audio::Style::Render() const {
    if (BeginTabBar("Style")) {
        if (BeginTabItem("Matrix mixer", nullptr, ImGuiTabItemFlags_NoPushId)) {
            audio.Graph.Style.Matrix.Draw();
            EndTabItem();
        }
        if (BeginTabItem("Faust graph", nullptr, ImGuiTabItemFlags_NoPushId)) {
            audio.Faust.Graph.Style.Draw();
            EndTabItem();
        }
        if (BeginTabItem("Faust params", nullptr, ImGuiTabItemFlags_NoPushId)) {
            audio.Faust.Params.Style.Draw();
            EndTabItem();
        }
        EndTabBar();
    }
}
