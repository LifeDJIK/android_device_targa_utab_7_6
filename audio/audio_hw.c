/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (c) 2014 Ivan Krakhmaliuk (LifeDJIK)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "LifeDJIK_Audio_HW_HAL"
#define LOG_NDEBUG 0

#define CARD 0
#define DEVICE 0

#define OUT_SAMPLE_RATE 48000
#define OUT_CHANNELS 2
#define OUT_PERIOD_SIZE 1024
#define OUT_PERIOD_COUNT 2

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>

#include <audio_utils/primitives.h>
#include <audio_utils/resampler.h>

struct audio_device {
    struct audio_hw_device device;

    uint32_t out_sample_rate;
    unsigned int out_channels;
    unsigned int out_period_size;
    unsigned int out_period_count;
};

struct stream_out {
    struct audio_stream_out stream;

    uint32_t sample_rate;
	audio_channel_mask_t channel_mask;
	audio_format_t format;

	size_t buffer_size;
    size_t resampler_buffer_size;

    unsigned int channels;
    void *resampler_buffer;

    size_t remix_buffer_size;
    void *remix_buffer;

    struct resampler_itfe *resampler;

    bool standby;

    struct pcm_config config;
    struct pcm *pcm;
    
    struct audio_device *dev;
};

struct stream_in {
    struct audio_stream_in stream;
};

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_out *lostream = (struct stream_out *)stream;
    return lostream->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_out *lostream = (struct stream_out *)stream;
    return lostream->buffer_size;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct stream_out *lostream = (struct stream_out *)stream;
    return lostream->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct stream_out *lostream = (struct stream_out *)stream;
    return lostream->format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct stream_out *lostream = (struct stream_out *)stream;
    if (!lostream->standby)
    {
        pcm_close(lostream->pcm);
        lostream->pcm = NULL;
        if (lostream->resampler) {
            ALOGV("Release resampler!");
            release_resampler(lostream->resampler);
            lostream->resampler = NULL;
        }
        lostream->standby = true;
    }
    return 0;
}

static int out_leave_standby(struct audio_stream *stream)
{
    struct stream_out *lostream = (struct stream_out *)stream;
    if (lostream->standby)
    {
        lostream->pcm = pcm_open(CARD, DEVICE, PCM_OUT, &lostream->config);
        if (!lostream->pcm || !pcm_is_ready(lostream->pcm))
        {
            ALOGE("Failed to open PCM: %s", pcm_get_error(lostream->pcm));
            return -1; // Maybe this should be changed to other value
        }
        if (lostream->config.rate != lostream->sample_rate)
        {
            ALOGV("Resampler: %i -> %i", lostream->sample_rate, lostream->config.rate);
            if (create_resampler(lostream->sample_rate, lostream->config.rate, lostream->config.channels, RESAMPLER_QUALITY_DEFAULT, NULL, &lostream->resampler))
            {
                ALOGE("Failed to create resampler");
                return -1; // Maybe this should be changed to other value
            }
        }
        lostream->standby = false;
    }
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
	ALOGV("%s: %s", __func__, kvpairs);
	struct mixer *mixer = mixer_open(0);
	int ret = 0;
	if (mixer) {
		struct str_parms *parms = str_parms_create_str(kvpairs);
		char value[32];
		ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
		if (ret >= 0) {
			int val = atoi(value);
			switch (val) {
				case AUDIO_DEVICE_OUT_SPEAKER: {
						ALOGI("%s: speaker route", __func__);
						unsigned int i;
						struct mixer_ctl *ctl_jack_function = mixer_get_ctl_by_name(mixer, "Jack Function");
						if (mixer_ctl_set_enum_by_string(ctl_jack_function, "Speaker")) { ALOGE("%s: failed to set: Jack Function", __func__); }
						struct mixer_ctl *ctl_spk_function = mixer_get_ctl_by_name(mixer, "Speaker Function");
						if (mixer_ctl_set_enum_by_string(ctl_spk_function, "On")) { ALOGE("%s: failed to set: Speaker Function", __func__); }
						struct mixer_ctl *ctl_spk_switch = mixer_get_ctl_by_name(mixer, "SPK Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_switch); i++) {
							if (mixer_ctl_set_value(ctl_spk_switch, i, 1)) { ALOGE("%s: failed to set: SPK Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_spk_volume = mixer_get_ctl_by_name(mixer, "SPK Playback Volume");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_volume); i++) {
							if (mixer_ctl_set_value(ctl_spk_volume, i, 31)) { ALOGE("%s: failed to set: SPK Playback Volume", __func__); }
						}
						struct mixer_ctl *ctl_spk_dac_l_switch = mixer_get_ctl_by_name(mixer, "SPXMIX Mixer DACL Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_dac_l_switch); i++) {
							if (mixer_ctl_set_value(ctl_spk_dac_l_switch, i, 1)) { ALOGE("%s: failed to set: SPXMIX Mixer DACL Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_spk_dac_r_switch = mixer_get_ctl_by_name(mixer, "SPXMIX Mixer DACR Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_dac_r_switch); i++) {
							if (mixer_ctl_set_value(ctl_spk_dac_r_switch, i, 1)) { ALOGE("%s: failed to set: SPXMIX Mixer DACR Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_l_switch = mixer_get_ctl_by_name(mixer, "HPL Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_l_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_l_switch, i, 0)) { ALOGE("%s: failed to set: HPL Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_r_switch = mixer_get_ctl_by_name(mixer, "HPR Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_r_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_r_switch, i, 0)) { ALOGE("%s: failed to set: HPR Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_volume = mixer_get_ctl_by_name(mixer, "HP Playback Volume");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_volume); i++) {
							if (mixer_ctl_set_value(ctl_hp_volume, i, 0)) { ALOGE("%s: failed to set: HP Playback Volume", __func__); }
						}
						struct mixer_ctl *ctl_hp_dac_l_switch = mixer_get_ctl_by_name(mixer, "HPMIXL Mixer DAC Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_dac_l_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_dac_l_switch, i, 0)) { ALOGE("%s: failed to set: HPMIXL Mixer DAC Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_dac_r_switch = mixer_get_ctl_by_name(mixer, "HPMIXR Mixer DAC Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_dac_r_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_dac_r_switch, i, 0)) { ALOGE("%s: failed to set: HPMIXR Mixer DAC Playback Switch", __func__); }
						}
					}
					break;
				case AUDIO_DEVICE_OUT_WIRED_HEADPHONE: {
						ALOGI("%s: wired headphone route", __func__);
						unsigned int i;
						struct mixer_ctl *ctl_jack_function = mixer_get_ctl_by_name(mixer, "Jack Function");
						if (mixer_ctl_set_enum_by_string(ctl_jack_function, "HeadPhone")) { ALOGE("%s: failed to set: Jack Function", __func__); }
						struct mixer_ctl *ctl_spk_function = mixer_get_ctl_by_name(mixer, "Speaker Function");
						if (mixer_ctl_set_enum_by_string(ctl_spk_function, "Off")) { ALOGE("%s: failed to set: Speaker Function", __func__); }
						struct mixer_ctl *ctl_spk_switch = mixer_get_ctl_by_name(mixer, "SPK Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_switch); i++) {
							if (mixer_ctl_set_value(ctl_spk_switch, i, 0)) { ALOGE("%s: failed to set: SPK Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_spk_volume = mixer_get_ctl_by_name(mixer, "SPK Playback Volume");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_volume); i++) {
							if (mixer_ctl_set_value(ctl_spk_volume, i, 0)) { ALOGE("%s: failed to set: SPK Playback Volume", __func__); }
						}
						struct mixer_ctl *ctl_spk_dac_l_switch = mixer_get_ctl_by_name(mixer, "SPXMIX Mixer DACL Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_dac_l_switch); i++) {
							if (mixer_ctl_set_value(ctl_spk_dac_l_switch, i, 0)) { ALOGE("%s: failed to set: SPXMIX Mixer DACL Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_spk_dac_r_switch = mixer_get_ctl_by_name(mixer, "SPXMIX Mixer DACR Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_spk_dac_r_switch); i++) {
							if (mixer_ctl_set_value(ctl_spk_dac_r_switch, i, 0)) { ALOGE("%s: failed to set: SPXMIX Mixer DACR Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_l_switch = mixer_get_ctl_by_name(mixer, "HPL Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_l_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_l_switch, i, 1)) { ALOGE("%s: failed to set: HPL Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_r_switch = mixer_get_ctl_by_name(mixer, "HPR Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_r_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_r_switch, i, 1)) { ALOGE("%s: failed to set: HPR Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_volume = mixer_get_ctl_by_name(mixer, "HP Playback Volume");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_volume); i++) {
							if (mixer_ctl_set_value(ctl_hp_volume, i, 31)) { ALOGE("%s: failed to set: HP Playback Volume", __func__); }
						}
						struct mixer_ctl *ctl_hp_dac_l_switch = mixer_get_ctl_by_name(mixer, "HPMIXL Mixer DAC Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_dac_l_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_dac_l_switch, i, 1)) { ALOGE("%s: failed to set: HPMIXL Mixer DAC Playback Switch", __func__); }
						}
						struct mixer_ctl *ctl_hp_dac_r_switch = mixer_get_ctl_by_name(mixer, "HPMIXR Mixer DAC Playback Switch");
						for (i = 0; i < mixer_ctl_get_num_values(ctl_hp_dac_r_switch); i++) {
							if (mixer_ctl_set_value(ctl_hp_dac_r_switch, i, 1)) { ALOGE("%s: failed to set: HPMIXR Mixer DAC Playback Switch", __func__); }
						}
					}
					break;
				default:
					ALOGW("%s: unknown route: %i", __func__, val);
			}
		}
		mixer_close(mixer);
		str_parms_destroy(parms);
	} else {
		ALOGV("%s: failed to open mixer!", __func__);
	}
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    return 42; // The answer to all the questions!
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return 0;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
	struct stream_out *lostream = (struct stream_out *)stream;
	
    //ALOGD("Bytes: %i", bytes);
    
    if (lostream->standby && out_leave_standby((struct audio_stream *)stream))
    {
        ALOGE("Write failed! No out standby!");
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
           out_get_sample_rate(&stream->common));
        return bytes;
    }
    
    const void *current_buffer = buffer;
    size_t current_bytes = bytes;
    size_t current_frame_size = audio_stream_frame_size(&stream->common);
    
    size_t current_buffer_frames = current_bytes / current_frame_size;
    size_t resampler_buffer_frames = lostream->resampler_buffer_size / current_frame_size;
    
    size_t processed_bytes = bytes;
    
    if (lostream->sample_rate != lostream->config.rate) {
        lostream->resampler->resample_from_input(lostream->resampler,
                                  (int16_t *)current_buffer,
                                  &current_buffer_frames,
                                  lostream->resampler_buffer,
                                  &resampler_buffer_frames);
        current_buffer = lostream->resampler_buffer;
        current_bytes = resampler_buffer_frames * current_frame_size;
        processed_bytes = current_buffer_frames * current_frame_size;
    }
    
    if (lostream->channels != lostream->config.channels) {
        // The only supported conversions for now are: mono -> stereo, stereo -> mono
        if (lostream->channels > lostream->config.channels) {
            // stereo -> mono
            downmix_to_mono_i16_from_stereo_i16(lostream->remix_buffer, current_buffer, current_bytes / current_frame_size);
            current_bytes /= 2;
            current_frame_size /= 2;
        } else {
            // mono -> stereo
            upmix_to_stereo_i16_from_mono_i16(lostream->remix_buffer, current_buffer, current_bytes / current_frame_size);
            current_bytes *= 2;
            current_frame_size *= 2;
        }
        current_buffer = lostream->remix_buffer;
    }
    
    if (pcm_write(lostream->pcm, current_buffer, current_bytes)) {
        ALOGE("Write failed");
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
           out_get_sample_rate(&stream->common));
        }
    
    return processed_bytes; // Still not sure if it is right!
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

#ifndef ICS_AUDIO_BLOB
static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -EINVAL;
}
#endif

/** audio_stream_in implementation **/
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    return 8000;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    return 320;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    return AUDIO_CHANNEL_IN_MONO;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    return 0;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    return 0;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    /* XXX: fake timing for audio input */
    usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
           in_get_sample_rate(&stream->common));
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct audio_device *ladev = (struct audio_device *)dev;
    struct stream_out *out;
    int ret = -1;

    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if (!out)
        return -ENOMEM;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
#ifndef ICS_AUDIO_BLOB
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
#endif
	
    // Initialize & check
    
    out->dev = ladev;
    
    if (config->format != AUDIO_FORMAT_PCM_16_BIT) {
		ALOGE("Failed to open output stream: unsupported format!");
        goto err_open;
	}

    out->format = config->format;
	out->sample_rate = config->sample_rate;
	out->channel_mask = config->channel_mask;
	out->channels = 0;

    switch (config->channel_mask) {
		case AUDIO_CHANNEL_OUT_MONO:
            out->channels = 1;
            break;
		case AUDIO_CHANNEL_OUT_STEREO:
            out->channels = 2;
			break;
	}
    
    if (!out->channels) {
        ALOGE("Failed to open output stream: unsupported channel count!");
        goto err_open;
    }
    
	out->config.format = PCM_FORMAT_S16_LE;
	out->config.rate = ladev->out_sample_rate;
    out->config.channels = ladev->out_channels;
    
    out->config.period_size = ladev->out_period_size;
    out->config.period_count = ladev->out_period_count;

    out->config.start_threshold = 0;
    out->config.stop_threshold = 0;
    out->config.silence_threshold = 0;
    out->config.avail_min = 0;
    
    out->buffer_size = audio_stream_frame_size(&out->stream.common) * out->config.period_size;

    out->remix_buffer_size = 0;
    out->remix_buffer = NULL;
    
    out->resampler = NULL;
    out->resampler_buffer_size = 0;
    out->resampler_buffer = NULL;
    
    out->standby = true;
    
    if (out->channels != out->config.channels) {
        // The only supported conversions for now are: mono -> stereo, stereo -> mono
        if (out->channels > out->config.channels) {
            // stereo -> mono
            out->remix_buffer_size = out->buffer_size / 2;
        } else {
            // mono -> stereo
            out->remix_buffer_size = out->buffer_size * 2;
        }
    }
    
    if (out->sample_rate != out->config.rate) {
        out->resampler_buffer_size = out->buffer_size;
    }
    
    if (out->remix_buffer_size) {
        out->remix_buffer = (void *)calloc(1, out->remix_buffer_size);
        if (!out->remix_buffer) {
            ALOGE("Failed to open output stream: remix buffer allocation failed!");
            ret = -ENOMEM;
            goto err_open;
        }
    }

    if (out->resampler_buffer_size) {
        out->resampler_buffer = (void *)calloc(1, out->resampler_buffer_size);
        if (!out->resampler_buffer) {
            ALOGE("Failed to open output stream: resampler buffer allocation failed!");
            // Do not forget about other buffers!
            if (out->remix_buffer) free(out->remix_buffer);
            ret = -ENOMEM;
            goto err_open;
        }
    }
    
    ALOGV("Opening output stream: rate = %i, channels = %i, buffer = %i, m = %i, s = %i",
            out->sample_rate, out->channels, out->buffer_size, out->remix_buffer_size, out->resampler_buffer_size);
    
    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
	struct stream_out *lostream = (struct stream_out *)stream;
	if (!lostream->standby) {
        out_standby((struct audio_stream *)stream);
    }
    if (lostream->resampler_buffer) {
        free(lostream->resampler_buffer);
    }
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    return -ENOSYS;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return NULL;
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

#ifndef ICS_AUDIO_BLOB
static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    return -ENOSYS;
}
#endif

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    return -ENOSYS;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    return -ENOSYS;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    return 320;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct audio_device *ladev = (struct audio_device *)dev;
    struct stream_in *in;
    int ret;

    in = (struct stream_in *)calloc(1, sizeof(struct stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    *stream_in = &in->stream;
    return 0;

err_open:
    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *in)
{
    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    free(device);
    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct audio_device *adev;
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct audio_device));
    if (!adev)
        return -ENOMEM;

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->device.common.module = (struct hw_module_t *) module;
    adev->device.common.close = adev_close;

    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
#ifndef ICS_AUDIO_BLOB
    adev->device.get_master_volume = adev_get_master_volume;
    adev->device.set_master_mute = adev_set_master_mute;
    adev->device.get_master_mute = adev_get_master_mute;
#endif
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
#ifndef ICS_AUDIO_BLOB
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
#endif
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;

    adev->out_sample_rate = OUT_SAMPLE_RATE;
    adev->out_channels = OUT_CHANNELS;
    adev->out_period_size = OUT_PERIOD_SIZE;
    adev->out_period_count = OUT_PERIOD_COUNT;
    
    ALOGV("Opening device: out_rate = %i, out_channels = %i, out_period = %i, out_count = %i",
            adev->out_sample_rate, adev->out_channels, adev->out_period_size, adev->out_period_count);

    *device = &adev->device.common;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "LifeDJIK Audio HW HAL",
        .author = "LifeDJIK & The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
