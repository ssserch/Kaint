#include "core/Parallel.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace parallel {

namespace {
thread_local bool g_background = false;
}

void enterBackground()
{
    if (g_background)
        return;
    g_background = true;
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__linux__)
    // En Linux cada hilo tiene su propio «nice»: unos puntos más que el proceso (subirlo no
    // necesita permisos; los hilos de cálculo terminan al acabar, así que no hace falta bajarlo).
    const pid_t tid = pid_t(syscall(SYS_gettid));
    errno = 0;
    const int nice = getpriority(PRIO_PROCESS, id_t(tid));
    if (errno == 0)
        setpriority(PRIO_PROCESS, id_t(tid), std::min(19, nice + 5));
#endif
}

bool inBackground()
{
    return g_background;
}

} // namespace parallel
