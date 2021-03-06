#include "tracexyplot.h"
#include <QGridLayout>
#include "qwtplotpiecewisecurve.h"
#include "qwt_series_data.h"
#include "trace.h"
#include <cmath>
#include <QFrame>
#include <qwt_plot_canvas.h>
#include <qwt_scale_div.h>
#include <qwt_plot_layout.h>
#include "tracemarker.h"
#include <qwt_symbol.h>
#include <qwt_picker_machine.h>
#include "xyplotaxisdialog.h"
#include <preferences.h>

using namespace std;

set<TraceXYPlot*> TraceXYPlot::allPlots;

const set<TraceXYPlot::YAxisType> TraceXYPlot::YAxisTypes = {TraceXYPlot::YAxisType::Disabled,
                                           TraceXYPlot::YAxisType::Magnitude,
                                           TraceXYPlot::YAxisType::Phase,
                                           TraceXYPlot::YAxisType::VSWR,
                                           TraceXYPlot::YAxisType::Impulse,
                                           TraceXYPlot::YAxisType::Step,
                                           TraceXYPlot::YAxisType::Impedance};

static double FrequencyAxisTransformation(TraceXYPlot::YAxisType type, complex<double> data) {
    switch(type) {
    case TraceXYPlot::YAxisType::Magnitude: return 20*log10(abs(data)); break;
    case TraceXYPlot::YAxisType::Phase: return arg(data) * 180.0 / M_PI; break;
    case TraceXYPlot::YAxisType::VSWR:
        if(abs(data) < 1.0) {
            return (1+abs(data)) / (1-abs(data));
        }
        break;
    default: break;
    }
    return numeric_limits<double>::quiet_NaN();
}
static double TimeAxisTransformation(TraceXYPlot::YAxisType type, Trace *t, int index) {
    auto timeData = t->getTDR()[index];
    switch(type) {
    case TraceXYPlot::YAxisType::Impulse: return timeData.impulseResponse; break;
    case TraceXYPlot::YAxisType::Step: return timeData.stepResponse; break;
    case TraceXYPlot::YAxisType::Impedance:
        if(abs(timeData.stepResponse) < 1.0) {
            return 50 * (1+timeData.stepResponse) / (1-timeData.stepResponse);
        }
        break;
    default: break;
    }
    return numeric_limits<double>::quiet_NaN();
}

class QwtTraceSeries : public QwtSeriesData<QPointF> {
public:
    QwtTraceSeries(Trace &t, TraceXYPlot::YAxisType Ytype, TraceXYPlot::XAxisType Xtype)
        : QwtSeriesData<QPointF>(),
          Ytype(Ytype),
          Xtype(Xtype),
          t(t){}
    size_t size() const override {
        switch(Ytype) {
        case TraceXYPlot::YAxisType::Magnitude:
        case TraceXYPlot::YAxisType::Phase:
        case TraceXYPlot::YAxisType::VSWR:
            return t.size();
        case TraceXYPlot::YAxisType::Impulse:
        case TraceXYPlot::YAxisType::Step:
        case TraceXYPlot::YAxisType::Impedance:
            return t.getTDR().size();
        default:
            return 0;
        }
    }
    QPointF sample(size_t i) const override {
        switch(Ytype) {
        case TraceXYPlot::YAxisType::Magnitude:
        case TraceXYPlot::YAxisType::Phase:
        case TraceXYPlot::YAxisType::VSWR: {
            Trace::Data d = t.sample(i);
            QPointF p;
            p.setX(d.frequency);
            p.setY(FrequencyAxisTransformation(Ytype, d.S));
            return p;
        }
        case TraceXYPlot::YAxisType::Impulse:
        case TraceXYPlot::YAxisType::Step:
        case TraceXYPlot::YAxisType::Impedance: {
            auto sample = t.getTDR()[i];
            QPointF p;
            // TODO set distance
            p.setX(sample.time);
            p.setY(TimeAxisTransformation(Ytype, &t, i));
            return p;
        }
        default:
            return QPointF();
        }

    }
    QRectF boundingRect() const override {
        return qwtBoundingRect(*this);
    }

private:
    TraceXYPlot::YAxisType Ytype;
    TraceXYPlot::XAxisType Xtype;
    Trace &t;
};

TraceXYPlot::TraceXYPlot(TraceModel &model, QWidget *parent)
    : TracePlot(parent),
      selectedMarker(nullptr)
{
    plot = new QwtPlot(this);

    auto canvas = new QwtPlotCanvas(plot);
    canvas->setFrameStyle(QFrame::Plain);
    plot->setCanvas(canvas);
    plot->setAutoFillBackground(true);
    grid = new QwtPlotGrid();
    grid->attach(plot);
    setColorFromPreferences();

    auto selectPicker = new XYplotPicker(plot->xBottom, plot->yLeft, QwtPicker::NoRubberBand, QwtPicker::ActiveOnly, plot->canvas());
    selectPicker->setStateMachine(new QwtPickerClickPointMachine);

    drawPicker = new XYplotPicker(plot->xBottom, plot->yLeft, QwtPicker::NoRubberBand, QwtPicker::ActiveOnly, plot->canvas());
    drawPicker->setStateMachine(new QwtPickerDragPointMachine);
    drawPicker->setTrackerPen(QPen(Qt::white));

    // Marker selection
    connect(selectPicker, SIGNAL(selected(QPointF)), this, SLOT(clicked(QPointF)));;
    // Marker movement
    connect(drawPicker, SIGNAL(moved(QPointF)), this, SLOT(moved(QPointF)));

    auto layout = new QGridLayout;
    layout->addWidget(plot);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
    plot->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    initializeTraceInfo(model);
    setAutoFillBackground(true);

    // Setup default axis
    setYAxis(0, YAxisType::Magnitude, false, false, -120, 20, 10);
    setYAxis(1, YAxisType::Phase, false, false, -180, 180, 30);
    // enable autoscaling and set for full span (no information about actual span available yet)
    setXAxis(0, 6000000000);
    setXAxis(XAxisType::Frequency, true, 0, 6000000000, 600000000);
    // get notified when the span changes
    connect(&model, &TraceModel::SpanChanged, this, qOverload<double, double>(&TraceXYPlot::setXAxis));

    allPlots.insert(this);
}

TraceXYPlot::~TraceXYPlot()
{
    for(int axis = 0;axis < 2;axis++) {
        for(auto pd : curves[axis]) {
            delete pd.second.curve;
        }
    }
    delete drawPicker;
    allPlots.erase(this);
}

void TraceXYPlot::setXAxis(double min, double max)
{
    sweep_fmin = min;
    sweep_fmax = max;
    updateXAxis();
}

void TraceXYPlot::setYAxis(int axis, TraceXYPlot::YAxisType type, bool log, bool autorange, double min, double max, double div)
{
    if(YAxis[axis].Ytype != type) {
        // remove traces that are active but not supported with the new axis type
        bool erased = false;
        do {
            erased = false;
            for(auto t : tracesAxis[axis]) {
                if(!supported(t, type)) {
                    enableTraceAxis(t, axis, false);
                    erased = true;
                    break;
                }
            }
        } while(erased);

        if(isTDRtype(YAxis[axis].Ytype)) {
            for(auto t : tracesAxis[axis]) {
                t->removeTDRinterest();
            }
        }
        YAxis[axis].Ytype = type;

        for(auto t : tracesAxis[axis]) {
            // supported but needs an adjusted QwtSeriesData
            auto td = curves[axis][t];
            td.data = createQwtSeriesData(*t, axis);
            // call to setSamples deletes old QwtSeriesData
            td.curve->setSamples(td.data);
            if(axis == 0) {
                // update marker data
                auto marker = t->getMarkers();
                for(auto m : marker) {
                    markerDataChanged(m);
                }
            }
            if(isTDRtype(type)) {
                t->addTDRinterest();
            }
        }
    }
    YAxis[axis].log = log;
    YAxis[axis].autorange = autorange;
    YAxis[axis].rangeMin = min;
    YAxis[axis].rangeMax = max;
    YAxis[axis].rangeDiv = div;
    // enable/disable y axis
    auto qwtaxis = axis == 0 ? QwtPlot::yLeft : QwtPlot::yRight;
    plot->enableAxis(qwtaxis, type != YAxisType::Disabled);
    if(autorange) {
        plot->setAxisAutoScale(qwtaxis, true);
    } else {
        plot->setAxisScale(qwtaxis, min, max, div);
    }
    updateContextMenu();
    replot();
}

void TraceXYPlot::setXAxis(XAxisType type, bool autorange, double min, double max, double div)
{
    XAxis.Xtype = type;
    XAxis.autorange = autorange;
    XAxis.rangeMin = min;
    XAxis.rangeMax = max;
    XAxis.rangeDiv = div;
    updateXAxis();
}

void TraceXYPlot::enableTrace(Trace *t, bool enabled)
{
    for(int axis = 0;axis < 2;axis++) {
        if(supported(t, YAxis[axis].Ytype)) {
            enableTraceAxis(t, axis, enabled);
        }
    }
}

void TraceXYPlot::updateGraphColors()
{
    for(auto p : allPlots) {
        p->setColorFromPreferences();
    }
}

bool TraceXYPlot::isTDRtype(TraceXYPlot::YAxisType type)
{
    switch(type) {
    case YAxisType::Impulse:
    case YAxisType::Step:
    case YAxisType::Impedance:
        return true;
    default:
        return false;
    }
}

void TraceXYPlot::updateContextMenu()
{
    contextmenu->clear();
    auto setup = new QAction("Axis setup...");
    connect(setup, &QAction::triggered, [this]() {
        auto setup = new XYplotAxisDialog(this);
        setup->show();
    });
    contextmenu->addAction(setup);
    for(int axis = 0;axis < 2;axis++) {
        if(YAxis[axis].Ytype == YAxisType::Disabled) {
            continue;
        }
        if(axis == 0) {
            contextmenu->addSection("Primary Traces");
        } else {
            contextmenu->addSection("Secondary Traces");
        }
        for(auto t : traces) {
            // Skip traces that are not applicable for the selected axis type
            if(!supported(t.first, YAxis[axis].Ytype)) {
                continue;
            }

            auto action = new QAction(t.first->name());
            action->setCheckable(true);
            if(tracesAxis[axis].find(t.first) != tracesAxis[axis].end()) {
                action->setChecked(true);
            }
            connect(action, &QAction::toggled, [=](bool active) {
                enableTraceAxis(t.first, axis, active);
            });
            contextmenu->addAction(action);
        }
    }
    contextmenu->addSeparator();
    auto close = new QAction("Close");
    contextmenu->addAction(close);
    connect(close, &QAction::triggered, [=]() {
        markedForDeletion = true;
    });
}

bool TraceXYPlot::supported(Trace *)
{
    // potentially possible to add every kind of trace (depends on axis)
    return true;
}

void TraceXYPlot::replot()
{
    plot->replot();
}

QString TraceXYPlot::AxisTypeToName(TraceXYPlot::YAxisType type)
{
    switch(type) {
    case YAxisType::Disabled: return "Disabled"; break;
    case YAxisType::Magnitude: return "Magnitude"; break;
    case YAxisType::Phase: return "Phase"; break;
    case YAxisType::VSWR: return "VSWR"; break;
    default: return "Unknown"; break;
    }
}

void TraceXYPlot::enableTraceAxis(Trace *t, int axis, bool enabled)
{
    bool alreadyEnabled = tracesAxis[axis].find(t) != tracesAxis[axis].end();
    if(alreadyEnabled != enabled) {
        if(enabled) {
            tracesAxis[axis].insert(t);
            CurveData cd;
            cd.data = createQwtSeriesData(*t, axis);
            cd.curve = new QwtPlotPiecewiseCurve();
            cd.curve->attach(plot);
            cd.curve->setYAxis(axis == 0 ? QwtPlot::yLeft : QwtPlot::yRight);
            cd.curve->setSamples(cd.data);
            curves[axis][t] = cd;
            // connect signals
            connect(t, &Trace::dataChanged, this, &TraceXYPlot::triggerReplot);
            connect(t, &Trace::colorChanged, this, &TraceXYPlot::traceColorChanged);
            connect(t, &Trace::visibilityChanged, this, &TraceXYPlot::traceColorChanged);
            connect(t, &Trace::visibilityChanged, this, &TraceXYPlot::triggerReplot);
            if(axis == 0) {
                connect(t, &Trace::markerAdded, this, &TraceXYPlot::markerAdded);
                connect(t, &Trace::markerRemoved, this, &TraceXYPlot::markerRemoved);
                auto tracemarkers = t->getMarkers();
                for(auto m : tracemarkers) {
                    markerAdded(m);
                }
            }
            if(isTDRtype(YAxis[axis].Ytype)) {
                t->addTDRinterest();
            }
            traceColorChanged(t);
        } else {
            if(isTDRtype(YAxis[axis].Ytype)) {
                t->removeTDRinterest();
            }
            tracesAxis[axis].erase(t);
            // clean up and delete
            if(curves[axis].find(t) != curves[axis].end()) {
                delete curves[axis][t].curve;
                curves[axis].erase(t);
            }
            int otherAxis = axis == 0 ? 1 : 0;
            if(curves[otherAxis].find(t) == curves[otherAxis].end()) {
                // this trace is not used anymore, disconnect from notifications
                disconnect(t, &Trace::dataChanged, this, &TraceXYPlot::triggerReplot);
                disconnect(t, &Trace::colorChanged, this, &TraceXYPlot::traceColorChanged);
                disconnect(t, &Trace::visibilityChanged, this, &TraceXYPlot::traceColorChanged);
                disconnect(t, &Trace::visibilityChanged, this, &TraceXYPlot::triggerReplot);
            }
            if(axis == 0) {
                disconnect(t, &Trace::markerAdded, this, &TraceXYPlot::markerAdded);
                disconnect(t, &Trace::markerRemoved, this, &TraceXYPlot::markerRemoved);
                auto tracemarkers = t->getMarkers();
                for(auto m : tracemarkers) {
                    markerRemoved(m);
                }
            }
        }

        updateContextMenu();
        replot();
    }
}

bool TraceXYPlot::supported(Trace *t, TraceXYPlot::YAxisType type)
{
    switch(type) {
    case YAxisType::Disabled:
        return false;
    case YAxisType::VSWR:
        if(!t->isReflection()) {
            return false;
        }
        break;
    default:
        break;
    }
    return true;
}

void TraceXYPlot::updateXAxis()
{
    if(XAxis.autorange && sweep_fmax-sweep_fmin > 0) {
        QList<double> tickList;
        for(double tick = sweep_fmin;tick <= sweep_fmax;tick+= (sweep_fmax-sweep_fmin)/10) {
            tickList.append(tick);
        }
        QwtScaleDiv scalediv(sweep_fmin, sweep_fmax, QList<double>(), QList<double>(), tickList);
        plot->setAxisScaleDiv(QwtPlot::xBottom, scalediv);
    } else {
        plot->setAxisScale(QwtPlot::xBottom, XAxis.rangeMin, XAxis.rangeMax, XAxis.rangeDiv);
    }
    triggerReplot();
}

QwtSeriesData<QPointF> *TraceXYPlot::createQwtSeriesData(Trace &t, int axis)
{
    return new QwtTraceSeries(t, YAxis[axis].Ytype, XAxis.Xtype);
}

void TraceXYPlot::traceColorChanged(Trace *t)
{
    for(int axis = 0;axis < 2;axis++) {
        if(curves[axis].find(t) != curves[axis].end()) {
            // trace active, change the pen color
            if(t->isVisible()) {
                if(axis == 0) {
                    curves[axis][t].curve->setPen(t->color());
                } else {
                    curves[axis][t].curve->setPen(t->color(), 1.0, Qt::DashLine);
                }
                for(auto m : t->getMarkers()) {
                    if(markers.count(m)) {
                        markers[m]->attach(plot);
                    }
                }
            } else {
                curves[axis][t].curve->setPen(t->color(), 0.0, Qt::NoPen);
                for(auto m : t->getMarkers()) {
                    if(markers.count(m)) {
                        markers[m]->detach();
                    }
                }
            }
        }
    }
}

void TraceXYPlot::markerAdded(TraceMarker *m)
{
    if(markers.count(m)) {
        return;
    }
    auto qwtMarker = new QwtPlotMarker;
    markers[m] = qwtMarker;
    markerSymbolChanged(m);
    connect(m, &TraceMarker::symbolChanged, this, &TraceXYPlot::markerSymbolChanged);
    connect(m, &TraceMarker::dataChanged, this, &TraceXYPlot::markerDataChanged);
    markerDataChanged(m);
    qwtMarker->attach(plot);
    triggerReplot();
}

void TraceXYPlot::markerRemoved(TraceMarker *m)
{
    disconnect(m, &TraceMarker::symbolChanged, this, &TraceXYPlot::markerSymbolChanged);
    disconnect(m, &TraceMarker::dataChanged, this, &TraceXYPlot::markerDataChanged);
    if(markers.count(m)) {
        markers[m]->detach();
        delete markers[m];
        markers.erase(m);
    }
    triggerReplot();
}

void TraceXYPlot::markerDataChanged(TraceMarker *m)
{
    auto qwtMarker = markers[m];
    qwtMarker->setXValue(m->getFrequency());
    qwtMarker->setYValue(FrequencyAxisTransformation(YAxis[0].Ytype, m->getData()));
    triggerReplot();
}

void TraceXYPlot::markerSymbolChanged(TraceMarker *m)
{
    auto qwtMarker = markers[m];
    auto old_sym = qwtMarker->symbol();
    qwtMarker->setSymbol(nullptr);
    delete old_sym;

    QwtSymbol *sym=new QwtSymbol;
    sym->setPixmap(m->getSymbol());
    sym->setPinPoint(QPointF(m->getSymbol().width()/2, m->getSymbol().height()));
    qwtMarker->setSymbol(sym);
    triggerReplot();
}

void TraceXYPlot::clicked(const QPointF pos)
{
    auto clickPoint = drawPicker->plotToPixel(pos);
    unsigned int closestDistance = numeric_limits<unsigned int>::max();
    TraceMarker *closestMarker = nullptr;
    for(auto m : markers) {
        auto markerPoint = drawPicker->plotToPixel(m.second->value());
        auto yDiff = abs(markerPoint.y() - clickPoint.y());
        auto xDiff = abs(markerPoint.x() - clickPoint.x());
        unsigned int distance = xDiff * xDiff + yDiff * yDiff;
        if(distance < closestDistance) {
            closestDistance = distance;
            closestMarker = m.first;
        }
    }
    if(closestDistance <= 400) {
        selectedMarker = closestMarker;
        selectedCurve = curves[0][selectedMarker->trace()].curve;
    } else {
        selectedMarker = nullptr;
        selectedCurve = nullptr;
    }
}

void TraceXYPlot::moved(const QPointF pos)
{
    if(!selectedMarker || !selectedCurve) {
        return;
    }
    selectedMarker->setFrequency(pos.x());
}

void TraceXYPlot::setColorFromPreferences()
{
    auto pref = Preferences::getInstance();
    plot->setCanvasBackground(pref.General.graphColors.background);
    auto pal = plot->palette();
    pal.setColor(QPalette::Window, pref.General.graphColors.background);
    pal.setColor(QPalette::WindowText, pref.General.graphColors.axis);
    pal.setColor(QPalette::Text, pref.General.graphColors.axis);
    plot->setPalette(pal);
    grid->setPen(pref.General.graphColors.divisions);
}

