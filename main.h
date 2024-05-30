#pragma once


#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

HRESULT CreateSourceReaderFromDevice(IMFMediaSource** ppSource, IMFSourceReader** ppReader);
HRESULT WriteWaveFile(IMFSourceReader* pReader, HANDLE hFile, LONG msDuration);
HRESULT ConfigureAudioFormat(IMFSourceReader* pReader, IMFMediaType** ppMediaType);
HRESULT WriteFileHeader(HANDLE hFile, IMFMediaType* pMediaType, WAVEFORMATEX** ppWav, DWORD* pcbWritten);
HRESULT	WriteWaveData(HANDLE hFile, IMFSourceReader* pReader, WAVEFORMATEX* pWav, DWORD cbMaxAudioDataSize, DWORD* pcbDataWritten);
HRESULT WriteToFile(HANDLE hFile, void* data, DWORD cbWriteSize, DWORD* cbWritten);
HRESULT UpdateFileHeaders(HANDLE hFile, DWORD cbHeaderSize, DWORD cbAudioDataSize);
DWORD   CalculateMaxAudioDataSize(IMFMediaType* pMediaType, DWORD cbHeader, DWORD msecDuration);

// wav - PCM header
struct WAV_HEADER {
	const UINT8 riff_id[4] = { 'R','I','F','F' };
	UINT32 file_size;
	const UINT8 wave_id[4] = { 'W','A','V','E' };
	const UINT8 fmt_id[4] = { 'f','m','t',' ' };
	UINT32 block_size = 16;
	UINT16 audio_format = 1;
	UINT16 channels = 1;
	UINT32 frequency = 22050;
	UINT32 byte_per_sec = 44100;
	UINT16 byte_per_block = 2;
	UINT16 bits_per_sample = 16;
	const UINT8 data_id[4] = { 'd','a','t','a' };
	UINT32 data_size;
};


template <class T> void SafeRelease(T** ppT) {
	if (*ppT) {
		(*ppT)->Release();
		*ppT = NULL;
	}
}

void ShutDownRelease(IMFMediaSource **ppSource) {
	if (*ppSource) {
		(*ppSource)->Shutdown();
		SafeRelease(ppSource);
	}
}
