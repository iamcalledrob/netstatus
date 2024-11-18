#include <stdbool.h>
#include <netlistmgr.h>
#include <ocidl.h>

#define WM_EXIT_PUMP (WM_USER + 0x0001)

struct ConnectionStatusMonitor;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ConnectionStatusMonitor * CSMHandle;
typedef void (*onConnectionStatusChange_t)(CSMHandle monitor, bool isConnected);

CSMHandle   ConnectionStatusMonitorCreate(onConnectionStatusChange_t);
void        ConnectionStatusMonitorFree(CSMHandle);

HRESULT     ConnectionStatusMonitorStart(CSMHandle);
void        ConnectionStatusMonitorStop(CSMHandle);


#ifdef __cplusplus
}
#endif