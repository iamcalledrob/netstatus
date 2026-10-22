#include <stdbool.h>
#include <netlistmgr.h>
#include <ocidl.h>

#ifdef __cplusplus
class ConnectionStatusMonitor;
typedef ConnectionStatusMonitor * CSMHandle;
extern "C" {
#else
typedef struct ConnectionStatusMonitor * CSMHandle;
#endif

typedef void (*onConnectionStatusChange_t)(CSMHandle monitor, bool isConnected);

CSMHandle   ConnectionStatusMonitorCreate(onConnectionStatusChange_t);
void        ConnectionStatusMonitorFree(CSMHandle);

HRESULT     ConnectionStatusMonitorStart(CSMHandle);
void        ConnectionStatusMonitorStop(CSMHandle);


#ifdef __cplusplus
}
#endif
