#include "MixerTab.h"
#include "MainWindow.h"
#include "../audio/AudioPipeline.h"
#include "../core/Diagnostics.h"
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace vc {

namespace {
constexpr int SLIDER_MAX = 1000;

int toSlider(double v, double lo, double hi)
{ return int(std::clamp((v - lo) / (hi - lo), 0.0, 1.0) * SLIDER_MAX); }

double fromSlider(int s, double lo, double hi)
{ return lo + (hi - lo) * (double(s) / double(SLIDER_MAX)); }
} // namespace

namespace {
// slider range helper (file-scope: usable inside lambdas without capture)
std::pair<double,double> rng(double a, double b) { return {a, b}; }
}

MixerTab::MixerTab(MainWindow* win, QWidget* parent)
    : QWidget(parent), m_win(win)
    , m_hpfEn(nullptr), m_nsEn(nullptr), m_gateEn(nullptr)
    , m_eqEn(nullptr), m_deessEn(nullptr), m_compEn(nullptr)
    , m_satEn(nullptr), m_loudEn(nullptr), m_limEn(nullptr)
    , m_hpfFreq(nullptr), m_nsAmount(nullptr)
    , m_gateThr(nullptr), m_gateRange(nullptr), m_gateHold(nullptr)
    , m_warm(nullptr), m_mudFreq(nullptr), m_mudGain(nullptr)
    , m_presFreq(nullptr), m_presGain(nullptr), m_clarFreq(nullptr)
    , m_clarGain(nullptr), m_air(nullptr)
    , m_deessFreq(nullptr), m_deessAmt(nullptr)
    , m_compThr(nullptr), m_compRatio(nullptr), m_compKnee(nullptr)
    , m_compAtk(nullptr), m_compRel(nullptr), m_compMakeup(nullptr)
    , m_satDrive(nullptr)
    , m_loudTarget(nullptr), m_loudMaxGain(nullptr)
    , m_limCeil(nullptr), m_limRel(nullptr)
    , m_outGain(nullptr)
    , m_hpfFreqV(nullptr), m_nsAmountV(nullptr)
    , m_gateThrV(nullptr), m_gateRangeV(nullptr), m_gateHoldV(nullptr)
    , m_warmV(nullptr), m_mudFreqV(nullptr), m_mudGainV(nullptr)
    , m_presFreqV(nullptr), m_presGainV(nullptr), m_clarFreqV(nullptr)
    , m_clarGainV(nullptr), m_airV(nullptr)
    , m_deessFreqV(nullptr), m_deessAmtV(nullptr)
    , m_compThrV(nullptr), m_compRatioV(nullptr), m_compKneeV(nullptr)
    , m_compAtkV(nullptr), m_compRelV(nullptr), m_compMakeupV(nullptr)
    , m_satDriveV(nullptr)
    , m_loudTargetV(nullptr), m_loudMaxGainV(nullptr)
    , m_limCeilV(nullptr), m_limRelV(nullptr)
    , m_outGainV(nullptr)
    , m_compGr(nullptr), m_gateGr(nullptr), m_loudGain(nullptr)
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* root = new QHBoxLayout(content);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(14);

    // ============================ column 1 ==============================
    auto* col1 = new QVBoxLayout;

    auto* hpfBox = new QGroupBox(QStringLiteral("1 · High-Pass Filter"));
    auto* hg = new QGridLayout(hpfBox);
    m_hpfEn = new QCheckBox(QStringLiteral("Enabled"));
    hg->addWidget(m_hpfEn, 0, 0, 1, 3);
    hg->addWidget(new QLabel(QStringLiteral("Cutoff")), 1, 0);
    m_hpfFreq = addSlider(hg, 1, 1, 20, 300, 1, m_params.hpfFreqHz);
    m_hpfFreqV = addValueLabel(hg, 1, 2);
    col1->addWidget(hpfBox);

    auto* nsBox = new QGroupBox(QStringLiteral("2 · Noise Suppression (RNNoise)"));
    auto* ng = new QGridLayout(nsBox);
    m_nsEn = new QCheckBox(QStringLiteral("Enabled"));
    ng->addWidget(m_nsEn, 0, 0, 1, 3);
    ng->addWidget(new QLabel(QStringLiteral("Strength")), 1, 0);
    m_nsAmount = addSlider(ng, 1, 1, 0, 100, 1, m_params.nsAmount);
    m_nsAmountV = addValueLabel(ng, 1, 2);
    col1->addWidget(nsBox);

    auto* gateBox = new QGroupBox(QStringLiteral("3 · Noise Gate / Expander"));
    auto* gg = new QGridLayout(gateBox);
    m_gateEn = new QCheckBox(QStringLiteral("Enabled"));
    gg->addWidget(m_gateEn, 0, 0, 1, 3);
    gg->addWidget(new QLabel(QStringLiteral("Threshold")), 1, 0);
    m_gateThr = addSlider(gg, 1, 1, -80, -20, 1, m_params.gateThresholdDb);
    m_gateThrV = addValueLabel(gg, 1, 2);
    gg->addWidget(new QLabel(QStringLiteral("Max reduction")), 2, 0);
    m_gateRange = addSlider(gg, 2, 1, -60, 0, 1, m_params.gateRangeDb);
    m_gateRangeV = addValueLabel(gg, 2, 2);
    gg->addWidget(new QLabel(QStringLiteral("Hold")), 3, 0);
    m_gateHold = addSlider(gg, 3, 1, 10, 500, 5, m_params.gateHoldMs);
    m_gateHoldV = addValueLabel(gg, 3, 2);
    m_gateGr = new QLabel(QStringLiteral("gate: — dB"));
    m_gateGr->setObjectName(QStringLiteral("mono"));
    gg->addWidget(m_gateGr, 4, 0, 1, 3);
    col1->addWidget(gateBox);

    auto* deessBox = new QGroupBox(QStringLiteral("5 · De-Esser"));
    auto* dg = new QGridLayout(deessBox);
    m_deessEn = new QCheckBox(QStringLiteral("Enabled"));
    dg->addWidget(m_deessEn, 0, 0, 1, 3);
    dg->addWidget(new QLabel(QStringLiteral("Sibilance freq")), 1, 0);
    m_deessFreq = addSlider(dg, 1, 1, 4000, 9000, 50, m_params.deessFreqHz);
    m_deessFreqV = addValueLabel(dg, 1, 2);
    dg->addWidget(new QLabel(QStringLiteral("Strength")), 2, 0);
    m_deessAmt = addSlider(dg, 2, 1, 0, 100, 1, m_params.deessAmount);
    m_deessAmtV = addValueLabel(dg, 2, 2);
    col1->addWidget(deessBox);

    auto* satBox = new QGroupBox(QStringLiteral("7 · Saturation (optional)"));
    auto* sg = new QGridLayout(satBox);
    m_satEn = new QCheckBox(QStringLiteral("Enabled"));
    sg->addWidget(m_satEn, 0, 0, 1, 3);
    sg->addWidget(new QLabel(QStringLiteral("Drive")), 1, 0);
    m_satDrive = addSlider(sg, 1, 1, 0, 100, 1, m_params.satDrive);
    m_satDriveV = addValueLabel(sg, 1, 2);
    col1->addWidget(satBox);

    col1->addStretch(1);
    root->addLayout(col1, 1);

    // ============================ column 2 ==============================
    auto* col2 = new QVBoxLayout;

    auto* eqBox = new QGroupBox(QStringLiteral("4 · Parametric EQ (voice curve)"));
    auto* eg = new QGridLayout(eqBox);
    m_eqEn = new QCheckBox(QStringLiteral("Enabled"));
    eg->addWidget(m_eqEn, 0, 0, 1, 3);
    eg->addWidget(new QLabel(QStringLiteral("Warmth (120 Hz shelf)")), 1, 0);
    m_warm = addSlider(eg, 1, 1, -6, 6, 0.5, m_params.eqWarmGainDb);
    m_warmV = addValueLabel(eg, 1, 2);
    eg->addWidget(new QLabel(QStringLiteral("Mud cut freq")), 2, 0);
    m_mudFreq = addSlider(eg, 2, 1, 200, 500, 5, m_params.eqMudFreqHz);
    m_mudFreqV = addValueLabel(eg, 2, 2);
    eg->addWidget(new QLabel(QStringLiteral("Mud cut gain")), 3, 0);
    m_mudGain = addSlider(eg, 3, 1, -9, 6, 0.5, m_params.eqMudGainDb);
    m_mudGainV = addValueLabel(eg, 3, 2);
    eg->addWidget(new QLabel(QStringLiteral("Presence freq")), 4, 0);
    m_presFreq = addSlider(eg, 4, 1, 1500, 6000, 25, m_params.eqPresenceFreqHz);
    m_presFreqV = addValueLabel(eg, 4, 2);
    eg->addWidget(new QLabel(QStringLiteral("Presence gain")), 5, 0);
    m_presGain = addSlider(eg, 5, 1, -6, 8, 0.5, m_params.eqPresenceGainDb);
    m_presGainV = addValueLabel(eg, 5, 2);
    eg->addWidget(new QLabel(QStringLiteral("Clarity freq")), 6, 0);
    m_clarFreq = addSlider(eg, 6, 1, 3000, 12000, 50, m_params.eqClarityFreqHz);
    m_clarFreqV = addValueLabel(eg, 6, 2);
    eg->addWidget(new QLabel(QStringLiteral("Clarity gain")), 7, 0);
    m_clarGain = addSlider(eg, 7, 1, -6, 8, 0.5, m_params.eqClarityGainDb);
    m_clarGainV = addValueLabel(eg, 7, 2);
    eg->addWidget(new QLabel(QStringLiteral("Air (10 kHz shelf)")), 8, 0);
    m_air = addSlider(eg, 8, 1, -6, 6, 0.5, m_params.eqAirGainDb);
    m_airV = addValueLabel(eg, 8, 2);
    col2->addWidget(eqBox);

    auto* compBox = new QGroupBox(QStringLiteral("6 · Compressor"));
    auto* cg = new QGridLayout(compBox);
    m_compEn = new QCheckBox(QStringLiteral("Enabled"));
    cg->addWidget(m_compEn, 0, 0, 1, 3);
    cg->addWidget(new QLabel(QStringLiteral("Threshold")), 1, 0);
    m_compThr = addSlider(cg, 1, 1, -60, 0, 1, m_params.compThresholdDb);
    m_compThrV = addValueLabel(cg, 1, 2);
    cg->addWidget(new QLabel(QStringLiteral("Ratio")), 2, 0);
    m_compRatio = addSlider(cg, 2, 1, 1, 20, 0.1, m_params.compRatio);
    m_compRatioV = addValueLabel(cg, 2, 2);
    cg->addWidget(new QLabel(QStringLiteral("Knee")), 3, 0);
    m_compKnee = addSlider(cg, 3, 1, 0, 24, 0.5, m_params.compKneeDb);
    m_compKneeV = addValueLabel(cg, 3, 2);
    cg->addWidget(new QLabel(QStringLiteral("Attack")), 4, 0);
    m_compAtk = addSlider(cg, 4, 1, 0.5, 100, 0.5, m_params.compAttackMs);
    m_compAtkV = addValueLabel(cg, 4, 2);
    cg->addWidget(new QLabel(QStringLiteral("Release")), 5, 0);
    m_compRel = addSlider(cg, 5, 1, 10, 1000, 5, m_params.compReleaseMs);
    m_compRelV = addValueLabel(cg, 5, 2);
    cg->addWidget(new QLabel(QStringLiteral("Makeup")), 6, 0);
    m_compMakeup = addSlider(cg, 6, 1, 0, 24, 0.5, m_params.compMakeupDb);
    m_compMakeupV = addValueLabel(cg, 6, 2);
    m_compGr = new QLabel(QStringLiteral("GR: — dB"));
    m_compGr->setObjectName(QStringLiteral("mono"));
    cg->addWidget(m_compGr, 7, 0, 1, 3);
    col2->addWidget(compBox);

    col2->addStretch(1);
    root->addLayout(col2, 1);

    // ============================ column 3 ==============================
    auto* col3 = new QVBoxLayout;

    auto* loudBox = new QGroupBox(QStringLiteral("8 · Loudness Control"));
    auto* lg = new QGridLayout(loudBox);
    m_loudEn = new QCheckBox(QStringLiteral("Enabled"));
    lg->addWidget(m_loudEn, 0, 0, 1, 3);
    lg->addWidget(new QLabel(QStringLiteral("Target (LUFS)")), 1, 0);
    m_loudTarget = addSlider(lg, 1, 1, -24, -12, 0.5, m_params.loudTargetLufs);
    m_loudTargetV = addValueLabel(lg, 1, 2);
    lg->addWidget(new QLabel(QStringLiteral("Max gain cap")), 2, 0);
    m_loudMaxGain = addSlider(lg, 2, 1, 0, 18, 0.5, m_params.loudMaxGainDb);
    m_loudMaxGainV = addValueLabel(lg, 2, 2);
    m_loudGain = new QLabel(QStringLiteral("applied: — dB"));
    m_loudGain->setObjectName(QStringLiteral("mono"));
    lg->addWidget(m_loudGain, 3, 0, 1, 3);
    auto* lnote = new QLabel(QStringLiteral(
        "-16 LUFS: podcast standard. -14: YouTube/streaming. "
        "Measured live — never a blind promise."));
    lnote->setObjectName(QStringLiteral("muted"));
    lnote->setWordWrap(true);
    lg->addWidget(lnote, 4, 0, 1, 3);
    col3->addWidget(loudBox);

    auto* limBox = new QGroupBox(QStringLiteral("9 · True-Peak Limiter"));
    auto* lig = new QGridLayout(limBox);
    m_limEn = new QCheckBox(QStringLiteral("Enabled"));
    lig->addWidget(m_limEn, 0, 0, 1, 3);
    lig->addWidget(new QLabel(QStringLiteral("Ceiling (dBTP)")), 1, 0);
    m_limCeil = addSlider(lig, 1, 1, -12, 0, 0.5, m_params.limCeilingDb);
    m_limCeilV = addValueLabel(lig, 1, 2);
    lig->addWidget(new QLabel(QStringLiteral("Release")), 2, 0);
    m_limRel = addSlider(lig, 2, 1, 10, 500, 5, m_params.limReleaseMs);
    m_limRelV = addValueLabel(lig, 2, 2);
    col3->addWidget(limBox);

    auto* outBox = new QGroupBox(QStringLiteral("10 · Output"));
    auto* og = new QGridLayout(outBox);
    og->addWidget(new QLabel(QStringLiteral("Output trim")), 0, 0);
    m_outGain = addSlider(og, 0, 1, -24, 24, 0.5, m_params.outputGainDb);
    m_outGainV = addValueLabel(og, 0, 2);
    col3->addWidget(outBox);

    col3->addStretch(1);
    root->addLayout(col3, 1);

    scroll->setWidget(content);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // ---- wiring: sliders -> params -------------------------------------
    auto bind = [this](QSlider* s, auto get, auto set) {
        connect(s, &QSlider::valueChanged, this, [this, get, set](int v) {
            if (!m_updating) {
                set(m_params, fromSlider(v, get().first, get().second));
                syncLabels();
                emit paramsEdited(m_params);
            }
        });
    };
    auto sliderVal = [](QSlider* s) { return s->value(); };

    // helpers producing get() pairs

    connect(m_hpfEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.hpfEnabled = on; emit paramsEdited(m_params); });
    bind(m_hpfFreq, [] { return rng(20, 300); },
         [this](DspParams& p, double v) { p.hpfFreqHz = float(v); });
    connect(m_nsEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.nsEnabled = on; emit paramsEdited(m_params); });
    bind(m_nsAmount, [] { return rng(0, 100); },
         [this](DspParams& p, double v) { p.nsAmount = float(v); });

    connect(m_gateEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.gateEnabled = on; emit paramsEdited(m_params); });
    bind(m_gateThr, [] { return rng(-80, -20); },
         [this](DspParams& p, double v) { p.gateThresholdDb = float(v); });
    bind(m_gateRange, [] { return rng(-60, 0); },
         [this](DspParams& p, double v) { p.gateRangeDb = float(v); });
    bind(m_gateHold, [] { return rng(10, 500); },
         [this](DspParams& p, double v) { p.gateHoldMs = float(v); });

    connect(m_eqEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.eqEnabled = on; emit paramsEdited(m_params); });
    bind(m_warm, [] { return rng(-6, 6); },
         [this](DspParams& p, double v) { p.eqWarmGainDb = float(v); });
    bind(m_mudFreq, [] { return rng(200, 500); },
         [this](DspParams& p, double v) { p.eqMudFreqHz = float(v); });
    bind(m_mudGain, [] { return rng(-9, 6); },
         [this](DspParams& p, double v) { p.eqMudGainDb = float(v); });
    bind(m_presFreq, [] { return rng(1500, 6000); },
         [this](DspParams& p, double v) { p.eqPresenceFreqHz = float(v); });
    bind(m_presGain, [] { return rng(-6, 8); },
         [this](DspParams& p, double v) { p.eqPresenceGainDb = float(v); });
    bind(m_clarFreq, [] { return rng(3000, 12000); },
         [this](DspParams& p, double v) { p.eqClarityFreqHz = float(v); });
    bind(m_clarGain, [] { return rng(-6, 8); },
         [this](DspParams& p, double v) { p.eqClarityGainDb = float(v); });
    bind(m_air, [] { return rng(-6, 6); },
         [this](DspParams& p, double v) { p.eqAirGainDb = float(v); });

    connect(m_deessEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.deessEnabled = on; emit paramsEdited(m_params); });
    bind(m_deessFreq, [] { return rng(4000, 9000); },
         [this](DspParams& p, double v) { p.deessFreqHz = float(v); });
    bind(m_deessAmt, [] { return rng(0, 100); },
         [this](DspParams& p, double v) { p.deessAmount = float(v); });

    connect(m_compEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.compEnabled = on; emit paramsEdited(m_params); });
    bind(m_compThr, [] { return rng(-60, 0); },
         [this](DspParams& p, double v) { p.compThresholdDb = float(v); });
    bind(m_compRatio, [] { return rng(1, 20); },
         [this](DspParams& p, double v) { p.compRatio = float(v); });
    bind(m_compKnee, [] { return rng(0, 24); },
         [this](DspParams& p, double v) { p.compKneeDb = float(v); });
    bind(m_compAtk, [] { return rng(0.5, 100); },
         [this](DspParams& p, double v) { p.compAttackMs = float(v); });
    bind(m_compRel, [] { return rng(10, 1000); },
         [this](DspParams& p, double v) { p.compReleaseMs = float(v); });
    bind(m_compMakeup, [] { return rng(0, 24); },
         [this](DspParams& p, double v) { p.compMakeupDb = float(v); });

    connect(m_satEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.satEnabled = on; emit paramsEdited(m_params); });
    bind(m_satDrive, [] { return rng(0, 100); },
         [this](DspParams& p, double v) { p.satDrive = float(v); });

    connect(m_loudEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.loudEnabled = on; emit paramsEdited(m_params); });
    bind(m_loudTarget, [] { return rng(-24, -12); },
         [this](DspParams& p, double v) { p.loudTargetLufs = float(v); });
    bind(m_loudMaxGain, [] { return rng(0, 18); },
         [this](DspParams& p, double v) { p.loudMaxGainDb = float(v); });

    connect(m_limEn, &QCheckBox::toggled, this, [this](bool on) {
        m_params.limEnabled = on; emit paramsEdited(m_params); });
    bind(m_limCeil, [] { return rng(-12, 0); },
         [this](DspParams& p, double v) { p.limCeilingDb = float(v); });
    bind(m_limRel, [] { return rng(10, 500); },
         [this](DspParams& p, double v) { p.limReleaseMs = float(v); });

    bind(m_outGain, [] { return rng(-24, 24); },
         [this](DspParams& p, double v) { p.outputGainDb = float(v); });

    (void)sliderVal;
    setParams(m_params);
}

QSlider* MixerTab::addSlider(QGridLayout* g, int row, int col,
                             double min, double max, double step, double val)
{
    Q_UNUSED(step);
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(0, SLIDER_MAX);
    s->setValue(toSlider(val, min, max));
    g->addWidget(s, row, col);
    s->setProperty("min", min);
    s->setProperty("max", max);
    return s;
}

QLabel* MixerTab::addValueLabel(QGridLayout* g, int row, int col)
{
    auto* l = new QLabel(QStringLiteral("—"));
    l->setObjectName(QStringLiteral("mono"));
    l->setMinimumWidth(76);
    g->addWidget(l, row, col);
    return l;
}

template <typename Getter>
static void setSliderVal(QSlider* s, double v, Getter&& bounds)
{
    s->blockSignals(true);
    s->setValue(toSlider(v, bounds().first, bounds().second));
    s->blockSignals(false);
}

void MixerTab::setParams(const DspParams& p)
{
    m_updating = true;
    m_params = p;

    m_hpfEn->setChecked(p.hpfEnabled);
    setSliderVal(m_hpfFreq, p.hpfFreqHz, [] { return rng(20, 300); });
    m_nsEn->setChecked(p.nsEnabled);
    setSliderVal(m_nsAmount, p.nsAmount, [] { return rng(0, 100); });
    m_gateEn->setChecked(p.gateEnabled);
    setSliderVal(m_gateThr, p.gateThresholdDb, [] { return rng(-80, -20); });
    setSliderVal(m_gateRange, p.gateRangeDb, [] { return rng(-60, 0); });
    setSliderVal(m_gateHold, p.gateHoldMs, [] { return rng(10, 500); });
    m_eqEn->setChecked(p.eqEnabled);
    setSliderVal(m_warm, p.eqWarmGainDb, [] { return rng(-6, 6); });
    setSliderVal(m_mudFreq, p.eqMudFreqHz, [] { return rng(200, 500); });
    setSliderVal(m_mudGain, p.eqMudGainDb, [] { return rng(-9, 6); });
    setSliderVal(m_presFreq, p.eqPresenceFreqHz, [] { return rng(1500, 6000); });
    setSliderVal(m_presGain, p.eqPresenceGainDb, [] { return rng(-6, 8); });
    setSliderVal(m_clarFreq, p.eqClarityFreqHz, [] { return rng(3000, 12000); });
    setSliderVal(m_clarGain, p.eqClarityGainDb, [] { return rng(-6, 8); });
    setSliderVal(m_air, p.eqAirGainDb, [] { return rng(-6, 6); });
    m_deessEn->setChecked(p.deessEnabled);
    setSliderVal(m_deessFreq, p.deessFreqHz, [] { return rng(4000, 9000); });
    setSliderVal(m_deessAmt, p.deessAmount, [] { return rng(0, 100); });
    m_compEn->setChecked(p.compEnabled);
    setSliderVal(m_compThr, p.compThresholdDb, [] { return rng(-60, 0); });
    setSliderVal(m_compRatio, p.compRatio, [] { return rng(1, 20); });
    setSliderVal(m_compKnee, p.compKneeDb, [] { return rng(0, 24); });
    setSliderVal(m_compAtk, p.compAttackMs, [] { return rng(0.5, 100); });
    setSliderVal(m_compRel, p.compReleaseMs, [] { return rng(10, 1000); });
    setSliderVal(m_compMakeup, p.compMakeupDb, [] { return rng(0, 24); });
    m_satEn->setChecked(p.satEnabled);
    setSliderVal(m_satDrive, p.satDrive, [] { return rng(0, 100); });
    m_loudEn->setChecked(p.loudEnabled);
    setSliderVal(m_loudTarget, p.loudTargetLufs, [] { return rng(-24, -12); });
    setSliderVal(m_loudMaxGain, p.loudMaxGainDb, [] { return rng(0, 18); });
    m_limEn->setChecked(p.limEnabled);
    setSliderVal(m_limCeil, p.limCeilingDb, [] { return rng(-12, 0); });
    setSliderVal(m_limRel, p.limReleaseMs, [] { return rng(10, 500); });
    setSliderVal(m_outGain, p.outputGainDb, [] { return rng(-24, 24); });

    syncLabels();
    m_updating = false;
}

void MixerTab::syncLabels()
{
    auto* p = &m_params;
    m_hpfFreqV->setText(QStringLiteral("%1 Hz").arg(p->hpfFreqHz, 0, 'f', 0));
    m_nsAmountV->setText(QStringLiteral("%1 %").arg(p->nsAmount, 0, 'f', 0));
    m_gateThrV->setText(QStringLiteral("%1 dB").arg(p->gateThresholdDb, 0, 'f', 1));
    m_gateRangeV->setText(QStringLiteral("%1 dB").arg(p->gateRangeDb, 0, 'f', 1));
    m_gateHoldV->setText(QStringLiteral("%1 ms").arg(p->gateHoldMs, 0, 'f', 0));
    m_warmV->setText(QStringLiteral("%1 dB").arg(p->eqWarmGainDb, 0, 'f', 1));
    m_mudFreqV->setText(QStringLiteral("%1 Hz").arg(p->eqMudFreqHz, 0, 'f', 0));
    m_mudGainV->setText(QStringLiteral("%1 dB").arg(p->eqMudGainDb, 0, 'f', 1));
    m_presFreqV->setText(QStringLiteral("%1 Hz").arg(p->eqPresenceFreqHz, 0, 'f', 0));
    m_presGainV->setText(QStringLiteral("%1 dB").arg(p->eqPresenceGainDb, 0, 'f', 1));
    m_clarFreqV->setText(QStringLiteral("%1 Hz").arg(p->eqClarityFreqHz, 0, 'f', 0));
    m_clarGainV->setText(QStringLiteral("%1 dB").arg(p->eqClarityGainDb, 0, 'f', 1));
    m_airV->setText(QStringLiteral("%1 dB").arg(p->eqAirGainDb, 0, 'f', 1));
    m_deessFreqV->setText(QStringLiteral("%1 Hz").arg(p->deessFreqHz, 0, 'f', 0));
    m_deessAmtV->setText(QStringLiteral("%1 %").arg(p->deessAmount, 0, 'f', 0));
    m_compThrV->setText(QStringLiteral("%1 dB").arg(p->compThresholdDb, 0, 'f', 1));
    m_compRatioV->setText(QStringLiteral("%1 : 1").arg(p->compRatio, 0, 'f', 1));
    m_compKneeV->setText(QStringLiteral("%1 dB").arg(p->compKneeDb, 0, 'f', 1));
    m_compAtkV->setText(QStringLiteral("%1 ms").arg(p->compAttackMs, 0, 'f', 1));
    m_compRelV->setText(QStringLiteral("%1 ms").arg(p->compReleaseMs, 0, 'f', 0));
    m_compMakeupV->setText(QStringLiteral("%1 dB").arg(p->compMakeupDb, 0, 'f', 1));
    m_satDriveV->setText(QStringLiteral("%1 %").arg(p->satDrive, 0, 'f', 0));
    m_loudTargetV->setText(QStringLiteral("%1 LUFS").arg(p->loudTargetLufs, 0, 'f', 1));
    m_loudMaxGainV->setText(QStringLiteral("±%1 dB").arg(p->loudMaxGainDb, 0, 'f', 1));
    m_limCeilV->setText(QStringLiteral("%1 dBTP").arg(p->limCeilingDb, 0, 'f', 1));
    m_limRelV->setText(QStringLiteral("%1 ms").arg(p->limReleaseMs, 0, 'f', 0));
    m_outGainV->setText(QStringLiteral("%1 dB").arg(p->outputGainDb, 0, 'f', 1));
}

// called by MainWindow poll: live gain-reduction readouts
void MixerTab::updateMeters()
{
    Diagnostics& d = m_win->engine().diagnostics();
    m_compGr->setText(QStringLiteral("GR: %1 dB")
        .arg(d.compGainDb.load(), 0, 'f', 1));
    m_gateGr->setText(QStringLiteral("gate: %1 dB")
        .arg(d.gateGainDb.load(), 0, 'f', 1));
    m_loudGain->setText(QStringLiteral("applied: %1 dB")
        .arg(d.loudnessGainDb.load(), 0, 'f', 1));
}

} // namespace vc
