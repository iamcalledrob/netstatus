#include <windows.h>
#include <iostream>
#include "monitor_windows.hpp"

class ConnectionStatusMonitor : public INetworkListManagerEvents {
public:
	ConnectionStatusMonitor(onConnectionStatusChange_t callback) {
		_m_ref = 1;
        _callback = callback;
	}

    // Could certainly improve error handling here
    HRESULT start() {
        HRESULT result;
        _thread_id = GetCurrentThreadId(); // Used to post stop message to only this thread

        CoInitialize(NULL);

        INetworkListManager *pNetworkListManager = NULL;
        result = CoCreateInstance(
            CLSID_NetworkListManager,
            NULL,
            CLSCTX_ALL,
            IID_INetworkListManager,
            (LPVOID *)&pNetworkListManager
        );
        if (!SUCCEEDED(result)) {
            goto FAIL_RELEASE_NONE;
        }

        // Request the current internet connection status now, because only changes are sent into the sink.
        VARIANT_BOOL isInitiallyConnected;
        result = pNetworkListManager->IsConnectedToInternet(&isInitiallyConnected);
        if (!SUCCEEDED(result)) {
            goto FAIL_RELEASE_NONE;
        }

        IConnectionPointContainer *pConnectionPointContainer;
        result = pNetworkListManager->QueryInterface(IID_IConnectionPointContainer, (void**)&pConnectionPointContainer);
        if (!SUCCEEDED(result)) {
            goto FAIL_RELEASE_NETWORK_LIST_MANAGER;
        }

        IConnectionPoint *pConnectionPoint;
        result = pConnectionPointContainer->FindConnectionPoint(IID_INetworkListManagerEvents, &pConnectionPoint);
        if (!SUCCEEDED(result)) {
            goto FAIL_RELEASE_CONNECTION_POINT_CONTAINER;
        }

        DWORD dwCookie;
        result = pConnectionPoint->Advise((IUnknown*)this, &dwCookie);
        if (!SUCCEEDED(result)) {
            goto FAIL_RELEASE_CONNECTION_POINT;
        }

        // Send initial status to callback.
        // Since message pump has not been started, this will be the first invokation of the callback, and also acts
        // as a signal that it is now safe to call stop().
        this->_callback(this, isInitiallyConnected == VARIANT_TRUE);

        // Start a message pump for this thread, processing messages and blocking until WM_EXIT_PUMP.
        // The processing of messages is required for `ConnectivityChanged` to get invoked.
        //
        // Since this is a golang-managed thread, it's assumed that no other message pump will be running.
        // Note: runtime.LockOSThread() + UnlockOSThread() will be required to ensure this.
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_EXIT_PUMP) {
                break;
            }
        }

SUCCESS:
        pConnectionPoint->Unadvise(dwCookie);
FAIL_RELEASE_CONNECTION_POINT:
        pConnectionPoint->Release();
FAIL_RELEASE_CONNECTION_POINT_CONTAINER:
        pConnectionPointContainer->Release();
FAIL_RELEASE_NETWORK_LIST_MANAGER:
        pNetworkListManager->Release();
FAIL_RELEASE_NONE:
        CoUninitialize();

        return result;
    }

    void stop() {
        PostThreadMessageA(_thread_id, WM_EXIT_PUMP, NULL, NULL);
    }

#pragma MARK INetworkListManagerEvents

	virtual HRESULT STDMETHODCALLTYPE ConnectivityChanged(NLM_CONNECTIVITY newConnectivity) {
	    bool isConnected = (newConnectivity & NLM_CONNECTIVITY_IPV4_INTERNET) != 0 || (newConnectivity & NLM_CONNECTIVITY_IPV6_INTERNET) != 0;
	    this->_callback(this, isConnected);
		return S_OK;
	}


	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
		if (IsEqualIID(riid, IID_IUnknown)) {
			*ppvObject = (IUnknown *)this;
			return S_OK;
		} else if (IsEqualIID(riid, IID_INetworkListManagerEvents)) {
			*ppvObject = (INetworkListManagerEvents *)this;
			return S_OK;
		} else {
			return E_NOINTERFACE;
		}
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void) {
		return (ULONG)InterlockedIncrement(&_m_ref);
	}

	virtual ULONG STDMETHODCALLTYPE Release(void) {
		LONG result = InterlockedDecrement(&_m_ref);
		return (ULONG)result;
	}

private:
	LONG _m_ref;
	DWORD _thread_id;
	onConnectionStatusChange_t _callback;
};

DWORD threadId = NULL;

CSMHandle ConnectionStatusMonitorCreate(onConnectionStatusChange_t callback) {
    return new ConnectionStatusMonitor(callback);
}

void ConnectionStatusMonitorFree(CSMHandle h) {
    delete h;
}

HRESULT ConnectionStatusMonitorStart(CSMHandle h) {
    return h->start();
}

void ConnectionStatusMonitorStop(CSMHandle h) {
    h->stop();
}




//void SubscribeToEvents()
//{
//	NetworkListManagerEventsSink* pSink = new NetworkListManagerEventsSink(&onChange);
//	pSink->start();
//}
//
//void UnsubscribeFromEvents()
//{
////    MSG msg;
////    msg.message = WM_RETICULATE_SPLINES;
////    PostThreadMessageA(threadId, msg.message, NULL, NULL);
//}


//
//class CNetworkListManagerEvent final : public INetworkListManagerEvents
//{
//public:
//	CNetworkListManagerEvent() : m_ref(1) { }
//	~CNetworkListManagerEvent() { }
//
//	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
//	{
//		if (IsEqualIID(riid, IID_IUnknown)) {
//			*ppvObject = (IUnknown *)this;
//		} else if (IsEqualIID(riid ,IID_INetworkListManagerEvents)) {
//			*ppvObject = (INetworkListManagerEvents *)this;
//		} else {
//			return E_NOINTERFACE;
//		}
//		AddRef();
//		return S_OK;
//	}
//
//	ULONG STDMETHODCALLTYPE AddRef()
//	{
//        std::cout << "AddRef\n";
//		return (ULONG)InterlockedIncrement(&m_ref);
//	}
//
//	ULONG STDMETHODCALLTYPE Release()
//	{
//		LONG Result = InterlockedDecrement(&m_ref);
//		if (Result == 0)
//			delete this;
//		return (ULONG)Result;
//	}
//
//	HRESULT STDMETHODCALLTYPE ConnectivityChanged(NLM_CONNECTIVITY newConnectivity)
//	{
//	    std::cout << "ConnectivityChanged\n";
//
//		return S_OK;
//	}
//
//private:
//
//	LONG m_ref;
//};
//
//IUnknown *pUnknown = nullptr;
//INetworkListManager *pNetworkListManager = nullptr;
//IConnectionPointContainer *pCPContainer = nullptr;
//IConnectionPoint *pConnectPoint = nullptr;
//CNetworkListManagerEvent *pNetEvent = nullptr;
//DWORD Cookie = 0;
//
//void SubscribeToEvents()
//{
//	std::cout << "SubscribeToEvents()\n";
//	CoInitialize(0);
//
//	HRESULT Result = CoCreateInstance(CLSID_NetworkListManager, NULL, CLSCTX_ALL, IID_IUnknown, (void **)&pUnknown);
//	if (SUCCEEDED(Result))
//	{
//		Result = pUnknown->QueryInterface(IID_INetworkListManager, (void **)&pNetworkListManager);
//		if (SUCCEEDED(Result))
//		{
//			VARIANT_BOOL IsConnect = VARIANT_FALSE;
//#if defined(_MSC_VER)
//			Result = pNetworkListManager->get_IsConnectedToInternet(&IsConnect);
//#else
//			Result = pNetworkListManager->IsConnectedToInternet(&IsConnect);
//#endif
//			if (SUCCEEDED(Result)) {
//			    if (IsConnect == VARIANT_TRUE) {
//				    std::cout << "SUCCEEDED: CurrentState: CONNECTED\n";
//                } else {
//                    std::cout << "SUCCEEDED: CurrentState: NOT CONNECTED\n";
//                }
//			}
//
//			Result = pNetworkListManager->QueryInterface(IID_IConnectionPointContainer, (void **)&pCPContainer);
//			if (SUCCEEDED(Result))
//			{
//				Result = pCPContainer->FindConnectionPoint(IID_INetworkListManagerEvents, &pConnectPoint);
//				if(SUCCEEDED(Result))
//				{
//					pNetEvent = new CNetworkListManagerEvent;
//					Result = pConnectPoint->Advise((IUnknown *)pNetEvent, &Cookie);
//					if (SUCCEEDED(Result))
//                        std::cout << "SUCCEEDED: Subscribed: TODO\n";
//					else
//                        std::cout << "FAILED: Subscribed: TODO\n";
//				} else
//					std::cout << "FAILED: Couldn't find connection point: TODO\n";
//			} else
//				std::cout << "FAILED: NetState: Unable to query NetworkListManager interface\n";
//		} else
//            std::cout << "FAILED: NetState: Unable to query global interface\n";
//	} else
//        std::cout << "FAILED: NetState: Unable to create INetworkListManager interface\n";
//}
//
//void UnSubscribeFromEvents()
//{
//    std::cout << "UnsubscribeFromEvents()\n";
//	try
//	{
//		if (pConnectPoint) {
//			pConnectPoint->Unadvise(Cookie);
//			pConnectPoint->Release();
//		}
//
//		if (pNetEvent)
//		{
//			pNetEvent->Release();
//		}
//
//		if (pCPContainer)
//		{
//			pCPContainer->Release();
//		}
//
//		if (pNetworkListManager)
//		{
//			pNetworkListManager->Release();
//		}
//
//		if (pUnknown)
//		{
//			pUnknown->Release();
//		}
//
//		CoUninitialize();
//	}
//	catch (std::exception& ex)
//	{
//	    std::cout << "UnsubscribeFromEvents: Exception: TODO\n";
//	}
//}