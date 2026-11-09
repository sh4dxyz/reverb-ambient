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
        .getChildFile ("D33P")
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
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool hasTake = recorder.hasTake();
    const bool active = recorder.isTakeActive();
    const bool over = isMouseOver (true);

    g.setColour (Theme::panel);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (over ? Theme::knobRim.brighter (0.4f) : Theme::edge.brighter (0.2f));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
    if (flash > 0.0f)
    {
        g.setColour (Theme::ink.withAlpha (flash * 0.25f));
        g.fillRoundedRectangle (b, 3.0f);
    }

    auto content = b.reduced (10.0f, 0.0f);

    // Status LED: amber while recording, cream when a take is ready.
    {
        const Point<float> c (content.getX() + 3.0f, content.getCentreY());
        const Colour led = active ? Theme::accent.withAlpha (0.55f + 0.45f * std::sin (phase * 5.0f))
                                  : (hasTake ? Theme::ink.withAlpha (0.8f) : Theme::knobRim);
        g.setColour (led);
        g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        content.removeFromLeft (14.0f);
    }

    // Duration on the right.
    auto right = content.removeFromRight (44.0f);
    g.setFont (Theme::labelFont (14.0f));
    g.setColour (hasTake ? Theme::ink : Theme::inkDim);
    g.drawText (hasTake ? String (recorder.currentTakeSeconds(), 1) + " s" : String ("--"), right, Justification::centredRight);

    // Small level history between text and duration.
    auto meter = content.removeFromRight (56.0f).reduced (4.0f, 11.0f);
    {
        const int n = 32;
        const float w = meter.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const size_t idx = (historyPos + history.size() - (size_t) n + (size_t) i) % history.size();
            const float h = jmax (1.0f, history[idx] * meter.getHeight());
            g.setColour (Theme::inkDim.withAlpha (0.55f));
            g.fillRect (meter.getX() + (float) i * w, meter.getBottom() - h, jmax (1.0f, w - 1.0f), h);
        }
    }

    g.setFont (Theme::labelFont (13.0f));
    g.setColour (over ? Theme::ink : Theme::inkDim.brighter (0.3f));
    g.drawText (lastMessage.isNotEmpty() ? lastMessage : String ("DRAG TO DAW"), content, Justification::centredLeft);
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
    const auto name = "D33P_" + stamp + (take.infinite ? "_FREEZE_" : "_") + juce::String (juce::roundToInt (take.seconds())) + "s.wav";
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
        lastMessage = juce::String ("PLAY SOMETHING FIRST");
        repaint();
        return;
    }

    const auto file = writeTake();
    if (! file.existsAsFile())
    {
        lastMessage = juce::String ("EXPORT FAILED");
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
