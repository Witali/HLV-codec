/*
 * Minimal portability shim for the Android logging call used by the
 * PacketVideo AMR-NB decoder. The application owns user-visible diagnostics.
 */
#ifndef AMRNB_PV_PORTABLE_LOG_H
#define AMRNB_PV_PORTABLE_LOG_H

#ifndef ALOGE
#define ALOGE(...) ((void)0)
#endif

static inline int android_errorWriteLog(int tag, const char *message) {
    (void)tag;
    (void)message;
    return 0;
}

#endif
