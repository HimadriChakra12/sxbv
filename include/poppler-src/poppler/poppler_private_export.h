/* poppler_private_export.h
 *
 * Hand-written replacement for CMake's generate_export_header() output.
 * sxbv links poppler core as a static library, so there is no shared-object
 * symbol-visibility boundary to manage -- this macro is a no-op.
 */

#ifndef POPPLER_PRIVATE_EXPORT_H
#define POPPLER_PRIVATE_EXPORT_H

#define POPPLER_PRIVATE_EXPORT

#endif /* POPPLER_PRIVATE_EXPORT_H */
