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

class TimeScaleDraw : public QwtScaleDraw
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
		QTimer *timer0 = new QTimer(this);
        QTimer *timer1 = new QTimer(this);
		double refreshRate;
		double systime;
        double dt;
		long long count; // keep track of plug-in time
		QString note;
        double thresh;
        double min_int;
        double samplingFrequency = 20000; // TO-DO: better to get this from control panel
        double spikeDetectWindow = 50e-3;
		
		// data handling
		static const int numChannels = 60;
        static const int vmBufferSize = 2000;
		int numVoltageReads;
		std::array<ringbuffer<double, vmBufferSize>, numChannels> vm;
		QwtArray<double> last_spike_time;
		QwtArray<int> state;
		int spkcount;
		struct spikeData {
			double spktime;
			double channelNum;
			double currentThresh;
			QwtArray<double> wave;
		};
		ringbuffer<spikeData, 10000> meaBuffer;
		spikeData spike;
		spikeData currentSpike;
        
        // spike detector variables
        QVector<int> initialSamplesToSkip;
		QVector<bool> regularDetect;
		QVector<double> spikeDetectionBuffer;
		ulong bufferOffset;
		QVector<double*> detectionCarryOverBuffer; // TO-DO: maybe change this to a vector of vectors
		int carryOverLength;
		int numPre;
		int numPost;
		double maxSpikeWidth;
		double minSpikeWidth;
        int downsample;
        QVector<QVector<double>> RMSList;
        QVector<double> channelThresh;
        double maxSpikeAmp;
        double minSpikeSlope;
		int deadTime;
        double currentThreshold;
        QVector<double> threshold;
        int numUpdatesForTrain = 200; // TO-DO: this needs to be 10/spikeDetectWindow
        QVector<int> numUpdates;
        double vm_temp;
		QVector<bool> inASpike; // true when the waveform is over or under the current detection threshold for a given channel
		QVector<bool> waitToComeDown;
		int threshPolarity;
		QVector<int> enterSpikeIndex;
		QVector<int> exitSpikeIndex;
		bool posCross; // polarity of inital threshold crossing
		int spikeWidth;
		int spikeMaxIndex;
		double spikeMax;
		QwtArray<double> waveform;
		bool goodSpike;
        double VOLTAGE_EPSILON = 0.1e-6; // 0.1 uV
        
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
        void detectSpikes(void);
        void updateThreshold(int);
        void calcThreshForOneBlock(int);
		bool withinThreshold(double, double, int);
		bool findSpikePolarityBySlopeOfCrossing(int);
		int findMaxDeflection(int, int);
		void createWaveform(int);
		bool checkSpike(void);
		double getSpikeSlope(QVector<double>);
};
