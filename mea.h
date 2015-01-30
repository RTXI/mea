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

#include <atomic>
#include <basicplot.h>
#include <default_gui_model.h>
#include <qwt_plot_curve.h>

class TimeScaleDraw: public QwtScaleDraw
{
	public:
		TimeScaleDraw(const QTime &base):
			baseTime(base)
		{
		}
		virtual QwtText label(double v) const
		{
			QTime upTime = baseTime.addSecs((int)v);
			return upTime.toString();
		}
	private:
		QTime baseTime;
};

template<typename T, size_t Size>
class ringbuffer {
	public:
		ringbuffer() : head_(0), tail_(0) {}

		bool push(const T & value)
		{
			size_t head = head_.load(std::memory_order_relaxed);
			size_t next_head = next(head);
			if (next_head == tail_.load(std::memory_order_acquire))
				return false;
			ring_[head] = value;
			head_.store(next_head, std::memory_order_release);
				return true;
		}
		bool pop(T & value)
		{
			size_t tail = tail_.load(std::memory_order_relaxed);
			if (tail == head_.load(std::memory_order_acquire))
				return false;
			value = ring_[tail];
			tail_.store(next(tail), std::memory_order_release);
				return true;
		}
	private:
		size_t next(size_t current)
		{
			return (current + 1) % Size;
		}
		T ring_[Size];
		std::atomic<size_t> head_, tail_;
};

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
		// testing variables (delete eventually)
		double prevtime;
		int channelSim;
		
		// inputs, states, related constants
		QTimer *timer = new QTimer(this);
		double dt;
		double refreshRate;
		double systime;
		long long count; // keep track of plug-in time
		QString note;
		
		// data handling
		double numChannels;
		int spkcount;
		struct spikeData {
			double channelNum;
			double spktime;
			// TO-DO: spike waveform (vector?)
		};
		ringbuffer<spikeData, 10000> meaBuffer;
		spikeData spike;
		spikeData currentSpike;
		
		// raster plot variables
		int displayTime = 600; // (s) change this to set the raster display window
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
