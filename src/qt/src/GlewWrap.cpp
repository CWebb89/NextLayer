// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include <GL/glew.h>

extern "C" GLenum __real_glewInit(void);

extern "C" GLenum __wrap_glewInit(void)
{
    const GLenum r = __real_glewInit();
    if (r == GLEW_ERROR_NO_GLX_DISPLAY && GLEW_VERSION_3_3)
        return GLEW_OK;
    return r;
}
