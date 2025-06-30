/*******************************************************************************

Filename    :   FramebufferValidation.h
Content     :   Framebuffer validation utilities
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include <GLES3/gl3.h>
#include "../utils/LogUtils.h"

// Detailed GL error checking with specific context
static const char* GetFramebufferStatusString(GLenum status) {
    switch (status) {
        case GL_FRAMEBUFFER_COMPLETE:
            return "GL_FRAMEBUFFER_COMPLETE";
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
        case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
            return "GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS";
        case GL_FRAMEBUFFER_UNSUPPORTED:
            return "GL_FRAMEBUFFER_UNSUPPORTED";
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
        default:
            return "Unknown framebuffer status";
    }
}

static bool ValidateFramebuffer(const char* context, GLuint fb, const char* file, int line) {
    bool isValid = true;

    GLint boundFb;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFb);

    // Ensure we check the correct framebuffer
    if (static_cast<GLuint>(boundFb) != fb) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        // Detailed error message with specific diagnostic context
        ALOGE("Framebuffer incomplete: %s (0x%x) at %s [file: %s, line: %d]",
              GetFramebufferStatusString(status), status, context, file, line);

        // Additional diagnostics: report what's attached
        GLint colorAttachment = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachment);

        GLint depthAttachment = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthAttachment);

        ALOGE("  Color attachment type: 0x%x", colorAttachment);
        ALOGE("  Depth attachment type: 0x%x", depthAttachment);

        // If color is a renderbuffer, check its samples and format
        if (colorAttachment == GL_RENDERBUFFER) {
            GLint samples = 0;
            GLint format = 0;
            GLuint renderbuffer = 0;

            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                                  reinterpret_cast<GLint*>(&renderbuffer));

            GLint originalRenderbuffer;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &originalRenderbuffer);

            glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);

            ALOGE("  Color renderbuffer: samples=%d, format=0x%x", samples, format);

            glBindRenderbuffer(GL_RENDERBUFFER, originalRenderbuffer);
        }

        // If depth is a renderbuffer, check its samples and format
        if (depthAttachment == GL_RENDERBUFFER) {
            GLint samples = 0;
            GLint format = 0;
            GLuint renderbuffer = 0;

            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                                  reinterpret_cast<GLint*>(&renderbuffer));

            GLint originalRenderbuffer;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &originalRenderbuffer);

            glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);

            ALOGE("  Depth renderbuffer: samples=%d, format=0x%x", samples, format);

            glBindRenderbuffer(GL_RENDERBUFFER, originalRenderbuffer);
        }

        isValid = false;
    }

    if (static_cast<GLuint>(boundFb) != fb) {
        glBindFramebuffer(GL_FRAMEBUFFER, boundFb);
    }

    return isValid;
}

#define VALIDATE_FRAMEBUFFER(context, fb) ValidateFramebuffer(context, fb, __FILE__, __LINE__)

// Validating framebuffer before and after operations
#define VALIDATE_FRAMEBUFFER_BINDING(context) do { \
    GLint currentFb = 0; \
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFb); \
    if (currentFb == 0) { \
        ALOGV("Framebuffer: %s - Using default framebuffer", context); \
    } else { \
        VALIDATE_FRAMEBUFFER(context, currentFb); \
    } \
} while(0)