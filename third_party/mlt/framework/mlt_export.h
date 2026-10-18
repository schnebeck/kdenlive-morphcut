/* Minimal replacement for the CMake-generated mlt_export.h (MLT 7.x),
 * used only when the system lacks the MLT development package. */
#ifndef MLT_EXPORT_H
#define MLT_EXPORT_H
#define MLT_EXPORT __attribute__((visibility("default")))
#define MLT_NO_EXPORT __attribute__((visibility("hidden")))
#define MLT_DEPRECATED __attribute__((__deprecated__))
#define MLT_DEPRECATED_EXPORT MLT_EXPORT MLT_DEPRECATED
#define MLT_DEPRECATED_NO_EXPORT MLT_NO_EXPORT MLT_DEPRECATED
#endif
