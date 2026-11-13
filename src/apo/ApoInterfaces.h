#pragma once
// Interfaces COM de Audio Processing Object (APO), equivalentes às do Windows
// SDK (audioenginebaseapo.h / audiomediatype.h), que o MinGW não traz. O
// layout das vtables e das structs segue o SDK; os IIDs foram conferidos nos
// binários do próprio Windows (audioeng.dll, audiodg.exe).

#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <mmdeviceapi.h>
#include <audioapotypes.h>
#include <mmreg.h>

namespace loudmixer::apo {

// --- Tipos ---------------------------------------------------------------------

struct UNCOMPRESSEDAUDIOFORMAT {
    GUID guidFormatType;
    DWORD dwSamplesPerFrame;
    DWORD dwBytesPerSampleContainer;
    DWORD dwValidBitsPerSample;
    FLOAT fFramesPerSecond;
    DWORD dwChannelMask;
};

enum APO_FLAG : UINT32 {
    APO_FLAG_NONE = 0x0,
    APO_FLAG_INPLACE = 0x1,
    APO_FLAG_SAMPLESPERFRAME_MUST_MATCH = 0x2,
    APO_FLAG_FRAMESPERSECOND_MUST_MATCH = 0x4,
    APO_FLAG_BITSPERSAMPLE_MUST_MATCH = 0x8,
    APO_FLAG_MIXER = 0x10,
    APO_FLAG_DEFAULT = 0xE,
};

struct APO_REG_PROPERTIES {
    CLSID clsid;
    APO_FLAG Flags;
    WCHAR szFriendlyName[256];
    WCHAR szCopyrightInfo[256];
    UINT32 u32MajorVersion;
    UINT32 u32MinorVersion;
    UINT32 u32MinInputConnections;
    UINT32 u32MaxInputConnections;
    UINT32 u32MinOutputConnections;
    UINT32 u32MaxOutputConnections;
    UINT32 u32MaxInstances;
    UINT32 u32NumAPOInterfaces;
    IID iidAPOInterfaceList[1];
};

enum APO_CONNECTION_BUFFER_TYPE {
    APO_CONNECTION_BUFFER_TYPE_ALLOCATED = 0,
    APO_CONNECTION_BUFFER_TYPE_EXTERNAL = 1,
    APO_CONNECTION_BUFFER_TYPE_DEPENDANT = 2,
};

struct IAudioMediaType;

struct APO_CONNECTION_DESCRIPTOR {
    APO_CONNECTION_BUFFER_TYPE Type;
    UINT_PTR pBuffer;
    UINT32 u32MaxFrameCount;
    IAudioMediaType* pFormat;
    UINT32 u32Signature;
};

struct APOInitBaseStruct {
    UINT32 cbSize;
    CLSID clsid;
};

// Prefixo comum de APOInitSystemEffects, APOInitSystemEffects2 e
// APOInitSystemEffects3: só lemos estes dois campos.
struct APOInitSystemEffectsPrefix {
    APOInitBaseStruct APOInit;
    IPropertyStore* pAPOEndpointProperties;
};

// APOInitSystemEffects2 (Windows 8.1+), só para leitura de diagnóstico.
struct APOInitSystemEffects2Layout {
    APOInitBaseStruct APOInit;
    IPropertyStore* pAPOEndpointProperties;
    IPropertyStore* pAPOSystemEffectsProperties;
    void* pReserved;
    IMMDeviceCollection* pDeviceCollection;
    UINT nSoftwareIoDeviceInCollection;
    UINT nSoftwareIoConnectorIndex;
    GUID AudioProcessingMode;
    BOOL InitializeForDiscoveryOnly;
};

constexpr HRESULT APOERR_ALREADY_INITIALIZED = static_cast<HRESULT>(0x887D0001L);
constexpr HRESULT APOERR_NOT_INITIALIZED = static_cast<HRESULT>(0x887D0002L);
constexpr HRESULT APOERR_FORMAT_NOT_SUPPORTED = static_cast<HRESULT>(0x887D0003L);
constexpr HRESULT APOERR_INVALID_APO_CLSID = static_cast<HRESULT>(0x887D0004L);
constexpr HRESULT APOERR_ALREADY_UNLOCKED = static_cast<HRESULT>(0x887D0006L);
constexpr HRESULT APOERR_NUM_CONNECTIONS_INVALID = static_cast<HRESULT>(0x887D0007L);
constexpr HRESULT APOERR_APO_LOCKED = static_cast<HRESULT>(0x887D000AL);

// --- Interfaces ----------------------------------------------------------------

// Interfaces COM não têm destrutor virtual por design (o objeto se destrói
// no próprio Release()).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif


// {4E997F73-B71F-4798-873B-ED7DFCF15B4D}
struct IAudioMediaType : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL* pfCompressed) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType* pIAudioType, DWORD* pdwFlags) = 0;
    virtual const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* pFormat) = 0;
};

// {FD7F2B29-24D0-4B5C-B177-592C39F9CA10}
struct IAudioProcessingObject : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* pTime) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) = 0;
    virtual HRESULT STDMETHODCALLTYPE Initialize(UINT32 cbDataSize, BYTE* pbyData) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* pOppositeFormat,
                                                             IAudioMediaType* pRequestedInputFormat,
                                                             IAudioMediaType** ppSupportedInputFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* pOppositeFormat,
                                                              IAudioMediaType* pRequestedOutputFormat,
                                                              IAudioMediaType** ppSupportedOutputFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* pu32ChannelCount) = 0;
};

// {9E1D6A6D-DDBC-4E95-A4C7-AD64BA37846C}
struct IAudioProcessingObjectRT : public IUnknown {
    virtual void STDMETHODCALLTYPE APOProcess(UINT32 u32NumInputConnections,
                                              APO_CONNECTION_PROPERTY** ppInputConnections,
                                              UINT32 u32NumOutputConnections,
                                              APO_CONNECTION_PROPERTY** ppOutputConnections) = 0;
    virtual UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 u32OutputFrameCount) = 0;
    virtual UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 u32InputFrameCount) = 0;
};

// {0E5ED805-ABA6-49C3-8F9A-2B8C889C4FA8}
struct IAudioProcessingObjectConfiguration : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 u32NumInputConnections,
                                                     APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                                     UINT32 u32NumOutputConnections,
                                                     APO_CONNECTION_DESCRIPTOR** ppOutputConnections) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockForProcess() = 0;
};

// {5FA00F27-ADD6-499A-8A9D-6B98521FA75B}: marcador, sem métodos.
struct IAudioSystemEffects : public IUnknown {};

// {BAFE99D2-7436-44CE-9E0E-4D89AFBFFF56}
struct IAudioSystemEffects2 : public IAudioSystemEffects {
    virtual HRESULT STDMETHODCALLTYPE GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) = 0;
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// --- IIDs e CLSIDs ---------------------------------------------------------------

constexpr IID IID_IAudioMediaType = {0x4E997F73, 0xB71F, 0x4798, {0x87, 0x3B, 0xED, 0x7D, 0xFC, 0xF1, 0x5B, 0x4D}};
constexpr IID IID_IAudioProcessingObject = {0xFD7F2B29, 0x24D0, 0x4B5C, {0xB1, 0x77, 0x59, 0x2C, 0x39, 0xF9, 0xCA, 0x10}};
constexpr IID IID_IAudioProcessingObjectRT = {0x9E1D6A6D, 0xDDBC, 0x4E95, {0xA4, 0xC7, 0xAD, 0x64, 0xBA, 0x37, 0x84, 0x6C}};
constexpr IID IID_IAudioProcessingObjectConfiguration = {0x0E5ED805, 0xABA6, 0x49C3, {0x8F, 0x9A, 0x2B, 0x8C, 0x88, 0x9C, 0x4F, 0xA8}};
constexpr IID IID_IAudioSystemEffects = {0x5FA00F27, 0xADD6, 0x499A, {0x8A, 0x9D, 0x6B, 0x98, 0x52, 0x1F, 0xA7, 0x5B}};
constexpr IID IID_IAudioSystemEffects2 = {0xBAFE99D2, 0x7436, 0x44CE, {0x9E, 0x0E, 0x4D, 0x89, 0xAF, 0xBF, 0xFF, 0x56}};

// CLSID do APO do LoudMixer: {9B7157F3-47F2-40FB-80E9-80327D8ECBF2}
constexpr CLSID CLSID_LoudMixerApo = {0x9B7157F3, 0x47F2, 0x40FB, {0x80, 0xE9, 0x80, 0x32, 0x7D, 0x8E, 0xCB, 0xF2}};
constexpr const wchar_t* kApoClsidString = L"{9B7157F3-47F2-40FB-80E9-80327D8ECBF2}";

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
constexpr GUID kSubtypeIeeeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// PKEY_AudioEndpoint_GUID = {1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E}, 4
constexpr PROPERTYKEY kPkeyAudioEndpointGuid = {{0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 4};

} // namespace loudmixer::apo
