#pragma once

// Utilitários COM mínimos (ComPtr, conversão UTF-16 <-> UTF-8, HRESULT).
// O plano previa WIL/WRL; para manter o backend compilável também com
// MinGW (CI e cross-compile a partir do Linux) usamos este header próprio.

#include <windows.h>
#include <objbase.h>

#include <string>
#include <utility>

namespace loudmixer::win {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ~ComPtr() { reset(); }

    ComPtr& operator=(ComPtr o) noexcept
    {
        std::swap(p_, o.p_);
        return *this;
    }

    // Assume a posse de um ponteiro já com AddRef (como vindo de CoCreateInstance).
    static ComPtr attach(T* raw)
    {
        ComPtr c;
        c.p_ = raw;
        return c;
    }

    void reset()
    {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }

    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    // Para parâmetros de saída: libera o atual e devolve o endereço interno.
    T** put()
    {
        reset();
        return &p_;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }

    template <typename U>
    ComPtr<U> as() const
    {
        ComPtr<U> out;
        if (p_)
            p_->QueryInterface(__uuidof(U), out.putVoid());
        return out;
    }

private:
    T* p_ = nullptr;
};

inline std::string toUtf8(const wchar_t* w)
{
    if (!w || !*w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1)
        return {};
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// Libera memória alocada pelo COM (CoTaskMemAlloc) ao sair do escopo.
struct CoTaskString {
    LPWSTR p = nullptr;
    ~CoTaskString() { if (p) CoTaskMemFree(p); }
    LPWSTR* put() { return &p; }
};

// Inicializa COM (MTA) na thread atual pelo tempo de vida do objeto.
class ComApartment {
public:
    ComApartment() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT hr_;
};

} // namespace loudmixer::win
