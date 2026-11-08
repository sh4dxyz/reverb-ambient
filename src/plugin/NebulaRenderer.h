#pragma once

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>

// Draws the audio-reactive nebula behind the editor. All public setters are called from
// the message thread; renderOpenGL() runs on the GL thread and only reads atomics.
class NebulaRenderer final : public juce::OpenGLRenderer
{
public:
    struct Visuals
    {
        std::atomic<float> level { 0 }, low { 0 }, high { 0 }, input { 0 };
        std::atomic<float> size { 0.5f }, decay { 0.5f }, tone { 0.3f }, shimmer { 0 }, duck { 0 }, infinite { 0 };
        std::atomic<float> mouseX { -1000 }, mouseY { -1000 }; // normalised 0..1, y down
    };

    explicit NebulaRenderer (juce::OpenGLContext& c) : context (c) {}

    Visuals visuals;

    // Normalised position (0..1, y down).
    void addRipple (float x, float y);

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    bool isWorking() const { return shaderReady.load(); }

private:
    juce::OpenGLContext& context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    GLuint vbo = 0, vao = 0;
    GLint positionAttrib = -1;
    std::atomic<bool> shaderReady { false };
    double startTime = juce::Time::getMillisecondCounterHiRes();

    struct Ripple
    {
        std::atomic<float> x { 0 }, y { 0 };
        std::atomic<double> start { -100.0 };
    };
    std::array<Ripple, 4> ripples;
    std::atomic<int> nextRipple { 0 };
};
