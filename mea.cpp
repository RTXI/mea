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
* MEA module: displays a raster plot of MEA activity
*/

#include "mea.h"
#include <algorithm>
#include <math.h>
#include <numeric>
#include <time.h>
#include <qwt_plot_renderer.h>

#include <sys/stat.h>

#if QT_VERSION >= 0x040300
#ifdef QT_SVG_LIB
#include <qsvggenerator.h>
#endif
#endif

#if QT_VERSION >= 0x040000
#include <qprintdialog.h>
#include <qfileinfo.h>
#else
#include <qwt_painter.h>
#endif

extern "C" Plugin::Object *createRTXIPlugin(void) {
	return new MEA();
}

static DefaultGUIModel::variable_t vars[] = {
	{ "Input", "MEA input", DefaultGUIModel::INPUT, },
	{ "Event Trigger", "Trigger that indicates the spike time/event (=1)", DefaultGUIModel::INPUT, },
	{ "Refresh Rate (s)", "Raster plot refresh rate", DefaultGUIModel::PARAMETER, },
	{ "Time (s)", "Time (s)", DefaultGUIModel::STATE, },
};

static size_t num_vars = sizeof(vars) / sizeof(DefaultGUIModel::variable_t);

// Default constructor
MEA::MEA(void) : DefaultGUIModel("MEA", ::vars, ::num_vars) {
	setWhatsThis(
	"<p><b>MEA:</b></p><p>This plug-in displays a raster plot of microelectrode array activity. Click and drag on the plot to resize the axes.</p>");
	initParameters();
	DefaultGUIModel::createGUI(vars, num_vars); // this is required to create the GUI
	customizeGUI();
	update(INIT);
	refresh(); // this is required to update the GUI with parameter and state values
	printf("\nStarting MEA plug-in\n"); // prints to terminal
}

void MEA::customizeGUI(void) {
	// TO-DO: allow plot to scale with module window
	//        inputs/outputs
	QGridLayout *customLayout = DefaultGUIModel::getLayout();
	
	rplot = new BasicPlot(this);
    rplot->setAxisScaleDraw(QwtPlot::xBottom, new TimeScaleDraw(QTime(0,0,0,0)));
	rCurve = new QwtPlotCurve("Curve 1");
	rCurve->setStyle(QwtPlotCurve::NoCurve);
	rCurve->setSymbol(new QwtSymbol(QwtSymbol::VLine, Qt::NoBrush, QPen(Qt::white), QSize(4,4)));
	rCurve->attach(rplot);
	rCurve->setPen(QColor(Qt::white));
	
	QVBoxLayout *rightLayout = new QVBoxLayout;
	QGroupBox *plotBox = new QGroupBox("MEA Raster Plot");
	QHBoxLayout *plotBoxLayout = new QHBoxLayout;
	plotBox->setLayout(plotBoxLayout);
	QPushButton *savePlotButton = new QPushButton("Save Screenshot");
	QPushButton *clearButton = new QPushButton("Clear Plot");
	plotBoxLayout->addWidget(savePlotButton);
	plotBoxLayout->addWidget(clearButton);
	rightLayout->addWidget(rplot);
	
	QObject::connect(clearButton, SIGNAL(clicked()), this, SLOT(clearData()));
	QObject::connect(savePlotButton, SIGNAL(clicked()), this, SLOT(screenshot()));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),this,SLOT(pause(bool)));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),clearButton,SLOT(setEnabled(bool)));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),DefaultGUIModel::modifyButton,SLOT(setEnabled(bool)));
	QObject::connect(this, SIGNAL(setPlotRange(double, double, double, double)), rplot, SLOT(setAxes(double, double, double, double)));
	
	DefaultGUIModel::pauseButton->setToolTip("Start/Stop protocol");
	DefaultGUIModel::modifyButton->setToolTip("Commit changes to parameter values");
	DefaultGUIModel::unloadButton->setToolTip("Close module");

	timer2->start(refreshRate * 1000); // max refresh rate = 4 Hz
	QObject::connect(timer2, SIGNAL(timeout(void)), this, SLOT(refreshMEA(void)));

	emit setPlotRange(0, systime, plotymin, plotymax);
	customLayout->addWidget(plotBox, 0, 0, 1, 2);
	customLayout->addLayout(rightLayout, 1, 1);
	setLayout(customLayout);
}

MEA::~MEA(void) {}

void MEA::execute(void) {
	systime = count * dt; // current time

	// QWT test code -- delete eventually
	//if (systime - prevtime > 0.01) { // combined firing rate (try to ramp this up to see the limits of QWT)
		//prevtime = systime;
		//channelSim = rand() % 60;
		//spike.channelNum = channelSim;
		//spike.spktime = systime;
		//meaBuffer.push_back(spike);
		
		//spkcount++;
	//}

	// TO-DO: change to atomic circular buffer
	// add spikes to circular buffer
	if (input(1) == 1) {
		spike.channelNum = 10; // TO-DO: pull from MEA input frame
		spike.spktime = systime; // TO-DO: update once input frame is more defined (may need to create MEA spike detector module)
		meaBuffer.push_back(spike);
	
		spkcount++;
	}
	
	count++; // increment count to measure time
	return;
}

void MEA::update(DefaultGUIModel::update_flags_t flag) {
	switch (flag) {
		case INIT:
			setState("Time (s)", systime);
			setParameter("Refresh Rate (s)", QString::number(refreshRate));
			break;
		
		case MODIFY:
			refreshRate = getParameter("Refresh Rate (s)").toDouble(); // To-do: constrain to > 4 Hz?
			bookkeep();
			break;

		case PAUSE:
			output(0) = 0; // stop command in case pause occurs in the middle of command
			printf("Protocol paused.\n");
			break;

		case UNPAUSE:
			printf("Protocol started.\n");
			bookkeep();
			break;

		case PERIOD:
			dt = RT::System::getInstance()->getPeriod() * 1e-9;
			bookkeep();
			break;
		
		default:
			break;
	}
}

// custom functions
void MEA::initParameters() {
	// TO-DO: add init values for new variables (see header file)
	systime = 0;
	dt = RT::System::getInstance()->getPeriod() * 1e-9;
	count = 0;
	bufferIndex = 0;
	prevtime = 0;
	spkcount = 0;
	
	meaBuffer.clear();
	assert(meaBuffer.size() == 0);
	meaBuffer.rset_capacity(n * numChannels);
	bookkeep();
}

// TO-DO: possibly delete this
void MEA::bookkeep() {
	timer2->start(refreshRate * 1000); // restart timer with new refreshRate
}

void MEA::refreshMEA() {
	if (meaBuffer.size() != 0) {
		// add new spikes
		bufferIndex = meaBuffer.size() - 1;
		for (int m = 0; m < spkcount; m++) {
			time.push_back(meaBuffer[bufferIndex].spktime);
			channels.push_back(meaBuffer[bufferIndex].channelNum);
			bufferIndex--;
		}
		// delete old spikes
		while (time.front() < systime - displayTime) {
			time.pop_front();
			channels.pop_front();
		}
		// replot (TO-DO: test the limits of this)
		rCurve->setSamples(time, channels);
	}
	if (systime <= displayTime) {
		emit setPlotRange(0, systime, plotymin, plotymax);
	} else {
		emit setPlotRange(systime-displayTime, systime, plotymin, plotymax);
	}
	rplot->replot();
	setState("Time (s)", systime);
	spkcount = 0;
}

void MEA::screenshot() {
	QwtPlotRenderer renderer;
	renderer.exportTo(rplot,"screenshot.pdf");
}

void MEA::clearData() {
	meaBuffer.clear();
	time.clear();
	channels.clear();
	
	rCurve->setSamples(time, channels);
	rplot->replot();
}
