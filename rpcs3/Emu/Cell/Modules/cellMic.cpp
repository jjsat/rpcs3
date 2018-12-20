#include "stdafx.h"
#include "Emu/System.h"
#include "Emu/Cell/PPUModule.h"
#include "Utilities/StrUtil.h"

#include "cellMic.h"
#include <Emu/IdManager.h>
#include <Emu/Cell/lv2/sys_event.h>

LOG_CHANNEL(cellMic);

template <>
void fmt_class_string<microphone_handler>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto value)
	{
		switch (value)
		{
		case microphone_handler::null: return "Null";
		case microphone_handler::standard: return "Standard";
		case microphone_handler::singstar: return "Singstar";
		case microphone_handler::real_singstar: return "Real Singstar";
		}

		return unknown;
	});
}

void mic_context::variable_byteswap(void* src, void* dst, u32 bytesize)
{
	switch (bytesize)
	{
	case 4:
		*(u32*)dst = *(be_t<u32>*)src;
		break;
	case 2:
		*(u16*)dst = *(be_t<u16>*)src;
		break;
	}
}

u32 mic_context::get_capture(microphone_device& mic)
{
	u32 num_samples = mic.inbuf_size / mic.sample_size;

	for (auto micdevice : mic.input_devices)
	{
		ALCint samples_in = 0;
		alcGetIntegerv(micdevice, ALC_CAPTURE_SAMPLES, 1, &samples_in);
		if (alcGetError(micdevice) != ALC_NO_ERROR)
		{
			cellMic.error("Error getting number of captured samples");
			return CELL_MIC_ERROR_FATAL;
		}
		num_samples = std::min<u32>(num_samples, samples_in);
	}

	for (u32 index = 0; index < mic.input_devices.size(); index++)
	{
		alcCaptureSamples(mic.input_devices[index], mic.internal_bufs[index].data(), num_samples);
	}

	return num_samples;
}

void mic_context::get_raw(microphone_device& mic, u32 num_samples)
{
	u8* tmp_ptr = mic.temp_buf.data();

	switch (mic.device_type)
	{
	case microphone_handler::real_singstar:
		// Straight copy from device
		memcpy(tmp_ptr, mic.internal_bufs[0].data(), num_samples * (mic.bit_resolution / 8 ) * mic.num_channels);
		break;
	case microphone_handler::standard:
		// BE Translation
		for (u32 index = 0; index < num_samples; index++)
		{
			for (u32 indchan = 0; indchan < mic.num_channels; indchan++)
			{
				const u32 curindex = (index * mic.sample_size) + indchan * (mic.bit_resolution / 8);
				variable_byteswap(mic.internal_bufs[0].data() + curindex, tmp_ptr + curindex, mic.bit_resolution / 8);
			}
		}
		break;
	case microphone_handler::singstar:
		verify(HERE), mic.sample_size == 4;

		// Mixing the 2 mics as if channels
		if (mic.input_devices.size() == 2)
		{
			for (u32 index = 0; index < (num_samples * 4); index += 4)
			{
				tmp_ptr[index] = mic.internal_bufs[0][(index / 2)];
				tmp_ptr[index + 1] = mic.internal_bufs[0][(index / 2) + 1];
				tmp_ptr[index + 2] = mic.internal_bufs[1][(index / 2)];
				tmp_ptr[index + 3] = mic.internal_bufs[1][(index / 2) + 1];
			}
		}
		else
		{
			for (u32 index = 0; index < (num_samples * 4); index += 4)
			{
				tmp_ptr[index] = mic.internal_bufs[0][(index / 2)];
				tmp_ptr[index + 1] = mic.internal_bufs[0][(index / 2) + 1];
				tmp_ptr[index + 2] = 0;
				tmp_ptr[index + 3] = 0;
			}
		}

		break;
	}

	mic.rbuf_raw.write_bytes(tmp_ptr, num_samples * mic.sample_size);
};

void mic_context::get_dsp(microphone_device& mic, u32 num_samples)
{
	u8* tmp_ptr = mic.temp_buf.data();

	switch (mic.device_type)
	{
	case microphone_handler::real_singstar:
		// Straight copy from device
		memcpy(tmp_ptr, mic.internal_bufs[0].data(), num_samples * (mic.bit_resolution / 8) * mic.num_channels);
		break;
	case microphone_handler::standard:
		// BE Translation
		for (u32 index = 0; index < num_samples; index++)
		{
			for (u32 indchan = 0; indchan < mic.num_channels; indchan++)
			{
				const u32 curindex = (index * mic.sample_size) + indchan * (mic.bit_resolution / 8);
				variable_byteswap(mic.internal_bufs[0].data() + curindex, tmp_ptr + curindex, mic.bit_resolution / 8);
			}
		}
		break;
	case microphone_handler::singstar:
		verify(HERE), mic.sample_size == 4;

		// Mixing the 2 mics as if channels
		if (mic.input_devices.size() == 2)
		{
			for (u32 index = 0; index < (num_samples * 4); index += 4)
			{
				tmp_ptr[index] = mic.internal_bufs[0][(index / 2)];
				tmp_ptr[index + 1] = mic.internal_bufs[0][(index / 2) + 1];
				tmp_ptr[index + 2] = mic.internal_bufs[1][(index / 2)];
				tmp_ptr[index + 3] = mic.internal_bufs[1][(index / 2) + 1];
			}
		}
		else
		{
			for (u32 index = 0; index < (num_samples * 4); index += 4)
			{
				tmp_ptr[index] = mic.internal_bufs[0][(index / 2)];
				tmp_ptr[index + 1] = mic.internal_bufs[0][(index / 2) + 1];
				tmp_ptr[index + 2] = 0;
				tmp_ptr[index + 3] = 0;
			}
		}

		break;
	}

	mic.rbuf_dsp.write_bytes(tmp_ptr, num_samples * mic.sample_size);
};

void mic_context::operator()()
{
	while (thread_ctrl::state() != thread_state::aborting && !Emu.IsStopped())
	{
		// The time between processing is copied from audio thread
		// Might be inaccurate for mic thread
		if (Emu.IsPaused())
		{
			thread_ctrl::wait_for(1000); // hack
			continue;
		}

		const u64 stamp0 = get_system_time();
		const u64 time_pos = stamp0 - start_time - Emu.GetPauseTime();

		const u64 expected_time = m_counter * 256 * 1000000 / 48000;
		if (expected_time >= time_pos)
		{
			thread_ctrl::wait_for(1000); // hack
			continue;
		}
		m_counter++;

		// Process signals
		{
			auto micl = g_idm->lock<mic_thread>(0);

			for (auto& mic_entry : mic_list)
			{
				auto& mic = mic_entry.second;
				if (mic.mic_opened && mic.mic_started)
				{
					if (mic.signal_types == CELLMIC_SIGTYPE_NULL)
						continue;

					const u32 num_samples = get_capture(mic);

					if (mic.signal_types&CELLMIC_SIGTYPE_RAW) get_raw(mic, num_samples);
					if (mic.signal_types&CELLMIC_SIGTYPE_DSP) get_dsp(mic, num_samples);
					// TODO: aux?
				}
			}

			auto micQueue = lv2_event_queue::find(event_queue_key);
			if (!micQueue)
				continue;

			for (auto& mic_entry : mic_list)
			{
				auto& mic = mic_entry.second;
				if (mic.mic_opened && mic.mic_started &&
					(((mic.signal_types&CELLMIC_SIGTYPE_RAW) && mic.rbuf_raw.m_used) || ((mic.signal_types&CELLMIC_SIGTYPE_DSP) && mic.rbuf_dsp.m_used)))
				{
					micQueue->send(0, CELL_MIC_DATA, mic_entry.first, 0);
				}
			}
		}
	}

	// Cleanup
	for (auto& mic_entry : mic_list)
	{
		mic_entry.second.close_microphone();
	}
}

s32 microphone_device::open_microphone()
{
	// Singstar mic has always 2 channels, each channel represent a physical microphone
	// Forcing num_channels to 2 if over as I couldn't find how to capture more than stereo through OpenAL
	if (device_type == microphone_handler::singstar || num_channels > 2)
		num_channels = 2;

	ALCdevice *device = alcCaptureOpenDevice(device_name[0].c_str(), raw_samplingrate, (num_channels == 2 && device_type != microphone_handler::singstar) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, inbuf_size);

	if (alcGetError(device) != ALC_NO_ERROR)
	{
		cellMic.error("Error opening capture device %s", device_name[0]);
#ifdef WIN32
		cellMic.error("Make sure microphone use is authorized under \"Microphone privacy settings\" in windows configuration");
#endif
		return CELL_MIC_ERROR_DEVICE_NOT_SUPPORT;
	}

	input_devices.push_back(device);
	internal_bufs.emplace_back();
	internal_bufs[0].resize(inbuf_size, 0);
	temp_buf.resize(inbuf_size, 0);

	if (signal_types&CELLMIC_SIGTYPE_RAW)
		rbuf_raw.set_size(inbuf_size);
	if (signal_types&CELLMIC_SIGTYPE_DSP)
		rbuf_dsp.set_size(inbuf_size);
	if (signal_types&CELLMIC_SIGTYPE_AUX)
		rbuf_aux.set_size(inbuf_size);

	if (device_type == microphone_handler::singstar && device_name.size() >= 2)
	{
		// Open a 2nd microphone into the same device
		device = alcCaptureOpenDevice(device_name[1].c_str(), raw_samplingrate, AL_FORMAT_MONO16, inbuf_size);
		if (alcGetError(device) != ALC_NO_ERROR)
		{
			// Ignore it and move on
			cellMic.error("Error opening 2nd singstar capture device %s", device_name[1]);
		}
		else
		{
			input_devices.push_back(device);
			internal_bufs.emplace_back();
			internal_bufs[1].resize(inbuf_size, 0);
		}
	}

	sample_size = (bit_resolution / 8) * num_channels;

	mic_opened = true;
	return CELL_OK;
}

s32 microphone_device::close_microphone()
{
	if (mic_started)
	{
		stop_microphone();
	}

	for (u32 index = 0; index < input_devices.size(); index++)
	{
		if (alcCaptureCloseDevice(input_devices[index]) != ALC_TRUE)
		{
			cellMic.error("Error closing capture device");
		}
	}

	input_devices.clear();
	internal_bufs.clear();

	mic_opened = false;

	return CELL_OK;
}

s32 microphone_device::start_microphone()
{
	for (auto micdevice : input_devices)
	{
		alcCaptureStart(micdevice);
		if (alcGetError(micdevice) != ALC_NO_ERROR)
		{
			cellMic.error("Error starting capture");
			stop_microphone();
			return CELL_MIC_ERROR_FATAL;
		}
	}

	mic_started = true;

	return CELL_OK;
}

s32 microphone_device::stop_microphone()
{
	for (auto micdevice : input_devices)
	{
		alcCaptureStop(micdevice);
		if (alcGetError(micdevice) != ALC_NO_ERROR)
		{
			cellMic.error("Error stopping capture");
		}
	}

	mic_started = false;

	return CELL_OK;
}

/// Initialization/Shutdown Functions

s32 cellMicInit()
{
	cellMic.notice("cellMicInit()");

	auto mic_t = g_idm->lock<mic_thread>(id_new);
	if (!mic_t)
		return CELL_MIC_ERROR_ALREADY_INIT;

	mic_t.create("Microphone Thread");

	auto device_list = fmt::split(g_cfg.audio.microphone_devices, { ";" });

	if (device_list.size())
	{
		switch (g_cfg.audio.microphone_type)
		{
		case microphone_handler::standard:
			for (u32 index = 0; index < device_list.size(); index++)
			{
				mic_t->mic_list.emplace(std::piecewise_construct, std::forward_as_tuple(index), std::forward_as_tuple());
				auto& mic = mic_t->mic_list[index];

				mic.device_type = microphone_handler::standard;
				mic.device_name.push_back(device_list[index]);
			}
			break;
		case microphone_handler::singstar:
		{
			mic_t->mic_list.emplace(std::piecewise_construct, std::forward_as_tuple(0), std::forward_as_tuple());
			auto& mic = mic_t->mic_list[0];
			mic.device_type = microphone_handler::singstar;
			mic.device_name.push_back(device_list[0]);
			if (device_list.size() >= 2) mic.device_name.push_back(device_list[1]);
			break;
		}
		case microphone_handler::real_singstar:
		{
			mic_t->mic_list.emplace(std::piecewise_construct, std::forward_as_tuple(0), std::forward_as_tuple());
			auto& mic = mic_t->mic_list[0];

			mic.device_type = microphone_handler::real_singstar;
			mic.device_name.push_back(device_list[0]);
			break;
		}
		}
	}

	return CELL_OK;
}

s32 cellMicEnd(ppu_thread& ppu)
{
	cellMic.notice("cellMicEnd()");

	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	*mic_t.get() = thread_state::aborting;
	mic_t.unlock();

	while (true)
	{
		if (ppu.is_stopped())
		{
			return 0;
		}

		thread_ctrl::wait_for(1000);

		auto mic_thr = g_idm->lock<mic_thread>(0);

		if (*mic_thr.get() == thread_state::finished)
		{
			mic_thr.destroy();
			mic_thr.unlock();
			break;
		}
	}

	return CELL_OK;
}

/// Open/Close Microphone Functions

s32 cellMicOpen(u32 dev_num, u32 sampleRate)
{
	cellMic.trace("cellMicOpen(dev_num=%um sampleRate=%u)", dev_num, sampleRate);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	if (device.mic_opened)
		return CELL_MIC_ERROR_ALREADY_OPEN;

	device.dsp_samplingrate = sampleRate;
	device.signal_types = CELLMIC_SIGTYPE_DSP;

	return device.open_microphone();
}

s32 cellMicOpenRaw(u32 dev_num, u32 sampleRate, u32 maxChannels)
{
	cellMic.trace("cellMicOpenRaw(dev_num=%d, sampleRate=%d, maxChannels=%d)", dev_num, sampleRate, maxChannels);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	if (device.mic_opened)
		return CELL_MIC_ERROR_ALREADY_OPEN;

	device.dsp_samplingrate = sampleRate;
	device.raw_samplingrate = sampleRate;
	device.num_channels = maxChannels;
	device.signal_types = CELLMIC_SIGTYPE_DSP | CELLMIC_SIGTYPE_RAW;

	return device.open_microphone();
}

s32 cellMicOpenEx(u32 dev_num, u32 rawSampleRate, u32 rawChannel, u32 DSPSampleRate, u32 bufferSizeMS, u8 signalType)
{
	cellMic.trace("cellMicOpenEx(dev_num=%d, rawSampleRate=%d, rawChannel=%d, DSPSampleRate=%d, bufferSizeMS=%d, signalType=0x%x)", dev_num, rawSampleRate, rawChannel, DSPSampleRate,
	    bufferSizeMS, signalType);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	if (device.mic_opened)
		return CELL_MIC_ERROR_ALREADY_OPEN;

	device.raw_samplingrate = rawSampleRate;
	device.dsp_samplingrate = DSPSampleRate;
	device.num_channels = rawChannel;
	device.signal_types = signalType;

	// TODO: bufferSizeMS

	return device.open_microphone();
}

u8 cellMicIsOpen(u32 dev_num)
{
	cellMic.trace("cellMicIsOpen(dev_num=%d)", dev_num);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return false;

	if (!mic_t->mic_list.count(dev_num))
		return false;

	return mic_t->mic_list[dev_num].mic_opened;
}

s32 cellMicIsAttached(u32 dev_num)
{
	cellMic.notice("cellMicIsAttached(dev_num=%d)", dev_num);
	return 1;
}

s32 cellMicClose(u32 dev_num)
{
	cellMic.trace("cellMicClose(dev_num=%d)", dev_num);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;
	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	if (!device.mic_opened)
		return CELL_MIC_ERROR_NOT_OPEN;

	device.close_microphone();
	return CELL_OK;
}

/// Starting/Stopping Microphone Functions

s32 cellMicStart(u32 dev_num)
{
	cellMic.trace("cellMicStart(dev_num=%d)", dev_num);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	if (!device.mic_opened)
		return CELL_MIC_ERROR_NOT_OPEN;

	return device.start_microphone();
}

s32 cellMicStartEx(u32 dev_num, u32 flags)
{
	cellMic.todo("cellMicStartEx(dev_num=%d, flags=%d)", dev_num, flags);

	// TODO: flags

	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	if (!device.mic_opened)
		return CELL_MIC_ERROR_NOT_OPEN;

	cellMic.error("We're getting started mate!");

	return device.start_microphone();
}

s32 cellMicStop(u32 dev_num)
{
	cellMic.trace("cellMicStop(dev_num=%d)", dev_num);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;
	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	if (!device.mic_opened)
		return CELL_MIC_ERROR_NOT_OPEN;

	if (device.mic_started)
	{
		device.stop_microphone();
	}

	return CELL_OK;
}

/// Microphone Attributes/States Functions

s32 cellMicGetDeviceAttr(u32 dev_num, CellMicDeviceAttr deviceAttributes, vm::ptr<u32> arg1, vm::ptr<u32> arg2)
{
	cellMic.trace("cellMicGetDeviceAttr(dev_num=%d, deviceAttribute=%d, arg1=*0x%x, arg2=*0x%x)", dev_num, (u32)deviceAttributes, arg1, arg2);

	if (!arg1 || (!arg2 && deviceAttributes == CELLMIC_DEVATTR_CHANVOL))
		return CELL_MIC_ERROR_PARAM;

	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;
	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	switch (deviceAttributes)
	{
	case CELLMIC_DEVATTR_LED:
		*arg1 = device.attr_led;
		break;
	case CELLMIC_DEVATTR_GAIN:
		*arg1 = device.attr_gain;
		break;
	case CELLMIC_DEVATTR_VOLUME:
		*arg1 = device.attr_volume;
		break;
	case CELLMIC_DEVATTR_AGC:
		*arg1 = device.attr_agc;
		break;
	case CELLMIC_DEVATTR_CHANVOL:
		*arg1 = device.attr_volume;
		break;
	case CELLMIC_DEVATTR_DSPTYPE:
		*arg1 = device.attr_dsptype;
		break;
	default:
		return CELL_MIC_ERROR_PARAM;
	}

	return CELL_OK;
}

s32 cellMicSetDeviceAttr(u32 dev_num, CellMicDeviceAttr deviceAttributes, u32 arg1, u32 arg2)
{
	cellMic.trace("cellMicSetDeviceAttr(dev_num=%d, deviceAttributes=%d, arg1=%d, arg2=%d)", dev_num, (u32)deviceAttributes, arg1, arg2);

	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;
	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];

	switch (deviceAttributes)
	{
	case CELLMIC_DEVATTR_CHANVOL:
		// Used by Singstar to set the volume of each mic
		if (arg1 > 2) return CELL_MIC_ERROR_PARAM;
		device.attr_chanvol[arg1] = arg2;
		break;
	case CELLMIC_DEVATTR_LED:
		device.attr_led = arg1;
		break;
	case CELLMIC_DEVATTR_GAIN:
		device.attr_gain = arg1;
		break;
	case CELLMIC_DEVATTR_VOLUME:
		device.attr_volume = arg1;
		break;
	case CELLMIC_DEVATTR_AGC:
		device.attr_agc = arg1;
		break;
	case CELLMIC_DEVATTR_DSPTYPE:
		device.attr_dsptype = arg1;
		break;
	default:
		return CELL_MIC_ERROR_PARAM;
	}

	return CELL_OK;
}

s32 cellMicGetSignalAttr()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicSetSignalAttr()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicGetSignalState(u32 dev_num, CellMicSignalState sig_state, vm::ptr<void> value)
{
	cellMic.todo("cellMicGetSignalState(dev_num=%d, signalSate=%d, value=*0x%x)", dev_num, (u32)sig_state, value);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	be_t<u32>* ival = (be_t<u32>*)value.get_ptr();
	be_t<f32>* fval = (be_t<f32>*)value.get_ptr();

	switch (sig_state)
	{
	case CELL_MIC_SIGSTATE_LOCTALK:
		*ival = 9; // Someone is probably talking
		break;
	case CELL_MIC_SIGSTATE_FARTALK:
		// TODO
		break;
	case CELL_MIC_SIGSTATE_NSR:
		// TODO
		break;
	case CELL_MIC_SIGSTATE_AGC:
		// TODO
		break;
	case CELL_MIC_SIGSTATE_MICENG:
		*fval = 40.0f; // 40 decibels
		break;
	case CELL_MIC_SIGSTATE_SPKENG:
		// TODO
		break;
	default:
		return CELL_MIC_ERROR_PARAM;
	}

	return CELL_OK;
}

s32 cellMicGetFormatRaw(u32 dev_num, vm::ptr<CellMicInputFormat> format)
{
	cellMic.trace("cellMicGetFormatRaw(dev_num=%d, format=0x%x)", dev_num, format);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& device = mic_t->mic_list[dev_num];


	format->subframeSize  = device.bit_resolution / 8; // Probably?
	format->bitResolution = device.bit_resolution;
	format->sampleRate    = device.raw_samplingrate;
	format->channelNum = device.num_channels;

	// Most games expect BE but Singstar expect LE
	switch (device.device_type)
	{
	case microphone_handler::standard:
		format->dataType = 1; // BE
		break;
	case microphone_handler::real_singstar:
	case microphone_handler::singstar:
		format->dataType = 0; // LE
		break;
	}

	return CELL_OK;
}

s32 cellMicGetFormatAux(u32 dev_num, vm::ptr<CellMicInputFormat> format)
{
	cellMic.todo("cellMicGetFormatAux(dev_num=%d, format=0x%x)", dev_num, format);

	return cellMicGetFormatRaw(dev_num, format);
}

s32 cellMicGetFormatDsp(u32 dev_num, vm::ptr<CellMicInputFormat> format)
{
	cellMic.todo("cellMicGetFormatDsp(dev_num=%d, format=0x%x)", dev_num, format);

	return cellMicGetFormatRaw(dev_num, format);
}

/// Event Queue Functions

s32 cellMicSetNotifyEventQueue(u64 key)
{
	cellMic.todo("cellMicSetNotifyEventQueue(key=0x%llx)", key);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	// default mic queue size = 4
	auto micQueue = lv2_event_queue::find(key);
	mic_t->event_queue_key = key;

	for (auto& mic_entry : mic_t->mic_list)
	{
		micQueue->send(0, CELL_MIC_ATTACH, mic_entry.first, 0);
	}

	return CELL_OK;
}

s32 cellMicSetNotifyEventQueue2(u64 key, u64 source)
{
	// TODO: Actually do things with the source variable
	cellMic.todo("cellMicSetNotifyEventQueue2(key=0x%llx, source=0x%llx", key, source);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	// default mic queue size = 4
	auto micQueue = lv2_event_queue::find(key);
	micQueue->send(0, CELL_MIC_ATTACH, 0, 0);
	mic_t->event_queue_key = key;

	return CELL_OK;
}

s32 cellMicRemoveNotifyEventQueue(u64 key)
{
	cellMic.warning("cellMicRemoveNotifyEventQueue(key=0x%llx)", key);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;

	mic_t->event_queue_key = 0;

	return CELL_OK;
}

/// Reading Functions

s32 cellMicReadRaw(u32 dev_num, vm::ptr<void> data, u32 maxBytes)
{
	cellMic.trace("cellMicReadRaw(dev_num=%d, data=0x%x, maxBytes=%d)", dev_num, data, maxBytes);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;
	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& mic = mic_t->mic_list[dev_num];

	if (!mic.mic_opened || !(mic.signal_types&CELLMIC_SIGTYPE_RAW))
		return CELL_MIC_ERROR_NOT_OPEN;

	u8* res_buf = (u8*)data.get_ptr();
	return mic.rbuf_raw.read_bytes(res_buf, maxBytes);
}

s32 cellMicRead(u32 dev_num, vm::ptr<void> data, u32 maxBytes)
{
	cellMic.todo("cellMicRead(dev_num=%d, data=0x%x, maxBytes=0x%x)", dev_num, data, maxBytes);
	auto mic_t = g_idm->lock<mic_thread>(0);
	if (!mic_t)
		return CELL_MIC_ERROR_NOT_INIT;
	if (!mic_t->mic_list.count(dev_num))
		return CELL_MIC_ERROR_DEVICE_NOT_FOUND;

	auto& mic = mic_t->mic_list[dev_num];

	if (!mic.mic_opened || !(mic.signal_types&CELLMIC_SIGTYPE_DSP))
		return CELL_MIC_ERROR_NOT_OPEN;

	u8* res_buf = (u8*)data.get_ptr();
	return mic.rbuf_dsp.read_bytes(res_buf, maxBytes);
}

s32 cellMicReadAux()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicReadDsp()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

/// Unimplemented Functions

s32 cellMicReset()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicGetDeviceGUID()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicGetType(u32 dev_num, vm::ptr<s32> ptr_type)
{
	cellMic.todo("cellMicGetType(dev_num=%d, ptr_type=*0x%x)", dev_num, ptr_type);

	*ptr_type = CELLMIC_TYPE_USBAUDIO;

	return CELL_OK;
}

s32 cellMicGetStatus()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicStopEx()
{
	fmt::throw_exception("Unexpected function" HERE);
}

s32 cellMicSysShareClose()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicGetFormat()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicSetMultiMicNotifyEventQueue()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicGetFormatEx()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicSysShareStop()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicSysShareOpen()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicCommand()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicSysShareStart()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicSysShareInit()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicSysShareEnd()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

s32 cellMicGetDeviceIdentifier()
{
	UNIMPLEMENTED_FUNC(cellMic);
	return CELL_OK;
}

DECLARE(ppu_module_manager::cellMic)("cellMic", []()
{
	REG_FUNC(cellMic, cellMicInit);
	REG_FUNC(cellMic, cellMicEnd);
	REG_FUNC(cellMic, cellMicOpen);
	REG_FUNC(cellMic, cellMicClose);
	REG_FUNC(cellMic, cellMicGetDeviceGUID);
	REG_FUNC(cellMic, cellMicGetType);
	REG_FUNC(cellMic, cellMicIsAttached);
	REG_FUNC(cellMic, cellMicIsOpen);
	REG_FUNC(cellMic, cellMicGetDeviceAttr);
	REG_FUNC(cellMic, cellMicSetDeviceAttr);
	REG_FUNC(cellMic, cellMicGetSignalAttr);
	REG_FUNC(cellMic, cellMicSetSignalAttr);
	REG_FUNC(cellMic, cellMicGetSignalState);
	REG_FUNC(cellMic, cellMicStart);
	REG_FUNC(cellMic, cellMicRead);
	REG_FUNC(cellMic, cellMicStop);
	REG_FUNC(cellMic, cellMicReset);
	REG_FUNC(cellMic, cellMicSetNotifyEventQueue);
	REG_FUNC(cellMic, cellMicSetNotifyEventQueue2);
	REG_FUNC(cellMic, cellMicRemoveNotifyEventQueue);
	REG_FUNC(cellMic, cellMicOpenEx);
	REG_FUNC(cellMic, cellMicStartEx);
	REG_FUNC(cellMic, cellMicGetFormatRaw);
	REG_FUNC(cellMic, cellMicGetFormatAux);
	REG_FUNC(cellMic, cellMicGetFormatDsp);
	REG_FUNC(cellMic, cellMicOpenRaw);
	REG_FUNC(cellMic, cellMicReadRaw);
	REG_FUNC(cellMic, cellMicReadAux);
	REG_FUNC(cellMic, cellMicReadDsp);
	REG_FUNC(cellMic, cellMicGetStatus);
	REG_FUNC(cellMic, cellMicStopEx); // this function shouldn't exist
	REG_FUNC(cellMic, cellMicSysShareClose);
	REG_FUNC(cellMic, cellMicGetFormat);
	REG_FUNC(cellMic, cellMicSetMultiMicNotifyEventQueue);
	REG_FUNC(cellMic, cellMicGetFormatEx);
	REG_FUNC(cellMic, cellMicSysShareStop);
	REG_FUNC(cellMic, cellMicSysShareOpen);
	REG_FUNC(cellMic, cellMicCommand);
	REG_FUNC(cellMic, cellMicSysShareStart);
	REG_FUNC(cellMic, cellMicSysShareInit);
	REG_FUNC(cellMic, cellMicSysShareEnd);
	REG_FUNC(cellMic, cellMicGetDeviceIdentifier);
});
