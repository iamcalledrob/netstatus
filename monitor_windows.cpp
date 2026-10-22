#include <windows.h>

#include "monitor_windows.hpp"

class ConnectionStatusMonitor : public INetworkListManagerEvents {
public:
    ConnectionStatusMonitor(onConnectionStatusChange_t callback)
        : _m_ref(1), _callback(callback) {
        _stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
        _creation_result = _stop_event != NULL
            ? S_OK
            : HRESULT_FROM_WIN32(GetLastError());
    }

    virtual ~ConnectionStatusMonitor() {
        if (_stop_event != NULL) {
            CloseHandle(_stop_event);
        }
    }

    HRESULT start() {
        HRESULT result = S_OK;
        INetworkListManager *pNetworkListManager = NULL;
        IConnectionPointContainer *pConnectionPointContainer = NULL;
        IConnectionPoint *pConnectionPoint = NULL;
        DWORD dwCookie = 0;
        bool advised = false;
        bool running = false;

        if (FAILED(_creation_result)) {
            return _creation_result;
        }

        result = CoInitialize(NULL);
        if (FAILED(result)) {
            return result;
        }

        result = CoCreateInstance(
            CLSID_NetworkListManager,
            NULL,
            CLSCTX_ALL,
            IID_INetworkListManager,
            (LPVOID *)&pNetworkListManager
        );
        if (FAILED(result)) {
            goto CLEANUP;
        }

        // Request the current internet connection status now, because only changes are sent into the sink.
        VARIANT_BOOL isInitiallyConnected;
        result = pNetworkListManager->IsConnectedToInternet(&isInitiallyConnected);
        if (FAILED(result)) {
            goto CLEANUP;
        }

        result = pNetworkListManager->QueryInterface(
            IID_IConnectionPointContainer,
            (void **)&pConnectionPointContainer
        );
        if (FAILED(result)) {
            goto CLEANUP;
        }

        result = pConnectionPointContainer->FindConnectionPoint(
            IID_INetworkListManagerEvents,
            &pConnectionPoint
        );
        if (FAILED(result)) {
            goto CLEANUP;
        }

        result = pConnectionPoint->Advise((IUnknown *)this, &dwCookie);
        if (FAILED(result)) {
            goto CLEANUP;
        }
        advised = true;

        if (WaitForSingleObject(_stop_event, 0) != WAIT_OBJECT_0) {
            // Since the message pump has not started, this is the first callback.
            this->_callback(this, isInitiallyConnected == VARIANT_TRUE);
        }

        // Wait for either cancellation or COM messages. Using a persistent
        // event makes a stop request reliable even before this thread starts.
        running = true;
        while (running) {
            DWORD waitResult = MsgWaitForMultipleObjects(
                1,
                &_stop_event,
                FALSE,
                INFINITE,
                QS_ALLINPUT
            );
            if (waitResult == WAIT_OBJECT_0) {
                break;
            }
            if (waitResult != WAIT_OBJECT_0 + 1) {
                result = HRESULT_FROM_WIN32(GetLastError());
                break;
            }

            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

CLEANUP:
        if (advised) {
            pConnectionPoint->Unadvise(dwCookie);
        }
        if (pConnectionPoint != NULL) {
            pConnectionPoint->Release();
        }
        if (pConnectionPointContainer != NULL) {
            pConnectionPointContainer->Release();
        }
        if (pNetworkListManager != NULL) {
            pNetworkListManager->Release();
        }
        CoUninitialize();

        return result;
    }

    void stop() {
        if (_stop_event != NULL) {
            SetEvent(_stop_event);
        }
    }

    virtual HRESULT STDMETHODCALLTYPE ConnectivityChanged(NLM_CONNECTIVITY newConnectivity) {
        bool isConnected =
            (newConnectivity & NLM_CONNECTIVITY_IPV4_INTERNET) != 0 ||
            (newConnectivity & NLM_CONNECTIVITY_IPV6_INTERNET) != 0;
        this->_callback(this, isConnected);
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
        if (ppvObject == NULL) {
            return E_POINTER;
        }
        *ppvObject = NULL;

        if (IsEqualIID(riid, IID_IUnknown)) {
            *ppvObject = (IUnknown *)this;
        } else if (IsEqualIID(riid, IID_INetworkListManagerEvents)) {
            *ppvObject = (INetworkListManagerEvents *)this;
        } else {
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    virtual ULONG STDMETHODCALLTYPE AddRef(void) {
        return (ULONG)InterlockedIncrement(&_m_ref);
    }

    virtual ULONG STDMETHODCALLTYPE Release(void) {
        LONG result = InterlockedDecrement(&_m_ref);
        if (result == 0) {
            delete this;
        }
        return (ULONG)result;
    }

private:
    LONG _m_ref;
    HANDLE _stop_event;
    HRESULT _creation_result;
    onConnectionStatusChange_t _callback;
};

CSMHandle ConnectionStatusMonitorCreate(onConnectionStatusChange_t callback) {
    return new ConnectionStatusMonitor(callback);
}

void ConnectionStatusMonitorFree(CSMHandle h) {
    h->Release();
}

HRESULT ConnectionStatusMonitorStart(CSMHandle h) {
    return h->start();
}

void ConnectionStatusMonitorStop(CSMHandle h) {
    h->stop();
}
