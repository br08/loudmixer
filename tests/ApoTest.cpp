// Testes do APO do LoudMixer (Windows). Carrega a DLL real e a exercita pelo
// mesmo caminho que o mecanismo de áudio usa (DllGetClassObject -> interfaces
// de APO), com IAudioMediaType e IPropertyStore falsos.

#include "apo/ApoInterfaces.h"
#include "apo/ApoShared.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace loudmixer::apo;

namespace {

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

// Objetos de pilha: AddRef/Release só contam (sem delete).
struct FakeMediaType final : IAudioMediaType {
    UNCOMPRESSEDAUDIOFORMAT format{};
    ULONG refs = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IAudioMediaType) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL* p) override
    {
        *p = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType*, DWORD*) override { return E_NOTIMPL; }
    const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat() override { return nullptr; }
    HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* p) override
    {
        *p = format;
        return S_OK;
    }
};

struct FakeProps final : IPropertyStore {
    std::wstring endpointGuid;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == __uuidof(IPropertyStore)) {
            *ppv = this;
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetCount(DWORD* c) override
    {
        *c = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAt(DWORD, PROPERTYKEY*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY key, PROPVARIANT* v) override
    {
        PropVariantInit(v);
        if (key.fmtid == kPkeyAudioEndpointGuid.fmtid && key.pid == kPkeyAudioEndpointGuid.pid) {
            const size_t bytes = (endpointGuid.size() + 1) * sizeof(wchar_t);
            v->pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
            std::memcpy(v->pwszVal, endpointGuid.c_str(), bytes);
            v->vt = VT_LPWSTR;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetValue(REFPROPERTYKEY, REFPROPVARIANT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit() override { return E_NOTIMPL; }
};

// Objeto "externo" de agregação, como o que o audiodg passa ao criar o APO.
struct FakeOuter final : IUnknown {
    ULONG refs = 1;
    IUnknown* inner = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(riid, ppv) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Layout de APOInitSystemEffects (Windows 7+).
struct InitSystemEffects {
    APOInitBaseStruct APOInit;
    IPropertyStore* pAPOEndpointProperties;
    IPropertyStore* pAPOSystemEffectsProperties;
    void* pReserved;
    IMMDeviceCollection* pDeviceCollection;
};

constexpr GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

FakeMediaType floatFormat(DWORD channels, float rate)
{
    FakeMediaType t;
    t.format = {kSubtypeIeeeFloat, channels, 4, 32, rate, 0};
    return t;
}

std::wstring guidText(const GUID& g)
{
    wchar_t buf[64];
    StringFromGUID2(g, buf, 64);
    return buf;
}

template <typename T>
struct Ptr {
    T* p = nullptr;
    ~Ptr()
    {
        if (p)
            p->Release();
    }
    T* operator->() const { return p; }
    void** put() { return reinterpret_cast<void**>(&p); }
};

using GetClassObjectFn = HRESULT(__stdcall*)(REFCLSID, REFIID, LPVOID*);

class ApoTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::string path = LOUDMIXER_APO_DLL;
        for (auto& c : path)
            if (c == '/')
                c = '\\';
        dll_ = LoadLibraryA(path.c_str());
        ASSERT_NE(dll_, nullptr) << "não carregou " << path;
        getClassObject_ = reinterpret_cast<GetClassObjectFn>(
            reinterpret_cast<void*>(GetProcAddress(dll_, "DllGetClassObject")));
        ASSERT_NE(getClassObject_, nullptr);
    }

    void SetUp() override
    {
        ASSERT_NE(getClassObject_, nullptr);
        Ptr<IClassFactory> factory;
        ASSERT_EQ(getClassObject_(CLSID_LoudMixerApo, IID_IClassFactory, factory.put()), S_OK);
        ASSERT_EQ(factory->CreateInstance(nullptr, IID_IAudioProcessingObject, apo_.put()), S_OK);
        ASSERT_EQ(apo_->QueryInterface(IID_IAudioProcessingObjectRT, rt_.put()), S_OK);
        ASSERT_EQ(apo_->QueryInterface(IID_IAudioProcessingObjectConfiguration, cfg_.put()), S_OK);
    }

    // Inicializa como o mecanismo faria, com um endpoint de teste único.
    void initWithEndpoint()
    {
        GUID g;
        CoCreateGuid(&g);
        props_.endpointGuid = guidText(g);
        InitSystemEffects init{};
        init.APOInit.cbSize = sizeof(init);
        init.APOInit.clsid = CLSID_LoudMixerApo;
        init.pAPOEndpointProperties = &props_;
        ASSERT_EQ(apo_->Initialize(sizeof(init), reinterpret_cast<BYTE*>(&init)), S_OK);
        ASSERT_TRUE(shared_.open(props_.endpointGuid)) << "memória compartilhada não foi criada";
    }

    void lock(DWORD channels)
    {
        format_ = floatFormat(channels, 48000.0f);
        inDesc_ = {APO_CONNECTION_BUFFER_TYPE_EXTERNAL, 0, 512, &format_, 0};
        outDesc_ = inDesc_;
        APO_CONNECTION_DESCRIPTOR* in[] = {&inDesc_};
        APO_CONNECTION_DESCRIPTOR* out[] = {&outDesc_};
        ASSERT_EQ(cfg_->LockForProcess(1, in, 1, out), S_OK);
    }

    // Processa no lugar (como o mecanismo faz com APOs in-place).
    void process(std::vector<float>& buf, DWORD channels, APO_BUFFER_FLAGS flags = BUFFER_VALID)
    {
        APO_CONNECTION_PROPERTY conn{reinterpret_cast<UINT_PTR>(buf.data()),
                                     static_cast<UINT32>(buf.size() / channels), flags, 0};
        APO_CONNECTION_PROPERTY* in[] = {&conn};
        APO_CONNECTION_PROPERTY* out[] = {&conn};
        rt_->APOProcess(1, in, 1, out);
        lastFlags_ = conn.u32BufferFlags;
        lastFrames_ = conn.u32ValidFrameCount;
    }

    static inline HMODULE dll_ = nullptr;
    static inline GetClassObjectFn getClassObject_ = nullptr;
    Ptr<IAudioProcessingObject> apo_;
    Ptr<IAudioProcessingObjectRT> rt_;
    Ptr<IAudioProcessingObjectConfiguration> cfg_;
    FakeProps props_;
    FakeMediaType format_;
    APO_CONNECTION_DESCRIPTOR inDesc_{}, outDesc_{};
    SharedView shared_;
    APO_BUFFER_FLAGS lastFlags_ = BUFFER_INVALID;
    UINT32 lastFrames_ = 0;
};

} // namespace

TEST(ApoIids, MatchWindowsSdk)
{
    EXPECT_EQ(guidText(IID_IAudioProcessingObject), L"{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}");
    EXPECT_EQ(guidText(IID_IAudioProcessingObjectRT), L"{9E1D6A6D-DDBC-4E95-A4C7-AD64BA37846C}");
    EXPECT_EQ(guidText(IID_IAudioProcessingObjectConfiguration), L"{0E5ED805-ABA6-49C3-8F9A-2B8C889C4FA8}");
    EXPECT_EQ(guidText(IID_IAudioSystemEffects), L"{5FA00F27-ADD6-499A-8A9D-6B98521FA75B}");
    EXPECT_EQ(guidText(IID_IAudioSystemEffects2), L"{BAFE99D2-7436-44CE-9E0E-4D89AFBFFF56}");
    EXPECT_EQ(guidText(IID_IAudioMediaType), L"{4E997F73-B71F-4798-873B-ED7DFCF15B4D}");
    EXPECT_EQ(guidText(CLSID_LoudMixerApo), kApoClsidString);
    EXPECT_EQ(guidText(kPkeyAudioEndpointGuid.fmtid), L"{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E}");
}

TEST_F(ApoTest, ExposesApoInterfacesOnly)
{
    Ptr<IUnknown> unk;
    EXPECT_EQ(apo_->QueryInterface(IID_IAudioSystemEffects, unk.put()), S_OK);
    Ptr<IUnknown> unk2;
    EXPECT_EQ(apo_->QueryInterface(IID_IAudioSystemEffects2, unk2.put()), S_OK);
    Ptr<IUnknown> none;
    EXPECT_EQ(apo_->QueryInterface(__uuidof(IMMDevice), none.put()), E_NOINTERFACE);

    Ptr<IClassFactory> f;
    EXPECT_EQ(getClassObject_(IID_IUnknown, IID_IClassFactory, f.put()), CLASS_E_CLASSNOTAVAILABLE);
}

TEST_F(ApoTest, RegistrationProperties)
{
    APO_REG_PROPERTIES* p = nullptr;
    ASSERT_EQ(apo_->GetRegistrationProperties(&p), S_OK);
    EXPECT_TRUE(p->clsid == CLSID_LoudMixerApo);
    EXPECT_EQ(p->Flags, 0xFu); // in-place + formatos iguais nos dois lados
    EXPECT_EQ(p->u32NumAPOInterfaces, 1u);
    EXPECT_TRUE(p->iidAPOInterfaceList[0] == IID_IAudioProcessingObject);
    EXPECT_STREQ(p->szFriendlyName, L"LoudMixer Boost");
    CoTaskMemFree(p);
}

TEST_F(ApoTest, AcceptsOnlyFloat32WithMatchingLayout)
{
    auto stereo = floatFormat(2, 48000.0f);
    IAudioMediaType* supported = nullptr;
    EXPECT_EQ(apo_->IsInputFormatSupported(nullptr, &stereo, &supported), S_OK);
    EXPECT_EQ(supported, &stereo);
    supported->Release();

    FakeMediaType pcm16;
    pcm16.format = {kSubtypePcm, 2, 2, 16, 48000.0f, 0};
    EXPECT_EQ(apo_->IsInputFormatSupported(nullptr, &pcm16, &supported), APOERR_FORMAT_NOT_SUPPORTED);
    EXPECT_EQ(supported, nullptr);

    auto mono = floatFormat(1, 48000.0f);
    EXPECT_EQ(apo_->IsOutputFormatSupported(&stereo, &mono, &supported), APOERR_FORMAT_NOT_SUPPORTED);
    auto otherRate = floatFormat(2, 44100.0f);
    EXPECT_EQ(apo_->IsOutputFormatSupported(&stereo, &otherRate, &supported), APOERR_FORMAT_NOT_SUPPORTED);
}

TEST_F(ApoTest, LifecycleErrors)
{
    APO_CONNECTION_DESCRIPTOR d{};
    APO_CONNECTION_DESCRIPTOR* arr[] = {&d};
    EXPECT_EQ(cfg_->LockForProcess(1, arr, 1, arr), APOERR_NOT_INITIALIZED);
    ASSERT_EQ(apo_->Initialize(0, nullptr), S_OK);
    EXPECT_EQ(apo_->Initialize(0, nullptr), APOERR_ALREADY_INITIALIZED);
    EXPECT_EQ(cfg_->UnlockForProcess(), APOERR_ALREADY_UNLOCKED);
    lock(2);
    EXPECT_EQ(cfg_->LockForProcess(1, arr, 1, arr), APOERR_APO_LOCKED);
    EXPECT_EQ(cfg_->UnlockForProcess(), S_OK);
}

TEST_F(ApoTest, WithoutEndpointIsBitExactPassthrough)
{
    ASSERT_EQ(apo_->Initialize(0, nullptr), S_OK);
    lock(2);
    std::vector<float> buf = {0.9f, -0.9f, 0.1234567f, -0.5f};
    const auto original = buf;
    process(buf, 2);
    EXPECT_EQ(buf, original);
    EXPECT_EQ(lastFlags_, BUFFER_VALID);
    EXPECT_EQ(lastFrames_, 2u);
}

TEST_F(ApoTest, AppliesGainFromSharedMemoryAndPublishesStats)
{
    initWithEndpoint();
    auto* s = shared_.get();
    EXPECT_FLOAT_EQ(s->gain.load(), 1.0f); // sem valor no registro: 100%

    lock(2);
    EXPECT_EQ(s->sampleRate.load(), 48000u);
    EXPECT_EQ(s->channels.load(), 2u);
    EXPECT_EQ(s->lockedInstances.load(), 1u);

    s->gain.store(2.0f);
    std::vector<float> buf(2 * 256, 0.25f);
    process(buf, 2);
    for (float v : buf)
        EXPECT_NEAR(v, 0.5f, 1e-6f); // abaixo do joelho: linear
    EXPECT_EQ(s->heartbeat.load(), 1u);
    EXPECT_EQ(s->processedSamples.load(), 512u);
    EXPECT_NEAR(s->peakOut.load(), 0.5f, 1e-6f);
    EXPECT_EQ(s->clippedSamples.load(), 0u);

    // Acima do joelho: soft-clip, nunca passa de 1.0.
    std::vector<float> loud(2 * 256, 0.9f);
    process(loud, 2);
    for (float v : loud) {
        EXPECT_GT(v, 0.8f);
        EXPECT_LT(v, 1.0f);
    }
    EXPECT_GT(s->clippedSamples.load(), 0u);

    EXPECT_EQ(cfg_->UnlockForProcess(), S_OK);
    EXPECT_EQ(s->lockedInstances.load(), 0u);
}

TEST_F(ApoTest, SilenceAndBypass)
{
    initWithEndpoint();
    lock(2);
    auto* s = shared_.get();
    s->gain.store(2.0f);

    std::vector<float> silent(4, 0.0f);
    process(silent, 2, BUFFER_SILENT);
    EXPECT_EQ(lastFlags_, BUFFER_SILENT);
    EXPECT_EQ(s->heartbeat.load(), 1u);

    s->bypass.store(1);
    std::vector<float> buf = {0.3f, -0.3f, 0.7f, -0.7f};
    const auto original = buf;
    process(buf, 2);
    EXPECT_EQ(buf, original);
}

// O audiodg cria o APO por agregação: externo + IUnknown interno.
TEST_F(ApoTest, SupportsComAggregationLikeAudiodg)
{
    Ptr<IClassFactory> f;
    ASSERT_EQ(getClassObject_(CLSID_LoudMixerApo, IID_IClassFactory, f.put()), S_OK);
    FakeOuter outer;
    void* bad = nullptr;
    EXPECT_EQ(f->CreateInstance(&outer, IID_IAudioProcessingObject, &bad), CLASS_E_NOAGGREGATION);

    IUnknown* inner = nullptr;
    ASSERT_EQ(f->CreateInstance(&outer, IID_IUnknown, reinterpret_cast<void**>(&inner)), S_OK);
    outer.inner = inner;
    EXPECT_EQ(outer.refs, 1u);

    IAudioProcessingObjectRT* rt = nullptr;
    ASSERT_EQ(inner->QueryInterface(IID_IAudioProcessingObjectRT, reinterpret_cast<void**>(&rt)), S_OK);
    EXPECT_EQ(outer.refs, 2u); // AddRef delegado ao externo

    IAudioProcessingObject* apo = nullptr;
    ASSERT_EQ(rt->QueryInterface(IID_IAudioProcessingObject, reinterpret_cast<void**>(&apo)), S_OK);
    EXPECT_EQ(outer.refs, 3u);
    IUnknown* identity = nullptr;
    ASSERT_EQ(apo->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity)), S_OK);
    EXPECT_EQ(identity, static_cast<IUnknown*>(&outer)); // identidade COM é a do externo

    ASSERT_EQ(apo->Initialize(0, nullptr), S_OK);
    identity->Release();
    apo->Release();
    rt->Release();
    EXPECT_EQ(outer.refs, 1u);
    EXPECT_EQ(inner->Release(), 0u); // o interno destrói o objeto
}
