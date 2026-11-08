#include "DragExportZone.h"

#include <juce_audio_formats/juce_audio_formats.h>

DragExportZone::DragExportZone (const rvb::TailRecorder& r) : recorder (r)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip (juce::String::fromUTF8 ("Glisse cette zone dans ta piste pour cr\xc3\xa9" "er un WAV de la queue de reverb. "
                                        "Double-clic : ouvrir le dossier des exports."));
}

juce::File DragExportZone::getExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Horizon Noir")
        .getChildFile ("Exports");
}

void DragExportZone::advance (float dt, float level)
{
    phase += dt;
    flash = juce::jmax (0.0f, flash - dt * 1.5f);
    historyTimer += dt;
    if (historyTimer >= 1.0f / 30.0f)
    {
        historyTimer = 0.0f;
        history[historyPos] = level;
        historyPos = (historyPos + 1) % history.size();
    }
    repaint();
}

void DragExportZone::paint (juce::Graphics& g)
{
    using namespace juce;
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool hasTake = recorder.hasTake();
    const bool active = recorder.isTakeActive();
    const bool over = isMouseOver (true);
    const float corner = b.getHeight() * 0.5f;

    g.setColour (Colour (0x880a0814));
    g.fillRoundedRectangle (b, corner);

    // Live level history drawn as a mirrored waveform.
    {
        auto wave = b.reduced (corner * 0.8f, 6.0f);
        Path p;
        const int n = (int) history.size();
        for (int i = 0; i < n; ++i)
        {
            const float v = history[(historyPos + (size_t) i) % history.size()];
            const float xx = wave.getX() + wave.getWidth() * (float) i / (float) (n - 1);
            const float hh = jmax (0.5f, v * wave.getHeight() * 0.5f);
            p.addRoundedRectangle (xx - 1.0f, wave.getCentreY() - hh, 2.0f, hh * 2.0f, 1.0f);
        }
        ColourGradient grad (Theme::magenta.withAlpha (0.35f), wave.getX(), 0, Theme::cyan.withAlpha (0.35f), wave.getRight(), 0, false);
        g.setGradientFill (grad);
        g.fillPath (p);
    }

    // Border: animated gradient while hovered or recording.
    {
        const float a = over ? 0.95f : (active ? 0.6f + 0.3f * std::sin (phase * 4.0f) : 0.35f);
        ColourGradient edge (Theme::magenta.withAlpha (a), b.getX(), b.getY(), Theme::cyan.withAlpha (a), b.getRight(), b.getBottom(), false);
        g.setGradientFill (edge);
        g.drawRoundedRectangle (b, corner, over ? 2.0f : 1.4f);
        if (flash > 0.0f)
        {
            g.setColour (Colours::white.withAlpha (flash * 0.5f));
            g.fillRoundedRectangle (b, corner);
        }
    }

    auto content = b.reduced (corner * 0.7f, 0.0f);

    // Drag glyph (arrow leaving a box).
    {
        auto icon = content.removeFromLeft (b.getHeight() * 0.8f).reduced (b.getHeight() * 0.18f);
        Path arrow;
        const auto c = icon.getCentre();
        const float s = icon.getHeight() * 0.5f;
        arrow.startNewSubPath (c.x, c.y - s);
        arrow.lineTo (c.x, c.y + s * 0.5f);
        arrow.startNewSubPath (c.x - s * 0.55f, c.y - s * 0.05f);
        arrow.lineTo (c.x, c.y + s * 0.5f);
        arrow.lineTo (c.x + s * 0.55f, c.y - s * 0.05f);
        arrow.startNewSubPath (c.x - s, c.y + s * 0.95f);
        arrow.lineTo (c.x + s, c.y + s * 0.95f);
        g.setColour (hasTake ? Colours::white : Theme::inkDim);
        g.strokePath (arrow, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Status on the right.
    auto status = content.removeFromRight (84.0f);
    String right;
    Colour rightCol = Theme::inkDim;
    if (hasTake)
    {
        const double secs = recorder.currentTakeSeconds();
        right = String (secs, 1) + " s";
        rightCol = active ? Theme::magenta.brighter (0.3f) : Theme::cyan;
    }
    else
    {
        right = "--";
    }
    g.setFont (Theme::labelFont (19.0f));
    g.setColour (rightCol);
    g.drawText (right, status.removeFromTop (status.getHeight() * 0.6f), Justification::centredRight);
    g.setFont (Theme::labelFont (12.0f, false));
    g.setColour (Theme::inkDim);
    g.drawText (active ? String ("REC") : (hasTake ? String::fromUTF8 ("PR\xc3\x8aT") : String ("EN ATTENTE")), status,
                Justification::centredRight);
    if (active)
    {
        g.setColour (Theme::magenta.withAlpha (0.5f + 0.5f * std::sin (phase * 6.0f)));
        g.fillEllipse (status.getRight() - 46.0f, status.getCentreY() - 3.0f, 6.0f, 6.0f);
    }

    // Main text.
    g.setColour (Colours::white.withAlpha (hasTake ? 1.0f : 0.6f));
    g.setFont (Theme::labelFont (18.0f));
    auto top = content.removeFromTop (content.getHeight() * 0.56f);
    g.drawText (lastMessage.isNotEmpty() ? lastMessage : String ("GLISSE LA NAPPE"), top, Justification::bottomLeft);
    g.setColour (Theme::inkDim);
    g.setFont (Theme::labelFont (13.0f, false));
    g.drawText (String::fromUTF8 ("vers ta piste \xc2\xb7 WAV 24 bits"), content, Justification::topLeft);
}

juce::File DragExportZone::writeTake()
{
    const auto take = recorder.renderTake();
    if (take.left.empty())
        return {};

    auto folder = getExportFolder();
    if (! folder.createDirectory())
        return {};

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
    const auto name = "HorizonNoir_" + stamp + (take.infinite ? "_INFINI_" : "_") + juce::String (juce::roundToInt (take.seconds())) + "s.wav";
    auto file = folder.getNonexistentChildFile (name.upToLastOccurrenceOf (".wav", false, false), ".wav", false);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return {};

    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), take.sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release(); // owned by the writer now

    const float* channels[] = { take.left.data(), take.right.data() };
    const bool ok = writer->writeFromFloatArrays (channels, 2, (int) take.left.size());
    writer.reset();
    if (! ok)
    {
        file.deleteFile();
        return {};
    }
    return file;
}

void DragExportZone::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6)
        return;
    dragStarted = true;

    if (! recorder.hasTake())
    {
        lastMessage = juce::String::fromUTF8 ("JOUE D'ABORD UN SON");
        repaint();
        return;
    }

    const auto file = writeTake();
    if (! file.existsAsFile())
    {
        lastMessage = juce::String::fromUTF8 ("EXPORT IMPOSSIBLE");
        repaint();
        return;
    }

    lastMessage = {};
    flash = 1.0f;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void DragExportZone::mouseDoubleClick (const juce::MouseEvent&)
{
    auto folder = getExportFolder();
    folder.createDirectory();
    folder.startAsProcess();
}
