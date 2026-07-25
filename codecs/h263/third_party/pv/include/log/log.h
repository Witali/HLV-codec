/*
 * Minimal portability shim for the Android logging calls used by the
 * PacketVideo decoder. The decoder only emits diagnostics on rejected input;
 * the host application owns user-visible error reporting.
 */
#ifndef H263_PV_PORTABLE_LOG_H
#define H263_PV_PORTABLE_LOG_H

#ifndef ALOGE
#define ALOGE(...) ((void)0)
#endif

static inline int android_errorWriteLog(int tag, const char *message) {
    (void)tag;
    (void)message;
    return 0;
}

#endif
