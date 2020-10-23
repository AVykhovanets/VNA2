#include "trace.h"

using namespace std;

Trace::Trace(QString name, QColor color, LiveParameter live)
    : _name(name),
      _color(color),
      _liveType(LivedataType::Overwrite),
      _liveParam(live),
      reflection(true),
      visible(true),
      paused(false),
      touchstone(false),
      calibration(false)
{

}

Trace::~Trace()
{
    emit deleted(this);
}

void Trace::clear() {
    if(paused) {
        return;
    }
    _data.clear();
    emit cleared(this);
    emit dataChanged();
}

void Trace::addData(Trace::Data d) {
    // add or replace data in vector while keeping it sorted with increasing frequency
    auto lower = lower_bound(_data.begin(), _data.end(), d, [](const Data &lhs, const Data &rhs) -> bool {
        return lhs.frequency < rhs.frequency;
    });
    if(lower == _data.end()) {
        // highest frequency yet, add to vector
        _data.push_back(d);
    } else if(lower->frequency == d.frequency) {
        switch(_liveType) {
        case LivedataType::Overwrite:
            // replace this data element
            *lower = d;
            break;
        case LivedataType::MaxHold:
            // replace this data element
            if(abs(d.S) > abs(lower->S)) {
                *lower = d;
            }
            break;
        case LivedataType::MinHold:
            // replace this data element
            if(abs(d.S) < abs(lower->S)) {
                *lower = d;
            }
            break;
        }

    } else {
        // insert at this position
        _data.insert(lower, d);
    }
    emit dataAdded(this, d);
    emit dataChanged();
}

void Trace::setName(QString name) {
    _name = name;
    emit nameChanged();
}

void Trace::fillFromTouchstone(Touchstone &t, unsigned int parameter, QString filename)
{
    if(parameter >= t.ports()*t.ports()) {
        throw runtime_error("Parameter for touchstone out of range");
    }
    clear();
    setTouchstoneParameter(parameter);
    setTouchstoneFilename(filename);
    for(unsigned int i=0;i<t.points();i++) {
        auto tData = t.point(i);
        Data d;
        d.frequency = tData.frequency;
        d.S = t.point(i).S[parameter];
        addData(d);
    }
    // check if parameter is square (e.i. S11/S22/S33/...)
    parameter++;
    bool isSquare = false;
    for (unsigned int i = 1; i * i <= parameter; i++) {

        // If (i * i = n)
        if ((parameter % i == 0) && (parameter / i == i)) {
            isSquare = true;
            break;
        }
    }
    if(isSquare == 1) {
        reflection = true;
    } else {
        reflection = false;
    }
    touchstone = true;
    emit typeChanged(this);
}

void Trace::fromLivedata(Trace::LivedataType type, LiveParameter param)
{
    touchstone = false;
    _liveType = type;
    _liveParam = param;
    if(param == LiveParameter::S11 || param == LiveParameter::S22) {
        reflection = true;
    } else {
        reflection = false;
    }
    emit typeChanged(this);
}

void Trace::setColor(QColor color) {
    if(_color != color) {
        _color = color;
        emit colorChanged(this);
    }
}

void Trace::addMarker(TraceMarker *m)
{
    markers.insert(m);
    emit markerAdded(m);
}

void Trace::removeMarker(TraceMarker *m)
{
    markers.erase(m);
    emit markerRemoved(m);
}

void Trace::setReflection(bool value)
{
    reflection = value;
}

void Trace::setCalibration(bool value)
{
    calibration = value;
}

std::set<TraceMarker *> Trace::getMarkers() const
{
    return markers;
}

void Trace::setVisible(bool visible)
{
    if(visible != this->visible) {
        this->visible = visible;
        emit visibilityChanged(this);
    }
}

bool Trace::isVisible()
{
    return visible;
}

void Trace::pause()
{
    paused = true;
}

void Trace::resume()
{
    paused = false;
}

bool Trace::isPaused()
{
    return paused;
}

bool Trace::isTouchstone()
{
    return touchstone;
}

bool Trace::isCalibration()
{
    return calibration;
}

bool Trace::isLive()
{
    return !isCalibration() && !isTouchstone();
}

bool Trace::isReflection()
{
    return reflection;
}

double Trace::findExtremumFreq(bool max)
{
    double compare = max ? numeric_limits<double>::min() : numeric_limits<double>::max();
    double freq = 0.0;
    for(auto d : _data) {
        double amplitude = abs(d.S);
        if((max && (amplitude > compare)) || (!max && (amplitude < compare))) {
            // higher/lower extremum found
            compare = amplitude;
            freq = d.frequency;
        }
    }
    return freq;
}

std::vector<double> Trace::findPeakFrequencies(unsigned int maxPeaks, double minLevel, double minValley)
{
    using peakInfo = struct peakinfo {
        double frequency;
        double level_dbm;
    };
    vector<peakInfo> peaks;
    double frequency = 0.0;
    double max_dbm = -200.0;
    double min_dbm = 200.0;
    for(auto d : _data) {
        double dbm = 20*log10(abs(d.S));
        if((dbm >= max_dbm) && (min_dbm <= dbm - minValley)) {
            // potential peak frequency
            frequency = d.frequency;
            max_dbm = dbm;
        }
        if(dbm <= min_dbm) {
            min_dbm = dbm;
        }
        if((dbm <= max_dbm - minValley) && (max_dbm >= minLevel)) {
            // peak was high enough and dropped below minValley afterwards
            peakInfo peak;
            peak.frequency = frequency;
            peak.level_dbm = max_dbm;
            peaks.push_back(peak);
            // reset
            frequency = 0.0;
            max_dbm = min_dbm = dbm;
        }
    }
    if(peaks.size() > maxPeaks) {
        // found more peaks than requested, remove excess peaks
        // sort with descending peak level
        sort(peaks.begin(), peaks.end(), [](peakInfo higher, peakInfo lower) {
           return higher.level_dbm >= lower.level_dbm;
        });
        // only keep the requested number of peaks
        peaks.resize(maxPeaks);
        // sort again with ascending frequencies
        sort(peaks.begin(), peaks.end(), [](peakInfo lower, peakInfo higher) {
           return higher.frequency >= lower.frequency;
        });
    }
    vector<double> frequencies;
    for(auto p : peaks) {
        frequencies.push_back(p.frequency);
    }
    return frequencies;
}

QString Trace::getTouchstoneFilename() const
{
    return touchstoneFilename;
}

void Trace::setTouchstoneFilename(const QString &value)
{
    touchstoneFilename = value;
}

unsigned int Trace::getTouchstoneParameter() const
{
    return touchstoneParameter;
}

std::complex<double> Trace::getData(double frequency)
{
    if(_data.size() == 0 || frequency < minFreq() || frequency > maxFreq()) {
        return std::numeric_limits<std::complex<double>>::quiet_NaN();
    }

    return sample(index(frequency)).S;
}

int Trace::index(double frequency)
{
    auto lower = lower_bound(_data.begin(), _data.end(), frequency, [](const Data &lhs, const double freq) -> bool {
        return lhs.frequency < freq;
    });
    return lower - _data.begin();
}

void Trace::setTouchstoneParameter(int value)
{
    touchstoneParameter = value;
}
