#ifndef SAR_VIRTUAL_WASAPI_TRANSPORT_ABI_H_
#define SAR_VIRTUAL_WASAPI_TRANSPORT_ABI_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAR_VWASAPI_TRANSPORT_MAGIC UINT32_C(0x56574153)
#define SAR_VWASAPI_TRANSPORT_VERSION UINT16_C(1)
#define SAR_VWASAPI_TRANSPORT_ALIGNMENT UINT32_C(64)
#define SAR_VWASAPI_TRANSPORT_MAX_BYTES UINT32_C(0x04000000)
#define SAR_VWASAPI_TRANSPORT_MIN_SLOTS UINT32_C(2)
#define SAR_VWASAPI_TRANSPORT_MAX_SLOTS UINT32_C(1024)
#define SAR_VWASAPI_TRANSPORT_MAX_CHANNELS UINT32_C(256)
#define SAR_VWASAPI_TRANSPORT_MAX_FRAMES_PER_SLOT UINT32_C(8192)

#define SAR_VWASAPI_DIRECTION_RENDER UINT32_C(1)
#define SAR_VWASAPI_DIRECTION_CAPTURE UINT32_C(2)

#define SAR_VWASAPI_SAMPLE_PCM_INT UINT32_C(1)
#define SAR_VWASAPI_SAMPLE_IEEE_FLOAT UINT32_C(2)

#define SAR_VWASAPI_RING_FLAG_PRODUCER_ATTACHED UINT32_C(0x00000001)
#define SAR_VWASAPI_RING_FLAG_RECEIVER_ATTACHED UINT32_C(0x00000002)
#define SAR_VWASAPI_SLOT_FLAG_SILENT UINT32_C(0x00000001)
#define SAR_VWASAPI_SLOT_FLAG_DISCONTINUITY UINT32_C(0x00000002)

/* All offsets are from the first byte of SarVirtualWasapiTransportHeader. */
typedef struct SarVirtualWasapiTransportHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t header_size;
  uint32_t total_size;
  uint32_t direction;
  uint32_t sample_format;
  uint32_t sample_rate;
  uint32_t channel_count;
  uint32_t bits_per_sample;
  uint32_t valid_bits_per_sample;
  uint32_t frames_per_slot;
  uint32_t slot_count;
  uint32_t ring_state_offset;
  uint32_t ring_state_size;
  uint32_t slot_table_offset;
  uint32_t slot_stride;
  uint32_t audio_data_offset;
  uint32_t audio_slot_stride;
  uint32_t reserved[15];
} SarVirtualWasapiTransportHeader;

/* Access shared 64-bit fields with Interlocked* in KMDF and atomic_ref in C++. */
typedef struct SarVirtualWasapiRingState {
  uint64_t producer_sequence;
  uint64_t consumer_sequence;
  uint64_t generation;
  uint64_t produced_frames;
  uint64_t consumed_frames;
  uint64_t dropped_frames;
  uint64_t silence_frames;
  uint64_t malformed_packets;
  uint32_t flags;
  uint32_t reserved32;
  uint64_t reserved[7];
} SarVirtualWasapiRingState;

typedef struct SarVirtualWasapiSlotState {
  uint64_t sequence;
  uint32_t frame_count;
  uint32_t flags;
  uint64_t device_position;
  uint64_t qpc_position;
  uint64_t reserved[4];
} SarVirtualWasapiSlotState;

typedef struct SarVirtualWasapiTransportCapabilities {
  uint32_t abi_magic;
  uint16_t minimum_version;
  uint16_t maximum_version;
  uint32_t maximum_transport_bytes;
  uint32_t supported_directions;
  uint32_t supported_sample_formats;
  uint32_t alignment;
  uint32_t reserved[10];
} SarVirtualWasapiTransportCapabilities;

typedef struct SarVirtualWasapiAttachRequest {
  uint32_t size;
  uint32_t direction;
  uint64_t section_handle;
  uint32_t section_bytes;
  uint32_t reserved;
  uint64_t client_cookie;
} SarVirtualWasapiAttachRequest;

typedef struct SarVirtualWasapiAttachResponse {
  uint32_t size;
  uint32_t transport_version;
  uint64_t attachment_id;
  uint64_t generation;
  uint64_t reserved[2];
} SarVirtualWasapiAttachResponse;

typedef struct SarVirtualWasapiDetachRequest {
  uint32_t size;
  uint32_t reserved;
  uint64_t attachment_id;
} SarVirtualWasapiDetachRequest;

#define SAR_VWASAPI_FILE_DEVICE_SOUND UINT32_C(0x0000001d)
#define SAR_VWASAPI_METHOD_BUFFERED UINT32_C(0)
#define SAR_VWASAPI_FILE_READ_WRITE_ACCESS UINT32_C(3)
#define SAR_VWASAPI_CTL_CODE(device, function, method, access) \
  (((device) << 16) | ((access) << 14) | ((function) << 2) | (method))
#define SAR_VWASAPI_IOCTL_QUERY_CAPABILITIES \
  SAR_VWASAPI_CTL_CODE(SAR_VWASAPI_FILE_DEVICE_SOUND, UINT32_C(0x900), \
                       SAR_VWASAPI_METHOD_BUFFERED, SAR_VWASAPI_FILE_READ_WRITE_ACCESS)
#define SAR_VWASAPI_IOCTL_ATTACH_TRANSPORT \
  SAR_VWASAPI_CTL_CODE(SAR_VWASAPI_FILE_DEVICE_SOUND, UINT32_C(0x901), \
                       SAR_VWASAPI_METHOD_BUFFERED, SAR_VWASAPI_FILE_READ_WRITE_ACCESS)
#define SAR_VWASAPI_IOCTL_DETACH_TRANSPORT \
  SAR_VWASAPI_CTL_CODE(SAR_VWASAPI_FILE_DEVICE_SOUND, UINT32_C(0x902), \
                       SAR_VWASAPI_METHOD_BUFFERED, SAR_VWASAPI_FILE_READ_WRITE_ACCESS)

#ifdef __cplusplus
}

static_assert(sizeof(SarVirtualWasapiTransportHeader) == 128);
static_assert(offsetof(SarVirtualWasapiTransportHeader, ring_state_offset) == 44);
static_assert(sizeof(SarVirtualWasapiRingState) == 128);
static_assert(offsetof(SarVirtualWasapiRingState, flags) == 64);
static_assert(sizeof(SarVirtualWasapiSlotState) == 64);
static_assert(sizeof(SarVirtualWasapiTransportCapabilities) == 64);
static_assert(sizeof(SarVirtualWasapiAttachRequest) == 32);
static_assert(sizeof(SarVirtualWasapiAttachResponse) == 40);
static_assert(sizeof(SarVirtualWasapiDetachRequest) == 16);
#endif

#endif
