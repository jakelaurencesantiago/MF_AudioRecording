#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "main.h"

using namespace std;

#define MIC_RECORDING

#define AUDIO_CHANNELS 1 //MONO

#define RECORDING_DURATION_MAX MAXDWORD
#define RECORDING_DURATION_10sec 10000 //ms

BOOL bRunning = FALSE;

void Recording() {
	HRESULT hr;

	hr = CoInitializeEx(0, COINIT_MULTITHREADED);

	if (FAILED(hr)) return;

	hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);

	if (FAILED(hr)) {
		CoUninitialize();
		return;
	}

	IMFMediaSource* pSource = NULL;
	IMFSourceReader* pReader = NULL;
	HANDLE hFile = INVALID_HANDLE_VALUE;

	do {

#ifdef MIC_RECORDING
		hr = CreateSourceReaderFromDevice(&pSource, &pReader);
#else
		hr = MFCreateSourceReaderFromURL(L"sample_in.wav", NULL, &pReader);
#endif
		if (FAILED(hr)) break;

		// create output file
		hFile = CreateFile(L"sample_out.wav", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);

		if (hFile == INVALID_HANDLE_VALUE) break;

		bRunning = TRUE;
		cout << "recording...\n";
		hr = WriteWaveFile(pReader, hFile, RECORDING_DURATION_10sec);

	} while (0);

	ShutDownRelease(&pSource);
	SafeRelease(&pReader);

	if (hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile);
	}

	MFShutdown();

	CoUninitialize();

	cout << (SUCCEEDED(hr) ? "success recording" : "failed recording") << endl;
}

int main()
{
	
	thread thRecording(Recording);
	while (true) {
		char ch = getchar();

		if (ch == 's') {
			bRunning = FALSE;
			thRecording.join();
			break;
		}
	}


	return 0;
}

HRESULT CreateSourceReaderFromDevice(IMFMediaSource **ppSource, IMFSourceReader **ppReader) {

	HRESULT hr = S_OK;

	IMFAttributes* pConfig = NULL;
	IMFActivate** pDevices = NULL;
	UINT32 deviceCnt = 0;

	do {
		// create search attributes
		hr = MFCreateAttributes(&pConfig, 1);

		if (FAILED(hr)) break;

		// set search criteria
		hr = pConfig->SetGUID(
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);

		if (FAILED(hr)) break;

		// enumerate devices
		hr = MFEnumDeviceSources(pConfig, &pDevices, &deviceCnt);

		if (FAILED(hr)) break;

		IMFMediaSource* pSource = NULL;
		BOOL success = FALSE;

		for (UINT32 i = 0; !success && i < deviceCnt; i++) {

			WCHAR* name = NULL;
			UINT32 len = 0;

			// get device friendly name
			hr = pDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
			if (SUCCEEDED(hr)) {
				printf("%S\n", name);
			}

			CoTaskMemFree(name);
			len = 0;

			// get device endpoint id
			hr = pDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, &name, &len);
			if (SUCCEEDED(hr) && name != NULL) {
				printf("%S\n", name);

				// get media source from device
				IMFAttributes* pAttr = NULL;

				hr = MFCreateAttributes(&pAttr, 2);

				if (SUCCEEDED(hr)) {
					hr = pAttr->SetGUID(
						MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
						MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
				}

				if (SUCCEEDED(hr)) {
					hr = pAttr->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, name);
				}

				if (SUCCEEDED(hr)) {
					hr = MFCreateDeviceSource(pAttr, &pSource);
					success = SUCCEEDED(hr); // true - device source created
				}

				SafeRelease(&pAttr);

				// or
				//hr = pDevices[i]->ActivateObject(IID_PPV_ARGS(&pSource));
			}

			CoTaskMemFree(name);
		}

		// device count is 0 or failed to create source
		if (!success) {
			if (deviceCnt == 0) {
				hr = E_FAIL;
			}
			break;
		}

		IMFSourceReader* pReader = NULL;
		// create the source reader
		hr = MFCreateSourceReaderFromMediaSource(pSource, NULL, &pReader);

		if (SUCCEEDED(hr)) {
			// get references
			*ppSource = pSource;

			*ppReader = pReader;
			(*ppReader)->AddRef();
		}
		else { // shutdown source on error
			ShutDownRelease(&pSource);
		}

		SafeRelease(&pReader);

	} while (0);



	// destroy objects
	for (UINT32 i = 0; i < deviceCnt; i++) {
		SafeRelease(&pDevices[i]);
	}
	CoTaskMemFree(pDevices);

	SafeRelease(&pConfig);

	return hr;
}

HRESULT WriteWaveFile(IMFSourceReader *pReader, HANDLE hFile, LONG msDuration) {
	HRESULT hr = S_OK;

	IMFMediaType* pAudioType = NULL;
	DWORD cbHeaderSize = 0;
	DWORD cbAudioWritten = 0;
	WAVEFORMATEX* pWav = NULL;

	do {
		
		hr = ConfigureAudioFormat(pReader, &pAudioType);

		if (FAILED(hr)) break;
		
		hr = WriteFileHeader(hFile, pAudioType, &pWav, &cbHeaderSize);

		if (FAILED(hr)) break;

		DWORD cbMaxAudioDataSize = CalculateMaxAudioDataSize(pAudioType, cbHeaderSize, msDuration);
		
		hr = WriteWaveData(hFile, pReader, pWav, cbMaxAudioDataSize, &cbAudioWritten);

		if (FAILED(hr)) break;

		hr = UpdateFileHeaders(hFile, cbHeaderSize, cbAudioWritten);

	} while (0);

	SafeRelease(&pAudioType);

	if (pWav != NULL) {
		CoTaskMemFree(pWav);
		pWav = NULL;
	}

	return hr;
}

//
// Configure audio stream format to PCM
//
HRESULT ConfigureAudioFormat(IMFSourceReader *pReader, IMFMediaType **ppMediaType) {
	HRESULT hr = S_OK;

	IMFMediaType* pPartialType = NULL;
	IMFMediaType* pUncompressedType = NULL;

	do {

		// create media type for uncompressed PCM audio
		hr = MFCreateMediaType(&pPartialType);

		if (FAILED(hr)) break;

		// set media type
		hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		
		if (FAILED(hr)) break;

		hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

		if (FAILED(hr)) break;

		// set channel to MONO
		hr = pPartialType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, AUDIO_CHANNELS);

		if (FAILED(hr)) break;

		// set reader to the same media type
		hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL,
			pPartialType);

		if (FAILED(hr)) break;
		
		// get the uncompressed format
		hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			&pUncompressedType);

		if (FAILED(hr)) break;

		// select the stream
		hr = pReader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);


		if (FAILED(hr)) break;

		*ppMediaType = pUncompressedType;
		(*ppMediaType)->AddRef(); //add reference to the pointer


	} while (0);

	SafeRelease(&pPartialType);
	SafeRelease(&pUncompressedType);

	return hr;
}

//
// wave file header
//
HRESULT WriteFileHeader(HANDLE hFile, IMFMediaType *pMediaType, WAVEFORMATEX **ppWav, DWORD *pcbWritten) {

	HRESULT hr = S_OK;

	UINT32 cbFormat = 0;

	WAVEFORMATEX* pWav = NULL;

	*pcbWritten = 0;

	do {
		// Convert PCM format into WAVEFORMATEX structure
		hr = MFCreateWaveFormatExFromMFMediaType(
			pMediaType,
			&pWav,
			&cbFormat);
		
		if (FAILED(hr)) break;
		
		DWORD cbWritten;

		WAV_HEADER wav_header;
		wav_header.file_size = 0; // temp size
		wav_header.block_size = 16;
		wav_header.data_size = 0; // temp size

		wav_header.audio_format = static_cast<UINT16> (pWav->wFormatTag);
		wav_header.channels = static_cast<UINT16> (pWav->nChannels);
		wav_header.frequency = static_cast<UINT32> (pWav->nSamplesPerSec);
		wav_header.byte_per_sec = static_cast<UINT32> (pWav->nAvgBytesPerSec);
		wav_header.byte_per_block = static_cast<UINT16> (pWav->nBlockAlign);
		wav_header.bits_per_sample = static_cast<UINT16> (pWav->wBitsPerSample);

		// Adjust format to valid Wave file header
		if (wav_header.audio_format == WAVE_FORMAT_EXTENSIBLE) {
			wav_header.audio_format = WAVE_FORMAT_PCM; // update to PCM format
			wav_header.channels = AUDIO_CHANNELS; // Mono
			wav_header.byte_per_block = wav_header.channels * wav_header.bits_per_sample / 8; // adjust to channel size
			wav_header.byte_per_sec = wav_header.byte_per_block * wav_header.frequency; // adjust to channel size
		}

		// write header info into file
		hr = WriteToFile(hFile, &wav_header, sizeof(wav_header), &cbWritten);

		if (FAILED(hr)) break;

		*pcbWritten = cbWritten;

	} while (0);

	*ppWav = pWav;

	return hr;
}

// 
// Decode PCM audio data and write into file
//
HRESULT	WriteWaveData(HANDLE hFile, IMFSourceReader *pReader, WAVEFORMATEX* pWav, DWORD cbMaxAudioDataSize, DWORD *pcbDataWritten) {
	HRESULT hr = S_OK;

	DWORD cbAudioDataSize = 0;
	DWORD cbBufferSize = 0;
	BYTE* pBufferData = NULL;

	IMFSample* pSample = NULL;
	IMFMediaBuffer* pBuffer = NULL;

	BOOL bBuffLock = FALSE;
	
	// write data into file
	while (bRunning) {
		DWORD dwFlags = 0;

		hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			0,
			NULL,
			&dwFlags,
			NULL,
			&pSample);

		if (FAILED(hr) ||
			dwFlags & MF_SOURCE_READERF_ERROR ||
			dwFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED || // media type changed and not supported by WAVE file format
			dwFlags & MF_SOURCE_READERF_ENDOFSTREAM // end of stream
			) {
			break;
		}

		if (pSample == NULL) {
			// no sample
			continue;
		}

		// get audio data sample pointer
		hr = pSample->ConvertToContiguousBuffer(&pBuffer);

		if (FAILED(hr)) break;

		// lock buffer; get byte data
		hr = pBuffer->Lock(&pBufferData, NULL, &cbBufferSize);

		if (FAILED(hr)) break;

		bBuffLock = TRUE;

		vector<BYTE> bufferVector;
		bufferVector.reserve(cbBufferSize / pWav->nBlockAlign * pWav->wBitsPerSample / 8); // reserve capacity in advance to avoid reallocation
		for (DWORD i = 0; i < cbBufferSize; i += pWav->nBlockAlign) {
			// assume 2 bytes per sample and MONO channel
			bufferVector.push_back(pBufferData[i]);
			bufferVector.push_back(pBufferData[i + 1]);
		}

		// update size
		cbBufferSize = bufferVector.size();

		if (cbMaxAudioDataSize - cbAudioDataSize < cbBufferSize) {
			cbBufferSize = cbMaxAudioDataSize - cbAudioDataSize;
		}

		DWORD cbBuffWrite = 0;
		hr = WriteToFile(hFile, bufferVector.data(), cbBufferSize, &cbBuffWrite);

		if (FAILED(hr)) break;

		cbAudioDataSize += cbBufferSize;

		// unlock buffer
		hr = pBuffer->Unlock();
		pBufferData = NULL;

		if (FAILED(hr)) break;

		bBuffLock = FALSE;

		SafeRelease(&pBuffer);
		SafeRelease(&pSample);

		if (cbAudioDataSize >= cbMaxAudioDataSize) {
			break;
		}

	}

	if (SUCCEEDED(hr)) {
		// total bytes written
		*pcbDataWritten = cbAudioDataSize;
	}

	if (pBuffer != NULL) {
		if (bBuffLock) {
			// unlock buffer if remained lock
			pBuffer->Unlock();
		}
		SafeRelease(&pBuffer);
	}

	SafeRelease(&pSample);

	return hr;
}

HRESULT WriteToFile(HANDLE hFile, void* data, DWORD cbWriteSize, DWORD *cbWritten) {
	DWORD cbSize;
	BOOL res = FALSE;

	// write header info into file
	res = WriteFile(hFile, data, cbWriteSize, &cbSize, NULL);

	*cbWritten = cbSize;

	return res ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

// Calculates how much audio to write to the WAVE file, given the 
// audio format and the maximum duration of the WAVE file.
DWORD CalculateMaxAudioDataSize(IMFMediaType *pMediaType, DWORD cbHeader, DWORD msecDuration) {
	
	UINT32 cbBlockAlign = 0; // bytes in an audio block
	UINT32 cbBytesPerSec = 0;
	UINT32 cbBitsPerSample = 0;
	UINT32 cbSamplePerSec = 0;
	BOOL isWavEx = TRUE;

	// true - WAVEFORMATEX; false otherwise
	isWavEx = (BOOL)MFGetAttributeUINT32(pMediaType, MF_MT_AUDIO_PREFER_WAVEFORMATEX, 0);

	// get block size and Bps
	if (isWavEx) {
		cbBlockAlign = MFGetAttributeUINT32(pMediaType, MF_MT_AUDIO_BLOCK_ALIGNMENT, 0);
		cbBytesPerSec = MFGetAttributeUINT32(pMediaType, MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 0);
	}
	else { // WAVE_FORMAT_EXTENSIBLE
		cbBitsPerSample = MFGetAttributeUINT32(pMediaType, MF_MT_AUDIO_BITS_PER_SAMPLE, 0);
		cbSamplePerSec = MFGetAttributeUINT32(pMediaType, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
		cbBlockAlign = AUDIO_CHANNELS * cbBitsPerSample / 8;
		cbBytesPerSec = cbBlockAlign * cbSamplePerSec;
	}


	// Calculate the maximum amount of audio data to write. 
	// This value equals (duration in seconds x bytes/second), but cannot
	// exceed the maximum size of the data chunk in the WAVE file.

	// largest possible size of data chunk
	DWORD cbMaxSize = MAXDWORD - cbHeader;

	// size of desired clip
	DWORD cbAudioClipSize = MulDiv(cbBytesPerSec, msecDuration, 1000);

	cbAudioClipSize = min(cbAudioClipSize, cbMaxSize);

	// round to the audio block size to prevent partial audio frame
	cbAudioClipSize = (cbAudioClipSize / cbBlockAlign) * cbBlockAlign;

	return cbAudioClipSize;
}

//
// Update the file size information of the WAVE file header
//
HRESULT UpdateFileHeaders(HANDLE hFile, 
	DWORD cbHeaderSize, // size of the header chunk
	DWORD cbAudioDataSize // size of the data chunk
) {
	HRESULT hr = S_OK;

	do {
		LARGE_INTEGER li;
		li.QuadPart = cbHeaderSize - sizeof(DWORD);

		if (SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) == 0) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			break;
		}

		DWORD cbOut = 0;

		// write data size
		hr = WriteToFile(hFile, &cbAudioDataSize, sizeof(cbAudioDataSize), &cbOut);

		if (FAILED(hr)) break;

		li.QuadPart = sizeof(FOURCC); // file size

		if (SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) == 0) {
			hr = HRESULT_FROM_WIN32(GetLastError());
			break;
		}

		// This is the size of the rest of the chunk 
		// following this number.This is the size of the
		// entire file in bytes minus 8 bytes for the
		// two fields not included in this count:
		// ChunkID and ChunkSize.
		DWORD cbChunkSize = cbHeaderSize + cbAudioDataSize - 8;

		hr = WriteToFile(hFile, &cbChunkSize, sizeof(cbChunkSize), &cbOut);

	} while (0);


	return hr;
}