#include "vna.h"
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <math.h>
#include <QToolBar>
#include <QMenu>
#include <QToolButton>
#include <QActionGroup>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <algorithm>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <iostream>
#include <fstream>
#include <QDateTime>
#include "unit.h"
#include <queue>
#include "CustomWidgets/toggleswitch.h"
#include "Device/manualcontroldialog.h"
#include "Traces/tracemodel.h"
#include "Traces/tracewidget.h"
#include "Traces/tracesmithchart.h"
#include "Traces/tracexyplot.h"
#include "Traces/traceimportdialog.h"
#include "CustomWidgets/tilewidget.h"
#include "CustomWidgets/siunitedit.h"
#include <QDockWidget>
#include "Traces/markerwidget.h"
#include "Tools/impedancematchdialog.h"
#include "Calibration/calibrationtracedialog.h"
#include "ui_main.h"
#include "Device/firmwareupdatedialog.h"
#include "preferences.h"
#include "Generator/signalgenwidget.h"
#include <QDesktopWidget>
#include <QApplication>
#include <QActionGroup>

VNA::VNA(AppWindow *window)
    : Mode(window, "Vector Network Analyzer"),
      central(new TileWidget(traceModel))
{
    averages = 1;
    calValid = false;
    calMeasuring = false;
    calDialog.reset();

    // Create default traces
    auto tS11 = new Trace("S11", Qt::yellow);
    tS11->fromLivedata(Trace::LivedataType::Overwrite, Trace::LiveParameter::S11);
    traceModel.addTrace(tS11);
    auto tS12 = new Trace("S12", Qt::blue);
    tS12->fromLivedata(Trace::LivedataType::Overwrite, Trace::LiveParameter::S12);
    traceModel.addTrace(tS12);
    auto tS21 = new Trace("S21", Qt::green);
    tS21->fromLivedata(Trace::LivedataType::Overwrite, Trace::LiveParameter::S21);
    traceModel.addTrace(tS21);
    auto tS22 = new Trace("S22", Qt::red);
    tS22->fromLivedata(Trace::LivedataType::Overwrite, Trace::LiveParameter::S22);
    traceModel.addTrace(tS22);

    auto tracesmith1 = new TraceSmithChart(traceModel);
    tracesmith1->enableTrace(tS11, true);
    auto tracesmith2 = new TraceSmithChart(traceModel);
    tracesmith2->enableTrace(tS22, true);

    auto traceXY1 = new TraceXYPlot(traceModel);
    traceXY1->enableTrace(tS12, true);
    auto traceXY2 = new TraceXYPlot(traceModel);
    traceXY2->enableTrace(tS21, true);

    connect(&traceModel, &TraceModel::requiredExcitation, this, &VNA::ExcitationRequired);

    central->splitVertically();
    central->Child1()->splitHorizontally();
    central->Child2()->splitHorizontally();
    central->Child1()->Child1()->setPlot(tracesmith1);
    central->Child1()->Child2()->setPlot(traceXY1);
    central->Child2()->Child1()->setPlot(traceXY2);
    central->Child2()->Child2()->setPlot(tracesmith2);

    // Create menu entries and connections
    auto calMenu = new QMenu("Calibration");
    window->menuBar()->insertMenu(window->getUi()->menuWindow->menuAction(), calMenu);
    actions.insert(calMenu->menuAction());
    auto calDisable = calMenu->addAction("Disabled");
    calDisable->setCheckable(true);
    calDisable->setChecked(true);
    calMenu->addSeparator();
    auto calData = calMenu->addAction("Calibration Data");
    connect(calData, &QAction::triggered, [=](){
       StartCalibrationDialog();
    });

    auto calImport = calMenu->addAction("Import error terms as traces");
    calImport->setEnabled(false);
    connect(calImport, &QAction::triggered, [=](){
        auto import = new TraceImportDialog(traceModel, cal.getErrorTermTraces());
        import->show();
    });

    auto calEditKit = calMenu->addAction("Edit Calibration Kit");
    connect(calEditKit, &QAction::triggered, [=](){
        cal.getCalibrationKit().edit();
    });

    // Tools menu
    auto toolsMenu = new QMenu("Tools");
    window->menuBar()->insertMenu(window->getUi()->menuWindow->menuAction(), toolsMenu);
    actions.insert(toolsMenu->menuAction());
    auto impedanceMatching = toolsMenu->addAction("Impedance Matching");
    connect(impedanceMatching, &QAction::triggered, this, &VNA::StartImpedanceMatching);

    defaultCalMenu = new QMenu("Default Calibration");
    assignDefaultCal = defaultCalMenu->addAction("Assign...");
    removeDefaultCal = defaultCalMenu->addAction("Remove");
    removeDefaultCal->setEnabled(false);
    defaultCalMenu->setEnabled(false);

    actions.insert(window->getUi()->menuDevice->addSeparator());
    window->getUi()->menuDevice->addMenu(defaultCalMenu);
    actions.insert(defaultCalMenu->menuAction());

    connect(assignDefaultCal, &QAction::triggered, [=](){
       if(window->getDevice()) {
           auto key = "DefaultCalibration"+window->getDevice()->serial();
           QSettings settings;
           auto filename = QFileDialog::getOpenFileName(nullptr, "Load calibration data", settings.value(key).toString(), "Calibration files (*.cal)", nullptr, QFileDialog::DontUseNativeDialog);
           if(!filename.isEmpty()) {
               settings.setValue(key, filename);
               removeDefaultCal->setEnabled(true);
           }
       }
    });
    connect(removeDefaultCal, &QAction::triggered, [=](){
        QSettings settings;
        settings.remove("DefaultCalibration"+window->getDevice()->serial());
        removeDefaultCal->setEnabled(false);
    });


    // Sweep toolbar
    auto tb_sweep = new QToolBar("Sweep");
    auto eStart = new SIUnitEdit("Hz", " kMG", 6);
    eStart->setFixedWidth(100);
    eStart->setToolTip("Start frequency");
    connect(eStart, &SIUnitEdit::valueChanged, this, &VNA::SetStartFreq);
    connect(this, &VNA::startFreqChanged, eStart, &SIUnitEdit::setValueQuiet);
    tb_sweep->addWidget(new QLabel("Start:"));
    tb_sweep->addWidget(eStart);

    auto eCenter = new SIUnitEdit("Hz", " kMG", 6);
    eCenter->setFixedWidth(100);
    eCenter->setToolTip("Center frequency");
    connect(eCenter, &SIUnitEdit::valueChanged, this, &VNA::SetCenterFreq);
    connect(this, &VNA::centerFreqChanged, eCenter, &SIUnitEdit::setValueQuiet);
    tb_sweep->addWidget(new QLabel("Center:"));
    tb_sweep->addWidget(eCenter);

    auto eStop = new SIUnitEdit("Hz", " kMG", 6);
    eStop->setFixedWidth(100);
    eStop->setToolTip("Stop frequency");
    connect(eStop, &SIUnitEdit::valueChanged, this, &VNA::SetStopFreq);
    connect(this, &VNA::stopFreqChanged, eStop, &SIUnitEdit::setValueQuiet);
    tb_sweep->addWidget(new QLabel("Stop:"));
    tb_sweep->addWidget(eStop);

    auto eSpan = new SIUnitEdit("Hz", " kMG", 6);
    eSpan->setFixedWidth(100);
    eSpan->setToolTip("Span");
    connect(eSpan, &SIUnitEdit::valueChanged, this, &VNA::SetSpan);
    connect(this, &VNA::spanChanged, eSpan, &SIUnitEdit::setValueQuiet);
    tb_sweep->addWidget(new QLabel("Span:"));
    tb_sweep->addWidget(eSpan);

    auto bFull = new QPushButton(QIcon::fromTheme("zoom-fit-best", QIcon(":/icons/zoom-fit.png")), "");
    bFull->setToolTip("Full span");
    connect(bFull, &QPushButton::clicked, this, &VNA::SetFullSpan);
    tb_sweep->addWidget(bFull);

    auto bZoomIn = new QPushButton(QIcon::fromTheme("zoom-in", QIcon(":/icons/zoom-in.png")), "");
    bZoomIn->setToolTip("Zoom in");
    connect(bZoomIn, &QPushButton::clicked, this, &VNA::SpanZoomIn);
    tb_sweep->addWidget(bZoomIn);

    auto bZoomOut = new QPushButton(QIcon::fromTheme("zoom-out", QIcon(":/icons/zoom-out.png")), "");
    bZoomOut->setToolTip("Zoom out");
    connect(bZoomOut, &QPushButton::clicked, this, &VNA::SpanZoomOut);
    tb_sweep->addWidget(bZoomOut);

    window->addToolBar(tb_sweep);
    toolbars.insert(tb_sweep);

    // Acquisition toolbar
    auto tb_acq = new QToolBar("Acquisition");
    auto dbm = new QDoubleSpinBox();
    dbm->setValue(settings.cdbm_excitation * 100);
    dbm->setFixedWidth(95);
    dbm->setRange(-100.0, 100.0);
    dbm->setSingleStep(0.25);
    dbm->setSuffix("dbm");
    dbm->setToolTip("Stimulus level");
    connect(dbm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VNA::SetSourceLevel);
    connect(this, &VNA::sourceLevelChanged, dbm, &QDoubleSpinBox::setValue);
    tb_acq->addWidget(new QLabel("Level:"));
    tb_acq->addWidget(dbm);

    auto points = new QSpinBox();
    points->setFixedWidth(55);
    points->setRange(1, 4501);
    points->setValue(settings.points);
    points->setSingleStep(100);
    points->setToolTip("Points/sweep");
    connect(points, qOverload<int>(&QSpinBox::valueChanged), this, &VNA::SetPoints);
    connect(this, &VNA::pointsChanged, points, &QSpinBox::setValue);
    tb_acq->addWidget(new QLabel("Points:"));
    tb_acq->addWidget(points);

    auto eBandwidth = new SIUnitEdit("Hz", " k", 3);
    eBandwidth->setValueQuiet(settings.if_bandwidth);
    eBandwidth->setFixedWidth(70);
    eBandwidth->setToolTip("IF bandwidth");
    connect(eBandwidth, &SIUnitEdit::valueChanged, this, &VNA::SetIFBandwidth);
    connect(this, &VNA::IFBandwidthChanged, eBandwidth, &SIUnitEdit::setValueQuiet);
    tb_acq->addWidget(new QLabel("IF BW:"));
    tb_acq->addWidget(eBandwidth);

    tb_acq->addWidget(new QLabel("Averaging:"));
    lAverages = new QLabel("0/");
    tb_acq->addWidget(lAverages);
    auto sbAverages = new QSpinBox;
    sbAverages->setRange(1, 99);
    sbAverages->setFixedWidth(40);
    connect(sbAverages, qOverload<int>(&QSpinBox::valueChanged), this, &VNA::SetAveraging);
    connect(this, &VNA::averagingChanged, sbAverages, &QSpinBox::setValue);
    tb_acq->addWidget(sbAverages);

    window->addToolBar(tb_acq);
    toolbars.insert(tb_acq);

    // Calibration toolbar (and populate calibration menu)
    auto tb_cal = new QToolBar("Calibration");
    tb_cal->addWidget(new QLabel("Calibration:"));
    auto cbEnableCal = new QCheckBox;
    tb_cal->addWidget(cbEnableCal);
    auto cbType = new QComboBox();
    auto calMenuGroup = new QActionGroup(this);
    calMenuGroup->addAction(calDisable);
    for(auto type : Calibration::Types()) {
        cbType->addItem(Calibration::TypeToString(type), (int) type);
        auto menuAction = new QAction(Calibration::TypeToString(type));
        calMenuGroup->addAction(menuAction);
        connect(menuAction, &QAction::triggered, [=](){
            ApplyCalibration(type);
        });
        connect(this, &VNA::CalibrationApplied, [=](Calibration::Type applied){
             if(type == applied) {
                 menuAction->setChecked(true);
             }
        });
        menuAction->setCheckable(true);
        calMenu->insertAction(calDisable, menuAction);
    }

    auto calToolbarLambda = [=]() {
        if(cbEnableCal->isChecked()) {
            // Get requested calibration type from combobox
            ApplyCalibration((Calibration::Type) cbType->itemData(cbType->currentIndex()).toInt());
        } else {
            DisableCalibration();
        }
    };

    // Calibration connections
    connect(cbEnableCal, &QCheckBox::stateChanged, calToolbarLambda);
    connect(cbType, qOverload<int>(&QComboBox::currentIndexChanged), calToolbarLambda);
    connect(this, &VNA::CalibrationDisabled, [=](){
        cbType->blockSignals(true);
        cbEnableCal->blockSignals(true);
        calDisable->setChecked(true);
        cbEnableCal->setCheckState(Qt::CheckState::Unchecked);
        cbType->blockSignals(false);
        cbEnableCal->blockSignals(false);
        calImport->setEnabled(false);
    });
    connect(calDisable, &QAction::triggered, this, &VNA::DisableCalibration);
    connect(this, &VNA::CalibrationApplied, [=](Calibration::Type applied){
        cbType->blockSignals(true);
        cbEnableCal->blockSignals(true);
        for(int i=0;i<cbType->count();i++) {
            if(cbType->itemData(i).toInt() == (int) applied) {
                cbType->setCurrentIndex(i);
                break;
            }
        }
        cbEnableCal->setCheckState(Qt::CheckState::Checked);
        cbType->blockSignals(false);
        cbEnableCal->blockSignals(false);
        calImport->setEnabled(true);
    });

    tb_cal->addWidget(cbType);

    window->addToolBar(tb_cal);
    toolbars.insert(tb_cal);


    markerModel = new TraceMarkerModel(traceModel);

    auto tracesDock = new QDockWidget("Traces");
    tracesDock->setWidget(new TraceWidget(traceModel));
    window->addDockWidget(Qt::LeftDockWidgetArea, tracesDock);
    docks.insert(tracesDock);


    auto markerWidget = new MarkerWidget(*markerModel);

    auto markerDock = new QDockWidget("Marker");
    markerDock->setWidget(markerWidget);
    window->addDockWidget(Qt::BottomDockWidgetArea, markerDock);
    docks.insert(markerDock);

    qRegisterMetaType<Protocol::Datapoint>("Datapoint");

    // Set initial sweep settings
    auto pref = Preferences::getInstance();
    if(pref.Acquisition.alwaysExciteBothPorts) {
        settings.excitePort1 = 1;
        settings.excitePort2 = 1;
    } else {
        settings.excitePort1 = traceModel.PortExcitationRequired(1);
        settings.excitePort2 = traceModel.PortExcitationRequired(2);
    }
    if(pref.Startup.RememberSweepSettings) {
        LoadSweepSettings();
    } else {
        settings.f_start = pref.Startup.DefaultSweep.start;
        settings.f_stop = pref.Startup.DefaultSweep.stop;
        ConstrainAndUpdateFrequencies();
        SetSourceLevel(pref.Startup.DefaultSweep.excitation);
        SetIFBandwidth(pref.Startup.DefaultSweep.bandwidth);
        SetAveraging(pref.Startup.DefaultSweep.averaging);
        SetPoints(pref.Startup.DefaultSweep.points);
    }

    // Set ObjectName for toolbars and docks
    for(auto d : findChildren<QDockWidget*>()) {
        d->setObjectName(d->windowTitle());
    }
    for(auto t : findChildren<QToolBar*>()) {
        t->setObjectName(t->windowTitle());
    }

    finalize(central);
}

void VNA::deactivate()
{
    StoreSweepSettings();
    Mode::deactivate();
}

void VNA::initializeDevice()
{
    defaultCalMenu->setEnabled(true);
    connect(window->getDevice(), &Device::DatapointReceived, this, &VNA::NewDatapoint, Qt::UniqueConnection);
    // Check if default calibration exists and attempt to load it
    QSettings s;
    auto key = "DefaultCalibration"+window->getDevice()->serial();
    if (s.contains(key)) {
        auto filename = s.value(key).toString();
        qDebug() << "Attempting to load default calibration file \"" << filename << "\"";
        if(QFile::exists(filename)) {
            cal.openFromFile(filename);
            ApplyCalibration(cal.getType());
        }
        removeDefaultCal->setEnabled(true);
    } else {
        qDebug() << "No default calibration file set for this device";
        removeDefaultCal->setEnabled(false);
    }
    // Configure initial state of device
    SettingsChanged();
}

void VNA::deviceDisconnected()
{
    defaultCalMenu->setEnabled(false);
}

using namespace std;

void VNA::NewDatapoint(Protocol::Datapoint d)
{
    d = average.process(d);
    if(calMeasuring) {

        if(average.currentSweep() == averages) {
            // this is the last averaging sweep, use values for calibration
            if(!calWaitFirst || d.pointNum == 0) {
                calWaitFirst = false;
                cal.addMeasurement(calMeasurement, d);
                if(d.pointNum == settings.points - 1) {
                    calMeasuring = false;
                    emit CalibrationMeasurementComplete(calMeasurement);
                }
            }
        }
        int percentage = (((average.currentSweep() - 1) * 100) + (d.pointNum + 1) * 100 / settings.points) / averages;
        calDialog.setValue(percentage);
    }
    if(calValid) {
        cal.correctMeasurement(d);
    }
    traceModel.addVNAData(d);
    emit dataChanged();
    if(d.pointNum == settings.points - 1) {
        UpdateAverageCount();
        markerModel->updateMarkers();
    }
}

void VNA::UpdateAverageCount()
{
    lAverages->setText(QString::number(average.getLevel()) + "/");
}

void VNA::SettingsChanged(std::function<void (Device::TransmissionResult)> cb)
{
    settings.suppressPeaks = Preferences::getInstance().Acquisition.suppressPeaks ? 1 : 0;
    if(window->getDevice()) {
        window->getDevice()->Configure(settings, cb);
    }
    average.reset(settings.points);
    traceModel.clearVNAData();
    UpdateAverageCount();
    emit traceModel.SpanChanged(settings.f_start, settings.f_stop);
}

void VNA::StartImpedanceMatching()
{
    auto dialog = new ImpedanceMatchDialog(*markerModel);
    dialog->show();
}

void VNA::SetStartFreq(double freq)
{
    settings.f_start = freq;
    if(settings.f_stop < freq) {
        settings.f_stop = freq;
    }
    ConstrainAndUpdateFrequencies();
}

void VNA::SetStopFreq(double freq)
{
    settings.f_stop = freq;
    if(settings.f_start > freq) {
        settings.f_start = freq;
    }
    ConstrainAndUpdateFrequencies();
}

void VNA::SetCenterFreq(double freq)
{
    auto old_span = settings.f_stop - settings.f_start;
    if (freq > old_span / 2) {
        settings.f_start = freq - old_span / 2;
        settings.f_stop = freq + old_span / 2;
    } else {
        settings.f_start = 0;
        settings.f_stop = 2 * freq;
    }
    ConstrainAndUpdateFrequencies();
}

void VNA::SetSpan(double span)
{
    auto old_center = (settings.f_start + settings.f_stop) / 2;
    if(old_center > span / 2) {
        settings.f_start = old_center - span / 2;
    } else {
        settings.f_start = 0;
    }
    settings.f_stop = old_center + span / 2;
    ConstrainAndUpdateFrequencies();
}

void VNA::SetFullSpan()
{
    settings.f_start = Device::Limits().minFreq;
    settings.f_stop = Device::Limits().maxFreq;
    ConstrainAndUpdateFrequencies();
}

void VNA::SpanZoomIn()
{
    auto center = (settings.f_start + settings.f_stop) / 2;
    auto old_span = settings.f_stop - settings.f_start;
    settings.f_start = center - old_span / 4;
    settings.f_stop = center + old_span / 4;
    ConstrainAndUpdateFrequencies();
}

void VNA::SpanZoomOut()
{
    auto center = (settings.f_start + settings.f_stop) / 2;
    auto old_span = settings.f_stop - settings.f_start;
    if(center > old_span) {
        settings.f_start = center - old_span;
    } else {
        settings.f_start = 0;
    }
    settings.f_stop = center + old_span;
    ConstrainAndUpdateFrequencies();
}

void VNA::SetSourceLevel(double level)
{
    // TODO remove hardcoded limits
    if(level > Device::Limits().cdbm_max / 100.0) {
        level = Device::Limits().cdbm_max / 100.0;
    } else if(level < Device::Limits().cdbm_min / 100.0) {
        level = Device::Limits().cdbm_min / 100.0;
    }
    emit sourceLevelChanged(level);
    settings.cdbm_excitation = level * 100;
    SettingsChanged();
}

void VNA::SetPoints(unsigned int points)
{
    // TODO remove hardcoded limits
    if (points < 1) {
        points = 1;
    } else if(points > Device::Limits().maxPoints) {
        points = Device::Limits().maxPoints;
    }
    emit pointsChanged(points);
    settings.points = points;
    SettingsChanged();
}

void VNA::SetIFBandwidth(double bandwidth)
{
    if(bandwidth > Device::Limits().maxIFBW) {
        bandwidth = Device::Limits().maxIFBW;
    } else if(bandwidth < Device::Limits().minIFBW) {
        bandwidth = Device::Limits().minIFBW;
    }
    settings.if_bandwidth = bandwidth;
    emit IFBandwidthChanged(bandwidth);
    SettingsChanged();
}

void VNA::SetAveraging(unsigned int averages)
{
    this->averages = averages;
    average.setAverages(averages);
    emit averagingChanged(averages);
    SettingsChanged();
}

void VNA::ExcitationRequired(bool port1, bool port2)
{
    if(Preferences::getInstance().Acquisition.alwaysExciteBothPorts) {
        port1 = true;
        port2 = true;
    }
    // check if settings actually changed
    if(settings.excitePort1 != port1
        || settings.excitePort2 != port2) {
        settings.excitePort1 = port1;
        settings.excitePort2 = port2;
        SettingsChanged();
    }
}

void VNA::DisableCalibration(bool force)
{
    if(calValid || force) {
        calValid = false;
        emit CalibrationDisabled();
        average.reset(settings.points);
    }
}

void VNA::ApplyCalibration(Calibration::Type type)
{
    if(cal.calculationPossible(type)) {
        try {
            if(cal.constructErrorTerms(type)) {
                calValid = true;
                average.reset(settings.points);
                emit CalibrationApplied(type);
            }
        } catch (runtime_error e) {
            QMessageBox::critical(this, "Calibration failure", e.what());
            DisableCalibration(true);
        }
    } else {
        // Not all required traces available
        // TODO start tracedata dialog with required traces
        QMessageBox::information(this, "Missing calibration traces", "Not all calibration traces for this type of calibration have been measured. The calibration can be enabled after the missing traces have been acquired.");
        DisableCalibration(true);
        StartCalibrationDialog(type);
    }
}

void VNA::StartCalibrationMeasurement(Calibration::Measurement m)
{
    auto device = window->getDevice();
    if(!device) {
        return;
    }
    // Stop sweep
    StopSweep();
     calMeasurement = m;
    // Delete any already captured data of this measurement
    cal.clearMeasurement(m);
    calWaitFirst = true;
    QString text = "Measuring \"";
    text.append(Calibration::MeasurementToString(m));
    text.append("\" parameters.");
    calDialog.setLabelText(text);
    calDialog.setCancelButtonText("Abort");
    calDialog.setWindowTitle("Taking calibration measurement...");
    calDialog.setValue(0);
    calDialog.setWindowModality(Qt::ApplicationModal);
    // always show the dialog
    calDialog.setMinimumDuration(0);
    connect(&calDialog, &QProgressDialog::canceled, [=]() {
        // the user aborted the calibration measurement
        calMeasuring = false;
        cal.clearMeasurement(calMeasurement);
    });
    // Trigger sweep to start from beginning
    SettingsChanged([=](Device::TransmissionResult){
        // enable calibration measurement only in transmission callback (prevents accidental sampling of data which was still being processed)
        calMeasuring = true;
    });
}

void VNA::ConstrainAndUpdateFrequencies()
{
    if(settings.f_stop > Device::Limits().maxFreq) {
        settings.f_stop = Device::Limits().maxFreq;
    }
    if(settings.f_start > settings.f_stop) {
        settings.f_start = settings.f_stop;
    }
    if(settings.f_start < Device::Limits().minFreq) {
        settings.f_start = Device::Limits().minFreq;
    }
    emit startFreqChanged(settings.f_start);
    emit stopFreqChanged(settings.f_stop);
    emit spanChanged(settings.f_stop - settings.f_start);
    emit centerFreqChanged((settings.f_stop + settings.f_start)/2);
    SettingsChanged();
}

void VNA::LoadSweepSettings()
{
    auto pref = Preferences::getInstance();
    QSettings s;
    settings.f_start = s.value("SweepStart", pref.Startup.DefaultSweep.start).toULongLong();
    settings.f_stop = s.value("SweepStop", pref.Startup.DefaultSweep.stop).toULongLong();
    ConstrainAndUpdateFrequencies();
    SetIFBandwidth(s.value("SweepBandwidth", pref.Startup.DefaultSweep.bandwidth).toUInt());
    SetPoints(s.value("SweepPoints", pref.Startup.DefaultSweep.points).toInt());
    SetAveraging(s.value("SweepAveraging", pref.Startup.DefaultSweep.averaging).toInt());
    SetSourceLevel(s.value("SweepLevel", pref.Startup.DefaultSweep.excitation).toDouble());
}

void VNA::StoreSweepSettings()
{
    QSettings s;
    s.setValue("SweepStart", static_cast<unsigned long long>(settings.f_start));
    s.setValue("SweepStop", static_cast<unsigned long long>(settings.f_stop));
    s.setValue("SweepBandwidth", settings.if_bandwidth);
    s.setValue("SweepPoints", settings.points);
    s.setValue("SweepAveraging", averages);
    s.setValue("SweepLevel", (double) settings.cdbm_excitation / 100.0);
}

void VNA::StopSweep()
{
    if(window->getDevice()) {
        window->getDevice()->SetIdle();
    }
}

void VNA::StartCalibrationDialog(Calibration::Type type)
{
    auto traceDialog = new CalibrationTraceDialog(&cal, type);
    connect(traceDialog, &CalibrationTraceDialog::triggerMeasurement, this, &VNA::StartCalibrationMeasurement);
    connect(traceDialog, &CalibrationTraceDialog::applyCalibration, this, &VNA::ApplyCalibration);
    connect(this, &VNA::CalibrationMeasurementComplete, traceDialog, &CalibrationTraceDialog::measurementComplete);
    traceDialog->show();
}
