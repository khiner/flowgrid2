#include "AudioDevice.h"

#include "imgui.h"
#include "miniaudio.h"

#include "Helper/String.h"

// Copied from `miniaudio.c::g_maStandardSampleRatePriorities`.
const std::vector<u32> AudioDevice::PrioritizedSampleRates = {
    ma_standard_sample_rate_48000,
    ma_standard_sample_rate_44100,

    ma_standard_sample_rate_32000,
    ma_standard_sample_rate_24000,
    ma_standard_sample_rate_22050,

    ma_standard_sample_rate_88200,
    ma_standard_sample_rate_96000,
    ma_standard_sample_rate_176400,
    ma_standard_sample_rate_192000,

    ma_standard_sample_rate_16000,
    ma_standard_sample_rate_11025,
    ma_standard_sample_rate_8000,

    ma_standard_sample_rate_352800,
    ma_standard_sample_rate_384000,
};

static std::unordered_map<IO, std::vector<ma_format>> NativeFormats;
static std::unordered_map<IO, std::vector<u32>> NativeSampleRates;

static ma_context AudioContext;
static u16 AudioContextInitializedCount = 0;

static std::vector<ma_device_info *> DeviceInfos[IO_Count];
static std::vector<string> DeviceNames[IO_Count];

// The native sample rate list may have a different prioritized order than our priority list.
// If `sample_rate_target == 0`, returns the the highest-priority sample rate that is also native to the device,
// If `sample_rate_target != 0`, returns the provided `sample_rate_target` if it is natively supported, or the first native sample rate otherwise.
// Assumes `NativeSampleRates` has already been populated.
static u32 GetHighestPriorityNativeSampleRate(IO type, u32 sample_rate_target) {
    if (!NativeSampleRates.contains(type) || NativeSampleRates.at(type).empty()) {
        throw std::runtime_error("No native sample rates found. Perhaps `InitContext` was not called before calling `GetHighestPriorityNativeSampleRate`?");
    }

    const auto &native_sample_rates = NativeSampleRates.at(type);
    if (sample_rate_target == 0) { // Default.
        // By default, we want to choose the highest-priority sample rate that is native to the device.
        for (u32 sample_rate : AudioDevice::PrioritizedSampleRates) {
            if (std::find(native_sample_rates.begin(), native_sample_rates.end(), sample_rate) != native_sample_rates.end()) return sample_rate;
        }
    } else {
        // Specific sample rate requested.
        if (std::find(native_sample_rates.begin(), native_sample_rates.end(), sample_rate_target) != native_sample_rates.end()) return sample_rate_target;
    }

    // Either a specific (non-default) sample rate is configured that's not natively supported,
    // or the device doesn't natively support any of the prioritized sample rates.
    // We return the first native sample rate.
    return native_sample_rates[0];
}

AudioDevice::AudioDevice(ComponentArgs &&args, IO type, u32 client_sample_rate, AudioDevice::AudioCallback callback, UserData user_data)
    : Component(std::move(args)), Type(type), Callback(callback), _UserData(user_data) {
    const Field::References listened_fields{Name, Format, Channels, NativeSampleRate};
    for (const Field &field : listened_fields) field.RegisterChangeListener(this);
    Init(client_sample_rate);
}

AudioDevice::~AudioDevice() {
    Uninit();
    Field::UnregisterChangeListener(this);
}

void AudioDevice::Init(u32 client_sample_rate) {
    AudioContextInitializedCount++;
    if (AudioContextInitializedCount <= 1) {
        ma_result result = ma_context_init(nullptr, 0, nullptr, &AudioContext);
        if (result != MA_SUCCESS) throw std::runtime_error(std::format("Error initializing audio context: {}", int(result)));

        static u32 PlaybackDeviceCount, CaptureDeviceCount;
        static ma_device_info *PlaybackDeviceInfos, *CaptureDeviceInfos;
        result = ma_context_get_devices(&AudioContext, &PlaybackDeviceInfos, &PlaybackDeviceCount, &CaptureDeviceInfos, &CaptureDeviceCount);
        if (result != MA_SUCCESS) throw std::runtime_error(std::format("Error getting audio devices: {}", int(result)));

        for (u32 i = 0; i < CaptureDeviceCount; i++) {
            DeviceInfos[IO_In].emplace_back(&CaptureDeviceInfos[i]);
            DeviceNames[IO_In].push_back(CaptureDeviceInfos[i].name);
        }
        for (u32 i = 0; i < PlaybackDeviceCount; i++) {
            DeviceInfos[IO_Out].emplace_back(&PlaybackDeviceInfos[i]);
            DeviceNames[IO_Out].push_back(PlaybackDeviceInfos[i].name);
        }

        for (const IO io : IO_All) {
            ma_device_info DeviceInfo;

            result = ma_context_get_device_info(&AudioContext, io == IO_In ? ma_device_type_capture : ma_device_type_playback, nullptr, &DeviceInfo);
            if (result != MA_SUCCESS) throw std::runtime_error(std::format("Error getting audio {} device info: {}", to_string(io), int(result)));

            // todo  Create a new format type that mirrors MA's (format, rate).
            for (u32 i = 0; i < DeviceInfo.nativeDataFormatCount; i++) {
                const auto &native_format = DeviceInfo.nativeDataFormats[i];
                NativeFormats[io].emplace_back(native_format.format);
                NativeSampleRates[io].emplace_back(native_format.sampleRate);
            }
        }

        // MA graph nodes require f32 format for in/out.
        // We could keep IO formats configurable, and add two decoders to/from f32, but MA already does this
        // conversion from native formats (if needed) since we specify f32 format in the device config, so it
        // would just be needlessly wasting cycles/memory (memory since an extra input buffer would be needed).
        // todo option to change dither mode, only present when used
        // config.capture.format = ToAudioFormat(InFormat);
        // config.playback.format = ToAudioFormat(OutFormat);

        // ResamplerConfig = ma_resampler_config_init(ma_format_f32, 2, 0, 0, ma_resample_algorithm_custom);
        // auto result = ma_resampler_init(&ResamplerConfig, nullptr, &Resampler);
        // if (result != MA_SUCCESS) throw std::runtime_error(std::format("Error initializing resampler: {}", result));
        // ResamplerConfig.pBackendVTable = &ResamplerVTable;
    }

    Device = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(Type == IO_In ? ma_device_type_capture : ma_device_type_playback);

    const ma_device_id *device_id = nullptr;
    for (const ma_device_info *info : DeviceInfos[Type]) {
        if (info->name == string_view(Name)) {
            device_id = &(info->id);
            break;
        }
    }

    if (Type == IO_In) {
        config.capture.pDeviceID = device_id;
        config.capture.format = ma_format_f32;
        config.capture.channels = Channels;
        // `noFixedSizedCallback` is more efficient, and seems to be ok.
        // Also seems fine for the output device, but only using it for the input device for now.
        config.noFixedSizedCallback = true;
    } else {
        config.playback.pDeviceID = device_id;
        config.playback.format = ma_format_f32;
        config.playback.channels = Channels;
    }

    config.dataCallback = Callback;
    config.pUserData = _UserData;

    u32 native_sample_rate_valid = GetHighestPriorityNativeSampleRate(Type, NativeSampleRate);
    ClientSampleRate = client_sample_rate == 0 ? native_sample_rate_valid : client_sample_rate;

    u32 from_sample_rate = Type == IO_In ? native_sample_rate_valid : ClientSampleRate;
    u32 to_sample_rate = Type == IO_In ? ClientSampleRate : native_sample_rate_valid;

    config.sampleRate = ClientSampleRate;
    // Format/channels/rate doesn't matter here.
    config.resampling = ma_resampler_config_init(ma_format_unknown, 0, from_sample_rate, to_sample_rate, ma_resample_algorithm_linear);
    config.noPreSilencedOutputBuffer = true; // The audio graph already ensures the output buffer already writes to every output frame.
    config.coreaudio.allowNominalSampleRateChange = true; // On Mac, allow changing the native system sample rate.

    ma_result result = ma_device_init(nullptr, &config, Device.get());
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Error initializing audio {} device: {}", to_string(Type), int(result)));

    // The device may have a different configuration than what we requested.
    // Update the fields to reflect the actual device configuration.
    if (Type == IO_Out) {
        if (Device->playback.internalSampleRate != NativeSampleRate) NativeSampleRate.Set_(Device->playback.internalSampleRate);
        if (Device->playback.name != Name) Name.Set_(Device->playback.name);
        if (Device->playback.format != Format) Format.Set_(Device->playback.format);
        if (Device->playback.channels != Channels) Channels.Set_(Device->playback.channels);
    } else {
        if (Device->capture.internalSampleRate != NativeSampleRate) NativeSampleRate.Set_(Device->capture.internalSampleRate);
        if (Device->capture.name != Name) Name.Set_(Device->capture.name);
        if (Device->capture.format != Format) Format.Set_(Device->capture.format);
        if (Device->capture.channels != Channels) Channels.Set_(Device->capture.channels);
    }
    result = ma_device_start(Device.get());
    if (result != MA_SUCCESS) throw std::runtime_error(std::format("Error starting audio {} device: {}", to_string(Type), int(result)));
}

void AudioDevice::Uninit() {
    if (IsStarted()) ma_device_stop(Device.get());
    ma_device_uninit(Device.get());
    Device.reset();

    AudioContextInitializedCount--;
    if (AudioContextInitializedCount <= 0) {
        for (const IO io : IO_All) {
            DeviceInfos[io].clear();
            DeviceNames[io].clear();
        }
        ma_context_uninit(&AudioContext);
    }
}

void AudioDevice::OnFieldChanged() {
    if (Name.IsChanged() || Format.IsChanged() || Channels.IsChanged() || NativeSampleRate.IsChanged()) {
        Uninit();
        Init(ClientSampleRate);
    }
}

bool AudioDevice::IsNativeSampleRate(u32 sample_rate) const {
    if (!NativeSampleRates.contains(Type)) return false;

    const auto &native_sample_rates = NativeSampleRates.at(Type);
    return std::find(native_sample_rates.begin(), native_sample_rates.end(), sample_rate) != native_sample_rates.end();
}

static bool IsNativeFormat(ma_format format, IO type) {
    if (!NativeFormats.contains(type)) return false;

    const auto &native_formats = NativeFormats.at(type);
    return std::find(native_formats.begin(), native_formats.end(), format) != native_formats.end();
}

string AudioDevice::GetFormatName(int format) const {
    return ::std::format("{}{}", ma_get_format_name(ma_format(format)), IsNativeFormat(ma_format(format), Type) ? "*" : "");
}

string AudioDevice::GetSampleRateName(u32 sample_rate) const { return to_string(sample_rate); }

void AudioDevice::SetClientSampleRate(u32 client_sample_rate) {
    if (client_sample_rate == ClientSampleRate) return;

    // For now at least, just restart the device even if there is a resampler, since the device data converter is not intended
    // to be adjusted like this, and it leaves other internal fields out of sync.
    // I'm pretty sure we could just manually update the `internalSampleRate` in addition to this, but other things may be needed.
    // Also, it seems performant enough to just restart the device always.
    // if (Type == IO_In) {
    //     if (Device->capture.converter.hasResampler) {
    //         ma_data_converter_set_rate(&Device->capture.converter, Device->capture.converter.resampler.sampleRateIn, sample_rate);
    //     }
    // } else {
    //     if (Device->playback.converter.hasResampler) {
    //         ma_data_converter_set_rate(&Device->playback.converter, sample_rate, Device->playback.converter.resampler.sampleRateOut);
    //     }
    // }

    Uninit();
    Init(client_sample_rate);
}

bool AudioDevice::IsStarted() const { return ma_device_is_started(Device.get()); }

using namespace ImGui;

void AudioDevice::Render() const {
    if (!IsStarted()) {
        TextUnformatted("Audio device is not started.");
        return;
    }

    TextUnformatted(StringHelper::Capitalize(to_string(Type)).c_str());
    Name.Render(DeviceNames[Type]);
    NativeSampleRate.Render(NativeSampleRates[Type]);
    // Format.Render(PrioritizedFormats); // todo choose (format, sample rate) pairs, since these are actually what's considered a "native format" by MA.

    if (TreeNode("Info")) {
        static char name[MA_MAX_DEVICE_NAME_LENGTH + 1];
        auto *device = Device.get();
        ma_device_get_name(device, Type == IO_In ? ma_device_type_capture : ma_device_type_playback, name, sizeof(name), nullptr);
        Text("%s (%s)", name, Type == IO_In ? "Capture" : "Playback");
        Text("Backend: %s", ma_get_backend_name(device->pContext->backend));
        if (Type == IO_In) {
            Text("Format: %s -> %s", ma_get_format_name(device->capture.internalFormat), ma_get_format_name(device->capture.format));
            Text("Channels: %d -> %d", device->capture.internalChannels, device->capture.channels);
            Text("Sample Rate: %d -> %d", device->capture.internalSampleRate, device->sampleRate);
            Text("Buffer Size: %d*%d (%d)\n", device->capture.internalPeriodSizeInFrames, device->capture.internalPeriods, (device->capture.internalPeriodSizeInFrames * device->capture.internalPeriods));
            if (TreeNodeEx("Conversion", ImGuiTreeNodeFlags_DefaultOpen)) {
                Text("Pre Format Conversion: %s\n", device->capture.converter.hasPreFormatConversion ? "YES" : "NO");
                Text("Post Format Conversion: %s\n", device->capture.converter.hasPostFormatConversion ? "YES" : "NO");
                Text("Channel Routing: %s\n", device->capture.converter.hasChannelConverter ? "YES" : "NO");
                Text("Resampling: %s\n", device->capture.converter.hasResampler ? "YES" : "NO");
                Text("Passthrough: %s\n", device->capture.converter.isPassthrough ? "YES" : "NO");
                {
                    char channel_map[1024];
                    ma_channel_map_to_string(device->capture.internalChannelMap, device->capture.internalChannels, channel_map, sizeof(channel_map));
                    Text("Channel Map In: {%s}\n", channel_map);

                    ma_channel_map_to_string(device->capture.channelMap, device->capture.channels, channel_map, sizeof(channel_map));
                    Text("Channel Map Out: {%s}\n", channel_map);
                }
                TreePop();
            }
        } else {
            Text("Format: %s -> %s", ma_get_format_name(device->playback.format), ma_get_format_name(device->playback.internalFormat));
            Text("Channels: %d -> %d", device->playback.channels, device->playback.internalChannels);
            Text("Sample Rate: %d -> %d", device->sampleRate, device->playback.internalSampleRate);
            Text("Buffer Size: %d*%d (%d)", device->playback.internalPeriodSizeInFrames, device->playback.internalPeriods, (device->playback.internalPeriodSizeInFrames * device->playback.internalPeriods));
            if (TreeNodeEx("Conversion", ImGuiTreeNodeFlags_DefaultOpen)) {
                Text("Pre Format Conversion:  %s", device->playback.converter.hasPreFormatConversion ? "YES" : "NO");
                Text("Post Format Conversion: %s", device->playback.converter.hasPostFormatConversion ? "YES" : "NO");
                Text("Channel Routing: %s", device->playback.converter.hasChannelConverter ? "YES" : "NO");
                Text("Resampling: %s", device->playback.converter.hasResampler ? "YES" : "NO");
                Text("Passthrough: %s", device->playback.converter.isPassthrough ? "YES" : "NO");
                {
                    char channel_map[1024];
                    ma_channel_map_to_string(device->playback.channelMap, device->playback.channels, channel_map, sizeof(channel_map));
                    Text("Channel Map In: {%s}", channel_map);

                    ma_channel_map_to_string(device->playback.internalChannelMap, device->playback.internalChannels, channel_map, sizeof(channel_map));
                    Text("Channel Map Out: {%s}", channel_map);
                }
                TreePop();
            }
        }
        TreePop();
    }
}

// todo implement for r8brain resampler
// todo I want to use this currently to support quality/fast resampling between _natively supported_ device sample rates.
//   Can I still use duplex mode in this case?
// static ma_resampler_config ResamplerConfig;
// static ma_resampler Resampler;
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
