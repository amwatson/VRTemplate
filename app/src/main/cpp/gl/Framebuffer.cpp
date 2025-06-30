/*******************************************************************************

Filename    :   Framebuffer.cpp
Content     :   OpenGL framebuffer management with multiview support
Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#include "Framebuffer.h"
#include "FramebufferValidation.h"
#include <vector>

//  pointer types for the OpenGL extension s we need
typedef void (GL_APIENTRY* PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(GLenum target,
                                                                    GLenum attachment, GLuint texture, GLint level, GLint baseViewIndex, GLsizei numViews);
typedef void (GL_APIENTRY* PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)(GLenum target,
                                                                       GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (GL_APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)(GLenum target,
                                                                        GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLsizei samples);

void CheckGLError([[maybe_unused]] const char *, [[maybe_unused]] const char *file, [[maybe_unused]] const int line) {
#if defined(DEBUG)
    for (GLint error = glGetError(); error; error = glGetError()) {
            const char* errorStr = "unknown error";
            switch (error) {
                case GL_INVALID_ENUM:       errorStr = "GL_INVALID_ENUM"; break;
                case GL_INVALID_VALUE:      errorStr = "GL_INVALID_VALUE"; break;
                case GL_INVALID_OPERATION:  errorStr = "GL_INVALID_OPERATION"; break;
                case GL_OUT_OF_MEMORY:      errorStr = "GL_OUT_OF_MEMORY"; break;
                case GL_INVALID_FRAMEBUFFER_OPERATION: errorStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            }
            ALOGE("GL error at %s:%d after %s: %s (0x%x)", file, line, , errorStr, error);
        }
#endif
}

struct GLExtensionState {
    bool hasMultiview = false;
    bool hasMultisampleRenderbuffer = false;
    bool hasMultisampleTexture = false;

    bool CheckExtensions() {
        const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
        if (!extensions) {
            ALOGE("Failed to get GL extensions string");
            return false;
        }

        // Log extensions for debugging
        ALOGD("GL Extensions: %s", extensions);

        hasMultiview = (strstr(extensions, "GL_OVR_multiview2") != nullptr);
        hasMultisampleRenderbuffer = (strstr(extensions, "GL_EXT_multisampled_render_to_texture") != nullptr);
        hasMultisampleTexture = (strstr(extensions, "GL_EXT_multisampled_render_to_texture") != nullptr);

        ALOGD("Extension support: multiview=%d, multisampled_renderbuffer=%d, multisampled_texture=%d",
              hasMultiview, hasMultisampleRenderbuffer, hasMultisampleTexture);

        return true;
    }
};

namespace {
    // Global  pointers
    PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR = nullptr;
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT = nullptr;

    // Extension state
    GLExtensionState gExtensionState;

    void ValidateRenderbufferState(GLuint rb, const char* label) {
        GLint currentRb = 0;
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &currentRb);

        glBindRenderbuffer(GL_RENDERBUFFER, rb);

        GLint width = 0, height = 0, samples = 0, format = 0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);

        ALOGD("%s: size=%dx%d, samples=%d, format=0x%x", label, width, height, samples, format);

        // Restore state
        glBindRenderbuffer(GL_RENDERBUFFER, currentRb);
    }
} // anonymous namespace

Framebuffer::Framebuffer()
        : mWidth(0)
        , mHeight(0)
        , mMultisamples(0)
        , mUseMultiview(false)
        , mTextureSwapChainLength(0)
        , mTextureSwapChainIndex(0)
{
}

Framebuffer::~Framebuffer() {
    Destroy();
}

bool Framebuffer::Create(XrSession session, GLenum colorFormat, int width, int height, int multisamples, bool useMultiview) {
    ALOGD("Creating framebuffer: %dx%d, multisamples=%d, multiview=%d, format=0x%x",
          width, height, multisamples, useMultiview, colorFormat);

    mWidth = width;
    mHeight = height;
    mMultisamples = multisamples;
    mUseMultiview = useMultiview;

    // Load extension s if needed
    if (glFramebufferTextureMultiviewOVR == nullptr) {
        glFramebufferTextureMultiviewOVR =
                (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)eglGetProcAddress("glFramebufferTextureMultiviewOVR");
    }
    if (glRenderbufferStorageMultisampleEXT == nullptr) {
        glRenderbufferStorageMultisampleEXT =
                (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)eglGetProcAddress("glRenderbufferStorageMultisampleEXT");
    }
    if (glFramebufferTexture2DMultisampleEXT == nullptr) {
        glFramebufferTexture2DMultisampleEXT =
                (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
    }

    // Check and log extension capability
    gExtensionState.CheckExtensions();

    // Validate  pointers were loaded correctly
    if (mUseMultiview && glFramebufferTextureMultiviewOVR == nullptr) {
        ALOGW("glFramebufferTextureMultiviewOVR not found");
        mUseMultiview = false;
    }

    if (mMultisamples > 1) {
        if (glRenderbufferStorageMultisampleEXT == nullptr) {
            ALOGW("glRenderbufferStorageMultisampleEXT not found, multisampling may not work");
        }
    }

    if (mUseMultiview) {
        const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
        if (!strstr(extensions, "GL_OVR_multiview2") || glFramebufferTextureMultiviewOVR == nullptr) {
            ALOGW("GL_OVR_multiview2 unsupported, falling back to non-multiview");
            mUseMultiview = false;
        }
    }

    // Verify color format
    uint32_t formatCount = 0;
    OXR(xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr));
    std::vector<int64_t> formats(formatCount);
    OXR(xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()));

    bool formatSupported = false;
    for (int64_t f : formats) {
        if (f == colorFormat) {
            formatSupported = true;
            break;
        }
    }

    if (!formatSupported) {
        ALOGE("Unsupported color format: 0x%x", colorFormat);
        return false;
    }

    XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    sci.format = colorFormat;
    sci.sampleCount = 1;  // We handle MSAA manually
    sci.width = width;
    sci.height = height;
    sci.faceCount = 1;
    sci.arraySize = mUseMultiview ? 2 : 1;
    sci.mipCount = 1;

    mColorSwapChain.mWidth = width;
    mColorSwapChain.mHeight = height;
    OXR(xrCreateSwapchain(session, &sci, &mColorSwapChain.mHandle));

    OXR(xrEnumerateSwapchainImages(mColorSwapChain.mHandle, 0, &mTextureSwapChainLength, nullptr));
    mColorSwapChainImages.resize(mTextureSwapChainLength, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    OXR(xrEnumerateSwapchainImages(
            mColorSwapChain.mHandle, mTextureSwapChainLength,
            &mTextureSwapChainLength,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(mColorSwapChainImages.data())));

    mDepthBuffers.resize(mTextureSwapChainLength, 0);
    mFrameBuffers.resize(mTextureSwapChainLength, 0);
    mMsaaColorBuffers.resize(mTextureSwapChainLength, 0);

    ALOGD("Creating %d framebuffers with swapchain textures", mTextureSwapChainLength);

    for (uint32_t i = 0; i < mTextureSwapChainLength; ++i) {
        const GLuint colorTex = mColorSwapChainImages[i].image;

        // Note: We know the dimensions from the XrSwapChainCreateInfo
        if (mUseMultiview) {
            GL(glBindTexture(GL_TEXTURE_2D_ARRAY, colorTex));
        } else {
            GL(glBindTexture(GL_TEXTURE_2D, colorTex));
        }

        ALOGD("Swapchain[%d]: texture=%u, expected size=%dx%d", i, colorTex, width, height);

        // Texture parameters
        if (mUseMultiview) {
            GL(glBindTexture(GL_TEXTURE_2D_ARRAY, colorTex));
            GL(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GL(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GL(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));
        } else {
            GL(glBindTexture(GL_TEXTURE_2D, colorTex));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GL(glBindTexture(GL_TEXTURE_2D, 0));
        }

        // Create depth buffer
        GL(glGenRenderbuffers(1, &mDepthBuffers[i]));
        GL(glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffers[i]));

        if (mMultisamples > 1 && glRenderbufferStorageMultisampleEXT != nullptr) {
            ALOGD("Creating multisampled depth buffer: samples=%d", mMultisamples);
            GL(glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, mMultisamples, GL_DEPTH_COMPONENT24, width, height));
        } else {
            if (mMultisamples > 1 && glRenderbufferStorageMultisampleEXT == nullptr) {
                ALOGW("glRenderbufferStorageMultisampleEXT missing, falling back to non-multisampled depth");
            }
            GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height));
        }

        ValidateRenderbufferState(mDepthBuffers[i], "Depth buffer");

        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

        // Create framebuffer
        GL(glGenFramebuffers(1, &mFrameBuffers[i]));
        GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFrameBuffers[i]));

        if (mUseMultiview) {
            GL(glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthBuffers[i]));
            GL(glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorTex, 0, 0, 2));
        } else {
            GL(glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthBuffers[i]));

            if (mMultisamples > 1) {
                // Key insight #1: We need a multisampled color buffer for MSAA
                GLuint msaaTex = 0;
                GL(glGenRenderbuffers(1, &msaaTex));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, msaaTex));

                // Key insight #2: Use EXT version if available, otherwise regular version
                if (glRenderbufferStorageMultisampleEXT != nullptr) {
                    ALOGD("Using glRenderbufferStorageMultisampleEXT for color buffer");
                    GL(glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, mMultisamples, colorFormat, width, height));
                } else {
                    ALOGD("Using glRenderbufferStorageMultisample for color buffer");
                    GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, mMultisamples, colorFormat, width, height));
                }

                ValidateRenderbufferState(msaaTex, "MSAA color buffer");

                GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
                GL(glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaTex));
                mMsaaColorBuffers[i] = msaaTex;
            } else {
                GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0));
                mMsaaColorBuffers[i] = 0;
            }
        }

        const GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            ALOGE("Incomplete framebuffer: %s (0x%x)", GetFramebufferStatusString(status), status);

            // Additional validation for better diagnostics
            if (status == GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE) {
                GLint msaaDepthSamples = 0, msaaColorSamples = 0;

                GL(glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffers[i]));
                GL(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &msaaDepthSamples));

                if (mMsaaColorBuffers[i] != 0) {
                    GL(glBindRenderbuffer(GL_RENDERBUFFER, mMsaaColorBuffers[i]));
                    GL(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &msaaColorSamples));
                }

                ALOGE("MSAA sample counts: depth=%d, color=%d", msaaDepthSamples, msaaColorSamples);

                // Key insight #3: The most common cause of GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
                // is mismatched sample counts between attachments
                if (msaaDepthSamples != msaaColorSamples) {
                    ALOGE("Mismatched MSAA sample counts! Depth=%d, Color=%d",
                          msaaDepthSamples, msaaColorSamples);

                    // Fix the sample count mismatch by adjusting the depth buffer
                    GL(glBindRenderbuffer(GL_RENDERBUFFER, mDepthBuffers[i]));
                    if (glRenderbufferStorageMultisampleEXT != nullptr) {
                        GL(glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, msaaColorSamples,
                                                               GL_DEPTH_COMPONENT24, width, height));
                    } else {
                        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaaColorSamples,
                                                            GL_DEPTH_COMPONENT24, width, height));
                    }

                    // Reattach and check again
                    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFrameBuffers[i]));
                    GL(glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                 GL_RENDERBUFFER, mDepthBuffers[i]));

                    const GLenum newStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
                    if (newStatus != GL_FRAMEBUFFER_COMPLETE) {
                        ALOGE("Still incomplete after sample count fix: %s (0x%x)",
                              GetFramebufferStatusString(newStatus), newStatus);
                    } else {
                        ALOGD("Fixed framebuffer by matching sample counts");
                    }
                }
            }

            if (status != GL_FRAMEBUFFER_COMPLETE) {
                GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
                return false;
            }
        }

        VALIDATE_FRAMEBUFFER("After creation", mFrameBuffers[i]);

        GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
    }

    return true;
}

void Framebuffer::Destroy() {
    if (!mFrameBuffers.empty()) {
        GL(glDeleteFramebuffers(mFrameBuffers.size(), mFrameBuffers.data()));
        mFrameBuffers.clear();
    }

    if (!mDepthBuffers.empty()) {
        GL(glDeleteRenderbuffers(mDepthBuffers.size(), mDepthBuffers.data()));
        mDepthBuffers.clear();
    }

    if (!mMsaaColorBuffers.empty()) {
        GL(glDeleteRenderbuffers(mMsaaColorBuffers.size(), mMsaaColorBuffers.data()));
        mMsaaColorBuffers.clear();
    }

    if (mColorSwapChain.mHandle != XR_NULL_HANDLE) {
        OXR(xrDestroySwapchain(mColorSwapChain.mHandle));
        mColorSwapChain.mHandle = XR_NULL_HANDLE;
    }

    mColorSwapChainImages.clear();

    mWidth = 0;
    mHeight = 0;
    mMultisamples = 0;
    mUseMultiview = false;
    mTextureSwapChainLength = 0;
    mTextureSwapChainIndex = 0;
    mColorSwapChain.mWidth = 0;
    mColorSwapChain.mHeight = 0;
}

void Framebuffer::Acquire() {
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    OXR(xrAcquireSwapchainImage(mColorSwapChain.mHandle, &acquireInfo,
                                &mTextureSwapChainIndex));

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = 1000000000; // 1 second in nanoseconds

    XrResult result = xrWaitSwapchainImage(mColorSwapChain.mHandle, &waitInfo);

    // Retry a few times if we get a timeout
    int retries = 0;
    while (result == XR_TIMEOUT_EXPIRED && retries < 3) {
        result = xrWaitSwapchainImage(mColorSwapChain.mHandle, &waitInfo);
        retries++;
        ALOGD("Retry %d xrWaitSwapchainImage due to XR_TIMEOUT_EXPIRED", retries);
    }

    if (result != XR_SUCCESS) {
        ALOGE("Failed to wait for swapchain image after %d retries: %d", retries, result);
    }
}

void Framebuffer::SetCurrent() const {
    if (!mFrameBuffers.empty() && mTextureSwapChainIndex < mFrameBuffers.size()) {
        GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                             mFrameBuffers[mTextureSwapChainIndex]));

        VALIDATE_FRAMEBUFFER("SetCurrent", mFrameBuffers[mTextureSwapChainIndex]);
    }
}

void Framebuffer::SetNone() {
    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
}

void Framebuffer::Release() const {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    OXR(xrReleaseSwapchainImage(mColorSwapChain.mHandle, &releaseInfo));
}

void Framebuffer::Resolve() const {
    if (mMultisamples > 1 && !mUseMultiview) {
        const GLuint msaaFb = mFrameBuffers[mTextureSwapChainIndex];

        // Create and configure a temporary framebuffer for resolving
        GLuint tempFbo = 0;
        GL(glGenFramebuffers(1, &tempFbo));
        GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempFbo));

        // Explicitly attach the swapchain texture to the temp framebuffer
        const GLuint colorTex = mColorSwapChainImages[mTextureSwapChainIndex].image;
        GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0));

        // Explicitly verify the framebuffer is complete before resolving
        GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            ALOGE("Resolve target framebuffer incomplete: 0x%x", status);
            GL(glDeleteFramebuffers(1, &tempFbo));
            return;
        }

        // Setup source and destination for the blit
        GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFb));
        GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tempFbo));

        // Ensure the current read framebuffer is valid
        status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            ALOGE("MSAA source framebuffer incomplete before blit: 0x%x", status);
            GL(glDeleteFramebuffers(1, &tempFbo));
            return;
        }

        // Perform the blit with clear error state
        glGetError(); // Clear previous errors

        GL(glBlitFramebuffer(0, 0, mWidth, mHeight,
                             0, 0, mWidth, mHeight,
                             GL_COLOR_BUFFER_BIT, GL_NEAREST));

        // Discard the depth attachment after resolve
        const GLenum depthAttachment = GL_DEPTH_ATTACHMENT;
        glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 1, &depthAttachment);

        // Cleanup
        GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, 0));
        GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
        GL(glDeleteFramebuffers(1, &tempFbo));
    }
}

void Framebuffer::DumpState() const {
    ALOGD("Framebuffer state:");
    ALOGD("  Size: %dx%d", mWidth, mHeight);
    ALOGD("  Multisamples: %d", mMultisamples);
    ALOGD("  Multiview: %s", mUseMultiview ? "true" : "false");
    ALOGD("  SwapChain length: %u", mTextureSwapChainLength);
    ALOGD("  Current index: %u", mTextureSwapChainIndex);

    if (mTextureSwapChainIndex < mFrameBuffers.size()) {
        const GLuint fb = mFrameBuffers[mTextureSwapChainIndex];

        GLint currentFb;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFb);

        // Save state
        glBindFramebuffer(GL_FRAMEBUFFER, fb);

        // Get current attachments
        GLint colorType = 0, depthType = 0;
        GLuint colorName = 0, depthName = 0;

        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorType);

        if (colorType != GL_NONE) {
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                                  reinterpret_cast<GLint*>(&colorName));
        }

        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthType);

        if (depthType != GL_NONE) {
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                                  reinterpret_cast<GLint*>(&depthName));
        }

        ALOGD("  Current framebuffer: %u", fb);
        ALOGD("  Color attachment: type=0x%x, name=%u", colorType, colorName);
        ALOGD("  Depth attachment: type=0x%x, name=%u", depthType, depthName);

        // Check if this framebuffer uses MSAA
        if (mMultisamples > 1 && !mUseMultiview) {
            const GLuint msaaColorBuffer = mMsaaColorBuffers[mTextureSwapChainIndex];
            if (msaaColorBuffer != 0) {
                // Save renderbuffer binding
                GLint currentRb;
                glGetIntegerv(GL_RENDERBUFFER_BINDING, &currentRb);

                // Get MSAA color buffer info
                glBindRenderbuffer(GL_RENDERBUFFER, msaaColorBuffer);
                GLint msaaSamples = 0, msaaFormat = 0;
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &msaaSamples);
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &msaaFormat);

                ALOGD("  MSAA color buffer: id=%u, samples=%d, format=0x%x",
                      msaaColorBuffer, msaaSamples, msaaFormat);

                // Get depth buffer info
                const GLuint depthBuffer = mDepthBuffers[mTextureSwapChainIndex];
                glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
                GLint depthSamples = 0, depthFormat = 0;
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &depthSamples);
                glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &depthFormat);

                ALOGD("  Depth buffer: id=%u, samples=%d, format=0x%x",
                      depthBuffer, depthSamples, depthFormat);

                // Restore state
                glBindRenderbuffer(GL_RENDERBUFFER, currentRb);

                // Check for sample count mismatch
                if (msaaSamples != depthSamples) {
                    ALOGE("  ERROR: MSAA sample count mismatch! Color=%d, Depth=%d",
                          msaaSamples, depthSamples);
                }
            }
        }

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        ALOGD("  Status: %s (0x%x)", GetFramebufferStatusString(status), status);

        // Restore state
        glBindFramebuffer(GL_FRAMEBUFFER, currentFb);
    }
}