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
#include <qwt_compat.h>

class MEA : public DefaultGUIModel {
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
		// inputs, states, related constants
		long long count; // keep track of plug-in time
		double dt;
		double systime;
		int triggered;
		// TO-DO: change this to a buffer of structs (each containing channel no., spike time, spike waveform)
		boost::circular_buffer<double> meaBuffer; // buffer for all incoming data
		// TO-DO: reduce or increase this size if needed
		int numChannels = 1; // TO-DO: change to 60 during actual testing
		int n = 500; // constant size of meaBuffer for an individual channel (roughly 5 seconds of data)
		
		double plotxmin; // units: time
		double plotxmax;
		double plotymin = 0; // units: MEA channel number
		double plotymax = 59; // TO-DO: set to (numChannels-1)
		
		// QT components
		BasicPlot *rplot;
		QwtPlotCurve *rCurve;
		
		// MEA functions
		void initParameters();
		void bookkeep();
		bool OpenFile(QString);
		QFile dataFile;
		QTextStream stream;
	
	private slots:
		// all custom slots
		void refreshMEA(void);
		void clearData();
		void saveData();
		void print();
		void exportSVG();
};
