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

/*
* TO-DO: make the circular buffer thread-safe (mutex)
*/

#include "mea.h"
#include <algorithm>
#include <math.h>
#include <numeric>
#include <time.h>

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
	// TO-DO: remove unnecessary buttons, etc
	//        fix log scaling on the plot
	//        fix input boxes
	QGridLayout *customLayout = DefaultGUIModel::getLayout();
	
	// Need to adjust these for the raster plot
	rplot = new BasicPlot(this);
	rCurve = new QwtPlotCurve("Curve 1");
	rCurve->setStyle(QwtPlotCurve::NoCurve);
	rCurve->setSymbol(new QwtSymbol(QwtSymbol::VLine, Qt::NoBrush, QPen(Qt::white), QSize(4,4)));
	rCurve->attach(rplot);
	rCurve->setPen(QColor(Qt::white));
	//rplot->setAxisScaleEngine(QwtPlot::xBottom, new QwtLogScaleEngine);
	//rplot->setAxisScale(QwtPlot::xBottom, 0.1, 100);
	////rplot->setAutoReplot(false);
	
	QVBoxLayout *rightLayout = new QVBoxLayout;
	QGroupBox *plotBox = new QGroupBox("MEA Raster Plot");
	QHBoxLayout *plotBoxLayout = new QHBoxLayout;
	plotBox->setLayout(plotBoxLayout);
	QPushButton *startButton = new QPushButton("Start Raster Plot");
	QPushButton *clearButton = new QPushButton("&Clear");
	QPushButton *savePlotButton = new QPushButton("Save Screenshot");
	QPushButton *printButton = new QPushButton("Print");
	QPushButton *saveDataButton = new QPushButton("Save Data");
	plotBoxLayout->addWidget(startButton);
	plotBoxLayout->addWidget(clearButton);
	plotBoxLayout->addWidget(savePlotButton);
	plotBoxLayout->addWidget(printButton);
	plotBoxLayout->addWidget(saveDataButton);
	
	rightLayout->addWidget(rplot);
	
	QObject::connect(startButton, SIGNAL(clicked()), this, SLOT(startPlot()));
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

	//// TO-DO: max refresh rate should be 4 Hz
	//QTimer *timer2 = new QTimer(this);
	//timer2->start(2000);
	//QObject::connect(timer2, SIGNAL(timeout(void)), this, SLOT(refreshMEA(void)));

	customLayout->addWidget(plotBox, 0, 0, 1, 2);
	customLayout->addLayout(rightLayout, 1, 1);
	setLayout(customLayout);
}

MEA::~MEA(void) {}

void MEA::execute(void) {
	systime = count * dt; // current time

	// QWT test code -- delete eventually
	if (systime - prevtime > 0.001) { // 1 kHz combined firing rate (try to ramp this up to see the limits of QWT)
		prevtime = systime; // update prevtime
		channelSim = rand() % 60;
		spike.channelNum = channelSim;
		spike.spktime = systime;
		meaBuffer.push_back(spike);
		
		spkcount++;
		std::cout << "Spike on channel " << channelSim << " at " << systime << " seconds" << std::endl;
		std::cout << "Current spike count: " << spkcount << std::endl;
	}

	// TO-DO: need to adjust things for multiple channels
	// add to circular buffer for displaying on raster plot
	if (input(1) == 1) {
		spike.channelNum = 0; // TO-DO: pull from MEA input frame
		spike.spktime = systime; // TO-DO: update once input frame is more defined (may need to create MEA spike detector module)
		meaBuffer.push_back(spike);
	
		// keep track of number of spikes and reset to 0 in refreshMEA
		spkcount++;
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
	i = 0;
	index = 0;
	spkcount = 0;
	systime = 0;
	prevtime = 0;
	dt = RT::System::getInstance()->getPeriod() * 1e-9; // s
	meaBuffer.clear();
	assert(meaBuffer.size() == 0);
	meaBuffer.rset_capacity(n * numChannels); // set default size of circular buffer
	plotxmin = 0; // units: seconds
	plotxmax = 10;
	bookkeep();
}

// TO-DO: possibly delete this
void MEA::bookkeep() {
	
}

void MEA::refreshMEA() {
	// TO-DO: need to test if buffer is empty
	
	// update raster plot starting from previous location in circular buffer
	//time = new double[spkcount];
	//channels = new double[spkcount];
	time.resize(spkcount);
	channels.resize(spkcount);
	for (int m = 0; m < spkcount; m++) {
		//rCurve->setSamples(&meaBuffer[i].channelNum, &meaBuffer[i].spktime, 8);
		channels[m] = meaBuffer[i].channelNum;
		time[m] = meaBuffer[i].spktime;
		if (i == meaBuffer.size() - 1) {
			i = 0;
		} else {
			i++;
		}
	}
	
	rCurve->setSamples(time, channels);//, n);
	rplot->replot();
	emit setPlotRange(plotxmin, plotxmax, plotymin, plotymax);
	
	//rCurve->setSamples(time, channels); // TO-DO: need to test; this may overwrite previous points on the plot (but I think hold on is the default)
	//rCurve->attach(rplot);
	//rplot->replot(); // TO-DO: is this the appropriate function?
	//rplot->show();
	//emit setPlotRange(-plotxmin, plotxmax, plotymin, plotymax); // TO-DO: need to fix x-axis scale and labels
	setState("Time (s)", systime);
	
	// reset spkcount
	spkcount = 0;
}

void MEA::startPlot() {
	// TO-DO: max refresh rate should be 4 Hz
	//        only allow this to run once?
	QTimer *timer2 = new QTimer(this);
	timer2->start(2000);
	QObject::connect(timer2, SIGNAL(timeout(void)), this, SLOT(refreshMEA(void)));
}

// To-DO: fix or delete?
void MEA::clearData() {
	//eventcount = 0;
	//for (int i = 0; i < n; i++) {
		//meaBuffer.push_back(0);
	//}
	//triggered = 0;
	
	//rCurve->setSamples(time, staavg);//, n);
	//rplot->replot();
}

// TO-DO: change STA stuff to raster plot
void MEA::saveData() {
	//QFileDialog* fd = new QFileDialog(this);//, "Save File As", TRUE);
	//fd->setFileMode(QFileDialog::AnyFile);
	//fd->setViewMode(QFileDialog::Detail);
	//QStringList fileList;
	//QString fileName;
	//if (fd->exec() == QDialog::Accepted) {
		//fileList = fd->selectedFiles();
		//if (!fileList.isEmpty()) fileName = fileList[0];
		
		//if (OpenFile(fileName)) {
			//for (int i = 0; i < n; i++) {
				//stream << (double) time[i] << " " << (double) staavg[i] << "\n";
			//}
			//dataFile.close();
			//printf("File closed.\n");
		//} else {
			//QMessageBox::information(this,
			//"Event-triggered Average: Save Average",
			//"There was an error writing to this file. You can view\n"
			//"the values that should be plotted in the terminal.\n");
		//}
	//}
}

// TO-DO: test and fix if necessary
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

// TO-DO: fix? this is commented out in STA module
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

// TO-DO: test and fix if necessary
void MEA::exportSVG() {
	//QString fileName = "STA.svg";
		
	//#if QT_VERSION < 0x040000
	
	//#ifndef QT_NO_FILEDIALOG
	//fileName = QFileDialog::getSaveFileName("STA.svg", "SVG Documents (*.svg)", this);
	//#endif
	//if (!fileName.isEmpty()) {
		//// enable workaround for Qt3 misalignments
		//QwtPainter::setSVGMode(true);
		//QPicture picture;
		//QPainter p(&picture);
		//rplot->print(&p, QRect(0, 0, 800, 600));
		//p.end();
		//picture.save(fileName, "svg");
	//}
	
	//#elif QT_VERSION >= 0x040300
	
	//#ifdef QT_SVG_LIB
	//#ifndef QT_NO_FILEDIALOG
	//fileName = QFileDialog::getSaveFileName(
	//this, "Export File Name", QString(),
	//"SVG Documents (*.svg)");
	//#endif
	//if ( !fileName.isEmpty() ) {
		//QSvgGenerator generator;
		//generator.setFileName(fileName);
		//generator.setSize(QSize(800, 600));
		//rplot->print(generator);
	//}
	//#endif
	//#endif
}
