/*
Copyright (C) 2014 Georgia Institute of Technology

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/*
* MEA
*/

#include <default_gui_model.h>
#include <boost/circular_buffer.hpp>
#include <basicplot.h>
//#include <RTXIprintfilter.h>
#include <QtGui>
#include <cstdlib>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_scale_engine.h>
#include <qwt_date_scale_engine.h>
#include <qwt_date_scale_draw.h>
#include <qwt_symbol.h>
#include <qwt_compat.h>

class TimeScaleDraw: public QwtScaleDraw
{
public:
    TimeScaleDraw(const QTime &base):
        baseTime(base) {
    }
    virtual QwtText label(double v) const {
        QTime upTime = baseTime.addSecs((int)v);
        return upTime.toString();
    }
private:
    QTime baseTime;
};

class MEA : public DefaultGUIModel
{
    Q_OBJECT

public:
    MEA(void);
    virtual ~MEA(void);
    void execute(void);
    void customizeGUI(void);

public slots:

signals: // all custom signals
    void setPlotRange(double newxmin, double newxmax, double newymin, double newymax);

protected:
    virtual void update(DefaultGUIModel::update_flags_t);

private:
    // testing variables (delete eventually)
    double prevtime;
    int channelSim;
    // inputs, states, related constants
    long long count; // keep track of plug-in time
    double dt;
    double systime;
    QTimer *timer2 = new QTimer(this);
    double refreshRate = 10;
    int spkcount; // spike count since last refresh
    struct spikeData {
        double channelNum;
        double spktime;
        // TO-DO: spike waveform (vector?)
    };
    spikeData spike;
    boost::circular_buffer<spikeData> meaBuffer; // buffer for all incoming data
    int n = 200; // maximum spikes per second for one channel
    int numChannels = 60; // TO-DO: change to 60 during actual testing
    int displayTime = 10*60; // change this to set the number of minutes of displayed raster data
    double bufferIndex;
    QwtArray<double> channels;
    QwtArray<double> time;
    double plotymin = 0;
    double plotymax = numChannels-1;

    // QT components
    BasicPlot *rplot;
    QwtPlotCurve *rCurve;

    // MEA functions
    void initParameters(void);
    void bookkeep(void);

private slots:
    // all custom slots
    void refreshMEA(void);
    void clearData(void);
    void screenshot(void);
};
