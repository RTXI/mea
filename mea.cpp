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
* This module displays a raster plot of MEA activity
*/

#include "mea.h"
#include <QtGui>
#include <qwt_plot_renderer.h>
#include <qwt_symbol.h>

extern "C" Plugin::Object *createRTXIPlugin(void) {
	return new MEA();
}

static DefaultGUIModel::variable_t vars[] = {
	{ "Input", "MEA input", DefaultGUIModel::INPUT, },
	{ "Event Trigger", "Trigger that indicates the spike time/event (=1)", DefaultGUIModel::INPUT, },
	// { "Stim", "", DefaultGUIModel::OUTPUT, }, // TO-DO: stimulation output
	{ "Refresh Rate (s)", "Raster plot refresh rate", DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
	{ "Note", "Time-stamped note to include in the output file", DefaultGUIModel::PARAMETER, },
	{ "Time (s)", "Time (s)", DefaultGUIModel::STATE, },
};

static size_t num_vars = sizeof(vars) / sizeof(DefaultGUIModel::variable_t);

// Default constructor
MEA::MEA(void) : DefaultGUIModel("MEA", ::vars, ::num_vars) {
	setWhatsThis(
		"<p><b>MEA:</b></p><p>This plug-in displays a raster plot of microelectrode array activity."
		" Click and drag on the plot to resize the axes.</p>");
	initParameters();
	DefaultGUIModel::createGUI(vars, num_vars); // this is required to create the GUI
	customizeGUI();
	update(INIT);
	refresh(); // this is required to update the GUI with parameter and state values
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
	QPushButton *savePlotButton = new QPushButton("Screenshot");
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

	timer->start(refreshRate * 1000);
	QObject::connect(timer, SIGNAL(timeout(void)), this, SLOT(refreshMEA(void)));

	emit setPlotRange(0, systime, plotymin, plotymax);
	customLayout->addWidget(plotBox, 0, 0, 1, 2);
	customLayout->addLayout(rightLayout, 1, 1);
	setLayout(customLayout);
}

MEA::~MEA(void) {}

void MEA::execute(void) {	
	systime = count * dt; // current time

	// QWT test code -- delete eventually
	if (systime - prevtime > 0.01) { // combined firing rate (try to ramp this up to see the limits of QWT)
		prevtime = systime;
		channelSim = rand() % 60;
		spike.channelNum = channelSim;
		spike.spktime = systime;
		meaBuffer.push(spike);
		
		spkcount++;
	}

	// add spikes to buffer
	if (input(1) == 1) {
		spike.channelNum = 10; // TO-DO: pull from MEA input frame
		spike.spktime = systime; // TO-DO: update once input frame is more defined (may need to create MEA spike detector module)
		meaBuffer.push(spike);
	
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
			setParameter("Note", note);
			break;
		case MODIFY:
			refreshRate = getParameter("Refresh Rate (s)").toDouble(); // To-do: constrain to > 4 Hz?
			bookkeep();
			break;
		case PAUSE:
			output(0) = 0; // stop command in case pause occurs in the middle of command
			break;
		case UNPAUSE:
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
	// testing variables (delete eventually)
    prevtime = 0;
    channelSim = 0;


	dt = RT::System::getInstance()->getPeriod() * 1e-9;
	systime = 0;
	refreshRate = 10; // max refresh rate = 4 Hz
	count = 0;
	note = "";
	
	numChannels = 60;
	spkcount = 0;
	plotymin = 0;
	plotymax = numChannels-1;

	bookkeep();
}

void MEA::bookkeep() {
	timer->start(refreshRate * 1000); // restart timer with new refreshRate
}

void MEA::refreshMEA() {
	for (int m = 0; m < spkcount; m++) {
		if(meaBuffer.pop(currentSpike)) {
			time.push_back(currentSpike.spktime);
			channels.push_back(currentSpike.channelNum);
		}
	}
	
	// delete old spikes
	if(!time.empty()) {
		while (time.front() < systime - displayTime) {
			time.pop_front();
			channels.pop_front();
		}
	}

	rCurve->setSamples(time, channels);
	
	if (systime <= displayTime) {
		emit setPlotRange(0, systime, plotymin, plotymax);
	} else {
		emit setPlotRange(systime-displayTime, systime, plotymin, plotymax);
	}
	rplot->replot();
	
	spkcount = 0;
}

void MEA::screenshot() {
	QwtPlotRenderer renderer;
	renderer.exportTo(rplot,"screenshot.pdf");
}

void MEA::clearData() {
	time.clear();
	channels.clear();
	
	rCurve->setSamples(time, channels);
	rplot->replot();
}
