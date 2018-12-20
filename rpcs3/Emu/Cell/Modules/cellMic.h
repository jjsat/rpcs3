#pragma once

#include "Utilities/Thread.h"

#include "3rdparty/OpenAL/include/alext.h"

// Error Codes
enum
{
	CELL_MIC_ERROR_ALREADY_INIT = 0x80140101,
	CELL_MIC_ERROR_SYSTEM = 0x80140102,
	CELL_MIC_ERROR_NOT_INIT = 0x80140103,
	CELL_MIC_ERROR_PARAM = 0x80140104,
	CELL_MIC_ERROR_PORT_FULL = 0x80140105,
	CELL_MIC_ERROR_ALREADY_OPEN = 0x80140106,
	CELL_MIC_ERROR_NOT_OPEN = 0x80140107,
	CELL_MIC_ERROR_NOT_RUN = 0x80140108,
	CELL_MIC_ERROR_TRANS_EVENT = 0x80140109,
	CELL_MIC_ERROR_OPEN = 0x8014010a,
	CELL_MIC_ERROR_SHAREDMEMORY = 0x8014010b,
	CELL_MIC_ERROR_MUTEX = 0x8014010c,
	CELL_MIC_ERROR_EVENT_QUEUE = 0x8014010d,
	CELL_MIC_ERROR_DEVICE_NOT_FOUND = 0x8014010e,
	CELL_MIC_ERROR_SYSTEM_NOT_FOUND = 0x8014010e,
	CELL_MIC_ERROR_FATAL = 0x8014010f,
	CELL_MIC_ERROR_DEVICE_NOT_SUPPORT = 0x80140110,
};

struct CellMicInputFormat
{
	u8    channelNum;
	u8    subframeSize;
	u8    bitResolution;
	u8    dataType;
	be_t<u32>   sampleRate;
};

enum CellMicSignalState : u32
{
	CELL_MIC_SIGSTATE_LOCTALK = 0,
	CELL_MIC_SIGSTATE_FARTALK = 1,
	CELL_MIC_SIGSTATE_NSR     = 3,
	CELL_MIC_SIGSTATE_AGC     = 4,
	CELL_MIC_SIGSTATE_MICENG  = 5,
	CELL_MIC_SIGSTATE_SPKENG  = 6,
};

enum CellMicCommand
{
	CELL_MIC_ATTACH = 2,
	CELL_MIC_DATA = 5,
};

enum CellMicDeviceAttr : u32
{
	CELLMIC_DEVATTR_LED     = 9,
	CELLMIC_DEVATTR_GAIN    = 10,
	CELLMIC_DEVATTR_VOLUME  = 201,
	CELLMIC_DEVATTR_AGC     = 202,
	CELLMIC_DEVATTR_CHANVOL = 301,
	CELLMIC_DEVATTR_DSPTYPE = 302,
};

enum CellMicSignalType : u8
{
	CELLMIC_SIGTYPE_NULL = 0,
	CELLMIC_SIGTYPE_DSP  = 1,
	CELLMIC_SIGTYPE_AUX  = 2,
	CELLMIC_SIGTYPE_RAW  = 4,
};

enum CellMicType : s32
{
	CELLMIC_TYPE_UNDEF     = -1,
	CELLMIC_TYPE_UNKNOWN   = 0,
	CELLMIC_TYPE_EYETOY1   = 1,
	CELLMIC_TYPE_EYETOY2   = 2,
	CELLMIC_TYPE_USBAUDIO  = 3,
	CELLMIC_TYPE_BLUETOOTH = 4,
	CELLMIC_TYPE_A2DP      = 5,
} CellMicType;

enum microphone_device_type
{
	MICDEVICE_UNKNOWN,
	MICDEVICE_STANDARD,
	MICDEVICE_SINGSTAR
};

struct simple_ringbuf
{
	std::vector<u8> m_container;
	u32 m_size = 0; // Size of buf
	u32 m_head = 0; // Write index
	u32 m_tail = 0; // Read index
	u32 m_used = 0; // Size used

	void set_size(u32 newsize)
	{
		m_container.resize(newsize, 0);
		m_size = newsize;
	}

	// Assumes size < m_size
	void write_bytes(u8* buf, u32 size)
	{
		if (u32 over_size = m_used + size; over_size > m_size)
		{
			//buffer is full, need to override data
			over_size -= m_size;
			m_tail += over_size;
			if (m_tail > m_size) m_tail -= m_size;
			m_used = m_size;
		}
		else
		{
			m_used += size;
		}

		u8* data = m_container.data();
		u32 new_head = m_head + size;

		if (new_head >= m_size)
		{
			u32 first_chunk = m_size - m_head;
			memcpy(data + m_head, buf, first_chunk);
			memcpy(data, buf + first_chunk, size - first_chunk);
			m_head = (new_head - m_size);
		}
		else
		{
			memcpy(data + m_head, buf, size);
			m_head = new_head;
		}
	}

	u32 read_bytes(u8* buf, u32 size)
	{
		u32 to_read = size > m_used ? m_used : size;
		if (!to_read)
			return 0;

		u8* data = m_container.data();
		u32 new_tail = m_tail + to_read;

		if (new_tail >= m_size)
		{
			u32 first_chunk = m_size - m_tail;
			memcpy(buf, data + m_tail, first_chunk);
			memcpy(buf + first_chunk, data, to_read - first_chunk);
			m_tail = (new_tail - m_size);
		}
		else
		{
			memcpy(buf, data + m_tail, to_read);
			m_tail = new_tail;
		}

		m_used -= to_read;

		return to_read;
	}
};

struct microphone_device
{
	microphone_handler device_type;
	std::vector<std::string> device_name;

	// Sampling information provided at opening of mic
	u32 raw_samplingrate = 48000;
	u32 dsp_samplingrate = 48000;
	u32 aux_samplingrate = 48000;
	u8 bit_resolution = 16;
	u8 num_channels = 2;

	u8 signal_types = CELLMIC_SIGTYPE_NULL;
	u32 inbuf_size = 400000; // Default value unknown

	u32 sample_size; // Determined at opening for internal use

	bool mic_opened = false;
	bool mic_started = false;

	// Microphone attributes
	u32 attr_gain = 3;
	u32 attr_volume = 145;
	u32 attr_agc = 0;
	u32 attr_chanvol[2] = { 145, 145 };
	u32 attr_led = 0;
	u32 attr_dsptype = 0;

	std::vector<ALCdevice*> input_devices;
	std::vector<std::vector<u8>> internal_bufs;
	std::vector<u8> temp_buf;

	simple_ringbuf rbuf_raw;
	simple_ringbuf rbuf_dsp;
	simple_ringbuf rbuf_aux;

	s32 open_microphone();
	s32 close_microphone();
	s32 start_microphone();
	s32 stop_microphone();
};

class mic_context
{
public:
	void operator()();

	u64 event_queue_key = 0;

	std::unordered_map<u8, microphone_device> mic_list;

protected:
	void variable_byteswap(void* src, void* dst, u32 bytesize);

	u32 get_capture(microphone_device& mic);
	void get_raw(microphone_device& mic, u32 num_samples);
	void get_dsp(microphone_device& mic, u32 num_samples);

protected:
	const u64 start_time = get_system_time();
	u64 m_counter = 0;

	//u32 signalStateLocalTalk = 9; // value is in range 0-10. 10 indicates talking, 0 indicating none.
	//u32 signalStateFarTalk = 0; // value is in range 0-10. 10 indicates talking from far away, 0 indicating none.
	//f32 signalStateNoiseSupression; // value is in decibels
	//f32 signalStateGainControl;
	//f32 signalStateMicSignalLevel; // value is in decibels
	//f32 signalStateSpeakerSignalLevel; // value is in decibels
};

using mic_thread = named_thread<mic_context>;
