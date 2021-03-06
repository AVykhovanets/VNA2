#include "signalgenwidget.h"
#include "ui_signalgenwidget.h"

SignalgeneratorWidget::SignalgeneratorWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SignalgeneratorWidget)
{
    ui->setupUi(this);
    ui->frequency->setUnit("Hz");
    ui->frequency->setPrefixes(" kMG");

    connect(ui->frequency, &SIUnitEdit::valueChanged, [=](double newval) {
       if(newval < Device::Limits().minFreq) {
           newval = Device::Limits().minFreq;
       } else if (newval > Device::Limits().maxFreq) {
           newval = Device::Limits().maxFreq;
       }
       ui->frequency->setValueQuiet(newval);
       emit SettingsChanged();
    });
    connect(ui->levelSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SignalgeneratorWidget::setLevel);
    connect(ui->levelSlider, &QSlider::valueChanged, [=](int value) {
        setLevel((double) value / 100.0);
    });
    connect(ui->EnablePort1, &QCheckBox::clicked, [=](){
        if(ui->EnablePort1->isChecked() && ui->EnablePort2->isChecked()) {
           ui->EnablePort2->setCheckState(Qt::CheckState::Unchecked);
        }
        emit SettingsChanged();
    });
    connect(ui->EnablePort2, &QCheckBox::clicked, [=](){
        if(ui->EnablePort1->isChecked() && ui->EnablePort2->isChecked()) {
           ui->EnablePort1->setCheckState(Qt::CheckState::Unchecked);
        }
        emit SettingsChanged();
    });
}

SignalgeneratorWidget::~SignalgeneratorWidget()
{
    delete ui;
}

Protocol::GeneratorSettings SignalgeneratorWidget::getDeviceStatus()
{
    Protocol::GeneratorSettings s = {};
    s.frequency = ui->frequency->value();
    s.cdbm_level = ui->levelSpin->value() * 100.0;
    if(ui->EnablePort1->isChecked()) {
        s.activePort = 1;
    } else if(ui->EnablePort2->isChecked()) {
        s.activePort = 2;
    } else {
        s.activePort = 0;
    }
    return s;
}

void SignalgeneratorWidget::setLevel(double level)
{
    // TODO constrain to frequency dependent levels
    ui->levelSpin->blockSignals(true);
    ui->levelSlider->blockSignals(true);
    ui->levelSpin->setValue(level);
    ui->levelSlider->setValue(level * 100.0);
    ui->levelSpin->blockSignals(false);
    ui->levelSlider->blockSignals(false);
    SettingsChanged();
}

void SignalgeneratorWidget::setFrequency(double frequency)
{
    ui->frequency->setValue(frequency);
}

