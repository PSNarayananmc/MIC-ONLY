#pragma once
#include <QWidget>
#include <QGridLayout>
#include "../dsp/DspParams.h"
#include "../core/Diagnostics.h"

class QLabel;
class QSlider;
class QCheckBox;
class QDoubleSpinBox;

namespace vc {

class MainWindow;

// Full-chain mixer: every DSP control in the requirement's signal order,
// each module with an enable switch. Changes publish to the engine via the
// seqlock and are smoothed on the audio side (no clicks while live).
class MixerTab : public QWidget {
    Q_OBJECT
public:
    explicit MixerTab(MainWindow* win, QWidget* parent = nullptr);

    void setParams(const DspParams& p);   // UI refresh (no signal loops)
    DspParams params() const { return m_params; }
    void updateMeters();                  // live GR readouts (called by poll)

signals:
    void paramsEdited(const vc::DspParams& p);

private:
    // helper builders keep this file readable
    QSlider* addSlider(QGridLayout* g, int row, int col, double min, double max,
                       double step, double val);
    QLabel* addValueLabel(QGridLayout* g, int row, int col);
    void syncLabels();

    MainWindow* m_win;
    DspParams m_params{};
    bool m_updating = false;

    // enables
    QCheckBox* m_hpfEn; QCheckBox* m_nsEn; QCheckBox* m_gateEn;
    QCheckBox* m_eqEn; QCheckBox* m_deessEn; QCheckBox* m_compEn;
    QCheckBox* m_satEn; QCheckBox* m_loudEn; QCheckBox* m_limEn;

    // sliders
    QSlider* m_hpfFreq; QSlider* m_nsAmount;
    QSlider* m_gateThr; QSlider* m_gateRange; QSlider* m_gateHold;
    QSlider* m_warm; QSlider* m_mudFreq; QSlider* m_mudGain;
    QSlider* m_presFreq; QSlider* m_presGain; QSlider* m_clarFreq;
    QSlider* m_clarGain; QSlider* m_air;
    QSlider* m_deessFreq; QSlider* m_deessAmt;
    QSlider* m_compThr; QSlider* m_compRatio; QSlider* m_compKnee;
    QSlider* m_compAtk; QSlider* m_compRel; QSlider* m_compMakeup;
    QSlider* m_satDrive;
    QSlider* m_loudTarget; QSlider* m_loudMaxGain;
    QSlider* m_limCeil; QSlider* m_limRel;
    QSlider* m_outGain;

    // value labels + GR displays
    QLabel* m_hpfFreqV; QLabel* m_nsAmountV;
    QLabel* m_gateThrV; QLabel* m_gateRangeV; QLabel* m_gateHoldV;
    QLabel* m_warmV; QLabel* m_mudFreqV; QLabel* m_mudGainV;
    QLabel* m_presFreqV; QLabel* m_presGainV; QLabel* m_clarFreqV;
    QLabel* m_clarGainV; QLabel* m_airV;
    QLabel* m_deessFreqV; QLabel* m_deessAmtV;
    QLabel* m_compThrV; QLabel* m_compRatioV; QLabel* m_compKneeV;
    QLabel* m_compAtkV; QLabel* m_compRelV; QLabel* m_compMakeupV;
    QLabel* m_satDriveV;
    QLabel* m_loudTargetV; QLabel* m_loudMaxGainV;
    QLabel* m_limCeilV; QLabel* m_limRelV;
    QLabel* m_outGainV;
    QLabel* m_compGr; QLabel* m_gateGr; QLabel* m_loudGain;
};

} // namespace vc
