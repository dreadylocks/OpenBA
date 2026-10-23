#pragma once
// ============================================================================
// src/engine/audio/audio_pcm.h -- the decode-at-load arithmetic (Phase D.4 SDL-4)
// ============================================================================
//
// D4_RULES §3 Q5: a non-streamed SoundBuffer whose format is xWMA is decoded to
// S16 PCM when the resource loads (SoundBuffer::DecodeAtLoad,
// vtbatch7_soundbuffer_soundeffect.cpp). Two pure pieces of that live here, so the
// unit tests reach them without an engine object:
//
//   * RewriteFormatAsPcm -- the WAVEFORMATEX the PCM is described by.
//   * XwmaPcmPlayBegin   -- where in the PCM a play starts, for retail's PlayBegin.
//
// THE START RULE. PlayPlatform (audio_play_platform.cpp) computes retail's
// PlayBegin exactly as retail does: `nBlockAlign * idx` with the xWMA packet size
// as nBlockAlign, idx the first seek-table entry above the byte position. Retail
// hands that value and the seek table to XAudio2, which decodes xWMA from a packet
// boundary. MEASURED against XAudio2 2.7 (handins/XWMA.md open item A4.3, exact to
// 0.0 on Sheep1 at PlayBegin 6690 and on Crowd at 22300):
//   let k be the packet whose table range holds PlayBegin in samples,
//       SeekSamples(k) <= PlayBegin < SeekSamples(k + 1);
//   XAudio2 plays the packet-0 PCM from sample PlayBegin when k == 0, and from
//   PlayBegin + FrameLength when k > 0, to the end.
// So once the PCM is in hand the seek table becomes a sample offset: the start is
// that sample of the decoded buffer (clamped to its length).
//
// PORTABILITY: no OS header, no engine type.
// ============================================================================

#include <cstdint>

namespace Platform { struct AudioWaveFormat; }

namespace Audio {

// Rewrite an xWMA WAVEFORMATEX in place as the 16-bit PCM it decodes to: tag 1
// (WAVE_FORMAT_PCM), the same channels and rate, nBlockAlign 2 * channels,
// nAvgBytesPerSec rate * nBlockAlign, wBitsPerSample 16, cbSize 0.
void RewriteFormatAsPcm(Platform::AudioWaveFormat& format);

// The first PCM sample a play starts at.
//   playBegin    retail's PlayBegin (samples, computed with the xWMA packet size)
//   seekTable    the xWMA cumulative decoded-byte table, packetCount entries
//   channels     the stream's channel count (a table entry / (2 * channels) = samples)
//   frameLength  the WMA frame length in samples (XwmaDecoder::FrameLength, 2048 at
//                44.1 / 48 kHz)
//   pcmSamples   the decoded buffer's length in samples (the result never exceeds it)
uint32_t XwmaPcmPlayBegin(uint32_t playBegin, const uint32_t* seekTable, uint32_t packetCount,
                          uint32_t channels, uint32_t frameLength, uint32_t pcmSamples);

// EXT SEAM (NOT RETAIL, docs/extensions/handins/SEAMLESS.md): ext.seamless_loops' gate -- DecodeAtLoad hands a buffer's decoded PCM to
// SoundBuffer::_extLoopSeam only when the seam is installed (the row ext.seamless_loops was on at boot),
// the buffer loops (SoundBuffer::_loopForever: PlayPlatform's loop count comes from that flag alone, so a
// one-shot buffer is never looped and a looping one never plays once), and the decode produced PCM.
// The seam is tested first: with the row off this is one null test.
inline bool LoopSeamApplies(bool seamInstalled, bool loopForever, const void* pcm, uint64_t frames) {
    return seamInstalled && loopForever && pcm != nullptr && frames > 0;
}

}  // namespace Audio
