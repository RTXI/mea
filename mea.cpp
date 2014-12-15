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

/*
* Thoughts:
* 	Want to implement a logarithmic time scale on the raster plot (see QwtLogScaleEngine)
* 	Increment the plot x-max parameter based on the current time
* 	Allow user to scroll to previous times in plot
*/

#include "mea.h"
#include <math.h>
#include <algorithm>
#include <numeric>
#include <time.h>

#include <QtGui>
#include <sys/stat.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>

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
	{ "Input", "MEA waveform input", DefaultGUIModel::INPUT, },
	{ "Plot x-min (s)", "X-min for the MEA raster plot",
		DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
	{ "Plot x-max (s)", "X-max for the MEA raster plot",
		DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE, },
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
	QGridLayout *customLayout = DefaultGUIModel::getLayout();
	
	// Need to adjust these for the raster plot
	rplot = new BasicPlot(this);
	rCurve = new QwtPlotCurve("Curve 1");
	rCurve->attach(rplot);
	rCurve->setPen(QColor(Qt::white));
	//rCurve->setAxisOptions
	
	QVBoxLayout *rightLayout = new QVBoxLayout;
	QGroupBox *plotBox = new QGroupBox("MEA Raster Plot");
	QHBoxLayout *plotBoxLayout = new QHBoxLayout;
	plotBox->setLayout(plotBoxLayout);
	QPushButton *clearButton = new QPushButton("&Clear");
	QPushButton *savePlotButton = new QPushButton("Save Screenshot");
	QPushButton *printButton = new QPushButton("Print");
	QPushButton *saveDataButton = new QPushButton("Save Data");
	plotBoxLayout->addWidget(clearButton);
	plotBoxLayout->addWidget(savePlotButton);
	plotBoxLayout->addWidget(printButton);
	plotBoxLayout->addWidget(saveDataButton);
	
	rightLayout->addWidget(rplot);
	
	QObject::connect(clearButton, SIGNAL(clicked()), this, SLOT(clearData()));
	QObject::connect(savePlotButton, SIGNAL(clicked()), this, SLOT(exportSVG()));
	QObject::connect(printButton, SIGNAL(clicked()), this, SLOT(print()));
	QObject::connect(saveDataButton, SIGNAL(clicked()), this, SLOT(saveData()));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),this,SLOT(pause(bool)));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),savePlotButton,SLOT(setEnabled(bool)));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),printButton,SLOT(setEnabled(bool)));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),saveDataButton,SLOT(setEnabled(bool)));
	QObject::connect(DefaultGUIModel::pauseButton, SIGNAL(toggled(bool)),DefaultGUIModel::modifyButton,SLOT(setEnabled(bool)));
	QObject::connect(this, SIGNAL(setPlotRange(double, double, double, double)), rplot, SLOT(setAxes(double, double, double, double)));
	
	DefaultGUIModel::pauseButton->setToolTip("Start/Stop protocol");
	DefaultGUIModel::modifyButton->setToolTip("Commit changes to parameter values");
	DefaultGUIModel::unloadButton->setToolTip("Close module");

	QTimer *timer2 = new QTimer(this);
	timer2->start(2000);
	QObject::connect(timer2, SIGNAL(timeout(void)), this, SLOT(refreshMEA(void)));

	customLayout->addWidget(plotBox, 0, 0, 1, 2);
	customLayout->addLayout(rightLayout, 1, 1);
	setLayout(customLayout);
}

MEA::~MEA(void) {}

void MEA::execute(void) {
	systime = count * dt; // current time,
	signalin.push_back(input(0)); // always buffer, we don't know when the event occurs
	
	if (triggered == 1) {
		wincount++;
		if (wincount == rightwin) { // compute average, do this way to keep numerical accuracy
			for (int i = 0; i < leftwin + rightwin + 1; i++) {
				stasum[i] = stasum[i] + signalin[i];
				staavg[i] = stasum[i] / eventcount;
			}
		} else if (wincount > rightwin) {
			wincount = 0;
			triggered = 0;
		}
	} else if (triggered == 0 && input(1) == 1) {
		triggered = 1;
		eventcount++;
	}
	
	count++; // increment count to measure time
	return;
}

void MEA::update(DefaultGUIModel::update_flags_t flag) {
	switch (flag) {
		case INIT:
			setParameter("Plot x-min (s)", QString::number(plotxmin));
			setParameter("Plot x-max (s)", QString::number(plotxmax));
			setState("Time (s)", systime);
			break;
		
		case MODIFY:
			plotxmin = getParameter("Plot x-min (s)").toDouble();
			plotxmax = getParameter("Plot x-max (s)").toDouble();
			bookkeep();
			break;

		case PAUSE:
			output(0) = 0; // stop command in case pause occurs in the middle of command
			printf("Protocol paused.\n");
			break;

		case UNPAUSE:
			bookkeep();
			printf("Protocol started.\n");
			break;

		case PERIOD:
			dt = RT::System::getInstance()->getPeriod() * 1e-9;
			bookkeep();
		
		default:
			break;
	}
}

// custom functions
void MEA::initParameters() {
	dt = RT::System::getInstance()->getPeriod() * 1e-9; // s
	signalin.clear();
	assert(signalin.size() == 0);
	plotxmin = 0; // s
	plotxmax = 10;
	plotymin = 0;
	plotymax = 59;
	bookkeep();
}

void MEA::bookkeep() {
	triggered = 0;
	count = 0;
	eventcount = 0;
	wincount = 0;
	systime = 0;
	n = leftwin + rightwin + 1;
	leftwin = int(plotxmin / dt);
	rightwin = int(plotxmax / dt);
	signalin.rset_capacity(n);
	staavg.resize(n);
	stasum.resize(n);
	time.resize(n);
	for (int i = 0; i < n; i++) {
		signalin.push_back(0);
		stasum[i] = 0;
		staavg[i] = 0;
		time[i] = dt * i - plotxmin;
	}
}

void MEA::refreshMEA() {
	rCurve->setSamples(time, staavg);//, n);
	rplot->replot();
	emit setPlotRange(-plotxmin, plotxmax, 0, 60);
}

void MEA::clearData() {
	eventcount = 0;
	for (int i = 0; i < n; i++) {
		signalin.push_back(0);
	}
	triggered = 0;
	wincount = 0;
	for (int i = 0; i < n; i++) {
		stasum[i] = 0;
		staavg[i] = 0;
	}
	
	rCurve->setSamples(time, staavg);//, n);
	rplot->replot();
}

void MEA::saveData() {
	QFileDialog* fd = new QFileDialog(this);//, "Save File As", TRUE);
	fd->setFileMode(QFileDialog::AnyFile);
	fd->setViewMode(QFileDialog::Detail);
	QStringList fileList;
	QString fileName;
	if (fd->exec() == QDialog::Accepted) {
		fileList = fd->selectedFiles();
		if (!fileList.isEmpty()) fileName = fileList[0];
		
		if (OpenFile(fileName)) {
			for (int i = 0; i < n; i++) {
				stream << (double) time[i] << " " << (double) staavg[i] << "\n";
			}
			dataFile.close();
			printf("File closed.\n");
		} else {
			QMessageBox::information(this,
			"Event-triggered Average: Save Average",
			"There was an error writing to this file. You can view\n"
			"the values that should be plotted in the terminal.\n");
		}
	}
}

bool MEA::OpenFile(QString FName) {
	dataFile.setFileName(FName);
	if (dataFile.exists()) {
		switch (QMessageBox::warning(this, "Event-triggered Average", tr(
				"This file already exists: %1.\n").arg(FName), "Overwrite", "Append",
				"Cancel", 0, 2)) {
			case 0: // overwrite
				dataFile.remove();
				if (!dataFile.open(QIODevice::Unbuffered | QIODevice::WriteOnly)) return false;
				break;
			
			case 1: // append
				if (!dataFile.open(QIODevice::Unbuffered | QIODevice::Append )) return false;
				break;
		
			case 2: // cancel
				return false;
				break;
		}
	} else {
		if (!dataFile.open(QIODevice::Unbuffered | QIODevice::WriteOnly))	return false;
	}
	stream.setDevice(&dataFile);
	printf("File opened: %s\n", FName.toUtf8().constData());
	return true;
}

void MEA::print() {
/*
	#if 1
	QPrinter printer;
	#else
	QPrinter printer(QPrinter::HighResolution);
	#if QT_VERSION < 0x040000
	printer.setOutputToFile(true);
	printer.setOutputFileName("/tmp/STA.ps");
	printer.setColorMode(QPrinter::Color);
	#else
	printer.setOutputFileName("/tmp/STA.pdf");
	#endif
	#endif
	
	QString docName = rplot->title().text();
	if (!docName.isEmpty())
		{
		docName.replace(QRegExp(QString::fromLatin1("\n")), tr(" -- "));
		printer.setDocName(docName);
	}
	
	printer.setCreator("RTXI");
	printer.setOrientation(QPrinter::Landscape);
	
	#if QT_VERSION >= 0x040000
	QPrintDialog dialog(&printer);
	if ( dialog.exec() ) {
		#else
		if (printer.setup()) {
			#endif
			RTXIPrintFilter filter;
			if (printer.colorMode() == QPrinter::GrayScale) {
				int options = QwtPlotPrintFilter::PrintAll;
				filter.setOptions(options);
				filter.color(QColor(29, 100, 141),
				QwtPlotPrintFilter::CanvasBackground);
				filter.color(Qt::white, QwtPlotPrintFilter::CurveSymbol);
			}
			rplot->print(printer, filter);
		}
*/
}
	
void MEA::exportSVG() {
	QString fileName = "STA.svg";
		
	#if QT_VERSION < 0x040000
	
	#ifndef QT_NO_FILEDIALOG
	fileName = QFileDialog::getSaveFileName("STA.svg", "SVG Documents (*.svg)", this);
	#endif
	if (!fileName.isEmpty()) {
		// enable workaround for Qt3 misalignments
		QwtPainter::setSVGMode(true);
		QPicture picture;
		QPainter p(&picture);
		rplot->print(&p, QRect(0, 0, 800, 600));
		p.end();
		picture.save(fileName, "svg");
	}
	
	#elif QT_VERSION >= 0x040300
	
	#ifdef QT_SVG_LIB
	#ifndef QT_NO_FILEDIALOG
	fileName = QFileDialog::getSaveFileName(
	this, "Export File Name", QString(),
	"SVG Documents (*.svg)");
	#endif
	if ( !fileName.isEmpty() ) {
		QSvgGenerator generator;
		generator.setFileName(fileName);
		generator.setSize(QSize(800, 600));
		rplot->print(generator);
	}
	#endif
	#endif
}
