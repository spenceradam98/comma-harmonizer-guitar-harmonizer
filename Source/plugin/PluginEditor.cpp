#include "PluginEditor.h"

using namespace juce;

namespace
{
constexpr int W = 1000, H = 650;
constexpr float kStart = MathConstants<float>::pi * -0.75f, kEnd = MathConstants<float>::pi * 0.75f;

String vid (int v, const char* name) { return "v" + String (v + 1) + "_" + name; }

const char* ordinal (int n)
{
    static const char* names[] = { "unison", "2nd", "3rd", "4th", "5th", "6th", "7th", "octave",
                                   "9th", "10th", "11th", "12th", "13th", "14th", "2 octaves" };
    return (n >= 0 && n < 15) ? names[n] : "";
}
}

// ============================================================================ helpers
String CommaEditor::noteWithCents (double hz)
{
    if (hz <= 0) return "--";
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - nearest) * 100.0);
    String s = String (cm::noteName (nearest)) + String (nearest / 12 - 1);
    if (cents != 0) s << (cents > 0 ? " +" : " ") << cents << "c";
    return s;
}

String CommaEditor::stepName (int degrees, int scaleSize)
{
    if (degrees == 0) return "unison";
    const String dir = degrees > 0 ? " up" : " down";
    if (scaleSize == 7 && std::abs (degrees) <= 14) return String (ordinal (std::abs (degrees))) + dir;
    return String (std::abs (degrees)) + (std::abs (degrees) == 1 ? " step" : " steps") + dir;
}

String CommaEditor::describeVoice (int v) const
{
    const auto& st = proc.getStatus();
    if (! proc.currentParams().voices[(size_t) v].on) return "off";
    if (st.detectedHz.load() <= 0) return "waiting for a note";
    const double hz = proc.rootHz() * std::pow (2.0, st.voiceCents[(size_t) v].load() / 1200.0);
    return "-> " + noteWithCents (hz);
}

// ============================================================================ setup
CommaEditor::CommaEditor (CommaProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    lnf.setTheme (themeByName (proc.getThemeName()));

    // ---- header ----
    rebuildPresetBox();
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    prevPreset.onClick = [this] {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    };
    nextPreset.onClick = [this] {
        proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms());
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    };
    for (int i = 0; i < (int) allThemes().size(); ++i) themeBox.addItem (allThemes()[(size_t) i].name, i + 1);
    themeBox.setText (lnf.theme().name, dontSendNotification);
    themeBox.onChange = [this] { applyTheme (themeBox.getText()); };
    themeBox.setTooltip ("Theme (saved with the session)");
    for (auto* c : { (Component*) &presetBox, (Component*) &prevPreset, (Component*) &nextPreset, (Component*) &themeBox })
        addAndMakeVisible (c);

    // ---- key and tuning ----
    for (int i = 0; i < 12; ++i) keyBox.addItem (cm::noteName (i), i + 1);
    {
        const auto& sc = cm::builtinScales();
        String lastCat;
        for (int i = 0; i < (int) sc.size(); ++i)
        {
            const String cat = cm::builtinCategory (i);
            if (cat != lastCat) { scaleBox.addSectionHeading (cat); lastCat = cat; }
            scaleBox.addItem (sc[(size_t) i].getName(), i + 1);
        }
        scaleBox.addSectionHeading ("Custom");
        scaleBox.addItem ("Custom (.scl)", (int) sc.size() + 1);
    }
    retuneBox.addItem ("Off", 1);
    retuneBox.addItem ("Snap to Scale", 2);
    retuneBox.addItem ("Remap Frets", 3);
    keyAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key", keyBox);
    scaleAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scale", scaleBox);
    retuneAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "retune", retuneBox);
    keyBox.setTooltip ("Root note of the scale");
    scaleBox.setTooltip ("Scale or tuning used for scale-step harmonies and retuning");
    retuneBox.setTooltip ("Off: your guitar stays as played.\nSnap to Scale: pulls each note to the nearest pitch of the tuning (e.g. just intonation, maqam quarter tones).\n"
                          "Remap Frets: each fret becomes the next step of the tuning, so a normal guitar plays it like a microtonal fretboard.");
    loadScl.onClick = [this] {
        chooser = std::make_unique<FileChooser> ("Load a Scala scale", File(), "*.scl");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == File()) return;
            const auto err = proc.loadScala (f);
            sclStatus = err.isEmpty() ? "Loaded: " + proc.getCustomScaleName() : "Couldn't load: " + err;
            repaint();
        });
    };
    loadScl.setTooltip ("Load any Scala .scl tuning file as the Custom scale (thousands are free online)");
    for (auto* c : { (Component*) &keyBox, (Component*) &scaleBox, (Component*) &retuneBox, (Component*) &loadScl })
        addAndMakeVisible (c);

    amount = &addKnob ("retuneAmt", "AMOUNT");
    speed = &addKnob ("retuneSpeed", "SPEED");
    glide = &addKnob ("glide", "GLIDE");
    refHz = &addKnob ("refHz", "A4 REF", true);
    gate = &addKnob ("gate", "GATE");
    humanize = &addKnob ("humanize", "HUMANIZE");
    dry = &addKnob ("dry", "DRY");
    wet = &addKnob ("wet", "HARMONY");
    output = &addKnob ("output", "OUTPUT", true);
    amount->slider.setTooltip ("How far notes are pulled into the tuning");
    speed->slider.setTooltip ("How quickly retuning reaches the target (0 = instant, hard-tune)");
    glide->slider.setTooltip ("Slide time when a scale-step harmony changes interval");
    gate->slider.setTooltip ("Pitch tracking ignores input quieter than this");
    humanize->slider.setTooltip ("Slow random pitch and timing drift on each voice, like a real second player");

    // ---- voices ----
    for (int v = 0; v < cm::kNumVoices; ++v)
    {
        auto& ui = voices[(size_t) v];
        ui.on.setButtonText ("VOICE " + String (v + 1));
        ui.on.setClickingTogglesState (true);
        ui.mode.addItem ("Interval", 1);
        ui.mode.addItem ("Scale steps", 2);
        ui.onAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, vid (v, "on"), ui.on);
        ui.modeAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, vid (v, "mode"), ui.mode);
        ui.mode.setTooltip ("Interval: a fixed shift in semitones.\nScale steps: follows the key and scale, so a 3rd is major or minor as needed (intelligent harmony).");
        addAndMakeVisible (ui.on);
        addAndMakeVisible (ui.mode);

        ui.semis = &addKnob (vid (v, "semis"), "INTERVAL", true);
        ui.degrees = &addKnob (vid (v, "degrees"), "STEPS", true);
        ui.cents = &addKnob (vid (v, "cents"), "DETUNE", true);
        ui.level = &addKnob (vid (v, "level"), "LEVEL");
        ui.pan = &addKnob (vid (v, "pan"), "PAN", true);
        ui.delay = &addKnob (vid (v, "delay"), "DELAY");
        ui.semis->slider.textFromValueFunction = [] (double x) {
            const int s = (int) std::lround (x);
            return s == 0 ? String ("0 st") : (s > 0 ? "+" : "") + String (s) + " st";
        };
        ui.degrees->slider.textFromValueFunction = [this] (double x) { return stepName ((int) std::lround (x), proc.currentScale().n); };
        ui.semis->slider.updateText();
        ui.degrees->slider.updateText();
    }

    grainTexture = Image (Image::ARGB, W, H, true);
    {
        Graphics tg (grainTexture);
        Random rnd (7);
        for (int i = 0; i < 1400; ++i)
        {
            tg.setColour (Colours::black.withAlpha (0.025f + 0.03f * rnd.nextFloat()));
            const float y = (float) rnd.nextInt (H);
            tg.drawLine (0, y, (float) W, y + rnd.nextFloat() * 6.0f - 3.0f, 0.6f + rnd.nextFloat());
        }
    }

    applyTheme (lnf.theme().name);
    for (int v = 0; v < cm::kNumVoices; ++v)
    {
        const bool scaleMode = proc.currentParams().voices[(size_t) v].mode == cm::VoiceMode::Scale;
        voices[(size_t) v].degrees->slider.setVisible (scaleMode);
        voices[(size_t) v].semis->slider.setVisible (! scaleMode);
    }
    setSize (W, H);
    startTimerHz (30);
}

CommaEditor::~CommaEditor() { setLookAndFeel (nullptr); }

void CommaEditor::rebuildPresetBox()
{
    presetBox.clear (dontSendNotification);
    const auto& ps = cm::factoryPresets();
    String lastCat;
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        if (lastCat != ps[(size_t) i].category) { presetBox.addSectionHeading (ps[(size_t) i].category); lastCat = ps[(size_t) i].category; }
        presetBox.addItem (ps[(size_t) i].name, i + 1);
    }
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
}

CommaEditor::Knob& CommaEditor::addKnob (const String& id, const String& label, bool bipolar)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    auto& s = k->slider;
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::TextBoxBelow, false, 74, 16);
    s.setRotaryParameters (kStart, kEnd, true);
    s.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    if (bipolar) s.getProperties().set ("bipolar", true);
    addAndMakeVisible (s);
    k->attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    if (auto* prm = proc.apvts.getParameter (id))
        s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void CommaEditor::applyTheme (const String& name)
{
    const auto& t = themeByName (name);
    lnf.setTheme (t);
    proc.setThemeName (t.name);
    for (int v = 0; v < cm::kNumVoices; ++v)
    {
        auto& ui = voices[(size_t) v];
        const auto col = (int64) t.voiceColour (v).getARGB();
        ui.on.getProperties().set ("colour", col);
        for (auto* k : { ui.semis, ui.degrees, ui.cents, ui.level, ui.pan, ui.delay })
            k->slider.getProperties().set ("colour", col);
    }
    for (auto& k : knobs) k->slider.setColour (Slider::textBoxTextColourId, t.dim);
    sendLookAndFeelChange();
    repaint();
}

// ============================================================================ layout
void CommaEditor::placeKnob (Knob& k, Rectangle<int> area)
{
    // area includes the label row on top
    k.slider.setBounds (area.withTrimmedTop (14));
}

void CommaEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    header = r.removeFromTop (44);
    {
        auto h = header;
        h.removeFromLeft (230);
        themeBox.setBounds (h.removeFromRight (150).reduced (0, 8));
        h.removeFromRight (16);
        prevPreset.setBounds (h.removeFromLeft (30).reduced (0, 8));
        presetBox.setBounds (h.removeFromLeft (320).reduced (4, 8));
        nextPreset.setBounds (h.removeFromLeft (30).reduced (0, 8));
    }
    r.removeFromTop (8);

    auto top = r.removeFromTop (318);
    pitchPanel = top.removeFromLeft (330);
    top.removeFromLeft (10);
    mixPanel = top.removeFromRight (250);
    top.removeFromRight (10);
    tuningPanel = top;

    // pitch panel: readout + wheel
    {
        auto p = pitchPanel.reduced (12);
        p.removeFromTop (18);
        readoutArea = p.removeFromTop (44);
        wheelArea = p;
    }

    // tuning panel
    {
        auto p = tuningPanel.reduced (12);
        p.removeFromTop (20);
        auto row = [&] (int h) { auto x = p.removeFromTop (h); p.removeFromTop (6); return x; };
        auto r1 = row (26);
        keyBox.setBounds (r1.removeFromLeft (70));
        r1.removeFromLeft (8);
        scaleBox.setBounds (r1);
        auto r2 = row (26);
        loadScl.setBounds (r2.removeFromRight (96));
        r2.removeFromRight (8);
        retuneBox.setBounds (r2);
        sclInfoArea = row (16);
        auto knobRow = row (88);
        const int kw = knobRow.getWidth() / 4;
        placeKnob (*amount, knobRow.removeFromLeft (kw));
        placeKnob (*speed, knobRow.removeFromLeft (kw));
        placeKnob (*glide, knobRow.removeFromLeft (kw));
        placeKnob (*refHz, knobRow);
        auto knobRow2 = row (88);
        const int kw2 = knobRow2.getWidth() / 4;
        knobRow2.removeFromLeft (kw2 / 2);
        placeKnob (*gate, knobRow2.removeFromLeft (kw2));
        placeKnob (*humanize, knobRow2.removeFromLeft (kw2));
    }

    // mix panel
    {
        auto p = mixPanel.reduced (12);
        p.removeFromTop (20);
        auto kr = p.removeFromTop (88);
        const int kw = kr.getWidth() / 3;
        placeKnob (*dry, kr.removeFromLeft (kw));
        placeKnob (*wet, kr.removeFromLeft (kw));
        placeKnob (*output, kr);
        p.removeFromTop (12);
        meterArea = p.removeFromTop (150);
    }

    // voices
    r.removeFromTop (10);
    const int gap = 10;
    const int sw = (r.getWidth() - gap * (cm::kNumVoices - 1)) / cm::kNumVoices;
    for (int v = 0; v < cm::kNumVoices; ++v)
    {
        auto& ui = voices[(size_t) v];
        ui.strip = r.removeFromLeft (sw);
        r.removeFromLeft (gap);
        auto s = ui.strip.reduced (10);
        auto h = s.removeFromTop (26);
        ui.on.setBounds (h.removeFromLeft (92));
        h.removeFromLeft (6);
        ui.mode.setBounds (h);
        s.removeFromTop (4);
        ui.infoArea = s.removeFromTop (18);
        auto k1 = s.removeFromTop (86);
        const int kw = k1.getWidth() / 3;
        auto first = k1.removeFromLeft (kw);
        placeKnob (*ui.semis, first);
        placeKnob (*ui.degrees, first);
        placeKnob (*ui.cents, k1.removeFromLeft (kw));
        placeKnob (*ui.level, k1);
        auto k2 = s.removeFromTop (86);
        k2.removeFromLeft (kw / 2);
        placeKnob (*ui.pan, k2.removeFromLeft (kw));
        placeKnob (*ui.delay, k2.removeFromLeft (kw));
    }
}

// ============================================================================ painting
void CommaEditor::drawPanel (Graphics& g, Rectangle<int> area, const String& title)
{
    const auto& t = lnf.theme();
    auto r = area.toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (t.edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (t.dim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (title, area.reduced (12, 8).removeFromTop (14), Justification::left);
    }
}

void CommaEditor::drawMeter (Graphics& g, Rectangle<float> r, float level, const String& label)
{
    const auto& t = lnf.theme();
    g.setColour (t.bg2);
    g.fillRoundedRectangle (r, 3.0f);
    const float db = level > 0 ? 20.0f * std::log10 (level) : -100.0f;
    const float frac = jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
    auto fill = r.reduced (2).withTrimmedTop ((r.getHeight() - 4) * (1.0f - frac));
    g.setColour (db > -1.0f ? Colour (0xffff4040) : t.accent);
    g.fillRoundedRectangle (fill, 2.0f);
    g.setColour (t.dim);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (label, r.withY (r.getBottom() + 2).withHeight (12).withSizeKeepingCentre (40, 12), Justification::centred);
}

void CommaEditor::drawWheel (Graphics& g, Rectangle<float> area)
{
    const auto& t = lnf.theme();
    const auto& st = proc.getStatus();
    const auto& scale = proc.currentScale();
    const double period = scale.period();
    const float d = jmin (area.getWidth(), area.getHeight()) - 34.0f;
    const auto c = area.getCentre();
    const float R = d * 0.5f;

    auto angleOf = [&] (double cents) {
        double r = std::fmod (cents, period);
        if (r < 0) r += period;
        return (float) (r / period * MathConstants<double>::twoPi);
    };

    g.setColour (t.bg2);
    g.fillEllipse (Rectangle<float> (d, d).withCentre (c));
    g.setColour (t.edge);
    g.drawEllipse (Rectangle<float> (d, d).withCentre (c), 1.0f);

    // faint 12-TET reference (when the period is an octave)
    if (std::abs (period - 1200.0) < 0.01)
        for (int i = 0; i < 12; ++i)
        {
            const float a = angleOf (i * 100.0);
            g.setColour (t.dim.withAlpha (0.35f));
            g.drawLine (Line<float> (c.getPointOnCircumference (R * 0.70f, a), c.getPointOnCircumference (R * 0.78f, a)), 1.0f);
        }

    // scale steps
    const int n = scale.n;
    for (int k = 0; k < n; ++k)
    {
        const double cents = scale.degreeCents (k);
        const float a = angleOf (cents);
        const bool root = k == 0;
        g.setColour (root ? t.accent : t.text.withAlpha (0.8f));
        g.drawLine (Line<float> (c.getPointOnCircumference (R * 0.84f, a), c.getPointOnCircumference (R, a)), root ? 3.0f : 1.6f);
        if (n <= 24)
        {
            const auto p = c.getPointOnCircumference (R + 11.0f, a);
            g.setColour (t.dim);
            g.setFont (FontOptions (n <= 12 ? 11.0f : 9.0f));
            g.drawText (String (k + 1), Rectangle<float> (24, 12).withCentre (p), Justification::centred);
        }
    }

    // centre text: key + scale
    g.setColour (t.dim);
    g.setFont (FontOptions (11.0f));
    const String keyName = cm::noteName ((int) proc.apvts.getRawParameterValue ("key")->load());
    g.drawFittedText (keyName + " " + scale.getName(), Rectangle<float> (R * 1.2f, 30).withCentre (c.translated (0, R * 0.30f)).toNearestInt(),
                      Justification::centred, 2);

    const bool voiced = st.voiced.load();
    const bool have = st.detectedHz.load() > 0;
    if (! have) return;

    auto dot = [&] (double cents, float radius, float size, Colour col, const String& txt)
    {
        const auto p = c.getPointOnCircumference (radius, angleOf (cents));
        if (t.glow)
        {
            g.setColour (col.withAlpha (0.22f));
            g.fillEllipse (Rectangle<float> (size * 2.2f, size * 2.2f).withCentre (p));
        }
        g.setColour (col.withMultipliedAlpha (voiced ? 1.0f : 0.45f));
        g.fillEllipse (Rectangle<float> (size, size).withCentre (p));
        if (txt.isNotEmpty())
        {
            g.setColour (col.contrasting (0.9f));
            g.setFont (FontOptions (size * 0.62f, Font::bold));
            g.drawText (txt, Rectangle<float> (size, size).withCentre (p), Justification::centred);
        }
    };

    // input (hollow ring) and lead (filled)
    const double in = st.inputCents.load();
    {
        const auto p = c.getPointOnCircumference (R * 0.62f, angleOf (in));
        g.setColour (t.text.withAlpha (voiced ? 0.9f : 0.4f));
        g.drawEllipse (Rectangle<float> (14, 14).withCentre (p), 2.0f);
        g.drawLine (Line<float> (c, p), 1.0f);
    }
    if (proc.currentParams().retune != cm::RetuneMode::Off)
        dot (st.leadCents.load(), R * 0.62f, 10.0f, t.accent, {});
    for (int v = 0; v < cm::kNumVoices; ++v)
        if (st.voiceOn[(size_t) v].load())
            dot (st.voiceCents[(size_t) v].load(), R * (0.92f - 0.0f), 18.0f, t.voiceColour (v), String (v + 1));
}

void CommaEditor::paint (Graphics& g)
{
    const auto& t = lnf.theme();
    g.setGradientFill (ColourGradient (t.bg2, 0, 0, t.bg, 0, (float) H, false));
    g.fillAll();
    if (t.grain) g.drawImageAt (grainTexture, 0, 0);

    // header: wordmark
    {
        auto h = header;
        g.setColour (t.text);
        g.setFont (FontOptions (30.0f, Font::bold));
        g.drawText ("COMMA", h.removeFromLeft (130), Justification::centredLeft);
        g.setColour (t.accent);
        g.setFont (FontOptions (11.0f));
        g.drawText ("harmonizer\nmicrotonal", h.removeFromLeft (100), Justification::centredLeft);
    }

    drawPanel (g, pitchPanel, "PITCH");
    drawPanel (g, tuningPanel, "KEY & TUNING");
    drawPanel (g, mixPanel, "MIX");

    // readout
    {
        const auto& st = proc.getStatus();
        const float hz = st.detectedHz.load();
        auto r = readoutArea;
        g.setColour (st.voiced.load() ? t.text : t.dim);
        g.setFont (FontOptions (28.0f, Font::bold));
        g.drawText (noteWithCents (hz), r.removeFromLeft (170), Justification::centredLeft);
        g.setFont (FontOptions (12.0f));
        g.setColour (t.dim);
        String info = hz > 0 ? String (hz, 1) + " Hz\n" : String ("play a note\n");
        if (hz > 0) info << "degree " << (st.degree.load() + 1) << " of " << proc.currentScale().n;
        g.drawFittedText (info, r, Justification::centredRight, 2);
    }
    drawWheel (g, wheelArea.toFloat());

    // scl status
    g.setColour (t.dim);
    g.setFont (FontOptions (11.0f));
    {
        String s = sclStatus;
        if (s.isEmpty())
            s = (int) proc.apvts.getRawParameterValue ("scale")->load() == CommaProcessor::customScaleIndex()
                    ? "Custom: " + proc.getCustomScaleName() : String ("Period " + String (proc.currentScale().period(), 1) + "c, "
                                                                       + String (proc.currentScale().n) + " notes");
        g.drawText (s, sclInfoArea, Justification::centredLeft);
    }

    // meters
    {
        auto m = meterArea.toFloat();
        const float mw = 16;
        auto pair = m.withSizeKeepingCentre (mw * 2 + 30, m.getHeight() - 16).withY (m.getY());
        drawMeter (g, pair.removeFromLeft (mw), inMeter, "IN");
        pair.removeFromLeft (30);
        drawMeter (g, pair.removeFromLeft (mw), outMeter, "OUT");
    }

    // voices
    for (int v = 0; v < cm::kNumVoices; ++v)
    {
        auto& ui = voices[(size_t) v];
        drawPanel (g, ui.strip, {});
        g.setColour (t.voiceColour (v));
        g.fillRoundedRectangle (ui.strip.toFloat().removeFromTop (3).reduced (8, 0), 1.5f);
        g.setColour (ui.on.getToggleState() ? t.text : t.dim);
        g.setFont (FontOptions (12.0f));
        g.drawText (describeVoice (v), ui.infoArea, Justification::centredLeft);
    }

    // knob labels
    g.setFont (FontOptions (10.0f, Font::bold));
    for (auto& k : knobs)
        if (k->slider.isVisible())
        {
            g.setColour (t.dim);
            g.drawText (k->label, k->slider.getBounds().withY (k->slider.getY() - 14).withHeight (14), Justification::centred);
        }
}

void CommaEditor::timerCallback()
{
    const auto& st = proc.getStatus();
    inMeter = jmax (st.inLevel.load(), inMeter * 0.85f);
    outMeter = jmax (st.outLevel.load(), outMeter * 0.85f);

    const auto p = proc.currentParams();
    bool layoutChanged = false;
    for (int v = 0; v < cm::kNumVoices; ++v)
    {
        auto& ui = voices[(size_t) v];
        const bool scaleMode = p.voices[(size_t) v].mode == cm::VoiceMode::Scale;
        if (ui.degrees->slider.isVisible() != scaleMode)
        {
            ui.degrees->slider.setVisible (scaleMode);
            ui.semis->slider.setVisible (! scaleMode);
            layoutChanged = true;
        }
        ui.degrees->slider.updateText();   // step names depend on the scale size
    }
    const bool retuneOn = p.retune != cm::RetuneMode::Off;
    amount->slider.setEnabled (retuneOn);
    speed->slider.setEnabled (retuneOn);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);

    if (layoutChanged) repaint();
    else
    {
        repaint (pitchPanel);
        repaint (meterArea);
        repaint (sclInfoArea);
        for (auto& ui : voices) repaint (ui.infoArea);
    }
}
