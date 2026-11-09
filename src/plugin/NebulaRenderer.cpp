#include "NebulaRenderer.h"
#include "NebulaShaders.h"

using namespace juce::gl;

void NebulaRenderer::addRipple (float x, float y)
{
    const int i = nextRipple.fetch_add (1) % (int) ripples.size();
    ripples[(size_t) i].x.store (x);
    ripples[(size_t) i].y.store (y);
    ripples[(size_t) i].start.store (juce::Time::getMillisecondCounterHiRes());
}

void NebulaRenderer::newOpenGLContextCreated()
{
    shaderReady.store (false);
    // Old or generic drivers (e.g. GDI OpenGL 1.1 on a VM) have no shader support: keep the
    // plain background instead of calling missing GL entry points.
    if (! context.areShadersAvailable() || juce::OpenGLShaderProgram::getLanguageVersion() < 1.2)
        return;

    auto program = std::make_unique<juce::OpenGLShaderProgram> (context);
    const bool ok = program->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (NebulaShaders::vertex))
                 && program->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (NebulaShaders::fragment))
                 && program->link();
    if (! ok)
    {
        DBG ("Nebula shader failed: " << program->getLastError());
        return;
    }
    shader = std::move (program);

    if (juce::OpenGLShaderProgram::getLanguageVersion() >= 1.3)
    {
        glGenVertexArrays (1, &vao);
        glBindVertexArray (vao);
    }

    static const GLfloat quad[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    glGenBuffers (1, &vbo);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    if (vao != 0) glBindVertexArray (0);

    positionAttrib = glGetAttribLocation (shader->getProgramID(), "position");
    shaderReady.store (positionAttrib >= 0);
}

void NebulaRenderer::renderOpenGL()
{
    const float scale = (float) context.getRenderingScale();
    auto* target = context.getTargetComponent();
    if (target == nullptr) return;
    const float w = scale * (float) target->getWidth();
    const float h = scale * (float) target->getHeight();

    glViewport (0, 0, juce::roundToInt (w), juce::roundToInt (h));
    juce::OpenGLHelpers::clear (juce::Colour (0xff05040a));
    if (! shaderReady.load() || shader == nullptr) return;

    glDisable (GL_DEPTH_TEST);
    glDisable (GL_BLEND);
    shader->use();

    const double now = juce::Time::getMillisecondCounterHiRes();
    auto set1 = [this] (const char* name, float v) { shader->setUniform (name, v); };
    shader->setUniform ("uRes", w, h);
    set1 ("uTime", (float) std::fmod ((now - startTime) * 0.001, 3600.0));
    set1 ("uLevel", visuals.level.load());
    set1 ("uLow", visuals.low.load());
    set1 ("uHigh", visuals.high.load());
    set1 ("uInput", visuals.input.load());
    set1 ("uSize", visuals.size.load());
    set1 ("uDecay", visuals.decay.load());
    set1 ("uTone", visuals.tone.load());
    set1 ("uShimmer", visuals.shimmer.load());
    set1 ("uDuck", visuals.duck.load());
    set1 ("uInf", visuals.infinite.load());
    set1 ("uFlow", visuals.flow.load());
    set1 ("uSwirl", visuals.swirl.load());
    set1 ("uHole", visuals.hole.load());
    set1 ("uDisk", visuals.disk.load());
    set1 ("uSpread", visuals.spread.load());
    set1 ("uPull", visuals.pull.load());
    set1 ("uFlash", visuals.flash.load());
    set1 ("uBurst", visuals.burst.load());
    // GL has y up.
    shader->setUniform ("uMouse", visuals.mouseX.load() * w, (1.0f - visuals.mouseY.load()) * h);

    static const char* rippleNames[] = { "uRipple0", "uRipple1", "uRipple2", "uRipple3" };
    for (size_t i = 0; i < ripples.size(); ++i)
    {
        const auto& r = ripples[i];
        const float age = (float) ((now - r.start.load()) * 0.001);
        shader->setUniform (rippleNames[i], r.x.load() * w, (1.0f - r.y.load()) * h, age);
    }

    if (vao != 0) glBindVertexArray (vao);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer ((GLuint) positionAttrib, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray ((GLuint) positionAttrib);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray ((GLuint) positionAttrib);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    if (vao != 0) glBindVertexArray (0);
}

void NebulaRenderer::openGLContextClosing()
{
    shaderReady.store (false);
    shader.reset();
    if (vbo != 0) glDeleteBuffers (1, &vbo);
    if (vao != 0) glDeleteVertexArrays (1, &vao);
    vbo = vao = 0;
}
